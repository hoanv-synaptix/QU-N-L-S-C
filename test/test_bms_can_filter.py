#!/usr/bin/env python3
"""
BMS CAN Filter Behavior Test Suite
==================================
Validates STM32F407 bxCAN filter logic for CAN2 (BMS).
Tests 1-5 cover 32-bit baseline, 16-bit 2-filter fix, parser, E2E, IDE-forcing.
"""

import struct
import sys


class BMS_ID:
    BATT_ST1       = 0x02F4
    CELL_VOLT      = 0x04F4
    CELL_TEMP      = 0x05F4
    ALM_INFO       = 0x07F4
    BATT_ST2       = 0x18F128F4
    CHG_REQUEST    = 0x18F0F472
    BMS_SW_STA     = 0x18F528F4
    CELL_TEMP_FULL = 0x18F228F4
    CTRL_INFO      = 0x18F0F428


class bxCANFilter:
    def __init__(self):
        self.mode = "32BIT"
        self.FR1 = 0
        self.FR2 = 0
        self.fi = [0, 0, 0, 0]
        self.fm = [0, 0, 0, 0]

    def config_32bit(self, fi_high, fi_low, fm_high, fm_low):
        self.mode = "32BIT"
        self.FR1 = ((fi_high & 0xFFFF) << 16) | (fi_low & 0xFFFF)
        self.FR2 = ((fm_high & 0xFFFF) << 16) | (fm_low & 0xFFFF)

    def config_16bit(self, f0, m0, f1, m1, f2=0, m2=0, f3=0, m3=0):
        self.mode = "16BIT"
        self.fi = [f0, f1, f2, f3]
        self.fm = [m0, m1, m2, m3]

    @staticmethod
    def pack_frame_32(std_id, ide, ext_id):
        if ide == 0:
            return (std_id & 0x7FF) << 21
        else:
            return (((ext_id >> 18) & 0x7FF) << 21) | (1 << 19) | (ext_id & 0x7FFFF)

    def match_32bit(self, std_id, ide, ext_id):
        fr1 = self.pack_frame_32(std_id, ide, ext_id)
        return ((fr1 ^ self.FR1) & self.FR2) == 0

    @staticmethod
    def match_16bit_std(std_id, fi_val, fm_val):
        """STD frame vs one 16-bit filter. Filter IDE must be 0."""
        if ((fi_val >> 1) & 1) != 0:
            return False
        s_hi = (std_id >> 5) & 0x3F
        s_lo = std_id & 0x1F
        fi_hi = (fi_val >> 10) & 0x3F
        fi_lo = fi_val & 0x1F
        fm_hi = (fm_val >> 10) & 0x3F
        fm_lo = fm_val & 0x1F
        return (((s_hi ^ fi_hi) & fm_hi) == 0) and (((s_lo ^ fi_lo) & fm_lo) == 0)

    @staticmethod
    def match_16bit_ext(ext_id, fi_val, fm_val):
        """EXT frame vs one 16-bit filter. Filter IDE must be 1.
        When mask=0: accept any EXTID (only IDE matters).
        When mask!=0: check EXTID[27:14] (upper 14 bits per RM0090)."""
        if fm_val == 0:
            return True
        ext_hi14 = (ext_id >> 14) & 0x3FFF
        return ((ext_hi14 ^ (fi_val & 0x3FFF)) & (fm_val & 0x3FFF)) == 0

    def match_16bit(self, std_id, ide, ext_id):
        """16-bit mode: bxCAN checks ALL 4 filters. Each filter independently
        accepts/rejects based on its forced IDE bit."""
        for idx in range(4):
            fi_val = self.fi[idx]
            fm_val = self.fm[idx]
            fi_ide = (fi_val >> 1) & 1
            if fi_ide == 0:
                if ide == 1:
                    continue
                if self.match_16bit_std(std_id, fi_val, fm_val):
                    return True
            else:
                if ide == 0:
                    continue
                if self.match_16bit_ext(ext_id, fi_val, fm_val):
                    return True
        return False

    def accept(self, std_id, ext_id, ide):
        if self.mode == "32BIT":
            return self.match_32bit(std_id, ide, ext_id)
        return self.match_16bit(std_id, ide, ext_id)

    def describe(self):
        if self.mode == "32BIT":
            fi_h = (self.FR1 >> 16) & 0xFFFF
            fi_l = self.FR1 & 0xFFFF
            fm_h = (self.FR2 >> 16) & 0xFFFF
            fm_l = self.FR2 & 0xFFFF
            return f"32-bit | FI_H=0x{fi_h:04X} FI_L=0x{fi_l:04X} FM_H=0x{fm_h:04X} FM_L=0x{fm_l:04X}"
        lines = []
        for i in range(4):
            ide = (self.fi[i] >> 1) & 1
            lines.append(f"  Filter {i}: FI=0x{self.fi[i]:04X} FM=0x{self.fm[i]:04X} IDE={ide}")
        return "\n".join(lines)


