#include "charger_core.h"
#include <string.h>

static const CHG_DriverOps_t *g_driver_table[CHG_MAX_DRIVER_ID];
static const CHG_DriverOps_t *g_active_driver = 0;
static CHG_DriverId_t g_active_driver_id = CHG_DRIVER_NONE;

static const CHG_DriverOps_t *get_active(void)
{
    return g_active_driver;
}

static bool names_match(const char *a, const char *b)
{
    if (a == 0 || b == 0) return false;
    return strcmp(a, b) == 0;
}

bool CHG_RegisterDriver(CHG_DriverId_t id, const CHG_DriverOps_t *ops)
{
    if ((uint32_t)id >= CHG_MAX_DRIVER_ID || ops == 0) return false;
    g_driver_table[id] = ops;
    if (g_active_driver == 0) {
        g_active_driver = ops;
        g_active_driver_id = id;
    }
    return true;
}

bool CHG_IsDriverRegistered(CHG_DriverId_t id)
{
    if ((uint32_t)id >= CHG_MAX_DRIVER_ID) return false;
    return g_driver_table[id] != 0;
}

bool CHG_SelectDriver(CHG_DriverId_t id)
{
    if ((uint32_t)id >= CHG_MAX_DRIVER_ID || g_driver_table[id] == 0) return false;
    g_active_driver = g_driver_table[id];
    g_active_driver_id = id;
    return true;
}

bool CHG_SelectDriverByName(const char *name)
{
    for (uint8_t i = 0; i < CHG_MAX_DRIVER_ID; i++) {
        if (g_driver_table[i] != 0 && names_match(g_driver_table[i]->name, name)) {
            g_active_driver = g_driver_table[i];
            g_active_driver_id = (CHG_DriverId_t)i;
            return true;
        }
    }
    return false;
}

CHG_DriverId_t CHG_GetActiveDriverId(void)
{
    return g_active_driver_id;
}

const char *CHG_GetActiveDriverName(void)
{
    const CHG_DriverOps_t *driver = get_active();
    return (driver != 0 && driver->name != 0) ? driver->name : "none";
}

void CHG_Init(void)
{
    const CHG_DriverOps_t *driver = get_active();
    if (driver != 0 && driver->init != 0) driver->init();
}

int8_t CHG_AddModule(uint8_t addr, uint8_t group)
{
    const CHG_DriverOps_t *driver = get_active();
    return (driver != 0 && driver->add_module != 0) ? driver->add_module(addr, group) : -1;
}

void CHG_RemoveModule(uint8_t idx)
{
    const CHG_DriverOps_t *driver = get_active();
    if (driver != 0 && driver->remove_module != 0) driver->remove_module(idx);
}

bool CHG_SetVoltage(uint8_t idx, float voltage_v)
{
    const CHG_DriverOps_t *driver = get_active();
    return (driver != 0 && driver->set_voltage != 0) ? driver->set_voltage(idx, voltage_v) : false;
}

bool CHG_SetCurrentLimit(uint8_t idx, float ratio)
{
    const CHG_DriverOps_t *driver = get_active();
    return (driver != 0 && driver->set_current_limit != 0) ? driver->set_current_limit(idx, ratio) : false;
}

bool CHG_Start(uint8_t idx)
{
    const CHG_DriverOps_t *driver = get_active();
    return (driver != 0 && driver->start != 0) ? driver->start(idx) : false;
}

bool CHG_Stop(uint8_t idx)
{
    const CHG_DriverOps_t *driver = get_active();
    return (driver != 0 && driver->stop != 0) ? driver->stop(idx) : false;
}

void CHG_SetVoltageAll(float voltage_v)
{
    const CHG_DriverOps_t *driver = get_active();
    if (driver != 0 && driver->set_voltage_all != 0) driver->set_voltage_all(voltage_v);
}

void CHG_SetCurrentLimitAll(float ratio)
{
    const CHG_DriverOps_t *driver = get_active();
    if (driver != 0 && driver->set_current_limit_all != 0) driver->set_current_limit_all(ratio);
}

void CHG_StartAll(void)
{
    const CHG_DriverOps_t *driver = get_active();
    if (driver != 0 && driver->start_all != 0) driver->start_all();
}

void CHG_StopAll(void)
{
    const CHG_DriverOps_t *driver = get_active();
    if (driver != 0 && driver->stop_all != 0) driver->stop_all();
}

void CHG_EmergencyStop(void)
{
    const CHG_DriverOps_t *driver = get_active();
    if (driver != 0 && driver->emergency_stop != 0) driver->emergency_stop();
}

void CHG_Process(uint32_t now_tick)
{
    const CHG_DriverOps_t *driver = get_active();
    if (driver != 0 && driver->process != 0) driver->process(now_tick);
}

void CHG_FeedCanFrame(uint32_t ext_id, const uint8_t *data, uint8_t dlc)
{
    const CHG_DriverOps_t *driver = get_active();
    if (driver != 0 && driver->feed_frame != 0) driver->feed_frame(ext_id, data, dlc);
}

void CHG_GetSystemSummary(CHG_SystemSummary_t *summary)
{
    const CHG_DriverOps_t *driver = get_active();
    if (summary == 0) return;
    memset(summary, 0, sizeof(*summary));
    if (driver != 0 && driver->get_system_summary != 0) driver->get_system_summary(summary);
}

uint8_t CHG_GetModuleCount(void)
{
    const CHG_DriverOps_t *driver = get_active();
    return (driver != 0 && driver->get_module_count != 0) ? driver->get_module_count() : 0;
}

bool CHG_GetModuleView(uint8_t idx, CHG_ModuleView_t *view)
{
    const CHG_DriverOps_t *driver = get_active();
    return (driver != 0 && driver->get_module_view != 0) ? driver->get_module_view(idx, view) : false;
}
