"""
Charger Configuration Models
对应 App gốc: ChargerConfig_Model, BattChargerConfig_Model, CellChargerConfig_Model, etc.
"""

from dataclasses import dataclass
from typing import Optional
import struct


# ============== Enums ==============

class DriverType:
    NONE = 0
    MAXWELL = 1
    LIANMING = 2
    TONHE = 3

    NAMES = {
        NONE: "None",
        MAXWELL: "Maxwell MXR",
        LIANMING: "LIANMING",
        TONHE: "TONHE V1.3",
    }


# ============== Config Data Classes ==============

@dataclass
class BattConfig:
    """Cấu hình ắc quy chính - Section 0x01 (20 bytes)"""
    capacity: int = 500       # Ah
    i_min: int = 5           # A
    i_max: int = 50          # A
    i_pre: int = 10          # A
    i_low: int = 5            # A
    v_min: int = 420         # V (×10, lưu 420 = 42.0V)
    v_max: int = 588         # V (×10)
    v_pre: int = 300         # V (×10)
    v_low: int = 420         # V (×10)
    temp_limit: int = 45      # °C

    FORMAT = "<HHHHHHHHHbB"  # 20 bytes
    SIZE = struct.calcsize(FORMAT)

    def to_bytes(self) -> bytes:
        return struct.pack(
            self.FORMAT,
            self.capacity, self.i_min, self.i_max, self.i_pre, self.i_low,
            self.v_min, self.v_max, self.v_pre, self.v_low,
            self.temp_limit, 0  # reserved
        )

    @classmethod
    def from_bytes(cls, data: bytes) -> "BattConfig":
        if len(data) < cls.SIZE:
            raise ValueError(f"BattConfig: expected {cls.SIZE} bytes, got {len(data)}")
        unpacked = struct.unpack(cls.FORMAT, data[:cls.SIZE])
        return cls(
            capacity=unpacked[0],
            i_min=unpacked[1],
            i_max=unpacked[2],
            i_pre=unpacked[3],
            i_low=unpacked[4],
            v_min=unpacked[5],
            v_max=unpacked[6],
            v_pre=unpacked[7],
            v_low=unpacked[8],
            temp_limit=unpacked[9],
        )


@dataclass
class CellConfig:
    """Cấu hình sạc theo điện áp cell - Section 0x02 (15 bytes)"""
    is_enable: bool = False
    v1: int = 3200           # mV
    v2: int = 3400           # mV
    v3: int = 3500           # mV
    i1: int = 100            # A
    i2: int = 50             # A
    i3: int = 10             # A
    delta_volt: int = 50     # mV

    FORMAT = "<BHHHHHHH"  # 15 bytes
    SIZE = struct.calcsize(FORMAT)

    def to_bytes(self) -> bytes:
        return struct.pack(
            self.FORMAT,
            1 if self.is_enable else 0,
            self.v1, self.v2, self.v3,
            self.i1, self.i2, self.i3,
            self.delta_volt
        )

    @classmethod
    def from_bytes(cls, data: bytes) -> "CellConfig":
        if len(data) < cls.SIZE:
            raise ValueError(f"CellConfig: expected {cls.SIZE} bytes, got {len(data)}")
        unpacked = struct.unpack(cls.FORMAT, data[:cls.SIZE])
        return cls(
            is_enable=bool(unpacked[0]),
            v1=unpacked[1], v2=unpacked[2], v3=unpacked[3],
            i1=unpacked[4], i2=unpacked[5], i3=unpacked[6],
            delta_volt=unpacked[7],
        )


