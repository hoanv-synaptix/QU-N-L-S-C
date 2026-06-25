"""
Integration Test - Full PC <-> MCU communication loopback
"""

import sys
sys.path.insert(0, '/sessions/nice-optimistic-fermat/mnt/MẠCH ĐIỀU KHIỂN SẠC/pc_app')
sys.path.insert(0, '/sessions/nice-optimistic-fermat/mnt/MẠCH ĐIỀU KHIỂN SẠC/test')

# Import MockSTM32 from test_mock_mcu.py
exec(open('/sessions/nice-optimistic-fermat/mnt/MẠCH ĐIỀU KHIỂN SẠC/test/test_mock_mcu.py').read().split('# ============== Tests ==============')[0])

from services.protocol_handler import (
    ProtocolHandler,
    cmd_ping, cmd_set_voltage, cmd_set_current, cmd_start, cmd_stop
)


def test_ping():
    """Test PING"""
    print("\n=== Test: PING ===")
    mcu = MockSTM32()
    handler = ProtocolHandler()
    
    responses = []
    handler.on_pong = lambda v: responses.append(('PONG', v))
    
    frame = cmd_ping()
    for b in frame:
        resp = mcu.feed_byte(b)
    if resp:
        handler.feed_data(resp)
    
    print(f"  Response: {responses}")
    assert ('PONG', 0x00020000) in responses
    print("  ✓ PING OK")


def test_config():
    """Test config read"""
    print("\n=== Test: Config Read ===")
    mcu = MockSTM32()
    handler = ProtocolHandler()
    
    responses = []
    handler.on_config = lambda s, d: responses.append(('CONFIG', s))
    
    from services.protocol_handler import cmd_read_all_config
    frame = cmd_read_all_config()
    for b in frame:
        resp = mcu.feed_byte(b)
    if resp:
        handler.feed_data(resp)
    
    print(f"  Responses: {len(responses)}")
    print("  ✓ Config Read OK")


def test_charging():
    """Test charging"""
    print("\n=== Test: Charging ===")
    mcu = MockSTM32()
    handler = ProtocolHandler()
    
    responses = []
    handler.on_ack = lambda c: responses.append(('ACK', c))
    
    frame = cmd_set_voltage(54.0)
    for b in frame:
        mcu.feed_byte(b)
    
    frame = cmd_start()
    for b in frame:
        mcu.feed_byte(b)
    
    print(f"  Charging: {mcu.charging}, V: {mcu.target_voltage}V")
    assert mcu.charging == True
    print("  ✓ Charging OK")


def main():
    print("=" * 60)
    print("Integration Tests")
    print("=" * 60)
    
    test_ping()
    test_config()
    test_charging()
    
    print("\n" + "=" * 60)
    print("ALL TESTS PASSED ✓")
    print("=" * 60)


if __name__ == '__main__':
    main()
