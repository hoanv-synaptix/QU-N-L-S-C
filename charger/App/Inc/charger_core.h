/**
 * @file charger_core.h
 * @brief Charger Core - Abstract Interface and Data Types
 * @note This module provides the abstract interface for all charger drivers.
 *
 * Architecture:
 *   ┌─────────────────────────────────────────┐
 *   │           app_charger.c                 │
 *   │      (Upper Application Layer)          │
 *   └─────────────────┬───────────────────────┘
 *                     │ uses
 *                     ↓
 *   ┌─────────────────────────────────────────┐
 *   │            charger_core.c/h               │
 *   │      (Abstract Interface Layer)           │
 *   │                                         │
 *   │  - CHG_DriverOps_t (interface)         │
 *   │  - CHG_ModuleView_t (data model)       │
 *   │  - Driver Registry (Select driver)     │
 *   │  - API Wrappers (CHG_SetVoltage...)    │
 *   └─────────────────┬───────────────────────┘
 *                     │ implements
 *     ┌───────────────┼───────────────┐
 *     ↓               ↓               ↓
 * ┌─────────┐   ┌─────────┐   ┌─────────┐
 * │ Maxwel  │   │ Lianming│   │ TonHe   │
 * └─────────┘   └─────────┘   └─────────┘
 *
 * Usage:
 *   1. Register drivers: CHG_RegisterDriver(DRIVER_ID, DRIVER_OPS);
 *   2. Select active driver: CHG_SelectDriver(DRIVER_ID);
 *   3. Initialize: CHG_Init();
 *   4. Add module(s): CHG_AddModule(addr, group);
 *   5. Control: CHG_SetVoltage(), CHG_Start(), etc.
 *   6. Process: Call CHG_Process() periodically
 *   7. Feed CAN frames: CHG_FeedCanFrame() from ISR
 *
 * Design Patterns:
 *   - Abstract Factory: Creates driver instances via ops table
 *   - Strategy Pattern: Swappable driver implementations
 *   - Proxy:charger_core delegates to active driver
 */

#ifndef CHARGER_CORE_H
#define CHARGER_CORE_H

#include <stdbool.h>
#include <stdint.h>

#define CHG_MAX_DRIVER_ID   8

typedef enum {
    CHG_DRIVER_NONE     = 0,
    CHG_DRIVER_MAXWELL  = 1,
    CHG_DRIVER_LIANMING = 2,
    CHG_DRIVER_TONHE    = 3,
} CHG_DriverId_t;

typedef enum {
    CHG_STATE_IDLE = 0,
    CHG_STATE_STARTING,
    CHG_STATE_RUNNING,
    CHG_STATE_OFFLINE,
    CHG_STATE_FAULT,
    CHG_STATE_RECOVERING,
} CHG_ModuleState_t;

typedef enum {
    CHG_ALARM_NONE             = 0x0000,
    CHG_ALARM_HW_FAULT         = (1 << 0), /* Lỗi phần cứng chung */
    CHG_ALARM_COMM_FAIL        = (1 << 1), /* Mất kết nối giao tiếp */
    CHG_ALARM_OVER_TEMP        = (1 << 2), /* Quá nhiệt */
    CHG_ALARM_OVER_VOLTAGE_OUT = (1 << 3), /* Quá áp đầu ra */
    CHG_ALARM_SHORT_CIRCUIT    = (1 << 4), /* Ngắn mạch */
    CHG_ALARM_AC_UNDER_VOLT    = (1 << 5), /* Áp đầu vào thấp */
} CHG_AlarmFlag_t;

/** Thống kê truyền thông cho 1 module (chuẩn hoá) */
typedef struct {
 uint32_t tx_count; /* Số frame đã gửi */
 uint32_t rx_count; /* Số response nhận được (OK) */
 uint32_t error_count;  /* Số response lỗi (NACK / parse fail) */
 uint32_t timeout_count; /* Số lần timeout giao tiếp */
 uint32_t recovery_count; /* Số lần recovery thành công */
} CHG_CommStats_t;

typedef struct {
    uint8_t          addr;
    uint8_t          group;
    bool             enabled;
    bool             online;
    bool             running;
    CHG_ModuleState_t state;
    float            voltage;
    float            current;
    float            current_limit;
    float            temp_dcdc;
    float            temp_ambient;
    uint32_t         alarm_status; /* Raw alarm bits (for PC/Telemetry) */
