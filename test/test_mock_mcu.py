"""
Mock STM32 MCU - Simulates firmware behavior
Tests the complete PC <-> MCU communication without hardware
"""

import struct
import time
from dataclasses import dataclass, field
from typing import Optional

# ============== Protocol Constants (matching firmware) ==============
SOF1 = 0xAA
SOF2 = 0x55
CRC8_POLY = 0x07
MAX_PAYLOAD = 64

# Commands PC -> MCU
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

# Responses MCU -> PC
RSP_STATUS = 0x81
RSP_ACK = 0x82
RSP_NACK = 0x83
RSP_PONG = 0x84
RSP_READ_REG = 0x85
RSP_CONFIG = 0x90

# Config Sections
CFG_SECTION_SYSTEM = 0x01
CFG_SECTION_CHARGER = 0x02
CFG_SECTION_MODULE = 0x03
CFG_SECTION_PROTECT = 0x04
CFG_SECTION_BMS = 0x05
CFG_SECTION_DISPLAY = 0x06


# ============== Config Structures ==============

@dataclass
class ConfigSystem:
    driver_id: int = 1       # 1=Maxwell
    module_count: int = 1
    fw_version: int = 0x00020000
    hw_version: int = 0x0100
    serial_number: int = 1234
    
    def to_bytes(self) -> bytes:
        data = struct.pack("<BB", self.driver_id, self.module_count)
        data += struct.pack("<BB", 0, 0)  # reserved
        data += struct.pack("<I", self.fw_version)
        data += struct.pack("<HH", self.hw_version, self.serial_number & 0xFFFF)
        return data
    
    @classmethod
    def from_bytes(cls, data: bytes) -> 'ConfigSystem':
        driver_id = data[0]
        module_count = data[1]
        fw_version = struct.unpack("<I", data[8:12])[0]
        hw_version, serial = struct.unpack("<HH", data[16:20])
        return cls(driver_id=driver_id, module_count=module_count, 
                   fw_version=fw_version, hw_version=hw_version, serial_number=serial)


@dataclass
class ConfigCharger:
    target_voltage: float = 57.6
    target_current: float = 20.0
    max_voltage: float = 60.0
    max_current: float = 25.0
    charge_timeout: int = 480
    
    def to_bytes(self) -> bytes:
        return struct.pack('<ffffBBH',
            self.target_voltage, self.target_current,
            self.max_voltage, self.max_current,
            0, 0, self.charge_timeout)


@dataclass
class RuntimeStatus:
    """Runtime status sent to PC"""
    voltage: float = 0.0
    total_current: float = 0.0
    temp_dcdc: float = 25.0
    temp_ambient: float = 25.0
    alarm_status: int = 0
    total_power_in: int = 0
    modules_online: int = 0
    modules_fault: int = 0
    charging: int = 0
    btn_start: int = 0
    btn_stop: int = 0
    
    def to_bytes(self) -> bytes:
        data = struct.pack("<f", self.voltage)
        data += struct.pack("<f", self.total_current)
        data += struct.pack("<f", self.temp_dcdc)
        data += struct.pack("<f", self.temp_ambient)
        data += struct.pack("<I", self.alarm_status)
        data += struct.pack("<I", self.total_power_in)
        data += struct.pack("<BBB", self.modules_online, self.modules_fault, self.charging)
        return data


# ============== CRC8 ==============

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


# ============== Mock STM32 ==============

