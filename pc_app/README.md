# SacPin Charger Control - PC Application

> **Version:** 1.0.0
> **Framework:** Python 3 + PyQt5
> **Communication:** USB CDC (Virtual COM Port) - Binary Protocol
> **Compatible Firmware:** STM32F407 + FreeRTOS

---

## 1. Giới thiệu

SacPin Charger Control là phần mềm PC dùng để giám sát và điều khiển bộ sạc pin dựa trên firmware STM32F407. Ứng dụng được phát triển như một bản nâng cấp từ app gốc `TronKhiApp`, với các tính năng mở rộng:

- Hỗ trợ nhiều module sạc song song (Maxwell, Lianming, TonHe)
- BMS tích hợp trực tiếp trong firmware
- Giao diện hiện đại với PyQt5
- Biểu đồ realtime
- Lưu/cấu hình từ file

---

## 2. Cài đặt

### 2.1 Yêu cầu

- **Python 3.8+**
- **Windows 10/11** hoặc **Linux/Mac**

### 2.2 Cài đặt dependencies

```bash
cd pc_app
pip install -r requirements.txt
```

### 2.3 Chạy ứng dụng

```bash
python main.py
```

### 2.4 Build thành file .exe

```bash
# Windows
build_exe.bat

# Linux/Mac
chmod +x build.sh
./build.sh
```

File exe sẽ được tạo tại: `dist/SacPinChargerControl.exe`

---

## 3. Kiến trúc

```
pc_app/
├── main.py                      # Entry point
├── requirements.txt             # Dependencies
├── SPEC.md                    # Đặc tả chi tiết
│
├── models/                    # Data models
│   ├── charger_config.py      # Cấu hình (Batt, Cell, SOC, Temp, Module)
│   └── charger_status.py      # Trạng thái (System, BMS, Enums)
│
├── services/                  # Core services
│   ├── serial_service.py      # USB CDC communication
│   └── protocol_handler.py    # Binary protocol parser
│
├── viewmodels/               # Business logic
│   └── main_viewmodel.py     # Main ViewModel
│
└── views/                    # UI Layer
    ├── main_window.py         # Main window
    └── widgets/
        ├── config_widgets.py  # Config input widgets
        └── chart_widget.py    # Real-time chart
```

---

## 4. Giao diện

### 4.1 Tab Monitor

Tab giám sát hiển thị:
- **Kết nối**: COM port, driver type, số module
- **Trạng thái bộ sạc**: Điện áp, dòng, SOC, nhiệt độ
- **BMS**: Điện áp, dòng, SOC, SOH, cell voltage/temp, relay status
- **Module cards**: Danh sách các module sạc với trạng thái
- **Đồ thị realtime**: Voltage và Current theo thời gian
- **Điều khiển**: START, STOP, EMERGENCY STOP

### 4.2 Tab Config

Tab cấu hình với các widget:
- **Battery Config**: Dung lượng, dòng min/max, điện áp, nhiệt độ giới hạn
- **Cell Config**: Cấu hình sạc theo điện áp cell
- **SOC Config**: Cấu hình sạc theo SOC
- **Temp Config**: Cấu hình sạc theo nhiệt độ
- **Protect Config**: Bảo vệ chênh áp cell, bảo vệ nhiệt jack cắm
- **Module Config**: Loại driver, số module, địa chỉ

Các nút chức năng:
- **Đọc từ Device**: Đọc cấu hình hiện tại từ thiết bị
- **Ghi xuống Device**: Ghi cấu hình xuống thiết bị
- **Load từ File**: Load cấu hình từ file .bin
- **Save ra File**: Lưu cấu hình ra file .bin

### 4.3 Tab Log

Tab log hiển thị:
- Danh sách các sự kiện với timestamp
- Lọc theo level (INFO, WARN, ERROR)
- Clear log
- Export ra file CSV

---

## 5. Protocol

### 5.1 Frame Format

```
┌──────┬──────┬──────┬─────────┬──────────┬──────┐
│ SOF1 │ SOF2 │ CMD  │ LEN     │ PAYLOAD  │ CRC8 │
│ 0xAA │ 0x55 │ 1B   │ 1B      │ LEN byte │ 1B   │
└──────┴──────┴──────┴─────────┴──────────┴──────┘
```

- **SOF**: Start of Frame (0xAA 0x55)
- **CMD**: Command code
- **LEN**: Payload length (0-255)
- **PAYLOAD**: Data (little-endian)
- **CRC8**: Checksum

### 5.2 Commands (PC → STM32)

