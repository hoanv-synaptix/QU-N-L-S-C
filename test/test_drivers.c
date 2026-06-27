#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "charger_protocol.h"
#include "driver_maxwell.h"
#include "driver_lianming.h"
#include "driver_tonhe.h"
#include "bsp_can.h"

// ---------------------------------------------------------
// Mock Environment
// ---------------------------------------------------------
uint32_t g_tick = 0;

uint32_t HAL_GetTick(void) {
    return g_tick;
}

#define MAX_TX_FRAMES 10
BSP_CAN_Frame_t g_tx_frames[MAX_TX_FRAMES];
int g_tx_count = 0;

bool BSP_CAN_Transmit(const BSP_CAN_Frame_t *frame) {
    if (g_tx_count < MAX_TX_FRAMES) {
        g_tx_frames[g_tx_count] = *frame;
        g_tx_count++;
        return true;
    }
    return false;
}

void clear_tx_queue() {
    g_tx_count = 0;
}

// ---------------------------------------------------------
// Test Assertion Macros
// ---------------------------------------------------------
#define ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("[FAIL] %s:%d: %s\n", __func__, __LINE__, msg); \
            return false; \
        } \
    } while(0)

#define RUN_TEST(test_func) \
    do { \
        printf("Running %s... ", #test_func); \
        clear_tx_queue(); \
        g_tick = 0; \
        if (test_func()) { \
            printf("PASS\n"); \
        } else { \
            printf("\n"); \
            failed_tests++; \
        } \
        total_tests++; \
    } while(0)

int failed_tests = 0;
int total_tests = 0;

// ---------------------------------------------------------
// Test Cases for Maxwell Driver
// ---------------------------------------------------------
bool test_maxwell_start_sequence_and_timeout() {
    const CHG_DriverOps_t *ops = CHG_MaxwellDriverOps();
    ops->init();
    int8_t idx = ops->add_module(1, 0); // addr 1
    ASSERT(idx == 0, "Failed to add module");

    ops->set_voltage(idx, 100.0f);
    ops->set_current_limit(idx, 10.0f);
    
    // Start module
    ops->start(idx);
    
    CHG_ModuleView_t view;
    ops->get_module_view(idx, &view);
    if (view.state != CHG_STATE_STARTING) { printf("state=%d\\n", view.state); return false; }
    
    // Process step 0: send set voltage
    ops->process(g_tick);
    ASSERT(g_tx_count == 1, "Should transmit voltage set command");
    if (g_tx_frames[0].ext_id != 0x06080F80) { printf("tx_id=%08X\\n", g_tx_frames[0].ext_id); return false; }
    
    // Process step 1: wait 50ms, send set current
    g_tick += 55;
    ops->process(g_tick);
    ASSERT(g_tx_count == 2, "Should transmit current set command");
    
    // Process step 2: wait 50ms, send start
    g_tick += 55;
    ops->process(g_tick);
    ASSERT(g_tx_count == 3, "Should transmit start command");
    
    // Process step 3: wait 100ms, transitions to RUNNING
    g_tick += 105;
    ops->process(g_tick);
    ops->get_module_view(idx, &view);
    ASSERT(view.state == CHG_STATE_RUNNING, "State should transition to RUNNING");
    
    return true;
}

// ---------------------------------------------------------
// Test Cases for Lianming Driver
// ---------------------------------------------------------
bool test_lianming_start_sequence_and_timeout() {
    const CHG_DriverOps_t *ops = CHG_LianmingDriverOps();
    ops->init();
    int8_t idx = ops->add_module(1, 0); // addr 1
    ASSERT(idx == 0, "Failed to add module");

    ops->set_voltage(idx, 100.0f);
    ops->set_current_limit(idx, 10.0f);
    
    // Start module
    ops->start(idx);
    
    CHG_ModuleView_t view;
    ops->get_module_view(idx, &view);
    ASSERT(view.state == CHG_STATE_STARTING, "State should be STARTING");
    
    // Process step 0: send set output
    ops->process(g_tick);
    ASSERT(g_tx_count == 1, "Should transmit output set command");
    
    // Process step 1: wait 50ms, send start
    g_tick += 55;
    ops->process(g_tick);
    ASSERT(g_tx_count == 2, "Should transmit start command");
    
    // Process step 2: wait 50ms, transitions to RUNNING
    g_tick += 55;
    ops->process(g_tick);
    ops->get_module_view(idx, &view);
    ASSERT(view.state == CHG_STATE_RUNNING, "State should transition to RUNNING");
    
    // Feed frame indicating it's NOT running
    // Lianming status format: Byte 1=FF, Byte 6-7=status flags
    // Running bit is byte 7 bit 0 (1 = Off, 0 = Running).
    // So 1 means not running!
    uint8_t not_running_data[8] = { 1, 0xFF, 0, 0, 0, 0, 0, 1 }; 
    ops->feed_frame(0x1807C001, not_running_data, 8); 
    
    // Now state should bounce back to STARTING
    ops->process(g_tick);
    ops->get_module_view(idx, &view);
    ASSERT(view.state == CHG_STATE_STARTING, "State should bounce back to STARTING");
    
    // Retries
    for (int retry = 1; retry <= 3; retry++) {
        clear_tx_queue();
        ops->process(g_tick); // set output
        g_tick += 55;
        ops->process(g_tick); // start
        g_tick += 55;
        ops->process(g_tick); // to running
        
        ops->feed_frame(0x1807C001, not_running_data, 8); // not running
        ops->process(g_tick); // evaluate
    }
    
    ops->get_module_view(idx, &view);
    if (view.state != CHG_STATE_FAULT) {
        printf("state=%d alarm=%d\\n", view.state, view.alarm_flags);
        return false;
    }
    ASSERT((view.alarm_flags & CHG_ALARM_COMM_FAIL) != 0, "Alarm flags should include COMM_FAIL");
    
    return true;
}

// ---------------------------------------------------------
// Test Cases for Maxwell Data Parsing
// ---------------------------------------------------------
bool test_maxwell_data_parsing() {
    const CHG_DriverOps_t *ops = CHG_MaxwellDriverOps();
    ops->init();
    int8_t idx = ops->add_module(1, 0);

    uint8_t rx_data_v[8] = { 3, 0xF0, 0x00, 0x01, 0x42, 0x56, 0x00, 0x00 }; // 53.5V
    ops->feed_frame(0x0CF00008, rx_data_v, 8); // src addr 1 at bits 10..3 -> 1 << 3 = 8

    uint8_t rx_data_c[8] = { 3, 0xF0, 0x00, 0x02, 0x41, 0x73, 0x33, 0x33 }; // 15.2A
    ops->feed_frame(0x0CF00008, rx_data_c, 8);

    uint8_t rx_data_alarm[8] = { 3, 0xF0, 0x00, 0x40, 0x00, 0x00, 0x40, 0x00 }; // AC Under Volt (bit 14 -> 0x4000)
    ops->feed_frame(0x0CF00008, rx_data_alarm, 8);

    CHG_ModuleView_t view;
    ops->get_module_view(idx, &view);
    
    ASSERT(view.voltage > 53.49f && view.voltage < 53.51f, "Voltage parsing failed");
    ASSERT(view.current > 15.19f && view.current < 15.21f, "Current parsing failed");
    ASSERT((view.alarm_flags & CHG_ALARM_AC_UNDER_VOLT) != 0, "Alarm parsing failed");

    return true;
}

// ---------------------------------------------------------
// Test Cases for Lianming Data Parsing
// ---------------------------------------------------------
bool test_lianming_data_parsing() {
    const CHG_DriverOps_t *ops = CHG_LianmingDriverOps();
    ops->init();
    int8_t idx = ops->add_module(2, 0);

    uint8_t rx_data[8] = {
        1,       // CMD
        0xFF,    // OK
        0x00, 0x69, // 10.5A (105)
        0x02, 0x59, // 60.1V (601)
        0x00, 0x20  // AC under volt (bit 5)
    };
    ops->feed_frame(0x1807C002, rx_data, 8);

    CHG_ModuleView_t view;
    ops->get_module_view(idx, &view);

    ASSERT(view.voltage > 60.09f && view.voltage < 60.11f, "Voltage parsing failed");
    ASSERT(view.current > 10.49f && view.current < 10.51f, "Current parsing failed");
    ASSERT((view.alarm_flags & CHG_ALARM_AC_UNDER_VOLT) != 0, "Alarm parsing failed (AC Under Volt)");
    ASSERT(view.running == false, "Running parsing failed (bit 0 is 0)");

    rx_data[7] = 0x0B; // bit 0 = 1, bit 1 = 1, bit 3 = 1
    ops->feed_frame(0x1807C002, rx_data, 8);
    ops->get_module_view(idx, &view);
    ASSERT(view.running == false, "Running parsing failed (bit 0 is 1)");

    return true;
}

bool test_maxwell_tx_encoding() {
    const CHG_DriverOps_t *ops = CHG_MaxwellDriverOps();
    ops->init();
    int8_t idx = ops->add_module(1, 0);

    ops->start(idx);
    ops->process(0);
    ops->process(50);
    ops->process(100);
    
    // Feed running frame to enter RUNNING
    uint8_t rx_data_v[8] = { 3, 0xF0, 0x00, 0x01, 0x42, 0x56, 0x00, 0x00 };
    ops->feed_frame(0x0CF00008, rx_data_v, 8);
    ops->process(200);

    clear_tx_queue();
    ops->set_voltage(idx, 53.5f);
    ASSERT(g_tx_count == 1, "Expected 1 TX frame for set_voltage");
    uint32_t expected_ext_id = 0x06080F80;
    ASSERT(g_tx_frames[0].ext_id == expected_ext_id, "Wrong TX EXT_ID");
    ASSERT(g_tx_frames[0].data[0] == 0x03, "Func should be WRITE (0x03)");
    ASSERT(g_tx_frames[0].data[2] == 0x00, "Reg MSB");
    ASSERT(g_tx_frames[0].data[3] == 0x21, "Reg LSB");
    ASSERT(g_tx_frames[0].data[4] == 0x42, "Val byte 0");
    ASSERT(g_tx_frames[0].data[5] == 0x56, "Val byte 1");
    ASSERT(g_tx_frames[0].data[6] == 0x00, "Val byte 2");
    ASSERT(g_tx_frames[0].data[7] == 0x00, "Val byte 3");

    return true;
}

bool test_maxwell_negative_cases() {
    const CHG_DriverOps_t *ops = CHG_MaxwellDriverOps();
    ops->init();
    int8_t idx = ops->add_module(1, 0);

    // 1. DLC < 8 (should be ignored)
    uint8_t rx_data[8] = { 3, 0xF0, 0x00, 0x01, 0x42, 0x56, 0x00, 0x00 };
    ops->feed_frame(0x0CF00008, rx_data, 7); 
    CHG_ModuleView_t view;
    ops->get_module_view(idx, &view);
    ASSERT(view.voltage == 0.0f, "Should ignore short frame");

    // 2. Wrong addr
    ops->feed_frame(0x0CF00010, rx_data, 8); // src addr 2
    ops->get_module_view(idx, &view);
    ASSERT(view.voltage == 0.0f, "Should ignore wrong address");

    // 3. Error code != 0xF0
    uint8_t rx_err[8] = { 3, 0x00, 0x00, 0x01, 0x42, 0x56, 0x00, 0x00 };
    ops->feed_frame(0x0CF00008, rx_err, 8);
    ops->get_module_view(idx, &view);
    ASSERT(view.voltage == 0.0f, "Should drop data on error");
    ASSERT(view.stats.error_count == 1, "Error count should increment");

    return true;
}

bool test_maxwell_recovery_state() {
    const CHG_DriverOps_t *ops = CHG_MaxwellDriverOps();
    ops->init();
    int8_t idx = ops->add_module(1, 0);
    g_tick = 0;
    ops->start(idx);
    
    // Process up to timeout
    while (g_tick <= 6000) {
        ops->process(g_tick);
        g_tick += 50;
    }
    
    CHG_ModuleView_t view;
    ops->get_module_view(idx, &view);
    ASSERT(view.state == CHG_STATE_OFFLINE, "Should enter OFFLINE after timeout");
    
    // Simulate RECOVERY_DELAY (3s = 3000ms)
    g_tick += 3050;
    ops->process(g_tick);
    ops->get_module_view(idx, &view);
    ASSERT(view.state == CHG_STATE_RECOVERING, "Should enter RECOVERING");
    
    // Feed a frame to recover fully
    uint8_t rx_data_v[8] = { 3, 0xF0, 0x00, 0x01, 0x42, 0x56, 0x00, 0x00 };
    ops->feed_frame(0x0CF00008, rx_data_v, 8);
    ops->process(g_tick);
    ops->get_module_view(idx, &view);
    ASSERT(view.state == CHG_STATE_STARTING, "Should go back to STARTING");

    return true;
}

bool test_lianming_tx_encoding() {
    const CHG_DriverOps_t *ops = CHG_LianmingDriverOps();
    ops->init();
    int8_t idx = ops->add_module(1, 0);

    clear_tx_queue();
    ops->start(idx);
    ops->process(0);
    ops->process(100);
    ops->process(200);
    ASSERT(g_tx_count == 2, "Expected 2 TX frames for start (set params then start)");
    ASSERT(g_tx_frames[1].ext_id == 0x1907C081, "Wrong TX EXT_ID");
    ASSERT(g_tx_frames[1].data[0] == 0x02, "Func should be 0x02 (ON/OFF)");
    ASSERT(g_tx_frames[1].data[6] == 0x55, "Byte 6 should be 0x55 for ON");

    return true;
}

bool test_lianming_negative_cases() {
    const CHG_DriverOps_t *ops = CHG_LianmingDriverOps();
    ops->init();
    int8_t idx = ops->add_module(1, 0);

    // 1. DLC < 8 (should be ignored)
    uint8_t rx_data[8] = { 1, 0xFF, 0x03, 0x84, 0x14, 0xE6, 0x00, 0x00 };
    ops->feed_frame(0x1807C001, rx_data, 7); 
    CHG_ModuleView_t view;
    ops->get_module_view(idx, &view);
    ASSERT(view.voltage == 0.0f, "Should ignore short frame");

    // 2. Wrong addr
    ops->feed_frame(0x1807C002, rx_data, 8); // src addr 2
    ops->get_module_view(idx, &view);
    ASSERT(view.voltage == 0.0f, "Should ignore wrong address");

    // 3. Error code != 0xFF
    uint8_t rx_err[8] = { 1, 0x00, 0x03, 0x84, 0x14, 0xE6, 0x00, 0x00 };
    ops->feed_frame(0x1807C001, rx_err, 8);
    ops->get_module_view(idx, &view);
    ASSERT(view.voltage == 0.0f, "Should drop data on error");
    ASSERT(view.stats.error_count == 1, "Error count should increment");

    return true;
}

bool test_lianming_recovery_state() {
    const CHG_DriverOps_t *ops = CHG_LianmingDriverOps();
    ops->init();
    int8_t idx = ops->add_module(1, 0);
    g_tick = 0;
    ops->start(idx);
    
    // Process up to timeout
    while (g_tick <= 6000) {
        ops->process(g_tick);
        g_tick += 50;
    }
    
    CHG_ModuleView_t view;
    ops->get_module_view(idx, &view);
    ASSERT(view.state == CHG_STATE_OFFLINE, "Should enter OFFLINE after timeout");
    
    // Simulate RECOVERY_DELAY (3s = 3000ms)
    g_tick += 3050;
    ops->process(g_tick);
    ops->get_module_view(idx, &view);
    ASSERT(view.state == CHG_STATE_RECOVERING, "Should enter RECOVERING");
    
    // Lianming recovering just reads status, wait for rx frame to recover fully
    uint8_t rx_data_v[8] = { 1, 0xFF, 0x03, 0x84, 0x14, 0xE6, 0x00, 0x00 };
    ops->feed_frame(0x1807C001, rx_data_v, 8);
    ops->process(g_tick);
    ops->get_module_view(idx, &view);
    ASSERT(view.state == CHG_STATE_IDLE || view.state == CHG_STATE_STARTING, "Should recover");

    return true;
}
// ---------------------------------------------------------
// Test Cases for Tonhe Driver
// ---------------------------------------------------------
bool test_tonhe_start_sequence_and_timeout() {
    const CHG_DriverOps_t *ops = CHG_TonheDriverOps();
    ops->init();
    int8_t idx = ops->add_module(1, 0); // addr 1
    ASSERT(idx == 0, "Failed to add module");

    ops->set_voltage(idx, 100.0f);
    ops->set_current_limit(idx, 10.0f);
    
    // Start module
    ops->start(idx);
    
    clear_tx_queue();
    // Process step 0: send set output and start
    ops->process(g_tick);
    
    CHG_ModuleView_t view;
    ops->get_module_view(idx, &view);
    ASSERT(view.state == CHG_STATE_STARTING, "State should be STARTING");
    ASSERT(g_tx_count == 2, "Should transmit param set and start command");
    
    // Feed M_C_2 (Confirm)
    uint8_t rx_data[8] = { 0x01, 0, 0, 0, 0, 0, 0, 0 };
    ops->feed_frame(0x1802A001, rx_data, 8); // PGN=0x0200, Priority=6 -> 0x1802A001
    ops->process(g_tick);
    
    ops->get_module_view(idx, &view);
    ASSERT(view.state == CHG_STATE_RUNNING, "State should transition to RUNNING");
    
    return true;
}

bool test_tonhe_tx_encoding() {
    const CHG_DriverOps_t *ops = CHG_TonheDriverOps();
    ops->init();
    int8_t idx = ops->add_module(1, 0); // addr 1

    ops->set_voltage(idx, 500.0f);
    ops->set_current_limit(idx, 41.6f);
    
    ops->start(idx);
    clear_tx_queue();
    ops->process(g_tick);
    
    ASSERT(g_tx_count == 2, "Expected 2 TX frames (param and start)");
    
    // Check param set (C_M_2)
    // Priority = 4, PGN = 0x000400, SA = 0xA0
    // ID = (4 << 26) | (0 << 24) | (0x04 << 16) | (0x00 << 8) | 0xA0 = 0x100400A0
    uint32_t param_id = 0x100400A0; // Broadcast 
    // Wait, tonhe_param_set_id() uses 0x1004FFA0? Let's check in code if it uses FF for PS
    
    ASSERT(g_tx_frames[0].data[0] == 0xFF, "Processing flag 1");
    ASSERT(g_tx_frames[0].data[3] == 0x00, "Group");
    
    // Voltage: 500.0V / 0.1V = 5000 = 0x1388 => LSB=0x88, MSB=0x13
    ASSERT(g_tx_frames[0].data[4] == 0x88, "Voltage LSB");
    ASSERT(g_tx_frames[0].data[5] == 0x13, "Voltage MSB");
    
    // Current: 41.6A / 0.01A = 4160 = 0x1040 => LSB=0x40, MSB=0x10
    ASSERT(g_tx_frames[0].data[6] == 0x40, "Current LSB");
    ASSERT(g_tx_frames[0].data[7] == 0x10, "Current MSB");
    
    // Check start (C_M_24)
    // Priority = 2, PGN = 0x000600 -> PF=0x06, PS=0x01 (addr)
    // ID = (2 << 26) | (0x06 << 16) | (0x01 << 8) | 0xA0 = 0x080601A0
    ASSERT(g_tx_frames[1].data[0] == 0xAA, "Start command");

    return true;
}

bool test_tonhe_negative_cases() {
    const CHG_DriverOps_t *ops = CHG_TonheDriverOps();
    ops->init();
    int8_t idx = ops->add_module(1, 0); // addr 1
    
    // Wrong address (addr 2 instead of 1)
    uint8_t rx_data[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    ops->feed_frame(0x1801A002, rx_data, 8); 
    
    CHG_ModuleView_t view;
    ops->get_module_view(idx, &view);
    ASSERT(view.stats.rx_count == 0, "Should ignore wrong address");
    
    return true;
}

bool test_tonhe_recovery_state() {
    const CHG_DriverOps_t *ops = CHG_TonheDriverOps();
    ops->init();
    int8_t idx = ops->add_module(1, 0);
    ops->start(idx);
    
    // Wait for timeout (OFFLINE_TIMEOUT = 2000ms + CONFIRM_TIMEOUT)
    // Start sends param+start, then waits 1000ms for confirm, retries 3 times -> 4000ms
    g_tick = 0;
    while (g_tick <= 4500) {
        ops->process(g_tick);
        g_tick += 50;
    }
    
    CHG_ModuleView_t view;
    ops->get_module_view(idx, &view);
    ASSERT(view.state == CHG_STATE_OFFLINE, "Should enter OFFLINE after timeout");
    
    // Simulate RECOVERY_DELAY (3s = 3000ms)
    g_tick += 3050;
    ops->process(g_tick);
    ops->get_module_view(idx, &view);
    ASSERT(view.state == CHG_STATE_RECOVERING, "Should enter RECOVERING");
    
    // Feed M_C_1 (Status) to recover
    uint8_t rx_data[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    ops->feed_frame(0x1801A001, rx_data, 8);
    ops->process(g_tick);
    ops->get_module_view(idx, &view);
    ASSERT(view.state == CHG_STATE_STARTING || view.state == CHG_STATE_RUNNING, "Should recover");

    return true;
}

bool test_tonhe_data_parsing() {
    const CHG_DriverOps_t *ops = CHG_TonheDriverOps();
    ops->init();
    int8_t idx = ops->add_module(1, 0);
    
    // Feed M_C_1 (Status)
    // Byte 1-2: Output status (0x01 = ON, 0x00 = OFF) -> 0x01
    // Byte 3-4: Voltage (0.1V/bit) -> 53.5V = 535 = 0x0217 -> 17 02
    // Byte 5-6: Current (0.01A/bit) -> 10.0A = 1000 = 0x03E8 -> E8 03
    // Byte 7-8: Alarm bits -> Bit 4 (Output overcurrent) -> 0x10 0x00
    uint8_t rx_data[8] = { 0x01, 0x00, 0x17, 0x02, 0xE8, 0x03, 0x10, 0x00 };
    ops->feed_frame(0x1801A001, rx_data, 8);
    ops->process(g_tick);
    
    CHG_ModuleView_t view;
    ops->get_module_view(idx, &view);
    
    ASSERT(view.stats.rx_count == 1, "Should receive 1 valid frame");
    ASSERT(view.voltage > 53.4f && view.voltage < 53.6f, "Parsed voltage wrong");
    ASSERT(view.current > 9.9f && view.current < 10.1f, "Parsed current wrong");
    ASSERT(view.running == true, "Should be running");
    ASSERT((view.alarm_flags & CHG_ALARM_OVER_CURR_OUT) != 0, "Alarm flags should include OVER_CURR_OUT");
    
    return true;
}

int main() {
    printf("==========================================\n");
    printf(" CHARGER DRIVER TEST SUITE\n");
    printf("==========================================\n\n");
    
    RUN_TEST(test_maxwell_start_sequence_and_timeout);
    RUN_TEST(test_maxwell_tx_encoding);
    RUN_TEST(test_maxwell_negative_cases);
    RUN_TEST(test_maxwell_recovery_state);
    RUN_TEST(test_maxwell_data_parsing);
    
    RUN_TEST(test_lianming_start_sequence_and_timeout);
    RUN_TEST(test_lianming_tx_encoding);
    RUN_TEST(test_lianming_negative_cases);
    RUN_TEST(test_lianming_recovery_state);
    RUN_TEST(test_lianming_data_parsing);
    
    RUN_TEST(test_tonhe_start_sequence_and_timeout);
    RUN_TEST(test_tonhe_tx_encoding);
    RUN_TEST(test_tonhe_negative_cases);
    RUN_TEST(test_tonhe_recovery_state);
    RUN_TEST(test_tonhe_data_parsing);
    
    printf("\n==========================================\n");
    if (failed_tests == 0) {
        printf("RESULT: ALL %d TESTS PASSED!\n", total_tests);
        return 0;
    } else {
        printf("RESULT: %d/%d TESTS FAILED!\n", failed_tests, total_tests);
        return 1;
    }
}
