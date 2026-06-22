#include "driver_lianming.h"
#include "bsp_can.h"
#include "charger_protocol.h"
#include <string.h>

#define LM_MAX_MODULES          8U
#define LM_POLL_COUNT           7U
#define LM_KEEPALIVE_TIMEOUT_MS 2000U
#define LM_RECOVERY_DELAY_MS    3000U
#define LM_STEP_DELAY_MS        50U
#define LM_MAX_RETRY            3U

typedef struct {
    CHG_ModuleView_t view;
    uint32_t last_state_tick;
    uint8_t poll_step;
    uint8_t retry_count;
    bool should_run;
} LM_Module_t;

static LM_Module_t g_modules[LM_MAX_MODULES];
static uint8_t g_module_count = 0;
static uint8_t g_rr_index = 0;

static const uint16_t g_poll_regs[LM_POLL_COUNT] = {
    CHG_REG_VOLTAGE,
    CHG_REG_CURRENT,
    CHG_REG_CURR_LIMIT,
    CHG_REG_TEMP_DCDC,
    CHG_REG_TEMP_AMBIENT,
    CHG_REG_ALARM_STATUS,
    CHG_REG_INPUT_POWER,
};

static CHG_AlarmFlag_t parse_lianming_alarm(uint32_t raw_alarm)
{
    CHG_AlarmFlag_t flags = CHG_ALARM_NONE;
    if (raw_alarm & (1UL << 0))  flags |= CHG_ALARM_HW_FAULT;
    if (raw_alarm & (1UL << 3))  flags |= CHG_ALARM_COMM_FAIL;
    if (raw_alarm & (1UL << 7))  flags |= CHG_ALARM_HW_FAULT; /* PFC abnormal */
    if (raw_alarm & (1UL << 14)) flags |= CHG_ALARM_AC_UNDER_VOLT;
    if (raw_alarm & (1UL << 16)) flags |= CHG_ALARM_COMM_FAIL;
    if (raw_alarm & (1UL << 28)) flags |= CHG_ALARM_SHORT_CIRCUIT;
    if (raw_alarm & (1UL << 30)) flags |= CHG_ALARM_OVER_TEMP;
    if (raw_alarm & (1UL << 31)) flags |= CHG_ALARM_OVER_VOLTAGE_OUT;
    return flags;
}