@dataclass
class SocConfig:
    """Cấu hình sạc theo SOC - Section 0x03 (17 bytes)"""
    is_enable: bool = False
    soc1: int = 80          # %
    soc2: int = 90          # %
    soc3: int = 95          # %
    i1: int = 100           # A
    i2: int = 50            # A
    i3: int = 10            # A
    delta_soc: int = 2      # %
    soc_charge3: int = 95   # %
    i_charge3: int = 10     # A

    FORMAT = "<BBBBHHBBBBHBBB"  # 17 bytes
    SIZE = struct.calcsize(FORMAT)

    def to_bytes(self) -> bytes:
        return struct.pack(
            self.FORMAT,
            1 if self.is_enable else 0,
            self.soc1, self.soc2, self.soc3,
            self.i1, self.i2, self.i3,
            self.delta_soc, self.soc_charge3, self.i_charge3,
            0, 0, 0  # reserved
        )

    @classmethod
    def from_bytes(cls, data: bytes) -> "SocConfig":
        if len(data) < cls.SIZE:
            raise ValueError(f"SocConfig: expected {cls.SIZE} bytes, got {len(data)}")
        unpacked = struct.unpack(cls.FORMAT, data[:cls.SIZE])
        return cls(
            is_enable=bool(unpacked[0]),
            soc1=unpacked[1], soc2=unpacked[2], soc3=unpacked[3],
            i1=unpacked[4], i2=unpacked[5], i3=unpacked[6],
            delta_soc=unpacked[7],
            soc_charge3=unpacked[8], i_charge3=unpacked[9],
        )


@dataclass
class TempConfig:
    """Cấu hình sạc theo nhiệt độ - Section 0x04 (20 bytes)"""
    is_enable: bool = False
    temp1: int = 0          # °C
    temp2: int = 10         # °C
    temp3: int = 35         # °C
    temp4: int = 40         # °C
    temp5: int = 45         # °C
    i1: int = 100           # A
    i2: int = 80            # A
    i3: int = 50            # A
    i4: int = 20            # A
    delta_temp: int = 5     # °C

    FORMAT = "<BbbbbbBBBBbBBBBBBBB"  # 20 bytes
    SIZE = struct.calcsize(FORMAT)

    def to_bytes(self) -> bytes:
        return struct.pack(
            self.FORMAT,
            1 if self.is_enable else 0,
            self.temp1, self.temp2, self.temp3, self.temp4, self.temp5,
            self.i1, self.i2, self.i3, self.i4,
            self.delta_temp,
            0, 0, 0, 0, 0, 0, 0, 0  # reserved
        )

    @classmethod
    def from_bytes(cls, data: bytes) -> "TempConfig":
        if len(data) < cls.SIZE:
            raise ValueError(f"TempConfig: expected {cls.SIZE} bytes, got {len(data)}")
        unpacked = struct.unpack(cls.FORMAT, data[:cls.SIZE])
        return cls(
            is_enable=bool(unpacked[0]),
            temp1=unpacked[1], temp2=unpacked[2], temp3=unpacked[3],
            temp4=unpacked[4], temp5=unpacked[5],
            i1=unpacked[6], i2=unpacked[7], i3=unpacked[8], i4=unpacked[9],
            delta_temp=unpacked[10],
        )


@dataclass
class ProtectConfig:
    """Cấu hình bảo vệ - Section 0x05 (7 bytes)"""
    is_cell_protect: bool = True
    is_jack_protect: bool = True
    delay_cell: int = 30    # seconds
    delay_jack: int = 10    # seconds
    delta_cell_volt: int = 100  # mV

    FORMAT = "<BBHHH"  # 7 bytes
    SIZE = struct.calcsize(FORMAT)

    def to_bytes(self) -> bytes:
        return struct.pack(
            self.FORMAT,
            1 if self.is_cell_protect else 0,
            1 if self.is_jack_protect else 0,
            self.delay_cell, self.delay_jack,
            self.delta_cell_volt
        )

    @classmethod
    def from_bytes(cls, data: bytes) -> "ProtectConfig":
        if len(data) < cls.SIZE:
            raise ValueError(f"ProtectConfig: expected {cls.SIZE} bytes, got {len(data)}")
        unpacked = struct.unpack(cls.FORMAT, data[:cls.SIZE])
        return cls(
            is_cell_protect=bool(unpacked[0]),
            is_jack_protect=bool(unpacked[1]),
            delay_cell=unpacked[2],
            delay_jack=unpacked[3],
            delta_cell_volt=unpacked[4],
        )


