/**
 * @file    pc_config.h
 * @brief   Configuration structures for PC ↔ MCU communication
 * @note    Defines all configurable parameters used by the system
 *
 * Configuration sections:
 *   - SECTION_SYSTEM:    System-level settings
 *   - SECTION_CHARGER:   Charging parameters
 *   - SECTION_MODULE:    Module configuration
 *   - SECTION_PROTECT:  Protection thresholds
 *   - SECTION_BMS:      BMS communication settings
 *   - SECTION_DISPLAY:  HMI display settings
 *   - SECTION_HISTORY:  Historical data
 */

#ifndef PC_CONFIG_H
#define PC_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/* ============== Section IDs ============== */
#define PC_CFG_SECTION_SYSTEM     0x01
#define PC_CFG_SECTION_CHARGER    0x02
#define PC_CFG_SECTION_MODULE     0x03
#define PC_CFG_SECTION_PROTECT    0x04
#define PC_CFG_SECTION_BMS        0x05
#define PC_CFG_SECTION_DISPLAY    0x06
#define PC_CFG_SECTION_HISTORY    0x07
#define PC_CFG_SECTION_ALL        0xFF

/* ============== Driver IDs ============== */
#define PC_DRIVER_NONE            0x00
#define PC_DRIVER_MAXWELL         0x01
#define PC_DRIVER_LIANMING        0x02
#define PC_DRIVER_TONHE           0x03

/* ============== SECTION: SYSTEM ============== */
/* System-level configuration (16 bytes) */
#pragma pack(push, 1)
typedef struct {
    uint8_t  driver_id;          /* Driver type: 0=None, 1=Maxwell, 2=Lianming, 3=TonHe */
    uint8_t  module_count;       /* Number of parallel modules */
    uint8_t  reserved1;         /* Reserved */
    uint8_t  reserved2;         /* Reserved */
    uint32_t fw_version;         /* Firmware version (MMNNPP) */
    uint16_t hw_version;        /* Hardware version */
    uint8_t  language;          /* 0=EN, 1=VN, 2=CN */
    uint8_t  debug_mode;         /* 0=Normal, 1=Debug */
    uint32_t serial_number;      /* Device serial number */
    uint16_t crc16;             /* CRC16 of this struct */
} PC_CfgSystem_t;
#pragma pack(pop)

/* ============== SECTION: CHARGER ============== */
/* Charging parameters (32 bytes) */
#pragma pack(push, 1)
typedef struct {
    /* Target settings */
    float target_voltage;        /* Target battery voltage (V) */
    float target_current;        /* Target charging current (A) */
    float max_voltage;          /* Maximum output voltage (V) */
    float max_current;          /* Maximum output current (A) */

    /* Charging profile */
    uint8_t charge_mode;        /* 0=CCCV, 1=Float, 2=Pulse */
    uint8_t float_voltage;      /* Float voltage percentage of target */
    uint16_t charge_timeout;   /* Maximum charge time (minutes) */

    /* Termination conditions */
    uint8_t term_by_soc;        /* Terminate by SOC (0=disable, 1=enable) */
    uint8_t term_soc_full;      /* SOC percentage for full (default 100) */
    uint8_t term_by_voltage;   /* Terminate by voltage (0=disable, 1=enable) */
    uint8_t term_voltage_delta;/* Voltage delta for termination (0.1V) */

    /* Enable flags */
    uint8_t auto_restart;       /* Auto restart after full (0=disable, 1=enable) */
    uint8_t reserved[3];
    uint16_t crc16;
} PC_CfgCharger_t;
#pragma pack(pop)

