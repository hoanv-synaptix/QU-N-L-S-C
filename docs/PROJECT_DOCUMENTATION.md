# Mạch Điều Khiển Sạc - Firmware Documentation

## 1. Tổng Quan Dự Án

**Mục tiêu:** Thiết kế firmware điều khiển sạc pin theo chu trình, hỗ trợ nhiều loại module sạc khác nhau (Maxwell, Lianming, TonHe).

**Phần cứng:**
- MCU: STM32F407 (Cortex-M4)
- CAN: 2x CAN bus cách ly (<1Mbps)
- HMI: DWIN RS485
- Điều khiển: Relay, Nút nhấn, LED

---

## 2. Kiến Trúc Tổng Thể

```
┌─────────────────────────────────────────────────────────────────────┐
│                        app_charger.c/h                              │
│                     (Application Layer)                              │
│  - Main loop, button handling, LED control, status reporting     │
└────────────────────────────┬────────────────────────────────────────┘
                             │ uses
                             ↓
┌─────────────────────────────────────────────────────────────────────┐
│                    charger_core.c/h                                  │
│                  (Abstract Interface Layer)                          │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │ CHG_DriverOps_t - Interface chuẩn (12 function pointers) │  │
│  │ CHG_ModuleView_t - Data model chuẩn                       │  │
│  │ Driver Registry - Quản lý/chọn driver runtime              │  │
│  │ API Wrappers - CHG_SetVoltage, CHG_Start, CHG_Process... │  │
│  └──────────────────────────────────────────────────────────────┘  │
└────────────────────────────┬────────────────────────────────────────┘
                             │ implements
         ┌───────────────────┼───────────────────┐
         ↓                   ↓                   ↓
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐
│ driver_maxwell  │ │ driver_lianming │ │ driver_tonhe   │
│                 │ │                 │ │                 │
│ Protocol: V1.50│ │ Protocol: V2.0  │ │ Protocol: V1.3 │
│ CAN: 125Kbps   │ │ CAN: 125Kbps   │ │ CAN: 125Kbps   │
│ Format: IEEE    │ │ Format: Integer │ │ Format: J1939  │
│ Float           │ │ mV/mA          │ │ 0.1V/0.01A    │
└─────────────────┘ └─────────────────┘ └─────────────────┘
                             │
                             ↓
┌─────────────────────────────────────────────────────────────────────┐
│                    charger_protocol.h                                │
│               (Helper Functions)                                    │
│  - Data conversion (Float/U32/U16 <-> Big-Endian)                │
│  - Register constants (CHG_REG_*)                                │
│  - Lưu ý: Mỗi driver tự implement CAN ID builder riêng!      │
└─────────────────────────────────────────────────────────────────────┘
                             │
                             ↓
┌─────────────────────────────────────────────────────────────────────┐
│                      bsp_can.c/h                                    │
│                   (Hardware Abstraction)                             │
│  - STM32 HAL CAN driver                                           │
│  - Frame transmit/receive                                         │
│  - Filter configuration                                           │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 3. Module Layers

### 3.1 Application Layer (`app_charger.c`)

**Vai trò:**
- Main loop xử lý button, LED, periodic tasks
- Gọi CHG_xxx() API để điều khiển
- Nhận CAN frames từ ISR và gọi `CHG_FeedCanFrame()`

**Files:**
- `App/Src/app_charger.c`
- `App/Inc/app_charger.h`

---

### 3.2 Core Layer (`charger_core.c/h`)

**Vai trò:**
- Định nghĩa interface chuẩn `CHG_DriverOps_t`
- Data model chuẩn `CHG_ModuleView_t`
- Driver registry - đăng ký và chọn driver lúc runtime
- API gateway - CHG_SetVoltage, CHG_Start, CHG_Process...

**Design Patterns:**
- Abstract Factory: Tạo driver instances qua ops table
- Strategy Pattern: Driver implementations có thể thay thế
- Proxy: charger_core delegate tới active driver

**Files:**
- `App/Src/charger_core.c`
- `App/Inc/charger_core.h`

---

### 3.3 Driver Layer (3 drivers)

Mỗi driver implements `CHG_DriverOps_t` interface:

| Driver | File | Protocol | CAN ID Format | Data Format |
|--------|------|----------|---------------|-------------|
| **Maxwell** | `driver_maxwell.c` | V1.50 | Proprietary 29-bit | IEEE Float |
| **Lianming** | `driver_lianming.c` | V2.0 | Custom 14-bit CMD + 7-bit Addr | Integer mV/mA |
| **TonHe** | `driver_tonhe.c` | V1.3 (J1939) | J1939 29-bit | Integer 0.1V/0.01A |

---

## 4. Chi Tiết Protocol

### 4.1 Maxwell Protocol (V1.50)

**CAN Frame Format (29-bit Extended ID):**
```
| Bit 28-20 | Bit 19 | Bit 18-11 | Bit 10-3 | Bit 2-0 |
| PROTNO    | PTP    | DSTADDR    | SRCADDR  | Group    |
| 0x060    | 0/1    | 0-63      | 0xF0     | 0-7      |
```

**Data Format (8 bytes):**
```
| Byte 0     | Byte 1   | Byte 2-3      | Byte 4-7        |
| Func Code  | Reserved | Register      | Data (IEEE 754) |
| 0x03/0x10 | 0x00     | 0x0021       | Float value     |
```

**Examples:**
- Set voltage 700V: `03 00 00 21 44 2F 00 00`
- Start: `03 00 00 30 00 00 00 00`
- Stop: `03 00 00 30 00 01 00 00`

**Files:**
- `driver_maxwell.c`, `driver_maxwell.h`

---

### 4.2 Lianming Protocol (V2.0)

**CAN Frame Format:**
```
TX ID:  0x1907C0 + module_addr (0-60)
RX ID:  0x1807C0 + module_addr (0-60)

