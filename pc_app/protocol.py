"""
Protocol definitions shared between PC App and tests
"""

import struct

# ============== Protocol Constants ==============
SOF1 = 0xAA
SOF2 = 0x55
CRC8_POLY = 0x07

# Commands (PC -> STM32)
CMD_SET_VOLTAGE = 0x01
CMD_SET_CURRENT = 0x02
CMD_START = 0x03
CMD_STOP = 0x04
CMD_SET_MODULE_ADDR = 0x05
CMD_PING = 0x06
CMD_READ_REG = 0x07
CMD_EMERGENCY_STOP = 0x08
CMD_SET_DRIVER = 0x09

# Responses (STM32 -> PC)
RSP_STATUS = 0x81
RSP_ACK = 0x82
RSP_NACK = 0x83
RSP_PONG = 0x84
RSP_READ_REG = 0x85

# Error codes
ERR_BAD_CRC = 0x01
ERR_UNKNOWN_CMD = 0x02
ERR_BAD_LENGTH = 0x03
ERR_CAN_TX_FAIL = 0x04
ERR_BAD_PARAM = 0x05

# Driver IDs
DRIVER_NONE = 0
DRIVER_MAXWELL = 1
DRIVER_LIANMING = 2
DRIVER_TONHE = 3

DRIVER_NAMES = {
    DRIVER_NONE: "None",
    DRIVER_MAXWELL: "Maxwell MXR",
    DRIVER_LIANMING: "LIANMING",
    DRIVER_TONHE: "TONHE V1.3",
}

# Alarm bit definitions (Maxwell)
MAXWELL_ALARM_BITS = {
    0: "Module Fault",
    1: "Module Protect",
    4: "Input Error",
    6: "SCI Failure",
    8: "DCDC Overvoltage",
    9: "PFC Abnormal",
    14: "AC Undervoltage",
    16: "CAN Failure",
    17: "Current Imbalance",
    22: "DCDC Off",
    23: "Power Limit",
    24: "Temp Derating",
    25: "AC Power Limit",
    27: "Fan Failure",
    28: "Short Circuit",
    30: "DCDC Overtemp",
    31: "Output Overvoltage",
}

# Alarm bit definitions (TONHE)
TONHE_ALARM_BITS = {
    0: "Input Undervoltage",
    1: "Input Phase Loss",
    2: "Input Overvoltage",
    3: "Output Overvoltage",
    4: "Output Overcurrent",
    5: "Temp High",
    6: "Fan Fault",
    7: "Hardware Fault",
    8: "Bus Exception",
    9: "SCI Comm Exception",
    10: "Discharge Fault",
    11: "PFC Shutdown",
    12: "Output Undervoltage Warning",
    13: "Output Overvoltage Warning",
    14: "Power Limit (Temp)",
    15: "Short Circuit",
}


# ============== Protocol Functions ==============

def crc8(data: bytes) -> int:
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ CRC8_POLY) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def build_frame(cmd: int, payload: bytes = b"") -> bytes:
    header = bytes([SOF1, SOF2, cmd, len(payload)])
    crc_data = bytes([cmd, len(payload)]) + payload
    return header + payload + bytes([crc8(crc_data)])


def parse_status(payload: bytes) -> dict:
    """Parse PC_StatusReport_t (29 bytes, packed, little-endian)"""
    if len(payload) < 29:
        return None

    voltage, current, temp_dcdc, temp_ambient, alarm, input_power, \
    modules_online, modules_fault, charging, btn_start, btn_stop = \
        struct.unpack("<ffffIIBBBBB", payload[:29])

    return {
        "voltage": voltage,
        "current": current,
        "temp_dcdc": temp_dcdc,
        "temp_ambient": temp_ambient,
        "alarm": alarm,
        "input_power": input_power,
        "modules_online": modules_online,
        "modules_fault": modules_fault,
        "charging": bool(charging),
        "btn_start": bool(btn_start),
        "btn_stop": bool(btn_stop),
    }


def get_alarm_text(alarm: int, driver: int) -> str:
    """Get alarm text based on driver type"""
    if alarm == 0:
        return "No alarms"

    if driver == DRIVER_TONHE:
        alarm_bits = TONHE_ALARM_BITS
    else:
        alarm_bits = MAXWELL_ALARM_BITS

    active = [name for bit, name in alarm_bits.items() if alarm & (1 << bit)]
    return " | ".join(active) if active else "Unknown alarm"


# ============== CAN Protocol Builders ==============

def build_maxwell_id(protno, ptp, dst, src, grp=0):
    """Build Maxwell 29-bit CAN ID

    Structure: [28:20]=PROTNO(9), [19]=PTP(1), [18:17]=RSV(2), [16:9]=DST(8), [8:1]=SRC(8), [0]=GRP(1)
    """
    id = 0
    id |= ((protno & 0x1FF) << 20)  # PROTNO in bits 20-28
    id |= ((ptp & 0x01) << 19)         # PTP in bit 19
    # RSV bits 17-18 = 0
    id |= ((dst & 0xFF) << 9)          # DST in bits 9-16
    id |= ((src & 0xFF) << 1)          # SRC in bits 1-8
    id |= (grp & 0x01)                  # GRP in bit 0
    return id


def build_tonhe_id(priority, pf, ps, sa):
    """Build TONHE/J1939 29-bit CAN ID"""
    id = 0
    id |= ((priority & 0x07) << 26)
    id |= ((pf & 0xFF) << 16)
    id |= ((ps & 0xFF) << 8)
    id |= (sa & 0xFF)
    return id


def parse_maxwell_id(ext_id):
    """Parse Maxwell 29-bit CAN ID"""
    protno = (ext_id >> 20) & 0x1FF
    ptp = (ext_id >> 19) & 0x01
    dst = (ext_id >> 9) & 0xFF
    src = (ext_id >> 1) & 0xFF
    grp = ext_id & 0