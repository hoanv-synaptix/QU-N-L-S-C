# Dự Án: Mạch Điều Khiển Sạc Pin Theo Chu Trình

## 1. Mục đích dự án

Thiết kế và lập trình firmware + PC app cho một bộ điều khiển sạc pin công nghiệp. Bộ điều khiển này đóng vai trò trung gian giữa:

- Module sạc công suất (Maxwell MXR Series) — thiết bị phần cứng biến đổi AC thành DC
- Hệ thống quản lý pin (BMS) — cung cấp thông số pin: SOC, nhiệt độ, điện áp, dòng điện
- Người vận hành — thông qua màn hình HMI (DWIN) và phần mềm PC

Bộ điều khiển nhận thông số pin từ BMS, nhận cài đặt chu trình sạc từ PC App, rồi điều khiển module sạc Maxwell tương ứng. Nó cũng thực hiện các chức năng bảo vệ (quá áp, quá nhiệt, mất liên lạc) và hiển thị trạng thái.

## 2. Kiến trúc tổng thể

```
┌─────────────┐    USB CDC     ┌───────────────────────────────┐     CAN Bus      ┌──────────────────┐
│   PC App    │◄──────────────►│      STM32F407 Controller     │◄────────────────►│  Maxwell MXR     │
│  (Python)   │   Virtual COM   │                               │   CAN1 125Kbps   │  Module sạc      │
└─────────────┘                │  ┌───────────┐ ┌───────────┐  │                   └──────────────────┘
                               │  │pc_protocol│ │  maxwell   │  │     CAN Bus      ┌──────────────────┐
┌─────────────┐    RS485       │  │           │ │  _charger  │  │◄────────────────►│  BMS Pin         │
│  Màn hình   │◄──────────────►│  └───────────┘ └───────────┘  │   CAN2 (TODO)    │  (CAN protocol)  │
│  DWIN HMI   │                │                               │                   └──────────────────┘
└─────────────┘                └───────────────────────────────┘
                                        │
                                        ├── 4x NTC10k (đo nhiệt độ jack sạc)
                                        ├── 3x Relay NO (đóng/ngắt mạch sạc)
                                        ├── 2x Nút nhấn (Start/Stop vật lý)
                                        ├── 2x LED 12V (xanh=sạc, đỏ=lỗi)
                                        ├── RTC (thời gian thực)
                                        ├── microSD (lưu log)
                                        └── Flash 64Mb (cài đặt, firmware backup)
```

## 3. Phần cứng

### 3.1 MCU: STM32F407VET6

Board demo hiện tại: **STM32F407VET6 "black board"** (xem schematic: `stm32f407vet6_black_sch.pdf`).

Thông số quan trọng:
- CPU: ARM Cortex-M4 @ 168MHz
- Flash: 512KB, RAM: 192KB
- 2x CAN controller native (CAN1, CAN2)
- USB OTG FS (dùng làm CDC Virtual COM)
- Crystal: 8MHz HSE, 32.768kHz LSE (cho RTC)

### 3.2 Pinout đã xác nhận

| Ngoại vi      | Pin           | Ghi chú                                    |
|---------------|---------------|---------------------------------------------|
| CAN1 RX       | PB8           | Remap (vì PA11/PA12 dành cho USB)          |
| CAN1 TX       | PB9           | Qua transceiver SN65HVD230                |
| USB OTG FS D- | PA11          | Connector J4 mini USB                      |
| USB OTG FS D+ | PA12          | Có BAT54C + 22Ω series resistor           |
| USART1 TX     | PA9           | Header ISP_COM (J6) — debug UART          |
| USART1 RX     | PA10          | 115200 baud                                |
| LED Green     | PD12 (tạm)   | Trạng thái sạc (cần xác nhận trên PCB thực)|
| LED Red       | PD13 (tạm)   | Trạng thái lỗi                            |

### 3.3 Yêu cầu phần cứng sản phẩm (chưa hoàn thành)