Bits: | 28-15: Command ID | 14-7: Reserved | 6-0: Module Address |
```

**Data Format - Set Output (CMD=0):**
```
| Byte 0 | Byte 1-3          | Byte 4-7          |
| CMD    | Current (mA)     | Voltage (mV)     |
| 0x00   | 0x015F90 (90A)   | 0x000186A0 (100V)|
```

**Data Format - Read Status (CMD=1):**
```
| Byte 0 | Byte 1 | Byte 2-3     | Byte 4-5    | Byte 6-7    |
| CMD    | Status| Current(A)   | Voltage(V) | Fault Flags |
| 0x01   | 0xFF  | 0x0117 (27.9)| 0x03E8 (100)| 0x0000     |
```

**Examples:**
- Set 100V, 90A: `ID=0x1907C083, Data: 00 01 5F 90 00 01 86 A0`
- Start module 1: `ID=0x1907C001, Data: 02 00 00 00 00 00 00 55`
- Stop module 1: `ID=0x1907C001, Data: 02 00 00 00 00 00 00 AA`

**Files:**
- `driver_lianming.c`, `driver_lianming.h`

---

### 4.3 TonHe Protocol (V1.3 - J1939)

**CAN Frame Format (J1939 29-bit Extended ID):**
```
| Bit 28-26 | Bit 25 | Bit 24 | Bit 23-16 | Bit 15-8 | Bit 7-0 |
| Priority  | R      | DP     | PF         | PS       | SA      |
| 0-7       | 0      | 0      | PDU Format | DestAddr | Source  |
```

**Addresses:**
- Controller: 0xA0
- Module: 0x01 - 0xF0
- Broadcast: 0xFF

**PGN (Parameter Group Numbers):**
- 0x000100: M_C_1 - Status
- 0x000200: M_C_2 - Confirm
- 0x000400: C_M_2 - Parameter Set
- 0x000600: C_M_24 - Start/Stop

**Data Format (Status):**
```
| Byte 0  | Byte 1-2      | Byte 3-4     | Byte 5-6 | Byte 7  |
| Status  | Voltage(0.1V)| Current(0.01A)| Fault   | PFC    |
| 0x01   | 0x0FA0 (400V)| 0x2710 (100A)| Bitfield| Bitfield|
```

**Examples:**
- Module status: `ID=0x1801A001, Data: 01 A0 0F 10 27 00 00 00`
- Start modules: `ID=0x0C03FFA0, Data: 07 00 00 AA 00 00 00 00`

**Files:**
- `driver_tonhe.c`, `driver_tonhe.h`

---

## 5. Sử Dụng Driver

### 5.1 Khởi tạo

```c
// Trong App_Init()

