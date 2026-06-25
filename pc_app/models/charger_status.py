"""
Charger Status Models - Trạng thái realtime từ device
对应 App gốc: BatteryChargerStatus_Model, ChargerStatusUc fields
"""

from dataclasses import dataclass, field
from typing import List
import struct


# ============== Enums ==============

class ChargeStatus:
    """Các pha sạc - EChargerBattMode"""
    STOP = 0
    NO_BATTERY = 1
    HAS_BATTERY = 2
    CHECK_BATTERY = 3
    CHARGING_CC = 4
    BATTERY_FULL = 5
    WAIT_OVER_TEMP = 6
    ERROR_BOARD = 7
    ERROR_CELL = 8
    ERROR_JACK = 9
    PRE_CHARGE = 10
    CHARGER_PARAM_ERROR = 11
    BMS_PROHIBIT = 12
    CHARGING_CV = 13
    CHARGER_OVER_OUTPUT_VOLTAGE = 14
    BATT_LOST_CAPACITY = 15

    NAMES = {
        STOP: "Stop",
        NO_BATTERY: "No Battery",
        HAS_BATTERY: "Has Battery",
        CHECK_BATTERY: "Checking Battery",
        CHARGING_CC: "Charging CC",
        BATTERY_FULL: "Battery Full",
        WAIT_OVER_TEMP: "Wait Over Temp",
        ERROR_BOARD: "Error Board",
        ERROR_CELL: "Error Cell",
        ERROR_JACK: "Error Jack",
        PRE_CHARGE: "Pre-charge",
        CHARGER_PARAM_ERROR: "Param Error",
        BMS_PROHIBIT: "BMS Prohibit",
        CHARGING_CV: "Charging CV",
        CHARGER_OVER_OUTPUT_VOLTAGE: "Over Voltage",
        BATT_LOST_CAPACITY: "Lost Capacity",
    }

    COLORS = {
        STOP: "#9E9E9E",           # Gray
        NO_BATTERY: "#FF9800",     # Orange
        HAS_BATTERY: "#9E9E9E",    # Gray
        CHECK_BATTERY: "#FFC107",  # Yellow
        CHARGING_CC: "#2196F3",    # Blue
        BATTERY_FULL: "#4CAF50",    # Green
        WAIT_OVER_TEMP: "#FF9800",  # Orange
        ERROR_BOARD: "#F44336",     # Red
        ERROR_CELL: "#F44336",      # Red
        ERROR_JACK: "#F44336",      # Red
        PRE_CHARGE: "#FFC107",      # Yellow
        CHARGER_PARAM_ERROR: "#F44336",  # Red
        BMS_PROHIBIT: "#F44336",    # Red
        CHARGING_CV: "#2196F3",     # Blue
        CHARGER_OVER_OUTPUT_VOLTAGE: "#F44336",  # Red
        BATT_LOST_CAPACITY: "#FF9800",  # Orange
    }


class ModuleState:
    """Trạng thái module sạc"""
    IDLE = 0
    STARTING = 1
    RUNNING = 2
    OFFLINE = 3
    FAULT = 4
    RECOVERING = 5

    NAMES = {
        IDLE: "Idle",
        STARTING: "Starting",
        RUNNING: "Running",
        OFFLINE: "Offline",
        FAULT: "Fault",
        RECOVERING: "Recovering",
    }

    COLORS = {
        IDLE: "#9E9E9E",       # Gray
        STARTING: "#FFC107",   # Yellow
        RUNNING: "#2196F3",    # Blue
        OFFLINE: "#FF9800",    # Orange
        FAULT: "#F44336",     # Red
        RECOVERING: "#FFC107", # Yellow
    }


# ============== Status Data Classes ==============

@dataclass
class ModuleStatus:
    """Trạng thái 1 module sạc"""
    addr: int = 0
    group: int = 0
    voltage: float = 0.0   # V
    current: float = 0.0   # A
    temp_dcdc: float = 0.0  # °C
    temp_ambient: float = 0.0  # °C
    alarm_status: int = 0
    input_power: int = 0   # W
    state: int = ModuleState.IDLE
    online: bool = False


