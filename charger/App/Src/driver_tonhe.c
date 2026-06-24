/**
 * @file driver_tonhe.c
 * @brief TonHe V1.3 Charging Module Driver - J1939-based CAN protocol
 * @note Protocol: SAE J1939-21, CAN 2.0B Extended Frame, 125Kbps
 *
 * Hardware Interface:
 *   - CAN bus: 125 Kbps, Extended 29-bit frame
 *   - Isolation: Required (isolated CAN transceiver)
 *
 * CAN Frame Format (29-bit Extended ID - J1939):
 *   | Bit 28-26 | Bit 25 | Bit 24 | Bit 23-16 | Bit 15-8 | Bit 7-0 |
 *   | Priority  | R      | DP     | PF         | PS       | SA      |
 *   | 0-7       | 0      | 0      | PDU Format | PDU Specific| Source |
 *
 * PDU Format (PF) defines message type:
 *   - PF < 240: PDU1 format (PS = Destination Address)
 *   - PF >= 240: PDU2 format (PS = Group Extension)
 *
 * Address:
 *   - Controller: 0xA0 (fixed)
 *   - Module: 0x01 - 0xF0 (1-240)
 *   - Broadcast: 0xFF
 *
 * Parameter Group Numbers (PGN):
 *   Uplink (Module -> Controller):
 *   - 0x000100: M_C_1 - Charging module status
 *   - 0x000200: M_C_2 - Start/stop confirmation
 *   - 0x000B00: M_C_3 - AC phase information
 *   - 0x009100: M_C_4 - Extended status/fault
 *
 *   Downlink (Controller -> Module):
 *   - 0x000300: C_M_1 - Broadcast start/stop
 *   - 0x000400: C_M_2 - Broadcast parameter set
 *   - 0x000600: C_M_24 - Specific module start/stop
 *
 * Data Format (M_C_1 - Status):
 *   | Byte 0    | Byte 1-2      | Byte 3-4     | Byte 5-6    | Byte 7   |
 *   | Status    | Voltage(0.1V)| Current(0.01A)| Fault/Warn | PFC Fault |
 *   | 0x00/0x01| 0x0FA0 (400V) | 0x2710 (100A)| Bit field  | Bit field |
 *
 * Control Commands:
 *   - Start: 0xAA
 *   - Stop:  0x55
 *
 * References:
 *   - "TonHeCANcommunicationbetweenchargingmoduleandmonitor TONHE V1.3.pdf"
 *   - "Tonhe.md" (Vietnamese translation)
 *
 * Example:
 *   Module status (400V, 100A, no fault):
 *     ID: 0x1801A001 (Priority=6, PF=0x01, PS=0xA0, SA=0x01)
 *     Data: 01 A0 0F 10 27 00 00 00
 *
 *   Start modules 1,2,3:
 *     ID: 0x0C03FFA0 (Priority=3, PF=0x03, PS=0xFF, SA=0xA0)
 *     Data: 07 00 00 AA 00 00 00 00
 */

#include "driver_tonhe.h"
#include "charger_protocol.h"
#include "bsp_can.h"
#include <string.h>

/* ============== Private Types ============== */

typedef struct {
    CHG_ModuleView_t view;
    uint8_t group;
    uint8_t poll_step;
    uint8_t retry_count;
    bool should_run;
    uint32_t last_tx_tick;
    uint32_t confirm_tick;
    bool waiting_confirm;
} TONHE_Internal_t;

/* ============== Private State ============== */

static TONHE_Internal_t g_modules[TONHE_MAX_MODULES];
static uint8_t g_module_count = 0;
static uint8_t g_current_idx = 0;

/* ============== CAN ID Builders ============== */

static uint32_t tonhe_build_id(uint8_t pf, uint8_t ps, uint8_t sa, uint8_t priority)
{
    return (((uint32_t)priority & 0x07U) << 26) |
           (((uint32_t)pf & 0xFFU) << 16) |
           (((uint32_t)ps & 0xFFU) << 8) |
           ((uint32_t)sa & 0xFFU);
}

