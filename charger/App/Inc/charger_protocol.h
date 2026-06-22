#ifndef CHARGER_PROTOCOL_H
#define CHARGER_PROTOCOL_H

#include <stdint.h>

#define CHG_PROTO_CAN_PROTNO            0x060U
#define CHG_PROTO_CAN_PTP_BROADCAST     0U
#define CHG_PROTO_CAN_PTP_POINT         1U
#define CHG_PROTO_CAN_ADDR_CONTROLLER   0xF0U
#define CHG_PROTO_CAN_ADDR_BROADCAST    0xFFU
#define CHG_PROTO_CAN_ADDR_GROUP_BCAST  0xFEU
#define CHG_PROTO_CAN_FUNC_SET          0x03U
#define CHG_PROTO_CAN_FUNC_READ         0x10U
#define CHG_PROTO_CAN_RESP_FLOAT        0x41U
#define CHG_PROTO_CAN_RESP_INT          0x42U
#define CHG_PROTO_CAN_RESP_OK           0xF0U
#define CHG_PROTO_CAN_RESP_FAIL         0xF2U

/* =========================================================
 * CANONICAL CHARGER REGISTERS (Dùng chung)
 * ========================================================= */
#define CHG_REG_VOLTAGE                 0x0001
#define CHG_REG_CURRENT                 0x0002
#define CHG_REG_CURR_LIMIT              0x0003
#define CHG_REG_TEMP_DCDC               0x0004
#define CHG_REG_TEMP_AMBIENT            0x000B
#define CHG_REG_RATED_POWER             0x0011
#define CHG_REG_RATED_CURRENT           0x0012
#define CHG_REG_SET_POWER               0x0020
#define CHG_REG_SET_VOLTAGE             0x0021
#define CHG_REG_SET_CURR_LIMIT          0x0022
#define CHG_REG_SET_OVP                 0x0023
#define CHG_REG_ON_OFF                  0x0030
#define CHG_REG_ALARM_STATUS            0x0040
#define CHG_REG_GROUP_ADDR              0x0043
#define CHG_REG_SHORT_RESET             0x0044
#define CHG_REG_INPUT_MODE_SET          0x0046
#define CHG_REG_INPUT_POWER             0x0048
#define CHG_REG_INPUT_MODE_RD           0x004B

static inline uint32_t CHG_ProtocolBuildCanId(uint8_t dst_addr,
                                              uint8_t src_addr,
                                              uint8_t ptp,
                                              uint8_t group)
{
    uint32_t id = 0;
    id |= ((uint32_t)CHG_PROTO_CAN_PROTNO & 0x1FFU) << 20;
    id |= ((uint32_t)ptp & 0x01U) << 19;
    id |= ((uint32_t)dst_addr & 0xFFU) << 11;
    id |= ((uint32_t)src_addr & 0xFFU) << 3;
    id |= ((uint32_t)group & 0x07U);
    return id;
}

static inline void CHG_ProtocolFloatToBE(float value, uint8_t *out)
{
    union { float f; uint8_t b[4]; } u;
    u.f = value;
    out[0] = u.b[3];
    out[1] = u.b[2];
    out[2] = u.b[1];
    out[3] = u.b[0];
}

static inline float CHG_ProtocolBEToFloat(const uint8_t *in)
{
    union { float f; uint8_t b[4]; } u;
    u.b[3] = in[0];
    u.b[2] = in[1];
    u.b[1] = in[2];
    u.b[0] = in[3];
    return u.f;
}

static inline uint32_t CHG_ProtocolBEToU32(const uint8_t *in)
{
    return ((uint32_t)in[0] << 24) |
           ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8) |
           ((uint32_t)in[3]);
}

static inline void CHG_ProtocolU32ToBE(uint32_t value, uint8_t *out)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

#endif /* CHARGER_PROTOCOL_H */