/**
 * @file driver_lianming.c
 * @brief Lianming Power Digital Charging Module Driver
 * @note Protocol V2.0 - CAN 2.0B Extended Frame, 125Kbps
 *
 * Hardware Interface:
 *   - CAN bus: 125 Kbps, Extended 29-bit frame
 *   - Isolation: Required (isolated CAN transceiver)
 *
 * CAN Frame Format (29-bit Extended ID):
 *   | Bit 28-15    | Bit 14-7 | Bit 6-0       |
 *   | Command ID    | Reserved  | Module Address |
 *   | 0x1907C0     | 0x00     | 0-60          |
 *
 * TX Command ID:  0x1907C0 + module_addr
 * RX Response ID: 0x1807C0 + module_addr
 *
 * Data Format - Set Output (CMD=0):
 *   | Byte 0 | Byte 1-3          | Byte 4-7          |
 *   | CMD    | Current (mA)      | Voltage (mV)     |
 *   | 0x00   | 0x015F90 (90A)   | 0x000186A0 (100V) |
 *
 * Data Format - Read Status (CMD=1):
 *   | Byte 0 | Byte 1    | Byte 2-3     | Byte 4-5    | Byte 6-7    |
 *   | CMD    | Status    | Current(A)   | Voltage(V) | Fault Flags |
 *   | 0x01   | 0xFF     | 0x0117(27.9)| 0x03E8(100) | 0x0000      |
 *
 * Data Format - Start/Stop (CMD=2):
 *   | Byte 0 | Byte 1-5 | Byte 6     |
 *   | CMD    | Reserved | Start/Stop |
 *   | 0x02   | 0x00    | 0x55/0xAA  |
 *
 * References:
 *   - "Lianming Power Digital Power Module CAN Communication Protocol V2.0.pdf"
 *   - "Lian.md" (Vietnamese translation)
 *
 * Example:
 *   Set 100V, 90A:  ID=0x1907C083, Data: 00 01 5F 90 00 01 86 A0
 *   Start module 1: ID=0x1907C001, Data: 02 00 00 00 00 00 00 55
 *   Stop module 1:  ID=0x1907C001, Data: 02 00 00 00 00 00 00 AA
 */

#include "driver_lianming.h"
#include "bsp_can.h"
#include "charger_protocol.h"
#include <string.h>

/* ============== Lianming-specific CAN Constants ============== */
/* Protocol V2.0 - CAN 2.0B Extended Frame, 125Kbps */

#define LM_ADDR_CONTROLLER       0xA0U       /* Controller address (not used in ID) */
#define LM_ADDR_BROADCAST        0x00U       /* Broadcast address (0) */
#define LM_MODULE_MIN_ADDR       1U           /* Module address range */
#define LM_MODULE_MAX_ADDR       60U

/* CAN ID Base Addresses */
#define LM_CMD_BASE             0x1907C080U    /* Command TX ID base */
#define LM_RESP_BASE            0x1807C080U    /* Response RX ID base */

/* Command Codes (Byte 0 of data) */
#define LM_CMD_SET_OUTPUT       0x00U        /* Set voltage/current */
#define LM_CMD_READ_INFO        0x01U        /* Read status */
#define LM_CMD_START_STOP       0x02U        /* Start/Stop */

/* Start/Stop values (Byte 6 of data) */
#define LM_START_VALUE          0x55U        /* Start */
#define LM_STOP_VALUE           0xAAU        /* Stop */

/* Response codes (Byte 1 of response) */
#define LM_RESP_OK              0xFFU        /* Success */
#define LM_RESP_FAIL            0x00U        /* Fail */

/* Scale factors */
#define LM_VOLTAGE_SCALE_MV    1U           /* milliVolts */
#define LM_CURRENT_SCALE_MA    1U           /* milliAmps */

/* ============== Configuration ============== */

#define LM_MAX_MODULES          8U
#define LM_KEEPALIVE_TIMEOUT_MS 2000U
#define LM_RECOVERY_DELAY_MS    3000U
#define LM_STEP_DELAY_MS        50U
#define LM_MAX_RETRY            3U

/* ============== CAN Frame ID Builder ============== */

/**
 * @brief  Build TX CAN ID for Lianming
 * @note   Format: Command ID (14 bits) | Module Address (7 bits)
 *         TX: 0x1907C080 + addr
 *         RX: 0x1807C080 + addr
 * @param  module_addr  Module address (1-60, 0 for broadcast)
 * @param  is_response  true for response ID, false for command ID
 * @retval 29-bit CAN ID
 */
