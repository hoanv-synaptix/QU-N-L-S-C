"""
Config Tab Widgets - Input widgets for configuration
"""

from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QGroupBox,
    QLabel, QLineEdit, QSpinBox, QDoubleSpinBox, QCheckBox,
    QComboBox, QPushButton, QScrollArea, QFrame
)
from PyQt5.QtCore import Qt

from models.charger_config import (
    BattConfig, CellConfig, SocConfig, TempConfig,
    ProtectConfig, ModuleConfig, DriverType
)


class ConfigInputMixin:
    """Mixin cung cấp helper methods cho config inputs"""

    def create_voltage_input(self, min_val: float, max_val: float, default: float, step: float = 0.1):
        """Tạo input cho điện áp (V)"""
        spin = QDoubleSpinBox()
        spin.setRange(min_val, max_val)
        spin.setValue(default)
        spin.setDecimals(1)
        spin.setSingleStep(step)
        spin.setSuffix(" V")
        return spin

    def create_current_input(self, min_val: float, max_val: float, default: float, step: float = 0.1):
        """Tạo input cho dòng điện (A)"""
        spin = QDoubleSpinBox()
        spin.setRange(min_val, max_val)
        spin.setValue(default)
        spin.setDecimals(1)
        spin.setSingleStep(step)
        spin.setSuffix(" A")
        return spin

    def create_current_ma_input(self, min_val: int, max_val: int, default: int):
        """Tạo input cho dòng điện (mA)"""
        spin = QSpinBox()
        spin.setRange(min_val, max_val)
        spin.setValue(default)
        spin.setSuffix(" mA")
        return spin

    def create_voltage_mv_input(self, min_val: int, max_val: int, default: int):
        """Tạo input cho điện áp (mV)"""
        spin = QSpinBox()
        spin.setRange(min_val, max_val)
        spin.setValue(default)
        spin.setSuffix(" mV")
        return spin

    def create_temp_input(self, min_val: int, max_val: int, default: int):
        """Tạo input cho nhiệt độ (°C)"""
        spin = QSpinBox()
        spin.setRange(min_val, max_val)
        spin.setValue(default)
        spin.setSuffix(" °C")
        return spin

    def create_percent_input(self, min_val: int, max_val: int, default: int):
        """Tạo input cho phần trăm (%)"""
        spin = QSpinBox()
        spin.setRange(min_val, max_val)
        spin.setValue(default)
        spin.setSuffix(" %")
        return spin

    def create_capacity_input(self, min_val: int, max_val: int, default: int):
        """Tạo input cho dung lượng (Ah)"""
        spin = QSpinBox()
        spin.setRange(min_val, max_val)
        spin.setValue(default)
        spin.setSuffix(" Ah")
        return spin


