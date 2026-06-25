# SPEC.md — SacPin PC Application (Clone from TronKhiApp)

> **Tham khảo:** TronKhiApp v1.0.11 (PkgCharger.exe decompiled)
> **Framework:** Python 3 + PyQt6
> **Giao tiếp:** USB CDC (Virtual COM Port), Binary Protocol
> **Tương thích firmware:** STM32F407 + FreeRTOS

---

## 1. Mục tiêu & Phạm vi

Clone app gốc TronKhiApp với tính năng mở rộng:
- Giữ nguyên logic chính: giám sát, điều khiển, cấu hình
- Thay Modbus RTU → USB CDC Binary Protocol
- Thêm hỗ trợ nhiều module song song (thay vì 1 rectifier)
- BMS tích hợp (thay vì SmartCharger window riêng)
- MVVVM architecture, không code-behind thuần

**Không cần:**
- SmartCharger window riêng (BMS đã trong firmware)
- PSoC bootloader (STM32 có bootloader riêng)
- Auto-update từ server

---

## 2. Kiến trúc

```
pc_app/
├── main.py                     # Entry point
├── protocol.py                  # Protocol definitions (shared)
├── models/
│   ├── __init__.py
│   ├── charger_config.py        # Cấu hình bộ sạc (Battery, Cell, SOC, Temp, Protect, Module)
│   ├── charger_status.py       # Trạng thái realtime (từ device)
│   ├── bms_status.py            # Trạng thái BMS (từ CAN2)
│   └── module_status.py         # Trạng thái từng module sạc
├── services/
│   ├── __init__.py
│   ├── serial_service.py         # USB CDC connect/disconnect, send/receive
│   ├── protocol_handler.py       # Frame parse, state machine, command queue
│   └── config_service.py         # Read/write config to device
├── viewmodels/
│   ├── __init__.py
│   ├── main_viewmodel.py         # MainWindow logic
│   ├── monitor_viewmodel.py      # Monitor panel logic
│   ├── config_viewmodel.py       # Config panel logic
│   └── chart_viewmodel.py        # Chart data management
└── views/
    ├── __init__.py
    ├── main_window.py            # MainWindow (chứa 3 tabs: Monitor, Config, Log)
    ├── monitor_page.py           # Tab giám sát realtime
    ├── config_page.py             # Tab cấu hình
    ├── log_page.py               # Tab log lỗi
    └── widgets/
        ├── __init__.py
        ├── module_card.py        # Card hiển thị 1 module sạc
        ├── bms_panel.py          # Panel BMS info
        ├── chart_widget.py       # Real-time chart (PyQtGraph)
        └── connection_panel.py   # COM port + connect button
```

---

## 3. Protocol mở rộng (PC ↔ STM32)

### 3.0 Data Encoding Conventions

> **Quy tắc xử lý số liệu:** Firmware dùng số nguyên (fixed-point) để tiết kiệm RAM/CPU. **App PC tự động chuyển đổi** — người dùng chỉ thấy giá trị thực, không cần biết đến format bên trong.

| Kiểu | Format trong struct | Ví dụ giá trị | Hiển thị trên UI | Người dùng nhập |
|------|---------------------|----------------|-------------------|------------------|
| Voltage | uint16_t | `546` | `54.6 V` | `54.6` |
| Current | uint16_t | `125` | `12.5 A` | `12.5` |
| Capacity | uint16_t | `5000` | `500.0 Ah` | `500` |
| Cell Voltage | uint16_t (mV) | `3450` | `3450 mV` | `3450` |
| Temperature | int8_t (°C) | `28` | `28 °C` | `28` |
| Percentage | uint8_t (%) | `78` | `78%` | `78` |

**Nguyên tắc xử lý trong App PC:**
- **Hiển thị**: Nhận `125` → Hiển thị `12.5 A` (tự động chia 10 với V, I, Ah)
- **Gửi xuống firmware**: Người dùng nhập `12.5` → Gửi `125` (tự động nhân 10)
- **Chỉ có mV và °C và % là giữ nguyên không chia**

### 3.1 Frame Format (giữ nguyên)

```
┌──────┬──────┬──────┬─────────┬──────────┬──────┐
│ SOF1 │ SOF2 │ CMD  │ LEN     │ PAYLOAD  │ CRC8 │
│ 0xAA │ 0x55 │ 1B   │ 1B      │ LEN byte │ 1B   │
└──────┴──────┴──────┴─────────┴──────────┴──────┘
```

### 3.2 Commands (PC → STM32)