static uint32_t lm_build_can_id(uint8_t module_addr, bool is_response)
{
    uint32_t base = is_response ? LM_RESP_BASE : LM_CMD_BASE;
    return base | (module_addr & 0x7FU);
}

/**
 * @brief  Extract module address from response ID
 * @param  ext_id  Response CAN ID
 * @retval Module address (1-60)
 */
static uint8_t lm_parse_response_addr(uint32_t ext_id)
{
    return (uint8_t)(ext_id & 0x7FU);
}

/**
 * @brief  Convert Lianming voltage payload to float (V)
 * @note   Response format: bytes 4-5 = voltage (0.1V/bit)
 */
static float lm_payload_to_voltage(const uint8_t *data)
{
    uint16_t value = CHG_ProtocolBEToU16(&data[4]);  /* Voltage at bytes 4-5 */
    return (float)value / 10.0f;  /* 0.1V/bit -> V */
}

/**
 * @brief  Convert Lianming current payload to float (A)
 * @note   Response format: bytes 2-3 = current (0.1A/bit)
 */
static float lm_payload_to_current(const uint8_t *data)
{
    uint16_t value = CHG_ProtocolBEToU16(&data[2]);  /* Current at bytes 2-3 */
    return (float)value / 10.0f;  /* 0.1A/bit -> A (Fixed from 100.0f) */
}

/* ============== Internal Types ============== */

typedef struct {
    CHG_ModuleView_t view;
    uint32_t last_state_tick;
    uint8_t retry_count;
    uint8_t start_attempts;
    bool should_run;
} LM_Module_t;

static LM_Module_t g_modules[LM_MAX_MODULES];
static uint8_t g_module_count = 0;
static uint8_t g_rr_index = 0;

/* Note: Lianming uses CMD=1 to read all status at once, no need for poll registers */

static CHG_AlarmFlag_t parse_lianming_alarm(uint16_t raw_alarm)
{
    CHG_AlarmFlag_t flags = CHG_ALARM_NONE;
    /* raw_alarm is (Byte6 << 8) | Byte7 */
    
    /* Byte 7 bits (0-7 in raw_alarm) */
    if (raw_alarm & (1U << 1)) flags |= CHG_ALARM_HW_FAULT;         /* Module fault */
    if (raw_alarm & (1U << 3)) flags |= CHG_ALARM_HW_FAULT;         /* Fan fault */
    if (raw_alarm & (1U << 4)) flags |= CHG_ALARM_HW_FAULT;         /* Input overvoltage */
    if (raw_alarm & (1U << 5)) flags |= CHG_ALARM_AC_UNDER_VOLT;    /* Input under-voltage */
    if (raw_alarm & (1U << 6)) flags |= CHG_ALARM_OVER_VOLTAGE_OUT; /* Output overvoltage */
    if (raw_alarm & (1U << 7)) flags |= CHG_ALARM_HW_FAULT;         /* Output under-voltage */
    
    /* Byte 6 bits (8-15 in raw_alarm) */
    if (raw_alarm & (1U << 8)) flags |= CHG_ALARM_SHORT_CIRCUIT;    /* Overcurrent protection */
    if (raw_alarm & (1U << 9)) flags |= CHG_ALARM_OVER_TEMP;        /* Over temperature */
    
    return flags;
}

static CHG_ModuleState_t lm_state_from_flags(const LM_Module_t *mod, bool fault, bool recovering)
{
    if (recovering) {
        return CHG_STATE_RECOVERING;
    }
    if (fault) {
        return CHG_STATE_FAULT;
    }
    if (mod == 0 || !mod->view.online) {
        return CHG_STATE_OFFLINE;
    }
    if (mod->should_run) {
        return mod->view.running ? CHG_STATE_RUNNING : CHG_STATE_STARTING;
    }
    return CHG_STATE_IDLE;
}

static void clear_view(CHG_ModuleView_t *view)
{
    memset(view, 0, sizeof(*view));
    view->enabled = true;
    view->current_limit = 1.0f;
    view->state = CHG_STATE_IDLE;
}

