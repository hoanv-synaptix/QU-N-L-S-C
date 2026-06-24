# CLAUDE.md - Firmware Mạch Điều Khiển Sạc

## 1. Tổng Quan Dự Án

**Dự án:** Firmware điều khiển sạc pin theo chu trình
**MCU:** STM32F407 (Cortex-M4, 2x CAN native)
**Framework:** STM32 HAL + FreeRTOS
**Mục tiêu:** Hỗ trợ nhiều loại module sạc (Maxwell, Lianming, TonHe)

### Phần cứng
- CAN1: Kết nối module sạc (125Kbps, cách ly, extended 29-bit)
- CAN2: Kết nối BMS pin (250Kbps, standard 11-bit + extended 29-bit)
- RS485: Màn hình HMI DWIN
- USB/ UART: PC App
- Relay: 3x NO, 5A/24VDC
- NTC: 4x cổng đo nhiệt độ

---

## 2. Kiến Trúc Code

```
app_charger.c/h         → Application Layer (main loop, buttons, LEDs)
         ↓ uses
┌───────────────────────┴──────────────────────────────────────┐
│                                                               │
charger_core.c/h        → Charger Interface (CHG_DriverOps_t)  │
         ↓ implements                                              bms_core.c/h → BMS Driver (CAN2, state machine)
┌──────────┬──────────┬──────────┐              ↓               ↓
↓          ↓          ↓         bms_protocol.c/h → CAN parse/     │
driver_    driver_    driver_   build helpers   │                 │
maxwell   lianming   tonhe                            (standalone) │
         ↓                                                      │
charger_protocol.h      → Helpers                           │
         ↓                                                      ↓
bsp_can.c/h            → Hardware Abstraction (STM32 HAL)    │
                                                          (BMS không dùng bsp_can)     │
```

---

## 3. Quy Tắc Code Cơ Bản

### 3.1 Cấu Trúc Thư Mục

```
charger/
├── App/
│   ├── Inc/           # Headers (.h)
│   │   ├── charger_core.h      # Charger abstract interface
│   │   ├── charger_protocol.h  # Charger helpers (Float/U16 conversion)
│   │   ├── driver_maxwell.h    # Maxwell driver
│   │   ├── driver_lianming.h   # Lianming driver
│   │   ├── driver_tonhe.h     # TonHe driver
│   │   ├── bms_protocol.h     # BMS CAN IDs, types, parse
│   │   ├── bms_core.h         # BMS state machine, API
│   │   └── app_charger.h
│   └── Src/           # Implementation (.c)
│       ├── charger_core.c      # Charger interface
│       ├── driver_maxwell.c    # Maxwell driver
│       ├── driver_lianming.c   # Lianming driver
│       ├── driver_tonhe.c     # TonHe driver
│       ├── bms_protocol.c     # BMS parse/build
│       ├── bms_core.c         # BMS driver core
│       ├── app_charger.c      # Application layer
│       └── bsp_can.c          # Charger CAN1 abstraction
├── Drivers/            # STM32 HAL
└── CMakeLists.txt
```

### 3.2 Quy Tắc Đặt Tên

| Loại | Quy tắc | Ví dụ |
|------|----------|--------|
| Functions | `module_action()` | `CHG_SetVoltage()` |
| Variables | `camelCase` | `totalCurrent` |
| Constants | `PREFIX_NAME` | `MXR_PROTNO` |
| Types | `Name_t` | `CHG_ModuleView_t` |
| Defines | `PREFIX_NAME` | `MAXWELL_DRIVER` |
| Files | `driver_xxx.c/h` | `driver_maxwell.c` |

### 3.3 Coding Standards

**Luôn luôn:**
- ✅ Include guard trong mọi header
- ✅ Function prototypes có comment Doxygen (`/** @brief */`)
- ✅ Enum có prefix đầy đủ (`CHG_STATE_xxx`)
- ✅ Magic numbers phải thành `#define`
- ✅ Error handling cho mọi function có thể fail
- ✅ Null checks trước khi dereference pointers

