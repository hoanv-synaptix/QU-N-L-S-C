"""
Configuration models for PC ↔ MCU communication
Defines all configuration structures matching the firmware
"""

import struct
from dataclasses import dataclass, field
from typing import Optional
from enum import IntEnum


# ============== Section IDs ==============
class CfgSection(IntEnum):
    SYSTEM = 0x01
    CHARGER = 0x02
    MODULE = 0x03
    PROTECT = 0x04
    BMS = 0x05
    DISPLAY = 0x06
    HISTORY = 0x07
    ALL = 0xFF


# ============== Driver IDs ==============
class DriverId(IntEnum):
    NONE = 0x00
    MAXWELL = 0x01
    LIANMING = 0x02
    TONHE = 0x03


DRIVER_NAMES = {
    DriverId.NONE: "None",
    DriverId.MAXWELL: "Maxwell MXR",
    DriverId.LIANMING: "Lianming",
    DriverId.TONHE: "TonHe V1.3",
}


# ============== SECTION: SYSTEM ==============
@dataclass
class CfgSystem:
    driver_id: int = DriverId.MAXWELL
    module_count: int = 1
    reserved1: int = 0
    reserved2: int = 0
    fw_version: int = 0x00020000
    hw_version: int = 0x0100
    language: int = 0  # 0=EN, 1=VN, 2=CN
    debug_mode: int = 0
    serial_number: int = 0

    FORMAT = "<BBBIBHBBBI"
    SIZE = 20

    def to_bytes(self) -> bytes:
        data = struct.pack(self.FORMAT,
            self.driver_id, self.module_count, self.reserved1, self.reserved2,
            self.fw_version, self.hw_version, self.language, self.debug_mode,
            self.serial_number, 0  # crc placeholder
        )
        return data

    @classmethod
    def from_bytes(cls, data: bytes) -> 'CfgSystem':
        if len(data) < cls.SIZE:
            raise ValueError(f"Expected {cls.SIZE} bytes, got {len(data)}")
        vals = struct.unpack(cls.FORMAT, data[:cls.SIZE])
        return cls(
            driver_id=vals[0],
            module_count=vals[1],
            reserved1=vals[2],
            reserved2=vals[3],
            fw_version=vals[4],
            hw_version=vals[5],
            language=vals[6],
            debug_mode=vals[7],
            serial_number=vals[8]
        )

    @property
    def fw_version_str(self) -> str:
        return f"v{(self.fw_version >> 16) & 0xFF}.{(self.fw_version >> 8) & 0xFF}.{self.fw_version & 0xFF}"

    @property
    def driver_name(self) -> str:
        return DRIVER_NAMES.get(self.driver_id, "Unknown")


# ============== SECTION: CHARGER ==============
@dataclass
class CfgCharger:
    target_voltage: float = 57.6
    target_current: float = 20.0
    max_voltage: float = 60.0
    max_current: float = 25.0
    charge_mode: int = 0  # 0=CCCV, 1=Float, 2=Pulse
    float_voltage: int = 95  # percentage
    charge_timeout: int = 480  # minutes
    term_by_soc: int = 1
    term_soc_full: int = 100
    term_by_voltage: int = 1
    term_voltage_delta: int = 10
    auto_restart: int = 0
    reserved: bytes = field(default_factory=lambda: bytes(3))

    FORMAT = "<ffffBBBBHBBBBBBB"
    SIZE = 34

    def to_bytes(self) -> bytes:
        return struct.pack(self.FORMAT,
            self.target_voltage, self.target_current,
            self.max_voltage, self.max_current,
            self.charge_mode, self.float_voltage, self.charge_timeout,
            self.term_by_soc, self.term_soc_full, self.term_by_voltage,
            self.term_voltage_delta, self.auto_restart, *self.reserved, 0
        )

    @classmethod
    def from_bytes(cls, data: bytes) -> 'CfgCharger':
        vals = struct.unpack(cls.FORMAT, data[:cls.SIZE])
        return cls(
            target_voltage=vals[0], target_current=vals[1],
            max_voltage=vals[2], max_current=vals[3],
            charge_mode=vals[4], float_voltage=vals[5], charge_timeout=vals[6],
            term_by_soc=vals[7], term_soc_full=vals[8], term_by_voltage=vals[9],
            term_voltage_delta=vals[10], auto_restart=vals[11],
            reserved=bytes(vals[12:15])
        )

    @property
    def charge_mode_name(self) -> str:
        modes = {0: "CCCV", 1: "Float", 2: "Pulse"}
        return modes.get(self.charge_mode, "Unknown")