/* Uplink IDs - currently not used but may be needed for future */
/* static uint32_t tonhe_status_id(uint8_t module_addr) { ... } */
/* static uint32_t tonhe_confirm_id(uint8_t module_addr) { ... } */
/* static uint32_t tonhe_ac_phase_id(uint8_t module_addr) { ... } */
/* static uint32_t tonhe_extended_id(uint8_t module_addr) { ... } */

/* Downlink IDs */
/* Broadcast command - currently not used */
/* static uint32_t tonhe_broadcast_cmd_id(void) { ... } */

static uint32_t tonhe_param_set_id(void)
{
    return tonhe_build_id(0x04, TONHE_ADDR_BROADCAST, TONHE_ADDR_CONTROLLER, 4);
}

static uint32_t tonhe_specific_cmd_id(uint8_t module_addr)
{
    return tonhe_build_id(0x06, module_addr, TONHE_ADDR_CONTROLLER, TONHE_PRIORITY_CMD);
}

/* ============== Helpers ============== */

static uint32_t now_tick(void)
{
    extern uint32_t HAL_GetTick(void);
    return HAL_GetTick();
}

/* ============== Alarm Parsing ============== */

static CHG_AlarmFlag_t tonhe_parse_fault(uint16_t fault_bits, uint8_t pfc_bits)
{
    CHG_AlarmFlag_t flags = CHG_ALARM_NONE;

    /* Byte 6-7: Fault/Warning bits */
    if (fault_bits & (1U << 0)) flags |= CHG_ALARM_AC_UNDER_VOLT;      /* Input undervoltage */
    if (fault_bits & (1U << 2)) flags |= CHG_ALARM_OVER_VOLTAGE_OUT;    /* Input overvoltage */
    if (fault_bits & (1U << 3)) flags |= CHG_ALARM_OVER_VOLTAGE_OUT;    /* Output overvoltage */
    if (fault_bits & (1U << 4)) flags |= CHG_ALARM_HW_FAULT;           /* Output overcurrent */
    if (fault_bits & (1U << 5)) flags |= CHG_ALARM_OVER_TEMP;          /* Temperature high */
    if (fault_bits & (1U << 6)) flags |= CHG_ALARM_HW_FAULT;           /* Fan fault */
    if (fault_bits & (1U << 7)) flags |= CHG_ALARM_HW_FAULT;           /* Hardware fault */
    if (fault_bits & (1U << 8)) flags |= CHG_ALARM_HW_FAULT;           /* Bus exception */
    if (fault_bits & (1U << 9)) flags |= CHG_ALARM_COMM_FAIL;          /* SCI communication */
    if (fault_bits & (1U << 10)) flags |= CHG_ALARM_HW_FAULT;         /* Discharge fault */
    if (fault_bits & (1U << 11)) flags |= CHG_ALARM_HW_FAULT;         /* PFC shutdown */
    if (fault_bits & (1U << 15)) flags |= CHG_ALARM_SHORT_CIRCUIT;    /* Short circuit */

    /* PFC fault byte 8 */
    if (pfc_bits & (1U << 0)) flags |= CHG_ALARM_HW_FAULT;            /* Input overcurrent */
    if (pfc_bits & (1U << 7)) flags |= CHG_ALARM_HW_FAULT;           /* Bus overvoltage */

    return flags;
}

static CHG_AlarmFlag_t tonhe_parse_extended_fault(uint16_t ext_bits)
{
    CHG_AlarmFlag_t flags = CHG_ALARM_NONE;

    if (ext_bits & (1U << 2)) flags |= CHG_ALARM_COMM_FAIL;           /* CAN timeout */
    if (ext_bits & (1U << 6)) flags |= CHG_ALARM_OVER_TEMP;           /* Internal overtemperature */
    if (ext_bits & (1U << 7)) flags |= CHG_ALARM_OVER_TEMP;           /* Air inlet overtemperature */
    if (ext_bits & (1U << 13)) flags |= CHG_ALARM_HW_FAULT;          /* Emergency stop */

    return flags;
}