class BMSParser:
    @staticmethod
    def u16le(data, pos):
        return data[pos] | (data[pos + 1] << 8)

    @staticmethod
    def parse_batt_st1(data):
        return {
            "raw_volt":  BMSParser.u16le(data, 0),
            "raw_curr":  struct.unpack("<h", data[2:4])[0],
            "soc":       data[4],
            "voltage_V": BMSParser.u16le(data, 0) * 0.1,
            "current_A": struct.unpack("<h", data[2:4])[0] * 0.1 - 400.0,
            "valid": True
        }

    @staticmethod
    def parse_cell_volt(data):
        return {
            "max_cell_volt": BMSParser.u16le(data, 0),
            "max_cv_no":     data[2],
            "min_cell_volt": BMSParser.u16le(data, 3),
            "min_cv_no":     data[5],
            "valid": True
        }

    @staticmethod
    def parse_cell_temp(data):
        return {
            "max_cell_temp": data[0] - 50,
            "max_ct_no":     data[1],
            "min_cell_temp": data[2] - 50,
            "min_ct_no":     data[3],
            "avg_cell_temp": data[4] - 50,
            "valid": True
        }

    @staticmethod
    def parse_alm_info(data):
        result = {"valid": True}
        names = ["low_pack_volt", "low_cell_volt", "high_pack_volt", "high_cell_volt",
                 "temp_cell_high_chg", "temp_cell_high_dchg",
                 "temp_cell_low_chg", "temp_cell_low_dchg",
                 "temp_relay_high", "over_chg_curr", "over_dchg_curr",
                 "cell_volt_diff", "low_soc"]
        for i, name in enumerate(names):
            byte_idx = (i * 2) // 8
            bit_offset = (i * 2) % 8
            result[name] = (data[byte_idx] >> bit_offset) & 0x03
        return result

    @staticmethod
    def parse_batt_st2(data):
        return {
            "cap_remain":  BMSParser.u16le(data, 0),
            "rate_cap":    BMSParser.u16le(data, 2),
            "cycle_count": BMSParser.u16le(data, 4),
            "soh":         data[6],
            "valid": True
        }

    @staticmethod
    def parse_chg_request(data):
        return {
            "batt_volt_req": BMSParser.u16le(data, 0),
            "batt_curr_req": BMSParser.u16le(data, 2),
            "valid": True
        }

    @staticmethod
    def parse_bms_sw_sta(data):
        b = data[0]
        return {
            "pre_discharge_sta": bool(b & 0x01),
            "discharge_sta":    bool(b & 0x02),
            "charge_sta":       bool(b & 0x04),
            "valid": True
        }

    @staticmethod
    def parse_frame(std_id, ext_id, data):
        if std_id != 0:
            if std_id == BMS_ID.BATT_ST1:
                return {"name": "BATT_ST1", **BMSParser.parse_batt_st1(data)}
            if std_id == BMS_ID.CELL_VOLT:
                return {"name": "CELL_VOLT", **BMSParser.parse_cell_volt(data)}
            if std_id == BMS_ID.CELL_TEMP:
                return {"name": "CELL_TEMP", **BMSParser.parse_cell_temp(data)}
            if std_id == BMS_ID.ALM_INFO:
                return {"name": "ALM_INFO", **BMSParser.parse_alm_info(data)}
        else:
            if ext_id == BMS_ID.BATT_ST2:
                return {"name": "BATT_ST2", **BMSParser.parse_batt_st2(data)}
            if ext_id == BMS_ID.CHG_REQUEST:
                return {"name": "CHG_REQUEST", **BMSParser.parse_chg_request(data)}
            if ext_id == BMS_ID.BMS_SW_STA:
                return {"name": "BMS_SW_STA", **BMSParser.parse_bms_sw_sta(data)}
            if ext_id == BMS_ID.CELL_TEMP_FULL:
                return {"name": "CELL_TEMP_FULL",
                        "temp_relay": data[0] - 50,
                        "temp_shunt": data[1] - 50,
                        "cell_temps": [data[i] - 50 for i in range(2, 8)],
                        "valid": True}
        return {"name": "UNKNOWN", "valid": False}