**Không bao giờ:**
- ❌ Hardcoded magic numbers trong code
- ❌ `malloc()` - dùng static arrays
- ❌ Global variables không cần thiết
- ❌ TODO/FIXME - giải quyết ngay hoặc remove

---

## 4. Design Patterns

### 4.1 Abstract Factory (Driver Interface)

Mỗi driver implement `CHG_DriverOps_t` interface:

```c
typedef struct {
    const char *name;
    void    (*init)(void);
    int8_t  (*add_module)(uint8_t addr, uint8_t group);
    // ... 12 function pointers
} CHG_DriverOps_t;
```

**Pattern:**
```c
// Driver implement
static const CHG_DriverOps_t g_driver_ops = {
    .name = "maxwell",
    .init = mx_init,
    .add_module = mx_add_module,
    // ...
};

const CHG_DriverOps_t *CHG_DriverNameDriverOps(void) {
    return &g_driver_ops;
}
```

### 4.2 Strategy Pattern

Chọn driver lúc runtime:

```c
CHG_RegisterDriver(CHG_DRIVER_MAXWELL, CHG_MaxwellDriverOps());
CHG_RegisterDriver(CHG_DRIVER_LIANMING, CHG_LianmingDriverOps());
CHG_SelectDriver(CHG_DRIVER_MAXWELL);
CHG_Init();  // Gọi init của driver đang chọn
```

### 4.3 State Machine

Mỗi driver có FSM riêng:

```c
typedef enum {
    CHG_STATE_IDLE,       // Chưa khởi tạo
    CHG_STATE_STARTING,    // Đang start
    CHG_STATE_RUNNING,     // Đang chạy
    CHG_STATE_OFFLINE,    // Mất kết nối
    CHG_STATE_FAULT,      // Có lỗi
    CHG_STATE_RECOVERING  // Đang khôi phục
} CHG_ModuleState_t;
```

---

## 5. Cách Viết Driver Mới

### 5.1 Tạo Files

```c
// driver_newmodule.h
#ifndef DRIVER_NEWMODULE_H
#define DRIVER_NEWMODULE_H

#include "charger_core.h"

/**
 * @brief Get driver operations table
 * @return Pointer to CHG_DriverOps_t
 */
const CHG_DriverOps_t *CHG_NewmoduleDriverOps(void);

#endif
```

### 5.2 Implement Driver

```c
// driver_newmodule.c

/**
 * @file driver_newmodule.c
 * @brief NewModule Charging Driver
 * @note Protocol: XXX - CAN 2.0B Extended Frame, 125Kbps
 *
 * Hardware Interface:
 *   - CAN: 125Kbps, Extended 29-bit
 *
 * CAN Frame Format:
 *   [Mô tả format]
 *
 * References:
 *   - "Protocol_Document.pdf"
 */

#include "driver_newmodule.h"
#include "bsp_can.h"
#include "charger_protocol.h"
#include <string.h>

/* ============== Constants ============== */
#define NM_MAX_MODULES         8U
#define NM_OFFLINE_TIMEOUT_MS  2000U
#define NM_RESP_OK            0xFFU

/* CAN IDs */
#define NM_CMD_BASE           0xXXXXXXU
#define NM_RESP_BASE          0xXXXXXXU

/* ============== Private Types ============== */
typedef struct {
    CHG_ModuleView_t view;
    uint8_t retry_count;
    bool should_run;
} NM_Module_t;

/* ============== Private State ============== */
static NM_Module_t g_modules[NM_MAX_MODULES];
static uint8_t g_module_count = 0;

/* ============== CAN ID Builder ============== */
/**
 * @brief Build CAN ID for NewModule
 */
static uint32_t nm_build_can_id(uint8_t module_addr) {
    return NM_CMD_BASE | (module_addr & 0x7FU);
}

/* ============== Helper Functions ============== */
static uint32_t now_tick(void) {
    extern uint32_t HAL_GetTick(void);
    return HAL_GetTick();
}

/* ============== CHG_DriverOps_t Implementation ============== */
static void nm_init(void) {
    memset(g_modules, 0, sizeof(g_modules));
    g_module_count = 0;
}

static int8_t nm_add_module(uint8_t addr, uint8_t group) {
    if (g_module_count >= NM_MAX_MODULES) return -1;
    g_modules[g_module_count].view.addr = addr;
    g_modules[g_module_count].view.group = group;
    g_modules[g_module_count].view.enabled = true;
    return g_module_count++;
}

// ... implement all 12 functions ...

/* ============== Ops Table ============== */
static const CHG_DriverOps_t g_newmodule_ops = {
    .name = "newmodule",
    .init = nm_init,
    .add_module = nm_add_module,
    // ... all functions
};

const CHG_DriverOps_t *CHG_NewmoduleDriverOps(void) {
    return &g_newmodule_ops;
}
```