/* ============== Command Senders ============== */

static void send_specific_start_stop(TONHE_Internal_t *mod, bool start)
{
    BSP_CAN_Frame_t frame;
    frame.ext_id = tonhe_specific_cmd_id(mod->view.addr);
    frame.dlc = 8;

    /* Byte 1: Start/Stop */
    frame.data[0] = start ? TONHE_CMD_START : TONHE_CMD_STOP;
    /* Byte 2: Mode (standby) */
    frame.data[1] = TONHE_MODE_STANDBY;
    /* Byte 3-4: Voltage (0.1V/bit) */
    uint16_t voltage_raw = (uint16_t)(mod->view.voltage / TONHE_VOLTAGE_SCALE);
    frame.data[2] = (uint8_t)(voltage_raw & 0xFF);
    frame.data[3] = (uint8_t)((voltage_raw >> 8) & 0xFF);
    /* Byte 5-6: Current (0.01A/bit) */
    uint16_t current_raw = (uint16_t)(mod->view.current_limit * 100.0f / TONHE_CURRENT_SCALE);
    frame.data[4] = (uint8_t)(current_raw & 0xFF);
    frame.data[5] = (uint8_t)((current_raw >> 8) & 0xFF);
    /* Byte 7-8: Reserved */
    frame.data[6] = 0x00;
    frame.data[7] = 0x00;

    if (BSP_CAN_Transmit(&frame)) {
        mod->view.stats.tx_count++;
        mod->last_tx_tick = now_tick();
        mod->waiting_confirm = true;
        mod->confirm_tick = now_tick();
    }
}

static void send_param_set(TONHE_Internal_t *mod)
{
    BSP_CAN_Frame_t frame;
    frame.ext_id = tonhe_param_set_id();
    frame.dlc = 8;

    /* Byte 1-3: Processing flag (all modules) */
    frame.data[0] = 0xFF;
    frame.data[1] = 0xFF;
    frame.data[2] = 0xFF;
    /* Byte 4: Group + Multiple (0 = default) */
    frame.data[3] = 0x00;
    /* Byte 5-6: Voltage */
    uint16_t voltage_raw = (uint16_t)(mod->view.voltage / TONHE_VOLTAGE_SCALE);
    frame.data[4] = (uint8_t)(voltage_raw & 0xFF);
    frame.data[5] = (uint8_t)((voltage_raw >> 8) & 0xFF);
    /* Byte 7-8: Current */
    uint16_t current_raw = (uint16_t)(mod->view.current_limit * 100.0f / TONHE_CURRENT_SCALE);
    frame.data[6] = (uint8_t)(current_raw & 0xFF);
    frame.data[7] = (uint8_t)((current_raw >> 8) & 0xFF);

    if (BSP_CAN_Transmit(&frame)) {
        mod->view.stats.tx_count++;
        mod->last_tx_tick = now_tick();
    }
}

/* ============== Message Parsers ============== */

