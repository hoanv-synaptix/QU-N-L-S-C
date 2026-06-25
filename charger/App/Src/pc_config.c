/**
 * @file    pc_config.c
 * @brief   Configuration management implementation with Flash persistence
 */

#include "pc_config.h"
#include "flash_storage.h"
#include "charger_core.h"
#include <string.h>

/* ============== Default Configurations ============== */

static const PC_CfgSystem_t default_system = {
    .driver_id = PC_DRIVER_MAXWELL,
    .module_count = 1,
    .reserved1 = 0,
    .reserved2 = 0,
    .fw_version = 0x00020000,
    .hw_version = 0x0100,
    .language = 0,
    .debug_mode = 0,
    .serial_number = 0,
    .crc16 = 0
};

static const PC_CfgCharger_t default_charger = {
    .target_voltage = 57.6f,
    .target_current = 20.0f,
    .max_voltage = 60.0f,
    .max_current = 25.0f,
    .charge_mode = 0,
    .float_voltage = 95,
    .charge_timeout = 480,
    .term_by_soc = 1,
    .term_soc_full = 100,
    .term_by_voltage = 1,
    .term_voltage_delta = 10,
    .auto_restart = 0,
    .reserved = {0},
    .crc16 = 0
};

static const PC_CfgModule_t default_module = {
    .base_address = 0x30,
    .group_id = 0,
    .rated_voltage = 48.0f,
    .rated_current = 10.0f,
    .parallel_count = 1,
    .comm_timeout = 2000,
    .retry_count = 3,
    .poll_interval = 500,
    .reserved = {0},
    .crc16 = 0
};

static const PC_CfgProtect_t default_protect = {
    .over_voltage = 60.0f,
    .under_voltage = 40.0f,
    .voltage_delta = 2.0f,
    .over_current = 30.0f,
    .current_delta = 5.0f,
    .over_temp_dcdc = 75.0f,
    .over_temp_ambient = 50.0f,
    .under_temp = -10.0f,
    .under_voltage_input = 180.0f,
    .over_voltage_input = 265.0f,
    .delay_over_temp = 60,
    .delay_over_current = 1000,
    .reserved = {0},
    .crc16 = 0
};

static const PC_CfgBms_t default_bms = {
    .bms_enabled = 1,
    .bms_can_channel = 1,
    .bms_can_id = 0xF4,
    .bms_max_voltage = 58.8f,
    .bms_max_current = 25.0f,
    .bms_min_voltage = 42.0f,
    .soc_full = 100,
    .soc_empty = 10,
    .temp_max_charge = 45.0f,
    .temp_min_charge = 0.0f,
    .bms_timeout = 5000,
    .bms_resp_timeout = 1000,
    .reserved = {0},
    .crc16 = 0
};

static const PC_CfgDisplay_t default_display = {
    .status_interval = 500,
    .graph_interval = 1000,
    .show_graph = 1,
    .show_details = 1,
    .auto_scroll = 1,
    .beep_enabled = 1,
    .brightness = 80,
    .contrast = 50,
    .idle_timeout = 10,
    .reserved = {0},
    .crc16 = 0
};

/* ============== Runtime Configuration Storage ============== */

static PC_CfgSystem_t   g_cfg_system;
static PC_CfgCharger_t  g_cfg_charger;
static PC_CfgModule_t   g_cfg_module;
static PC_CfgProtect_t  g_cfg_protect;
static PC_CfgBms_t     g_cfg_bms;
static PC_CfgDisplay_t g_cfg_display;

/* ============== CRC16 ============== */

uint16_t PC_CRC16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/* ============== Helper Functions ============== */

static uint8_t* PC_Cfg_GetSectionPtr(uint8_t section)
{
    switch (section) {
        case PC_CFG_SECTION_SYSTEM:   return (uint8_t*)&g_cfg_system;
        case PC_CFG_SECTION_CHARGER:  return (uint8_t*)&g_cfg_charger;
        case PC_CFG_SECTION_MODULE:   return (uint8_t*)&g_cfg_module;
        case PC_CFG_SECTION_PROTECT:  return (uint8_t*)&g_cfg_protect;
        case PC_CFG_SECTION_BMS:      return (uint8_t*)&g_cfg_bms;
        case PC_CFG_SECTION_DISPLAY: return (uint8_t*)&g_cfg_display;
        default: return NULL;
    }
}