---

## 6. Protocol Requirements

### 6.1 Mỗi Driver PHẢI tự implement:

1. **CAN ID Builder** - Vì mỗi module dùng format khác nhau:
   - Maxwell: `mxr_build_frame_id(dst, src, ptp, group)`
   - Lianming: `lm_build_can_id(module_addr)`
   - TonHe: `tonhe_build_id(pf, ps, sa, priority)`

2. **Data Conversion** - Tùy protocol:
   - Maxwell: IEEE Float 32-bit (`CHG_ProtocolFloatToBE`)
   - Lianming: Integer mV/mA (`CHG_ProtocolU16ToBE`)
   - TonHe: Integer 0.1V/0.01A

3. **12 Functions của CHG_DriverOps_t:**
   - `init`, `add_module`, `remove_module`
   - `set_voltage`, `set_current_limit`
   - `start`, `stop`
   - `set_voltage_all`, `set_current_limit_all`, `start_all`, `stop_all`, `emergency_stop`
   - `process`, `feed_frame`
   - `get_system_summary`, `get_module_count`, `get_module_view`

---

## 7. Error Handling

### 7.1 Return Values

```c
// Boolean - operation success/fail
bool function_success(void);

// Index - module index or -1 if error
int8_t function_with_index(uint8_t idx);

// Pointer - valid pointer or NULL
Type_t *function_returns_pointer(void);
```

### 7.2 Null Checks

```c
// Always check pointers
if (driver == NULL || driver->init == NULL) {
    return false;
}

// Check array bounds
if (idx >= g_module_count) {
    return false;
}
```

---

## 8. Testing & Debug

### 8.1 CAN Frame Analysis

Luôn verify với tài liệu protocol gốc:
- Maxwell: `CAN Communication Protocol - Maxwell_V1.50.pdf`
- Lianming: `Lianming Power Digital Power Module CAN Communication Protocol V2.0.pdf`
- TonHe: `TonHeCANcommunicationbetweenchargingmoduleandmonitor TONHE V1.3.pdf`

### 8.2 Build Verification

```bash
# Build với warnings as errors
cmake -DCMAKE_C_FLAGS="-Werror" ..
make
```

---

## 9. Common Patterns

### 9.1 Periodic Processing

```c
// Trong main loop
void App_Loop(void) {
    static uint32_t last_tick = 0;
    uint32_t now = HAL_GetTick();

    if (now - last_tick >= PROCESS_INTERVAL_MS) {
        CHG_Process(now);  // Gọi driver process
        last_tick = now;
    }
}
```

### 9.2 CAN Frame Feeding

```c
// Trong CAN RX ISR
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan) {
    BSP_CAN_Frame_t frame;
    if (BSP_CAN_Receive(&frame, 0)) {
        CHG_FeedCanFrame(frame.ext_id, frame.data, frame.dlc);
    }
}
```

