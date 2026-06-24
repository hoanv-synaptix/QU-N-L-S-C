#!/usr/bin/env python3
"""
Mock STM32 firmware for testing
Simulates CAN bus and USB CDC responses
"""

import struct
import time
import threading
import queue
import sys
sys.path.insert(0, "../pc_app")

from protocol import (
    SOF1, SOF2,
    CMD_SET_VOLTAGE, CMD_SET_CURRENT, CMD_START, CMD_STOP,
    CMD_SET_MODULE_ADDR, CMD_PING, CMD_EMERGENCY_STOP, CMD_SET_DRIVER,
    RSP_STATUS, RSP_ACK, RSP_NACK, RSP_PONG,
    ERR_BAD_CRC, ERR_UNKNOWN_CMD, ERR_BAD_LENGTH, ERR_BAD_PARAM
)

# Simulated module state
class MockModule:
    def __init__(self, addr=0):
        self.addr = addr
        self.voltage = 0.0
        self.current = 0.0
        self.current_limit = 20.0  # Amps
        self.temp_dcdc = 25.0
        self.temp_ambient = 25.0
        self.alarm = 0
        self.running = False
        self.input_power = 0

    def get_status_bytes(self):
        return struct.pack(
            "<ffffIIBBBBB",
            self.voltage,
            self.current,
            self.temp_dcdc,
            self.temp_ambient,
            self.alarm,
            int(self.input_power),
            1 if self.running else 0,
            0,  # modules_fault
            1 if self.running else 0,  # charging
            0,  # btn_start
            0,  # btn_stop
        )

class MockSTM32:
    def __init__(self):
        self.modules = [MockModule(0)]  # Default module 0
        self.current_driver = 1  # Maxwell
        self.fw_version = (2, 0, 0)
        self.set_voltage = 54.6
        self.set_current = 20.0
        self.running = False
        self.rx_queue = queue.Queue()

    def crc8(self, data):
        crc = 0x00
        for byte in data:
            crc ^= byte
            for _ in range(8):
                if crc & 0x80:
                    crc = ((crc << 1) ^ 0x07) & 0xFF
                else:
                    crc = (crc << 1) & 0xFF
        return crc

    def build_frame(self, cmd, payload=b""):
        header = bytes([SOF1, SOF2, cmd, len(payload)])
        crc_data = bytes([cmd, len(payload)]) + payload
        return header + payload + bytes([self.crc8(crc_data)])

    def process_command(self, cmd, payload):
        """Process a command and return response"""
        print(f"  [STM32] Processing cmd=0x{cmd:02X} len={len(payload)}")

        if cmd == CMD_SET_VOLTAGE:
            if len(payload) != 4:
                return self.build_frame(RSP_NACK, bytes([cmd, ERR_BAD_LENGTH]))
            self.set_voltage, = struct.unpack("<f", payload)
            print(f"  [STM32] Set voltage = {self.set_voltage}V")
            return self.build_frame(RSP_ACK, bytes([cmd]))

        elif cmd == CMD_SET_CURRENT:
            if len(payload) != 4:
                return self.build_frame(RSP_NACK, bytes([cmd, ERR_BAD_LENGTH]))
            self.set_current, = struct.unpack("<f", payload)
            print(f"  [STM32] Set current = {self.set_current}A")
            # Update module current_limit
            for mod in self.modules:
                mod.current_limit = self.set_current
            return self.build_frame(RSP_ACK, bytes([cmd]))

        elif cmd == CMD_START:
            self.running = True
            for mod in self.modules:
                mod.running = True
                mod.current = mod.current_limit
                mod.input_power = mod.voltage * mod.current
            print(f"  [STM32] START - modules running")
            return self.build_frame(RSP_ACK, bytes([cmd]))

        elif cmd == CMD_STOP:
            self.running = False
            for mod in self.modules:
                mod.running = False
                mod.current = 0
                mod.input_power = 0
            print(f"  [STM32] STOP - modules stopped")
            return self.build_frame(RSP_ACK, bytes([cmd]))

        elif cmd == CMD_EMERGENCY_STOP:
            self.running = False
            for mod in self.modules:
                mod.running = False
                mod.current = 0
                mod.alarm = 0x01  # Hardware fault
            print(f"  [STM32] EMERGENCY STOP")
            return self.build_frame(RSP_ACK, bytes([cmd]))

        elif cmd == CMD_SET_MODULE_ADDR:
            if len(payload) < 2:
                return self.build_frame(RSP_NACK, bytes([cmd, ERR_BAD_LENGTH]))
            addr = payload[0]
            print(f"  [STM32] Set module address = {addr}")
            return self.build_frame(RSP_ACK, bytes([cmd]))

        elif cmd == CMD_SET_DRIVER:
            if len(payload) < 1:
                return self.build_frame(RSP_NACK, bytes([cmd, ERR_BAD_LENGTH]))
            self.current_driver = payload[0]
            driver_names = {1: "Maxwell", 2: "LIANMING", 3: "TONHE"}
            print(f"  [STM32] Set driver = {driver_names.get(self.current_driver, 'Unknown')}")
            return self.build_frame(RSP_ACK, bytes([cmd]))

        elif cmd == CMD_PING:
            ver = (self.fw_version[0] << 16) | (self.fw_version[1] << 8) | self.fw_version[2]
            ver_bytes = struct.pack("<I", ver)
            return self.build_frame(RSP_PONG, ver_bytes)

        else:
            return self.build_frame(RSP_NACK, bytes([cmd, ERR_UNKNOWN_CMD]))

    def get_status(self):
        """Generate status report"""
        # Use first module for now
        mod = self.modules[0]
        return self.build_frame(RSP_STATUS, mod.get_status_bytes())

    def process_rx_buffer(self, buf):
        """Process received bytes, return responses"""
        responses = []
        while len(buf) >= 5:
            # Find SOF
            idx = -1
            for i in range(len(buf) - 1):
                if buf[i] == SOF1 and buf[i+1] == SOF2:
                    idx = i
                    break
            if idx < 0:
                buf.clear()
                return responses
            if idx > 0:
                del buf[:idx]

            if len(buf) < 4:
                break

            cmd = buf[2]
            plen = buf[3]
            total = 4 + plen + 1

            if len(buf) < total:
                break

            frame = bytes(buf[:total])
            del buf[:total]

            # Verify CRC
            crc_data = frame[2:4+plen]
            if self.crc8(crc_data) != frame[-1]:
                print(f"  [STM32] CRC mismatch!")
                responses.append(self.build_frame(RSP_NACK, bytes([cmd, ERR_BAD_CRC])))
                continue

            payload = frame[4:4+plen]
            resp = self.process_command(cmd, payload)
            if resp:
                responses.append(resp)

            # Also send status after command
            time.sleep(0.01)  # Small delay
            responses.append(self.get_status())

        return responses