static uint16_t PC_Cfg_GetSectionSize(uint8_t section)
{
    switch (section) {
        case PC_CFG_SECTION_SYSTEM:   return sizeof(PC_CfgSystem_t);
        case PC_CFG_SECTION_CHARGER:  return sizeof(PC_CfgCharger_t);
        case PC_CFG_SECTION_MODULE:   return sizeof(PC_CfgModule_t);
        case PC_CFG_SECTION_PROTECT:  return sizeof(PC_CfgProtect_t);
        case PC_CFG_SECTION_BMS:      return sizeof(PC_CfgBms_t);
        case PC_CFG_SECTION_DISPLAY:  return sizeof(PC_CfgDisplay_t);
        default: return 0;
    }
}

/* ============== Configuration API ============== */

int PC_Cfg_Load(void)
{
    /* First load defaults */
    memcpy(&g_cfg_system, &default_system, sizeof(PC_CfgSystem_t));
    memcpy(&g_cfg_charger, &default_charger, sizeof(PC_CfgCharger_t));
    memcpy(&g_cfg_module, &default_module, sizeof(PC_CfgModule_t));
    memcpy(&g_cfg_protect, &default_protect, sizeof(PC_CfgProtect_t));
    memcpy(&g_cfg_bms, &default_bms, sizeof(PC_CfgBms_t));
    memcpy(&g_cfg_display, &default_display, sizeof(PC_CfgDisplay_t));

    /* Try to load from flash */
    if (FlashStorage_IsValid()) {
        uint8_t buffer[2048];
        uint16_t len = sizeof(buffer);
        
        if (FlashStorage_Load(buffer, &len)) {
            /* Parse loaded config */
            uint16_t offset = 0;
            
            /* System */
            uint16_t size = PC_Cfg_GetSectionSize(PC_CFG_SECTION_SYSTEM);
            if (offset + size <= len) {
                memcpy(&g_cfg_system, buffer + offset, size);
                offset += size;
            }
            
            /* Charger */
            size = PC_Cfg_GetSectionSize(PC_CFG_SECTION_CHARGER);
            if (offset + size <= len) {
                memcpy(&g_cfg_charger, buffer + offset, size);
                offset += size;
            }
            
            /* Module */
            size = PC_Cfg_GetSectionSize(PC_CFG_SECTION_MODULE);
            if (offset + size <= len) {
                memcpy(&g_cfg_module, buffer + offset, size);
                offset += size;
            }
            
            /* Protect */
            size = PC_Cfg_GetSectionSize(PC_CFG_SECTION_PROTECT);
            if (offset + size <= len) {
                memcpy(&g_cfg_protect, buffer + offset, size);
                offset += size;
            }
            
            /* BMS */
            size = PC_Cfg_GetSectionSize(PC_CFG_SECTION_BMS);
            if (offset + size <= len) {
                memcpy(&g_cfg_bms, buffer + offset, size);
                offset += size;
            }
            
            /* Display */
            size = PC_Cfg_GetSectionSize(PC_CFG_SECTION_DISPLAY);
            if (offset + size <= len) {
                memcpy(&g_cfg_display, buffer + offset, size);
            }
        }
    }

    /* Apply loaded config to charger driver */
    CHG_SelectDriver((CHG_DriverId_t)g_cfg_system.driver_id);
    CHG_Init();
    
    return 0;
}

int PC_Cfg_Save(void)
{
    /* Build combined config buffer */
    uint8_t buffer[2048];
    uint16_t offset = 0;
    
    /* System */
    memcpy(buffer + offset, &g_cfg_system, sizeof(g_cfg_system));
    offset += sizeof(g_cfg_system);
    
    /* Charger */
    memcpy(buffer + offset, &g_cfg_charger, sizeof(g_cfg_charger));
    offset += sizeof(g_cfg_charger);
    
    /* Module */
    memcpy(buffer + offset, &g_cfg_module, sizeof(g_cfg_module));
    offset += sizeof(g_cfg_module);
    
    /* Protect */
    memcpy(buffer + offset, &g_cfg_protect, sizeof(g_cfg_protect));
    offset += sizeof(g_cfg_protect);
    
    /* BMS */
    memcpy(buffer + offset, &g_cfg_bms, sizeof(g_cfg_bms));
    offset += sizeof(g_cfg_bms);
    
    /* Display */
    memcpy(buffer + offset, &g_cfg_display, sizeof(g_cfg_display));
    offset += sizeof(g_cfg_display);
    
    /* Save to flash */
    return FlashStorage_Save(buffer, offset) ? 0 : -1;
}