| CMD | Tên | Payload | Mô tả |
|-----|-----|---------|--------|
| 0x01 | SET_VOLTAGE | float (4B) | Đặt điện áp output |
| 0x02 | SET_CURRENT | float (4B) | Đặt dòng giới hạn |
| 0x03 | START | - | Bắt đầu sạc |
| 0x04 | STOP | - | Dừng sạc |
| 0x05 | SET_MODULE_ADDR | uint8 addr + uint8 group | Đặt địa chỉ module |
| 0x06 | PING | - | Kiểm tra kết nối |
| 0x07 | READ_REG | uint16 reg | Đọc 1 register |
| 0x08 | EMERGENCY_STOP | - | Dừng khẩn cấp |
| 0x09 | SET_DRIVER | uint8 driver_id | Chọn driver (1=Maxwell, 2=Lianming, 3=TonHe) |
| **0x10** | **READ_CONFIG** | uint8 section | **Đọc 1 section config** |
| **0x11** | **WRITE_CONFIG** | uint8 section + data | **Ghi 1 section config** |
| **0x12** | **READ_ALL_CONFIG** | - | **Đọc toàn bộ config** |
| **0x13** | **WRITE_ALL_CONFIG** | data | **Ghi toàn bộ config** |
| **0x14** | **SET_MODULE_COUNT** | uint8 count | **Số module song song (1-8)** |

### 3.3 Responses (STM32 → PC)

| CMD | Tên | Payload | Mô tả |
|-----|-----|---------|--------|
| 0x81 | RSP_STATUS | SystemStatus_t (40B) | Status report định kỳ |
| 0x82 | RSP_ACK | uint8 cmd | Command nhận OK |
| 0x83 | RSP_NACK | uint8 cmd + uint8 error | Lỗi |
| 0x84 | RSP_PONG | uint32 fw_version | PING response |
| 0x85 | RSP_REG_VALUE | uint16 reg + value | READ_REG response |
| **0x90** | **RSP_CONFIG** | section + data | **Config data** |
| **0x91** | **RSP_BMS_STATUS** | BMS_View_t (32B) | **BMS status từ CAN2** |

### 3.4 Status Report (RSP_STATUS) — 40 bytes

```python
# SystemStatus_t (packed, little-endian)
struct SystemStatus_t:
    float   batt_voltage       # 4B  Điện áp pin
    float   batt_current      # 4B  Dòng sạc
    uint8_t batt_soc          # 1B  State of Charge (0-100%)
    int8_t  batt_temp         # 1B  Nhiệt độ pin (°C)
    float   vout              # 4B  Điện áp output tổng
    float   iout_total        # 4B  Tổng dòng output
    float   temp_charger      # 1B  Nhiệt độ bộ sạc
    uint8_t charge_status     # 1B  EChargerBattMode enum
    uint8_t modules_online    # 1B  Số module đang online
    uint8_t modules_fault     # 1B  Số module đang lỗi
    uint8_t modules_total     # 1B  Tổng số module
    uint32_t alarm_flags     # 4B  Alarm bits (BMS)
    uint32_t system_alarm     # 4B  System alarm (charger)
    uint8_t charging          # 1B  1=đang sạc
    uint8_t btn_start         # 1B  Nút start được nhấn
    uint8_t btn_stop          # 1B  Nút stop được nhấn
    uint8_t sd_status         # 1B  0=không có SD, 1=OK, 2=lỗi
```

### 3.5 BMS Status (RSP_BMS_STATUS) — 32 bytes

```python
# BMS_View_t (packed, little-endian)
struct BMS_View_t:
    uint8_t  state             # 1B  0=OFFLINE, 1=ONLINE, 2=FAULT
    uint8_t  online            # 1B  1=đang respond
    float    batt_voltage      # 4B  Điện áp pin (V)
    float    batt_current      # 4B  Dòng (A, âm=discharge)
    uint8_t  soc               # 1B  State of Charge (0-100%)
    float    cap_remain         # 4B  Dung lượng còn lại (Ah)
    uint8_t  soh               # 1B  State of Health (0-100%)
    uint16_t max_cell_volt     # 2B  Cell voltage max (mV)
    uint16_t min_cell_volt     # 2B  Cell voltage min (mV)
    int8_t   max_cell_temp     # 1B  Nhiệt độ max (°C)
    int8_t   min_cell_temp     # 1B  Nhiệt độ min (°C)
    uint8_t  charge_relay_closed # 1B  1=relay sạc đóng
    uint8_t  discharge_relay_closed # 1B  1=relay xả đóng
    uint32_t alarm_flags       # 4B  BMS_ALARM_* bitmask
    float    chg_volt_request  # 4B  Voltage request từ BMS
    float    chg_curr_request  # 4B  Current request từ BMS
```