### 9.3 Module State Management

```c
// Set voltage - lưu setpoint, gửi command nếu đang chạy
bool nm_set_voltage(uint8_t idx, float voltage_v) {
    if (idx >= g_module_count) return false;

    g_modules[idx].view.voltage = voltage_v;

    if (g_modules[idx].view.state == CHG_STATE_RUNNING) {
        // Gửi command ngay
        return send_voltage_command(&g_modules[idx], voltage_v);
    }
    return true;  // Lưu setpoint, gửi khi start
}
```

---

## 10. Memory Considerations

### 10.1 Static Allocation

- **KHÔNG dùng malloc()**
- Dùng static arrays có kích thước cố định
- Định nghĩa MAX constants rõ ràng

```c
#define MAX_MODULES 8
static Module_t g_modules[MAX_MODULES];
```

### 10.2 Stack Usage

- Tránh recursion
- Large structures truyền bằng pointer
- Local arrays tối đa vài hundred bytes

---

## 11. Constants Definition

### 11.1 Nên define rõ ràng

```c
// ✅ Tốt
#define MXR_OFFLINE_TIMEOUT_MS  1500U
#define MXR_RESP_OK             0xF0U

// ❌ Không tốt
if (timeout > 1500)  // Magic number
if (status == 0xF0)  // Magic number
```

### 11.2 Bit Operations

```c
// ✅ Dùng shift để define flags
#define ALARM_OVER_TEMP    (1U << 2)
#define ALARM_OVER_VOLTAGE (1U << 3)

// ✅ Kiểm tra với bitwise
if (alarm_status & ALARM_OVER_TEMP) {
    // xử lý
}
```

---

## 12. Documentation

### 12.1 File Header

```c
/**
 * @file driver_xxx.c
 * @brief XXX Charging Module Driver
 * @note Protocol: X.X - CAN 2.0B Extended Frame, 125Kbps
 *
 * Hardware Interface:
 *   - CAN: 125Kbps, Extended 29-bit
 *   - Isolation: Required
 *
 * CAN Frame Format:
 *   [Mô tả]
 *
 * References:
 *   - "Protocol_Document.pdf"
 */
```

### 12.2 Function Comments

```c
/**
 * @brief Set output voltage for module
 * @param idx Module index (0 to module_count-1)
 * @param voltage_v Target voltage in Volts
 * @return true if successful, false if invalid index
 */
bool driver_set_voltage(uint8_t idx, float voltage_v);
```

---

## 13. Quick Reference

### 13.1 Thêm Driver Mới

1. Tạo `driver_xxx.h` và `driver_xxx.c`
2. Implement `CHG_DriverOps_t` (12 functions)
3. Thêm vào `app_charger.c`:
   - Include header
   - `CHG_RegisterDriver()`
4. Rebuild

### 13.2 Sửa Protocol

1. Verify với tài liệu PDF gốc
2. Sửa trong driver tương ứng
3. Thêm test case nếu cần
4. Update documentation

### 13.3 Debug Common Issues

| Vấn đề | Check |
|---------|-------|
| Module không respond | CAN ID đúng format? |
| Data sai | Byte order (big-endian)? |
| Timeout liên tục | Wiring, baudrate? |
| Crash | Null pointer, array bounds? |

---

## 14. BMS CAN Driver (CAN2)

### 14.1 Tổng Quan

BMS giao tiếp qua **CAN2** (250Kbps), tách biệt hoàn toàn với CAN1 dùng cho charger modules.

```
CAN1 (125Kbps, extended)  ──→  Module sạc (Maxwell, Lianming, TonHe)
CAN2 (250Kbps, std+ext)   ←──  BMS pin
```

### 14.2 CAN Settings