static bool is_critical_alarm(CHG_AlarmFlag_t flags)
{
    return (flags != CHG_ALARM_NONE);
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

static uint32_t build_id(uint8_t dst, uint8_t src, uint8_t ptp, uint8_t group)
{
    return CHG_ProtocolBuildCanId(dst, src, ptp, group);
}

static void f32_to_be(float value, uint8_t *out)
{
    CHG_ProtocolFloatToBE(value, out);
}

static float be_to_f32(const uint8_t *in)
{
    return CHG_ProtocolBEToFloat(in);
}

static uint32_t be_to_u32(const uint8_t *in)
{
    return CHG_ProtocolBEToU32(in);
}

static void u32_to_be(uint32_t value, uint8_t *out)
{
    CHG_ProtocolU32ToBE(value, out);
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
    mod->view.state = state;
    mod->last_state_tick = now;
    mod->retry_count = 0;
    switch (state) {
    case CHG_STATE_IDLE:
        mod->view.online = false;
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

static void lm_send(uint8_t addr, uint8_t group, uint8_t func, uint16_t reg, const uint8_t *payload4)
{
    BSP_CAN_Frame_t frame;
    frame.ext_id = build_id(addr, CHG_PROTO_CAN_ADDR_CONTROLLER, CHG_PROTO_CAN_PTP_POINT, group);
    frame.dlc = 8;
    frame.data[0] = func;
    frame.data[1] = 0x00;
    frame.data[2] = (uint8_t)(reg >> 8);
    frame.data[3] = (uint8_t)(reg & 0xFF);
    frame.data[4] = payload4[0];
    frame.data[5] = payload4[1];
    frame.data[6] = payload4[2];
    frame.data[7] = payload4[3];
    (void)BSP_CAN_Transmit(&frame);
}

static void lm_set_float(uint8_t idx, uint16_t reg, float value)
{
    uint8_t payload[4];
    f32_to_be(value, payload);
    lm_send(g_modules[idx].view.addr, g_modules[idx].view.group, CHG_PROTO_CAN_FUNC_SET, reg, payload);
}

static void lm_set_u32(uint8_t idx, uint16_t reg, uint32_t value)
{
    uint8_t payload[4];
    u32_to_be(value, payload);
    lm_send(g_modules[idx].view.addr, g_modules[idx].view.group, CHG_PROTO_CAN_FUNC_SET, reg, payload);
}

static void lm_read(uint8_t idx, uint16_t reg, uint32_t now)
{
    uint8_t payload[4] = {0, 0, 0, 0};
    g_modules[idx].view.last_tx_tick = now;
    lm_send(g_modules[idx].view.addr, g_modules[idx].view.group, CHG_PROTO_CAN_FUNC_READ, reg, payload);
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
    bool fault = is_critical_alarm(mod->view.alarm_flags);
    bool recovering = (mod->view.state == CHG_STATE_RECOVERING);
    mod->view.state = lm_state_from_flags(mod, fault, recovering);
    switch (mod->view.state) {
    case CHG_STATE_RUNNING:
        mod->view.online = true;
        mod->view.running = true;
        break;
    case CHG_STATE_STARTING:
        mod->view.online = true;
        mod->view.running = false;
        break;
    case CHG_STATE_IDLE:
        mod->view.online = true;
        mod->view.running = false;
        break;
    case CHG_STATE_OFFLINE:
    case CHG_STATE_RECOVERING:
        mod->view.online = false;
        mod->view.running = false;
        break;
    case CHG_STATE_FAULT:
        mod->view.online = true;
        mod->view.running = false;
        break;
    }
    mod->view.last_rx_tick = now;
}

static void apply_status(uint8_t idx, uint16_t reg, const uint8_t *data, uint32_t now)
{
    LM_Module_t *mod = &g_modules[idx];
    switch (reg) {
    case CHG_REG_VOLTAGE:      mod->view.voltage = be_to_f32(data); break;
    case CHG_REG_CURRENT:      mod->view.current = be_to_f32(data); break;
    case CHG_REG_CURR_LIMIT:   mod->view.current_limit = be_to_f32(data); break;
    case CHG_REG_TEMP_DCDC:    mod->view.temp_dcdc = be_to_f32(data); break;
    case CHG_REG_TEMP_AMBIENT: mod->view.temp_ambient = be_to_f32(data); break;
    case CHG_REG_ALARM_STATUS: 
        mod->view.alarm_status = be_to_u32(data);
        mod->view.alarm_flags = parse_lianming_alarm(mod->view.alarm_status);
        break;
    case CHG_REG_INPUT_POWER:  mod->view.input_power = be_to_u32(data); break;
    default: break;
    }

    mod->view.online = true;
    mod->view.running = ((mod->view.alarm_status & (1UL << 22)) == 0U);
    mod->view.last_rx_tick = now;

    if (is_critical_alarm(mod->view.alarm_flags)) {
        set_state(mod, CHG_STATE_FAULT, now);
        return;
    }

    if (mod->view.state == CHG_STATE_OFFLINE || mod->view.state == CHG_STATE_RECOVERING) {
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
        lm_read(idx, g_poll_regs[mod->poll_step], now);
        mod->poll_step = (uint8_t)((mod->poll_step + 1U) % LM_POLL_COUNT);
        if (mod->should_run) {
            set_state(mod, CHG_STATE_STARTING, now);
        }
        break;

    case CHG_STATE_STARTING:
        if (mod->retry_count == 0U) {
            lm_set_float(idx, CHG_REG_SET_VOLTAGE, mod->view.voltage);
            mod->retry_count = 1U;
            mod->last_state_tick = now;
        } else if (mod->retry_count == 1U && (now - mod->last_state_tick) >= LM_STEP_DELAY_MS) {
            lm_set_float(idx, CHG_REG_SET_CURR_LIMIT, mod->view.current_limit);
            mod->retry_count = 2U;
            mod->last_state_tick = now;
        } else if (mod->retry_count == 2U && (now - mod->last_state_tick) >= LM_STEP_DELAY_MS) {
            lm_set_u32(idx, CHG_REG_ON_OFF, 0x00000000U);
            mod->retry_count = 3U;
            mod->last_state_tick = now;
        } else if (mod->retry_count >= 3U && (now - mod->last_state_tick) >= LM_STEP_DELAY_MS) {
            set_state(mod, CHG_STATE_RUNNING, now);
        }
        break;

    case CHG_STATE_RUNNING:
        lm_read(idx, g_poll_regs[mod->poll_step], now);
        mod->poll_step = (uint8_t)((mod->poll_step + 1U) % LM_POLL_COUNT);
        if (!mod->should_run) {
            lm_set_u32(idx, CHG_REG_ON_OFF, 0x00010000U);
            set_state(mod, CHG_STATE_IDLE, now);
            break;
        }
        if ((now - mod->view.last_rx_tick) > LM_KEEPALIVE_TIMEOUT_MS) {
            set_state(mod, CHG_STATE_OFFLINE, now);
        }
        break;

    case CHG_STATE_OFFLINE:
        if ((now - mod->last_state_tick) >= LM_RECOVERY_DELAY_MS) {
            set_state(mod, CHG_STATE_RECOVERING, now);
        }
        break;

    case CHG_STATE_RECOVERING:
        lm_read(idx, CHG_REG_ALARM_STATUS, now);
        mod->retry_count++;
        if (mod->retry_count >= LM_MAX_RETRY) {
            set_state(mod, CHG_STATE_OFFLINE, now);
        }
        break;

    case CHG_STATE_FAULT:
        lm_read(idx, CHG_REG_ALARM_STATUS, now);
        if (!is_critical_alarm(mod->view.alarm_flags)) {
            set_state(mod, mod->should_run ? CHG_STATE_STARTING : CHG_STATE_IDLE, now);
        }
        if ((now - mod->view.last_rx_tick) > LM_KEEPALIVE_TIMEOUT_MS) {
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
    mod->poll_step = 0;
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
    lm_set_float(idx, CHG_REG_SET_VOLTAGE, voltage_v);
    return true;
}

static bool lm_set_current_limit(uint8_t idx, float ratio)
{
    if (idx >= g_module_count || !g_modules[idx].view.enabled) return false;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    g_modules[idx].view.current_limit = ratio;
    lm_set_float(idx, CHG_REG_SET_CURR_LIMIT, ratio);
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
    lm_set_u32(idx, CHG_REG_ON_OFF, 0x00010000U);
    set_state(&g_modules[idx], CHG_STATE_IDLE, now);
    return true;
}

static void lm_set_voltage_all(float voltage_v)
{
    for (uint8_t i = 0; i < g_module_count; i++) {
        (void)lm_set_voltage(i, voltage_v);
    }
}

static void lm_set_current_limit_all(float ratio)
{
    for (uint8_t i = 0; i < g_module_count; i++) {
        (void)lm_set_current_limit(i, ratio);
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
        lm_set_u32(i, CHG_REG_ON_OFF, 0x00010000U);
        set_state(&g_modules[i], CHG_STATE_IDLE, now);
    }
}

static void lm_process(uint32_t now_tick)
{
    if (g_module_count == 0U) return;
    process_module(g_rr_index, now_tick);
    g_rr_index = (uint8_t)((g_rr_index + 1U) % g_module_count);
}

static void lm_feed_frame(uint32_t ext_id, const uint8_t *data, uint8_t dlc)
{
    if (dlc < 8U || data == 0) return;

    extern uint32_t HAL_GetTick(void);
    uint32_t now = HAL_GetTick();
    uint8_t src_addr = (uint8_t)((ext_id >> 3) & 0xFFU);
    uint8_t idx = find_by_addr(src_addr);
    if (idx == 0xFFU) return;

    if (data[1] != CHG_PROTO_CAN_RESP_OK) return;

    uint16_t reg = ((uint16_t)data[2] << 8) | data[3];
    apply_status(idx, reg, &data[4], now);
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
        if (is_critical_alarm(m->alarm_flags)) {
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