static void parse_status(const uint8_t *data, uint8_t src_addr, uint32_t now)
{
    TONHE_Internal_t *mod = NULL;

    /* Find module by address */
    for (uint8_t i = 0; i < g_module_count; i++) {
        if (g_modules[i].view.enabled && g_modules[i].view.addr == src_addr) {
            mod = &g_modules[i];
            break;
        }
    }
    if (mod == NULL) return;

    /* Byte 1: Module status */
    uint8_t status = data[0];
    if (status == TONHE_STATUS_ON) {
        mod->view.running = true;
    } else {
        mod->view.running = false;
    }

    /* Byte 2-3: Output voltage (0.1V/bit, LE) */
    uint16_t voltage_raw = ((uint16_t)data[2] | ((uint16_t)data[3] << 8));
    mod->view.voltage = (float)voltage_raw * TONHE_VOLTAGE_SCALE;

    /* Byte 4-5: Output current (0.01A/bit, LE) */
    uint16_t current_raw = ((uint16_t)data[4] | ((uint16_t)data[5] << 8));
    mod->view.current = (float)current_raw * TONHE_CURRENT_SCALE;

    /* Byte 6-7: Fault/Warning */
    uint16_t fault_bits = ((uint16_t)data[6] | ((uint16_t)data[7] << 8));
    mod->view.alarm_status = fault_bits;

    /* Byte 8: PFC Fault */
    uint8_t pfc_bits = data[7];
    mod->view.alarm_flags = tonhe_parse_fault(fault_bits, pfc_bits);

    /* Update online status */
    mod->view.online = true;
    mod->view.last_rx_tick = now;
    mod->view.stats.rx_count++;

    /* Update state based on status */
    if (status == TONHE_STATUS_FAULT_OFF) {
        mod->view.state = CHG_STATE_FAULT;
    } else if (status == TONHE_STATUS_ON && mod->should_run) {
        mod->view.state = CHG_STATE_RUNNING;
    } else if (status == TONHE_STATUS_NORMAL_OFF) {
        mod->view.state = CHG_STATE_IDLE;
    }
}

static void parse_confirm(const uint8_t *data, uint8_t src_addr, uint32_t now)
{
    TONHE_Internal_t *mod = NULL;

    for (uint8_t i = 0; i < g_module_count; i++) {
        if (g_modules[i].view.enabled && g_modules[i].view.addr == src_addr) {
            mod = &g_modules[i];
            break;
        }
    }
    if (mod == NULL) return;

    /* Byte 1: Command received (0x01 = success) */
    if (data[0] == 0x01) {
        mod->waiting_confirm = false;
        mod->view.stats.recovery_count++;
        if (mod->should_run && mod->view.state == CHG_STATE_STARTING) {
            mod->view.state = CHG_STATE_RUNNING;
        }
    } else {
        mod->view.stats.error_count++;
    }

    mod->view.last_rx_tick = now;
    mod->view.stats.rx_count++;
}

static void parse_ac_phase(const uint8_t *data, uint8_t src_addr, uint32_t now)
{
    TONHE_Internal_t *mod = NULL;

    for (uint8_t i = 0; i < g_module_count; i++) {
        if (g_modules[i].view.enabled && g_modules[i].view.addr == src_addr) {
            mod = &g_modules[i];
            break;
        }
    }
    if (mod == NULL) return;

    /* Byte 1-2: A-phase voltage */
    uint16_t va = ((uint16_t)data[0] | ((uint16_t)data[1] << 8));
    (void)va; /* Not stored in standard view */

    /* Byte 3-4: B-phase voltage */
    uint16_t vb = ((uint16_t)data[2] | ((uint16_t)data[3] << 8));
    (void)vb;

    /* Byte 5-6: C-phase voltage */
    uint16_t vc = ((uint16_t)data[4] | ((uint16_t)data[5] << 8));
    (void)vc;

    /* Byte 7-8: Ambient temperature */
    uint16_t temp_raw = ((uint16_t)data[6] | ((uint16_t)data[7] << 8));
    mod->view.temp_ambient = (float)temp_raw * TONHE_TEMP_SCALE;

    mod->view.last_rx_tick = now;
    mod->view.stats.rx_count++;
}