static void set_state(LM_Module_t *mod, CHG_ModuleState_t state, uint32_t now)
{
    if (mod->view.state != state && state == CHG_STATE_STARTING) {
        mod->start_attempts++;
    } else if (state == CHG_STATE_IDLE || state == CHG_STATE_OFFLINE || state == CHG_STATE_FAULT) {
        mod->start_attempts = 0;
    }
    mod->view.state = state;
    mod->last_state_tick = now;
    mod->retry_count = 0;
    switch (state) {
    case CHG_STATE_IDLE:
        mod->view.online = true;
        mod->view.running = false;
        break;
    case CHG_STATE_STARTING:
        mod->view.online = true;
        mod->view.running = false;
        break;
    case CHG_STATE_RUNNING:
        mod->view.online = true;
        mod->view.running = true;
        break;
    case CHG_STATE_OFFLINE:
        mod->view.online = false;
        mod->view.running = false;
        break;
    case CHG_STATE_FAULT:
        mod->view.online = true;
        mod->view.running = false;
        break;
    case CHG_STATE_RECOVERING:
        mod->view.online = false;
        mod->view.running = false;
        break;
    }
}

/**
 * @brief  Send CAN frame to Lianming module (command format)
 * @note   Lianming format: CMD | Current(mA) | Voltage(mV)
 */
/**
 * @brief  Send read status command
 */
static void lm_send_read(LM_Module_t *mod)
{
    BSP_CAN_Frame_t frame;
    frame.ext_id = lm_build_can_id(mod->view.addr, false);
    frame.dlc = 8;
    frame.data[0] = LM_CMD_READ_INFO;      /* CMD = 1: Read status */
    frame.data[1] = 0x00;
    frame.data[2] = 0x00;
    frame.data[3] = 0x00;
    frame.data[4] = 0x00;
    frame.data[5] = 0x00;
    frame.data[6] = 0x00;
    frame.data[7] = 0x00;
    if (BSP_CAN_Transmit(&frame)) {
        mod->view.stats.tx_count++;
    }
}

/**
 * @brief  Set output voltage and current
 * @note   Lianming format: CMD=0, Byte 1-3: Current(mA), Byte 4-7: Voltage(mV)
 */
static void lm_set_output(uint8_t idx, float voltage_v, float current_a)
{
    /* Lianming format:
     * Byte 0: CMD (0x00 = Set)
     * Byte 1-3: Current (mA, 3 bytes big-endian)
     * Byte 4-7: Voltage (mV, 4 bytes big-endian)
     * Example: 00 01 5F 90 00 01 86 A0 = 90A, 100V
     */
    uint32_t voltage_mv = (uint32_t)(voltage_v * 1000.0f);  /* V -> mV */
    uint32_t current_ma = (uint32_t)(current_a * 1000.0f);  /* A -> mA */
    LM_Module_t *mod = &g_modules[idx];

    BSP_CAN_Frame_t frame;
    frame.ext_id = lm_build_can_id(mod->view.addr, false);
    frame.dlc = 8;
    frame.data[0] = LM_CMD_SET_OUTPUT;    /* CMD = 0 */
    frame.data[1] = (uint8_t)(current_ma >> 16);  /* Current byte 2 */
    frame.data[2] = (uint8_t)(current_ma >> 8);   /* Current byte 1 */
    frame.data[3] = (uint8_t)(current_ma);        /* Current byte 0 */
    frame.data[4] = (uint8_t)(voltage_mv >> 24);  /* Voltage byte 3 */
    frame.data[5] = (uint8_t)(voltage_mv >> 16);  /* Voltage byte 2 */
    frame.data[6] = (uint8_t)(voltage_mv >> 8);   /* Voltage byte 1 */
    frame.data[7] = (uint8_t)(voltage_mv);        /* Voltage byte 0 */

    if (BSP_CAN_Transmit(&frame)) {
        mod->view.stats.tx_count++;
    }
}

/**
 * @brief  Start module
 */
static void lm_start_module(uint8_t idx)
{
    LM_Module_t *mod = &g_modules[idx];
    BSP_CAN_Frame_t frame;
    frame.ext_id = lm_build_can_id(mod->view.addr, false);
    frame.dlc = 8;
    frame.data[0] = LM_CMD_START_STOP;     /* CMD = 2 */
    frame.data[1] = 0x00;
    frame.data[2] = 0x00;
    frame.data[3] = 0x00;
    frame.data[4] = 0x00;
    frame.data[5] = 0x00;
    frame.data[6] = LM_START_VALUE;         /* 0x55 = Start */
    frame.data[7] = 0x00;
    if (BSP_CAN_Transmit(&frame)) {
        mod->view.stats.tx_count++;
    }
}

/**
 * @brief  Stop module
 */
