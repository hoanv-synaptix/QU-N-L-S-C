/**
 * @file    maxwell_charger.c
 * @brief   Maxwell MXR Series - Multi-module driver implementation
 * @version 2.0
 * @note    Xử lý: poll round-robin, timeout detection, auto-recovery FSM
 */

#include "maxwell_charger.h"
#include "bsp_can.h"
#include "charger_protocol.h"
#include <string.h>

/* ============== Private State ============== */

static MXR_Module_t  g_modules[MXR_MAX_MODULES];
static uint8_t       g_module_count = 0;
static uint8_t       g_current_idx  = 0;  /* Round-robin index */

/* Registers cần poll cho mỗi module */
#define MXR_POLL_COUNT  7U
static const uint16_t g_poll_regs[MXR_POLL_COUNT] = {
    CHG_REG_VOLTAGE,
    CHG_REG_CURRENT,
    CHG_REG_CURR_LIMIT,
    CHG_REG_TEMP_DCDC,
    CHG_REG_TEMP_AMBIENT,
    CHG_REG_ALARM_STATUS,
    CHG_REG_INPUT_POWER,
};
#define POLL_REG_COUNT  MXR_POLL_COUNT

/* ============== Byte Conversion Helpers ============== */

static void float_to_bytes_be(float val, uint8_t *buf)
{
    CHG_ProtocolFloatToBE(val, buf);
}

static float bytes_be_to_float(const uint8_t *buf)
{
    return CHG_ProtocolBEToFloat(buf);
}

static uint32_t bytes_be_to_u32(const uint8_t *buf)
{
    return CHG_ProtocolBEToU32(buf);
}

static void u32_to_bytes_be(uint32_t val, uint8_t *buf)
{
    CHG_ProtocolU32ToBE(val, buf);
}
/* ============== CAN Frame Helpers ============== */

uint32_t MXR_BuildFrameID(uint8_t dst_addr, uint8_t src_addr,
                           uint8_t ptp, uint8_t group)
{
    return CHG_ProtocolBuildCanId(dst_addr, src_addr, ptp, group);
}

/** Gửi command tới 1 module cụ thể */
static bool send_to_module(MXR_Module_t *mod, uint8_t func,
                           uint16_t reg, const uint8_t *payload4)
{
    BSP_CAN_Frame_t frame;
    frame.ext_id = CHG_ProtocolBuildCanId(mod->addr, CHG_PROTO_CAN_ADDR_CONTROLLER, CHG_PROTO_CAN_PTP_POINT, mod->group);
    frame.dlc = 8;
    frame.data[0] = func;
    frame.data[1] = 0x00;
    frame.data[2] = (uint8_t)(reg >> 8);
    frame.data[3] = (uint8_t)(reg & 0xFF);
    frame.data[4] = payload4[0];
    frame.data[5] = payload4[1];
    frame.data[6] = payload4[2];
    frame.data[7] = payload4[3];

    bool ok = BSP_CAN_Transmit(&frame);
    if (ok) {
        mod->stats.tx_count++;
    }
    return ok;
}

/** Gửi set float register */
static bool send_set_float(MXR_Module_t *mod, uint16_t reg, float val)
{
    uint8_t payload[4];
    float_to_bytes_be(val, payload);
    return send_to_module(mod, CHG_PROTO_CAN_FUNC_SET, reg, payload);
}

/** Gửi set int register */
static bool send_set_u32(MXR_Module_t *mod, uint16_t reg, uint32_t val)
{
    uint8_t payload[4];
    u32_to_bytes_be(val, payload);
    return send_to_module(mod, CHG_PROTO_CAN_FUNC_SET, reg, payload);
}

/** Gửi read register */
static bool send_read(MXR_Module_t *mod, uint16_t reg)
{
    uint8_t payload[4] = {0, 0, 0, 0};
    return send_to_module(mod, CHG_PROTO_CAN_FUNC_READ, reg, payload);
}