- Nguồn nuôi: 24VAC (từ biến áp 380V-24VAC) hoặc 12VDC/5A từ module
- 2x cổng CAN cách ly, tốc độ < 1Mbps, có lọc nhiễu
- 3x relay dry-contact NO (5A/24VDC, 100.000 lần đóng cắt)
- 4x đầu vào NTC10k (sai số ±1°C)
- 2x nút nhấn input
- 2x LED 12V 50mA
- RS485 + nguồn 12VDC/1A cho màn hình DWIN
- MicroSD slot
- Flash ngoại 64Mb (W25Q16 trên board demo)
- Cổng IOT card (dự phòng)
- Chống nhiễu, chân tiếp địa ra vỏ sạc

## 4. Giao thức truyền thông

### 4.1 CAN Bus — Maxwell MXR Module (CAN1)

**Chuẩn:** CAN 2.0B Extended Frame (29-bit ID), 125 Kbps

**Cấu trúc Frame ID (29-bit):**

```
Bit:  28───────20  19   18─17   16────────9   8─────────1   0
      ┌─────────┐  ┌─┐  ┌───┐  ┌──────────┐  ┌──────────┐  ┌─┐
      │ PROTNO  │  │P│  │RSV│  │ DST_ADDR │  │ SRC_ADDR │  │G│
      │ (9 bit) │  │T│  │   │  │ (8 bit)  │  │ (8 bit)  │  │R│
      └─────────┘  │P│  └───┘  └──────────┘  └──────────┘  │P│
                   └─┘                                       └─┘

- PROTNO = 0x060 (mặc định Maxwell)
- PTP: 1 = point-to-point, 0 = broadcast
- DST_ADDR: địa chỉ đích (module: 0x00~0x3F, broadcast: 0xFF)
- SRC_ADDR: địa chỉ nguồn (controller cố định = 0xF0)
- GRP: group bit (LSB)
```

**Gửi lệnh cài đặt (function code 0x03):**

```
Byte:  0     1     2-3          4-7
      ┌────┬────┬────────────┬──────────────────┐
      │0x03│0x00│Register No.│ Data (float/int) │
      └────┴────┴────────────┴──────────────────┘
```

**Gửi lệnh đọc (function code 0x10):**

```
Byte:  0     1     2-3          4-7
      ┌────┬────┬────────────┬──────────────────┐
      │0x10│0x00│Register No.│   0x00000000     │
      └────┴────┴────────────┴──────────────────┘
```

**Response từ module:**

```
Byte:  0          1           2-3          4-7
      ┌─────────┬───────────┬────────────┬──────┐
      │Type     │Error code │Register No.│ Data │
      │0x41=flt │0xF0=OK    │            │      │
      │0x42=int │0xF2=Fail  │            │      │
      └─────────┴───────────┴────────────┴──────┘
```

**Các register quan trọng:**

| Register | R/W   | Kiểu  | Mô tả                                  |
|----------|-------|-------|-----------------------------------------|
| 0x0001   | Read  | float | Điện áp đầu ra (V)                     |
| 0x0002   | Read  | float | Dòng điện đầu ra (A)                   |
| 0x0003   | Read  | float | Current limit point (ratio)             |
| 0x0004   | Read  | float | Nhiệt độ board DCDC (°C)               |
| 0x000B   | Read  | float | Nhiệt độ môi trường (°C)               |
| 0x0011   | Read  | float | Công suất định mức (W)                 |
| 0x0012   | Read  | float | Dòng định mức (A)                      |
| 0x0020   | Write | float | Cài công suất đầu ra (W)               |
| 0x0021   | Write | float | Cài điện áp đầu ra (V)                 |
| 0x0022   | Write | float | Cài current limit (ratio 0.0~1.0)      |
| 0x0023   | Write | float | Cài OVP upper limit (V)                |
| 0x0030   | Write | int   | Start=0x00000000, Shutdown=0x00010000  |
| 0x0040   | Read  | int   | Alarm/status bits (xem bảng alarm)     |
| 0x0048   | Read  | int   | Input power (W)                        |

**Alarm bits (register 0x0040):**