int PC_Cfg_Reset(void)
{
    PC_Cfg_Load();  /* Load defaults */
    return PC_Cfg_Save();  /* Save to flash */
}

int PC_Cfg_GetSection(uint8_t section, void *data, uint16_t *len)
{
    uint8_t *ptr = PC_Cfg_GetSectionPtr(section);
    uint16_t size = PC_Cfg_GetSectionSize(section);

    if (ptr == NULL || data == NULL || len == NULL) {
        return -1;
    }

    memcpy(data, ptr, size);
    *len = size;
    return 0;
}

int PC_Cfg_SetSection(uint8_t section, const void *data, uint16_t len)
{
    uint8_t *ptr = PC_Cfg_GetSectionPtr(section);
    uint16_t size = PC_Cfg_GetSectionSize(section);

    if (ptr == NULL || data == NULL) {
        return -1;
    }

    if (len != size) {
        return -2;
    }

    memcpy(ptr, data, size);
    return 0;
}

/* ============== Runtime Status ============== */

void PC_Status_Fill(PC_RuntimeStatus_t *status)
{
    CHG_SystemSummary_t sum;
    CHG_GetSystemSummary(&sum);

    memset(status, 0, sizeof(PC_RuntimeStatus_t));

    /* System state */
    status->system_state = sum.running ? 1 : 0;
    status->driver_id = g_cfg_system.driver_id;
    status->modules_total = sum.module_count;
    status->modules_online = sum.modules_online;

    /* Charger output */
    status->output_voltage = sum.voltage;
    status->output_current = sum.total_current;
    status->output_power = sum.total_power_in;

    /* Battery info - would come from BMS */
    status->batt_voltage = sum.voltage;
    status->batt_current = sum.total_current;
    status->batt_soc = 0;

    /* Temperatures */
    status->temp_dcdc = sum.max_temp_dcdc;
    status->temp_ambient = sum.max_temp_ambient;
    status->temp_battery = 0;

    /* Input */
    status->input_voltage = 220.0f;
    status->input_power = sum.total_power_in;

    /* Alarms */
    status->alarm_flags = sum.system_alarms;
    status->module_alarms = sum.module_alarms;

    /* Timing */
    status->charge_time_sec = 0;
    status->charged_wh = 0;
    status->charged_ah = 0;

    /* Counters */
    status->cycle_count = 0;
    status->error_count = sum.modules_fault;
}

/* ============== History (Simplified) ============== */

#define MAX_HISTORY_RECORDS 100
static PC_HistoryRecord_t g_history[MAX_HISTORY_RECORDS];
static uint16_t g_history_count = 0;
static uint16_t g_history_index = 0;

int PC_History_Add(const PC_HistoryRecord_t *record)
{
    if (record == NULL) return -1;

    memcpy(&g_history[g_history_index], record, sizeof(PC_HistoryRecord_t));
    g_history_index = (g_history_index + 1) % MAX_HISTORY_RECORDS;
    if (g_history_count < MAX_HISTORY_RECORDS) {
        g_history_count++;
    }
    return 0;
}

int PC_History_Get(uint16_t index, PC_HistoryRecord_t *record)
{
    if (record == NULL) return -1;
    if (index >= g_history_count) return -2;

    uint16_t actual_index = (g_history_index + index) % MAX_HISTORY_RECORDS;
    memcpy(record, &g_history[actual_index], sizeof(PC_HistoryRecord_t));
    return 0;
}

int PC_History_Count(void)
{
    return g_history_count;
}

int PC_History_Clear(void)
{
    g_history_count = 0;
    g_history_index = 0;
    memset(g_history, 0, sizeof(g_history));
    return 0;
}
