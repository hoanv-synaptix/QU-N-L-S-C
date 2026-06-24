# Dự Án: Mạch Điều Khiển Sạc Pin Theo Chu Trình

## 1. Mục đích dự án

Thiết kế và lập trình firmware + PC app cho một bộ điều khiển sạc pin công nghiệp. Bộ điều khiển này đóng vai trò trung gian giữa:

- **Module sạc công suất** — Maxwell MXR Series, Lianming, hoặc TONHE V1.3
- **Hệ thống quản lý pin (BMS)** — cung cấp thông số pin: SOC, nhiệt độ, điện áp, dòng điện
- **Người vận hành** — thông qua màn hình HMI (DWIN) và phần mềm PC

Bộ điều khiển nhận thông số pin từ BMS, nhận cài đặt chu trình sạc từ PC App, rồi điều khiển module sạc tương ứng. Nó cũng thực hiện các chức năng bảo vệ (quá áp, quá nhiệt, mất liên lạc) và hiển thị trạng thái.

## 2. Kiến trúc tổng thể

```
┌─────────────┐    USB CDC     ┌───────────────────────────────┐     CAN Bus      ┌──────────────────┐
│   PC App    │◄──────────────►│      STM32F407 Controller     │◄────────────────►│  Charging Module │
│  (Python)   │   Virtual COM   │                               │   CAN1 125Kbps   │  (Maxwell/LIANMING/
└─────────────┘                │  ┌───────────┐ ┌───────────┐  │                   │   TONHE)
                              │  │pc_protocol│ │charger_   │  │     CAN Bus      ┌──────────────────┐
┌─────────────┐    RS485       │  │           │ │core       │  │◄────────────────►│  BMS Pin         │
│  Màn hình   │◄──────────────►│  │           │ │(canonical)│  │   CAN2 (TODO)    │  (CAN protocol)  │
│  DWIN HMI   │                │  └───────────┘ └─────┬─────┘  │                   └──────────────────┘
└─────────────┘                │                      │
                             ├── driver_maxwell.c  │  (plug-in driver)
                             ├── driver_lianming.c │
                             └── driver_tonhe.c   │  (J1939-based)
                                              │
                             ┌─────────────────┴─────────────────┘
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

Board demo hiện tại: **STM32F407VET6 "black board"**

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
| LED Green     | PA6           | Trạng thái sạc (active LOW)              |
| LED Red       | PA7           | Trạng thái lỗi (active LOW)              |
| BTN_START     | PA0           | Active HIGH                                |
| BTN_STOP      | PE4           | Active LOW                                 |

## 4. Kiến trúc Firmware (Canonical Driver Layer)

### 4.1 Cấu trúc file

```
charger/
├── App/
│   ├── Inc/
│   │   ├── app_charger.h         # Application layer API
│   │   ├── bsp_can.h             # CAN BSP (HAL wrapper)
│   │   ├── charger_core.h         # ⭐ Canonical interface (CHG_DriverOps_t)
│   │   ├── charger_protocol.h     # Shared CAN constants, register maps
│   │   ├── debug_log.h           # UART logging
│   │   ├── driver_lianming.h     # Vendor: Lianming
│   │   ├── driver_maxwell.h      # Vendor: Maxwell
│   │   ├── driver_tonhe.h       # Vendor: TONHE V1.3
│   │   └── pc_protocol.h        # PC ↔ STM32 USB CDC protocol
│   └── Src/
│       ├── app_charger.c          # Main loop, button/LED handling
│       ├── bsp_can.c             # CAN1/CAN2 HAL wrapper + mock
│       ├── charger_core.c          # Driver registry + dispatcher
│       ├── debug_log.c
│       ├── driver_lianming.c      # ⭐ Canonical driver
│       ├── driver_maxwell.c       # ⭐ Canonical driver
│       ├── driver_tonhe.c        # ⭐ Canonical driver (J1939)
│       └── pc_protocol.c          # PC command parser
├── Core/                         # STM32CubeMX generated
├── cmake/                       # CMake build
├── USB_DEVICE/                  # USB CDC
└── CMakeLists.txt
```

### 4.2 Canonical Interface (charger_core.h)

```c
typedef struct {
    const char *name;
    void    (*init)(void);
    int8_t  (*add_module)(uint8_t addr, uint8_t group);
    void    (*remove_module)(uint8_t idx);
    bool    (*set_voltage)(uint8_t idx, float voltage_v);
    bool    (*set_current_limit)(uint8_t idx, float ratio);
    bool    (*start)(uint8_t idx);
    bool    (*stop)(uint8_t idx);
    void    (*set_voltage_all)(float voltage_v);
    void    (*set_current_limit_all)(float ratio);
    void    (*start_all)(void);
    void    (*stop_all)(void);
    void    (*emergency_stop)(void);
    void    (*process)(uint32_t now_tick);
    void    (*feed_frame)(uint32_t ext_id, const uint8_t *data, uint8_t dlc);
    void    (*get_system_summary)(CHG_SystemSummary_t *summary);
    uint8_t (*get_module_count)(void);
    bool    (*get_module_view)(uint8_t idx, CHG_ModuleView_t *view);
} CHG_DriverOps_t;
```

### 4.3 Canonical Types

```c
typedef enum {
    CHG_DRIVER_NONE = 0,
    CHG_DRIVER_MAXWELL = 1,
    CHG_DRIVER_LIANMING = 2,
    CHG_DRIVER_TONHE = 3,
} CHG_DriverId_t;