class BatteryConfigWidget(QGroupBox, ConfigInputMixin):
    """Widget cấu hình ắc quy"""

    def __init__(self, parent=None):
        super().__init__("Cấu hình ắc quy", parent)
        self._setup_ui()

    def _setup_ui(self):
        layout = QGridLayout(self)

        # Row 0
        layout.addWidget(QLabel("Dung lượng:"), 0, 0)
        self.capacity_input = self.create_capacity_input(10, 5000, 500)
        layout.addWidget(self.capacity_input, 0, 1)

        layout.addWidget(QLabel("Nhiệt độ giới hạn:"), 0, 2)
        self.temp_limit_input = self.create_temp_input(0, 80, 45)
        layout.addWidget(self.temp_limit_input, 0, 3)

        # Row 1 - Currents
        layout.addWidget(QLabel("I Min (A):"), 1, 0)
        self.i_min_input = self.create_current_input(0, 200, 5, 0.5)
        layout.addWidget(self.i_min_input, 1, 1)

        layout.addWidget(QLabel("I Max (A):"), 1, 2)
        self.i_max_input = self.create_current_input(0, 500, 50, 1.0)
        layout.addWidget(self.i_max_input, 1, 3)

        # Row 2
        layout.addWidget(QLabel("I Pre-charge (A):"), 2, 0)
        self.i_pre_input = self.create_current_input(0, 100, 10, 0.5)
        layout.addWidget(self.i_pre_input, 2, 1)

        layout.addWidget(QLabel("I Low (A):"), 2, 2)
        self.i_low_input = self.create_current_input(0, 100, 5, 0.5)
        layout.addWidget(self.i_low_input, 2, 3)

        # Row 3 - Voltages
        layout.addWidget(QLabel("V Min (V):"), 3, 0)
        self.v_min_input = self.create_voltage_input(0, 100, 42.0)
        layout.addWidget(self.v_min_input, 3, 1)

        layout.addWidget(QLabel("V Max (V):"), 3, 2)
        self.v_max_input = self.create_voltage_input(0, 100, 58.8)
        layout.addWidget(self.v_max_input, 3, 3)

        # Row 4
        layout.addWidget(QLabel("V Pre-charge (V):"), 4, 0)
        self.v_pre_input = self.create_voltage_input(0, 100, 30.0)
        layout.addWidget(self.v_pre_input, 4, 1)

        layout.addWidget(QLabel("V Low (V):"), 4, 2)
        self.v_low_input = self.create_voltage_input(0, 100, 42.0)
        layout.addWidget(self.v_low_input, 4, 3)

    def get_config(self) -> BattConfig:
        """Lấy config từ inputs"""
        return BattConfig(
            capacity=int(self.capacity_input.value() * 10),  # ×10 for storage
            i_min=int(self.i_min_input.value() * 10),
            i_max=int(self.i_max_input.value() * 10),
            i_pre=int(self.i_pre_input.value() * 10),
            i_low=int(self.i_low_input.value() * 10),
            v_min=int(self.v_min_input.value() * 10),
            v_max=int(self.v_max_input.value() * 10),
            v_pre=int(self.v_pre_input.value() * 10),
            v_low=int(self.v_low_input.value() * 10),
            temp_limit=self.temp_limit_input.value()
        )

    def set_config(self, cfg: BattConfig):
        """Set inputs từ config"""
        self.capacity_input.setValue(cfg.capacity / 10.0)
        self.i_min_input.setValue(cfg.i_min / 10.0)
        self.i_max_input.setValue(cfg.i_max / 10.0)
        self.i_pre_input.setValue(cfg.i_pre / 10.0)
        self.i_low_input.setValue(cfg.i_low / 10.0)
        self.v_min_input.setValue(cfg.v_min / 10.0)
        self.v_max_input.setValue(cfg.v_max / 10.0)
        self.v_pre_input.setValue(cfg.v_pre / 10.0)
        self.v_low_input.setValue(cfg.v_low / 10.0)
        self.temp_limit_input.setValue(cfg.temp_limit)


class CellConfigWidget(QGroupBox, ConfigInputMixin):
    """Widget cấu hình sạc theo cell"""

    def __init__(self, parent=None):
        super().__init__("Cấu hình sạc theo điện áp cell", parent)
        self._setup_ui()

    def _setup_ui(self):
        layout = QGridLayout(self)

        # Enable checkbox
        self.enable_checkbox = QCheckBox("Bật chế độ sạc theo cell")
        layout.addWidget(self.enable_checkbox, 0, 0, 1, 4)

        # Row 1 - Voltages
        layout.addWidget(QLabel("V1 (mV):"), 1, 0)
        self.v1_input = self.create_voltage_mv_input(0, 5000, 3200)
        layout.addWidget(self.v1_input, 1, 1)

        layout.addWidget(QLabel("V2 (mV):"), 1, 2)
        self.v2_input = self.create_voltage_mv_input(0, 5000, 3400)
        layout.addWidget(self.v2_input, 1, 3)

        # Row 2
        layout.addWidget(QLabel("V3 (mV):"), 2, 0)
        self.v3_input = self.create_voltage_mv_input(0, 5000, 3500)
        layout.addWidget(self.v3_input, 2, 1)

        layout.addWidget(QLabel("Delta V (mV):"), 2, 2)
        self.delta_volt_input = self.create_voltage_mv_input(0, 1000, 50)
        layout.addWidget(self.delta_volt_input, 2, 3)

        # Row 3 - Currents
        layout.addWidget(QLabel("I1 (A):"), 3, 0)
        self.i1_input = self.create_current_input(0, 500, 100, 1)
        layout.addWidget(self.i1_input, 3, 1)

        layout.addWidget(QLabel("I2 (A):"), 3, 2)
        self.i2_input = self.create_current_input(0, 500, 50, 1)
        layout.addWidget(self.i2_input, 3, 3)

        layout.addWidget(QLabel("I3 (A):"), 4, 0)
        self.i3_input = self.create_current_input(0, 500, 10, 1)
        layout.addWidget(self.i3_input, 4, 1)

    def get_config(self) -> CellConfig:
        return CellConfig(
            is_enable=self.enable_checkbox.isChecked(),
            v1=self.v1_input.value(),
            v2=self.v2_input.value(),
            v3=self.v3_input.value(),
            i1=self.i1_input.value(),
            i2=self.i2_input.value(),
            i3=self.i3_input.value(),
            delta_volt=self.delta_volt_input.value()
        )

    def set_config(self, cfg: CellConfig):
        self.enable_checkbox.setChecked(cfg.is_enable)
        self.v1_input.setValue(cfg.v1)
        self.v2_input.setValue(cfg.v2)
        self.v3_input.setValue(cfg.v3)
        self.i1_input.setValue(cfg.i1)
        self.i2_input.setValue(cfg.i2)
        self.i3_input.setValue(cfg.i3)
        self.delta_volt_input.setValue(cfg.delta_volt)