### 3.6 Config Sections

| Section | ID | Mô tả | Kích thước |
|---------|-----|-------|-----------|
| BATTERY | 0x01 | Cấu hình ắc quy chính | 20 bytes |
| CELL | 0x02 | Cấu hình sạc theo điện áp cell | 15 bytes |
| SOC | 0x03 | Cấu hình sạc theo SOC | 17 bytes |
| TEMP | 0x04 | Cấu hình sạc theo nhiệt độ | 16 bytes |
| PROTECT | 0x05 | Cấu hình bảo vệ (jack, cell) | 7 bytes |
| MODULE | 0x06 | Cấu hình module (type, addr, count) | 8 bytes |
| CAN | 0x07 | Cấu hình CAN (bitrate, filter) | 8 bytes |

### 3.7 Battery Config (Section 0x01) — 20 bytes

```python
# BattConfig_t (packed)
struct BattConfig_t:
    uint16_t capacity          # 2B  Dung lượng ắc quy (Ah)
    uint16_t i_min            # 2B  Dòng sạc tối thiểu (A)
    uint16_t i_max            # 2B  Dòng sạc tối đa (A)
    uint16_t i_pre            # 2B  Dòng pre-charge (A)
    uint16_t i_low            # 2B  Dòng sạc yếu (A)
    uint16_t v_min            # 2B  Điện áp min (V)
    uint16_t v_max            # 2B  Điện áp max (V)
    uint16_t v_pre            # 2B  Điện áp pre-charge (V)
    uint16_t v_low            # 2B  Điện áp sạc yếu (V)
    int8_t   temp_limit       # 1B  Nhiệt độ giới hạn (°C)
    uint8_t  reserved         # 1B
```

### 3.8 Cell Config (Section 0x02) — 15 bytes

```python
# CellConfig_t (packed)
struct CellConfig_t:
    uint8_t  is_enable         # 1B  1=bật sạc theo cell
    uint16_t v1               # 2B  Điện áp ngưỡng 1 (mV)
    uint16_t v2               # 2B  Điện áp ngưỡng 2 (mV)
    uint16_t v3               # 2B  Điện áp ngưỡng 3 (mV)
    uint16_t i1               # 2B  Dòng giai đoạn 1 (A)
    uint16_t i2               # 2B  Dòng giai đoạn 2 (A)
    uint16_t i3               # 2B  Dòng giai đoạn 3 (A)
    uint16_t delta_volt       # 2B  Chênh áp giữa các cell (mV)
```

### 3.9 SOC Config (Section 0x03) — 17 bytes

```python
# SocConfig_t (packed)
struct SocConfig_t:
    uint8_t  is_enable         # 1B  1=bật sạc theo SOC
    uint8_t  soc1              # 1B  Ngưỡng SOC 1 (%)
    uint8_t  soc2              # 1B  Ngưỡng SOC 2 (%)
    uint8_t  soc3              # 1B  Ngưỡng SOC 3 (%)
    uint16_t i1               # 2B  Dòng giai đoạn 1 (A)
    uint16_t i2               # 2B  Dòng giai đoạn 2 (A)
    uint16_t i3               # 2B  Dòng giai đoạn 3 (A)
    uint8_t  delta_soc        # 1B  Delta SOC (%)
    uint8_t  soc_charge3      # 1B  SOC bắt đầu giai đoạn 3 (%)
    uint16_t i_charge3        # 2B  Dòng giai đoạn 3 (A)
    uint8_t  reserved[3]      # 3B
```

### 3.10 Temp Config (Section 0x04) — 16 bytes

```python
# TempConfig_t (packed)
struct TempConfig_t:
    uint8_t  is_enable         # 1B  1=bật sạc theo nhiệt độ
    int8_t   temp1             # 1B  Ngưỡng nhiệt 1 (°C)
    int8_t   temp2             # 1B  Ngưỡng nhiệt 2 (°C)
    int8_t   temp3             # 1B  Ngưỡng nhiệt 3 (°C)
    int8_t   temp4             # 1B  Ngưỡng nhiệt 4 (°C)
    int8_t   temp5             # 1B  Ngưỡng nhiệt 5 (°C)
    uint16_t i1               # 2B  Dòng giai đoạn 1 (A)
    uint16_t i2               # 2B  Dòng giai đoạn 2 (A)
    uint16_t i3               # 2B  Dòng giai đoạn 3 (A)
    uint16_t i4               # 2B  Dòng giai đoạn 4 (A)
    int8_t   delta_temp       # 1B  Delta nhiệt độ (°C)
    uint8_t  reserved[1]      # 1B
```