// 1. Register all available drivers
CHG_RegisterDriver(CHG_DRIVER_MAXWELL,  CHG_MaxwellDriverOps());
CHG_RegisterDriver(CHG_DRIVER_LIANMING, CHG_LianmingDriverOps());
CHG_RegisterDriver(CHG_DRIVER_TONHE,    CHG_TonheDriverOps());

// 2. Select active driver (at runtime or compile-time)
CHG_SelectDriver(CHG_DRIVER_MAXWELL);  // or LIANMING, TONHE

// 3. Initialize
CHG_Init();

// 4. Add module(s)
CHG_AddModule(1, 0);  // addr=1, group=0
```

### 5.2 Điều khiển

```c
// Set voltage and current
CHG_SetVoltage(0, 700.0f);      // Module 0, 700V
CHG_SetCurrentLimit(0, 0.8f);   // 80% of rated

// Start/Stop
CHG_Start(0);    // Start module 0
CHG_Stop(0);     // Stop module 0

// Control all modules
CHG_SetVoltageAll(700.0f);
CHG_StartAll();
CHG_StopAll();
CHG_EmergencyStop();
```

### 5.3 Runtime Processing

```c
// Trong main loop (App_Loop)
void App_Loop(void) {
    uint32_t now = HAL_GetTick();

    // Process charger state machine (gọi mỗi 10-50ms)
    if (now - last_process >= 20) {
        CHG_Process(now);
        last_process = now;
    }

    // Get system status for HMI/PC
    CHG_SystemSummary_t summary;
    CHG_GetSystemSummary(&summary);
    // summary.total_current, summary.modules_online, etc.
}
```

### 5.4 CAN Frame Handling

```c
// Trong CAN RX ISR callback
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan) {
    BSP_CAN_Frame_t frame;
    if (BSP_CAN_Receive(&frame, 0)) {
        CHG_FeedCanFrame(frame.ext_id, frame.data, frame.dlc);
    }
}
```

---

## 6. Thêm Driver Mới

Để thêm driver cho module sạc mới:

### 6.1 Tạo Files

```
App/Inc/driver_newmodule.h
App/Src/driver_newmodule.c
```

### 6.2 Implement Interface

```c
// Trong driver_newmodule.c

// 1. Define constants (CAN ID format, command codes, etc.)
#define NM_CMD_XXX    0xXX
#define NM_RESP_XXX   0xXX

// 2. Implement CAN ID builder
static uint32_t nm_build_can_id(uint8_t dst, uint8_t src) {
    // Build the 29-bit ID according to protocol
}

// 3. Implement all CHG_DriverOps_t functions
static void nm_init(void);
static int8_t nm_add_module(uint8_t addr, uint8_t group);
static void nm_remove_module(uint8_t idx);
static bool nm_set_voltage(uint8_t idx, float voltage_v);
static bool nm_set_current_limit(uint8_t idx, float ratio);
static bool nm_start(uint8_t idx);
static bool nm_stop(uint8_t idx);
static void nm_set_voltage_all(float voltage_v);
static void nm_set_current_limit_all(float ratio);
static void nm_start_all(void);
static void nm_stop_all(void);
static void nm_emergency_stop(void);
static void nm_process(uint32_t now_tick);
static void nm_feed_frame(uint32_t ext_id, const uint8_t *data, uint8_t dlc);
static void nm_get_system_summary(CHG_SystemSummary_t *summary);
static uint8_t nm_get_module_count(void);
static bool nm_get_module_view(uint8_t idx, CHG_ModuleView_t *view);

// 4. Create ops table
static const CHG_DriverOps_t g_newmodule_ops = {
    .name = "newmodule",
    .init = nm_init,
    .add_module = nm_add_module,
    // ... all other functions
};

const CHG_DriverOps_t *CHG_NewmoduleDriverOps(void) {
    return &g_newmodule_ops;
}
```

### 6.3 Update app_charger.c

```c
// Include new driver header
#include "driver_newmodule.h"