class SocConfigWidget(QGroupBox, ConfigInputMixin):
    """Widget cấu hình sạc theo SOC"""

    def __init__(self, parent=None):
        super().__init__("Cấu hình sạc theo SOC", parent)
        self._setup_ui()

    def _setup_ui(self):
        layout = QGridLayout(self)

        # Enable checkbox
        self.enable_checkbox = QCheckBox("Bật chế độ sạc theo SOC")
        layout.addWidget(self.enable_checkbox, 0, 0, 1, 4)

        # Row 1 - SOC thresholds
        layout.addWidget(QLabel("SOC1 (%):"), 1, 0)
        self.soc1_input = self.create_percent_input(0, 100, 80)
        layout.addWidget(self.soc1_input, 1, 1)

        layout.addWidget(QLabel("SOC2 (%):"), 1, 2)
        self.soc2_input = self.create_percent_input(0, 100, 90)
        layout.addWidget(self.soc2_input, 1, 3)

        # Row 2
        layout.addWidget(QLabel("SOC3 (%):"), 2, 0)
        self.soc3_input = self.create_percent_input(0, 100, 95)
        layout.addWidget(self.soc3_input, 2, 1)

        layout.addWidget(QLabel("Delta SOC (%):"), 2, 2)
        self.delta_soc_input = self.create_percent_input(0, 20, 2)
        layout.addWidget(self.delta_soc_input, 2, 3)

        # Row 3 - Currents
        layout.addWidget(QLabel("I1 (A):"), 3, 0)
        self.i1_input = self.create_current_input(0, 500, 100, 1)
        layout.addWidget(self.i1_input, 3, 1)

        layout.addWidget(QLabel("I2 (A):"), 3, 2)
        self.i2_input = self.create_current_input(0, 500, 50, 1)
        layout.addWidget(self.i2_input, 3, 3)

        # Row 4
        layout.addWidget(QLabel("I3 (A):"), 4, 0)
        self.i3_input = self.create_current_input(0, 500, 10, 1)
        layout.addWidget(self.i3_input, 4, 1)

        layout.addWidget(QLabel("SOC Charge3 (%):"), 4, 2)
        self.soc_charge3_input = self.create_percent_input(0, 100, 95)
        layout.addWidget(self.soc_charge3_input, 4, 3)

        layout.addWidget(QLabel("I Charge3 (A):"), 5, 0)
        self.i_charge3_input = self.create_current_input(0, 500, 10, 1)
        layout.addWidget(self.i_charge3_input, 5, 1)

    def get_config(self) -> SocConfig:
        return SocConfig(
            is_enable=self.enable_checkbox.isChecked(),
            soc1=self.soc1_input.value(),
            soc2=self.soc2_input.value(),
            soc3=self.soc3_input.value(),
            i1=self.i1_input.value(),
            i2=self.i2_input.value(),
            i3=self.i3_input.value(),
            delta_soc=self.delta_soc_input.value(),
            soc_charge3=self.soc_charge3_input.value(),
            i_charge3=self.i_charge3_input.value()
        )

    def set_config(self, cfg: SocConfig):
        self.enable_checkbox.setChecked(cfg.is_enable)
        self.soc1_input.setValue(cfg.soc1)
        self.soc2_input.setValue(cfg.soc2)
        self.soc3_input.setValue(cfg.soc3)
        self.i1_input.setValue(cfg.i1)
        self.i2_input.setValue(cfg.i2)
        self.i3_input.setValue(cfg.i3)
        self.delta_soc_input.setValue(cfg.delta_soc)
        self.soc_charge3_input.setValue(cfg.soc_charge3)
        self.i_charge3_input.setValue(cfg.i_charge3)