static void lm_stop_module(uint8_t idx)
{
    LM_Module_t *mod = &g_modules[idx];
    BSP_CAN_Frame_t frame;
    frame.ext_id = lm_build_can_id(mod->view.addr, false);
    frame.dlc = 8;
    frame.data[0] = LM_CMD_START_STOP;     /* CMD = 2 */
    frame.data[1] = 0x00;
    frame.data[2] = 0x00;
    frame.data[3] = 0x00;
    frame.data[4] = 0x00;
    frame.data[5] = 0x00;
    frame.data[6] = LM_STOP_VALUE;          /* 0xAA = Stop */
    frame.data[7] = 0x00;
    if (BSP_CAN_Transmit(&frame)) {
        mod->view.stats.tx_count++;
    }
}

/**
 * @brief  Send read status command (keepalive)
 */
static void lm_read_status(uint8_t idx, uint32_t now)
{
    g_modules[idx].view.last_tx_tick = now;
    lm_send_read(&g_modules[idx]);
}

static uint8_t find_by_addr(uint8_t addr)
{
    for (uint8_t i = 0; i < g_module_count; i++) {
        if (g_modules[i].view.enabled && g_modules[i].view.addr == addr) {
            return i;
        }
    }
    return 0xFFU;
}

static void sync_state_from_flags(LM_Module_t *mod, uint32_t now)
{
    bool fault = mod->view.alarm_flags != CHG_ALARM_NONE;
    bool recovering = (mod->view.state == CHG_STATE_RECOVERING);
    CHG_ModuleState_t new_state = lm_state_from_flags(mod, fault, recovering);
    set_state(mod, new_state, now);
    mod->view.last_rx_tick = now;
}

/**
 * @brief  Apply Lianming response data to module view
 * @note   Lianming response format (CMD=1 Read):
 *         Byte 0: CMD echo
 *         Byte 1: Status (0xFF = OK)
 *         Byte 2-3: Current (0.01A/bit, big-endian)
 *         Byte 4-5: Voltage (0.1V/bit, big-endian)
 *         Byte 6-7: Status flags
 */
static void apply_status(uint8_t idx, const uint8_t *data, uint32_t now)
{
    LM_Module_t *mod = &g_modules[idx];

    /* Parse the standard response format from Lianming */
    mod->view.current = lm_payload_to_current(data);      /* bytes 2-3 */
    mod->view.voltage = lm_payload_to_voltage(data);      /* bytes 4-5 */

    /* Byte 6-7: Status flags */
    uint16_t status_flags = CHG_ProtocolBEToU16(&data[6]);
    mod->view.alarm_status = status_flags;
    /* Preserve COMM_FAIL from software timeout */
    mod->view.alarm_flags = parse_lianming_alarm(status_flags) | (mod->view.alarm_flags & CHG_ALARM_COMM_FAIL);

    /* Running state: bit 0 of status = 0 means running */
    mod->view.running = ((status_flags & 0x01U) == 0U);

    mod->view.online = true;
    mod->view.last_rx_tick = now;

    if (mod->view.alarm_flags != CHG_ALARM_NONE) {
        set_state(mod, CHG_STATE_FAULT, now);
        return;
    }

    if (mod->view.state == CHG_STATE_OFFLINE || mod->view.state == CHG_STATE_RECOVERING) {
 mod->view.stats.recovery_count++;
        set_state(mod, mod->should_run ? CHG_STATE_STARTING : CHG_STATE_IDLE, now);
        return;
    }

    sync_state_from_flags(mod, now);
}

