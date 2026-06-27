#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "bms_core.h"
#include "bsp_can.h"
#include "stm32f4xx_hal.h"

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

CAN_HandleTypeDef hcan1;
CAN_HandleTypeDef hcan2;
UART_HandleTypeDef huart1;

int HAL_CAN_AddTxMessage(CAN_HandleTypeDef *hcan, CAN_TxHeaderTypeDef *pHeader, uint8_t aData[], uint32_t *pTxMailbox) {
    if (g_tx_count < MAX_TX_FRAMES) {
        g_tx_frames[g_tx_count].ext_id = pHeader->ExtId;
        g_tx_frames[g_tx_count].dlc = pHeader->DLC;
        memcpy(g_tx_frames[g_tx_count].data, aData, pHeader->DLC);
        g_tx_count++;
        return HAL_OK;
    }
    return -1;
}

int HAL_UART_Transmit(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size, uint32_t Timeout) {
    return HAL_OK; // Do nothing
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

int total_tests = 0;
int failed_tests = 0;

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

// ---------------------------------------------------------
// Test Cases
// ---------------------------------------------------------

bool test_bms_init_and_offline() {
    BMS_Init();
    
    // Initially should be OFFLINE
    BMS_View_t view;
    BMS_GetView(&view);
    ASSERT(view.state == BMS_STATE_OFFLINE, "Initial state must be OFFLINE");
    ASSERT(view.online == false, "Must be offline");
    
    // Process without feeding any frames
    g_tick += 100;
    BMS_Process(g_tick);
    
    BMS_GetView(&view);
    ASSERT(view.state == BMS_STATE_OFFLINE, "Must remain OFFLINE");
    ASSERT(view.online == false, "Must remain offline");
    
    return true;
}

bool test_bms_feed_batt_st1() {
    BMS_Init();
    
    // Feed BATT_ST1 (0x02F4)
    // raw_volt = V * 10. Let's send 53.5V -> 535 = 0x0217 -> 17 02
    // raw_curr = (I + 400) * 10. Let's send 10.5A -> 410.5 -> 4105 = 0x1009 -> 09 10
    // soc = 85% = 0x55
    uint8_t rx_data[8] = { 0x17, 0x02, 0x09, 0x10, 0x55, 0x00, 0x00, 0x00 };
    BMS_FeedFrame(0, 0x02F4, rx_data, 8);
    
    BMS_Process(g_tick);
    
    BMS_View_t view;
    BMS_GetView(&view);
    
    ASSERT(view.state == BMS_STATE_ONLINE, "Must transition to ONLINE");
    ASSERT(view.online == true, "Must be online");
    ASSERT(view.batt_voltage > 53.4f && view.batt_voltage < 53.6f, "Voltage parsing failed");
    ASSERT(view.batt_current > 10.4f && view.batt_current < 10.6f, "Current parsing failed");
    ASSERT(view.soc == 85, "SOC parsing failed");
    
    return true;
}

bool test_bms_feed_cell_volt_temp() {
    BMS_Init();
    
    // Feed CELL_VOLT (0x04F4)
    // max_cell = 3200mV (0x0C80) -> 80 0C, no = 1
    // min_cell = 3100mV (0x0C1C) -> 1C 0C, no = 10
    uint8_t volt_data[8] = { 0x80, 0x0C, 0x01, 0x1C, 0x0C, 0x0A, 0x00, 0x00 };
    BMS_FeedFrame(0, 0x04F4, volt_data, 8);
    
    // Feed CELL_TEMP (0x05F4)
    // max_temp = 35C -> 35 + 50 = 85 (0x55), no = 2
    // min_temp = 30C -> 30 + 50 = 80 (0x50), no = 5
    // avg_temp = 32C -> 32 + 50 = 82 (0x52)
    uint8_t temp_data[8] = { 0x55, 0x02, 0x50, 0x05, 0x52, 0x00, 0x00, 0x00 };
    BMS_FeedFrame(0, 0x05F4, temp_data, 8);
    
    BMS_Process(g_tick);
    
    BMS_View_t view;
    BMS_GetView(&view);
    
    ASSERT(view.max_cell_volt == 3200, "Max cell volt mismatch");
    ASSERT(view.min_cell_volt == 3100, "Min cell volt mismatch");
    ASSERT(view.max_cell_temp == 35, "Max cell temp mismatch");
    ASSERT(view.min_cell_temp == 30, "Min cell temp mismatch");
    
    return true;
}

bool test_bms_feed_alm_info() {
    BMS_Init();
    
    // Feed data to go online
    uint8_t rx_data1[8] = { 0x17, 0x02, 0x09, 0x10, 0x55, 0x00, 0x00, 0x00 };
    BMS_FeedFrame(0, 0x02F4, rx_data1, 8);
    BMS_Process(g_tick);
    
    // Feed ALM_INFO (0x07F4)
    // byte0: low_pack_volt(bit0-1)=0, low_cell_volt(bit2-3)=2(fault)->0x08, high_pack(bit4-5)=0, high_cell(bit6-7)=0
    // => byte0 = 0x08
    uint8_t alm_data[8] = { 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    BMS_FeedFrame(0, 0x07F4, alm_data, 8);
    
    BMS_Process(g_tick);
    
    BMS_View_t view;
    BMS_GetView(&view);
    ASSERT(view.state == BMS_STATE_FAULT, "Must transition to FAULT on critical alarm");
    ASSERT((view.alarm_flags & BMS_ALARM_LOW_CELL_VOLT) != 0, "LOW_CELL_VOLT flag not set");
    
    // Clear alarm
    uint8_t alm_data_clear[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    BMS_FeedFrame(0, 0x07F4, alm_data_clear, 8);
    
    // Need a valid data frame to update state back to ONLINE
    uint8_t rx_data[8] = { 0x17, 0x02, 0x09, 0x10, 0x55, 0x00, 0x00, 0x00 };
    BMS_FeedFrame(0, 0x02F4, rx_data, 8);
    
    BMS_Process(g_tick);
    BMS_GetView(&view);
    
    ASSERT(view.state == BMS_STATE_ONLINE, "Must recover to ONLINE when alarms clear");
    ASSERT((view.alarm_flags & BMS_ALARM_LOW_CELL_VOLT) == 0, "LOW_CELL_VOLT flag should be cleared");
    
    return true;
}

bool test_bms_timeout_watchdog() {
    BMS_Init();
    
    // Feed data to go online
    uint8_t rx_data[8] = { 0x17, 0x02, 0x09, 0x10, 0x55, 0x00, 0x00, 0x00 };
    BMS_FeedFrame(0, 0x02F4, rx_data, 8);
    BMS_Process(g_tick);
    
    BMS_View_t view;
    BMS_GetView(&view);
    ASSERT(view.state == BMS_STATE_ONLINE, "Must be ONLINE initially");
    
    // Wait for timeout (OFFLINE_TIMEOUT = 5000ms)
    g_tick += 5500;
    BMS_Process(g_tick);
    
    BMS_GetView(&view);
    ASSERT(view.state == BMS_STATE_OFFLINE, "Must transition to OFFLINE after timeout");
    ASSERT((view.alarm_flags & BMS_ALARM_BMS_OFFLINE) != 0, "OFFLINE alarm flag must be set");
    
    return true;
}

bool test_bms_ctrl_info_tx() {
    BMS_Init();
    
    BMS_ChargeCtrl_t ctrl = {
        .allow_charge = true,
        .allow_discharge = false
    };
    
    BMS_SendCtrlInfo(&ctrl);
    
    // In BMS_Process, it transmits Ctrl_INFO periodically or immediately if flagged
    BMS_Process(g_tick);
    
    ASSERT(g_tx_count > 0, "Must transmit Ctrl_INFO");
    
    // Ctrl_INFO: ID = 0x18F0F428
    ASSERT(g_tx_frames[0].ext_id == 0x18F0F428, "Wrong TX ID");
    // byte0: charge_enable = 1
    ASSERT(g_tx_frames[0].data[0] == 0x01, "Charge enable byte wrong");
    
    return true;
}

int main() {
    printf("==========================================\n");
    printf(" BMS CORE TEST SUITE\n");
    printf("==========================================\n\n");
    
    RUN_TEST(test_bms_init_and_offline);
    RUN_TEST(test_bms_feed_batt_st1);
    RUN_TEST(test_bms_feed_cell_volt_temp);
    RUN_TEST(test_bms_feed_alm_info);
    RUN_TEST(test_bms_timeout_watchdog);
    RUN_TEST(test_bms_ctrl_info_tx);
    
    printf("\n==========================================\n");
    if (failed_tests == 0) {
        printf("RESULT: ALL %d TESTS PASSED!\n", total_tests);
        return 0;
    } else {
        printf("RESULT: %d/%d TESTS FAILED!\n", failed_tests, total_tests);
        return 1;
    }
}