/* ============== FSM State Change ============== */

static void set_state(MXR_Module_t *mod, MXR_State_t new_state, uint32_t now)
{
    mod->state = new_state;
    mod->state_enter_tick = now;
    mod->retry_count = 0;
}

/* ============== Response Parsing ============== */

/** Tìm module theo source address trong CAN frame */
static MXR_Module_t *find_module_by_addr(uint8_t addr)
{
    for (uint8_t i = 0; i < g_module_count; i++) {
        if (g_modules[i].enabled && g_modules[i].addr == addr) {
            return &g_modules[i];
        }
    }
    return NULL;
}

/** Cập nhật status module từ response hợp lệ */
static CHG_AlarmFlag_t parse_maxwell_alarm(uint32_t raw_alarm)
{
    CHG_AlarmFlag_t flags = CHG_ALARM_NONE;
    if (raw_alarm & MXR_ALARM_MODULE_FAULT)    flags |= CHG_ALARM_HW_FAULT;
    if (raw_alarm & MXR_ALARM_SCI_FAILURE)     flags |= CHG_ALARM_COMM_FAIL;
    if (raw_alarm & MXR_ALARM_DCDC_OV)         flags |= CHG_ALARM_OVER_VOLTAGE_OUT;
    if (raw_alarm & MXR_ALARM_AC_UNDERVOLTAGE) flags |= CHG_ALARM_AC_UNDER_VOLT;
    if (raw_alarm & MXR_ALARM_CAN_FAILURE)     flags |= CHG_ALARM_COMM_FAIL;
    if (raw_alarm & MXR_ALARM_SHORT_CIRCUIT)   flags |= CHG_ALARM_SHORT_CIRCUIT;
    if (raw_alarm & MXR_ALARM_DCDC_OVERTEMP)   flags |= CHG_ALARM_OVER_TEMP;
    if (raw_alarm & MXR_ALARM_DCDC_OUTPUT_OV)  flags |= CHG_ALARM_OVER_VOLTAGE_OUT;
    return flags;
}

static void apply_response(MXR_Module_t *mod, const MXR_Response_t *resp, uint32_t now)
{
    if (resp->error_code != CHG_PROTO_CAN_RESP_OK) {
        return;
    }

    switch (resp->reg_number) {
        case CHG_REG_VOLTAGE:       mod->voltage       = resp->data.f_val; break;
        case CHG_REG_CURRENT:       mod->current       = resp->data.f_val; break;
        case CHG_REG_CURR_LIMIT:    mod->current_limit = resp->data.f_val; break;
        case CHG_REG_TEMP_DCDC:     mod->temp_dcdc     = resp->data.f_val; break;
        case CHG_REG_TEMP_AMBIENT:  mod->temp_ambient  = resp->data.f_val; break;
        case CHG_REG_ALARM_STATUS:  
            mod->alarm_status  = resp->data.u_val; 
            mod->alarm_flags   = parse_maxwell_alarm(mod->alarm_status);
            break;
        case CHG_REG_INPUT_POWER:   mod->input_power   = resp->data.u_val; break;
        default: break;
    }

    mod->last_rx_tick = now;
    mod->stats.rx_count++;

    /* Transition: nếu đang offline/recovering mà nhận response -> online */
    if (mod->state == MXR_STATE_OFFLINE || mod->state == MXR_STATE_RECOVERING) {
        mod->stats.recovery_count++;
        /* Nếu muốn chạy, vào STARTING để re-apply setpoint */
        if (mod->setpoint.should_run) {
            set_state(mod, MXR_STATE_STARTING, now);
        } else {
            set_state(mod, MXR_STATE_IDLE, now);
        }
    }

    /* Kiểm tra alarm critical */
    if (MXR_HasCriticalAlarm(mod) && mod->state == MXR_STATE_RUNNING) {
        set_state(mod, MXR_STATE_FAULT, now);
        /* Emergency: gửi stop ngay */
        send_set_u32(mod, CHG_REG_ON_OFF, 0x00010000);
    }
}
/* ============== Process FSM cho 1 module ============== */