def make_frames():
    return [
        ("BATT_ST1",       0x02F4, 0,             0, b"\x27\x10\x00\x00\x64\x00\x00\x00"),
        ("CELL_VOLT",      0x04F4, 0,             0, b"\x0b\xc2\x01\x0b\x80\x02\x00\x00"),
        ("CELL_TEMP",      0x05F4, 0,             0, b"\x35\x01\x32\x02\x33\x00\x00\x00"),
        ("ALM_INFO",       0x07F4, 0,             0, b"\x00\x00\x00\x00\x00\x00\x00\x00"),
        ("BATT_ST2",       0,      0x18F128F4,    1, b"\xe8\x03\x10\x1e\x0a\x00\x64\x00"),
        ("CHG_REQUEST",    0,      0x18F0F472,    1, b"\xa4\x0e\x10\x27\x00\x00\x00\x00"),
        ("BMS_SW_STA",     0,      0x18F528F4,    1, b"\x04\x00\x00\x00\x00\x00\x00\x00"),
        ("CELL_TEMP_FULL", 0,      0x18F228F4,    1, bytes([0x32, 0x33, 0x35, 0x34, 0x36, 0x33, 0x32, 0x00])),
    ]


def test_32bit_filter():
    print()
    print("=" * 70)
    print("TEST 1: Original 32-bit mask=0 filter")
    print("=" * 70)
    print()
    f = bxCANFilter()
    f.config_32bit(0, 0, 0, 0)
    print(f"  {f.describe()}")
    print()
    all_pass = True
    for label, std_id, ext_id, ide, data in make_frames():
        ok = f.accept(std_id, ext_id, ide)
        all_pass = all_pass and ok
        t = "STD" if ide == 0 else "EXT"
        sid = f"0x{std_id:03X}" if ide == 0 else f"0x{ext_id:08X}"
        print(f"  {label:<20} [{t}] {sid} -> {'PASS' if ok else 'FAIL'}")
    print()
    print(f"  RESULT: {'ALL PASS' if all_pass else 'SOME FAILED'}")
    return all_pass


def test_16bit_filter():
    print()
    print("=" * 70)
    print("TEST 2: Fixed 16-bit 2-filter (new BSP_CAN2_Start)")
    print("=" * 70)
    print()
    f = bxCANFilter()
    f.config_16bit(f0=0x0000, m0=0x0000, f1=0x0006, m1=0x0000)
    print(f"  {f.describe()}")
    print()
    all_pass = True
    for label, std_id, ext_id, ide, data in make_frames():
        ok = f.accept(std_id, ext_id, ide)
        all_pass = all_pass and ok
        t = "STD" if ide == 0 else "EXT"
        sid = f"0x{std_id:03X}" if ide == 0 else f"0x{ext_id:08X}"
        print(f"  {label:<20} [{t}] {sid} -> {'PASS' if ok else 'FAIL'}")
    print()
    print(f"  RESULT: {'ALL PASS - Fix is correct!' if all_pass else 'SOME FAILED'}")
    return all_pass


def test_parser():
    print()
    print("=" * 70)
    print("TEST 3: BMS Protocol Parser Verification")
    print("=" * 70)
    print()
    all_ok = True
    for label, std_id, ext_id, ide, data in make_frames():
        result = BMSParser.parse_frame(std_id, ext_id, data)
        ok = result.get("valid", False)
        all_ok = all_ok and ok
        print(f"  {'PASS' if ok else 'FAIL'} {label}")
        if ok:
            for k, v in result.items():
                if k not in ("name", "valid"):
                    print(f"      {k} = {v}")
    print()
    print(f"  RESULT: {'ALL PARSED CORRECTLY' if all_ok else 'SOME PARSE ERRORS'}")
    return all_ok


def test_e2e():
    print()
    print("=" * 70)
    print("TEST 4: End-to-End - Filter + Parse Roundtrip")
    print("=" * 70)
    print()
    f = bxCANFilter()
    f.config_16bit(f0=0x0000, m0=0x0000, f1=0x0006, m1=0x0000)
    all_ok = True
    for label, std_id, ext_id, ide, data in make_frames():
        filt_ok = f.accept(std_id, ext_id, ide)
        if filt_ok:
            parsed = BMSParser.parse_frame(std_id, ext_id, data)
            parse_ok = parsed.get("valid", False)
        else:
            parse_ok = False
        e2e_ok = filt_ok and parse_ok
        all_ok = all_ok and e2e_ok
        print(f"  {label:<20} filter={'PASS' if filt_ok else 'FAIL'} parse={'PASS' if parse_ok else 'FAIL'} -> {'PASS' if e2e_ok else 'FAIL'}")
    print()
    print(f"  RESULT: {'E2E ALL PASS' if all_ok else 'E2E FAILED'}")
    return all_ok