### 3.11 Protect Config (Section 0x05) — 7 bytes

```python
# ProtectConfig_t (packed)
struct ProtectConfig_t:
    uint8_t  is_cell_protect   # 1B  Bật bảo vệ chênh áp cell
    uint8_t  is_jack_protect   # 1B  Bật bảo vệ nhiệt jack cắm
    uint16_t delay_cell        # 2B  Delay bảo vệ cell (s)
    uint16_t delay_jack        # 2B  Delay bảo vệ jack (s)
    uint16_t delta_cell_volt   # 2B  Ngưỡng chênh áp cell (mV)
```

### 3.12 Module Config (Section 0x06) — 8 bytes

```python
# ModuleConfig_t (packed)
struct ModuleConfig_t:
    uint8_t  driver_id         # 1B  1=Maxwell, 2=Lianming, 3=TonHe
    uint8_t  module_count     # 1B  Số module song song (1-8)
    uint8_t  base_addr         # 1B  Địa chỉ base của module đầu tiên
    uint8_t  base_group        # 1B  Group của module
    uint16_t v_float           # 2B  Điện áp float (V)
    uint16_t i_float           # 2B  Dòng float (A)
```

---

## 4. UI Design

### 4.1 MainWindow Layout

```
┌─────────────────────────────────────────────────────────────┐
│  SacPin Charger Control v1.0        [Connection: COM3 ●]   │
├─────────────────────────────────────────────────────────────┤
│  [ Monitor ]  [ Config ]  [ Log ]                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  (Tab content based on selected tab)                        │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 4.2 Monitor Tab Layout

```
┌──────────────────────────────────────────────────────────────┐
│ Connection Panel: [COM Port ▼] [Refresh] [Connect ●]       │
├────────────────────────┬───────────────────────────────────┤
│ Charger Status          │ BMS Status                        │
│ ┌────────────────────┐  │ ┌───────────────────────────────┐  │
│ │ Batt: 52.3V  15.2A │  │ │ V: 52.3V  I: 15.2A           │  │
│ │ SOC: 78%  Temp: 28°C│ │ │ SOC: 78%  SOH: 95%           │  │
│ │ Status: Charging CC│  │ │ Cell: 3280~3450mV  32°C      │  │
│ │ [START] [STOP]     │  │ │ Relay: Chg● Dis●             │  │
│ └────────────────────┘  │ │ Alarm: None                  │  │
│                         │ └───────────────────────────────┘  │
│ Module List (1-8 cards)  │                                  │
│ ┌────────┐ ┌────────┐    │ Real-time Chart                   │
│ │ Mod #1 │ │ Mod #2 │    │ ┌───────────────────────────────┐  │
│ │ 54.6V  │ │ 54.6V  │    │ │ V (blue) / I (red) vs Time    │  │
│ │ 10.2A  │ │ 10.1A  │    │ │                               │  │
│ │ 35°C ● │ │ 34°C ● │    │ └───────────────────────────────┘  │
│ └────────┘ └────────┘    │                                  │
├──────────────────────────┴──────────────────────────────────┤
│ SD Card: ● OK  |  FW: v1.2.3  |  Driver: Maxwell MXR       │
└──────────────────────────────────────────────────────────────┘
```

### 4.3 Config Tab Layout

```
┌──────────────────────────────────────────────────────────────┐
│ [Read from Device]  [Write to Device]  [Save to File] [Load]│
├────────────────────────┬───────────────────────────────────┤
│ Battery Config         │ Cell Config                       │
│ Capacity: [500] Ah     │ ☑ Enable                          │
│ I_min:   [5.0] A       │ V1: [3200] mV  I1: [100]%         │
│ I_max:   [50.0] A     │ V2: [3400] mV  I2: [50]%          │
│ V_min:   [42.0] V     │ V3: [3500] mV  I3: [10]%          │
│ V_max:   [58.8] V     │ Delta: [50] mV                    │
│ Temp Limit: [45] °C   │                                    │
├────────────────────────┼───────────────────────────────────┤
│ SOC Config             │ Temp Config                       │
│ ☑ Enable               │ ☑ Enable                          │
│ SOC1: [80]%  I1: [100]%│ T1: [0]°C  I1: [100]%           │
│ SOC2: [90]%  I2: [50]%│ T2: [10]°C  I2: [80]%            │
│ SOC3: [95]%  I3: [10]%│ T3: [35]°C  I3: [50]%            │
│ Delta: [2]%           │ T4: [40]°C  I4: [20]%            │
├────────────────────────┼───────────────────────────────────┤
│ Module Config          │ Protection Config                 │
│ Driver: [Maxwell ▼]   │ ☑ Cell Protect  Delay: [30] s    │
│ Count: [2] modules    │ ☑ Jack Protect  Delay: [10] s    │
│ Base Addr: [0]        │ Delta Cell: [100] mV              │
│ V Float: [54.6] V     │                                    │
│ I Float: [5.0] A      │                                    │
└──────────────────────────────────────────────────────────────┘
```

### 4.4 Log Tab Layout

```
┌──────────────────────────────────────────────────────────────┐
│ Filter: [All ▼]  [Clear Log]  [Export CSV]                   │
├──────────────────────────────────────────────────────────────┤
│ Time       │ Type   │ Message                                │
│ 10:23:45   │ INFO   │ Connected to COM3                      │
│ 10:23:46   │ INFO   │ BMS online: V=52.3V, I=15.2A, SOC=78%  │
│ 10:24:12   │ WARN   │ Module #2 offline                      │
│ 10:25:00   │ ERROR  │ BMS alarm: Cell overvoltage            │
│ 10:26:33   │ INFO   │ Charging started                       │
│ ...        │ ...    │ ...                                    │
└──────────────────────────────────────────────────────────────┘
```

### 4.5 Color Scheme

| Element | Color | Meaning |
|---------|-------|---------|
| Online/OK | `#4CAF50` (Green) | Module đang hoạt động, không lỗi |
| Offline/Warning | `#FF9800` (Orange) | Module offline, cảnh báo |
| Error/Fault | `#F44336` (Red) | Lỗi, alarm |
| Charging Active | `#2196F3` (Blue) | Đang sạc |
| Background | `#F5F5F5` (Light Gray) | Nền chính |
| Card Background | `#FFFFFF` (White) | Thẻ module, panel |
| Text Primary | `#212121` (Dark Gray) | Text chính |
| Text Secondary | `#757575` (Medium Gray) | Text phụ, label |