class MockSTM32:
    """Simulates STM32 firmware behavior"""
    
    def __init__(self):
        # Config (stored in "flash")
        self.config_system = ConfigSystem()
        self.config_charger = ConfigCharger()
        
        # Runtime state
        self.charging = False
        self.target_voltage = 57.6
        self.target_current = 1.0
        self.modules_online = 0
        self.alarm_status = 0
        
        # RX state machine
        self.rx_state = 'WAIT_SOF1'
        self.rx_cmd = 0
        self.rx_len = 0
        self.rx_payload = bytearray()
        
        # TX buffer
        self.tx_buffer = bytearray()
    
    def feed_byte(self, byte: int) -> Optional[bytes]:
        """Process one byte, return response if complete frame"""
        self._process_byte(byte)
        
        if self.tx_buffer:
            response = bytes(self.tx_buffer)
            self.tx_buffer = bytearray()
            return response
        return None
    
    def _process_byte(self, byte: int):
        """RX state machine"""
        if self.rx_state == 'WAIT_SOF1':
            if byte == SOF1:
                self.rx_state = 'WAIT_SOF2'
        
        elif self.rx_state == 'WAIT_SOF2':
            if byte == SOF2:
                self.rx_state = 'WAIT_CMD'
            else:
                self.rx_state = 'WAIT_SOF1'
        
        elif self.rx_state == 'WAIT_CMD':
            self.rx_cmd = byte
            self.rx_state = 'WAIT_LEN'
        
        elif self.rx_state == 'WAIT_LEN':
            self.rx_len = byte
            self.rx_payload = bytearray()
            if self.rx_len == 0:
                self.rx_state = 'WAIT_CRC'
            elif self.rx_len > MAX_PAYLOAD:
                self.rx_state = 'WAIT_SOF1'
            else:
                self.rx_state = 'WAIT_PAYLOAD'
        
        elif self.rx_state == 'WAIT_PAYLOAD':
            self.rx_payload.append(byte)
            if len(self.rx_payload) >= self.rx_len:
                self.rx_state = 'WAIT_CRC'
        
        elif self.rx_state == 'WAIT_CRC':
            # Verify CRC
            crc_data = bytes([self.rx_cmd, self.rx_len]) + bytes(self.rx_payload)
            calc_crc = crc8(crc_data)
            
            if calc_crc == byte:
                self._handle_frame()
            else:
                self._send_nack(self.rx_cmd, 0x01)  # BAD_CRC
            
            self.rx_state = 'WAIT_SOF1'
    
    def _handle_frame(self):
        """Process complete frame"""
        cmd = self.rx_cmd
        payload = bytes(self.rx_payload)
        
        if cmd == CMD_PING:
            self._send_pong()
        
        elif cmd == CMD_SET_VOLTAGE:
            if len(payload) == 4:
                self.target_voltage = struct.unpack('<f', payload)[0]
                self._send_ack(cmd)
            else:
                self._send_nack(cmd, 0x03)
        
        elif cmd == CMD_SET_CURRENT:
            if len(payload) == 4:
                self.target_current = struct.unpack('<f', payload)[0]
                self._send_ack(cmd)
            else:
                self._send_nack(cmd, 0x03)
        
        elif cmd == CMD_START:
            self.charging = True
            self.modules_online = self.config_system.module_count
            self._send_ack(cmd)
        
        elif cmd == CMD_STOP:
            self.charging = False
            self._send_ack(cmd)
        
        elif cmd == CMD_READ_CONFIG:
            if len(payload) >= 1:
                section = payload[0]
                self._send_config(section)
            else:
                self._send_nack(cmd, 0x03)
        
        elif cmd == CMD_WRITE_CONFIG:
            if len(payload) >= 1:
                section = payload[0]
                data = payload[1:]
                self._save_config(section, data)
                self._send_ack(cmd)
            else:
                self._send_nack(cmd, 0x03)
        
        elif cmd == CMD_READ_ALL_CONFIG:
            self._send_all_config()
        
        elif cmd == CMD_SET_DRIVER:
            if len(payload) >= 1:
                self.config_system.driver_id = payload[0]
                self._send_ack(cmd)
            else:
                self._send_nack(cmd, 0x03)
        
        else:
            self._send_nack(cmd, 0x02)  # UNKNOWN_CMD
    
    def _save_config(self, section: int, data: bytes):
        """Save config (simulate flash write)"""
        if section == CFG_SECTION_SYSTEM:
            self.config_system = ConfigSystem.from_bytes(data)
        # Add other sections as needed
    
    def _send_ack(self, cmd: int):
        self._build_and_send(RSP_ACK, bytes([cmd]))
    
    def _send_nack(self, cmd: int, err: int):
        self._build_and_send(RSP_NACK, bytes([cmd, err]))
    
    def _send_pong(self):
        ver = struct.pack('<I', self.config_system.fw_version)
        self._build_and_send(RSP_PONG, ver)
    
    def _send_config(self, section: int):
        if section == CFG_SECTION_SYSTEM:
            data = bytes([section]) + self.config_system.to_bytes()
            self._build_and_send(RSP_CONFIG, data)
        elif section == CFG_SECTION_CHARGER:
            data = bytes([section]) + self.config_charger.to_bytes()
            self._build_and_send(RSP_CONFIG, data)
    
    def _send_all_config(self):
        data = bytes([CFG_SECTION_SYSTEM]) + self.config_system.to_bytes()
        data += bytes([CFG_SECTION_CHARGER]) + self.config_charger.to_bytes()
        self._build_and_send(RSP_CONFIG, data)
    
    def _build_and_send(self, cmd: int, payload: bytes):
        frame = bytes([SOF1, SOF2, cmd, len(payload)])
        crc_data = bytes([cmd, len(payload)]) + payload
        frame += payload + bytes([crc8(crc_data)])
        self.tx_buffer.extend(frame)
    
    def get_status(self) -> bytes:
        """Generate status report"""
        status = RuntimeStatus(
            voltage=self.target_voltage if self.charging else 0.0,
            total_current=self.target_current * 10 if self.charging else 0.0,
            temp_dcdc=35.0,
            temp_ambient=25.0,
            alarm_status=self.alarm_status,
            total_power_in=int(self.target_voltage * self.target_current * 10) if self.charging else 0,
            modules_online=self.modules_online if self.charging else 0,
            charging=1 if self.charging else 0
        )
        
        payload = status.to_bytes()
        frame = bytes([SOF1, SOF2, RSP_STATUS, len(payload)])
        crc_data = bytes([RSP_STATUS, len(payload)]) + payload
        frame += payload + bytes([crc8(crc_data)])
        
        return frame