static void parse_extended(const uint8_t *data, uint8_t src_addr, uint32_t now)
{
    TONHE_Internal_t *mod = NULL;

    for (uint8_t i = 0; i < g_module_count; i++) {
        if (g_modules[i].view.enabled && g_modules[i].view.addr == src_addr) {
            mod = &g_modules[i];
            break;
        }
    }
    if (mod == NULL) return;

    /* Byte 1-2: Extended fault */
    uint16_t ext_fault = ((uint16_t)data[2] | ((uint16_t)data[3] << 8));
    mod->view.alarm_flags |= tonhe_parse_extended_fault(ext_fault);

    mod->view.last_rx_tick = now;
    mod->view.stats.rx_count++;
}

/* ============== State Machine ============== */

static void set_state(TONHE_Internal_t *mod, CHG_ModuleState_t st, uint32_t now)
{
    mod->view.state = st;
    mod->retry_count = 0;
}

static void process_module(TONHE_Internal_t *mod, uint32_t now)
{
    if (!mod->view.enabled) return;

    uint32_t since_rx = now - mod->view.last_rx_tick;

    switch (mod->view.state) {

    case CHG_STATE_IDLE:
        /* Wait for start command */
        if (mod->should_run) {
            /* Send parameter first */
            send_param_set(mod);
            /* Then send start command */
            send_specific_start_stop(mod, true);
            set_state(mod, CHG_STATE_STARTING, now);
        }
        break;

    case CHG_STATE_STARTING:
        /* Wait for confirm, retry if needed */
        if (mod->waiting_confirm && (now - mod->confirm_tick) > TONHE_CONFIRM_TIMEOUT_MS) {
            if (mod->retry_count < 3) {
                send_specific_start_stop(mod, true);
                mod->retry_count++;
            } else {
                /* Timeout, go to offline */
                set_state(mod, CHG_STATE_OFFLINE, now);
            }
        }
        /* If confirm received, should be in RUNNING */
        if (mod->view.state == CHG_STATE_STARTING && !mod->waiting_confirm) {
            mod->view.state = CHG_STATE_RUNNING;
        }
        break;

    case CHG_STATE_RUNNING:
        /* Monitor for offline */
        if (since_rx > TONHE_OFFLINE_TIMEOUT_MS) {
            mod->view.stats.timeout_count++;
            mod->view.online = false;
            set_state(mod, CHG_STATE_OFFLINE, now);
        }
        /* Handle stop command */
        if (!mod->should_run) {
            send_specific_start_stop(mod, false);
            set_state(mod, CHG_STATE_IDLE, now);
        }
        break;

    case CHG_STATE_OFFLINE:
        /* Try to recover */
        if ((now - mod->last_tx_tick) > TONHE_RECOVERY_DELAY_MS) {
            send_param_set(mod);
            set_state(mod, CHG_STATE_RECOVERING, now);
        }
        break;

    case CHG_STATE_RECOVERING:
        /* Wait for response */
        if (since_rx < TONHE_OFFLINE_TIMEOUT_MS) {
            /* Recovered! */
            mod->view.stats.recovery_count++;
            if (mod->should_run) {
                send_specific_start_stop(mod, true);
                set_state(mod, CHG_STATE_STARTING, now);
            } else {
                set_state(mod, CHG_STATE_IDLE, now);
            }
        }
        if ((now - mod->last_tx_tick) > TONHE_CONFIRM_TIMEOUT_MS && mod->retry_count >= 3) {
            set_state(mod, CHG_STATE_OFFLINE, now);
        }
        break;

    case CHG_STATE_FAULT:
        /* Try to recover when alarm clears */
        if (mod->view.alarm_flags == CHG_ALARM_NONE) {
            if (mod->should_run) {
                send_specific_start_stop(mod, true);
                set_state(mod, CHG_STATE_STARTING, now);
            } else {
                set_state(mod, CHG_STATE_IDLE, now);
            }
        }
        /* Also check for offline */
        if (since_rx > TONHE_OFFLINE_TIMEOUT_MS) {
            set_state(mod, CHG_STATE_OFFLINE, now);
        }
        break;
    }
}