---

## 5. Enums

### 5.1 Charge Status (EChargerBattMode)

| Value | Tên | Màu |
|-------|-----|-----|
| 0 | Stop | Gray |
| 1 | NoBattery | Orange |
| 2 | HasBattery | Gray |
| 3 | CheckBattery | Yellow |
| 4 | ChargingCC | Blue |
| 5 | BatteryFull | Green |
| 6 | WaitOverTemp | Orange |
| 7 | ErrorBoard | Red |
| 8 | ErrorCell | Red |
| 9 | ErrorJack | Red |
| 10 | PreCharge | Yellow |
| 11 | ChargerParamError | Red |
| 12 | BmsProhibit | Red |
| 13 | ChargingCV | Blue |
| 14 | ChargerOverOutputVoltage | Red |
| 15 | BattLostCapacity | Orange |

### 5.2 Module State (EModuleState)

| Value | Tên | Icon |
|-------|-----|------|
| 0 | IDLE | ⚪ Gray |
| 1 | STARTING | 🟡 Yellow |
| 2 | RUNNING | 🔵 Blue |
| 3 | OFFLINE | 🟠 Orange |
| 4 | FAULT | 🔴 Red |
| 5 | RECOVERING | 🟡 Yellow |

### 5.3 Driver Types (EDriverType)

| Value | Tên |
|-------|-----|
| 0 | None |
| 1 | Maxwell MXR |
| 2 | Lianming |
| 3 | TonHe V1.3 |

---

## 6. Data Flow

