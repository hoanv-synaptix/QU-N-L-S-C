#include "charger_maxwell_adapter.h"
#include "maxwell_charger.h"
#include <string.h>

static CHG_ModuleState_t map_state(MXR_State_t state)
{
    switch (state) {
    case MXR_STATE_IDLE:        return CHG_STATE_IDLE;
    case MXR_STATE_STARTING:    return CHG_STATE_STARTING;
    case MXR_STATE_RUNNING:     return CHG_STATE_RUNNING;
    case MXR_STATE_OFFLINE:     return CHG_STATE_OFFLINE;
    case MXR_STATE_FAULT:       return CHG_STATE_FAULT;
    case MXR_STATE_RECOVERING:  return CHG_STATE_RECOVERING;
    default:                    return CHG_STATE_IDLE;
    }
}

static void mx_init(void) { MXR_Init(); }
static int8_t mx_add_module(uint8_t addr, uint8_t group) { return MXR_AddModule(addr, group); }
static void mx_remove_module(uint8_t idx) { MXR_RemoveModule(idx); }
static bool mx_set_voltage(uint8_t idx, float voltage_v) { return MXR_SetVoltage(idx, voltage_v); }
static bool mx_set_current_limit(uint8_t idx, float ratio) { return MXR_SetCurrentLimit(idx, ratio); }
static bool mx_start(uint8_t idx) { return MXR_Start(idx); }
static bool mx_stop(uint8_t idx) { return MXR_Stop(idx); }
static void mx_set_voltage_all(float voltage_v) { MXR_SetVoltageAll(voltage_v); }
static void mx_set_current_limit_all(float ratio) { MXR_SetCurrentLimitAll(ratio); }
static void mx_start_all(void) { MXR_StartAll(); }
static void mx_stop_all(void) { MXR_StopAll(); }
static void mx_emergency_stop(void) { MXR_EmergencyStop(); }
static void mx_process(uint32_t now_tick) { MXR_Process(now_tick); }
static void mx_feed_frame(uint32_t ext_id, const uint8_t *data, uint8_t dlc) { MXR_FeedCanFrame(ext_id, data, dlc); }

static void mx_get_system_summary(CHG_SystemSummary_t *summary)
{
    MXR_SystemSummary_t mx_summary;
    if (summary == 0) return;
    MXR_GetSystemSummary(&mx_summary);
    summary->total_current = mx_summary.total_current;
    summary->total_power_in = mx_summary.total_power_in;
    summary->voltage = mx_summary.voltage;
    summary->modules_online = mx_summary.modules_online;
    summary->modules_fault = mx_summary.modules_fault;
    summary->any_critical = mx_summary.any_critical;
}

static uint8_t mx_get_module_count(void) { return MXR_GetModuleCount(); }

static bool mx_get_module_view(uint8_t idx, CHG_ModuleView_t *view)
{
    const MXR_Module_t *mod = MXR_GetModule(idx);
    if (view == 0 || mod == 0) return false;
    memset(view, 0, sizeof(*view));
    view->addr = mod->addr;
    view->group = mod->group;
    view->enabled = mod->enabled;
    view->state = map_state(mod->state);
    view->online = (mod->state == MXR_STATE_RUNNING || mod->state == MXR_STATE_STARTING);
    view->running = (mod->state == MXR_STATE_RUNNING);
    view->voltage = mod->voltage;
    view->current = mod->current;
    view->current_limit = mod->current_limit;
    view->temp_dcdc = mod->temp_dcdc;
    view->temp_ambient = mod->temp_ambient;
    view->alarm_status = mod->alarm_status;
    view->alarm_flags = mod->alarm_flags;
    view->input_power = mod->input_power;
    view->last_rx_tick = mod->last_rx_tick;
    view->last_tx_tick = mod->last_tx_tick;
    return true;
}

static const CHG_DriverOps_t g_mx_ops = {
    .name = "maxwell",
    .init = mx_init,
    .add_module = mx_add_module,
    .remove_module = mx_remove_module,
    .set_voltage = mx_set_voltage,
    .set_current_limit = mx_set_current_limit,
    .start = mx_start,
    .stop = mx_stop,
    .set_voltage_all = mx_set_voltage_all,
    .set_current_limit_all = mx_set_current_limit_all,
    .start_all = mx_start_all,
    .stop_all = mx_stop_all,
    .emergency_stop = mx_emergency_stop,
    .process = mx_process,
    .feed_frame = mx_feed_frame,
    .get_system_summary = mx_get_system_summary,
    .get_module_count = mx_get_module_count,
    .get_module_view = mx_get_module_view,
};

const CHG_DriverOps_t *CHG_MaxwellDriverOps(void)
{
    return &g_mx_ops;
}