// Register in App_Init
CHG_RegisterDriver(CHG_DRIVER_NEWMODULE, CHG_NewmoduleDriverOps());
```

---

## 7. Data Models

### 7.1 CHG_ModuleView_t

```c
typedef struct {
    uint8_t          addr;           // Module address (0-63)
    uint8_t          group;          // Group number
    bool             enabled;         // Module is enabled
    bool             online;          // Module is online
    bool             running;         // Module is running
    CHG_ModuleState_t state;         // FSM state
    float            voltage;        // Output voltage (V)
    float            current;         // Output current (A)
    float            current_limit;   // Current limit (A or ratio)
    float            temp_dcdc;       // DCDC temperature (C)
    float            temp_ambient;    // Ambient temperature (C)
    uint32_t         alarm_status;    // Raw alarm bits
    CHG_AlarmFlag_t  alarm_flags;    // Standardized flags
    uint32_t         input_power;    // Input power (W)
    uint32_t         last_rx_tick;  // Last RX timestamp
    uint32_t         last_tx_tick;  // Last TX timestamp
    CHG_CommStats_t  stats;         // Communication stats
} CHG_ModuleView_t;
```

### 7.2 CHG_DriverOps_t

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

---

## 8. Build System

**Build với CMake:**
```bash
cd charger/build
cmake ..
make
```

**Hoặc với STM32CubeIDE:**
- Import project từ `charger/` folder
- Build như bình thường

---

## 9. Tài Liệu Tham Khảo

### 9.1 Protocol Documents
- `CAN Communication Protocol - Maxwell_V1.50.pdf`
- `Lianming Power Digital Power Module CAN Communication Protocol V2.0.pdf`
- `TonHeCANcommunicationbetweenchargingmoduleandmonitor TONHE V1.3.pdf`

### 9.2 Hardware
- `stm32f407vet6_black_sch.pdf` - Board schematic

### 9.3 Code Files
| File | Mô tả |
|------|-------|
| `driver_maxwell.c/h` | Maxwell driver |
| `driver_lianming.c/h` | Lianming driver |
| `driver_tonhe.c/h` | TonHe driver |
| `charger_core.c/h` | Abstract interface |
| `charger_protocol.h` | Helper functions |
| `bsp_can.c/h` | CAN HAL |
| `app_charger.c/h` | Application layer |

---

## 10. Design Decisions

### 10.1 Tại sao mỗi driver tự build CAN ID?

Vì 3 module dùng 3 format CAN ID hoàn toàn khác nhau:
- Maxwell: PROTNO(9b) + PTP(1b) + DST(8b) + SRC(8b) + GRP(3b)
- Lianming: Command ID(14b) + Module Address(7b)
- TonHe: Priority(3b) + PF(8b) + PS(8b) + SA(8b)

→ Không có điểm chung nào cả!

### 10.2 Tại sao giữ charger_protocol.h?

Giữ lại các helper functions cho data conversion:
- Float <-> Big-Endian (cho Maxwell)
- U32 <-> Big-Endian
- U16 <-> Big-Endian (cho Lianming, TonHe)

Register constants (CHG_REG_*) cũng được giữ làm canonical map.

### 10.3 Tại sao dùng CHG_ModuleView_t?

Upper layer (app_charger, HMI, PC) cần một data model thống nhất để đọc trạng thái mà không cần biết đang dùng driver nào.

---

## 11. Lịch Sử Thay Đổi

| Ngày | Thay đổi |
|------|-----------|
| 2026-06-24 | Tách riêng CAN ID builders cho từng driver |
| 2026-06-24 | Thêm comments chi tiết cho từng driver |
| 2026-06-24 | Sửa Magic Numbers thành defines |
| 2026-06-24 | Verify protocol với tài liệu PDF |

---

## 12. Liên Hệ & Hỗ Trợ

Để hiểu thêm về codebase:
- Đọc file header để biết interface
- Xem `app_charger.c` để biết cách sử dụng
- Xem driver bất kỳ để biết pattern implement

---

## 13. PC Communication Protocol

### 13.1 Giao Diện Vật Lý

- **USB CDC (Virtual COM)** - STM32F407 USB Device
- Baudrate: 115200 (không quan trọng vì là USB CDC)
- Cổng COM được enumerate tự động bởi PC

### 13.2 Frame Format

```
┌──────┬──────┬──────┬─────────┬────────────┬──────┐
│ SOF1 │ SOF2 │ CMD  │  LEN    │  PAYLOAD   │ CRC8 │
│ 0xAA │ 0x55 │ 1B   │  1B     │  LEN bytes │  1B  │
└──────┴──────┴──────┴─────────┴────────────┴──────┘
```
- **SOF**: Start of frame (0xAA 0x55)
- **CMD**: Command/Response code
- **LEN**: Payload length (0-64)
- **CRC8**: Polynomial 0x07, tính trên [CMD, LEN, PAYLOAD]

### 13.3 Commands (PC → MCU)

| CMD | Name | Payload | Mô tả |
|-----|------|---------|-------|
| 0x01 | SET_VOLTAGE | float (4B) | Đặt điện áp đầu ra (V) |
| 0x02 | SET_CURRENT | float (4B) | Đặt dòng sạc (tỷ lệ 0~1) |
| 0x03 | START | none | Bắt đầu sạc |
| 0x04 | STOP | none | Dừng sạc |
| 0x05 | SET_MODULE_ADDR | uint8 addr + uint8 group | Cài địa chỉ module |
| 0x06 | PING | none | Kiểm tra kết nối |
| 0x07 | READ_REG | uint8 idx + uint16 reg | Đọc register module |
| 0x08 | EMERGENCY_STOP | none | Dừng khẩn cấp |
| 0x09 | SET_DRIVER | uint8 driver_id | Chọn driver |
| 0x10 | READ_CONFIG | uint8 section | Đọc 1 section config |
| 0x11 | WRITE_CONFIG | uint8 section + data | Ghi 1 section |
| 0x12 | READ_ALL_CONFIG | none | Đọc toàn bộ config |
| 0x13 | WRITE_ALL_CONFIG | all data | Ghi toàn bộ config |
| 0x14 | SET_MODULE_COUNT | uint8 count | Số module song song |
| 0x20 | GET_STATUS | none | Yêu cầu status ngay |
| 0x21 | HISTORY_GET | uint16 index | Đọc lịch sử |
| 0x22 | HISTORY_CLEAR | none | Xóa lịch sử |
| 0x23 | RESET_CONFIG | none | Reset về mặc định |

### 13.4 Responses (MCU → PC)

| CMD | Name | Payload | Mô tả |
|-----|------|---------|-------|
| 0x81 | STATUS | 29 bytes | Status định kỳ |
| 0x82 | ACK | uint8 cmd | Command được chấp nhận |
| 0x83 | NACK | uint8 cmd + uint8 err | Command bị từ chối |
| 0x84 | PONG | uint32 fw_version | Trả lời PING |
| 0x85 | READ_REG | varies | Kết quả đọc register |
| 0x90 | CONFIG | section + data | Config response |
| 0x91 | BMS_STATUS | varies | Status từ BMS |
| 0x92 | HISTORY | index + record | Lịch sử |

### 13.5 Files

| File | Mô tả |
|------|-------|
| `App/Inc/pc_protocol.h` | Protocol definitions |
| `App/Src/pc_protocol.c` | Protocol implementation |

---

## 14. Configuration System

### 14.1 Cấu Trúc Config

Hệ thống chia thành 6 sections:

| Section | ID | Mô tả |
|---------|-----|-------|
| SYSTEM | 0x01 | Driver, module count, version |
| CHARGER | 0x02 | Voltage, current, timeout |
| MODULE | 0x03 | Address, comm settings |
| PROTECT | 0x04 | Over/under voltage, temp limits |
| BMS | 0x05 | BMS enable, CAN settings |
| DISPLAY | 0x06 | Status interval, UI settings |

### 14.2 Config Sections Chi Tiết

**PC_CfgSystem_t (20 bytes):**
- driver_id, module_count, fw_version, hw_version, serial_number

**PC_CfgCharger_t (34 bytes):**
- target_voltage, target_current, max_voltage, max_current
- charge_timeout, term_by_soc, term_soc_full, auto_restart

**PC_CfgModule_t (26 bytes):**
- base_address, group_id, rated_voltage, rated_current
- comm_timeout, retry_count, poll_interval

**PC_CfgProtect_t (34 bytes):**
- over_voltage, under_voltage, over_current
- over_temp_dcdc, over_temp_ambient
- delay_over_temp, delay_over_current

**PC_CfgBms_t (26 bytes):**
- bms_enabled, bms_can_channel, bms_max_voltage
- soc_full, temp_max_charge, bms_timeout

**PC_CfgDisplay_t (14 bytes):**
- status_interval, graph_interval, show_graph
- beep_enabled, brightness

### 14.3 Files

| File | Mô tả |
|------|-------|
| `App/Inc/pc_config.h` | Config structures |
| `App/Src/pc_config.c` | Config management |

---

## 15. Flash Storage

### 15.1 Tổng Quan

Dùng Flash nội của STM32F407 để lưu config, không bị mất khi mất điện.

### 15.2 Vùng Nhớ

- **Địa chỉ**: 0x0803E800 (sector 5, 128KB cuối)
- **Dung lượng**: 2KB
- **Cấu trúc**: Magic + Version + CRC + Timestamp + Data

### 15.3 API

```c
void FlashStorage_Init(void);           // Khởi tạo
bool FlashStorage_Load(uint8_t *data, uint16_t *len);  // Đọc
bool FlashStorage_Save(const uint8_t *data, uint16_t len); // Ghi
bool FlashStorage_IsValid(void);       // Kiểm tra hợp lệ
bool FlashStorage_Erase(void);         // Xóa
uint32_t FlashStorage_GetTimestamp(void); // Thời điểm lưu
```

### 15.4 Files

| File | Mô tả |
|------|-------|
| `App/Inc/flash_storage.h` | Flash API |
| `App/Src/flash_storage.c` | Flash implementation |

---

## 16. Startup Flow

### 16.1 MCU Boot Sequence

```
1. System Reset
   ↓