| CMD | Tên | Payload | Mô tả |
|-----|-----|---------|--------|
| 0x01 | SET_VOLTAGE | float (4B) | Đặt điện áp output |
| 0x02 | SET_CURRENT | float (4B) | Đặt dòng giới hạn |
| 0x03 | START | - | Bắt đầu sạc |
| 0x04 | STOP | - | Dừng sạc (an toàn) |
| 0x08 | EMERGENCY_STOP | - | Dừng khẩn cấp |
| 0x09 | SET_DRIVER | uint8 | Chọn driver |
| 0x10 | READ_CONFIG | uint8 section | Đọc 1 section config |
| 0x11 | WRITE_CONFIG | section + data | Ghi 1 section config |
| 0x12 | READ_ALL_CONFIG | - | Đọc toàn bộ config |
| 0x13 | WRITE_ALL_CONFIG | data | Ghi toàn bộ config |
| 0x14 | SET_MODULE_COUNT | uint8 | Số module song song |

### 5.3 Responses (STM32 → PC)

| CMD | Tên | Payload | Mô tả |
|-----|-----|---------|--------|
| 0x81 | RSP_STATUS | SystemStatus (40B) | Status định kỳ |
| 0x82 | RSP_ACK | uint8 cmd | Command OK |
| 0x83 | RSP_NACK | cmd + error | Lỗi |
| 0x84 | RSP_PONG | uint32 version | PING response |
| 0x90 | RSP_CONFIG | section + data | Config data |
| 0x91 | RSP_BMS_STATUS | BmsStatus (32B) | BMS status |

---

## 6. Data Models

### 6.1 Config Sections

| Section | ID | Kích thước |
|---------|-----|------------|
| Battery | 0x01 | 20 bytes |
| Cell | 0x02 | 15 bytes |
| SOC | 0x03 | 17 bytes |
| Temp | 0x04 | 20 bytes |
| Protect | 0x05 | 7 bytes |
| Module | 0x06 | 8 bytes |
| CAN | 0x07 | 8 bytes |

### 6.2 Enums

**Charge Status:**
- 0: Stop
- 1: NoBattery
- 2: HasBattery
- 3: CheckBattery
- 4: ChargingCC
- 5: BatteryFull
- 6: WaitOverTemp
- 7-15: Various errors

**Driver Types:**
- 1: Maxwell MXR
- 2: Lianming
- 3: TonHe V1.3

---

## 7. STOP vs EMERGENCY STOP

| Lệnh | Hành vi | Use case |
|------|----------|----------|
| **STOP** | Tắt output theo sequence an toàn, tắt relay đầu ra một cách bình thường | Sạc xong, tạm dừng |
| **EMERGENCY STOP** | Cắt relay ngay lập tức, không qua quy trình | Quá dòng, quá nhiệt, lỗi nghiêm trọng |

---

## 8. Xử lý số liệu

> **Quy tắc:** Firmware dùng số nguyên (fixed-point) để tiết kiệm RAM/CPU. **App PC tự động chuyển đổi** - người dùng chỉ thấy giá trị thực.

| Kiểu | Format | Ví dụ | Hiển thị |
|------|--------|--------|-----------|
| Voltage | uint16_t | `546` | `54.6 V` |
| Current | uint16_t | `125` | `12.5 A` |
| Capacity | uint16_t | `5000` | `500 Ah` |
| Cell Voltage | uint16_t (mV) | `3450` | `3450 mV` |
| Temperature | int8_t (°C) | `28` | `28 °C` |
| Percentage | uint8_t (%) | `78` | `78%` |

---

## 9. Troubleshooting

### 9.1 Không kết nối được

1. Kiểm tra USB đã cắm vào máy tính
2. Kiểm tra driver USB CDC đã cài đặt (Virtual COM Port)
3. Thử cổng COM khác
4. Kiểm tra baudrate (115200)

### 9.2 Module offline

1. Kiểm tra đấu nối CAN bus
2. Kiểm tra CAN baudrate đúng (125Kbps cho module sạc)
3. Kiểm tra địa chỉ module

### 9.3 BMS offline

1. Kiểm tra CAN2 (250Kbps) kết nối với BMS
2. Kiểm tra dây CAN bus

---

## 10. File Format

### 10.1 Config File (.bin)

File cấu hình là binary file chứa toàn bộ cấu hình theo thứ tự:

```
[Battery 20B][Cell 15B][SOC 17B][Temp 20B][Protect 7B][Module 8B][CAN 8B]
= 95 bytes total
```

### 10.2 Log File (.csv)

```
Time,Level,Message
2024-01-01 10:23:45,INFO,Connected to COM3
2024-01-01 10:23:46,INFO,BMS online: V=52.3V, I=15.2A, SOC=78%
```

---

## 11. Tham khảo

- App gốc: TronKhiApp v1.0.11 (PkgCharger.exe)
- Protocol spec: `firmware/App/Demo/pc_protocol.h`
- Demo app cũ: `pc_app/charger_demo.py`
- Firmware architecture: Xem `CLAUDE.md`

---

## 12. License

Copyright © 2024 SacPin Team