| Thông số | Giá trị |
|----------|----------|
| Baudrate | 250 Kbps |
| CAN2 prescaler | 21 (BS1=13, BS2=2, SJW=1) |
| GPIO | PB12=RX, PB13=TX |
| Byte order | Little-endian |
| Frame types | Standard (11-bit) + Extended (29-bit) |

### 14.3 Message IDs (9 loại)

**Standard frames (11-bit):**

| ID | Tên | Cycle | Mô tả |
|----|-----|-------|--------|
| 0x02F4 | BATT_ST1 | 20ms | Voltage, Current, SOC |
| 0x04F4 | CELL_VOLT | 100ms | Max/Min cell voltage |
| 0x05F4 | CELL_TEMP | 500ms | Max/Min/Avg temperature |
| 0x07F4 | ALM_INFO | Event | Alarm flags (severity 0-3) |

**Extended frames (29-bit):**

| ID | Tên | Cycle | Mô tả |
|----|-----|-------|--------|
| 0x18F128F4 | BATT_ST2 | 100ms | Capacity, Cycle count, SOH |
| 0x18F0F472 | ChgRequest | 1000ms | BMS yêu cầu voltage/current sạc |
| 0x18F528F4 | BmsSwSta | 500ms | Relay status (pre-discharge, discharge, charge) |
| 0x18E0XXF4 | CELL_VOLT_FULL | 1000ms | Cell voltage chi tiết (8 frames × 4 cells = 32 cells) |
| 0x18F228F4 | CELL_TEMP_FULL | 1000ms | Temperature chi tiết (Relay, Shunt, 6 cells) |

### 14.4 Data Access

```c
// Lấy snapshot BMS data (gọi bất kỳ lúc nào trong main loop)
BMS_View_t view;
BMS_GetView(&view);

// Các trường có sẵn:
view.state              // BMS_STATE_OFFLINE / ONLINE / FAULT
view.online             // true nếu BMS đang respond
view.batt_voltage       // V (float)
view.batt_current       // A (float, âm = discharge)
view.soc                // % (0-100)
view.cap_remain         // Ah × 0.1
view.soh                // % (0-100)
view.max_cell_volt      // mV
view.min_cell_volt      // mV
view.max_cell_temp      // °C
view.charge_relay_closed // bool
view.alarm_flags        // BMS_ALARM_* bitmask
view.chg_volt_request   // V (từ BMS ChgRequest)
view.chg_curr_request   // A
```

### 14.5 Alarm Flags

```c
BMS_ALARM_LOW_PACK_VOLT   // (1 << 0)
BMS_ALARM_LOW_CELL_VOLT   // (1 << 1)
BMS_ALARM_HIGH_PACK_VOLT  // (1 << 2)
BMS_ALARM_HIGH_CELL_VOLT  // (1 << 3)
BMS_ALARM_TEMP_HIGH_CHG   // (1 << 4)
BMS_ALARM_OVER_CHG_CURR   // (1 << 9)
BMS_ALARM_BMS_OFFLINE     // (1 << 13)
BMS_ALARM_STALE_DATA      // (1 << 14)

// Check alarm:
if (view.alarm_flags & BMS_ALARM_HIGH_CELL_VOLT) { ... }
```

### 14.6 Điều khiển BMS (Ctrl_INFO)

Gửi lệnh cho phép/cấm sạc/xả đến BMS:

```c
// Bật cho phép sạc
BMS_ChargeCtrl_t ctrl = {
    .allow_charge = true,
    .allow_discharge = true
};
BMS_SendCtrlInfo(&ctrl);  // Gửi ngay lập tức

// Hoặc tắt sạc
ctrl.allow_charge = false;
BMS_SendCtrlInfo(&ctrl);
```

Driver tự động gửi Ctrl_INFO mỗi 500ms (để keep-alive).

### 14.7 Integration Flow