| Bit | Ý nghĩa                    | Mức độ    |
|-----|-----------------------------|-----------|
| 0   | Module fault                | Critical  |
| 1   | Module protection           | Warning   |
| 4   | Input mode detection error  | Warning   |
| 6   | Internal SCI failure        | Critical  |
| 8   | DCDC overvoltage            | Critical  |
| 9   | PFC voltage abnormal        | Warning   |
| 14  | AC undervoltage             | Warning   |
| 16  | CAN communication failure   | Warning   |
| 17  | Current imbalance           | Warning   |
| 22  | DCDC on/off (0=On, 1=Off)  | Status    |
| 23  | Module power limit          | Info      |
| 24  | Temperature derating        | Warning   |
| 25  | AC power limit              | Info      |
| 27  | Fan failure                 | Warning   |
| 28  | DCDC short circuit          | Critical  |
| 30  | DCDC over temperature       | Critical  |
| 31  | DCDC output overvoltage     | Critical  |

**Tính toán current limit:**

```
current_limit_ratio = dòng_mong_muốn / dòng_định_mức
VD: muốn 10A, dòng định mức 20A → ratio = 0.5
```

**Dữ liệu số (float/int):** IEEE 754 32-bit, truyền big-endian (MSB first) trên CAN bus.

**Keep-alive quan trọng:** Module Maxwell TỰ ĐỘNG TẮT OUTPUT khi controller ngừng gửi CAN frame. Timeout chính xác chưa xác định (cần test thực tế). Firmware phải poll liên tục (hiện tại mỗi ~100ms) để giữ module sống.

### 4.2 USB CDC Protocol — PC App ↔ STM32

**Kết nối vật lý:** USB OTG FS (J4 mini USB trên board) → Virtual COM Port trên PC, 115200 baud (baud rate không ảnh hưởng vì là USB).

**Frame format:**

```
┌──────┬──────┬──────┬─────────┬──────────────┬──────┐
│ SOF1 │ SOF2 │ CMD  │  LEN   │   PAYLOAD    │ CRC8 │
│ 0xAA │ 0x55 │ 1B   │  1B    │  LEN bytes   │ 1B   │
└──────┴──────┴──────┴─────────┴──────────────┴──────┘

CRC8: polynomial 0x07, tính trên [CMD, LEN, PAYLOAD]
Payload: little-endian (khác với CAN bus là big-endian)
```

**Lệnh từ PC → STM32:**

| CMD  | Tên              | Payload         | Mô tả                        |
|------|------------------|-----------------|-------------------------------|
| 0x01 | SET_VOLTAGE      | float (4B)      | Cài điện áp (V)              |
| 0x02 | SET_CURRENT      | float (4B)      | Cài current limit (ratio)    |
| 0x03 | START            | (none)          | Bật module sạc               |
| 0x04 | STOP             | (none)          | Tắt module sạc               |
| 0x05 | SET_MODULE_ADDR  | u8 addr + u8 grp| Đổi module target            |
| 0x06 | PING             | (none)          | Test kết nối                 |
| 0x07 | READ_REG         | u16 reg         | Đọc register tùy ý           |

**Response từ STM32 → PC:**

| CMD  | Tên        | Payload                           | Mô tả                     |
|------|------------|-----------------------------------|----------------------------|
| 0x81 | STATUS     | PC_StatusReport_t (26 bytes)      | Gửi định kỳ mỗi 200ms    |
| 0x82 | ACK        | u8 cmd                            | Lệnh thực hiện OK          |
| 0x83 | NACK       | u8 cmd + u8 error_code            | Lệnh thất bại             |
| 0x84 | PONG       | u32 fw_version                    | Trả lời PING              |
| 0x85 | REG_VALUE  | u16 reg + data                    | Giá trị register (chưa impl)|

**Status report struct (26 bytes, packed, little-endian):**

```c
typedef struct {
    float    voltage;        // Byte 0-3:   Điện áp đầu ra (V)
    float    current;        // Byte 4-7:   Dòng điện đầu ra (A)
    float    temp_dcdc;      // Byte 8-11:  Nhiệt độ DCDC (°C)
    float    temp_ambient;   // Byte 12-15: Nhiệt độ môi trường (°C)
    uint32_t alarm_status;   // Byte 16-19: Raw alarm bits
    uint32_t input_power;    // Byte 20-23: Công suất vào (W)
    uint8_t  charging;       // Byte 24:    1=đang sạc, 0=off
    uint8_t  module_online;  // Byte 25:    1=module có response
} PC_StatusReport_t;         // Total: 26 bytes
```

