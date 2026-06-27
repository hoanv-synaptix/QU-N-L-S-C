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
    CHG_ALARM_OVER_CURR_OUT    = (1 << 6), /* Quá dòng đầu ra */
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
    CHG_AlarmFlag_t  alarm_flags;  /* Standardized flags (for Firmware logic) */
    uint32_t         input_power;
    uint32_t         last_rx_tick;
    uint32_t         last_tx_tick;
 CHG_CommStats_t stats;
} CHG_ModuleView_t;

typedef struct {
    float    total_current;
    float    total_power_in;
    float    voltage;
    uint8_t  modules_online;
    uint8_t  modules_fault;
    bool     any_critical;
} CHG_SystemSummary_t;

typedef struct {
    const char *name;
    void    (*init)(void);
    int8_t  (*add_module)(uint8_t addr, uint8_t group);
    void    (*remove_module)(uint8_t idx);
    bool    (*set_voltage)(uint8_t idx, float voltage_v);
    bool    (*set_current_limit)(uint8_t idx, float ratio);
    bool    (*start)(uint8_t idx);
    bool    (*stop)(uint8_t idx);
    void    (*set_voltage_all)(float voltage_v);
    void    (*set_current_limit_all)(float ratio);
    void    (*start_all)(void);
    void    (*stop_all)(void);
    void    (*emergency_stop)(void);
    void    (*process)(uint32_t now_tick);
    void    (*feed_frame)(uint32_t ext_id, const uint8_t *data, uint8_t dlc);
    void    (*get_system_summary)(CHG_SystemSummary_t *summary);
    uint8_t (*get_module_count)(void);
    bool    (*get_module_view)(uint8_t idx, CHG_ModuleView_t *view);
} CHG_DriverOps_t;

bool CHG_RegisterDriver(CHG_DriverId_t id, const CHG_DriverOps_t *ops);
bool CHG_SelectDriver(CHG_DriverId_t id);
bool CHG_SelectDriverByName(const char *name);
bool CHG_IsDriverRegistered(CHG_DriverId_t id);
CHG_DriverId_t CHG_GetActiveDriverId(void);
const char *CHG_GetActiveDriverName(void);

void CHG_Init(void);
int8_t CHG_AddModule(uint8_t addr, uint8_t group);
void CHG_RemoveModule(uint8_t idx);
bool CHG_SetVoltage(uint8_t idx, float voltage_v);
bool CHG_SetCurrentLimit(uint8_t idx, float ratio);
bool CHG_Start(uint8_t idx);
bool CHG_Stop(uint8_t idx);
void CHG_SetVoltageAll(float voltage_v);
void CHG_SetCurrentLimitAll(float ratio);
void CHG_StartAll(void);
void CHG_StopAll(void);
void CHG_EmergencyStop(void);
void CHG_Process(uint32_t now_tick);
void CHG_FeedCanFrame(uint32_t ext_id, const uint8_t *data, uint8_t dlc);
void CHG_GetSystemSummary(CHG_SystemSummary_t *summary);
uint8_t CHG_GetModuleCount(void);
bool CHG_GetModuleView(uint8_t idx, CHG_ModuleView_t *view);

#endif /* CHARGER_CORE_H */
