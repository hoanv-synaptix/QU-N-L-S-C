/**
 * @file    pc_protocol.c
 * @brief   PC protocol handler - parse lệnh, gửi status, gọi Maxwell API v2
 */

#include "pc_protocol.h"
#include "charger_core.h"
#include "usbd_cdc_if.h"
#include <string.h>

/* ============== Private ============== */

static uint8_t g_charging = 0;
static float last_set_voltage = 0.0f;
static float last_set_current = 1.0f;

/* RX state machine */
typedef enum {
    ST_SOF1 = 0, ST_SOF2, ST_CMD, ST_LEN, ST_PAYLOAD, ST_CRC
} rx_state_t;

static rx_state_t rx_state = ST_SOF1;
static uint8_t rx_cmd, rx_len, rx_idx;
static uint8_t rx_payload[PC_MAX_PAYLOAD];

/* ============== CRC8 ============== */

static uint8_t crc8(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ PC_CRC8_POLY) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* ============== TX helpers ============== */

static void send_frame(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    uint8_t buf[PC_MAX_PAYLOAD + 5];
    uint16_t i = 0;
    buf[i++] = PC_SOF1;
    buf[i++] = PC_SOF2;
    buf[i++] = cmd;
    buf[i++] = len;
    for (uint8_t j = 0; j < len; j++) buf[i++] = payload[j];
    buf[i++] = crc8(&buf[2], (uint16_t)(len + 2));
    CDC_Transmit_FS(buf, i);
}
static void send_ack(uint8_t cmd)  { send_frame(PC_RSP_ACK, &cmd, 1); }
static void send_nack(uint8_t cmd, uint8_t err) {
    uint8_t p[2] = { cmd, err };
    send_frame(PC_RSP_NACK, p, 2);
}

static float payload_float(const uint8_t *p) {
    union { float f; uint8_t b[4]; } u;
    u.b[0]=p[0]; u.b[1]=p[1]; u.b[2]=p[2]; u.b[3]=p[3];
    return u.f;
}

static void pack_u8(uint8_t value, uint8_t *out)
{
    out[0] = value;
}

static void pack_u32_le(uint32_t value, uint8_t *out)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static void pack_float_le(float value, uint8_t *out)
{
    union { float f; uint8_t b[4]; } u;
    u.f = value;
    out[0] = u.b[0];
    out[1] = u.b[1];
    out[2] = u.b[2];
    out[3] = u.b[3];
}

static bool send_read_reg_response(uint8_t module_idx, uint16_t reg, uint8_t type,
                                   const uint8_t *data, uint8_t len)
{
    uint8_t payload[8];
    if (len > 4U) {
        return false;
    }

    payload[0] = module_idx;
    payload[1] = (uint8_t)(reg >> 8);
    payload[2] = (uint8_t)(reg & 0xFFU);
    payload[3] = type;
    if (len > 0U) {
        payload[4] = data[0];
        if (len > 1U) payload[5] = data[1];
        if (len > 2U) payload[6] = data[2];
        if (len > 3U) payload[7] = data[3];
    }
    send_frame(PC_RSP_READ_REG, payload, (uint8_t)(4U + len));
    return true;
}

static bool read_module_field(uint8_t module_idx, uint16_t reg)
{
    CHG_ModuleView_t view;
    uint8_t data[4];

    if (!CHG_GetModuleView(module_idx, &view) || !view.enabled) {
        return false;
    }

    switch (reg) {
    case 0x0001:
        pack_float_le(view.voltage, data);
        return send_read_reg_response(module_idx, reg, 0x01, data, 4);
    case 0x0002:
        pack_float_le(view.current, data);
        return send_read_reg_response(module_idx, reg, 0x01, data, 4);
    case 0x0003:
        pack_float_le(view.current_limit, data);
        return send_read_reg_response(module_idx, reg, 0x01, data, 4);
    case 0x0004:
        pack_float_le(view.temp_dcdc, data);
        return send_read_reg_response(module_idx, reg, 0x01, data, 4);
    case 0x000B:
        pack_float_le(view.temp_ambient, data);
        return send_read_reg_response(module_idx, reg, 0x01, data, 4);
    case 0x0040:
        pack_u32_le(view.alarm_status, data);
        return send_read_reg_response(module_idx, reg, 0x02, data, 4);
    case 0x0048:
        pack_u32_le(view.input_power, data);
        return send_read_reg_response(module_idx, reg, 0x02, data, 4);
    case 0x0100:
        pack_u8((uint8_t)view.state, data);
        return send_read_reg_response(module_idx, reg, 0x03, data, 1);
    case 0x0101:
        pack_u8(view.online ? 1U : 0U, data);
        return send_read_reg_response(module_idx, reg, 0x03, data, 1);
    case 0x0102:
        pack_u8(view.running ? 1U : 0U, data);
        return send_read_reg_response(module_idx, reg, 0x03, data, 1);
    case 0x0103:
        pack_u8(view.addr, data);
        return send_read_reg_response(module_idx, reg, 0x03, data, 1);
    case 0x0104:
        pack_u8(view.group, data);
        return send_read_reg_response(module_idx, reg, 0x03, data, 1);
    default:
        return false;
    }
}

/* ============== Command handler ============== */

