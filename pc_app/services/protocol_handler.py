"""
Protocol Handler - Frame parsing và command/response handling
Xử lý protocol binary giữa PC và STM32
"""

import struct
import logging
from typing import Callable, Optional, Dict, Any
from dataclasses import dataclass, field

from models.charger_status import SystemStatus, BmsStatus
from models.charger_config import ChargerConfig

logger = logging.getLogger(__name__)

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
CMD_READ_CONFIG = 0x10
CMD_WRITE_CONFIG = 0x11
CMD_READ_ALL_CONFIG = 0x12
CMD_WRITE_ALL_CONFIG = 0x13
CMD_SET_MODULE_COUNT = 0x14

# Responses (STM32 -> PC)
RSP_STATUS = 0x81
RSP_ACK = 0x82
RSP_NACK = 0x83
RSP_PONG = 0x84
RSP_REG_VALUE = 0x85
RSP_CONFIG = 0x90
RSP_BMS_STATUS = 0x91

# Error codes
ERR_BAD_CRC = 0x01
ERR_UNKNOWN_CMD = 0x02
ERR_BAD_LENGTH = 0x03
ERR_CAN_TX_FAIL = 0x04
ERR_BAD_PARAM = 0x05


# ============== Protocol Functions ==============

def crc8(data: bytes) -> int:
    """Tính CRC8"""
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
    """Đóng gói 1 frame"""
    header = bytes([SOF1, SOF2, cmd, len(payload)])
    crc_data = bytes([cmd, len(payload)]) + payload
    return header + payload + bytes([crc8(crc_data)])


# ============== RX State Machine ==============

class RxState:
    WAIT_SOF1 = 0
    WAIT_SOF2 = 1
    WAIT_CMD = 2
    WAIT_LEN = 3
    WAIT_PAYLOAD = 4
    WAIT_CRC = 5


@dataclass
class ProtocolHandler:
    """Protocol handler với state machine"""

    # Callbacks
    on_status: Optional[Callable[[SystemStatus], None]] = None
    on_bms_status: Optional[Callable[[BmsStatus], None]] = None
    on_config: Optional[Callable[[int, bytes], None]] = None
    on_pong: Optional[Callable[[int], None]] = None
    on_ack: Optional[Callable[[int], None]] = None
    on_nack: Optional[Callable[[int, int], None]] = None
    on_log: Optional[Callable[[str], None]] = None

    # Internal state
    _rx_state: int = RxState.WAIT_SOF1
    _rx_cmd: int = 0
    _rx_len: int = 0
    _rx_payload: bytearray = field(default_factory=bytearray)
    _rx_idx: int = 0

    def feed_byte(self, byte: int):
        """Nạp 1 byte vào state machine"""
        self._process_byte(byte)

    def feed_data(self, data: bytes):
        """Nạp nhiều byte"""
        for b in data:
            self._process_byte(b)

    def _process_byte(self, byte: int):
        """Xử lý 1 byte"""
        state = self._rx_state

        if state == RxState.WAIT_SOF1:
            if byte == SOF1:
                self._rx_state = RxState.WAIT_SOF2

        elif state == RxState.WAIT_SOF2:
            if byte == SOF2:
                self._rx_state = RxState.WAIT_CMD
            else:
                self._rx_state = RxState.WAIT_SOF1

        elif state == RxState.WAIT_CMD:
            self._rx_cmd = byte
            self._rx_state = RxState.WAIT_LEN

        elif state == RxState.WAIT_LEN:
            self._rx_len = byte
            self._rx_idx = 0

            if self._rx_len > 64:
                # Payload too large, reset
                self._log(f"RX: payload too large ({self._rx_len})")
                self._rx_state = RxState.WAIT_SOF1
            elif self._rx_len == 0:
                self._rx_state = RxState.WAIT_CRC
            else:
                self._rx_payload = bytearray()
                self._rx_state = RxState.WAIT_PAYLOAD

        elif state == RxState.WAIT_PAYLOAD:
            self._rx_payload.append(byte)
            self._rx_idx += 1
            if self._rx_idx >= self._rx_len:
                self._rx_state = RxState.WAIT_CRC

        elif state == RxState.WAIT_CRC:
            # Verify CRC
            crc_data = bytes([self._rx_cmd, self._rx_len]) + bytes(self._rx_payload)
            calc_crc = crc8(crc_data)

            if calc_crc == byte:
                self._handle_frame(self._rx_cmd, bytes(self._rx_payload))
            else:
                self._log(f"RX: CRC mismatch (expected 0x{byte:02X}, got 0x{calc_crc:02X})")

            self._rx_state = RxState.WAIT_SOF1

    def _handle_frame(self, cmd: int, payload: bytes):
        """Xử lý 1 frame hoàn chỉnh"""
        self._log(f"RX: cmd=0x{cmd:02X}, len={len(payload)}")

        if cmd == RSP_STATUS:
            try:
                status = SystemStatus.from_bytes(payload)
                if self.on_status:
                    self.on_status(status)
            except Exception as e:
                self._log(f"Error parsing status: {e}")

        elif cmd == RSP_BMS_STATUS:
            try:
                bms_status = BmsStatus.from_bytes(payload)
                if self.on_bms_status:
                    self.on_bms_status(bms_status)
            except Exception as e:
                self._log(f"Error parsing BMS status: {e}")

        elif cmd == RSP_CONFIG:
            if len(payload) >= 1:
                section = payload[0]
                data = payload[1:]
                if self.on_config:
                    self.on_config(section, data)

        elif cmd == RSP_PONG:
            if len(payload) >= 4:
                version = struct.unpack("<I", payload[:4])[0]
                if self.on_pong:
                    self.on_pong(version)

        elif cmd == RSP_ACK:
            if len(payload) >= 1:
                if self.on_ack:
                    self.on_ack(payload[0])

        elif cmd == RSP_NACK:
            if len(payload) >= 2:
                err_cmd = payload[0]
                err_code = payload[1]
                if self.on_nack:
                    self.on_nack(err_cmd, err_code)
                self._log(f"NACK: cmd=0x{err_cmd:02X}, err={err_code}")

    def _log(self, msg: str):
        """Log message"""
        logger.debug(msg)
        if self.on_log:
            self.on_log(msg)