class TempConfigWidget(QGroupBox, ConfigInputMixin):
    """Widget cấu hình sạc theo nhiệt độ"""

    def __init__(self, parent=None):
        super().__init__("Cấu hình sạc theo nhiệt độ", parent)
        self._setup_ui()

    def _setup_ui(self):
        layout = QGridLayout(self)

        # Enable checkbox
        self.enable_checkbox = QCheckBox("Bật chế độ sạc theo nhiệt độ")
        layout.addWidget(self.enable_checkbox, 0, 0, 1, 4)

        # Row 1 - Temperature thresholds
        layout.addWidget(QLabel("Temp1 (°C):"), 1, 0)
        self.temp1_input = self.create_temp_input(-40, 80, 0)
        layout.addWidget(self.temp1_input, 1, 1)

        layout.addWidget(QLabel("Temp2 (°C):"), 1, 2)
        self.temp2_input = self.create_temp_input(-40, 80, 10)
        layout.addWidget(self.temp2_input, 1, 3)

        # Row 2
        layout.addWidget(QLabel("Temp3 (°C):"), 2, 0)
        self.temp3_input = self.create_temp_input(-40, 80, 35)
        layout.addWidget(self.temp3_input, 2, 1)

        layout.addWidget(QLabel("Temp4 (°C):"), 2, 2)
        self.temp4_input = self.create_temp_input(-40, 80, 40)
        layout.addWidget(self.temp4_input, 2, 3)

        layout.addWidget(QLabel("Temp5 (°C):"), 3, 0)
        self.temp5_input = self.create_temp_input(-40, 80, 45)
        layout.addWidget(self.temp5_input, 3, 1)

        layout.addWidget(QLabel("Delta Temp (°C):"), 3, 2)
        self.delta_temp_input = self.create_temp_input(0, 20, 5)
        layout.addWidget(self.delta_temp_input, 3, 3)

        # Row 3 - Currents
        layout.addWidget(QLabel("I1 (A):"), 4, 0)
        self.i1_input = self.create_current_input(0, 500, 100, 1)
        layout.addWidget(self.i1_input, 4, 1)

        layout.addWidget(QLabel("I2 (A):"), 4, 2)
        self.i2_input = self.create_current_input(0, 500, 80, 1)
        layout.addWidget(self.i2_input, 4, 3)

        # Row 4
        layout.addWidget(QLabel("I3 (A):"), 5, 0)
        self.i3_input = self.create_current_input(0, 500, 50, 1)
        layout.addWidget(self.i3_input, 5, 1)

        layout.addWidget(QLabel("I4 (A):"), 5, 2)
        self.i4_input = self.create_current_input(0, 500, 20, 1)
        layout.addWidget(self.i4_input, 5, 3)

    def get_config(self) -> TempConfig:
        return TempConfig(
            is_enable=self.enable_checkbox.isChecked(),
            temp1=self.temp1_input.value(),
            temp2=self.temp2_input.value(),
            temp3=self.temp3_input.value(),
            temp4=self.temp4_input.value(),
            temp5=self.temp5_input.value(),
            i1=self.i1_input.value(),
            i2=self.i2_input.value(),
            i3=self.i3_input.value(),
            i4=self.i4_input.value(),
            delta_temp=self.delta_temp_input.value()
        )

    def set_config(self, cfg: TempConfig):
        self.enable_checkbox.setChecked(cfg.is_enable)
        self.temp1_input.setValue(cfg.temp1)
        self.temp2_input.setValue(cfg.temp2)
        self.temp3_input.setValue(cfg.temp3)
        self.temp4_input.setValue(cfg.temp4)
        self.temp5_input.setValue(cfg.temp5)
        self.i1_input.setValue(cfg.i1)
        self.i2_input.setValue(cfg.i2)
        self.i3_input.setValue(cfg.i3)
        self.i4_input.setValue(cfg.i4)
        self.delta_temp_input.setValue(cfg.delta_temp)


