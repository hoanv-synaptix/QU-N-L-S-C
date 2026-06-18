/**
 * @file    pc_protocol.c
 * @brief   PC protocol handler - parse lệnh từ PC, gọi Maxwell driver
 * @note    Phần USB CDC transmit được abstract qua pc_link_send().
 *          Nhận byte qua PC_Protocol_FeedByte() (gọi từ USB RX callback).
 */

#include "pc_protocol.h"
#include "maxwell_charger.h"
#include <string.h>

/* ============== Link layer abstraction ============== */
/* Hàm này do tầng USB CDC cung cấp (usbd_cdc_if.c) */
extern void pc_link_send(const uint8_t *data, uint16_t len);
/* HAL_GetTick để timestamp - khai báo extern tránh include hal ở đây */
extern uint32_t HAL_GetTick(void);

/* ============== CRC8 ============== */

uint8_t PC_CRC8(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0x00;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x80)
                crc = (uint8_t)((crc << 1) ^ PC_CRC8_POLY);
            else
                crc <<= 1;
        }
    }
    return crc;
}

uint16_t PC_BuildFrame(uint8_t *out, uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    uint16_t i = 0;
    out[i++] = PC_SOF1;
    out[i++] = PC_SOF2;
    out[i++] = cmd;
    out[i++] = len;
    for (uint8_t j = 0; j < len; j++) {
        out[i++] = payload[j];
    }
    /* CRC tính trên [cmd, len, payload] = out[2 .. 2+2+len) */
    out[i++] = PC_CRC8(&out[2], (uint16_t)(len + 2));
    return i;
}

/* ============== RX State Machine ============== */

typedef enum {
    ST_WAIT_SOF1 = 0,
    ST_WAIT_SOF2,
    ST_WAIT_CMD,
    ST_WAIT_LEN,
    ST_WAIT_PAYLOAD,
    ST_WAIT_CRC
} rx_state_t;

static rx_state_t rx_state = ST_WAIT_SOF1;
static uint8_t    rx_cmd;
static uint8_t    rx_len;
static uint8_t    rx_payload[PC_MAX_PAYLOAD];
static uint8_t    rx_idx;

/* Status được cập nhật từ vòng poll Maxwell, gửi định kỳ về PC */
static MXR_ModuleStatus_t g_module_status;
static uint8_t            g_charging = 0;

/* ============== Helpers gửi response ============== */

static void send_ack(uint8_t cmd)
{
    uint8_t frame[8];
    uint16_t n = PC_BuildFrame(frame, PC_RSP_ACK, &cmd, 1);
    pc_link_send(frame, n);
}

static void send_nack(uint8_t cmd, uint8_t err)
{
    uint8_t payload[2] = { cmd, err };
    uint8_t frame[8];
    uint16_t n = PC_BuildFrame(frame, PC_RSP_NACK, payload, 2);
    pc_link_send(frame, n);
}

/* Đọc float little-endian từ payload */
static float payload_to_float(const uint8_t *p)
{
    union { float f; uint8_t b[4]; } conv;
    conv.b[0] = p[0];
    conv.b[1] = p[1];
    conv.b[2] = p[2];
    conv.b[3] = p[3];
    return conv.f;
}

/* ============== Xử lý 1 frame hoàn chỉnh ============== */