```c
// Trong app_charger.c:
void App_Init(void) {
    BSP_CAN2_Start();     // Khởi tạo CAN2 hardware
    BMS_Init();           // Khởi tạo BMS driver
}

void App_Loop(void) {
    // BMS_Process mỗi 20ms (cùng CHG_Process)
    if ((now - last_tick) >= 20) {
        CHG_Process(now);
        BMS_Process(now);  // Timeout check, Ctrl_INFO TX
    }
}

// CAN2 RX Interrupt → BMS_FeedFrame
void App_CAN2_RxCallback(void) {
    // Nhận frame, phân biệt standard/extended
    BMS_FeedFrame(ext_id, std_id, data, dlc);
}
```

### 14.8 Raw → Physical Conversions

```c
// Định nghĩa trong bms_protocol.h
BMS_RAW_TO_VOLT(raw)   // raw × 0.1f → V
BMS_RAW_TO_CURR(raw)   // (raw × 0.1f - 400) → A (offset -400)
BMS_RAW_TO_TEMP(raw)   // raw - 50 → °C
BMS_RAW_TO_CAP_AH(raw) // raw × 0.1f → Ah
```

### 14.9 Design Notes

- BMS driver **độc lập** với charger driver — không share state
- BMS giao tiếp CAN2 trực tiếp với HAL (không qua `bsp_can.c`)
- `valid` flag trong mỗi message struct: `true` khi frame đã được parse ít nhất 1 lần
- Timeout BMS offline: **5s** không nhận frame nào → `BMS_STATE_OFFLINE`
- Stale data threshold: **2s** không nhận frame nào → báo `BMS_ALARM_STALE_DATA`

---

## 15. Related Files

| File | Purpose |
|------|---------|
| `docs/PROJECT_DOCUMENTATION.md` | Tài liệu tổng hợp |
| `CAN BMS_BB_PKG V1.0.md` | BMS CAN protocol spec (250Kbps, 9 message types) |
| `charger/App/Inc/charger_core.h` | Charger abstract interface |
| `charger/App/Inc/bms_core.h` | BMS driver public API |
| `charger/App/Inc/bms_protocol.h` | BMS CAN IDs, types, parse declarations |
| `charger/App/Inc/charger_protocol.h` | Charger helpers |
| `charger/App/Inc/driver_*.h` | Charger module drivers |
| `charger/App/Src/app_charger.c` | Application layer (BMS_Init, BMS_Process, App_CAN2_RxCallback) |
| `charger/App/Src/bms_core.c` | BMS state machine, timeout, Ctrl_INFO TX |
| `charger/App/Src/bms_protocol.c` | BMS CAN frame parser |

| File | Purpose |
|------|---------|
| `docs/PROJECT_DOCUMENTATION.md` | Tài liệu tổng hợp |
| `CAN BMS_BB_PKG V1.0.md` | BMS CAN protocol spec (250Kbps, 9 message types) |
| `charger/App/Inc/charger_core.h` | Charger abstract interface |
| `charger/App/Inc/bms_core.h` | BMS driver public API |
| `charger/App/Inc/bms_protocol.h` | BMS CAN IDs, types, parse declarations |
| `charger/App/Inc/charger_protocol.h` | Charger helpers |
| `charger/App/Inc/driver_*.h` | Charger module drivers |
| `charger/App/Src/app_charger.c` | Application layer (BMS_Init, BMS_Process, App_CAN2_RxCallback) |
| `charger/App/Src/bms_core.c` | BMS state machine, timeout, Ctrl_INFO TX |
| `charger/App/Src/bms_protocol.c` | BMS CAN frame parser |

---

## 16. Design Philosophy

> **"Clean abstractions, clear boundaries, consistent patterns"**

1. **Separation of Concerns** - Mỗi layer có nhiệm vụ rõ ràng
2. **Dependency Inversion** - Upper layer phụ thuộc interface, không implementation
3. **Single Responsibility** - Mỗi driver chỉ làm việc với 1 loại module
4. **Open/Closed** - Thêm driver mới không sửa existing code
5. **Memory Safety** - Static allocation, null checks, bounds checking