static void process_frame(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    bool ok = false;

    switch (cmd) {
    case PC_CMD_SET_VOLTAGE:
        if (len != 4) { send_nack(cmd, PC_ERR_BAD_LENGTH); return; }
        last_set_voltage = payload_float(payload);
        CHG_SetVoltageAll(last_set_voltage);
        ok = true;
        break;

    case PC_CMD_SET_CURRENT:
        if (len != 4) { send_nack(cmd, PC_ERR_BAD_LENGTH); return; }
        last_set_current = payload_float(payload);
        CHG_SetCurrentLimitAll(last_set_current);
        ok = true;
        break;

    case PC_CMD_START:
        CHG_StartAll();
        g_charging = 1;
        ok = true;
        break;

    case PC_CMD_STOP:
        CHG_StopAll();
        g_charging = 0;
        ok = true;
        break;

    case PC_CMD_EMERGENCY_STOP:
        CHG_EmergencyStop();
        g_charging = 0;
        ok = true;
        break;

    case PC_CMD_SET_DRIVER:
        if (len != 1) { send_nack(cmd, PC_ERR_BAD_LENGTH); return; }
        if (!CHG_SelectDriver((CHG_DriverId_t)payload[0])) {
            send_nack(cmd, PC_ERR_CAN_TX_FAIL);
            return;
        }
        CHG_Init();
        ok = true;
        break;

    case PC_CMD_SET_MODULE_ADDR:
        if (len != 2) { send_nack(cmd, PC_ERR_BAD_LENGTH); return; }
        CHG_Init(); /* Reset current driver state */
        CHG_AddModule(payload[0], payload[1]);
        CHG_SetVoltageAll(last_set_voltage);
        CHG_SetCurrentLimitAll(last_set_current);
        if (g_charging) {
            CHG_StartAll();
        }
        ok = true;
        break;

    case PC_CMD_READ_REG: {
        uint8_t module_idx;
        uint16_t reg;
        if (len != 3) { send_nack(cmd, PC_ERR_BAD_LENGTH); return; }
        module_idx = payload[0];
        reg = (uint16_t)(((uint16_t)payload[1] << 8) | payload[2]);
        if (!read_module_field(module_idx, reg)) {
            send_nack(cmd, PC_ERR_BAD_PARAM);
            return;
        }
        return;
    }

    case PC_CMD_PING:
        PC_Protocol_SendPong();
        return;

    default:
        send_nack(cmd, PC_ERR_UNKNOWN_CMD);
        return;
    }

    if (ok) send_ack(cmd);
    else    send_nack(cmd, PC_ERR_CAN_TX_FAIL);
}

/* ============== Public API ============== */

void PC_Protocol_FeedByte(uint8_t byte)
{
    switch (rx_state) {
    case ST_SOF1:
        if (byte == PC_SOF1) rx_state = ST_SOF2;
        break;
    case ST_SOF2:
        rx_state = (byte == PC_SOF2) ? ST_CMD : ST_SOF1;
        break;
    case ST_CMD:
        rx_cmd = byte; rx_state = ST_LEN;
        break;
    case ST_LEN:
        rx_len = byte; rx_idx = 0;
        if (rx_len > PC_MAX_PAYLOAD) rx_state = ST_SOF1;
        else if (rx_len == 0) rx_state = ST_CRC;
        else rx_state = ST_PAYLOAD;
        break;
    case ST_PAYLOAD:
        rx_payload[rx_idx++] = byte;
        if (rx_idx >= rx_len) rx_state = ST_CRC;
        break;
    case ST_CRC: {
        uint8_t buf[PC_MAX_PAYLOAD + 2];
        buf[0] = rx_cmd; buf[1] = rx_len;
        memcpy(&buf[2], rx_payload, rx_len);
        if (crc8(buf, rx_len + 2) == byte) {
            process_frame(rx_cmd, rx_payload, rx_len);
        } else {
            send_nack(rx_cmd, PC_ERR_BAD_CRC);
        }
        rx_state = ST_SOF1;
        break;
    }
    }
}

void PC_Protocol_SendStatus(void)
{
    CHG_SystemSummary_t sum;
    CHG_GetSystemSummary(&sum);

    /* Lấy temp cao nhất từ module đầu tiên online */
    float temp_dcdc = 0, temp_amb = 0;
    uint32_t alarm_or = 0;
    CHG_ModuleView_t view;
    for (uint8_t i = 0; i < CHG_GetModuleCount(); i++) {
        if (!CHG_GetModuleView(i, &view) || !view.enabled) continue;
        if (view.temp_dcdc > temp_dcdc) temp_dcdc = view.temp_dcdc;
        if (view.temp_ambient > temp_amb) temp_amb = view.temp_ambient;
        alarm_or |= view.alarm_status;
    }

    PC_StatusReport_t report;
    report.voltage        = sum.voltage;
    report.total_current  = sum.total_current;
    report.temp_dcdc      = temp_dcdc;
    report.temp_ambient   = temp_amb;
    report.alarm_status   = alarm_or;
    report.total_power_in = (uint32_t)sum.total_power_in;
    report.modules_online = sum.modules_online;
    report.modules_fault  = sum.modules_fault;
    report.charging       = g_charging;
    report.btn_start      = 0; /* Sẽ cập nhật từ GPIO */
    report.btn_stop       = 0;

    send_frame(PC_RSP_STATUS, (uint8_t *)&report, sizeof(report));
}

void PC_Protocol_SendPong(void)
{
    uint32_t ver = ((uint32_t)FW_VERSION_MAJOR << 16) |
                   ((uint32_t)FW_VERSION_MINOR << 8) |
                   FW_VERSION_PATCH;
    send_frame(PC_RSP_PONG, (uint8_t *)&ver, 4);
}

bool PC_Protocol_IsCharging(void)
{
    return g_charging != 0;
}