### 4.3 CAN Bus — BMS Pin (CAN2) [CHƯA TRIỂN KHAI]

Dự kiến dùng CAN2 của STM32. Cần nhận các thông số:
- SOC (State of Charge)
- Điện áp cell max/min
- Dòng điện pack
- Nhiệt độ pack
- Mã lỗi BMS
- Dung lượng

Giao thức tùy thuộc vào hãng BMS cụ thể (chưa chốt).

## 5. Kiến trúc Firmware

### 5.1 Tổ chức code

```
firmware/
├── App/
│   └── Demo/
│       ├── main_demo.c         # Demo bare-metal: init + superloop test 30s
│       ├── demo_main.c         # Demo với PC App: init + main loop
│       ├── demo_main.h
│       ├── pc_protocol.c       # Protocol handler PC ↔ STM32
│       ├── pc_protocol.h
│       └── pc_link_usb.c       # Glue code: USB CDC ↔ pc_protocol
├── Drivers/
│   ├── BSP/
│   │   ├── bsp_can.c/.h       # CAN abstraction (HAL wrapper)
│   │   └── bsp_uart.c/.h      # UART debug output
│   └── Maxwell/
│       ├── maxwell_charger.c   # Maxwell CAN protocol driver
│       └── maxwell_charger.h
```

### 5.2 Phân lớp (Layer Architecture)

```
┌─────────────────────────────────────────────────────────┐
│  Application Layer                                       │
│  ┌─────────────┐  ┌─────────────┐  ┌────────────────┐  │
│  │ demo_main.c │  │ main_demo.c │  │ (future: RTOS  │  │
│  │ (PC App     │  │ (standalone │  │  charge_task,  │  │
│  │  control)   │  │  CAN test)  │  │  bms_task...)  │  │
│  └──────┬──────┘  └──────┬──────┘  └────────────────┘  │
│         │                 │                              │
├─────────┼─────────────────┼──────────────────────────────┤
│  Protocol / Driver Layer  │                              │
│  ┌──────┴──────┐  ┌──────┴──────┐                       │
│  │pc_protocol  │  │maxwell_     │                       │
│  │(USB frame   │  │charger      │                       │
│  │ parse/build)│  │(CAN frame   │                       │
│  └──────┬──────┘  │ build/parse)│                       │
│         │         └──────┬──────┘                       │
├─────────┼────────────────┼───────────────────────────────┤
│  BSP Layer (Hardware Abstraction)                        │
│  ┌──────┴──────┐  ┌─────┴──────┐  ┌──────────────┐     │
│  │ USB CDC     │  │  bsp_can   │  │  bsp_uart    │     │
│  │ (CubeMX)   │  │  (CAN1)    │  │  (USART1)    │     │
│  └─────────────┘  └────────────┘  └──────────────┘     │
├──────────────────────────────────────────────────────────┤
│  STM32 HAL + CMSIS                                       │
└──────────────────────────────────────────────────────────┘
```

### 5.3 Flow hoạt động (demo hiện tại)

**Mode 1: Standalone CAN test (`main_demo.c`)**
1. Init: Clock 168MHz, UART debug, CAN1 125Kbps
2. Cài đặt: Set voltage → Set current limit → Start module
3. Loop: Poll register mỗi 100ms, in status mỗi 500ms
4. Sau 30s: tự động Stop module, tiếp tục poll

**Mode 2: PC App control (`demo_main.c`)**
1. Init: CAN1, USB CDC, LED, Maxwell driver
2. Main loop thực hiện 5 nhiệm vụ:
   - Poll module mỗi 100ms (keep-alive + đọc V/I/alarm)
   - Kiểm tra offline timeout (1000ms không response → offline)
   - Gửi status report về PC mỗi 200ms
   - Cập nhật LED (xanh=sạc OK, đỏ=alarm/offline)
   - Safety: nếu alarm nghiêm trọng → tự động Stop

**Nhận lệnh từ PC:** USB CDC RX callback → `PC_Protocol_FeedByte()` (state machine parse frame) → `process_frame()` → gọi Maxwell driver API.