def test_ide_forcing():
    print()
    print("=" * 70)
    print("TEST 5: IDE Forced-Bit Proof (RM0090, 16-bit mode)")
    print("=" * 70)
    print()
    print("Per RM0090, the IDE bit is FORCED in 16-bit mode.")
    print("To isolate one filter, mask the other 3 with 0xFFFF.")
    print()

    all_ok = True

    def check(label, std_id, ext_id, ide, f0, m0, f1, m1, f2, m2, f3, m3, expected):
        nonlocal all_ok
        filt = bxCANFilter()
        filt.config_16bit(f0, m0, f1, m1, f2, m2, f3, m3)
        result = filt.accept(std_id, ext_id, ide)
        ok = result == expected
        all_ok = all_ok and ok
        status = "PASS" if ok else "FAIL"
        print(f"  {status} {label:<30} (expected={'PASS' if expected else 'FAIL'})")
        return ok

    DISABLE = 0xFFFF

    # ---- Section A: Filter 0 only (STD, IDE=0) ----
    print("  --- Filter 0 (IDE=0, accepts STD only) ---")
    check("STD ID=0x000",       0x000, 0,            0, 0x0000, 0, 0,    DISABLE, 0,    DISABLE, 0,    DISABLE, True)
    check("STD ID=0x123",       0x123, 0,            0, 0x0000, 0, 0,    DISABLE, 0,    DISABLE, 0,    DISABLE, True)
    check("STD ID=0x7FF",       0x7FF, 0,            0, 0x0000, 0, 0,    DISABLE, 0,    DISABLE, 0,    DISABLE, True)
    check("EXT ID=0x12345",     0,     0x12345,      1, 0x0000, 0, 0,    DISABLE, 0,    DISABLE, 0,    DISABLE, False)
    check("EXT ID=0x1FFFFFFF",  0,     0x1FFFFFFF,   1, 0x0000, 0, 0,    DISABLE, 0,    DISABLE, 0,    DISABLE, False)

    # ---- Section B: Filter 1 only (EXT, IDE=1) ----
    print()
    print("  --- Filter 1 (IDE=1, accepts EXT only) ---")
    check("EXT ID=0x00000",     0, 0,            1, DISABLE, DISABLE, 0x0006, 0, 0, DISABLE, 0, DISABLE, True)
    check("EXT ID=0x12345",     0, 0x12345,      1, DISABLE, DISABLE, 0x0006, 0, 0, DISABLE, 0, DISABLE, True)
    check("EXT ID=0x1FFFFFFF",  0, 0x1FFFFFFF,   1, DISABLE, DISABLE, 0x0006, 0, 0, DISABLE, 0, DISABLE, True)
    check("STD ID=0x123",       0x123, 0,        0, DISABLE, DISABLE, 0x0006, 0, 0, DISABLE, 0, DISABLE, False)
    check("STD ID=0x7FF",       0x7FF, 0,        0, DISABLE, DISABLE, 0x0006, 0, 0, DISABLE, 0, DISABLE, False)

    # ---- Section C: Combined pair accepts BOTH ----
    print()
    print("  --- Combined pair (filter 0 + 1): accepts BOTH types ---")
    check("STD ID=0x2F4 (BATT_ST1)",      0x2F4, 0,             0, 0x0000, 0, 0x0006, 0, 0, 0, 0, 0, True)
    check("EXT ID=0x18F128F4 (BATT_ST2)", 0,     0x18F128F4,    1, 0x0000, 0, 0x0006, 0, 0, 0, 0, 0, True)
    check("STD ID=0x7F4 (ALM_INFO)",      0x7F4, 0,             0, 0x0000, 0, 0x0006, 0, 0, 0, 0, 0, True)
    check("EXT ID=0x18F0F472 (CHG)",      0,     0x18F0F472,    1, 0x0000, 0, 0x0006, 0, 0, 0, 0, 0, True)

    print()
    print(f"  RESULT: {'IDE-FORCING CONFIRMED' if all_ok else 'UNEXPECTED BEHAVIOR'}")
    return all_ok


