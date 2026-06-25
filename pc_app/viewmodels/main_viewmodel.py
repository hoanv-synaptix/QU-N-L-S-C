"""
Main ViewModel - Main window logic
Quản lý kết nối, trạng thái app, và điều phối các tab
"""

import logging
from typing import Optional, List
from dataclasses import dataclass, field
from datetime import datetime

from services.serial_service import SerialService
from services.protocol_handler import (
    ProtocolHandler, cmd_ping, cmd_start, cmd_stop, cmd_emergency_stop,
    cmd_set_voltage, cmd_set_current, cmd_set_driver, cmd_read_all_config,
    cmd_write_all_config, cmd_set_module_count, get_error_name
)
from models.charger_status import SystemStatus, BmsStatus, ChargeStatus, ModuleState
from models.charger_config import ChargerConfig, DriverType

logger = logging.getLogger(__name__)


@dataclass
class LogEntry:
    """1 dòng log"""
    timestamp: datetime
    level: str  # INFO, WARN, ERROR
    message: str

    def to_csv_row(self) -> List[str]:
        return [
            self.timestamp.strftime("%Y-%m-%d %H:%M:%S"),
            self.level,
            self.message
        ]


class MainViewModel:
    """Main ViewModel - điều phối toàn bộ app"""

    def __init__(self):
        # Services
        self.serial_service = SerialService()
        self.protocol_handler = ProtocolHandler()

        # State
        self.connected = False
        self.fw_version: Optional[int] = None
        self.driver_type: int = DriverType.MAXWELL

        # Data
        self.system_status: Optional[SystemStatus] = None
        self.bms_status: Optional[BmsStatus] = None
        self.charger_config = ChargerConfig()  # Initialize default config

        # Logs
        self.logs: List[LogEntry] = []
        self.max_logs = 1000

        # Callbacks (sẽ được set từ View)
        self.on_connection_changed: Optional[callable] = None
        self.on_status_updated: Optional[callable] = None
        self.on_bms_updated: Optional[callable] = None
        self.on_config_updated: Optional[callable] = None
        self.on_log_added: Optional[callable] = None
        self.on_error: Optional[callable] = None

        # Setup protocol callbacks
        self._setup_protocol_callbacks()

    def _setup_protocol_callbacks(self):
        """Setup callbacks cho protocol handler"""
        h = self.protocol_handler

        h.on_status = self._on_status
        h.on_bms_status = self._on_bms_status
        h.on_pong = self._on_pong
        h.on_ack = self._on_ack
        h.on_nack = self._on_nack
        h.on_config = self._on_config
        h.on_log = self._on_protocol_log

    def _on_status(self, status: SystemStatus):
        """Callback khi nhận status"""
        self.system_status = status
        if self.on_status_updated:
            self.on_status_updated(status)

    def _on_bms_status(self, status: BmsStatus):
        """Callback khi nhận BMS status"""
        self.bms_status = status
        if self.on_bms_updated:
            self.on_bms_updated(status)

    def _on_pong(self, version: int):
        """Callback khi nhận PONG"""
        self.fw_version = version
        major = (version >> 16) & 0xFF
        minor = (version >> 8) & 0xFF
        patch = version & 0xFF
        self._log(f"FW Version: v{major}.{minor}.{patch}", "INFO")
        self._notify_connection_changed()

    def _on_ack(self, cmd: int):
        """Callback khi nhận ACK"""
        self._log(f"ACK: cmd=0x{cmd:02X}", "INFO")

    def _on_nack(self, cmd: int, err_code: int):
        """Callback khi nhận NACK"""
        err_name = get_error_name(err_code)
        self._log(f"NACK: cmd=0x{cmd:02X}, error={err_name}", "ERROR")
        if self.on_error:
            self.on_error(f"Command 0x{cmd:02X} failed: {err_name}")

    def _on_protocol_log(self, msg: str):
        """Callback cho protocol log"""
        logger.debug(msg)

    def _on_config(self, section: int, data: bytes):
        """Callback khi nhận config response"""
        from models.charger_config import (
            BattConfig, CellConfig, SocConfig, TempConfig, ProtectConfig, ModuleConfig, CanConfig
        )

        try:
            if section == 0x01:
                self.charger_config.battery = BattConfig.from_bytes(data)
                self._log("Received Battery Config", "INFO")
            elif section == 0x02:
                self.charger_config.cell = CellConfig.from_bytes(data)
                self._log("Received Cell Config", "INFO")
            elif section == 0x03:
                self.charger_config.soc = SocConfig.from_bytes(data)
                self._log("Received SOC Config", "INFO")
            elif section == 0x04:
                self.charger_config.temp = TempConfig.from_bytes(data)
                self._log("Received Temp Config", "INFO")
            elif section == 0x05:
                self.charger_config.protect = ProtectConfig.from_bytes(data)
                self._log("Received Protect Config", "INFO")
            elif section == 0x06:
                self.charger_config.module = ModuleConfig.from_bytes(data)
                self._log("Received Module Config", "INFO")
            elif section == 0x07:
                self.charger_config.can = CanConfig.from_bytes(data)
                self._log("Received CAN Config", "INFO")
            else:
                self._log(f"Unknown config section: 0x{section:02X}", "WARN")

            # Notify view to update UI
            if self.on_config_updated:
                self.on_config_updated(self.charger_config)

        except Exception as e:
            self._log(f"Error parsing config: {e}", "ERROR")

    # ============== Connection ==============

    def get_available_ports(self) -> List[str]:
        """Lấy danh sách cổng COM available"""
        return SerialService.list_ports()

    def connect(self, port: str) -> bool:
        """Kết nối đến device"""
        if not self.serial_service.connect(port):
            self._log(f"Failed to connect to {port}", "ERROR")
            return False

        # Start RX thread
        self.protocol_handler.serial_service = self.serial_service
        self.serial_service.start_rx(self.protocol_handler.feed_data)

        # Send ping to get version
        self.send_command(cmd_ping())

        self.connected = True
        self._log(f"Connected to {port}", "INFO")
        self._notify_connection_changed()
        return True

    def disconnect(self):
        """Ngắt kết nối"""
        self.serial_service.disconnect()
        self.connected = False
        self._log("Disconnected", "INFO")
        self._notify_connection_changed()

    def _notify_connection_changed(self):
        if self.on_connection_changed:
            self.on_connection_changed(self.connected)

    # ============== Commands ==============

    def send_command(self, frame: bytes) -> bool:
        """Gửi command"""
        return self.serial_service.send(frame)

    def cmd_start(self) -> bool:
        """Bắt đầu sạc"""
        self._log("Start charging", "INFO")
        return self.send_command(cmd_start())

    def cmd_stop(self) -> bool:
        """Dừng sạc"""
        self._log("Stop charging", "INFO")
        return self.send_command(cmd_stop())

    def cmd_emergency_stop(self) -> bool:
        """Dừng khẩn cấp"""
        self._log("EMERGENCY STOP", "WARN")
        return self.send_command(cmd_emergency_stop())

    def cmd_set_voltage(self, voltage: float) -> bool:
        """Đặt điện áp"""
        self._log(f"Set voltage: {voltage:.1f}V", "INFO")
        return self.send_command(cmd_set_voltage(voltage))

    def cmd_set_current(self, current: float) -> bool:
        """Đặt dòng"""
        self._log(f"Set current: {current:.1f}A", "INFO")
        return self.send_command(cmd_set_current(current))

    def cmd_set_driver(self, driver_id: int) -> bool:
        """Chọn driver"""
        self.driver_type = driver_id
        driver_name = DriverType.NAMES.get(driver_id, "Unknown")
        self._log(f"Set driver: {driver_name}", "INFO")
        return self.send_command(cmd_set_driver(driver_id))

    def cmd_set_module_count(self, count: int) -> bool:
        """Đặt số module"""
        self._log(f"Set module count: {count}", "INFO")
        return self.send_command(cmd_set_module_count(count))

    def cmd_read_config(self) -> bool:
        """Đọc config từ device"""
        self._log("Reading config from device...", "INFO")
        return self.send_command(cmd_read_all_config())

    def cmd_write_config(self, config: ChargerConfig) -> bool:
        """Ghi config xuống device"""
        self._log("Writing config to device...", "INFO")
        return self.send_command(cmd_write_all_config(config))

    # ============== Logging ==============

    def _log(self, message: str, level: str = "INFO"):
        """Thêm log entry"""
        entry = LogEntry(
            timestamp=datetime.now(),
            level=level,
            message=message
        )
        self.logs.append(entry)

        # Limit logs size
        if len(self.logs) > self.max_logs:
            self.logs = self.logs[-self.max_logs:]

        # Notify
        if self.on_log_added:
            self.on_log_added(entry)

    def get_logs(self, level: Optional[str] = None) -> List[LogEntry]:
        """Lấy logs, có thể lọc theo level"""
        if level:
            return [log for log in self.logs if log.level == level]
        return self.logs

    def clear_logs(self):
        """Xóa logs"""
        self.logs.clear()

    def export_logs_csv(self, filepath: str) -> bool:
        """Export logs ra CSV"""
        try:
            import csv
            with open(filepath, 'w', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                writer.writerow(["Time", "Level", "Message"])
                for log in self.logs:
                    writer.writerow(log.to_csv_row())
            self._log(f"Logs exported to {filepath}", "INFO")
            return True
        except Exception as e:
            self._log(f"Failed to export logs: {e}", "ERROR")
            return False