/**
 * Xử lý logic cho 1 module: poll, detect timeout, recovery
 * Gọi mỗi khi đến lượt module này trong round-robin.
 */
static void process_module(MXR_Module_t *mod, uint32_t now)
{
    if (!mod->enabled) return;

    uint32_t now_tick = now;
    uint32_t since_rx = now - mod->last_rx_tick;
    uint32_t since_state = now - mod->state_enter_tick;

    switch (mod->state) {

    case MXR_STATE_IDLE:
        /* Poll để detect module có online không */
        send_read(mod, g_poll_regs[mod->poll_step]);
        mod->poll_step = (mod->poll_step + 1) % POLL_REG_COUNT;
        mod->last_tx_tick = now;

        /* Nếu có response gần đây -> module online, check setpoint */
        if (since_rx < MXR_OFFLINE_TIMEOUT_MS && mod->setpoint.should_run) {
            set_state(mod, MXR_STATE_STARTING, now);
        }
        break;

    case MXR_STATE_STARTING:
        /* Apply setpoint: voltage -> current -> start */
        if (mod->retry_count == 0) {
            send_set_float(mod, CHG_REG_SET_VOLTAGE, mod->setpoint.voltage_v);
            mod->retry_count++;
            mod->state_enter_tick = now_tick;
        } else if (mod->retry_count == 1 && (now_tick - mod->state_enter_tick) >= 50) {
            send_set_float(mod, CHG_REG_SET_CURR_LIMIT, mod->setpoint.current_limit);
            mod->retry_count++;
            mod->state_enter_tick = now_tick;
        } else if (mod->retry_count == 2 && (now_tick - mod->state_enter_tick) >= 50) {
            send_set_u32(mod, CHG_REG_ON_OFF, 0x00000000); /* Start */
            mod->retry_count = 3;
            mod->last_tx_tick = now;
        } else if (mod->retry_count >= 3 && (now - mod->last_tx_tick) >= 100) {
            /* Đã gửi xong startup sequence, chuyển sang running */
            set_state(mod, MXR_STATE_RUNNING, now);
        }
        break;

    case MXR_STATE_RUNNING:
        /* Poll liên tục (keep-alive + đọc status) */
        send_read(mod, g_poll_regs[mod->poll_step]);
        mod->poll_step = (mod->poll_step + 1) % POLL_REG_COUNT;
        mod->last_tx_tick = now;

        /* Detect offline */
        if (since_rx > MXR_OFFLINE_TIMEOUT_MS) {
            mod->stats.timeout_count++;
            set_state(mod, MXR_STATE_OFFLINE, now);
        }

        /* Detect user muốn stop */
        if (!mod->setpoint.should_run) {
            send_set_u32(mod, CHG_REG_ON_OFF, 0x00010000);
            set_state(mod, MXR_STATE_IDLE, now);
        }
        break;

    case MXR_STATE_OFFLINE:
        /* Thử gửi read để detect module quay lại */
        if (since_state > MXR_RECOVERY_DELAY_MS) {
            set_state(mod, MXR_STATE_RECOVERING, now);
        }
        break;

    case MXR_STATE_RECOVERING:
        /* Gửi read request, đợi response */
        send_read(mod, CHG_REG_ALARM_STATUS);
        mod->last_tx_tick = now;
        mod->retry_count++;

        if (mod->retry_count >= MXR_MAX_RETRIES) {
            /* Vẫn không response, quay lại OFFLINE chờ thêm */
            set_state(mod, MXR_STATE_OFFLINE, now);
        }
        break;

    case MXR_STATE_FAULT:
        /* Chờ alarm tự clear (module tự phục hồi) */
        /* Vẫn poll để biết khi nào alarm hết */
        send_read(mod, CHG_REG_ALARM_STATUS);
        mod->last_tx_tick = now;

        /* Nếu alarm đã clear -> thử start lại */
        if (!(mod->alarm_flags & (CHG_ALARM_HW_FAULT | CHG_ALARM_OVER_VOLTAGE_OUT | CHG_ALARM_SHORT_CIRCUIT | CHG_ALARM_OVER_TEMP | CHG_ALARM_COMM_FAIL)) &&
            since_rx < MXR_OFFLINE_TIMEOUT_MS) {
            if (mod->setpoint.should_run) {
                set_state(mod, MXR_STATE_STARTING, now);
            } else {
                set_state(mod, MXR_STATE_IDLE, now);
            }
        }

        /* Nếu mất liên lạc luôn */
        if (since_rx > MXR_OFFLINE_TIMEOUT_MS) {
            set_state(mod, MXR_STATE_OFFLINE, now);
        }
        break;
    }
}
/* ============== Public API Implementation ============== */