def test_strict_filter():
    print()
    print("=" * 70)
    print("TEST 6: Strict Hardware Filter (NEW bsp_can.c)")
    print("=" * 70)
    print()
    print("Simulating Banks 14, 15, 16, 17 as defined in the new bsp_can.c")

    all_ok = True

    # Helper macro logic ported to Python
    def ext_id_high(ext_id): return (ext_id >> 13) & 0xFFFF
    def ext_id_low(ext_id):  return (((ext_id << 3) | 4) & 0xFFFF)

    # Bank 14: 16-bit List for 0x02F4, 0x04F4, 0x05F4, 0x07F4
    bank14 = bxCANFilter()
    bank14.config_16bit(0x02F4 << 5, 0x04F4 << 5, 0x05F4 << 5, 0x07F4 << 5)

    # Bank 15: 32-bit List for 0x18F128F4, 0x18F0F472
    bank15 = bxCANFilter()
    bank15.config_32bit(ext_id_high(0x18F128F4), ext_id_low(0x18F128F4),
                        ext_id_high(0x18F0F472), ext_id_low(0x18F0F472))

    # Bank 16: 32-bit List for 0x18F528F4, 0x18F228F4
    bank16 = bxCANFilter()
    bank16.config_32bit(ext_id_high(0x18F528F4), ext_id_low(0x18F528F4),
                        ext_id_high(0x18F228F4), ext_id_low(0x18F228F4))

    # Bank 17: 32-bit Mask for 0x18E000F4 (masking bits 16-18)
    bank17 = bxCANFilter()
    bank17.config_32bit(ext_id_high(0x18E000F4), ext_id_low(0x18E000F4),
                        ext_id_high(0xFFF8FFFF), (((0xFFF8FFFF << 3) | 6) & 0xFFFF))

    filters = [bank14, bank15, bank16, bank17]

    for label, std_id, ext_id, ide, data in make_frames():
        # A frame is accepted if ANY of the filters accept it
        accepted = any(f.accept(std_id, ext_id, ide) for f in filters)
        
        all_ok = all_ok and accepted
        t = "STD" if ide == 0 else "EXT"
        sid = f"0x{std_id:03X}" if ide == 0 else f"0x{ext_id:08X}"
        print(f"  {label:<20} [{t}] {sid} -> {'PASS (Accepted)' if accepted else 'FAIL (Rejected)'}")

    # Test an invalid ID to ensure it is REJECTED
    print("\n  --- Testing rejection of garbage IDs ---")
    garbage_std = 0x123
    garbage_ext = 0x18EEEEEE
    
    rej_std = not any(f.accept(garbage_std, 0, 0) for f in filters)
    rej_ext = not any(f.accept(0, garbage_ext, 1) for f in filters)
    
    print(f"  Garbage STD 0x123      -> {'PASS (Rejected)' if rej_std else 'FAIL (Accepted)'}")
    print(f"  Garbage EXT 0x18EEEEEE -> {'PASS (Rejected)' if rej_ext else 'FAIL (Accepted)'}")
    
    all_ok = all_ok and rej_std and rej_ext

    print()
    print(f"  RESULT: {'ALL STRICT FILTERS PASS' if all_ok else 'STRICT FILTERS FAILED'}")
    return all_ok


def main():
    print()
    print("=" * 70)
    print("  BMS CAN Filter Behavior Test Suite")
    print("=" * 70)

    t1 = test_32bit_filter()
    t2 = test_16bit_filter()
    t3 = test_parser()
    t4 = test_e2e()
    t5 = test_ide_forcing()
    t6 = test_strict_filter()

    print()
    print("=" * 70)
    print("FINAL SUMMARY")
    print("=" * 70)
    print(f"  Test 1 - 32-bit mask=0 accepts all:    {'PASS' if t1 else 'FAIL'}")
    print(f"  Test 2 - 16-bit 2-filter accepts all:  {'PASS' if t2 else 'FAIL'}")
    print(f"  Test 3 - BMS parser correctness:       {'PASS' if t3 else 'FAIL'}")
    print(f"  Test 4 - E2E filter+parse roundtrip:   {'PASS' if t4 else 'FAIL'}")
    print(f"  Test 5 - IDE-forcing proof:            {'PASS' if t5 else 'FAIL'}")
    print(f"  Test 6 - Strict Hardware Filters:      {'PASS' if t6 else 'FAIL'}")
    print()

    ok = t1 and t2 and t3 and t4 and t5 and t6
    if ok:
        print("  CONCLUSION: Fix validated by simulation.")
        print("  Build and flash to hardware to confirm.")
    else:
        print("  CONCLUSION: Fix needs review.")

    print()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