static void process_module(uint8_t idx, uint32_t now)
{
    LM_Module_t *mod = &g_modules[idx];
    if (!mod->view.enabled) {
        return;
    }

    switch (mod->view.state) {
    case CHG_STATE_IDLE:
        /* Send read status periodically for keepalive */
        lm_read_status(idx, now);
        if (mod->should_run) {
            set_state(mod, CHG_STATE_STARTING, now);
        }
        break;

    case CHG_STATE_STARTING:
        if (mod->start_attempts > LM_MAX_RETRY) {
            mod->view.alarm_flags |= CHG_ALARM_COMM_FAIL; /* Timeout */
            set_state(mod, CHG_STATE_FAULT, now);
            break;
        }
        if (mod->retry_count == 0U) {
            /* Set voltage and current together (Lianming format) */
            lm_set_output(idx, mod->view.voltage, mod->view.current_limit * 100.0f);
            mod->retry_count = 1U;
            mod->last_state_tick = now;
        } else if (mod->retry_count == 1U && (now - mod->last_state_tick) >= LM_STEP_DELAY_MS) {
            /* Start module */
            lm_start_module(idx);
            mod->retry_count = 2U;
            mod->last_state_tick = now;
        } else if (mod->retry_count >= 2U && (now - mod->last_state_tick) >= LM_STEP_DELAY_MS) {
            set_state(mod, CHG_STATE_RUNNING, now);
        }
        break;

    case CHG_STATE_RUNNING:
        mod->view.stats.rx_count++;
        lm_read_status(idx, now);
        if (!mod->should_run) {
            lm_stop_module(idx);
            set_state(mod, CHG_STATE_IDLE, now);
            break;
        }
        if ((now - mod->view.last_rx_tick) > LM_KEEPALIVE_TIMEOUT_MS) {
            mod->view.stats.timeout_count++;
            set_state(mod, CHG_STATE_OFFLINE, now);
        }
        break;

    case CHG_STATE_OFFLINE:
        if ((now - mod->last_state_tick) >= LM_RECOVERY_DELAY_MS) {
            set_state(mod, CHG_STATE_RECOVERING, now);
        }
        break;

    case CHG_STATE_RECOVERING:
        lm_read_status(idx, now);
        mod->retry_count++;
        if (mod->retry_count >= LM_MAX_RETRY) {
            set_state(mod, CHG_STATE_OFFLINE, now);
        }
        break;

    case CHG_STATE_FAULT:
        lm_read_status(idx, now);
        if (mod->view.alarm_flags == CHG_ALARM_NONE) {
            set_state(mod, mod->should_run ? CHG_STATE_STARTING : CHG_STATE_IDLE, now);
        }
        if ((now - mod->view.last_rx_tick) > LM_KEEPALIVE_TIMEOUT_MS) {
            mod->view.stats.timeout_count++;
            set_state(mod, CHG_STATE_OFFLINE, now);
        }
        break;
    }
}

void CHG_Lianming_Init(void)
{
    memset(g_modules, 0, sizeof(g_modules));
    g_module_count = 0;
    g_rr_index = 0;
}

static void lm_init(void)
{
    CHG_Lianming_Init();
}

static int8_t lm_add_module(uint8_t addr, uint8_t group)
{
    if (g_module_count >= LM_MAX_MODULES) {
        return -1;
    }

    LM_Module_t *mod = &g_modules[g_module_count];
    memset(mod, 0, sizeof(*mod));
    clear_view(&mod->view);
    mod->view.addr = addr;
    mod->view.group = group;
    mod->should_run = false;
    mod->last_state_tick = 0;
    mod->retry_count = 0;
    g_module_count++;
    return (int8_t)(g_module_count - 1U);
}

static void lm_remove_module(uint8_t idx)
{
    if (idx >= g_module_count) {
        return;
    }
    g_modules[idx].view.enabled = false;
    g_modules[idx].should_run = false;
    g_modules[idx].view.state = CHG_STATE_IDLE;
}

static bool lm_set_voltage(uint8_t idx, float voltage_v)
{
    if (idx >= g_module_count || !g_modules[idx].view.enabled) return false;
    g_modules[idx].view.voltage = voltage_v;
    /* Lianming: send voltage+current together when running */
    if (g_modules[idx].view.state == CHG_STATE_RUNNING) {
        lm_set_output(idx, voltage_v, g_modules[idx].view.current_limit * 100.0f);
    }
    return true;
}

static bool lm_set_current_limit(uint8_t idx, float current_a)
{
    if (idx >= g_module_count || !g_modules[idx].view.enabled) return false;
    if (current_a < 0.0f) current_a = 0.0f;
    g_modules[idx].view.current_limit = current_a;
    /* Lianming: send voltage+current together when running */
    if (g_modules[idx].view.state == CHG_STATE_RUNNING) {
        lm_set_output(idx, g_modules[idx].view.voltage, current_a);
    }
    return true;
}

static bool lm_start(uint8_t idx)
{
    extern uint32_t HAL_GetTick(void);
    uint32_t now = HAL_GetTick();
    if (idx >= g_module_count || !g_modules[idx].view.enabled) return false;
    g_modules[idx].should_run = true;
    if (g_modules[idx].view.state == CHG_STATE_IDLE) {
        set_state(&g_modules[idx], CHG_STATE_STARTING, now);
    }
    return true;
}

