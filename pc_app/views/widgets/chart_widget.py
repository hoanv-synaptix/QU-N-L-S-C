"""
Real-time Chart Widget using PyQtGraph
"""

import pyqtgraph as pg
from pyqtgraph.Qt import QtGui
from collections import deque
from PyQt5.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton
from PyQt5.QtCore import Qt


class RealtimeChart(QWidget):
    """Widget hiển thị biểu đồ realtime"""

    def __init__(self, max_points: int = 200, parent=None):
        super().__init__(parent)
        self.max_points = max_points

        # Data buffers
        self.time_data = deque(maxlen=max_points)
        self.voltage_data = deque(maxlen=max_points)
        self.current_data = deque(maxlen=max_points)

        self._setup_ui()
        self._init_data()

    def _setup_ui(self):
        layout = QVBoxLayout(self)

        # Title
        title = QLabel("Điện áp & Dòng sạc theo thời gian")
        title.setStyleSheet("font-weight: bold;")
        layout.addWidget(title)

        # Chart
        self.plot_widget = pg.PlotWidget()
        self.plot_widget.setBackground('w')  # White background
        self.plot_widget.setLabel('left', 'Giá trị')
        self.plot_widget.setLabel('bottom', 'Thời gian (s)')
        self.plot_widget.showGrid(x=True, y=True, alpha=0.3)
        self.plot_widget.setYRange(0, 100)

        # Add legend
        self.plot_widget.addLegend()

        # Voltage curve (blue)
        self.voltage_curve = self.plot_widget.plot(
            [], [],
            pen=pg.mkPen(color='#2196F3', width=2),
            name='Voltage (V)'
        )

        # Current curve (red)
        self.current_curve = self.plot_widget.plot(
            [], [],
            pen=pg.mkPen(color='#F44336', width=2),
            name='Current (A)'
        )

        layout.addWidget(self.plot_widget)

        # Controls
        controls = QHBoxLayout()
        controls.addStretch()

        self.clear_btn = QPushButton("Clear")
        self.clear_btn.clicked.connect(self.clear)
        controls.addWidget(self.clear_btn)

        layout.addLayout(controls)

    def _init_data(self):
        """Initialize with empty data"""
        for i in range(self.max_points):
            self.time_data.append(i - self.max_points)
            self.voltage_data.append(0)
            self.current_data.append(0)

    def add_data(self, voltage: float, current: float):
        """Thêm 1 điểm dữ liệu mới"""
        # Add time point
        if len(self.time_data) > 0:
            last_time = self.time_data[-1]
        else:
            last_time = 0

        self.time_data.append(last_time + 1)
        self.voltage_data.append(voltage)
        self.current_data.append(current)

        # Update curves
        self.voltage_curve.setData(list(self.time_data), list(self.voltage_data))
        self.current_curve.setData(list(self.time_data), list(self.current_data))

        # Auto-scale Y axis
        if len(self.voltage_data) > 0:
            v_max = max(self.voltage_data) * 1.2
            i_max = max(self.current_data) * 1.2
            y_max = max(v_max, i_max, 10)  # At least 10
            self.plot_widget.setYRange(0, y_max)

    def clear(self):
        """Clear chart data"""
        self._init_data()
        self.voltage_curve.setData([], [])
        self.current_curve.setData([], [])


class MiniChart(QWidget):
    """Mini chart nhỏ cho 1 giá trị"""

    def __init__(self, label: str, color: str = '#2196F3', max_points: int = 50, parent=None):
        super().__init__(parent)
        self.label = label
        self.color = color
        self.max_points = max_points
        self.data = deque(maxlen=max_points)

        self._setup_ui()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        # Label
        self.label_widget = QLabel(self.label)
        self.label_widget.setStyleSheet("font-size: 10px;")
        layout.addWidget(self.label_widget)

        # Chart
        self.plot_widget = pg.PlotWidget()
        self.plot_widget.setBackground('w')
        self.plot_widget.setFixedHeight(60)
        self.plot_widget.showGrid(x=True, y=True, alpha=0.3)
        self.plot_widget.hideButtons()

        self.curve = self.plot_widget.plot(
            [], [],
            pen=pg.mkPen(color=self.color, width=1.5)
        )

        layout.addWidget(self.plot_widget)

    def add_data(self, value: float):
        """Thêm điểm dữ liệu"""
        self.data.append(value)
        self.curve.setData(list(self.data))

        # Auto-scale
        if len(self.data) > 1:
            y_max = max(self.data) * 1.2
            self.plot_widget.setYRange(0, max(y_max, 1))