typedef enum {
    CHG_STATE_IDLE = 0,
    CHG_STATE_STARTING,
    CHG_STATE_RUNNING,
    CHG_STATE_OFFLINE,
    CHG_STATE_FAULT,
    CHG_STATE_RECOVERING,
} CHG_ModuleState_t;

typedef enum {
    CHG_ALARM_NONE = 0x0000,
    CHG_ALARM_HW_FAULT = (1 << 0),
    CHG_ALARM_COMM_FAIL = (1 << 1),
    CHG_ALARM_OVER_TEMP = (1 << 2),
    CHG_ALARM_OVER_VOLTAGE_OUT = (1 << 3),
    CHG_ALARM_SHORT_CIRCUIT = (1 << 4),
    CHG_ALARM_AC_UNDER_VOLT = (1 << 5),
} CHG_AlarmFlag_t;

typedef struct {
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t error_count;
    uint32_t timeout_count;
    uint32_t recovery_count;
} CHG_CommStats_t;

typedef struct {
    uint8_t          addr;
    uint8_t          group;
    bool             enabled;
    bool             online;
    bool             running;
    CHG_ModuleState_t state;
    float            voltage;
    float            current;
    float            current_limit;
    float            temp_dcdc;
    float            temp_ambient;
    uint32_t         alarm_status;
    CHG_AlarmFlag_t  alarm_flags;
    uint32_t         input_power;
    uint32_t         last_rx_tick;
    uint32_t         last_tx_tick;
    CHG_CommStats_t stats;
} CHG_ModuleView_t;
```

## 5. Giao thức truyền thông

### 5.1 CAN Bus — Maxwell MXR Module

**Chuẩn:** CAN 2.0B Extended Frame (29-bit ID), 125 Kbps

**Cấu trúc Frame ID:**
```
Bit:  28───────20  19   18─17   16────────9   8─────────1   0
      ┌─────────┐  ┌─┐  ┌───┐  ┌──────────┐  ┌──────────┐  ┌─┐
      │ PROTNO  │  │P│  │RSV│  │ DST_ADDR │  │ SRC_ADDR │  │G│
      │ (9 bit) │  │T│  │   │  │ (8 bit)  │  │ (8 bit)  │  │R│
      └─────────┘  │P│  └───┘  └──────────┘  └──────────┘  │P│
                   └─┘                                       └─┘

