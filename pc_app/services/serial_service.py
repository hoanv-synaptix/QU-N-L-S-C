"""
Serial Service - USB CDC Communication
Quản lý kết nối COM port và gửi/nhận dữ liệu
"""

import serial
import serial.tools.list_ports
from typing import Optional, Callable, List
import threading
import time
import logging

logger = logging.getLogger(__name__)


class SerialService:
    """Service quản lý kết nối serial"""

    def __init__(self):
        self.serial_port: Optional[serial.Serial] = None
        self.running = False
        self.rx_thread: Optional[threading.Thread] = None
        self.rx_callback: Optional[Callable[[bytes], None]] = None

    @staticmethod
    def list_ports() -> List[str]:
        """Liệt kê các cổng COM available"""
        ports = serial.tools.list_ports.comports()
        return [p.device for p in ports]

    def connect(self, port: str, baudrate: int = 115200, timeout: float = 0.1) -> bool:
        """
        Kết nối đến cổng serial

        Args:
            port: Tên cổng COM (vd: 'COM3')
            baudrate: Tốc độ baud (mặc định 115200 cho USB CDC)
            timeout: Timeout cho read (giây)

        Returns:
            True nếu kết nối thành công
        """
        try:
            self.disconnect()

            self.serial_port = serial.Serial(
                port=port,
                baudrate=baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=timeout,
                write_timeout=1.0
            )

            logger.info(f"Connected to {port} at {baudrate} bps")
            return True

        except serial.SerialException as e:
            logger.error(f"Failed to connect to {port}: {e}")
            return False

    def disconnect(self):
        """Ngắt kết nối"""
        self.running = False

        if self.rx_thread and self.rx_thread.is_alive():
            self.rx_thread.join(timeout=1.0)

        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
            self.serial_port = None

        logger.info("Disconnected")

    def is_connected(self) -> bool:
        """Kiểm tra đang kết nối"""
        return self.serial_port is not None and self.serial_port.is_open

    def send(self, data: bytes) -> bool:
        """
        Gửi dữ liệu

        Args:
            data: Bytes cần gửi

        Returns:
            True nếu gửi thành công
        """
        if not self.is_connected():
            logger.warning("Cannot send: not connected")
            return False

        try:
            self.serial_port.write(data)
            return True

        except serial.SerialException as e:
            logger.error(f"Send failed: {e}")
            return False

    def start_rx(self, callback: Callable[[bytes], None]):
        """
        Bắt đầu nhận dữ liệu trong background thread

        Args:
            callback: Hàm được gọi khi có dữ liệu nhận được
        """
        if not self.is_connected():
            logger.warning("Cannot start RX: not connected")
            return

        self.rx_callback = callback
        self.running = True
        self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        self.rx_thread.start()
        logger.info("RX thread started")

    def stop_rx(self):
        """Dừng nhận dữ liệu"""
        self.running = False

    def _rx_loop(self):
        """Background thread nhận dữ liệu"""
        buffer = bytearray()

        while self.running and self.is_connected():
            try:
                # Đọc dữ liệu available
                data = self.serial_port.read(64)

                if data:
                    buffer.extend(data)

                    # Gọi callback với dữ liệu đã nhận
                    if self.rx_callback:
                        self.rx_callback(bytes(data))

            except serial.SerialException as e:
                logger.error(f"RX error: {e}")
                break
            except Exception as e:
                logger.error(f"Unexpected RX error: {e}")
                break

        logger.info("RX thread stopped")

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.disconnect()
