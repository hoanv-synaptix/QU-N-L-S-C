/**
 * @file    pc_protocol.c
 * @brief   PC protocol handler - parse lệnh, gửi status, gọi Maxwell API v2
 */

#include "pc_protocol.h"
#include "maxwell_charger.h"
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

/* ============== Command handler ============== */

static void process_frame(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    bool ok = false;

    switch (cmd) {
    case PC_CMD_SET_VOLTAGE:
        if (len != 4) { send_nack(cmd, PC_ERR_BAD_LENGTH); return; }
        last_set_voltage = payload_float(payload);
        MXR_SetVoltageAll(last_set_voltage);
        ok = true;
        break;

    case PC_CMD_SET_CURRENT:
        if (len != 4) { send_nack(cmd, PC_ERR_BAD_LENGTH); return; }
        last_set_current = payload_float(payload);
        MXR_SetCurrentLimitAll(last_set_current);
        ok = true;
        break;

    case PC_CMD_START:
        MXR_StartAll();
        g_charging = 1;
        ok = true;
        break;

    case PC_CMD_STOP:
        MXR_StopAll();
        g_charging = 0;
        ok = true;
        break;

    case PC_CMD_EMERGENCY_STOP:
        MXR_EmergencyStop();
        g_charging = 0;
        ok = true;
        break;

    case PC_CMD_SET_MODULE_ADDR:
        if (len != 2) { send_nack(cmd, PC_ERR_BAD_LENGTH); return; }
        MXR_Init(); /* Xóa các cấu hình module cũ */
        MXR_AddModule(payload[0], payload[1]);
        /* Khôi phục lại setpoint cũ */
        MXR_SetVoltageAll(last_set_voltage);
        MXR_SetCurrentLimitAll(last_set_current);
        if (g_charging) {
            MXR_StartAll(); /* Tự động start lại nếu hệ thống đang sạc */
        }
        ok = true;
        break;

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
    MXR_SystemSummary_t sum;
    MXR_GetSystemSummary(&sum);

    /* Lấy temp cao nhất từ module đầu tiên online */
    float temp_dcdc = 0, temp_amb = 0;
    uint32_t alarm_or = 0;
    for (uint8_t i = 0; i < MXR_GetModuleCount(); i++) {
        const MXR_Module_t *m = MXR_GetModule(i);
        if (m == NULL || !m->enabled) continue;
        if (m->temp_dcdc > temp_dcdc) temp_dcdc = m->temp_dcdc;
        if (m->temp_ambient > temp_amb) temp_amb = m->temp_ambient;
        alarm_or |= m->alarm_status;
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