- PROTNO = 0x060 (Maxwell)
- PTP: 1 = point-to-point, 0 = broadcast
- DST_ADDR: 0x00~0x3F (module), 0xFF (broadcast)
- SRC_ADDR: 0xF0 (controller)
```

**Register map:** Xem tài liệu gốc Maxwell MXR.

### 5.2 CAN Bus — TONHE V1.3 Module

**Chuẩn:** SAE J1939-21, 29-bit Extended, 125 Kbps (isolated)

**CAN ID Structure:**
```
[28:26] Priority (3 bits) - 0=highest, 7=lowest
[25]    Reserved (1 bit) - always 0
[24]    DP - Data Page (1 bit) - always 0
[23:16] PF - PDU Format (8 bits)
[15:8]  PS - Specific (target address for PDU1)
[7:0]   SA - Source Address
```

**Addresses:**
- Main Controller: 0xA0
- Module: 1-240
- Broadcast: 0xFF

**PGN Table:**

| PGN | Name | Direction | Priority |
|-----|------|-----------|----------|
| 0x000100 | M_C_1: Status | Module→Master | 6 |
| 0x000200 | M_C_2: Confirm | Module→Master | 2 |
| 0x000B00 | M_C_3: AC Phase | Module→Master | 6 |
| 0x009100 | M_C_4: Extended | Module→Master | 7 |
| 0x000300 | C_M_1: Broadcast Start/Stop | Master→Module | 2 |
| 0x000400 | C_M_2: Parameter Set | Master→Module | 4 |
| 0x000600 | C_M_24: Specific Start/Stop | Master→Module | 2 |

Chi tiết: xem `docs/tonhe_protocol_analysis.md`

### 5.3 USB CDC Protocol — PC App ↔ STM32

**Frame format:**
```
┌──────┬──────┬──────┬─────────┬──────────────┬──────┐
│ SOF1 │ SOF2 │ CMD  │  LEN   │   PAYLOAD    │ CRC8 │
│ 0xAA │ 0x55 │ 1B   │  1B    │  LEN bytes   │ 1B   │
└──────┴──────┴──────┴─────────┴──────────────┴──────┘
```

**Commands:**

| CMD  | Name              | Payload         |
|------|------------------|-----------------|
| 0x01 | SET_VOLTAGE      | float (4B)      |
| 0x02 | SET_CURRENT      | float (4B)      |
| 0x03 | START            | (none)          |
| 0x04 | STOP             | (none)          |
| 0x05 | SET_MODULE_ADDR  | u8 addr + u8 grp|
| 0x06 | PING             | (none)          |
| 0x07 | READ_REG         | u16 reg         |
| 0x08 | EMERGENCY_STOP   | (none)          |
| 0x09 | SET_DRIVER       | u8 driver_id    |

**Status report (29 bytes):**
```c
typedef struct {
    float    voltage;
    float    total_current;
    float    temp_dcdc;
    float    temp_ambient;
    uint32_t alarm_status;
    uint32_t total_power_in;
    uint8_t  modules_online;
    uint8_t  modules_fault;
    uint8_t  charging;
    uint8_t  btn_start;
    uint8_t  btn_stop;
} PC_StatusReport_t;  // 29 bytes
```

## 6. Driver Implementation

### 6.1 Maxwell Driver

File: `driver_maxwell.c`

- Protocol: Custom 29-bit Extended (PROTNO=0x060)
- Communication: Request/Response (polling registers)
- Keep-alive: Module self-shuts off when CAN stops — must poll continuously

### 6.2 Lianming Driver

File: `driver_lianming.c`

- Protocol: Similar to Maxwell with some differences
- Communication: Request/Response (polling)

### 6.3 TONHE Driver

File: `driver_tonhe.c`

- Protocol: SAE J1939-21 based (PGN 0x000100, 0x000600...)
- Communication: Event-driven (periodic status + commands)
- Address: 1-240 (configurable)

## 7. Build System

### 7.1 CMake

```bash
cd charger
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### 7.2 Flash

```bash
./flash.bat
```

## 8. Trạng thái dự án

### Đã hoàn thành (✓)

- [x] Canonical driver architecture (CHG_DriverOps_t)
- [x] Maxwell driver (driver_maxwell.c)
- [x] Lianming driver (driver_lianming.c)
- [x] TONHE V1.3 driver (driver_tonhe.c)
- [x] PC ↔ STM32 USB CDC protocol
- [x] BSP CAN abstraction
- [x] Debug UART logging
- [x] Button/LED handling
- [x] CAN2 mock for testing

### TODO

- [ ] CAN2 for BMS communication
- [ ] DWIN HMI (RS485)
- [ ] Charging profiles (CC-CV, multi-stage)
- [ ] NTC10k temperature sensing (ADC)
- [ ] Relay control
- [ ] RTC integration
- [ ] microSD logging
- [ ] External flash for settings

## 9. Coding Conventions

### 9.1 Style
- C: snake_case for functions/variables, UPPER_CASE for defines
- Prefix: `CHG_` (canonical), `MXR_` (Maxwell private), `LM_` (Lianming private), `TONHE_` (TONHE private)
- Comment: Vietnamese or English, prioritize clarity

### 9.2 Byte Order
- **CAN bus:** Big-endian (MSB first) — use `CHG_ProtocolFloatToBE()`, `CHG_ProtocolBEToFloat()`
- **USB protocol:** Little-endian (native STM32)

### 9.3 Safety Rules
- Module Maxwell TỰ TẮT when CAN stops → must poll continuously
- TONHE: Modules send periodic status (500ms) — master must monitor timeout
- Critical alarm (fault, OV, overtemp, short) → immediately Stop
- Relay only closes when V_charge > 90% V_bat (prevent spark/inrush)