static void process_frame(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    bool ok = false;

    switch (cmd) {
        case PC_CMD_SET_VOLTAGE:
            if (len != 4) { send_nack(cmd, PC_ERR_BAD_LENGTH); return; }
            ok = MXR_SetVoltage(payload_to_float(payload));
            break;

        case PC_CMD_SET_CURRENT:
            if (len != 4) { send_nack(cmd, PC_ERR_BAD_LENGTH); return; }
            ok = MXR_SetCurrentLimit(payload_to_float(payload));
            break;

        case PC_CMD_START:
            ok = MXR_Start();
            if (ok) g_charging = 1;
            break;

        case PC_CMD_STOP:
            ok = MXR_Stop();
            if (ok) g_charging = 0;
            break;

        case PC_CMD_SET_MODULE_ADDR:
            if (len != 2) { send_nack(cmd, PC_ERR_BAD_LENGTH); return; }
            MXR_Init(payload[0], payload[1]);
            ok = true;
            break;

        case PC_CMD_PING: {
            uint32_t fw_version = 0x00010000;  /* v1.0.0 */
            uint8_t frame[12];
            uint16_t n = PC_BuildFrame(frame, PC_RSP_PONG,
                                       (uint8_t *)&fw_version, 4);
            pc_link_send(frame, n);
            return;
        }

        case PC_CMD_READ_REG:
            if (len != 2) { send_nack(cmd, PC_ERR_BAD_LENGTH); return; }
            ok = MXR_ReadRegister((uint16_t)(payload[0] | (payload[1] << 8)));
            break;

        default:
            send_nack(cmd, PC_ERR_UNKNOWN_CMD);
            return;
    }

    if (ok) send_ack(cmd);
    else    send_nack(cmd, PC_ERR_CAN_TX_FAIL);
}

/* ============== Public: nạp từng byte nhận từ USB ============== */

void PC_Protocol_FeedByte(uint8_t byte)
{
    switch (rx_state) {
        case ST_WAIT_SOF1:
            if (byte == PC_SOF1) rx_state = ST_WAIT_SOF2;
            break;

        case ST_WAIT_SOF2:
            rx_state = (byte == PC_SOF2) ? ST_WAIT_CMD : ST_WAIT_SOF1;
            break;

        case ST_WAIT_CMD:
            rx_cmd = byte;
            rx_state = ST_WAIT_LEN;
            break;

        case ST_WAIT_LEN:
            rx_len = byte;
            rx_idx = 0;
            if (rx_len > PC_MAX_PAYLOAD) {
                rx_state = ST_WAIT_SOF1;   /* len quá lớn, bỏ */
            } else if (rx_len == 0) {
                rx_state = ST_WAIT_CRC;
            } else {
                rx_state = ST_WAIT_PAYLOAD;
            }
            break;

        case ST_WAIT_PAYLOAD:
            rx_payload[rx_idx++] = byte;
            if (rx_idx >= rx_len) rx_state = ST_WAIT_CRC;
            break;

        case ST_WAIT_CRC: {
            /* CRC tính trên [cmd, len, payload] */
            uint8_t buf[PC_MAX_PAYLOAD + 2];
            buf[0] = rx_cmd;
            buf[1] = rx_len;
            memcpy(&buf[2], rx_payload, rx_len);
            uint8_t calc = PC_CRC8(buf, rx_len + 2);

            if (calc == byte) {
                process_frame(rx_cmd, rx_payload, rx_len);
            } else {
                send_nack(rx_cmd, PC_ERR_BAD_CRC);
            }
            rx_state = ST_WAIT_SOF1;
            break;
        }
    }
}

/* ============== Public: gọi từ main loop để gửi status định kỳ ============== */

void PC_Protocol_SendStatus(void)
{
    PC_StatusReport_t report;
    report.voltage       = g_module_status.voltage;
    report.current       = g_module_status.current;
    report.temp_dcdc     = g_module_status.temp_dcdc;
    report.temp_ambient  = g_module_status.temp_ambient;
    report.alarm_status  = g_module_status.alarm_status;
    report.input_power   = g_module_status.input_power;
    report.charging      = g_charging;
    report.module_online = g_module_status.is_online ? 1 : 0;

    uint8_t frame[5 + sizeof(PC_StatusReport_t)];
    uint16_t n = PC_BuildFrame(frame, PC_RSP_STATUS,
                               (uint8_t *)&report, sizeof(report));
    pc_link_send(frame, n);
}

/* ============== Public: truy cập status struct cho vòng poll Maxwell ============== */

MXR_ModuleStatus_t *PC_Protocol_GetStatusBuffer(void)
{
    return &g_module_status;
}

uint8_t PC_Protocol_IsCharging(void)
{
    return g_charging;
}