void MXR_Init(void)
{
    memset(g_modules, 0, sizeof(g_modules));
    g_module_count = 0;
    g_current_idx  = 0;
}

int8_t MXR_AddModule(uint8_t addr, uint8_t group)
{
    if (g_module_count >= MXR_MAX_MODULES) return -1;

    MXR_Module_t *mod = &g_modules[g_module_count];
    memset(mod, 0, sizeof(MXR_Module_t));
    mod->addr    = addr;
    mod->group   = group;
    mod->enabled = true;
    mod->state   = MXR_STATE_IDLE;
    mod->setpoint.voltage_v     = 0.0f;
    mod->setpoint.current_limit = 1.0f;
    mod->setpoint.should_run    = false;

    return (int8_t)(g_module_count++);
}

void MXR_RemoveModule(uint8_t idx)
{
    if (idx >= g_module_count) return;

    MXR_Module_t *mod = &g_modules[idx];
    /* Stop nếu đang chạy */
    if (mod->state == MXR_STATE_RUNNING || mod->state == MXR_STATE_STARTING) {
        send_set_u32(mod, CHG_REG_ON_OFF, 0x00010000);
    }
    mod->enabled = false;
    mod->state   = MXR_STATE_IDLE;
}

const MXR_Module_t *MXR_GetModule(uint8_t idx)
{
    if (idx >= g_module_count) return NULL;
    return &g_modules[idx];
}

uint8_t MXR_GetModuleCount(void)
{
    return g_module_count;
}

/* --- Per-module control --- */

bool MXR_SetVoltage(uint8_t idx, float voltage_v)
{
    if (idx >= g_module_count || !g_modules[idx].enabled) return false;
    g_modules[idx].setpoint.voltage_v = voltage_v;
    /* Gửi ngay nếu đang running */
    if (g_modules[idx].state == MXR_STATE_RUNNING) {
        return send_set_float(&g_modules[idx], CHG_REG_SET_VOLTAGE, voltage_v);
    }
    return true;
}

bool MXR_SetCurrentLimit(uint8_t idx, float ratio)
{
    if (idx >= g_module_count || !g_modules[idx].enabled) return false;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    g_modules[idx].setpoint.current_limit = ratio;
    if (g_modules[idx].state == MXR_STATE_RUNNING) {
        return send_set_float(&g_modules[idx], CHG_REG_SET_CURR_LIMIT, ratio);
    }
    return true;
}

bool MXR_Start(uint8_t idx)
{
    if (idx >= g_module_count || !g_modules[idx].enabled) return false;
    g_modules[idx].setpoint.should_run = true;
    /* FSM sẽ tự chuyển sang STARTING ở process tiếp theo */
    return true;
}

bool MXR_Stop(uint8_t idx)
{
    if (idx >= g_module_count || !g_modules[idx].enabled) return false;
    g_modules[idx].setpoint.should_run = false;
    /* Gửi stop ngay nếu đang running */
    if (g_modules[idx].state == MXR_STATE_RUNNING ||
        g_modules[idx].state == MXR_STATE_STARTING) {
        send_set_u32(&g_modules[idx], CHG_REG_ON_OFF, 0x00010000);
    }
    return true;
}