# ============== SECTION: MODULE ==============
@dataclass
class CfgModule:
    base_address: int = 0x30
    group_id: int = 0
    rated_voltage: float = 48.0
    rated_current: float = 10.0
    parallel_count: int = 1
    comm_timeout: int = 2000  # ms
    retry_count: int = 3
    poll_interval: int = 500  # ms
    reserved: bytes = field(default_factory=lambda: bytes(6))

    FORMAT = "<BBffHHBB"
    SIZE = 26

    def to_bytes(self) -> bytes:
        return struct.pack(self.FORMAT,
            self.base_address, self.group_id,
            self.rated_voltage, self.rated_current,
            self.parallel_count, self.comm_timeout,
            self.retry_count, self.poll_interval
        ) + self.reserved + bytes([0, 0])  # padding + crc

    @classmethod
    def from_bytes(cls, data: bytes) -> 'CfgModule':
        vals = struct.unpack(cls.FORMAT, data[:20])
        return cls(
            base_address=vals[0], group_id=vals[1],
            rated_voltage=vals[2], rated_current=vals[3],
            parallel_count=vals[4], comm_timeout=vals[5],
            retry_count=vals[6], poll_interval=vals[7]
        )


# ============== SECTION: PROTECTION ==============
@dataclass
class CfgProtect:
    over_voltage: float = 60.0
    under_voltage: float = 40.0
    voltage_delta: float = 2.0
    over_current: float = 30.0
    current_delta: float = 5.0
    over_temp_dcdc: float = 75.0
    over_temp_ambient: float = 50.0
    under_temp: float = -10.0
    under_voltage_input: float = 180.0
    over_voltage_input: float = 265.0
    delay_over_temp: int = 60  # seconds
    delay_over_current: int = 1000  # ms
    reserved: bytes = field(default_factory=lambda: bytes(4))

    FORMAT = "<ffffffffHH"
    SIZE = 34

    def to_bytes(self) -> bytes:
        return struct.pack(self.FORMAT,
            self.over_voltage, self.under_voltage, self.voltage_delta,
            self.over_current, self.current_delta,
            self.over_temp_dcdc, self.over_temp_ambient, self.under_temp,
            self.under_voltage_input, self.over_voltage_input,
            self.delay_over_temp, self.delay_over_current
        ) + self.reserved + bytes([0, 0])

    @classmethod
    def from_bytes(cls, data: bytes) -> 'CfgProtect':
        vals = struct.unpack(cls.FORMAT, data[:cls.SIZE])
        return cls(
            over_voltage=vals[0], under_voltage=vals[1], voltage_delta=vals[2],
            over_current=vals[3], current_delta=vals[4],
            over_temp_dcdc=vals[5], over_temp_ambient=vals[6], under_temp=vals[7],
            under_voltage_input=vals[8], over_voltage_input=vals[9],
            delay_over_temp=vals[10], delay_over_current=vals[11]
        )


# ============== SECTION: BMS ==============
@dataclass
class CfgBms:
    bms_enabled: int = 1
    bms_can_channel: int = 1  # CAN2
    bms_can_id: int = 0xF4
    bms_max_voltage: float = 58.8
    bms_max_current: float = 25.0
    bms_min_voltage: float = 42.0
    soc_full: int = 100
    soc_empty: int = 10
    temp_max_charge: float = 45.0
    temp_min_charge: float = 0.0
    bms_timeout: int = 5000  # ms
    bms_resp_timeout: int = 1000  # ms
    reserved: bytes = field(default_factory=lambda: bytes(2))

    FORMAT = "<BBIfiffBBffHH"
    SIZE = 26

    def to_bytes(self) -> bytes:
        return struct.pack(self.FORMAT,
            self.bms_enabled, self.bms_can_channel, self.bms_can_id,
            self.bms_max_voltage, self.bms_max_current, self.bms_min_voltage,
            self.soc_full, self.soc_empty,
            self.temp_max_charge, self.temp_min_charge,
            self.bms_timeout, self.bms_resp_timeout
        ) + self.reserved + bytes([0, 0])

    @classmethod
    def from_bytes(cls, data: bytes) -> 'CfgBms':
        vals = struct.unpack(cls.FORMAT, data[:cls.SIZE])
        return cls(
            bms_enabled=vals[0], bms_can_channel=vals[1], bms_can_id=vals[2],
            bms_max_voltage=vals[3], bms_max_current=vals[4], bms_min_voltage=vals[5],
            soc_full=vals[6], soc_empty=vals[7],
            temp_max_charge=vals[8], temp_min_charge=vals[9],
            bms_timeout=vals[10], bms_resp_timeout=vals[11]
        )


# ============== SECTION: DISPLAY ==============
@dataclass
class CfgDisplay:
    status_interval: int = 500  # ms
    graph_interval: int = 1000  # ms
    show_graph: int = 1
    show_details: int = 1
    auto_scroll: int = 1
    beep_enabled: int = 1
    brightness: int = 80
    contrast: int = 50
    idle_timeout: int = 10  # minutes
    reserved: bytes = field(default_factory=lambda: bytes(3))

    FORMAT = "<HHBBBBB"
    SIZE = 14

    def to_bytes(self) -> bytes:
        return struct.pack(self.FORMAT,
            self.status_interval, self.graph_interval,
            self.show_graph, self.show_details, self.auto_scroll,
            self.beep_enabled, self.brightness, self.contrast
        ) + bytes([self.idle_timeout >> 8, self.idle_timeout & 0xFF]) + self.reserved + bytes([0, 0])

    @classmethod
    def from_bytes(cls, data: bytes) -> 'CfgDisplay':
        vals = struct.unpack(cls.FORMAT, data[:cls.SIZE])
        return cls(
            status_interval=vals[0], graph_interval=vals[1],
            show_graph=vals[2], show_details=vals[3], auto_scroll=vals[4],
            beep_enabled=vals[5], brightness=vals[6], contrast=vals[7]
        )