@dataclass
class ModuleConfig:
    """Cấu hình module sạc - Section 0x06 (8 bytes)"""
    driver_id: int = DriverType.MAXWELL
    module_count: int = 1
    base_addr: int = 0
    base_group: int = 0
    v_float: int = 546  # V (×10, lưu 546 = 54.6V)
    i_float: int = 50   # A

    FORMAT = "<BBBBHH"  # 8 bytes
    SIZE = struct.calcsize(FORMAT)

    def to_bytes(self) -> bytes:
        return struct.pack(
            self.FORMAT,
            self.driver_id, self.module_count, self.base_addr, self.base_group,
            self.v_float, self.i_float
        )

    @classmethod
    def from_bytes(cls, data: bytes) -> "ModuleConfig":
        if len(data) < cls.SIZE:
            raise ValueError(f"ModuleConfig: expected {cls.SIZE} bytes, got {len(data)}")
        unpacked = struct.unpack(cls.FORMAT, data[:cls.SIZE])
        return cls(
            driver_id=unpacked[0],
            module_count=unpacked[1],
            base_addr=unpacked[2],
            base_group=unpacked[3],
            v_float=unpacked[4],
            i_float=unpacked[5],
        )


@dataclass
class CanConfig:
    """Cấu hình CAN - Section 0x07 (8 bytes)"""
    can1_baudrate: int = 125000  # bps
    can2_baudrate: int = 250000  # bps

    FORMAT = "<II"  # 8 bytes
    SIZE = struct.calcsize(FORMAT)

    def to_bytes(self) -> bytes:
        return struct.pack(self.FORMAT, self.can1_baudrate, self.can2_baudrate)

    @classmethod
    def from_bytes(cls, data: bytes) -> "CanConfig":
        if len(data) < cls.SIZE:
            raise ValueError(f"CanConfig: expected {cls.SIZE} bytes, got {len(data)}")
        unpacked = struct.unpack(cls.FORMAT, data[:cls.SIZE])
        return cls(can1_baudrate=unpacked[0], can2_baudrate=unpacked[1])


# ============== Full Config ==============

class ChargerConfig:
    """Toàn bộ cấu hình bộ sạc"""

    def __init__(self):
        self.battery = BattConfig()
        self.cell = CellConfig()
        self.soc = SocConfig()
        self.temp = TempConfig()
        self.protect = ProtectConfig()
        self.module = ModuleConfig()
        self.can = CanConfig()

    def to_bytes(self) -> bytes:
        """Đóng gói toàn bộ config thành 1 byte stream"""
        return (
            self.battery.to_bytes() +
            self.cell.to_bytes() +
            self.soc.to_bytes() +
            self.temp.to_bytes() +
            self.protect.to_bytes() +
            self.module.to_bytes() +
            self.can.to_bytes()
        )

    @classmethod
    def from_bytes(cls, data: bytes) -> "ChargerConfig":
        """Giải mã từ byte stream"""
        cfg = cls()
        offset = 0

        cfg.battery = BattConfig.from_bytes(data[offset:offset + BattConfig.SIZE])
        offset += BattConfig.SIZE

        cfg.cell = CellConfig.from_bytes(data[offset:offset + CellConfig.SIZE])
        offset += CellConfig.SIZE

        cfg.soc = SocConfig.from_bytes(data[offset:offset + SocConfig.SIZE])
        offset += SocConfig.SIZE

        cfg.temp = TempConfig.from_bytes(data[offset:offset + TempConfig.SIZE])
        offset += TempConfig.SIZE

        cfg.protect = ProtectConfig.from_bytes(data[offset:offset + ProtectConfig.SIZE])
        offset += ProtectConfig.SIZE

        cfg.module = ModuleConfig.from_bytes(data[offset:offset + ModuleConfig.SIZE])
        offset += ModuleConfig.SIZE

        cfg.can = CanConfig.from_bytes(data[offset:offset + CanConfig.SIZE])

        return cfg


# ============== Unit Conversion Helpers ==============

def voltage_to_display(raw: int) -> float:
    """Chuyển giá trị lưu trữ → hiển thị (V)"""
    return raw / 10.0


def voltage_to_raw(value: float) -> int:
    """Chuyển giá trị nhập → lưu trữ (×10)"""
    return int(round(value * 10))


def current_to_display(raw: int) -> float:
    """Chuyển giá trị lưu trữ → hiển thị (A)"""
    return raw / 10.0


def current_to_raw(value: float) -> int:
    """Chuyển giá trị nhập → lưu trữ (×10)"""
    return int(round(value * 10))


def capacity_to_display(raw: int) -> float:
    """Chuyển giá trị lưu trữ → hiển thị (Ah)"""
    return raw / 10.0


def capacity_to_raw(value: float) -> int:
    """Chuyển giá trị nhập → lưu trữ (×10)"""
    return int(round(value * 10))
