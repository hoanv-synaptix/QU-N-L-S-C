/**
 * @file    maxwell_charger.h
 * @brief   Maxwell MXR Series Charging Module Driver - Multi-module support
 * @version 2.0
 * @note    Protocol V1.50 - CAN 2.0B Extended Frame, 125Kbps
 *          Tách biệt khỏi HAL, chỉ phụ thuộc bsp_can.h
 *
 *  Thay đổi so với v1.0:
 *    - Hỗ trợ điều khiển nhiều module mắc song song (instance-based API)
 *    - Tự phát hiện module offline (timeout detection)
 *    - Cơ chế auto-recovery khi module mất liên lạc
 *    - Thống kê comm: tx_count, rx_count, error_count, retry_count
 *    - Tổng hợp dòng/công suất từ nhiều module
 */

#ifndef MAXWELL_CHARGER_H
#define MAXWELL_CHARGER_H

#include <stdint.h>
#include <stdbool.h>
#include "charger_protocol.h"
#include "charger_core.h"

/* ============== Configuration ============== */

#define MXR_MAX_MODULES         8       /* Tối đa 8 module mắc song song */
#define MXR_CAN_BAUDRATE        125000U
#define MXR_OFFLINE_TIMEOUT_MS  1500    /* Không response trong 1.5s -> offline */
#define MXR_RECOVERY_DELAY_MS   3000    /* Chờ 3s trước khi thử recovery */
#define MXR_MAX_RETRIES         3       /* Số lần retry trước khi báo offline */

/* ============== Protocol Constants ============== */
/* Defined in charger_protocol.h */

/* ============== Register Map ============== */
/* Common registers defined in charger_protocol.h as CHG_REG_... */

/* Specific registers for Maxwell */
#define MXR_REG_INPUT_VOLTAGE   0x0005
#define MXR_REG_PFC0_VOLTAGE    0x0008
#define MXR_REG_PFC1_VOLTAGE    0x000A
#define MXR_REG_AC_PHASE_A      0x000C
#define MXR_REG_AC_PHASE_B      0x000D
#define MXR_REG_AC_PHASE_C      0x000E
#define MXR_REG_TEMP_PFC        0x0010
#define MXR_REG_SET_ALTITUDE    0x0017
#define MXR_REG_SET_CURRENT_INT 0x001B
#define MXR_REG_SET_GROUP       0x001E
#define MXR_REG_SET_ADDR_MODE   0x001F
#define MXR_REG_OVP_RESET       0x0031
#define MXR_REG_ALTITUDE_RD     0x004A

/* ============== Alarm Status Bits ============== */

#define MXR_ALARM_MODULE_FAULT      (1U << 0)
#define MXR_ALARM_MODULE_PROTECT    (1U << 1)
#define MXR_ALARM_INPUT_ERROR       (1U << 4)
#define MXR_ALARM_SCI_FAILURE       (1U << 6)
#define MXR_ALARM_DCDC_OV           (1U << 8)
#define MXR_ALARM_PFC_ABNORMAL      (1U << 9)
#define MXR_ALARM_AC_UNDERVOLTAGE   (1U << 14)
#define MXR_ALARM_CAN_FAILURE       (1U << 16)
#define MXR_ALARM_CURR_IMBALANCE    (1U << 17)
#define MXR_ALARM_DCDC_OFF          (1U << 22)
#define MXR_ALARM_POWER_LIMIT       (1U << 23)
#define MXR_ALARM_TEMP_DERATING     (1U << 24)
#define MXR_ALARM_AC_POWER_LIMIT    (1U << 25)
#define MXR_ALARM_FAN_FAILURE       (1U << 27)
#define MXR_ALARM_SHORT_CIRCUIT     (1U << 28)
#define MXR_ALARM_DCDC_OVERTEMP     (1U << 30)
#define MXR_ALARM_DCDC_OUTPUT_OV    (1U << 31)

/* Alarm nghiêm trọng: cần emergency stop */
#define MXR_ALARM_CRITICAL_MASK     (MXR_ALARM_MODULE_FAULT   | \
                                     MXR_ALARM_DCDC_OV        | \
                                     MXR_ALARM_SHORT_CIRCUIT  | \
                                     MXR_ALARM_DCDC_OVERTEMP  | \
                                     MXR_ALARM_DCDC_OUTPUT_OV | \
                                     MXR_ALARM_SCI_FAILURE)