/* --- System-wide control --- */

void MXR_SetVoltageAll(float voltage_v)
{
    for (uint8_t i = 0; i < g_module_count; i++) {
        MXR_SetVoltage(i, voltage_v);
    }
}

void MXR_SetCurrentLimitAll(float ratio)
{
    for (uint8_t i = 0; i < g_module_count; i++) {
        MXR_SetCurrentLimit(i, ratio);
    }
}

void MXR_StartAll(void)
{
    for (uint8_t i = 0; i < g_module_count; i++) {
        MXR_Start(i);
    }
}

void MXR_StopAll(void)
{
    for (uint8_t i = 0; i < g_module_count; i++) {
        MXR_Stop(i);
    }
}

void MXR_EmergencyStop(void)
{
    for (uint8_t i = 0; i < g_module_count; i++) {
        if (!g_modules[i].enabled) continue;
        g_modules[i].setpoint.should_run = false;
        send_set_u32(&g_modules[i], CHG_REG_ON_OFF, 0x00010000);
        set_state(&g_modules[i], MXR_STATE_IDLE, g_modules[i].last_rx_tick);
    }
}

/* --- Runtime processing --- */

void MXR_Process(uint32_t now_tick)
{
    if (g_module_count == 0) return;

    /* Round-robin: xử lý 1 module mỗi lần gọi */
    MXR_Module_t *mod = &g_modules[g_current_idx];
    if (mod->enabled) {
        process_module(mod, now_tick);
    }

    g_current_idx = (g_current_idx + 1) % g_module_count;
}

void MXR_FeedCanFrame(uint32_t ext_id, const uint8_t *data, uint8_t dlc)
{
    if (dlc < 8) return;

    /* Trích source address từ frame ID */
    uint8_t src_addr = (uint8_t)((ext_id >> 3) & 0xFF);

    MXR_Module_t *mod = find_module_by_addr(src_addr);
    if (mod == NULL) return;  /* Không phải module của mình */

    /* Parse response */
    MXR_Response_t resp;
    resp.src_addr   = src_addr;
    resp.data_type  = data[0];
    resp.error_code = data[1];
    resp.reg_number = ((uint16_t)data[2] << 8) | data[3];

    if (resp.error_code != CHG_PROTO_CAN_RESP_OK) {
        mod->stats.error_count++;
        return;
    }

    /* parse data bytes */
    if (resp.data_type == CHG_PROTO_CAN_RESP_FLOAT) {
        resp.data.f_val = bytes_be_to_float(&data[4]);
    } else {
        resp.data.u_val = bytes_be_to_u32(&data[4]);
    }
    resp.valid = true;

    /* Lấy tick hiện tại (cần extern HAL_GetTick) */
    extern uint32_t HAL_GetTick(void);
    apply_response(mod, &resp, HAL_GetTick());
}

void MXR_GetSystemSummary(MXR_SystemSummary_t *summary)
{
    memset(summary, 0, sizeof(MXR_SystemSummary_t));

    for (uint8_t i = 0; i < g_module_count; i++) {
        MXR_Module_t *mod = &g_modules[i];
        if (!mod->enabled) continue;

        if (mod->state == MXR_STATE_RUNNING || mod->state == MXR_STATE_STARTING) {
            summary->modules_online++;
            summary->total_current += mod->current;
            summary->total_power_in += (float)mod->input_power;
            /* Lấy voltage từ module online đầu tiên */
            if (summary->voltage == 0.0f && mod->voltage > 0.0f) {
                summary->voltage = mod->voltage;
            }
        }

        if (mod->state == MXR_STATE_FAULT) {
            summary->modules_fault++;
            summary->any_critical = true;
        }

        if (MXR_HasCriticalAlarm(mod)) {
            summary->any_critical = true;
        }
    }
}


