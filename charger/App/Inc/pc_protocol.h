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
#define PC_CMD_SET_DRIVER       0x09

/* Configuration commands */
#define PC_CMD_READ_CONFIG      0x10
#define PC_CMD_WRITE_CONFIG     0x11
#define PC_CMD_READ_ALL_CONFIG  0x12
#define PC_CMD_WRITE_ALL_CONFIG 0x13
#define PC_CMD_SET_MODULE_COUNT 0x14

/* Extended commands */
#define PC_CMD_GET_STATUS       0x20
#define PC_CMD_HISTORY_GET      0x21
#define PC_CMD_HISTORY_CLEAR    0x22
#define PC_CMD_RESET_CONFIG     0x23
#define PC_CMD_GET_BMS_DATA     0x40
#define PC_CMD_FW_UPDATE        0x30

/* Responses STM32 -> PC */
#define PC_RSP_STATUS           0x81
#define PC_RSP_ACK              0x82
#define PC_RSP_NACK             0x83
#define PC_RSP_PONG             0x84
#define PC_RSP_READ_REG         0x85
#define PC_RSP_CONFIG           0x90
#define PC_RSP_BMS_STATUS       0x91
#define PC_RSP_HISTORY          0x92
#define PC_RSP_BMS_DATA         0x93
#define PC_RSP_FW_DATA          0x94

/* NACK error codes */
#define PC_ERR_BAD_CRC          0x01
#define PC_ERR_UNKNOWN_CMD      0x02
#define PC_ERR_BAD_LENGTH       0x03
#define PC_ERR_CAN_TX_FAIL      0x04
#define PC_ERR_BAD_PARAM        0x05

/* Firmware version */
#define FW_VERSION_MAJOR    2
#define FW_VERSION_MINOR    0
#define FW_VERSION_PATCH    0

/* ============== API ============== */

/** Feed one byte from USB CDC RX to parser */
void PC_Protocol_FeedByte(uint8_t byte);

/** Send status report to PC (call periodically) */
void PC_Protocol_SendStatus(void);

/** Send PONG response */
void PC_Protocol_SendPong(void);

/** Check if charging state (set by PC START command) */
bool PC_Protocol_IsCharging(void);

/** CRC8 calculation */
uint8_t PC_CRC8(const uint8_t *data, uint16_t len);

#endif /* PC_PROTOCOL_H */