# ============== RUNTIME STATUS ==============
class SystemState(IntEnum):
    IDLE = 0
    CHARGING = 1
    FULL = 2
    FAULT = 3


@dataclass
class RuntimeStatus:
    system_state: int = 0
    driver_id: int = 0
    modules_total: int = 0
    modules_online: int = 0
    output_voltage: float = 0.0
    output_current: float = 0.0
    output_power: float = 0.0
    batt_voltage: float = 0.0
    batt_current: float = 0.0
    batt_soc: int = 0
    temp_dcdc: float = 0.0
    temp_ambient: float = 0.0
    temp_battery: float = 0.0
    input_voltage: float = 0.0
    input_power: float = 0.0
    alarm_flags: int = 0
    module_alarms: int = 0
    charge_time_sec: int = 0
    charged_wh: int = 0
    charged_ah: int = 0
    cycle_count: int = 0
    error_count: int = 0

    FORMAT = "<BBB Bffff Bffff fII IBBB fII BBHH"
    SIZE = 48

    @classmethod
    def from_bytes(cls, data: bytes) -> 'RuntimeStatus':
        if len(data) < cls.SIZE:
            # Pad with zeros if needed
            data = data + bytes(cls.SIZE - len(data))
        
        # Unpack carefully - some fields may be misaligned
        vals = struct.unpack("<BBB", data[0:3])
        system_state = vals[0]
        driver_id = vals[1]
        modules_total = vals[2]

        vals2 = struct.unpack("<B", data[3:4])
        modules_online = vals2[0]

        vals3 = struct.unpack("<ffff", data[4:20])
        output_voltage = vals3[0]
        output_current = vals3[1]
        output_power = vals3[2]
        # skip 1 byte

        return cls(
            system_state=system_state,
            driver_id=driver_id,
            modules_total=modules_total,
            modules_online=modules_online,
            output_voltage=output_voltage,
            output_current=output_current,
            output_power=output_power,
        )

    @property
    def state_name(self) -> str:
        return SystemState(self.system_state).name if self.system_state < 4 else "UNKNOWN"

    @property
    def driver_name(self) -> str:
        return DRIVER_NAMES.get(self.driver_id, "Unknown")


# ============== HISTORY RECORD ==============
@dataclass
class HistoryRecord:
    timestamp: int = 0
    year: int = 0
    month: int = 0
    day: int = 0
    hour: int = 0
    minute: int = 0
    second: int = 0
    start_voltage: float = 0.0
    end_voltage: float = 0.0
    charged_energy: float = 0.0
    charged_capacity: float = 0.0
    duration_minutes: int = 0
    termination_type: int = 0
    error_code: int = 0
    module_count: int = 0

    @property
    def date_str(self) -> str:
        return f"{self.year:04d}-{self.month:02d}-{self.day:02d} {self.hour:02d}:{self.minute:02d}"

    @property
    def term_name(self) -> str:
        names = {0: "Manual", 1: "Full", 2: "Timeout", 3: "Error"}
        return names.get(self.termination_type, "Unknown")


# ============== Combined Config ==============
@dataclass
class FullConfig:
    system: CfgSystem = field(default_factory=CfgSystem)
    charger: CfgCharger = field(default_factory=CfgCharger)
    module: CfgModule = field(default_factory=CfgModule)
    protect: CfgProtect = field(default_factory=CfgProtect)
    bms: CfgBms = field(default_factory=CfgBms)
    display: CfgDisplay = field(default_factory=CfgDisplay)

    def to_bytes(self) -> bytes:
        return (self.system.to_bytes() + self.charger.to_bytes() + 
                self.module.to_bytes() + self.protect.to_bytes() + 
                self.bms.to_bytes() + self.display.to_bytes())

    @classmethod
    def from_bytes(cls, data: bytes) -> 'FullConfig':
        # Parse each section
        offset = 0
        system = CfgSystem.from_bytes(data[offset:offset + CfgSystem.SIZE])
        offset += CfgSystem.SIZE
        
        charger = CfgCharger.from_bytes(data[offset:offset + CfgCharger.SIZE])
        offset += CfgCharger.SIZE
        
        module = CfgModule.from_bytes(data[offset:offset + CfgModule.SIZE])
        offset += CfgModule.SIZE
        
        protect = CfgProtect.from_bytes(data[offset:offset + CfgProtect.SIZE])
        offset += CfgProtect.SIZE
        
        bms = CfgBms.from_bytes(data[offset:offset + CfgBms.SIZE])
        offset += CfgBms.SIZE
        
        display = CfgDisplay.from_bytes(data[offset:offset + CfgDisplay.SIZE])
        
        return cls(system=system, charger=charger, module=module,
                   protect=protect, bms=bms, display=display)
