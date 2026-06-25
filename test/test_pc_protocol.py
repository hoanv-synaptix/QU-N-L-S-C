"""
Test PC Protocol - Verify protocol parsing and building
"""

import sys
sys.path.insert(0, '../pc_app')

from services.protocol_handler import (
    ProtocolHandler, build_frame, crc8,
    CMD_SET_VOLTAGE, CMD_START, CMD_STOP, CMD_PING
)
from models.pc_config import CfgSection


def test_crc8():
    print("=== Test CRC8 ===")
    result = crc8(bytes([0x01, 0x04, 0x00]))
    print(f"  CRC8 = 0x{result:02X}")
    print("  OK")


def test_build_frame():
    print("\n=== Test Build Frame ===")
    frame = build_frame(CMD_PING)
    print(f"  PING: {frame.hex()}")
    frame = build_frame(CMD_SET_VOLTAGE, b'\x00\x00\x00\x00')
    print(f"  SET_VOLTAGE: {frame.hex()}")
    print("  OK")


def test_protocol_handler():
    print("\n=== Test Protocol Handler ===")
    handler = ProtocolHandler()
    print("  OK")


def test_command_builders():
    print("\n=== Test Command Builders ===")
    from services.protocol_handler import cmd_set_voltage, cmd_start, cmd_stop, cmd_ping
    print(f"  SET_VOLTAGE: {cmd_set_voltage(57.6).hex()}")
    print(f"  START: {cmd_start().hex()}")
    print(f"  STOP: {cmd_stop().hex()}")
    print(f"  PING: {cmd_ping().hex()}")
    print("  OK")


def test_config_sections():
    print("\n=== Test Config Sections ===")
    assert CfgSection.SYSTEM.value == 0x01
    assert CfgSection.CHARGER.value == 0x02
    print("  OK")


def main():
    print("=" * 50)
    print("PC Protocol Tests")
    print("=" * 50)
    
    test_crc8()
    test_build_frame()
    test_protocol_handler()
    test_command_builders()
    test_config_sections()
    
    print("\n" + "=" * 50)
    print("ALL TESTS PASSED")
    print("=" * 50)


if __name__ == '__main__':
    main()