```
┌──────────────────────────────────────────────────────────────┐
│                         PC Application                       │
│                                                              │
│  USB CDC ──► SerialService ──► ProtocolHandler              │
│              ▲                       │                      │
│              │                       ▼                      │
│         Response              ┌──────────────────┐           │
│         callbacks            │ SystemStatus_t   │           │
│                               │ BMS_View_t       │           │
│                               │ Config data      │           │
│                               └────────┬─────────┘           │
│                                        │                      │
│                                        ▼                      │
│                               ┌──────────────────┐           │
│                               │  ViewModels      │           │
│                               │  (Qt Signals)     │           │
│                               └────────┬─────────┘           │
│                                        │                      │
│                                        ▼                      │
│                               ┌──────────────────┐           │
│                               │  Views (PyQt6)   │           │
│                               │  UI Updates      │           │
│                               └──────────────────┘           │
└──────────────────────────────────────────────────────────────┘
                              │
                              │ USB CDC
                              ▼
┌──────────────────────────────────────────────────────────────┐
│                         STM32 Firmware                       │
│                                                              │
│  CAN1 (125Kbps) ◄─── Maxwell/Lianming/TonHe modules         │
│  CAN2 (250Kbps) ◄─── BMS                                     │
│  USB CDC ─────────► PC Protocol Handler                      │
│                                                              │
│  App Layer: app_charger.c (start/stop, buttons, LEDs)        │
│  Charger Core: charger_core.c (module management)           │
│  BMS Core: bms_core.c (BMS CAN driver)                      │
│  PC Protocol: pc_protocol.c (USB CDC ↔ commands)            │
└──────────────────────────────────────────────────────────────┘
```

---

## 7. Error Handling

### 7.1 Connection Errors
- **Port not found**: Show dialog "Port not available"
- **Port in use**: Show dialog "Port already in use by another application"
- **Timeout**: Show warning "Device not responding", retry 3 times then show error
- **CRC error**: Log warning, request retransmit

### 7.2 Protocol Errors (NACK)
- Log error with command code and error description
- Show toast notification for critical errors

### 7.3 BMS Errors
- BMS offline (>5s no response): Show warning, update BMS panel with "OFFLINE"
- BMS alarm: Show alarm details, change BMS panel border to red

### 7.4 Module Errors
- Module offline: Update module card with orange border
- Module fault: Update module card with red border, show alarm text
- All modules offline: Disable START button, show error

---

## 8. Acceptance Criteria

### 8.1 Connection
- [ ] App detects available COM ports on startup
- [ ] App can connect/disconnect without error
- [ ] Connection status indicator updates correctly
- [ ] Auto-reconnect on unexpected disconnect (optional)

### 8.2 Monitor Tab
- [ ] Real-time status updates every 200ms
- [ ] All status fields display correctly
- [ ] START/STOP buttons work
- [ ] Module cards update for each module
- [ ] BMS panel shows BMS data
- [ ] Real-time chart updates smoothly

### 8.3 Config Tab
- [ ] Read config from device works
- [ ] Write config to device works
- [ ] All config sections display correctly
- [ ] Validation prevents invalid values
- [ ] Save/Load config to/from file works

### 8.4 Log Tab
- [ ] All events are logged with timestamp
- [ ] Filter by type works
- [ ] Clear log works
- [ ] Export to CSV works

### 8.5 Error Handling
- [ ] Connection errors show appropriate messages
- [ ] Protocol errors are logged
- [ ] BMS offline is detected and displayed
- [ ] Module offline/fault is detected and displayed

---

## 9. Implementation Phases

### Phase 1: Foundation (Protocol + Connection)
- Protocol definitions
- Serial service
- Protocol handler
- Basic UI with connection panel
- PING/PONG connection test

### Phase 2: Monitor Tab
- SystemStatus parsing
- BMS_View parsing
- Monitor page UI
- START/STOP commands
- Module cards
- BMS panel

### Phase 3: Config Tab
- Config models
- Read/Write config commands
- Config page UI
- Validation

### Phase 4: Chart + Log
- Real-time chart widget
- Log service
- Log page UI
- Export CSV

### Phase 5: Polish
- Error handling improvements
- Performance optimization
- Documentation
- Build executable

---

## 10. Dependencies

```
pyserial>=3.5
PyQt6>=6.5
pyqtgraph>=0.13
PyInstaller>=6.0  (for building .exe)
```

---

## 11. File Naming Convention

| Type | Convention | Example |
|------|-----------|---------|
| Python modules | snake_case.py | serial_service.py |
| Classes | PascalCase | ChargerConfig |
| Functions | snake_case | parse_status |
| Constants | SCREAMING_SNAKE_CASE | CMD_START |
| Qt widgets | PascalCase | MonitorPage |
| ViewModels | PascalCase | MonitorViewModel |

---

## 12. References

- App gốc: TronKhiApp v1.0.11 (PkgCharger.exe decompiled)
- Protocol spec: firmware/App/Demo/pc_protocol.h
- Demo app: pc_app/charger_demo.py
- Firmware architecture: CLAUDE.md Section 2
- BMS CAN spec: CLAUDE.md Section 14