# ============== Command Builders ==============

def cmd_set_voltage(voltage: float) -> bytes:
    """CMD_SET_VOLTAGE"""
    payload = struct.pack("<f", voltage)
    return build_frame(CMD_SET_VOLTAGE, payload)


def cmd_set_current(current: float) -> bytes:
    """CMD_SET_CURRENT"""
    payload = struct.pack("<f", current)
    return build_frame(CMD_SET_CURRENT, payload)


def cmd_start() -> bytes:
    """CMD_START"""
    return build_frame(CMD_START)


def cmd_stop() -> bytes:
    """CMD_STOP"""
    return build_frame(CMD_STOP)


def cmd_emergency_stop() -> bytes:
    """CMD_EMERGENCY_STOP"""
    return build_frame(CMD_EMERGENCY_STOP)


def cmd_set_module_addr(addr: int, group: int = 0) -> bytes:
    """CMD_SET_MODULE_ADDR"""
    payload = bytes([addr, group])
    return build_frame(CMD_SET_MODULE_ADDR, payload)


def cmd_ping() -> bytes:
    """CMD_PING"""
    return build_frame(CMD_PING)


def cmd_set_driver(driver_id: int) -> bytes:
    """CMD_SET_DRIVER"""
    return build_frame(CMD_SET_DRIVER, bytes([driver_id]))


def cmd_read_config(section: int) -> bytes:
    """CMD_READ_CONFIG"""
    return build_frame(CMD_READ_CONFIG, bytes([section]))


def cmd_write_config(section: int, data: bytes) -> bytes:
    """CMD_WRITE_CONFIG, section + data"""
    return build_frame(CMD_WRITE_CONFIG, bytes([section]) + data)


def cmd_read_all_config() -> bytes:
    """CMD_READ_ALL_CONFIG"""
    return build_frame(CMD_READ_ALL_CONFIG)


def cmd_write_all_config(config: ChargerConfig) -> bytes:
    """CMD_WRITE_ALL_CONFIG"""
    return build_frame(CMD_WRITE_ALL_CONFIG, config.to_bytes())


def cmd_set_module_count(count: int) -> bytes:
    """CMD_SET_MODULE_COUNT"""
    return build_frame(CMD_SET_MODULE_COUNT, bytes([count]))


# ============== Error Names ==============

ERROR_NAMES = {
    ERR_BAD_CRC: "Bad CRC",
    ERR_UNKNOWN_CMD: "Unknown Command",
    ERR_BAD_LENGTH: "Bad Length",
    ERR_CAN_TX_FAIL: "CAN TX Fail",
    ERR_BAD_PARAM: "Bad Parameter",
}


def get_error_name(err_code: int) -> str:
    return ERROR_NAMES.get(err_code, f"Unknown Error {err_code}")


# ============== Additional Commands ==============
CMD_GET_STATUS = 0x20
CMD_HISTORY_GET = 0x21
CMD_HISTORY_CLEAR = 0x22
CMD_RESET_CONFIG = 0x23
CMD_FW_UPDATE = 0x30
CMD_GET_BMS_DATA = 0x40

# Additional Responses
RSP_HISTORY = 0x92
RSP_BMS_DATA = 0x93
RSP_FW_DATA = 0x94


# ============== Command Builders for Config ==============

def cmd_read_config(section: int) -> bytes:
    """CMD_READ_CONFIG - Read single section"""
    return build_frame(CMD_READ_CONFIG, bytes([section]))


def cmd_write_config(section: int, data: bytes) -> bytes:
    """CMD_WRITE_CONFIG - Write single section"""
    return build_frame(CMD_WRITE_CONFIG, bytes([section]) + data)


def cmd_read_all_config() -> bytes:
    """CMD_READ_ALL_CONFIG - Read all config"""
    return build_frame(CMD_READ_ALL_CONFIG)


def cmd_write_all_config(config_bytes: bytes) -> bytes:
    """CMD_WRITE_ALL_CONFIG - Write all config"""
    return build_frame(CMD_WRITE_ALL_CONFIG, config_bytes)


def cmd_get_status() -> bytes:
    """CMD_GET_STATUS - Request current status"""
    return build_frame(CMD_GET_STATUS)


def cmd_history_get(index: int) -> bytes:
    """CMD_HISTORY_GET - Get history record"""
    return build_frame(CMD_HISTORY_GET, bytes([index >> 8, index & 0xFF]))


def cmd_history_clear() -> bytes:
    """CMD_HISTORY_CLEAR - Clear all history"""
    return build_frame(CMD_HISTORY_CLEAR)


def cmd_reset_config() -> bytes:
    """CMD_RESET_CONFIG - Reset to defaults"""
    return build_frame(CMD_RESET_CONFIG)


def cmd_get_bms_data() -> bytes:
    """CMD_GET_BMS_DATA - Request BMS data"""
    return build_frame(CMD_GET_BMS_DATA)


# ============== Parse Helpers ==============

def parse_config_response(cmd: int, payload: bytes) -> tuple:
    """Parse config response - returns (section, data)"""
    if len(payload) < 1:
        return None, None
    section = payload[0]
    data = payload[1:]
    return section, data