/* ============== Types ============== */

/** Trạng thái hoạt động của module */
typedef enum {
    MXR_STATE_IDLE = 0,         /* Chưa khởi tạo / chưa start */
    MXR_STATE_STARTING,         /* Đã gửi start, đợi confirm */
    MXR_STATE_RUNNING,          /* Đang sạc bình thường */
    MXR_STATE_OFFLINE,          /* Mất liên lạc (timeout) */
    MXR_STATE_FAULT,            /* Có alarm nghiêm trọng */
    MXR_STATE_RECOVERING,       /* Đang thử khôi phục */
} MXR_State_t;

/** Thống kê truyền thông cho 1 module */
typedef struct {
    uint32_t tx_count;          /* Số frame đã gửi */
    uint32_t rx_count;          /* Số response nhận được */
    uint32_t error_count;       /* Số lần response lỗi (NACK hoặc parse fail) */
    uint32_t timeout_count;     /* Số lần timeout */
    uint32_t recovery_count;    /* Số lần recovery thành công */
} MXR_CommStats_t;

/** Cài đặt mong muốn cho module (setpoint) */
typedef struct {
    float    voltage_v;         /* Điện áp mong muốn (V) */
    float    current_limit;     /* Giới hạn dòng (ratio 0~1) */
    bool     should_run;        /* true = muốn module chạy */
} MXR_Setpoint_t;

/** Instance cho 1 module sạc */
typedef struct {
    /* Cấu hình */
    uint8_t  addr;              /* Địa chỉ CAN (0~63) */
    uint8_t  group;             /* Group number */
    bool     enabled;           /* Module có được quản lý không */

    /* Trạng thái đo được */
    float    voltage;           /* Output voltage (V) */
    float    current;           /* Output current (A) */
    float    current_limit;     /* Current limit point (ratio) */
    float    temp_dcdc;         /* DCDC board temperature (C) */
    float    temp_ambient;      /* Ambient temperature (C) */
    uint32_t alarm_status;      /* Raw alarm bits */
    CHG_AlarmFlag_t alarm_flags; /* Standardized alarm flags */
    uint32_t input_power;       /* Input power (W) */

    /* Trạng thái quản lý */
    MXR_State_t    state;       /* FSM state */
    MXR_Setpoint_t setpoint;    /* Cài đặt mong muốn */
    MXR_CommStats_t stats;      /* Thống kê comm */

    /* Timing */
    uint32_t last_rx_tick;      /* Tick nhận response cuối */
    uint32_t last_tx_tick;      /* Tick gửi request cuối */
    uint32_t state_enter_tick;  /* Tick vào state hiện tại */
    uint8_t  retry_count;       /* Đếm retry trong state hiện tại */

    /* Poll state machine */
    uint8_t  poll_step;         /* Register đang poll */
} MXR_Module_t;

/** Tổng hợp trạng thái toàn hệ thống (nhiều module) */
typedef struct {
    float    total_current;     /* Tổng dòng output (A) */
    float    total_power_in;    /* Tổng công suất vào (W) */
    float    voltage;           /* Điện áp chung (V) - từ module đầu tiên online */
    uint8_t  modules_online;    /* Số module đang online */
    uint8_t  modules_fault;     /* Số module đang fault */
    bool     any_critical;      /* Có bất kỳ module nào alarm critical */
} MXR_SystemSummary_t;

/** Response from module (dùng khi parse CAN frame) */
typedef struct {
    uint8_t  src_addr;          /* Địa chỉ module gửi response */
    uint8_t  data_type;         /* 0x41=float, 0x42=int */
    uint8_t  error_code;        /* 0xF0=OK */
    uint16_t reg_number;        /* Register number */
    union {
        float    f_val;
        uint32_t u_val;
        int32_t  i_val;
    } data;
    bool     valid;
} MXR_Response_t;

/* ============== API ============== */

/**
 * @brief  Khởi tạo hệ thống Maxwell (gọi 1 lần sau BSP_CAN_Init)
 *         Clear tất cả module instances.
 */
void MXR_Init(void);

/**
 * @brief  Thêm 1 module vào hệ thống
 * @param  addr   Địa chỉ CAN module (0~63, xem DIP switch trên module)
 * @param  group  Group number (0~7)
 * @retval Index của module (0 ~ MXR_MAX_MODULES-1), hoặc -1 nếu đầy
 */