---

## 17. Embedded Coding Standards (MISRA C + Best Practices)

### 16.1 Tổng Quan

Tiêu chuẩn code cho dự án này kết hợp:
- **MISRA C:2012** - Essential subset cho safety-critical
- **Simple C** - Tránh phức tạp, ưu tiên readability
- **Company Best Practices** - Embedded specific rules

### 16.2 Variables & Types

```c
// ✅ Dùng fixed-width types cho embedded
#include <stdint.h>

uint32_t timeout_ms;    // ✅ Rõ ràng, portable
uint8_t  flags;        // ✅

int i;                 // ❌ Không rõ kích thước
long data;             // ❌ Platform-dependent

// ✅ Typedef cho opaque types
typedef uint32_t tick_ms_t;
typedef uint16_t adc_raw_t;

// ✅ Enum đủ size
typedef enum {
    STATE_IDLE = 0,
    STATE_RUNNING = 1,
    STATE_ERROR = 2
} module_state_t;  // Should be uint8_t if < 256 values
```

### 16.3 Arithmetic

```c
// ✅ Tránh overflow
uint32_t timeout = 1000U;  // Use U suffix
uint32_t elapsed = now - last;  // Works if now >= last

// ❌ Dangerous - overflow possible
uint16_t count = 65535U;
count++;  // Wraps to 0!

// ✅ Dùng uint32_t cho counters hoặc timestamps
uint32_t counter;
counter++;

// ✅ Division - đảm bảo không chia cho 0
if (divisor != 0) {
    result = numerator / divisor;
}
```

### 16.4 Pointer Usage

```c
// ✅ Always check NULL
void process(uint8_t *data, uint16_t len) {
    if (data == NULL) {
        return;
    }
    // ...
}

// ✅ Const correctness
void transmit(const uint8_t *data, uint16_t len);  // Won't modify

// ✅ NULL after free (if dynamic was needed)
uint8_t *buf = allocate(100);
if (buf != NULL) {
    use(buf);
    buf = NULL;  // Defensive
}
```

### 16.5 Control Flow

```c
// ✅ Always use braces
if (condition) {
    do_something();
}

// ❌ Error-prone
if (condition)
    do_something();

// ✅ Prefer early returns
bool validate(const Config *cfg) {
    if (cfg == NULL) return false;
    if (cfg->timeout == 0) return false;
    if (cfg->max_retries > 10) return false;
    return true;
}

// ✅ Simple switch - consider if/else if few cases
switch (state) {
    case STATE_IDLE:    handle_idle(); break;
    case STATE_RUNNING: handle_running(); break;
    case STATE_ERROR:   handle_error(); break;
    default:            /* Should not happen */ break;
}
```

### 16.6 Functions

```c
// ✅ Small, focused functions - tối đa 50-100 lines
// ✅ Functions do one thing well

// ✅ Parameter count - tối đa 4-5 parameters
// If more needed, use structure
bool configure_module(uint8_t addr, const ModuleConfig *cfg);
// ✅

// ✅ Static functions first, then public
static void internal_helper(void);
void public_api_function(void);

// ❌ Too many parameters
bool setup(int a, int b, int c, int d, int e, int f, int g);
```

### 16.7 Complexity - KISS (Keep It Simple, Stupid)

```c
// ✅ Simple, readable code
if (flags & FLAG_ENABLED) {
    start_module();
}

// ❌ Over-engineered - không cần thiết
typedef enum {
    FLAG_DISABLED = 0,
    FLAG_ENABLED_BIT = 1,
    FLAG_ENABLED_MASK = (1 << FLAG_ENABLED_BIT),
    FLAG_ENABLED_SET = FLAG_ENABLED_MASK
} FlagState;

// ✅ Linear is better than clever
for (uint8_t i = 0; i < count; i++) {
    process_item(items[i]);
}

// ❌ Bit manipulation không cần thiết
#define ITEM_PROCESSED(i) (processed[(i)/8] |= (1 << ((i)%8)))
```