class ProtectConfigWidget(QGroupBox, ConfigInputMixin):
    """Widget cấu hình bảo vệ"""

    def __init__(self, parent=None):
        super().__init__("Cấu hình bảo vệ", parent)
        self._setup_ui()

    def _setup_ui(self):
        layout = QGridLayout(self)

        # Enable checkboxes
        self.cell_protect_checkbox = QCheckBox("Bật bảo vệ chênh áp cell")
        layout.addWidget(self.cell_protect_checkbox, 0, 0, 1, 2)

        self.jack_protect_checkbox = QCheckBox("Bật bảo vệ nhiệt jack cắm")
        layout.addWidget(self.jack_protect_checkbox, 0, 2, 1, 2)

        # Row 1 - Delays
        layout.addWidget(QLabel("Delay Cell (s):"), 1, 0)
        self.delay_cell_input = QSpinBox()
        self.delay_cell_input.setRange(0, 300)
        self.delay_cell_input.setValue(30)
        self.delay_cell_input.setSuffix(" s")
        layout.addWidget(self.delay_cell_input, 1, 1)

        layout.addWidget(QLabel("Delay Jack (s):"), 1, 2)
        self.delay_jack_input = QSpinBox()
        self.delay_jack_input.setRange(0, 300)
        self.delay_jack_input.setValue(10)
        self.delay_jack_input.setSuffix(" s")
        layout.addWidget(self.delay_jack_input, 1, 3)

        # Row 2 - Delta
        layout.addWidget(QLabel("Delta Cell Volt (mV):"), 2, 0)
        self.delta_cell_volt_input = self.create_voltage_mv_input(0, 1000, 100)
        layout.addWidget(self.delta_cell_volt_input, 2, 1)

    def get_config(self) -> ProtectConfig:
        return ProtectConfig(
            is_cell_protect=self.cell_protect_checkbox.isChecked(),
            is_jack_protect=self.jack_protect_checkbox.isChecked(),
            delay_cell=self.delay_cell_input.value(),
            delay_jack=self.delay_jack_input.value(),
            delta_cell_volt=self.delta_cell_volt_input.value()
        )

    def set_config(self, cfg: ProtectConfig):
        self.cell_protect_checkbox.setChecked(cfg.is_cell_protect)
        self.jack_protect_checkbox.setChecked(cfg.is_jack_protect)
        self.delay_cell_input.setValue(cfg.delay_cell)
        self.delay_jack_input.setValue(cfg.delay_jack)
        self.delta_cell_volt_input.setValue(cfg.delta_cell_volt)


class ModuleConfigWidget(QGroupBox, ConfigInputMixin):
    """Widget cấu hình module sạc"""

    def __init__(self, parent=None):
        super().__init__("Cấu hình Module sạc", parent)
        self._setup_ui()

    def _setup_ui(self):
        layout = QGridLayout(self)

        # Driver type
        layout.addWidget(QLabel("Loại Driver:"), 0, 0)
        self.driver_combo = QComboBox()
        self.driver_combo.addItems([
            "Maxwell MXR",
            "LIANMING",
            "TONHE V1.3"
        ])
        self.driver_combo.setCurrentIndex(0)
        layout.addWidget(self.driver_combo, 0, 1)

        # Module count
        layout.addWidget(QLabel("Số Module:"), 0, 2)
        self.module_count_input = QSpinBox()
        self.module_count_input.setRange(1, 8)
        self.module_count_input.setValue(1)
        layout.addWidget(self.module_count_input, 0, 3)

        # Base address
        layout.addWidget(QLabel("Base Address:"), 1, 0)
        self.base_addr_input = QSpinBox()
        self.base_addr_input.setRange(0, 63)
        self.base_addr_input.setValue(0)
        layout.addWidget(self.base_addr_input, 1, 1)

        layout.addWidget(QLabel("Group:"), 1, 2)
        self.base_group_input = QSpinBox()
        self.base_group_input.setRange(0, 1)
        self.base_group_input.setValue(0)
        layout.addWidget(self.base_group_input, 1, 3)

        # Float settings
        layout.addWidget(QLabel("V Float (V):"), 2, 0)
        self.v_float_input = self.create_voltage_input(0, 100, 54.6)
        layout.addWidget(self.v_float_input, 2, 1)

        layout.addWidget(QLabel("I Float (A):"), 2, 2)
        self.i_float_input = self.create_current_input(0, 100, 5.0)
        layout.addWidget(self.i_float_input, 2, 3)

    def get_config(self) -> ModuleConfig:
        driver_ids = [DriverType.MAXWELL, DriverType.LIANMING, DriverType.TONHE]
        return ModuleConfig(
            driver_id=driver_ids[self.driver_combo.currentIndex()],
            module_count=self.module_count_input.value(),
            base_addr=self.base_addr_input.value(),
            base_group=self.base_group_input.value(),
            v_float=int(self.v_float_input.value() * 10),
            i_float=int(self.i_float_input.value() * 10)
        )

    def set_config(self, cfg: ModuleConfig):
        idx = 0
        if cfg.driver_id == DriverType.LIANMING:
            idx = 1
        elif cfg.driver_id == DriverType.TONHE:
            idx = 2
        self.driver_combo.setCurrentIndex(idx)
        self.module_count_input.setValue(cfg.module_count)
        self.base_addr_input.setValue(cfg.base_addr)
        self.base_group_input.setValue(cfg.base_group)
        self.v_float_input.setValue(cfg.v_float / 10.0)
        self.i_float_input.setValue(cfg.i_float / 10.0)