int8_t MXR_AddModule(uint8_t addr, uint8_t group);

/**
 * @brief  Xóa 1 module khỏi hệ thống (gửi Stop trước nếu đang chạy)
 * @param  idx  Index module (từ MXR_AddModule)
 */
void MXR_RemoveModule(uint8_t idx);

/**
 * @brief  Lấy con trỏ module instance (read-only cho upper layer)
 * @param  idx  Module index
 * @retval Pointer tới MXR_Module_t, hoặc NULL nếu idx invalid
 */
const MXR_Module_t *MXR_GetModule(uint8_t idx);

/**
 * @brief  Lấy số module đang active
 */
uint8_t MXR_GetModuleCount(void);

/* --- Điều khiển theo module --- */

/**
 * @brief  Cài đặt điện áp cho 1 module
 * @param  idx        Module index
 * @param  voltage_v  Điện áp mong muốn (V)
 */
bool MXR_SetVoltage(uint8_t idx, float voltage_v);

/**
 * @brief  Cài đặt giới hạn dòng cho 1 module
 * @param  idx    Module index
 * @param  ratio  0.0 ~ 1.0 (so với dòng định mức)
 */
bool MXR_SetCurrentLimit(uint8_t idx, float ratio);

/**
 * @brief  Bật 1 module sạc
 */
bool MXR_Start(uint8_t idx);

/**
 * @brief  Tắt 1 module sạc
 */
bool MXR_Stop(uint8_t idx);

/* --- Điều khiển toàn hệ thống (broadcast) --- */

/**
 * @brief  Cài đặt điện áp cho TẤT CẢ module enabled
 */
void MXR_SetVoltageAll(float voltage_v);

/**
 * @brief  Cài đặt current limit cho TẤT CẢ module enabled
 */
void MXR_SetCurrentLimitAll(float ratio);

/**
 * @brief  Start TẤT CẢ module enabled
 */
void MXR_StartAll(void);

/**
 * @brief  Stop TẤT CẢ module enabled
 */
void MXR_StopAll(void);

/**
 * @brief  Emergency stop: tắt tất cả ngay lập tức, clear setpoint
 */
void MXR_EmergencyStop(void);

/* --- Xử lý runtime (gọi từ main loop / RTOS task) --- */

/**
 * @brief  Tick xử lý chính - gọi định kỳ mỗi ~10-50ms
 *         Thực hiện: poll từng module, detect timeout, auto-recovery
 * @param  now_tick  Giá trị HAL_GetTick() hiện tại
 *
 * Hàm này xoay vòng giữa các module: mỗi lần gọi xử lý 1 module,
 * gửi 1 CAN request. Interval 100ms × 7 regs × N modules = full cycle.
 */
void MXR_Process(uint32_t now_tick);

/**
 * @brief  Nạp CAN frame nhận được vào driver (gọi từ CAN RX callback)
 * @param  ext_id  29-bit extended ID
 * @param  data    8 bytes data
 * @param  dlc     Data length code
 * @note   Hàm này phải được gọi cho MỌI frame nhận trên CAN bus Maxwell.
 *         Driver tự xác định frame thuộc module nào.
 */
void MXR_FeedCanFrame(uint32_t ext_id, const uint8_t *data, uint8_t dlc);

/**
 * @brief  Lấy tổng hợp trạng thái hệ thống
 * @param  summary  Output struct
 */
void MXR_GetSystemSummary(MXR_SystemSummary_t *summary);

/* --- Utility --- */

/**
 * @brief  Build 29-bit CAN Extended ID theo Maxwell protocol
 */
uint32_t MXR_BuildFrameID(uint8_t dst_addr, uint8_t src_addr,
                           uint8_t ptp, uint8_t group);

/**
 * @brief  Kiểm tra module có alarm critical không
 */
static inline bool MXR_HasCriticalAlarm(const MXR_Module_t *mod) {
    return mod->alarm_flags != CHG_ALARM_NONE;
}

/**
 * @brief  Kiểm tra module có online không
 */
static inline bool MXR_IsOnline(const MXR_Module_t *mod) {
    return (mod->state == MXR_STATE_RUNNING || mod->state == MXR_STATE_STARTING);
}

#endif /* MAXWELL_CHARGER_H */