static bool lm_stop(uint8_t idx)
{
    extern uint32_t HAL_GetTick(void);
    uint32_t now = HAL_GetTick();
    if (idx >= g_module_count || !g_modules[idx].view.enabled) return false;
    g_modules[idx].should_run = false;
    lm_stop_module(idx);
    set_state(&g_modules[idx], CHG_STATE_IDLE, now);
    return true;
}

static void lm_set_voltage_all(float voltage_v)
{
    for (uint8_t i = 0; i < g_module_count; i++) {
        (void)lm_set_voltage(i, voltage_v);
    }
}

static void lm_set_current_limit_all(float current_a)
{
    for (uint8_t i = 0; i < g_module_count; i++) {
        (void)lm_set_current_limit(i, current_a);
    }
}

static void lm_start_all(void)
{
    for (uint8_t i = 0; i < g_module_count; i++) {
        (void)lm_start(i);
    }
}

static void lm_stop_all(void)
{
    for (uint8_t i = 0; i < g_module_count; i++) {
        (void)lm_stop(i);
    }
}

static void lm_emergency_stop(void)
{
    extern uint32_t HAL_GetTick(void);
    uint32_t now = HAL_GetTick();
    for (uint8_t i = 0; i < g_module_count; i++) {
        if (!g_modules[i].view.enabled) continue;
        g_modules[i].should_run = false;
        lm_stop_module(i);
        set_state(&g_modules[i], CHG_STATE_IDLE, now);
    }
}

static void lm_process(uint32_t now)
{
    if (g_module_count == 0U) return;
    process_module(g_rr_index, now);
    g_rr_index = (uint8_t)((g_rr_index + 1U) % g_module_count);
}

static void lm_feed_frame(uint32_t ext_id, const uint8_t *data, uint8_t dlc)
{
    if (dlc < 8U || data == 0) return;

    /* Response ID format: 0x1807C0X where X = module address */
    uint8_t src_addr = lm_parse_response_addr(ext_id);
    uint8_t idx = find_by_addr(src_addr);
    if (idx == 0xFFU) return;

    extern uint32_t HAL_GetTick(void);
    uint32_t now = HAL_GetTick();

    if (data[1] != LM_RESP_OK) {
        g_modules[idx].view.stats.error_count++;
        return;
    }

    g_modules[idx].view.stats.rx_count++;

    /* Response format: 0x1807C0X where X = module address
     * Data: CMD(1) | Status(1) | Current(2) | Voltage(2) | Status(2) */
    apply_status(idx, data, now);
}

static void lm_get_system_summary(CHG_SystemSummary_t *summary)
{
    if (summary == 0) return;
    memset(summary, 0, sizeof(*summary));

    for (uint8_t i = 0; i < g_module_count; i++) {
        const CHG_ModuleView_t *m = &g_modules[i].view;
        if (!m->enabled) continue;
        if (m->online) {
            summary->modules_online++;
            summary->total_current += m->current;
            summary->total_power_in += (float)m->input_power;
            if (summary->voltage == 0.0f && m->voltage > 0.0f) {
                summary->voltage = m->voltage;
            }
        }
        if (m->alarm_flags != CHG_ALARM_NONE) {
            summary->any_critical = true;
            summary->modules_fault++;
        }
    }
}

static uint8_t lm_get_module_count(void)
{
    return g_module_count;
}

static bool lm_get_module_view(uint8_t idx, CHG_ModuleView_t *view)
{
    if (idx >= g_module_count || view == 0) return false;
    *view = g_modules[idx].view;
    return true;
}

static const CHG_DriverOps_t g_lm_ops = {
    .name = "lianming",
    .init = lm_init,
    .add_module = lm_add_module,
    .remove_module = lm_remove_module,
    .set_voltage = lm_set_voltage,
    .set_current_limit = lm_set_current_limit,
    .start = lm_start,
    .stop = lm_stop,
    .set_voltage_all = lm_set_voltage_all,
    .set_current_limit_all = lm_set_current_limit_all,
    .start_all = lm_start_all,
    .stop_all = lm_stop_all,
    .emergency_stop = lm_emergency_stop,
    .process = lm_process,
    .feed_frame = lm_feed_frame,
    .get_system_summary = lm_get_system_summary,
    .get_module_count = lm_get_module_count,
    .get_module_view = lm_get_module_view,
};

const CHG_DriverOps_t *CHG_LianmingDriverOps(void)
{
    return &g_lm_ops;
}
