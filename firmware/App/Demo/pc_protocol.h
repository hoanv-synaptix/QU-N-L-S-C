/**
 * @file    pc_protocol.h
 * @brief   Protocol giao tiếp giữa PC App và STM32 qua USB CDC (Virtual COM)
 * @note    Protocol nhị phân, frame-based, có CRC8 kiểm tra lỗi.
 *          Dùng chung định nghĩa cho cả firmware và Python app.
 *
 *  ====== FRAME FORMAT ======
 *  ┌──────┬──────┬──────┬─────────┬──────────┬──────┐
 *  │ SOF1 │ SOF2 │ CMD  │ LEN     │ PAYLOAD  │ CRC8 │
 *  │ 0xAA │ 0x55 │ 1B   │ 1B      │ LEN byte │ 1B   │
 *  └──────┴──────┴──────┴─────────┴──────────┴──────┘
 *  - SOF: Start of frame (2 byte đồng bộ)
 *  - CMD: Command code (xem PC_CMD_xxx)
 *  - LEN: Số byte của PAYLOAD (0-255)
 *  - PAYLOAD: Dữ liệu (little-endian)
 *  - CRC8: Tính trên [CMD, LEN, PAYLOAD], poly 0x07
 */

#ifndef PC_PROTOCOL_H
#define PC_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "maxwell_charger.h"

/* ============== Frame constants ============== */
#define PC_SOF1                 0xAA
#define PC_SOF2                 0x55
#define PC_MAX_PAYLOAD          64
#define PC_CRC8_POLY            0x07

/* ============== Command codes (PC -> STM32) ============== */
#define PC_CMD_SET_VOLTAGE      0x01    /* Payload: float (4B) - điện áp V */
#define PC_CMD_SET_CURRENT      0x02    /* Payload: float (4B) - tỷ lệ dòng 0~1 */
#define PC_CMD_START            0x03    /* Payload: none */
#define PC_CMD_STOP             0x04    /* Payload: none */
#define PC_CMD_SET_MODULE_ADDR  0x05    /* Payload: uint8 addr + uint8 group */
#define PC_CMD_PING             0x06    /* Payload: none - kiểm tra kết nối */
#define PC_CMD_READ_REG         0x07    /* Payload: uint16 reg - đọc register tùy ý */

/* ============== Response codes (STM32 -> PC) ============== */
#define PC_RSP_STATUS           0x81    /* Status report định kỳ (xem PC_StatusReport_t) */
#define PC_RSP_ACK              0x82    /* Payload: uint8 cmd đã nhận OK */
#define PC_RSP_NACK             0x83    /* Payload: uint8 cmd + uint8 error code */
#define PC_RSP_PONG             0x84    /* Trả lời PING, payload: uint32 fw_version */
#define PC_RSP_REG_VALUE        0x85    /* Payload: uint16 reg + float/uint32 value */

/* ============== NACK error codes ============== */
#define PC_ERR_BAD_CRC          0x01
#define PC_ERR_UNKNOWN_CMD      0x02
#define PC_ERR_BAD_LENGTH       0x03
#define PC_ERR_CAN_TX_FAIL      0x04

/* ============== Status report payload ============== */
/* CMD = PC_RSP_STATUS, đóng gói trạng thái module gửi định kỳ về PC */
#pragma pack(push, 1)
typedef struct {
    float    voltage;           /* V đầu ra */
    float    current;           /* A đầu ra */
    float    temp_dcdc;         /* Nhiệt độ DCDC */
    float    temp_ambient;      /* Nhiệt độ môi trường */
    uint32_t alarm_status;      /* Raw alarm bits */
    uint32_t input_power;       /* Công suất vào (W) */
    uint8_t  charging;          /* 1 = đang sạc (đã start), 0 = off */
    uint8_t  module_online;     /* 1 = module có response */
} PC_StatusReport_t;
#pragma pack(pop)

/* ============== API (firmware side) ============== */

/** Tính CRC8 trên buffer */
uint8_t PC_CRC8(const uint8_t *data, uint16_t len);

/**
 * @brief  Đóng gói 1 frame để gửi đi
 * @param  out       Buffer output (tối thiểu LEN+5 byte)
 * @param  cmd       Command code
 * @param  payload   Dữ liệu (có thể NULL nếu len=0)
 * @param  len       Số byte payload
 * @retval Tổng số byte của frame đã đóng gói
 */
uint16_t PC_BuildFrame(uint8_t *out, uint8_t cmd, const uint8_t *payload, uint8_t len);

/**
 * @brief  Nạp 1 byte nhận từ USB CDC vào state machine.
 *         Gọi từ USB RX callback cho mỗi byte.
 */
void PC_Protocol_FeedByte(uint8_t byte);

/**
 * @brief  Đóng gói và gửi status report về PC. Gọi định kỳ từ main loop.
 */
void PC_Protocol_SendStatus(void);

/**
 * @brief  Trả về con trỏ status buffer để vòng poll Maxwell cập nhật vào.
 */
MXR_ModuleStatus_t *PC_Protocol_GetStatusBuffer(void);

/**
 * @brief  1 nếu đang ở trạng thái sạc (đã nhận START), 0 nếu không.
 */
uint8_t PC_Protocol_IsCharging(void);

#endif /* PC_PROTOCOL_H */