/* ============== SECTION: MODULE ============== */
/* Module configuration (32 bytes) */
#pragma pack(push, 1)
typedef struct {
    /* Module addressing */
    uint8_t  base_address;     /* Base CAN address for modules */
    uint8_t  group_id;         /* Group ID for parallel operation */

    /* Module parameters */
    float    rated_voltage;    /* Rated output voltage (V) */
    float    rated_current;     /* Rated output current per module (A) */
    uint16_t parallel_count;    /* Number of parallel modules */

    /* Communication */
    uint16_t comm_timeout;     /* Module communication timeout (ms) */
    uint8_t  retry_count;      /* Number of retries before fault */

    /* Status polling */
    uint16_t poll_interval;    /* Status poll interval (ms) */
    uint8_t  reserved[6];
    uint16_t crc16;
} PC_CfgModule_t;
#pragma pack(pop)

/* ============== SECTION: PROTECTION ============== */
/* Protection thresholds (32 bytes) */
#pragma pack(push, 1)
typedef struct {
    /* Voltage protection */
    float over_voltage;        /* Output overvoltage threshold (V) */
    float under_voltage;       /* Output undervoltage threshold (V) */
    float voltage_delta;       /* Output voltage deviation (V) */

    /* Current protection */
    float over_current;        /* Overcurrent threshold (A) */
    float current_delta;       /* Current imbalance threshold (A) */

    /* Temperature protection */
    float over_temp_dcdc;      /* DCDC overtemperature (°C) */
    float over_temp_ambient;   /* Ambient overtemperature (°C) */
    float under_temp;          /* Undertemperature (°C) */

    /* Input protection */
    float under_voltage_input; /* Input undervoltage (V) */
    float over_voltage_input;  /* Input overvoltage (V) */

    /* Timing */
    uint16_t delay_over_temp;  /* Overtemp delay (seconds) */
    uint16_t delay_over_current;/* Overcurrent delay (ms) */

    uint8_t reserved[4];
    uint16_t crc16;
} PC_CfgProtect_t;
#pragma pack(pop)

/* ============== SECTION: BMS ============== */
/* BMS communication settings (24 bytes) */
#pragma pack(push, 1)
typedef struct {
    /* BMS connection */
    uint8_t  bms_enabled;      /* BMS enabled (0=disable, 1=enable) */
    uint8_t  bms_can_channel;  /* CAN channel (0=CAN1, 1=CAN2) */
    uint32_t bms_can_id;       /* BMS CAN ID base */

    /* BMS parameters */
    float    bms_max_voltage;  /* Maximum charge voltage from BMS (V) */
    float    bms_max_current;  /* Maximum charge current from BMS (A) */
    float    bms_min_voltage;  /* Minimum voltage (V) */

    /* SOC settings */
    uint8_t  soc_full;        /* SOC considered full (%) */
    uint8_t  soc_empty;       /* SOC considered empty (%) */

    /* Temperature limits */
    float    temp_max_charge;  /* Max charging temperature (°C) */
    float    temp_min_charge;  /* Min charging temperature (°C) */

    /* Timeout */
    uint16_t bms_timeout;      /* BMS timeout (ms) */
    uint16_t bms_resp_timeout; /* BMS response timeout (ms) */

    uint8_t reserved[2];
    uint16_t crc16;
} PC_CfgBms_t;
#pragma pack(pop)

/* ============== SECTION: DISPLAY ============== */
/* HMI Display settings (16 bytes) */
#pragma pack(push, 1)
typedef struct {
    /* Display update */
    uint16_t status_interval;  /* Status update interval (ms) */
    uint16_t graph_interval;    /* Graph update interval (ms) */

    /* Display options */
    uint8_t show_graph;        /* Show real-time graph (0=disable, 1=enable) */
    uint8_t show_details;     /* Show detailed module info */
    uint8_t auto_scroll;      /* Auto scroll log */
    uint8_t beep_enabled;     /* Beep on events */

    /* Screen settings */
    uint8_t brightness;        /* Display brightness (0-100) */
    uint8_t contrast;         /* Display contrast (0-100) */
    uint16_t idle_timeout;    /* Idle timeout (minutes, 0=never) */

    uint8_t reserved[3];
    uint16_t crc16;
} PC_CfgDisplay_t;
#pragma pack(pop)

