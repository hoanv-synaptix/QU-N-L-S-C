"""
Main Window - Entry point cho PyQt5 UI
SacPin Charger Control Application
"""

import sys
import logging
from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QTabWidget, QLabel, QPushButton, QComboBox, QMessageBox,
    QStatusBar, QMenuBar, QMenu, QGroupBox, QGridLayout, QTableWidget,
    QTableWidgetItem, QSpinBox, QFrame, QScrollArea, QSizePolicy
)
from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QColor
from PyQt5.QtWidgets import QAction

from viewmodels.main_viewmodel import MainViewModel
from models.charger_status import ChargeStatus, BmsState

logger = logging.getLogger(__name__)


class ModuleCard(QFrame):
    """Card hiển thị thông tin 1 module sạc"""

    def __init__(self, module_id: int, parent=None):
        super().__init__(parent)
        self.module_id = module_id
        self._setup_ui()

    def _setup_ui(self):
        self.setFrameStyle(QFrame.Shape.StyledPanel | QFrame.Shadow.Raised)
        self.setMinimumSize(150, 120)
        self.setStyleSheet("""
            QFrame {
                background-color: #FFFFFF;
                border: 2px solid #E0E0E0;
                border-radius: 8px;
                padding: 5px;
            }
        """)

        layout = QVBoxLayout(self)

        # Header
        header = QLabel(f"Module #{self.module_id + 1}")
        header.setStyleSheet("font-weight: bold; font-size: 12px;")
        layout.addWidget(header)

        # Info labels
        self.voltage_label = QLabel("V: --- V")
        layout.addWidget(self.voltage_label)

        self.current_label = QLabel("I: --- A")
        layout.addWidget(self.current_label)

        self.temp_label = QLabel("Temp: --- °C")
        layout.addWidget(self.temp_label)

        self.state_label = QLabel("State: ---")
        layout.addWidget(self.state_label)

    def update_status(self, voltage: float, current: float, temp: float, state: int, online: bool):
        """Cập nhật trạng thái module"""
        self.voltage_label.setText(f"V: {voltage:.1f} V")
        self.current_label.setText(f"I: {current:.1f} A")
        self.temp_label.setText(f"Temp: {temp:.1f} °C")

        # State
        from models.charger_status import ModuleState
        state_name = ModuleState.NAMES.get(state, "Unknown")
        self.state_label.setText(f"State: {state_name}")

        # Color based on state
        if not online:
            border_color = "#FF9800"  # Orange - offline
            state_color = "#FF9800"
        elif state == ModuleState.FAULT:
            border_color = "#F44336"  # Red - fault
            state_color = "#F44336"
        elif state == ModuleState.RUNNING:
            border_color = "#4CAF50"  # Green - running
            state_color = "#4CAF50"
        elif state == ModuleState.STARTING or state == ModuleState.RECOVERING:
            border_color = "#FFC107"  # Yellow
            state_color = "#FFC107"
        else:
            border_color = "#E0E0E0"  # Gray
            state_color = "#9E9E9E"

        self.setStyleSheet(f"""
            QFrame {{
                background-color: #FFFFFF;
                border: 2px solid {border_color};
                border-radius: 8px;
                padding: 5px;
            }}
        """)
        self.state_label.setStyleSheet(f"color: {state_color}; font-weight: bold;")


