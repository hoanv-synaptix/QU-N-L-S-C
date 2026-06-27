#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "charger_protocol.h"
#include "driver_maxwell.h"
#include "driver_lianming.h"
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
    
    printf("\n==========================================\n");
    if (failed_tests == 0) {
        printf("RESULT: ALL %d TESTS PASSED!\n", total_tests);
        return 0;
    } else {
        printf("RESULT: %d/%d TESTS FAILED!\n", failed_tests, total_tests);
        return 1;
    }
}