@dataclass
class SystemStatus:
    """Trạng thái hệ thống - RSP_STATUS (40 bytes)"""
    batt_voltage: float = 0.0      # V
    batt_current: float = 0.0       # A
    batt_soc: int = 0               # %
    batt_temp: int = 0               # °C
    vout: float = 0.0               # V
    iout_total: float = 0.0         # A
    temp_charger: int = 0            # °C
    charge_status: int = ChargeStatus.STOP
    modules_online: int = 0
    modules_fault: int = 0
    modules_total: int = 0
    alarm_flags: int = 0              # BMS alarm bits
    system_alarm: int = 0            # System alarm bits
    charging: bool = False
    btn_start: bool = False
    btn_stop: bool = False
    sd_status: int = 0               # 0=none, 1=OK, 2=error

    # Module list (dynamic)
    modules: List[ModuleStatus] = field(default_factory=list)

    FORMAT = "<ffBbffBBBBIIIBBB"  # 40 bytes
    SIZE = struct.calcsize(FORMAT)

    @classmethod
    def from_bytes(cls, data: bytes) -> "SystemStatus":
        if len(data) < cls.SIZE:
            raise ValueError(f"SystemStatus: expected {cls.SIZE} bytes, got {len(data)}")

        unpacked = struct.unpack(cls.FORMAT, data[:cls.SIZE])
        status = cls(
            batt_voltage=unpacked[0],
            batt_current=unpacked[1],
            batt_soc=unpacked[2],
            batt_temp=unpacked[3],
            vout=unpacked[4],
            iout_total=unpacked[5],
            temp_charger=unpacked[6],
            charge_status=unpacked[7],
            modules_online=unpacked[8],
            modules_fault=unpacked[9],
            modules_total=unpacked[10],
            alarm_flags=unpacked[11],
            system_alarm=unpacked[12],
            charging=bool(unpacked[13]),
            btn_start=bool(unpacked[14]),
            btn_stop=bool(unpacked[15]),
            sd_status=unpacked[16],
        )
        return status


# ============== BMS Status ==============

class BmsState:
    """Trạng thái BMS"""
    OFFLINE = 0
    ONLINE = 1
    FAULT = 2

    NAMES = {
        OFFLINE: "Offline",
        ONLINE: "Online",
        FAULT: "Fault",
    }


@dataclass
class BmsStatus:
    """Trạng thái BMS - RSP_BMS_STATUS (32 bytes)"""
    state: int = BmsState.OFFLINE
    online: bool = False
    batt_voltage: float = 0.0    # V
    batt_current: float = 0.0     # A
    soc: int = 0                  # %
    cap_remain: float = 0.0       # Ah
    soh: int = 0                  # %
    max_cell_volt: int = 0        # mV
    min_cell_volt: int = 0        # mV
    max_cell_temp: int = 0        # °C
    min_cell_temp: int = 0        # °C
    charge_relay_closed: bool = False
    discharge_relay_closed: bool = False
    alarm_flags: int = 0
    chg_volt_request: float = 0.0  # V
    chg_curr_request: float = 0.0   # A

    FORMAT = "<BBffBfhBBbBbBIff"  # 32 bytes
    SIZE = struct.calcsize(FORMAT)

    @classmethod
    def from_bytes(cls, data: bytes) -> "BmsStatus":
        if len(data) < cls.SIZE:
            raise ValueError(f"BmsStatus: expected {cls.SIZE} bytes, got {len(data)}")

        unpacked = struct.unpack(cls.FORMAT, data[:cls.SIZE])
        status = cls(
            state=unpacked[0],
            online=bool(unpacked[1]),
            batt_voltage=unpacked[2],
            batt_current=unpacked[3],
            soc=unpacked[4],
            cap_remain=unpacked[5],
            soh=unpacked[6],
            max_cell_volt=unpacked[7],
            min_cell_volt=unpacked[8],
            max_cell_temp=unpacked[9],
            min_cell_temp=unpacked[10],
            charge_relay_closed=bool(unpacked[11]),
            discharge_relay_closed=bool(unpacked[12]),
            alarm_flags=unpacked[13],
            chg_volt_request=unpacked[14],
            chg_curr_request=unpacked[15],
        )
        return status


# ============== Alarm Bit Definitions ==============

BMS_ALARM_BITS = {
    0: "Low Pack Voltage",
    1: "Low Cell Voltage",
    2: "High Pack Voltage",
    3: "High Cell Voltage",
    4: "High Temp (Charge)",
    9: "Over Charge Current",
    13: "BMS Offline",
    14: "Stale Data",
}

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


def get_bms_alarm_text(alarm_flags: int) -> str:
    """Chuyển alarm bits → text"""
    if alarm_flags == 0:
        return "No alarms"

    active = [name for bit, name in BMS_ALARM_BITS.items()
              if alarm_flags & (1 << bit)]
    return " | ".join(active) if active else "Unknown alarm"


def get_module_alarm_text(alarm_flags: int, driver_type: int = 1) -> str:
    """Chuyển alarm bits → text (theo driver type)"""
    if alarm_flags == 0:
        return "No alarms"

    if driver_type == 3:  # TONHE
        alarm_bits = TONHE_ALARM_BITS
    else:
        alarm_bits = MAXWELL_ALARM_BITS

    active = [name for bit, name in alarm_bits.items()
              if alarm_flags & (1 << bit)]
    return " | ".join(active) if active else "Unknown alarm"