2. HAL_Init(), SystemClock_Config()
   ↓
3. App_Init()
   ├── FlashStorage_Init()
   ├── PC_Cfg_Load() → Đọc Flash HOẶC dùng defaults
   ├── BSP_CAN_Start() → Khởi tạo CAN1
   ├── BSP_CAN2_Start() → Khởi tạo CAN2 (BMS)
   ├── BMS_Init()
   ├── CHG_RegisterDriver() → Đăng ký 3 drivers
   ├── CHG_SelectDriver(driver_id từ config)
   ├── CHG_Init() → Khởi tạo driver
   └── CHG_AddModule() → Thêm module
   ↓
4. App_Loop() (vòng lặp vô hạn)
   ├── CHG_Process() mỗi 20ms
   ├── BMS_Process() mỗi 20ms
   ├── PC_Protocol_SendStatus() mỗi 200ms
   └── Button/LED processing
```

### 16.2 PC Config Flow

```
PC App                              MCU
  │                                   │
  ├─ CMD_READ_ALL_CONFIG ────────────→│
  │←──────────────────────────────────┤
  │           RSP_CONFIG (all)        │
  │                                   │
  ├─ CMD_WRITE_CONFIG ──────────────→│
  │   (section + data)                │
  │←──────────────────────────────────┤
  │              ACK                   │
  │                                   │
  ├─ (tự động ghi vào Flash) ──────→│
  │                                   │