/* ============== SECTION: HISTORY ============== */
/* History record (32 bytes) */
#pragma pack(push, 1)
typedef struct {
    uint32_t timestamp;       /* Unix timestamp */
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;

    /* Charge data */
    float start_voltage;       /* Voltage at start (V) */
    float end_voltage;         /* Voltage at end (V) */
    float charged_energy;      /* Energy charged (Wh) */
    float charged_capacity;   /* Capacity charged (Ah) */
    uint16_t duration_minutes; /* Charge duration (minutes) */

    /* Final state */
    uint8_t termination_type;  /* 0=Manual, 1=Full, 2=Timeout, 3=Error */
    uint8_t error_code;        /* Error code if any */
    uint8_t module_count;      /* Modules used */

    uint8_t reserved[6];
    uint16_t crc16;
} PC_HistoryRecord_t;
#pragma pack(pop)

/* ============== Combined Configuration ============== */
/* All configurations combined (for flash storage) */
#define PC_CONFIG_MAGIC         0xDEADBEEF

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;            /* Magic number for validation */
    uint32_t version;         /* Config version */

    PC_CfgSystem_t   system;
    PC_CfgCharger_t charger;
    PC_CfgModule_t  module;
    PC_CfgProtect_t protect;
    PC_CfgBms_t     bms;
    PC_CfgDisplay_t display;

    uint32_t signature;       /* Auth signature */
    uint16_t crc16;           /* CRC16 of all above */
} PC_ConfigFull_t;
#pragma pack(pop)

/* ============== Runtime Status ============== */
/* Current runtime status from MCU (sent periodically) */
#pragma pack(push, 1)
typedef struct {
    /* System state */
    uint8_t  system_state;    /* 0=Idle, 1=Charging, 2=Full, 3=Fault */
    uint8_t  driver_id;       /* Current driver */
    uint8_t  modules_total;   /* Total modules configured */
    uint8_t  modules_online; /* Modules responding */

    /* Charger output */
    float    output_voltage;  /* Actual output voltage (V) */
    float    output_current;  /* Actual output current (A) */
    float    output_power;    /* Actual output power (W) */

    /* Battery info */
    float    batt_voltage;    /* Battery voltage (V) */
    float    batt_current;    /* Battery current (A) */
    uint8_t  batt_soc;        /* Battery SOC (%) */

    /* Temperatures */
    float    temp_dcdc;       /* DCDC temperature (°C) */
    float    temp_ambient;    /* Ambient temperature (°C) */
    float    temp_battery;    /* Battery temperature (°C) */

    /* Input */
    float    input_voltage;   /* Input voltage (V) */
    float    input_power;     /* Input power (W) */

    /* Alarms */
    uint32_t alarm_flags;     /* System alarm bits */
    uint32_t module_alarms;   /* Module alarm bits OR'd */

    /* Timing */
    uint32_t charge_time_sec;/* Charge time (seconds) */
    uint32_t charged_wh;     /* Energy charged (Wh) */
    uint32_t charged_ah;     /* Capacity charged (mAh) */

    /* Counters */
    uint16_t cycle_count;    /* Total charge cycles */
    uint16_t error_count;    /* Error count */

    uint8_t reserved[2];
} PC_RuntimeStatus_t;
#pragma pack(pop)

/* ============== API ============== */

/* Configuration management */
int  PC_Cfg_Load(void);
int  PC_Cfg_Save(void);
int  PC_Cfg_Reset(void);
int  PC_Cfg_GetSection(uint8_t section, void *data, uint16_t *len);
int  PC_Cfg_SetSection(uint8_t section, const void *data, uint16_t len);

/* Runtime status */
void PC_Status_Fill(PC_RuntimeStatus_t *status);

/* History */
int  PC_History_Add(const PC_HistoryRecord_t *record);
int  PC_History_Get(uint16_t index, PC_HistoryRecord_t *record);
int  PC_History_Count(void);
int  PC_History_Clear(void);

/* Validation */
uint16_t PC_CRC16(const uint8_t *data, uint16_t len);

#endif /* PC_CONFIG_H */