# ============== Tests ==============

def test_ping_pong():
    """Test PING/PONG exchange"""
    print("\n=== Test PING/PONG ===")
    
    mcu = MockSTM32()
    
    # Send PING command
    ping_frame = bytes([SOF1, SOF2, CMD_PING, 0, crc8(bytes([CMD_PING, 0]))])
    print(f"  PC -> MCU: {ping_frame.hex()}")
    
    response = mcu.feed_byte(ping_frame[0])
    for b in ping_frame[1:]:
        response = mcu.feed_byte(b)
    
    print(f"  MCU -> PC: {response.hex()}")
    
    assert response[2] == RSP_PONG
    version = struct.unpack('<I', response[4:8])[0]
    print(f"  FW Version: 0x{version:08X}")
    assert version == 0x00020000
    
    print("  ✓ PING/PONG test passed")


def test_set_voltage():
    """Test SET_VOLTAGE command"""
    print("\n=== Test SET_VOLTAGE ===")
    
    mcu = MockSTM32()
    
    voltage = 54.0
    payload = struct.pack('<f', voltage)
    frame = build_frame(CMD_SET_VOLTAGE, payload)
    print(f"  PC -> MCU: {frame.hex()}")
    
    for b in frame:
        resp = mcu.feed_byte(b)
    
    if resp:
        print(f"  MCU -> PC: {resp.hex()}")
        assert resp[2] == RSP_ACK
    
    assert mcu.target_voltage == voltage
    print(f"  Target voltage set to: {mcu.target_voltage}V")
    print("  ✓ SET_VOLTAGE test passed")


def test_start_charging():
    """Test START command"""
    print("\n=== Test START ===")
    
    mcu = MockSTM32()
    
    # Set voltage first
    mcu.target_voltage = 57.6
    
    # Send START
    frame = build_frame(CMD_START)
    print(f"  PC -> MCU: {frame.hex()}")
    
    for b in frame:
        resp = mcu.feed_byte(b)
    
    if resp:
        print(f"  MCU -> PC: {resp.hex()}")
        assert resp[2] == RSP_ACK
    
    assert mcu.charging == True
    print(f"  Charging: {mcu.charging}")
    print("  ✓ START test passed")


def test_read_config():
    """Test READ_CONFIG"""
    print("\n=== Test READ_CONFIG ===")
    
    mcu = MockSTM32()
    
    # Read SYSTEM config
    frame = build_frame(CMD_READ_CONFIG, bytes([CFG_SECTION_SYSTEM]))
    print(f"  PC -> MCU: {frame.hex()}")
    
    for b in frame:
        resp = mcu.feed_byte(b)
    
    if resp:
        print(f"  MCU -> PC: {resp.hex()}")
        assert resp[2] == RSP_CONFIG
        section = resp[4]
        print(f"  Section: 0x{section:02X}")
    
    print("  ✓ READ_CONFIG test passed")


def test_status_report():
    """Test STATUS report generation"""
    print("\n=== Test STATUS Report ===")
    
    mcu = MockSTM32()
    mcu.charging = True
    mcu.target_voltage = 57.6
    mcu.target_current = 0.8
    mcu.modules_online = 1
    
    status = mcu.get_status()
    print(f"  Status frame: {status.hex()}")
    
    assert status[2] == RSP_STATUS
    voltage = struct.unpack('<f', status[4:8])[0]
    current = struct.unpack('<f', status[8:12])[0]
    print(f"  Voltage: {voltage}V, Current: {current}A")
    
    print("  ✓ STATUS test passed")


def build_frame(cmd: int, payload: bytes = b'') -> bytes:
    """Build a protocol frame"""
    frame = bytes([SOF1, SOF2, cmd, len(payload)])
    crc_data = bytes([cmd, len(payload)]) + payload
    return frame + payload + bytes([crc8(crc_data)])


def main():
    print("=" * 60)
    print("Mock STM32 Tests")
    print("=" * 60)
    
    test_ping_pong()
    test_set_voltage()
    test_start_charging()
    test_read_config()
    test_status_report()
    
    print("\n" + "=" * 60)
    print("ALL TESTS PASSED ✓")
    print("=" * 60)


if __name__ == '__main__':
    main()
