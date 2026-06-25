"""
Configuration Service - Handle PC ↔ MCU configuration exchange
"""

import logging
from typing import Optional, Callable
from dataclasses import dataclass

from models.pc_config import (
    CfgSection, DriverId, CfgSystem, CfgCharger, CfgModule, CfgProtect,
    CfgBms, CfgDisplay, RuntimeStatus, FullConfig
)
from services.protocol_handler import (
    CMD_READ_CONFIG, CMD_WRITE_CONFIG, CMD_READ_ALL_CONFIG, CMD_WRITE_ALL_CONFIG,
    CMD_GET_STATUS, CMD_HISTORY_GET, CMD_HISTORY_CLEAR, CMD_RESET_CONFIG,
    RSP_CONFIG, RSP_STATUS, RSP_HISTORY, RSP_ACK, RSP_NACK
)

logger = logging.getLogger(__name__)


class ConfigService:
    """Service quản lý cấu hình với MCU"""
    
    def __init__(self, protocol_handler):
        self.protocol_handler = protocol_handler
        self.current_config: Optional[FullConfig] = None
        self.status: Optional[RuntimeStatus] = None
        
        # Callbacks
        self.on_config_loaded: Optional[Callable[[FullConfig], None]] = None
        self.on_status_updated: Optional[Callable[[RuntimeStatus], None]] = None
        self.on_error: Optional[Callable[[str], None]] = None
        
        # Register handlers
        self._register_handlers()
    
    def _register_handlers(self):
        """Đăng ký các handler cho responses từ MCU"""
        if hasattr(self.protocol_handler, 'on_config'):
            self.protocol_handler.on_config = self._handle_config_response
        if hasattr(self.protocol_handler, 'on_status'):
            self.protocol_handler.on_status = self._handle_status_response
        if hasattr(self.protocol_handler, 'on_ack'):
            self.protocol_handler.on_ack = self._handle_ack
        if hasattr(self.protocol_handler, 'on_nack'):
            self.protocol_handler.on_nack = self._handle_nack
    
    def _handle_config_response(self, section: int, data: bytes):
        """Xử lý response cấu hình từ MCU"""
        try:
            if section == CfgSection.ALL:
                self.current_config = FullConfig.from_bytes(data)
                logger.info("Received full config from MCU")
                if self.on_config_loaded:
                    self.on_config_loaded(self.current_config)
            else:
                # Handle single section
                cfg_class = {
                    CfgSection.SYSTEM: CfgSystem,
                    CfgSection.CHARGER: CfgCharger,
                    CfgSection.MODULE: CfgModule,
                    CfgSection.PROTECT: CfgProtect,
                    CfgSection.BMS: CfgBms,
                    CfgSection.DISPLAY: CfgDisplay,
                }.get(section)
                
                if cfg_class:
                    config = cfg_class.from_bytes(data)
                    logger.info(f"Received config section {section}: {config}")
                    
        except Exception as e:
            logger.error(f"Error parsing config: {e}")
            if self.on_error:
                self.on_error(f"Config parse error: {e}")
    
    def _handle_status_response(self, status: RuntimeStatus):
        """Xử lý status response từ MCU"""
        self.status = status
        if self.on_status_updated:
            self.on_status_updated(status)
    
    def _handle_ack(self, cmd: int):
        """Xử lý ACK từ MCU"""
        logger.debug(f"ACK for cmd 0x{cmd:02X}")
    
    def _handle_nack(self, cmd: int, err_code: int):
        """Xử lý NACK từ MCU"""
        error_msg = f"NACK for cmd 0x{cmd:02X}, error {err_code}"
        logger.error(error_msg)
        if self.on_error:
            self.on_error(error_msg)
    
    # ============== Public API ==============
    
    def load_all_config(self) -> bool:
        """Yêu cầu MCU gửi toàn bộ cấu hình"""
        from services.protocol_handler import cmd_read_all_config
        frame = cmd_read_all_config()
        return self.protocol_handler.send_frame(frame)
    
    def load_config_section(self, section: CfgSection) -> bool:
        """Yêu cầu MCU gửi một section cấu hình"""
        from services.protocol_handler import cmd_read_config
        frame = cmd_read_config(section)
        return self.protocol_handler.send_frame(frame)
    
    def save_all_config(self, config: FullConfig) -> bool:
        """Gửi toàn bộ cấu hình xuống MCU"""
        from services.protocol_handler import cmd_write_all_config
        frame = cmd_write_all_config(config)
        result = self.protocol_handler.send_frame(frame)
        if result:
            self.current_config = config
        return result
    
    def save_config_section(self, section: CfgSection, data) -> bool:
        """Gửi một section cấu hình xuống MCU"""
        from services.protocol_handler import cmd_write_config
        frame = cmd_write_config(section, data.to_bytes())
        return self.protocol_handler.send_frame(frame)
    
    def request_status(self) -> bool:
        """Yêu cầu MCU gửi status hiện tại"""
        from services.protocol_handler import cmd_get_status
        frame = cmd_get_status()
        return self.protocol_handler.send_frame(frame)
    
    def reset_config(self) -> bool:
        """Reset cấu hình về mặc định"""
        from services.protocol_handler import cmd_reset_config
        frame = cmd_reset_config()
        return self.protocol_handler.send_frame(frame)
    
    def get_history(self, index: int) -> bool:
        """Đọc một record lịch sử"""
        from services.protocol_handler import cmd_history_get
        frame = cmd_history_get(index)
        return self.protocol_handler.send_frame(frame)
    
    def clear_history(self) -> bool:
        """Xóa lịch sử"""
        from services.protocol_handler import cmd_history_clear
        frame = cmd_history_clear()
        return self.protocol_handler.send_frame(frame)
    
    # ============== Convenience Methods ==============
    
    def get_current_config(self) -> Optional[FullConfig]:
        """Lấy cấu hình hiện tại"""
        return self.current_config
    
    def get_status(self) -> Optional[RuntimeStatus]:
        """Lấy status hiện tại"""
        return self.status
    
    def update_charger_params(self, voltage: float, current: float) -> bool:
        """Cập nhật thông số sạc nhanh"""
        if self.current_config:
            self.current_config.charger.target_voltage = voltage
            self.current_config.charger.target_current = current
            return self.save_config_section(CfgSection.CHARGER, self.current_config.charger)
        return False
    
    def set_driver(self, driver_id: DriverId) -> bool:
        """Chọn driver và cập nhật"""
        if self.current_config:
            self.current_config.system.driver_id = driver_id
            return self.save_config_section(CfgSection.SYSTEM, self.current_config.system)
        return False
