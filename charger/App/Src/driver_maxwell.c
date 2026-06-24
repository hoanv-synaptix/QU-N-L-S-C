/**
 * @file driver_maxwell.c
 * @brief Maxwell MXR Series Charging Module Driver
 * @note Protocol V1.50 - CAN 2.0B Extended Frame, 125Kbps
 *
 * Hardware Interface:
 *   - CAN bus: 125 Kbps, Extended 29-bit frame
 *   - Isolation: Required (isolated CAN transceiver)
 *
 * CAN Frame Format (29-bit Extended ID):
 *   | Bit 28-20 | Bit 19 | Bit 18-11 | Bit 10-3 | Bit 2-0 |
 *   | PROTNO     | PTP     | DSTADDR    | SRCADDR  | Group   |
 *   | 0x060     | 0/1     | 0-63       | 0xF0     | 0-7     |
 *
 * Data Format (8 bytes):
 *   | Byte 0     | Byte 1   | Byte 2-3      | Byte 4-7        |
 *   | Func Code  | Reserved | Register      | Data (IEEE 754) |
 *   | 0x03/0x10 | 0x00     | 0x0021 (Volt) | Float value     |
 *
 * References:
 *   - "CAN Communication Protocol - Maxwell_V1.50.pdf"
 *   - "Maxwell.md" ( Vietnamese translation)
 *
 * Example:
 *   Set voltage 700V: 03 00 00 21 44 2F 00 00 (0x442F0000 = 700.0f)
 *   Start module:    03 00 00 30 00 00 00 00
 *   Stop module:     03 00 00 30 00 01 00 00
 */

#include "charger_core.h"
#include "charger_protocol.h"
#include "bsp_can.h"
#include <string.h>

/* ============== Maxwell-specific CAN Constants ============== */
/* Protocol theo Maxwell V1.50 - 29-bit Extended Frame */

#define MXR_PROTNO              0x060U      /* Protocol Number */
#define MXR_PTP_POINT           1U          /* Point-to-Point */
#define MXR_PTP_BROADCAST       0U          /* Broadcast */
#define MXR_ADDR_CONTROLLER     0xF0U       /* Controller address */
#define MXR_ADDR_BROADCAST      0xFFU       /* Broadcast address */
#define MXR_FUNC_SET            0x03U       /* Write register */
#define MXR_FUNC_READ           0x10U       /* Read register */
#define MXR_RESP_FLOAT          0x41U       /* Float response */
#define MXR_RESP_INT            0x42U       /* Integer response */
#define MXR_RESP_OK             0xF0U       /* Response OK */
#define MXR_RESP_FAIL           0xF2U       /* Response fail */

/* Command values for REG_ON_OFF */
#define MXR_CMD_STOP            0x00010000U  /* Stop command */
#define MXR_CMD_START          0x00000000U  /* Start command */

/* ============== Configuration ============== */

#define MXR_MAX_MODULES 8
#define MXR_OFFLINE_TIMEOUT_MS 1500
#define MXR_RECOVERY_DELAY_MS 3000
#define MXR_MAX_RETRIES 3
#define MXR_POLL_REG_COUNT 7

/* Vendor-specific register (không dùng chung, chỉ riêng Maxwell) */
#define MXR_REG_INPUT_VOLTAGE 0x0005
#define MXR_REG_PFC0_VOLTAGE 0x0008
#define MXR_REG_PFC1_VOLTAGE 0x000A
#define MXR_REG_AC_PHASE_A 0x000C
#define MXR_REG_AC_PHASE_B 0x000D
#define MXR_REG_AC_PHASE_C 0x000E
#define MXR_REG_TEMP_PFC 0x0010
#define MXR_REG_SET_ALTITUDE 0x0017
#define MXR_REG_SET_CURRENT_INT 0x001B
#define MXR_REG_SET_GROUP 0x001E
#define MXR_REG_SET_ADDR_MODE 0x001F
#define MXR_REG_OVP_RESET 0x0031
#define MXR_REG_ALTITUDE_RD 0x004A