/* ============== CHG_DriverOps_t Implementation ============== */

static void tonhe_init(void)
{
    memset(g_modules, 0, sizeof(g_modules));
    g_module_count = 0;
    g_current_idx = 0;
}

static int8_t tonhe_add_module(uint8_t addr, uint8_t group)
{
    if (g_module_count >= TONHE_MAX_MODULES) return -1;
    if (addr < TONHE_MODULE_MIN_ADDR || addr > TONHE_MODULE_MAX_ADDR) return -1;

    TONHE_Internal_t *mod = &g_modules[g_module_count];
    memset(mod, 0, sizeof(*mod));

    mod->view.addr = addr;
    mod->view.group = group;
    mod->view.enabled = true;
    mod->view.state = CHG_STATE_IDLE;
    mod->view.current_limit = 1.0f;
    mod->view.voltage = 0.0f;
    mod->group = group;
    mod->should_run = false;
    mod->waiting_confirm = false;

    return (int8_t)(g_module_count++);
}

static void tonhe_remove_module(uint8_t idx)
{
    if (idx >= g_module_count) return;
    g_modules[idx].view.enabled = false;
    g_modules[idx].view.state = CHG_STATE_IDLE;
}

static bool tonhe_set_voltage(uint8_t idx, float voltage_v)
{
    if (idx >= g_module_count || !g_modules[idx].view.enabled) return false;

    /* Clamp to limits */
    if (voltage_v > TONHE_MAX_OUTPUT_VOLTAGE_V) voltage_v = TONHE_MAX_OUTPUT_VOLTAGE_V;
    if (voltage_v < 0) voltage_v = 0;

    g_modules[idx].view.voltage = voltage_v;

    /* If running, send new param immediately */
    if (g_modules[idx].view.state == CHG_STATE_RUNNING) {
        send_param_set(&g_modules[idx]);
    }
    return true;
}

static bool tonhe_set_current_limit(uint8_t idx, float current_a)
{
    if (idx >= g_module_count || !g_modules[idx].view.enabled) return false;
    if (current_a < 0.0f) current_a = 0.0f;
    if (current_a > TONHE_MAX_OUTPUT_CURRENT_A) current_a = TONHE_MAX_OUTPUT_CURRENT_A;

    g_modules[idx].view.current_limit = current_a; /* Store actual Amps */

    if (g_modules[idx].view.state == CHG_STATE_RUNNING) {
        send_param_set(&g_modules[idx]);
    }
    return true;
}

static bool tonhe_start(uint8_t idx)
{
    if (idx >= g_module_count || !g_modules[idx].view.enabled) return false;
    g_modules[idx].should_run = true;
    return true;
}

static bool tonhe_stop(uint8_t idx)
{
    if (idx >= g_module_count || !g_modules[idx].view.enabled) return false;
    g_modules[idx].should_run = false;
    if (g_modules[idx].view.state == CHG_STATE_RUNNING) {
        send_specific_start_stop(&g_modules[idx], false);
    }
    return true;
}

static void tonhe_set_voltage_all(float voltage_v)
{
    for (uint8_t i = 0; i < g_module_count; i++) {
        (void)tonhe_set_voltage(i, voltage_v);
    }
}

static void tonhe_set_current_limit_all(float ratio)
{
    for (uint8_t i = 0; i < g_module_count; i++) {
        (void)tonhe_set_current_limit(i, ratio);
    }
}

static void tonhe_start_all(void)
{
    for (uint8_t i = 0; i < g_module_count; i++) {
        (void)tonhe_start(i);
    }
}

static void tonhe_stop_all(void)
{
    for (uint8_t i = 0; i < g_module_count; i++) {
        (void)tonhe_stop(i);
    }
}