```

### 16.3 Power Loss Recovery

```
Mất điện
    ↓
MCU reboot
    ↓
App_Init()
    ↓
PC_Cfg_Load()
    ├── Kiểm tra Flash có valid config?
    │   ├── Có → Load từ Flash
    │   └── Không → Dùng defaults
    ↓
Tiếp tục hoạt động với config đã lưu
```

---

## 17. PC App Architecture

### 17.1 File Structure

```
pc_app/
├── main.py                      # Entry point
├── protocol.py                  # Legacy protocol helpers
├── models/
│   ├── pc_config.py           # Config data models
│   └── charger_status.py       # Status models
├── services/
│   ├── serial_service.py       # USB CDC communication
│   ├── protocol_handler.py    # Frame parsing & building
│   └── config_service.py      # Config management
└── views/
    └── main_window.py          # PyQt5 UI
```

### 17.2 Key Classes

| Class | File | Mô tả |
|-------|------|-------|
| SerialService | serial_service.py | USB CDC read/write |
| ProtocolHandler | protocol_handler.py | Frame state machine |
| ConfigService | config_service.py | Config PC ↔ MCU |
| MainViewModel | main_viewmodel.py | UI logic |

---

## 18. Lịch Sử Thay Đổi (tiếp)

| Ngày | Thay đổi |
|------|-----------|
| 2026-06-25 | Thêm PC communication protocol |
| 2026-06-25 | Thêm Flash storage cho config persistence |
| 2026-06-25 | Thêm 6 sections config system |
| 2026-06-25 | Đồng bộ firmware ↔ PC App |
| 2026-06-25 | Cập nhật tất cả drivers get_system_summary() |