/* Vendor-specific alarm bits */
#define MXR_ALARM_MODULE_FAULT (1U << 0)
#define MXR_ALARM_MODULE_PROTECT (1U << 1)
#define MXR_ALARM_INPUT_ERROR  (1U << 4)
#define MXR_ALARM_SCI_FAILURE (1U << 6)
#define MXR_ALARM_DCDC_OV (1U << 8)
#define MXR_ALARM_PFC_ABNORMAL (1U << 9)
#define MXR_ALARM_AC_UNDERVOLTAGE (1U << 14)
#define MXR_ALARM_CAN_FAILURE (1U << 16)
#define MXR_ALARM_CURR_IMBALANCE (1U << 17)
#define MXR_ALARM_DCDC_OFF (1U << 22)
#define MXR_ALARM_POWER_LIMIT (1U << 23)
#define MXR_ALARM_TEMP_DERATING (1U << 24)
#define MXR_ALARM_AC_POWER_LIMIT (1U << 25)
#define MXR_ALARM_FAN_FAILURE (1U << 27)
#define MXR_ALARM_SHORT_CIRCUIT (1U << 28)
#define MXR_ALARM_DCDC_OVERTEMP (1U << 30)
#define MXR_ALARM_DCDC_OUTPUT_OV (1U << 31)

#define MXR_ALARM_CRITICAL_MASK (MXR_ALARM_MODULE_FAULT | \
 MXR_ALARM_DCDC_OV | \
 MXR_ALARM_SHORT_CIRCUIT | \
 MXR_ALARM_DCDC_OVERTEMP | \
 MXR_ALARM_DCDC_OUTPUT_OV | \
 MXR_ALARM_SCI_FAILURE)

/* ============== Internal types ============== */

typedef struct {
 float voltage_v;
 float current_limit;
 bool should_run;
} MXR_Setpoint_t;

typedef struct {
 /* Public surface (chính là CHG_ModuleView_t) */
 CHG_ModuleView_t view;

 /* Driver-private state */
 MXR_Setpoint_t setpoint;
 uint32_t state_enter_tick;
 uint8_t retry_count;
 uint8_t poll_step;
} MXR_Internal_t;

/* ============== Private state ============== */

static MXR_Internal_t g_modules[MXR_MAX_MODULES];
static uint8_t g_module_count = 0;
static uint8_t g_current_idx = 0;

static const uint16_t g_poll_regs[MXR_POLL_REG_COUNT] = {
 CHG_REG_VOLTAGE,
 CHG_REG_CURRENT,
 CHG_REG_CURR_LIMIT,
 CHG_REG_TEMP_DCDC,
 CHG_REG_TEMP_AMBIENT,
 CHG_REG_ALARM_STATUS,
 CHG_REG_INPUT_POWER,
};

/* ============== CAN Frame ID Builder ============== */

/**
 * @brief  Build 29-bit CAN Extended ID theo Maxwell protocol
 * @note   Format: PROTNO(9b) | PTP(1b) | DSTADDR(8b) | SRCADDR(8b) | GRP(3b)
 * @param  dst_addr  Địa chỉ đích (0-63)
 * @param  src_addr  Địa chỉ nguồn (0xF0 cho controller)
 * @param  ptp       1=Point-to-Point, 0=Broadcast
 * @param  group     Group number (0-7)
 * @retval 29-bit CAN ID
 */
static uint32_t mxr_build_frame_id(uint8_t dst_addr, uint8_t src_addr,
                                   uint8_t ptp, uint8_t group)
{
    uint32_t id = 0;
    id |= ((uint32_t)MXR_PROTNO & 0x1FFU) << 20;    /* bits 20-28: PROTNO */
    id |= ((uint32_t)ptp & 0x01U) << 19;            /* bit 19: PTP */
    id |= ((uint32_t)dst_addr & 0xFFU) << 11;        /* bits 11-18: DSTADDR */
    id |= ((uint32_t)src_addr & 0xFFU) << 3;        /* bits 3-10: SRCADDR */
    id |= ((uint32_t)group & 0x07U);                /* bits 0-2: GRP */
    return id;
}

/* ============== Helpers ============== */

static uint32_t now_tick(void) {
 extern uint32_t HAL_GetTick(void);
 return HAL_GetTick();
}