### 16.8 Avoid

```c
// ❌ No magic numbers
if (timeout > 1000)  // Bad
if (timeout > DEFAULT_TIMEOUT_MS)  // Good

// ❌ No complex expressions in macros
#define MAX(a, b) ((a) > (b) ? (a) : (b))  // OK but avoid

// ❌ No function-like macros - use inline instead
static inline uint32_t max_u32(uint32_t a, uint32_t b) {
    return (a > b) ? a : b;
}

// ❌ No recursion
// ❌ No dynamic allocation (malloc)
// ❌ No floating point in ISR
// ❌ No blocking in ISR
```

### 16.9 Comments

```c
// ✅ What, not how
// Timeout exceeded - restart module
if (elapsed > TIMEOUT_MS) {
    restart();
}

// ❌ Redundant
i++;  // Increment i

// ✅ Explain hardware quirks
// Note: DCDC requires 50ms after voltage set before current can change
delay_ms(50);
```

### 16.10 MISRA Essentials

| Rule | Description | Example |
|------|-------------|---------|
| R.10.1 | Operands of &&, \|\| not have side effects | `if (ptr && ptr->valid)` ✅ |
| R.12.2 | Expression not depend on precedence | `a = b + c * d;` ❌ |
| R.13.1 | Init variables before use | `uint32_t now = HAL_GetTick();` ✅ |
| R.14.1 | Always have default in switch | `default: break;` ✅ |
| R.15.1 | Braces for all blocks | `if (x) { y; }` ✅ |
| R.16.1 | All functions have prototype | `void init(void);` ✅ |
| R.17.1 | No recursion | ✅ We use loops |
| R.21.1 | No malloc/calloc | ✅ We use static |
| R.22.1 | Check return of non-void functions | `if (!init()) return ERROR;` ✅ |

### 16.11 Compiler Warnings

```cmake
# CMakeLists.txt - enable all warnings
set(CMAKE_C_FLAGS_DEBUG "-Wall -Wextra -Wpedantic -Werror")
set(CMAKE_C_FLAGS_RELEASE "-Wall -Wextra -Wpedantic")
```

---

## 18. Code Review Checklist

Trước khi commit/PR:

- [ ] Build không có warnings/errors
- [ ] No magic numbers - all defined
- [ ] No new dynamic allocation
- [ ] Functions < 100 lines
- [ ] Meaningful variable names
- [ ] Comments explain "why", not "what"
- [ ] Error cases handled
- [ ] NULL checks on pointers
- [ ] Bounds checks on arrays
- [ ] Tested with static analysis

---

## 19. Common Embedded Pitfalls

| Pitfall | Prevention |
|---------|------------|
| **Integer overflow** | Use larger type or check bounds |
| **Missing bounds check** | Always validate array index |
| **Race condition** | Disable interrupts or use atomic ops |
| **Stack overflow** | Keep functions small, no deep recursion |
| **Memory leak** | No malloc - use static |
| **Uninitialized variables** | Always initialize |
| **Wrong byte order** | Use conversion helpers |
| **Watchdog timeout** | Pet dog in main loop |

---

## 20. Performance Tips

```c
// ✅ Use local variables for frequently accessed data
uint32_t count = module_count;  // Cache in register
for (uint8_t i = 0; i < count; i++) {
    process(g_modules[i]);
}

// ✅ Prefer unsigned for counts/indexes
uint8_t index;

// ✅ Use appropriate data structures
// Linear search OK for small arrays (< 20)
// For larger, consider sorted + binary search

// ✅ Avoid division in loops
// Pre-calculate if possible
divisor = 1000;
for (...) {
    result = value / divisor;  // OK - constant
}
```

---

*Last updated: 2026-06-24*
*Maintainers: Firmware Team*