**Nhận response từ module:** CAN RX callback → `MXR_ParseResponse()` → `MXR_UpdateStatus()` → cập nhật status struct chung.

### 5.4 Timing quan trọng

| Hoạt động                    | Chu kỳ    | Ghi chú                         |
|------------------------------|-----------|----------------------------------|
| Poll CAN register            | 100 ms    | Giữ module sống + đọc data      |
| Full poll cycle (7 regs)     | ~700 ms   | 7 register × 100ms mỗi request  |
| Status report → PC           | 200 ms    | Update GUI PC                    |
| Offline detection timeout    | 1000 ms   | Module không trả lời → offline   |
| Maxwell keep-alive timeout   | Chưa xác định | Module tự tắt khi mất CAN      |

## 6. PC Application

### 6.1 Thông tin chung

- Ngôn ngữ: Python 3.10+
- GUI: Tkinter
- Serial: pyserial
- Build: PyInstaller → `ChargerDemo.exe` (single file)
- Tương thích: Windows 10, 11

### 6.2 Cấu trúc

```
pc_app/
├── charger_demo.py       # Source code chính (GUI + protocol)
├── requirements.txt      # pyserial>=3.5, pyinstaller>=6.0
├── ChargerDemo.spec      # PyInstaller spec file
├── build_exe.bat         # Script build exe
├── build/                # Build artifacts
└── dist/
    └── ChargerDemo.exe   # Executable (đã build sẵn)
```

### 6.3 Chức năng hiện tại

- Quét và kết nối COM port (USB CDC)
- Gửi PING kiểm tra kết nối, hiển thị firmware version
- Cài đặt điện áp (V) và current limit (%)
- Start / Stop module sạc
- Hiển thị realtime: V, I, nhiệt độ, công suất, trạng thái online
- Hiển thị alarm text khi có lỗi
- Log hoạt động có timestamp

## 7. Trạng thái dự án hiện tại

### Đã hoàn thành (✓)

- [x] Maxwell CAN protocol driver (`maxwell_charger.c/.h`)
- [x] BSP CAN abstraction (`bsp_can.c/.h`)
- [x] PC ↔ STM32 binary protocol (`pc_protocol.c/.h`)
- [x] USB CDC glue code (`pc_link_usb.c`)
- [x] Demo standalone CAN test (`main_demo.c`)
- [x] Demo PC App control (`demo_main.c`)
- [x] PC App Python GUI (`charger_demo.py`)
- [x] Build thành .exe

### Chưa triển khai (TODO)

- [ ] Truyền thông CAN với BMS pin (CAN2)
- [ ] Màn hình DWIN HMI (RS485)
- [ ] Chu trình sạc (CC-CV, multi-stage) — logic điều khiển theo profile
- [ ] Đo nhiệt độ NTC10k (ADC)
- [ ] Điều khiển relay (đóng khi V_sạc > 90% V_pin)
- [ ] Nút nhấn Start/Stop vật lý
- [ ] RTC integration (timestamp log/alarm)
- [ ] MicroSD logging
- [ ] Flash ngoại (lưu cài đặt, FW backup)
- [ ] Hỗ trợ nhiều module sạc mắc song song
- [ ] Mật khẩu kích pin trên HMI
- [ ] Lưu lịch sử mã lỗi theo thời gian
- [ ] Tích lũy năng lượng sạc (kWh)
- [ ] Bảo vệ jack sạc (chênh áp, quá nhiệt NTC)

### 3. Current Status

*   **Firmware**:
    *   **USB CDC**: Mạch đã gửi/nhận dữ liệu PC App cực kỳ mượt mà. Đã sửa lỗi thiếu Endpoint và tăng heap/stack size để chạy ổn định trên Windows.
    *   **App Logic**: Đã triển khai FSM vòng lặp điều khiển chính (IDLE -> STARTING -> RUNNING -> FAULT) trong `maxwell_charger.c` qua CAN1.
    *   **Mạch giả lập phần cứng (Mock CAN2)**: Đã kết nối vòng Loopback (CAN1 TX nối vào CAN2 RX và ngược lại). Mạch tự giả lập một cục sạc Maxwell và phản hồi lại các gói tin, ngắt kết nối khi gọi sai địa chỉ.
    *   **Sửa lỗi CAN ID (Maxwell V1.50 Protocol)**: Đã điều chỉnh cách dịch bit cho cấu trúc CAN ID chuẩn xác 100% so với tài liệu và tool giao tiếp thực tế của Maxwell.
    *   **Debug UART**: Đã bổ sung cơ chế Log ra UART1 (PA9, Baud 115200) để ghi vết các gói tin CAN TX, RX (Hỗ trợ cực lớn cho quá trình ghép nối mạch sạc thật).