static bool send_frame(MXR_Internal_t *m, uint8_t func, uint16_t reg, const uint8_t *payload4)
{
 BSP_CAN_Frame_t frame;
 frame.ext_id = mxr_build_frame_id(m->view.addr, MXR_ADDR_CONTROLLER,
                                    MXR_PTP_POINT, m->view.group);
 frame.dlc = 8;
 frame.data[0] = func;
 frame.data[1] = 0x00;
 frame.data[2] = (uint8_t)(reg >> 8);
 frame.data[3] = (uint8_t)(reg & 0xFF);
 frame.data[4] = payload4[0];
 frame.data[5] = payload4[1];
 frame.data[6] = payload4[2];
 frame.data[7] = payload4[3];

 if (BSP_CAN_Transmit(&frame)) {
 m->view.stats.tx_count++;
 m->view.last_tx_tick = now_tick();
 return true;
 }
 return false;
}

static bool send_set_float(MXR_Internal_t *m, uint16_t reg, float val)
{
 uint8_t payload[4];
 CHG_ProtocolFloatToBE(val, payload);
 return send_frame(m, MXR_FUNC_SET, reg, payload);
}

static bool send_set_u32(MXR_Internal_t *m, uint16_t reg, uint32_t val)
{
 uint8_t payload[4];
 CHG_ProtocolU32ToBE(val, payload);
 return send_frame(m, MXR_FUNC_SET, reg, payload);
}

static bool send_read(MXR_Internal_t *m, uint16_t reg)
{
 uint8_t payload[4] = {0, 0, 0, 0};
 return send_frame(m, MXR_FUNC_READ, reg, payload);
}

static void set_state(MXR_Internal_t *m, CHG_ModuleState_t st, uint32_t now)
{
 m->view.state = st;
 m->state_enter_tick = now;
 m->retry_count = 0;
}

static MXR_Internal_t *find_by_addr(uint8_t addr)
{
 for (uint8_t i = 0; i < g_module_count; i++) {
 if (g_modules[i].view.enabled && g_modules[i].view.addr == addr) {
 return &g_modules[i];
 }
 }
 return NULL;
}

static CHG_AlarmFlag_t parse_maxwell_alarm(uint32_t raw)
{
 CHG_AlarmFlag_t flags = CHG_ALARM_NONE;
 if (raw & MXR_ALARM_MODULE_FAULT) flags |= CHG_ALARM_HW_FAULT;
 if (raw & MXR_ALARM_SCI_FAILURE) flags |= CHG_ALARM_COMM_FAIL;
 if (raw & MXR_ALARM_DCDC_OV) flags |= CHG_ALARM_OVER_VOLTAGE_OUT;
 if (raw & MXR_ALARM_AC_UNDERVOLTAGE) flags |= CHG_ALARM_AC_UNDER_VOLT;
 if (raw & MXR_ALARM_CAN_FAILURE) flags |= CHG_ALARM_COMM_FAIL;
 if (raw & MXR_ALARM_SHORT_CIRCUIT) flags |= CHG_ALARM_SHORT_CIRCUIT;
 if (raw & MXR_ALARM_DCDC_OVERTEMP) flags |= CHG_ALARM_OVER_TEMP;
 if (raw & MXR_ALARM_DCDC_OUTPUT_OV) flags |= CHG_ALARM_OVER_VOLTAGE_OUT;
 return flags;
}

/* ============== Response handler ============== */