def test_mock_stm32():
    """Test mock STM32"""
    print("=" * 60)
    print("Mock STM32 Test Suite")
    print("=" * 60)
    print()

    stm32 = MockSTM32()
    buf = bytearray()

    # Test 1: PING
    print("=== Test 1: PING ===")
    frame = stm32.build_frame(CMD_PING)
    print(f"  TX: {frame.hex()}")
    responses = stm32.process_rx_buffer(bytearray(frame))
    for resp in responses:
        print(f"  RX: {resp.hex()}")
        cmd = resp[2]
        if cmd == RSP_PONG:
            ver, = struct.unpack("<I", resp[4:8])
            major = (ver >> 16) & 0xFF
            minor = (ver >> 8) & 0xFF
            patch = ver & 0xFF
            print(f"  ✓ PONG: v{major}.{minor}.{patch}")
    print()

    # Test 2: SET_VOLTAGE
    print("=== Test 2: SET_VOLTAGE 54.6V ===")
    frame = stm32.build_frame(CMD_SET_VOLTAGE, struct.pack("<f", 54.6))
    print(f"  TX: {frame.hex()}")
    responses = stm32.process_rx_buffer(bytearray(frame))
    for resp in responses:
        print(f"  RX: {resp.hex()}")
        cmd = resp[2]
        if cmd == RSP_ACK:
            print(f"  ✓ ACK received")
        elif cmd == RSP_STATUS:
            print(f"  ✓ Status received")
    print()

    # Test 3: SET_CURRENT (Amps)
    print("=== Test 3: SET_CURRENT 20A ===")
    frame = stm32.build_frame(CMD_SET_CURRENT, struct.pack("<f", 20.0))
    print(f"  TX: {frame.hex()}")
    responses = stm32.process_rx_buffer(bytearray(frame))
    for resp in responses:
        print(f"  RX: {resp.hex()}")
    print()

    # Test 4: START
    print("=== Test 4: START ===")
    frame = stm32.build_frame(CMD_START)
    print(f"  TX: {frame.hex()}")
    responses = stm32.process_rx_buffer(bytearray(frame))
    for resp in responses:
        print(f"  RX: {resp.hex()}")
    print()

    # Test 5: STOP
    print("=== Test 5: STOP ===")
    frame = stm32.build_frame(CMD_STOP)
    print(f"  TX: {frame.hex()}")
    responses = stm32.process_rx_buffer(bytearray(frame))
    for resp in responses:
        print(f"  RX: {resp.hex()}")
    print()

    # Test 6: SET_DRIVER
    print("=== Test 6: SET_DRIVER (TONHE) ===")
    frame = stm32.build_frame(CMD_SET_DRIVER, bytes([3]))
    print(f"  TX: {frame.hex()}")
    responses = stm32.process_rx_buffer(bytearray(frame))
    for resp in responses:
        print(f"  RX: {resp.hex()}")
    print()

    # Test 7: Emergency Stop
    print("=== Test 7: EMERGENCY_STOP ===")
    frame = stm32.build_frame(CMD_EMERGENCY_STOP)
    print(f"  TX: {frame.hex()}")
    responses = stm32.process_rx_buffer(bytearray(frame))
    for resp in responses:
        print(f"  RX: {resp.hex()}")
    print()

    print("=" * 60)
    print("All tests completed!")
    print("=" * 60)


if __name__ == "__main__":
    test_mock_stm32()