*   **Software (PC)**:
    *   **Giao diện**: Hoàn thiện cấu trúc bảng điều khiển Tkinter, bóc tách cấu trúc 29 bytes.
    *   **Tính năng**: Có ô nhập địa chỉ thiết bị linh động (Module Addr), nút Set Addr.
    *   **Bộ nhớ trạng thái (Cache Setpoint)**: Nhớ cấu hình Voltage/Current cũ và tự động tải lại nếu đổi địa chỉ Module.
    *   **Trạng thái**: Tự báo lỗi Offline nếu mất kết nối (Dựa trên module addr timeout).

### Vấn đề đã biết / cần test

1. **Maxwell keep-alive timeout**: Chưa xác định chính xác. Cần test: tắt polling rồi đo thời gian module off.
2. **Loại lệnh keep-alive**: Chưa biết chỉ read có đủ hay phải gửi set/start lại.
3. **Pin CAN/LED trên PCB thực**: Demo dùng PD12/PD13, sản phẩm thực có thể khác.
4. **USB CDC busy handling**: Hiện dùng retry loop, production nên dùng ring buffer + DMA.

## 8. Cách build và chạy

### 8.1 Firmware

**Toolchain:** STM32CubeIDE hoặc ARM GCC + Makefile (chưa có Makefile trong repo)

**Bước:**
1. Tạo project STM32CubeIDE cho STM32F407VET6
2. Cấu hình trong CubeMX: CAN1 (PB8/PB9, 125Kbps), USB OTG FS CDC, USART1 (PA9/PA10)
3. Copy thư mục `firmware/` vào project
4. Chọn 1 trong 2 demo:
   - `main_demo.c` — test CAN standalone (cần UART debug)
   - `demo_main.c` — dùng với PC App (cần USB CDC)
5. Build và flash qua ST-Link / SWD

### 8.2 PC App

```bash
# Cài dependencies
cd pc_app
pip install -r requirements.txt

# Chạy trực tiếp
python charger_demo.py

# Hoặc build exe
pyinstaller ChargerDemo.spec
# Output: dist/ChargerDemo.exe
```

## 9. Quy ước và ghi chú cho developer/AI

### 9.1 Coding style
- C: snake_case cho functions/variables, UPPER_CASE cho defines/macros
- Prefix theo module: `MXR_` (Maxwell), `BSP_CAN_` (CAN BSP), `PC_` (PC protocol)
- Comment bằng tiếng Việt hoặc English đều được, ưu tiên rõ ràng
- Tách lớp rõ ràng: Application → Protocol/Driver → BSP → HAL

### 9.2 Byte order
- **CAN bus (Maxwell):** Big-endian (MSB first)
- **USB protocol (PC ↔ STM32):** Little-endian (native STM32)
- Đây là điểm dễ gây bug — luôn kiểm tra khi viết code parse/build frame

### 9.3 Safety rules
- Module Maxwell TỰ TẮT khi mất CAN → firmware phải poll liên tục
- Khi phát hiện alarm critical (fault, OV, overtemp, short) → ngay lập tức Stop
- Relay chỉ đóng khi V_sạc > 90% V_pin (tránh spark/inrush)
- Relay phải ngắt khi sạc xong HOẶC có lỗi

### 9.4 Hướng phát triển tiếp theo
1. Migrate sang FreeRTOS: mỗi khối chức năng = 1 task riêng
2. Thêm CAN2 cho BMS (task riêng, queue chia sẻ data)
3. Implement chu trình sạc CC-CV multi-stage
4. Tích hợp HMI DWIN (RS485, MODBUS-like protocol)
5. Thiết kế PCB sản phẩm (với cách ly CAN, relay driver, NTC input)