static void apply_response(MXR_Internal_t *m, const uint8_t *data, uint32_t now)
{
 uint8_t data_type = data[0];
 uint8_t error_code = data[1];
 uint16_t reg = ((uint16_t)data[2] << 8) | data[3];

 if (error_code != MXR_RESP_OK) {
 m->view.stats.error_count++;
 return;
 }

 switch (reg) {
 case CHG_REG_VOLTAGE: m->view.voltage = CHG_ProtocolBEToFloat(&data[4]); break;
 case CHG_REG_CURRENT: m->view.current = CHG_ProtocolBEToFloat(&data[4]); break;
 case CHG_REG_CURR_LIMIT: m->view.current_limit = CHG_ProtocolBEToFloat(&data[4]); break;
 case CHG_REG_TEMP_DCDC: m->view.temp_dcdc = CHG_ProtocolBEToFloat(&data[4]); break;
 case CHG_REG_TEMP_AMBIENT: m->view.temp_ambient = CHG_ProtocolBEToFloat(&data[4]); break;
 case CHG_REG_ALARM_STATUS: {
 m->view.alarm_status = CHG_ProtocolBEToU32(&data[4]);
 m->view.alarm_flags = parse_maxwell_alarm(m->view.alarm_status);
 break;
 }
 case CHG_REG_INPUT_POWER: {
 uint32_t raw = CHG_ProtocolBEToU32(&data[4]);
 if (raw <= CHG_INPUT_POWER_MAX_W) {
 m->view.input_power = raw;
 }
 break;
 }
 default: return; /* Unknown reg: count as error */
 }

 (void)data_type;
 m->view.last_rx_tick = now;
 m->view.stats.rx_count++;

 /* Recovery transition: nếu vừa online lại */
 if (m->view.state == CHG_STATE_OFFLINE || m->view.state == CHG_STATE_RECOVERING) {
 m->view.stats.recovery_count++;
 set_state(m, m->setpoint.should_run ? CHG_STATE_STARTING : CHG_STATE_IDLE, now);
 }

 /* Critical alarm -> stop ngay */
 if (m->view.alarm_flags != CHG_ALARM_NONE && m->view.state == CHG_STATE_RUNNING) {
 set_state(m, CHG_STATE_FAULT, now);
 send_set_u32(m, CHG_REG_ON_OFF, MXR_CMD_STOP);
 }
}

/* ============== FSM per module ============== */

static void process_module(MXR_Internal_t *m, uint32_t now)
{
 if (!m->view.enabled) return;

 uint32_t since_rx = now - m->view.last_rx_tick;
 uint32_t since_state = now - m->state_enter_tick;

 switch (m->view.state) {

 case CHG_STATE_IDLE:
 send_read(m, g_poll_regs[m->poll_step]);
 m->poll_step = (m->poll_step + 1) % MXR_POLL_REG_COUNT;
 if (since_rx < MXR_OFFLINE_TIMEOUT_MS && m->setpoint.should_run) {
 set_state(m, CHG_STATE_STARTING, now);
 }
 break;

 case CHG_STATE_STARTING:
 if (m->retry_count == 0) {
 send_set_float(m, CHG_REG_SET_VOLTAGE, m->setpoint.voltage_v);
 m->retry_count++;
 m->state_enter_tick = now;
 } else if (m->retry_count == 1 && (now - m->state_enter_tick) >= 50) {
 send_set_float(m, CHG_REG_SET_CURR_LIMIT, m->setpoint.current_limit);
 m->retry_count++;
 m->state_enter_tick = now;
 } else if (m->retry_count == 2 && (now - m->state_enter_tick) >= 50) {
 send_set_u32(m, CHG_REG_ON_OFF, MXR_CMD_START);
 m->retry_count = 3;
 m->view.last_tx_tick = now;
 } else if (m->retry_count >= 3 && (now - m->view.last_tx_tick) >= 100) {
 set_state(m, CHG_STATE_RUNNING, now);
 }
 break;

 case CHG_STATE_RUNNING:
 send_read(m, g_poll_regs[m->poll_step]);
 m->poll_step = (m->poll_step + 1) % MXR_POLL_REG_COUNT;
 if (since_rx > MXR_OFFLINE_TIMEOUT_MS) {
 m->view.stats.timeout_count++;
 set_state(m, CHG_STATE_OFFLINE, now);
 }
 if (!m->setpoint.should_run) {
 send_set_u32(m, CHG_REG_ON_OFF, MXR_CMD_STOP);
 set_state(m, CHG_STATE_IDLE, now);
 }
 break;

 case CHG_STATE_OFFLINE:
 if (since_state > MXR_RECOVERY_DELAY_MS) {
 set_state(m, CHG_STATE_RECOVERING, now);
 }
 break;

 case CHG_STATE_RECOVERING:
 send_read(m, CHG_REG_ALARM_STATUS);
 m->retry_count++;
 if (m->retry_count >= MXR_MAX_RETRIES) {
 set_state(m, CHG_STATE_OFFLINE, now);
 }
 break;

 case CHG_STATE_FAULT:
 send_read(m, CHG_REG_ALARM_STATUS);
 if (m->view.alarm_flags == CHG_ALARM_NONE && since_rx < MXR_OFFLINE_TIMEOUT_MS) {
 set_state(m, m->setpoint.should_run ? CHG_STATE_STARTING : CHG_STATE_IDLE, now);
 }
 if (since_rx > MXR_OFFLINE_TIMEOUT_MS) {
 set_state(m, CHG_STATE_OFFLINE, now);
 }
 break;
 }
}

