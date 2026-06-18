/**
 * @file    pc_protocol.h
 * @brief   Binary protocol PC ↔ STM32 qua USB CDC
 * @note    Frame: [0xAA][0x55][CMD][LEN][PAYLOAD...][CRC8]
 *          CRC8 poly=0x07, tính trên [CMD, LEN, PAYLOAD]
 *          Payload: little-endian
 */

#ifndef PC_PROTOCOL_H
#define PC_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

/* Frame constants */
#define PC_SOF1             0xAA
#define PC_SOF2             0x55
#define PC_MAX_PAYLOAD      64
#define PC_CRC8_POLY        0x07

/* Commands PC -> STM32 */
#define PC_CMD_SET_VOLTAGE      0x01
#define PC_CMD_SET_CURRENT      0x02
#define PC_CMD_START            0x03
#define PC_CMD_STOP             0x04
#define PC_CMD_SET_MODULE_ADDR  0x05
#define PC_CMD_PING             0x06
#define PC_CMD_READ_REG         0x07
#define PC_CMD_EMERGENCY_STOP   0x08

/* Responses STM32 -> PC */
#define PC_RSP_STATUS           0x81
#define PC_RSP_ACK              0x82
#define PC_RSP_NACK             0x83
#define PC_RSP_PONG             0x84

/* NACK error codes */
#define PC_ERR_BAD_CRC          0x01
#define PC_ERR_UNKNOWN_CMD      0x02
#define PC_ERR_BAD_LENGTH       0x03
#define PC_ERR_CAN_TX_FAIL      0x04

/* Status report (gửi định kỳ về PC) */
#pragma pack(push, 1)
typedef struct {
    float    voltage;        /* V đầu ra (chung, từ module đầu tiên) */
    float    total_current;  /* Tổng A từ tất cả module */
    float    temp_dcdc;      /* Nhiệt độ DCDC cao nhất */
    float    temp_ambient;   /* Nhiệt độ môi trường */
    uint32_t alarm_status;   /* OR tất cả alarm bits */
    uint32_t total_power_in; /* Tổng W input */
    uint8_t  modules_online; /* Số module đang chạy */
    uint8_t  modules_fault;  /* Số module lỗi */
    uint8_t  charging;       /* 1 = có module đang sạc */
    uint8_t  btn_start;      /* Trạng thái nút Start */
    uint8_t  btn_stop;       /* Trạng thái nút Stop */
} PC_StatusReport_t;        /* 31 bytes */
#pragma pack(pop)

/* ============== API ============== */

/** Nạp 1 byte từ USB CDC RX vào parser */
void PC_Protocol_FeedByte(uint8_t byte);

/** Gửi status report về PC (gọi định kỳ) */
void PC_Protocol_SendStatus(void);

/** Gửi PONG response */
void PC_Protocol_SendPong(void);

/** Kiểm tra trạng thái charging (do PC đặt) */
bool PC_Protocol_IsCharging(void);

/** Firmware version */
#define FW_VERSION_MAJOR    2
#define FW_VERSION_MINOR    0
#define FW_VERSION_PATCH    0

#endif /* PC_PROTOCOL_H */