static void tonhe_emergency_stop(void)
{
    uint32_t now = now_tick();
    for (uint8_t i = 0; i < g_module_count; i++) {
        if (!g_modules[i].view.enabled) continue;
        g_modules[i].should_run = false;
        send_specific_start_stop(&g_modules[i], false);
        set_state(&g_modules[i], CHG_STATE_IDLE, now);
    }
}

static void tonhe_process(uint32_t now)
{
    if (g_module_count == 0) return;
    TONHE_Internal_t *mod = &g_modules[g_current_idx];
    if (mod->view.enabled) {
        process_module(mod, now);
    }
    g_current_idx = (g_current_idx + 1) % g_module_count;
}

static void tonhe_feed_frame(uint32_t ext_id, const uint8_t *data, uint8_t dlc)
{
    if (dlc < 8 || data == NULL) return;

    /* Extract source address (SA) = lower 8 bits */
    uint8_t src_addr = (uint8_t)(ext_id & 0xFF);

    /* Extract PGN (PF << 8 | PS, masked) */
    uint16_t pf = (uint16_t)((ext_id >> 16) & 0xFF);
    uint16_t ps = (uint16_t)((ext_id >> 8) & 0xFF);
    uint16_t pgn = (pf << 8) | ps;

    uint32_t now = now_tick();

    switch (pgn) {
    case 0x0001:  /* M_C_1: Status */
        parse_status(data, src_addr, now);
        break;
    case 0x0002:  /* M_C_2: Confirm */
        parse_confirm(data, src_addr, now);
        break;
    case 0x000B:  /* M_C_3: AC Phase */
        parse_ac_phase(data, src_addr, now);
        break;
    case 0x0091:  /* M_C_4: Extended */
        parse_extended(data, src_addr, now);
        break;
    default:
        break;
    }
}

static void tonhe_get_system_summary(CHG_SystemSummary_t *summary)
{
    if (summary == NULL) return;
    memset(summary, 0, sizeof(*summary));

    for (uint8_t i = 0; i < g_module_count; i++) {
        CHG_ModuleView_t *v = &g_modules[i].view;
        if (!v->enabled) continue;

        if (v->state == CHG_STATE_RUNNING || v->state == CHG_STATE_STARTING) {
            summary->modules_online++;
            summary->total_current += v->current;
            summary->total_power_in += v->voltage * v->current;
            if (summary->voltage == 0.0f && v->voltage > 0.0f) {
                summary->voltage = v->voltage;
            }
        }

        if (v->state == CHG_STATE_FAULT) {
            summary->modules_fault++;
            summary->any_critical = true;
        }

        if (v->alarm_flags != CHG_ALARM_NONE) {
            summary->any_critical = true;
        }
    }
}

static uint8_t tonhe_get_module_count(void)
{
    return g_module_count;
}

static bool tonhe_get_module_view(uint8_t idx, CHG_ModuleView_t *view)
{
    if (idx >= g_module_count || view == NULL) return false;
    *view = g_modules[idx].view;
    return true;
}

/* ============== Driver Table ============== */

static const CHG_DriverOps_t g_tonhe_ops = {
    .name = "tonhe",
    .init = tonhe_init,
    .add_module = tonhe_add_module,
    .remove_module = tonhe_remove_module,
    .set_voltage = tonhe_set_voltage,
    .set_current_limit = tonhe_set_current_limit,
    .start = tonhe_start,
    .stop = tonhe_stop,
    .set_voltage_all = tonhe_set_voltage_all,
    .set_current_limit_all = tonhe_set_current_limit_all,
    .start_all = tonhe_start_all,
    .stop_all = tonhe_stop_all,
    .emergency_stop = tonhe_emergency_stop,
    .process = tonhe_process,
    .feed_frame = tonhe_feed_frame,
    .get_system_summary = tonhe_get_system_summary,
    .get_module_count = tonhe_get_module_count,
    .get_module_view = tonhe_get_module_view,
};

const CHG_DriverOps_t *CHG_TonheDriverOps(void)
{
    return &g_tonhe_ops;
}