class MainWindow(QMainWindow):
    """Main Window"""

    def __init__(self):
        super().__init__()

        # ViewModel
        self.viewmodel = MainViewModel()

        # Module cards
        self.module_cards = []

        # Setup UI
        self._setup_ui()
        self._setup_callbacks()
        self._refresh_ports()

    def _setup_ui(self):
        """Setup UI components"""
        self.setWindowTitle("SacPin Charger Control v1.0")
        self.setMinimumSize(1200, 800)

        # Central widget
        central = QWidget()
        self.setCentralWidget(central)

        main_layout = QVBoxLayout(central)

        # Tab widget
        self.tabs = QTabWidget()
        main_layout.addWidget(self.tabs)

        # Create tabs
        self._create_monitor_tab()
        self._create_config_tab()
        self._create_log_tab()

        # Status bar
        self.statusBar().showMessage("Disconnected")

        # Menu bar
        self._create_menu()

    def _create_menu(self):
        """Create menu bar"""
        menubar = self.menuBar()

        # File menu
        file_menu = menubar.addMenu("File")

        load_config_action = QAction("Load Config", self)
        load_config_action.triggered.connect(self._on_load_config)
        file_menu.addAction(load_config_action)

        save_config_action = QAction("Save Config", self)
        save_config_action.triggered.connect(self._on_save_config)
        file_menu.addAction(save_config_action)

        file_menu.addSeparator()

        export_log_action = QAction("Export Log", self)
        export_log_action.triggered.connect(self._on_export_log)
        file_menu.addAction(export_log_action)

        file_menu.addSeparator()

        exit_action = QAction("Exit", self)
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)

        # Help menu
        help_menu = menubar.addMenu("Help")

        about_action = QAction("About", self)
        about_action.triggered.connect(self._on_about)
        help_menu.addAction(about_action)

    def _create_connection_tab(self):
        """Create connection tab"""
        connection_group = QGroupBox("Kết nối")
        layout = QGridLayout(connection_group)

        # Port selection
        layout.addWidget(QLabel("COM Port:"), 0, 0)
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(150)
        layout.addWidget(self.port_combo, 0, 1)

        self.refresh_btn = QPushButton("Refresh")
        self.refresh_btn.clicked.connect(self._refresh_ports)
        layout.addWidget(self.refresh_btn, 0, 2)

        # Connect button
        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self._toggle_connection)
        layout.addWidget(self.connect_btn, 0, 3)

        # Connection status
        self.connection_label = QLabel("Disconnected")
        self.connection_label.setStyleSheet("color: red; font-weight: bold;")
        layout.addWidget(self.connection_label, 0, 4)

        # Driver selection
        layout.addWidget(QLabel("Driver:"), 1, 0)
        self.driver_combo = QComboBox()
        self.driver_combo.addItems(["Maxwell MXR", "LIANMING", "TONHE V1.3"])
        self.driver_combo.setCurrentIndex(0)
        layout.addWidget(self.driver_combo, 1, 1, 1, 2)

        self.set_driver_btn = QPushButton("Set Driver")
        self.set_driver_btn.clicked.connect(self._on_set_driver)
        layout.addWidget(self.set_driver_btn, 1, 3)

        # Module count
        layout.addWidget(QLabel("Module Count:"), 2, 0)
        self.module_count_spin = QSpinBox()
        self.module_count_spin.setMinimum(1)
        self.module_count_spin.setMaximum(8)
        self.module_count_spin.setValue(1)
        layout.addWidget(self.module_count_spin, 2, 1)

        self.set_module_count_btn = QPushButton("Set")
        self.set_module_count_btn.clicked.connect(self._on_set_module_count)
        layout.addWidget(self.set_module_count_btn, 2, 2)

        # FW Version
        layout.addWidget(QLabel("FW Version:"), 2, 3)
        self.fw_version_label = QLabel("---")
        layout.addWidget(self.fw_version_label, 2, 4)

        # SD Card Status
        layout.addWidget(QLabel("SD Card:"), 3, 0)
        self.sd_status_label = QLabel("---")
        layout.addWidget(self.sd_status_label, 3, 1)

        return connection_group

    def _create_monitor_tab(self):
        """Create monitor tab"""
        monitor_widget = QWidget()
        layout = QVBoxLayout(monitor_widget)

        # === Row 1: Connection + Charger Status ===
        row1_layout = QHBoxLayout()

        # Connection group
        row1_layout.addWidget(self._create_connection_tab())

        # Charger status group
        status_group = QGroupBox("Trạng thái bộ sạc")
        status_layout = QGridLayout(status_group)

        # Row 1
        status_layout.addWidget(QLabel("Điện áp pin:"), 0, 0)
        self.batt_voltage_label = QLabel("--- V")
        status_layout.addWidget(self.batt_voltage_label, 0, 1)

        status_layout.addWidget(QLabel("Dòng sạc:"), 0, 2)
        self.batt_current_label = QLabel("--- A")
        status_layout.addWidget(self.batt_current_label, 0, 3)

        status_layout.addWidget(QLabel("SOC:"), 0, 4)
        self.soc_label = QLabel("--- %")
        status_layout.addWidget(self.soc_label, 0, 5)

        # Row 2
        status_layout.addWidget(QLabel("Nhiệt độ pin:"), 1, 0)
        self.batt_temp_label = QLabel("--- °C")
        status_layout.addWidget(self.batt_temp_label, 1, 1)

        status_layout.addWidget(QLabel("Điện áp output:"), 1, 2)
        self.vout_label = QLabel("--- V")
        status_layout.addWidget(self.vout_label, 1, 3)

        status_layout.addWidget(QLabel("Dòng output:"), 1, 4)
        self.iout_label = QLabel("--- A")
        status_layout.addWidget(self.iout_label, 1, 5)

        # Row 3 - Status and control
        status_layout.addWidget(QLabel("Trạng thái:"), 2, 0)
        self.charge_status_label = QLabel("Stop")
        status_layout.addWidget(self.charge_status_label, 2, 1, 1, 2)

        self.start_btn = QPushButton("START")
        self.start_btn.setStyleSheet("""
            QPushButton {
                background-color: #4CAF50;
                color: white;
                font-weight: bold;
                padding: 10px;
                border-radius: 5px;
            }
            QPushButton:disabled {
                background-color: #CCCCCC;
            }
        """)
        self.start_btn.clicked.connect(self._on_start)
        status_layout.addWidget(self.start_btn, 2, 3)

        self.stop_btn = QPushButton("STOP")
        self.stop_btn.setStyleSheet("""
            QPushButton {
                background-color: #F44336;
                color: white;
                font-weight: bold;
                padding: 10px;
                border-radius: 5px;
            }
            QPushButton:disabled {
                background-color: #CCCCCC;
            }
        """)
        self.stop_btn.clicked.connect(self._on_stop)
        status_layout.addWidget(self.stop_btn, 2, 4)

        self.estop_btn = QPushButton("EMERGENCY STOP")
        self.estop_btn.setStyleSheet("""
            QPushButton {
                background-color: #B71C1C;
                color: white;
                font-weight: bold;
                padding: 10px;
                border-radius: 5px;
            }
        """)
        self.estop_btn.clicked.connect(self._on_emergency_stop)
        status_layout.addWidget(self.estop_btn, 2, 5)

        row1_layout.addWidget(status_group)
        layout.addLayout(row1_layout)

        # === Row 2: BMS Panel + Module Cards ===
        row2_layout = QHBoxLayout()

        # BMS Panel
        bms_group = QGroupBox("BMS (Pin)")
        bms_layout = QGridLayout(bms_group)

        # Row 1
        bms_layout.addWidget(QLabel("Trạng thái:"), 0, 0)
        self.bms_state_label = QLabel("Offline")
        self.bms_state_label.setStyleSheet("font-weight: bold;")
        bms_layout.addWidget(self.bms_state_label, 0, 1)

        bms_layout.addWidget(QLabel("Điện áp:"), 0, 2)
        self.bms_voltage_label = QLabel("--- V")
        bms_layout.addWidget(self.bms_voltage_label, 0, 3)

        bms_layout.addWidget(QLabel("Dòng:"), 0, 4)
        self.bms_current_label = QLabel("--- A")
        bms_layout.addWidget(self.bms_current_label, 0, 5)

        # Row 2
        bms_layout.addWidget(QLabel("SOC:"), 1, 0)
        self.bms_soc_label = QLabel("--- %")
        bms_layout.addWidget(self.bms_soc_label, 1, 1)

        bms_layout.addWidget(QLabel("SOH:"), 1, 2)
        self.bms_soh_label = QLabel("--- %")
        bms_layout.addWidget(self.bms_soh_label, 1, 3)

        bms_layout.addWidget(QLabel("Dung lượng:"), 1, 4)
        self.bms_cap_label = QLabel("--- Ah")
        bms_layout.addWidget(self.bms_cap_label, 1, 5)

        # Row 3 - Cell voltages
        bms_layout.addWidget(QLabel("Cell Volt:"), 2, 0)
        self.bms_cell_volt_label = QLabel("--- mV")
        bms_layout.addWidget(self.bms_cell_volt_label, 2, 1, 1, 2)

        bms_layout.addWidget(QLabel("Cell Temp:"), 2, 3)
        self.bms_cell_temp_label = QLabel("--- °C")
        bms_layout.addWidget(self.bms_cell_temp_label, 2, 4, 1, 2)

        # Row 4 - Relays
        bms_layout.addWidget(QLabel("Relay Sạc:"), 3, 0)
        self.bms_chg_relay_label = QLabel("OFF")
        bms_layout.addWidget(self.bms_chg_relay_label, 3, 1)

        bms_layout.addWidget(QLabel("Relay Xả:"), 3, 2)
        self.bms_discharge_relay_label = QLabel("OFF")
        bms_layout.addWidget(self.bms_discharge_relay_label, 3, 3)

        # Row 5 - Alarm
        bms_layout.addWidget(QLabel("Cảnh báo:"), 4, 0)
        self.bms_alarm_label = QLabel("None")
        self.bms_alarm_label.setStyleSheet("color: green;")
        bms_layout.addWidget(self.bms_alarm_label, 4, 1, 1, 5)

        # BMS Request
        bms_layout.addWidget(QLabel("Yêu cầu:"), 5, 0)
        self.bms_request_label = QLabel("V: --- V, I: --- A")
        bms_layout.addWidget(self.bms_request_label, 5, 1, 1, 5)

        row2_layout.addWidget(bms_group)

        # Module Cards Area
        module_group = QGroupBox("Danh sách Module")
        module_layout = QVBoxLayout(module_group)

        # Scroll area for module cards
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setMinimumHeight(180)

        cards_container = QWidget()
        self.module_cards_layout = QHBoxLayout(cards_container)
        self.module_cards_layout.setAlignment(Qt.AlignmentFlag.AlignLeft)

        # Create 8 module cards (max)
        for i in range(8):
            card = ModuleCard(i)
            card.setVisible(False)  # Hidden by default
            self.module_cards.append(card)
            self.module_cards_layout.addWidget(card)

        scroll.setWidget(cards_container)
        module_layout.addWidget(scroll)

        row2_layout.addWidget(module_group)
        layout.addLayout(row2_layout)

        # === Row 2.5: Real-time Chart ===
        chart_group = QGroupBox("Đồ thị Realtime")
        chart_layout = QVBoxLayout(chart_group)

        try:
            from views.widgets.chart_widget import RealtimeChart
            self.realtime_chart = RealtimeChart(max_points=100)
            chart_layout.addWidget(self.realtime_chart)
        except Exception as e:
            # If pyqtgraph not available, show placeholder
            placeholder = QLabel("Chart requires pyqtgraph")
            placeholder.setAlignment(Qt.AlignmentFlag.AlignCenter)
            chart_layout.addWidget(placeholder)
            self.realtime_chart = None

        layout.addWidget(chart_group)

        # === Row 3: System Info ===
        info_layout = QHBoxLayout()

        # Module summary
        module_summary_group = QGroupBox("Tổng kết Module")
        module_summary_layout = QGridLayout(module_summary_group)
        module_summary_layout.addWidget(QLabel("Online:"), 0, 0)
        self.modules_online_label = QLabel("0 / 0")
        module_summary_layout.addWidget(self.modules_online_label, 0, 1)
        module_summary_layout.addWidget(QLabel("Lỗi:"), 0, 2)
        self.modules_fault_label = QLabel("0")
        self.modules_fault_label.setStyleSheet("color: red;")
        module_summary_layout.addWidget(self.modules_fault_label, 0, 3)
        info_layout.addWidget(module_summary_group)

        # System alarm
        alarm_group = QGroupBox("Cảnh báo hệ thống")
        alarm_layout = QVBoxLayout(alarm_group)
        self.system_alarm_label = QLabel("None")
        self.system_alarm_label.setStyleSheet("color: green;")
        alarm_layout.addWidget(self.system_alarm_label)
        info_layout.addWidget(alarm_group)

        layout.addLayout(info_layout)

        self.tabs.addTab(monitor_widget, "Monitor")

    def _create_config_tab(self):
        """Create config tab"""
        from views.widgets.config_widgets import (
            BatteryConfigWidget, CellConfigWidget, SocConfigWidget,
            TempConfigWidget, ProtectConfigWidget, ModuleConfigWidget
        )

        config_widget = QWidget()
        layout = QVBoxLayout(config_widget)

        # Toolbar
        toolbar = QHBoxLayout()

        self.read_config_btn = QPushButton("Đọc từ Device")
        self.read_config_btn.clicked.connect(self._on_read_config)
        toolbar.addWidget(self.read_config_btn)

        self.write_config_btn = QPushButton("Ghi xuống Device")
        self.write_config_btn.clicked.connect(self._on_write_config)
        toolbar.addWidget(self.write_config_btn)

        toolbar.addStretch()

        self.load_file_btn = QPushButton("Load từ File")
        self.load_file_btn.clicked.connect(self._on_load_config)
        toolbar.addWidget(self.load_file_btn)

        self.save_file_btn = QPushButton("Save ra File")
        self.save_file_btn.clicked.connect(self._on_save_config)
        toolbar.addWidget(self.save_file_btn)

        layout.addLayout(toolbar)

        # Scroll area for config widgets
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)

        container = QWidget()
        container_layout = QVBoxLayout(container)

        # Config widgets
        self.battery_config = BatteryConfigWidget()
        container_layout.addWidget(self.battery_config)

        self.cell_config = CellConfigWidget()
        container_layout.addWidget(self.cell_config)

        self.soc_config = SocConfigWidget()
        container_layout.addWidget(self.soc_config)

        self.temp_config = TempConfigWidget()
        container_layout.addWidget(self.temp_config)

        self.protect_config = ProtectConfigWidget()
        container_layout.addWidget(self.protect_config)

        self.module_config = ModuleConfigWidget()
        container_layout.addWidget(self.module_config)

        container_layout.addStretch()

        scroll.setWidget(container)
        layout.addWidget(scroll)

        self.tabs.addTab(config_widget, "Config")

    def _create_log_tab(self):
        """Create log tab"""
        log_widget = QWidget()
        layout = QVBoxLayout(log_widget)

        # Toolbar
        toolbar = QHBoxLayout()

        filter_label = QLabel("Lọc:")
        toolbar.addWidget(filter_label)

        self.log_filter_combo = QComboBox()
        self.log_filter_combo.addItems(["All", "INFO", "WARN", "ERROR"])
        self.log_filter_combo.currentTextChanged.connect(self._apply_log_filter)
        toolbar.addWidget(self.log_filter_combo)

        toolbar.addStretch()

        clear_btn = QPushButton("Clear Log")
        clear_btn.clicked.connect(self._on_clear_log)
        toolbar.addWidget(clear_btn)

        layout.addLayout(toolbar)

        # Log table
        self.log_table = QTableWidget()
        self.log_table.setColumnCount(3)
        self.log_table.setHorizontalHeaderLabels(["Time", "Level", "Message"])
        self.log_table.setColumnWidth(0, 150)
        self.log_table.setColumnWidth(1, 80)
        self.log_table.setMinimumHeight(400)
        layout.addWidget(self.log_table)

        self.tabs.addTab(log_widget, "Log")

    def _setup_callbacks(self):
        """Setup ViewModel callbacks"""
        vm = self.viewmodel

        vm.on_connection_changed = self._on_connection_changed
        vm.on_status_updated = self._on_status_updated
        vm.on_bms_updated = self._on_bms_updated
        vm.on_config_updated = self._on_config_updated
        vm.on_log_added = self._on_log_added
        vm.on_error = self._on_error

    # ============== Event Handlers ==============

    def _refresh_ports(self):
        """Refresh COM port list"""
        ports = self.viewmodel.get_available_ports()
        self.port_combo.clear()
        self.port_combo.addItems(ports)

        if not ports:
            QMessageBox.warning(
                self, "Warning",
                "Không tìm thấy cổng COM nào!"
            )

    def _toggle_connection(self):
        """Toggle connection"""
        if self.viewmodel.connected:
            self.viewmodel.disconnect()
        else:
            port = self.port_combo.currentText()
            if not port:
                QMessageBox.warning(self, "Warning", "Vui lòng chọn cổng COM!")
                return

            if not self.viewmodel.connect(port):
                QMessageBox.critical(
                    self, "Error",
                    f"Không thể kết nối đến {port}!"
                )

    def _on_connection_changed(self, connected: bool):
        """Handle connection changed"""
        if connected:
            self.connect_btn.setText("Disconnect")
            self.connection_label.setText("Connected")
            self.connection_label.setStyleSheet("color: green; font-weight: bold;")
            self.statusBar().showMessage("Connected")
            self.start_btn.setEnabled(True)
            self.stop_btn.setEnabled(True)
        else:
            self.connect_btn.setText("Connect")
            self.connection_label.setText("Disconnected")
            self.connection_label.setStyleSheet("color: red; font-weight: bold;")
            self.statusBar().showMessage("Disconnected")
            self.fw_version_label.setText("---")
            self.sd_status_label.setText("---")
            self.start_btn.setEnabled(False)
            self.stop_btn.setEnabled(False)

            # Reset displays
            self._reset_status_display()
            self._reset_bms_display()

    def _reset_status_display(self):
        """Reset status display to defaults"""
        self.batt_voltage_label.setText("--- V")
        self.batt_current_label.setText("--- A")
        self.soc_label.setText("--- %")
        self.batt_temp_label.setText("--- °C")
        self.vout_label.setText("--- V")
        self.iout_label.setText("--- A")
        self.charge_status_label.setText("Stop")
        self.charge_status_label.setStyleSheet("color: #9E9E9E;")
        self.modules_online_label.setText("0 / 0")
        self.modules_fault_label.setText("0")
        self.system_alarm_label.setText("None")
        self.system_alarm_label.setStyleSheet("color: green;")

        # Hide all module cards
        for card in self.module_cards:
            card.setVisible(False)

    def _reset_bms_display(self):
        """Reset BMS display to defaults"""
        self.bms_state_label.setText("Offline")
        self.bms_state_label.setStyleSheet("color: #9E9E9E;")
        self.bms_voltage_label.setText("--- V")
        self.bms_current_label.setText("--- A")
        self.bms_soc_label.setText("--- %")
        self.bms_soh_label.setText("--- %")
        self.bms_cap_label.setText("--- Ah")
        self.bms_cell_volt_label.setText("--- mV")
        self.bms_cell_temp_label.setText("--- °C")
        self.bms_chg_relay_label.setText("OFF")
        self.bms_discharge_relay_label.setText("OFF")
        self.bms_alarm_label.setText("None")
        self.bms_alarm_label.setStyleSheet("color: green;")
        self.bms_request_label.setText("V: --- V, I: --- A")

    def _on_status_updated(self, status):
        """Handle status updated"""
        # Update labels
        self.batt_voltage_label.setText(f"{status.batt_voltage:.1f} V")
        self.batt_current_label.setText(f"{status.batt_current:.1f} A")
        self.soc_label.setText(f"{status.batt_soc} %")
        self.batt_temp_label.setText(f"{status.batt_temp} °C")
        self.vout_label.setText(f"{status.vout:.1f} V")
        self.iout_label.setText(f"{status.iout_total:.1f} A")

        # SD Status
        sd_status_map = {0: "None", 1: "OK", 2: "Error"}
        self.sd_status_label.setText(sd_status_map.get(status.sd_status, "Unknown"))

        # Update charge status
        status_name = ChargeStatus.NAMES.get(status.charge_status, "Unknown")
        self.charge_status_label.setText(status_name)

        # Color based on status
        status_color = ChargeStatus.COLORS.get(status.charge_status, "#9E9E9E")
        self.charge_status_label.setStyleSheet(f"color: {status_color}; font-weight: bold;")

        # Update button states
        self.start_btn.setEnabled(not status.charging and status.modules_online > 0)
        self.stop_btn.setEnabled(status.charging)

        # Update module summary
        self.modules_online_label.setText(f"{status.modules_online} / {status.modules_total}")
        self.modules_fault_label.setText(str(status.modules_fault))

        # Update system alarm
        if status.system_alarm != 0:
            from models.charger_status import get_module_alarm_text
            alarm_text = get_module_alarm_text(status.system_alarm, self.viewmodel.driver_type)
            self.system_alarm_label.setText(alarm_text)
            self.system_alarm_label.setStyleSheet("color: red;")
        else:
            self.system_alarm_label.setText("None")
            self.system_alarm_label.setStyleSheet("color: green;")

        # Update module cards
        for i, card in enumerate(self.module_cards):
            if i < status.modules_total:
                card.setVisible(True)
                # Get module status from status.modules if available
                if hasattr(status, 'modules') and i < len(status.modules):
                    mod = status.modules[i]
                    card.update_status(
                        mod.voltage, mod.current, mod.temp_dcdc,
                        mod.state, mod.online
                    )
                else:
                    # Placeholder if no module data
                    card.update_status(0, 0, 0, 0, False)
            else:
                card.setVisible(False)

        # Update realtime chart
        if self.realtime_chart:
            self.realtime_chart.add_data(status.vout, status.iout_total)

    def _on_bms_updated(self, status):
        """Handle BMS status updated"""
        # State
        state_name = BmsState.NAMES.get(status.state, "Unknown")
        self.bms_state_label.setText(state_name)

        if status.state == BmsState.ONLINE:
            self.bms_state_label.setStyleSheet("color: green; font-weight: bold;")
        elif status.state == BmsState.FAULT:
            self.bms_state_label.setStyleSheet("color: red; font-weight: bold;")
        else:
            self.bms_state_label.setStyleSheet("color: #FF9800; font-weight: bold;")

        # Voltage, Current
        self.bms_voltage_label.setText(f"{status.batt_voltage:.1f} V")
        self.bms_current_label.setText(f"{status.batt_current:.1f} A")

        # SOC, SOH, Capacity
        self.bms_soc_label.setText(f"{status.soc} %")
        self.bms_soh_label.setText(f"{status.soh} %")
        self.bms_cap_label.setText(f"{status.cap_remain:.1f} Ah")

        # Cell voltage, temp
        self.bms_cell_volt_label.setText(f"{status.min_cell_volt}~{status.max_cell_volt} mV")
        self.bms_cell_temp_label.setText(f"{status.min_cell_temp}~{status.max_cell_temp} °C")

        # Relays
        self.bms_chg_relay_label.setText("ON" if status.charge_relay_closed else "OFF")
        self.bms_chg_relay_label.setStyleSheet(
            "color: green; font-weight: bold;" if status.charge_relay_closed else "color: #9E9E9E;"
        )

        self.bms_discharge_relay_label.setText("ON" if status.discharge_relay_closed else "OFF")
        self.bms_discharge_relay_label.setStyleSheet(
            "color: green; font-weight: bold;" if status.discharge_relay_closed else "color: #9E9E9E;"
        )

        # Alarm
        from models.charger_status import get_bms_alarm_text
        alarm_text = get_bms_alarm_text(status.alarm_flags)
        self.bms_alarm_label.setText(alarm_text)

        if status.alarm_flags != 0:
            self.bms_alarm_label.setStyleSheet("color: red;")
        else:
            self.bms_alarm_label.setStyleSheet("color: green;")

        # Request
        self.bms_request_label.setText(
            f"V: {status.chg_volt_request:.1f} V, I: {status.chg_curr_request:.1f} A"
        )

    def _on_config_updated(self, config):
        """Handle config updated from device"""
        self._set_config_to_ui(config)
        self.viewmodel._log("Config loaded from device", "INFO")

    def _on_log_added(self, entry):
        """Handle new log entry"""
        # Add to table
        row = self.log_table.rowCount()
        self.log_table.insertRow(row)

        # Time
        time_item = QTableWidgetItem(entry.timestamp.strftime("%H:%M:%S"))
        self.log_table.setItem(row, 0, time_item)

        # Level
        level_item = QTableWidgetItem(entry.level)
        if entry.level == "ERROR":
            level_item.setBackground(QColor("#FFCDD2"))
        elif entry.level == "WARN":
            level_item.setBackground(QColor("#FFE0B2"))
        self.log_table.setItem(row, 1, level_item)

        # Message
        msg_item = QTableWidgetItem(entry.message)
        self.log_table.setItem(row, 2, msg_item)

        # Scroll to bottom
        self.log_table.scrollToBottom()

        # Apply filter
        self._apply_log_filter()

    def _apply_log_filter(self):
        """Apply log filter"""
        filter_level = self.log_filter_combo.currentText()

        for row in range(self.log_table.rowCount()):
            if filter_level == "All":
                self.log_table.showRow(row)
            else:
                item = self.log_table.item(row, 1)
                if item and item.text() == filter_level:
                    self.log_table.showRow(row)
                else:
                    self.log_table.hideRow(row)

    def _on_error(self, message: str):
        """Handle error"""
        QMessageBox.critical(self, "Error", message)

    def _on_set_driver(self):
        """Set driver type"""
        driver_ids = [1, 2, 3]  # Maxwell, Lianming, TonHe
        driver_id = driver_ids[self.driver_combo.currentIndex()]
        self.viewmodel.cmd_set_driver(driver_id)

    def _on_set_module_count(self):
        """Set module count"""
        count = self.module_count_spin.value()
        self.viewmodel.cmd_set_module_count(count)

    def _on_start(self):
        """Start charging"""
        self.viewmodel.cmd_start()

    def _on_stop(self):
        """Stop charging"""
        self.viewmodel.cmd_stop()

    def _on_emergency_stop(self):
        """Emergency stop"""
        reply = QMessageBox.question(
            self, "Confirm",
            "Bạn có chắc muốn DỪNG KHẨN CẤP?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        if reply == QMessageBox.StandardButton.Yes:
            self.viewmodel.cmd_emergency_stop()

    def _on_load_config(self):
        """Load config from file"""
        from PyQt5.QtWidgets import QFileDialog
        from models.charger_config import ChargerConfig

        filepath, _ = QFileDialog.getOpenFileName(
            self, "Load Config", "", "Config Files (*.bin);;All Files (*)"
        )
        if not filepath:
            return

        try:
            with open(filepath, 'rb') as f:
                data = f.read()

            config = ChargerConfig.from_bytes(data)

            # Update UI
            self.battery_config.set_config(config.battery)
            self.cell_config.set_config(config.cell)
            self.soc_config.set_config(config.soc)
            self.temp_config.set_config(config.temp)
            self.protect_config.set_config(config.protect)
            self.module_config.set_config(config.module)

            self.viewmodel._log(f"Loaded config from {filepath}", "INFO")
            QMessageBox.information(self, "Success", f"Loaded config from {filepath}")

        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to load config: {e}")

    def _on_save_config(self):
        """Save config to file"""
        from PyQt5.QtWidgets import QFileDialog

        filepath, _ = QFileDialog.getSaveFileName(
            self, "Save Config", "charger_config.bin", "Config Files (*.bin);;All Files (*)"
        )
        if not filepath:
            return

        try:
            # Get config from UI
            config = self._get_config_from_ui()

            # Save to file
            with open(filepath, 'wb') as f:
                f.write(config.to_bytes())

            self.viewmodel._log(f"Saved config to {filepath}", "INFO")
            QMessageBox.information(self, "Success", f"Saved config to {filepath}")

        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to save config: {e}")

    def _on_read_config(self):
        """Read config from device"""
        self.viewmodel.cmd_read_config()

    def _on_write_config(self):
        """Write config to device"""
        config = self._get_config_from_ui()
        self.viewmodel.cmd_write_config(config)

    def _get_config_from_ui(self):
        """Get config from UI widgets"""
        from models.charger_config import ChargerConfig

        config = ChargerConfig()
        config.battery = self.battery_config.get_config()
        config.cell = self.cell_config.get_config()
        config.soc = self.soc_config.get_config()
        config.temp = self.temp_config.get_config()
        config.protect = self.protect_config.get_config()
        config.module = self.module_config.get_config()
        return config

    def _set_config_to_ui(self, config):
        """Set config to UI widgets"""
        self.battery_config.set_config(config.battery)
        self.cell_config.set_config(config.cell)
        self.soc_config.set_config(config.soc)
        self.temp_config.set_config(config.temp)
        self.protect_config.set_config(config.protect)
        self.module_config.set_config(config.module)

    def _on_export_log(self):
        """Export log to CSV"""
        from PyQt5.QtWidgets import QFileDialog
        filepath, _ = QFileDialog.getSaveFileName(
            self, "Export Log", "charger_log.csv", "CSV Files (*.csv)"
        )
        if filepath:
            self.viewmodel.export_logs_csv(filepath)
            QMessageBox.information(self, "Success", f"Log exported to {filepath}")

    def _on_clear_log(self):
        """Clear log"""
        self.log_table.setRowCount(0)
        self.viewmodel.clear_logs()

    def _on_about(self):
        """Show about dialog"""
        QMessageBox.about(
            self, "About",
            "SacPin Charger Control\n"
            "Version 1.0.0\n\n"
            "PC App for STM32F407 Charger Controller\n"
            "Supports Maxwell, Lianming, and TONHE modules"
        )

    def closeEvent(self, event):
        """Handle close event"""
        if self.viewmodel.connected:
            self.viewmodel.disconnect()
        event.accept()


def main():
    """Entry point"""
    # Setup logging
    logging.basicConfig(
        level=logging.DEBUG,
        format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
    )

    # Create app
    app = QApplication(sys.argv)
    app.setStyle("Fusion")

    # Create and show window
    window = MainWindow()
    window.show()

    # Run
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