/* ============== CHG_DriverOps_t implementation ============== */

static void mx_init(void)
{
 memset(g_modules, 0, sizeof(g_modules));
 g_module_count = 0;
 g_current_idx = 0;
}

static int8_t mx_add_module(uint8_t addr, uint8_t group)
{
 if (g_module_count >= MXR_MAX_MODULES) return -1;

 MXR_Internal_t *m = &g_modules[g_module_count];
 memset(m, 0, sizeof(*m));
 m->view.addr = addr;
 m->view.group = group;
 m->view.enabled = true;
 m->view.state = CHG_STATE_IDLE;
 m->view.current_limit = 1.0f;
 m->setpoint.voltage_v = 0.0f;
 m->setpoint.current_limit = 1.0f;
 m->setpoint.should_run = false;

 return (int8_t)(g_module_count++);
}

static void mx_remove_module(uint8_t idx)
{
 if (idx >= g_module_count) return;
 MXR_Internal_t *m = &g_modules[idx];
 if (m->view.state == CHG_STATE_RUNNING || m->view.state == CHG_STATE_STARTING) {
 send_set_u32(m, CHG_REG_ON_OFF, MXR_CMD_STOP);
 }
 m->view.enabled = false;
 m->view.state = CHG_STATE_IDLE;
}

static bool mx_set_voltage(uint8_t idx, float voltage_v)
{
 if (idx >= g_module_count || !g_modules[idx].view.enabled) return false;
 g_modules[idx].setpoint.voltage_v = voltage_v;
 g_modules[idx].view.voltage = voltage_v;
 if (g_modules[idx].view.state == CHG_STATE_RUNNING) {
 return send_set_float(&g_modules[idx], CHG_REG_SET_VOLTAGE, voltage_v);
 }
 return true;
}

static bool mx_set_current_limit(uint8_t idx, float current_a)
{
 if (idx >= g_module_count || !g_modules[idx].view.enabled) return false;
 if (current_a < 0.0f) current_a = 0.0f;
 /* Convert Amps to ratio based on rated current (default 20A if not known) */
 float rated_current = (g_modules[idx].view.current > 0) ? g_modules[idx].view.current : 20.0f;
 float ratio = current_a / rated_current;
 if (ratio > 1.0f) ratio = 1.0f;
 g_modules[idx].setpoint.current_limit = current_a; /* Store actual Amps */
 g_modules[idx].view.current_limit = current_a;
 if (g_modules[idx].view.state == CHG_STATE_RUNNING) {
 return send_set_float(&g_modules[idx], CHG_REG_SET_CURR_LIMIT, ratio);
 }
 return true;
}

static bool mx_start(uint8_t idx)
{
 if (idx >= g_module_count || !g_modules[idx].view.enabled) return false;
 g_modules[idx].setpoint.should_run = true;
 return true;
}

static bool mx_stop(uint8_t idx)
{
 if (idx >= g_module_count || !g_modules[idx].view.enabled) return false;
 g_modules[idx].setpoint.should_run = false;
 MXR_Internal_t *m = &g_modules[idx];
 if (m->view.state == CHG_STATE_RUNNING || m->view.state == CHG_STATE_STARTING) {
 send_set_u32(m, CHG_REG_ON_OFF, MXR_CMD_STOP);
 }
 return true;
}

static void mx_set_voltage_all(float voltage