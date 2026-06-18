/**
 * @file    app_charger.c
 * @brief   Application layer — main loop logic
 */

#include "app_charger.h"
#include "bsp_can.h"
#include "maxwell_charger.h"
#include "pc_protocol.h"
#include "debug_log.h"
#include "main.h"
#include "can.h"
#include "stm32f4xx_hal.h"

/* ============== Configuration ============== */

#define APP_MODULE_ADDR         0x00    /* DIP switch trên module đầu tiên */
#define APP_MODULE_GROUP        0x00
#define APP_PROCESS_INTERVAL_MS 20      /* MXR_Process mỗi 20ms */
#define APP_STATUS_INTERVAL_MS  200     /* Gửi status về PC mỗi 200ms */
#define APP_LED_INTERVAL_MS     100     /* Cập nhật LED mỗi 100ms */
#define APP_BTN_DEBOUNCE_MS     50      /* Debounce nút nhấn */

/* ============== Private state ============== */

static uint32_t last_process_tick = 0;
static uint32_t last_status_tick  = 0;
static uint32_t last_led_tick     = 0;

/* Button debounce */
static uint32_t btn_start_last    = 0;
static uint32_t btn_stop_last     = 0;
static uint8_t  btn_start_prev    = 0;
static uint8_t  btn_stop_prev     = 0;

/* ============== LED control (active LOW) ============== */

static void led_run_on(void)   { HAL_GPIO_WritePin(LED_RUN_GPIO_Port, LED_RUN_Pin, GPIO_PIN_RESET); }
static void led_run_off(void)  { HAL_GPIO_WritePin(LED_RUN_GPIO_Port, LED_RUN_Pin, GPIO_PIN_SET); }
static void led_fault_on(void) { HAL_GPIO_WritePin(LED_FAULT_GPIO_Port, LED_FAULT_Pin, GPIO_PIN_RESET); }
static void led_fault_off(void){ HAL_GPIO_WritePin(LED_FAULT_GPIO_Port, LED_FAULT_Pin, GPIO_PIN_SET); }

/* ============== Button read ============== */

/* BTN_START = PA0, active HIGH (nhấn = 1) */
static uint8_t read_btn_start(void) {
    return HAL_GPIO_ReadPin(BTN_START_GPIO_Port, BTN_START_Pin) == GPIO_PIN_SET ? 1 : 0;
}

/* BTN_STOP = PE4, active LOW (nhấn = 0) */
static uint8_t read_btn_stop(void) {
    return HAL_GPIO_ReadPin(BTN_STOP_GPIO_Port, BTN_STOP_Pin) == GPIO_PIN_RESET ? 1 : 0;
}
/* ============== Init ============== */

void App_Init(void)
{
    /* In banner khởi động */
    LOG_Banner();
    LOG("App_Init: Khoi dong he thong...\r\n");

    /* LED tắt hết khi khởi động */
    led_run_off();
    led_fault_off();

    /* Start CAN (filter + interrupt) */
    LOG("App_Init: Khoi dong CAN bus...\r\n");
    if (!BSP_CAN_Start()) {
        /* CAN init fail -> bật LED fault */
        LOG("App_Init: LOI - Khong the khoi dong CAN bus!\r\n");
        led_fault_on();
        return;
    }
    LOG("App_Init: CAN bus khoi dong thanh cong.\r\n");

    /* Maxwell driver init + add module mặc định */
    LOG("App_Init: Khoi tao Maxwell driver, them module (addr=0x%02X)\r\n", APP_MODULE_ADDR);
    MXR_Init();
    MXR_AddModule(APP_MODULE_ADDR, APP_MODULE_GROUP);

    /* Khởi động MOCK CAN2 (Mạch giả lập module Maxwell) */
    BSP_CAN2_Start();
    LOG("App_Init: Da khoi dong CAN2 Mock.\r\n");

    LOG("App_Init: Hoan tat khoi tao.\r\n");
}

/* ============== Main Loop ============== */

void App_Loop(void)
{
    uint32_t now = HAL_GetTick();

    /* (1) Maxwell process — poll, FSM, timeout detection */
    if ((now - last_process_tick) >= APP_PROCESS_INTERVAL_MS) {
        last_process_tick = now;
        MXR_Process(now);
    }

    /* (2) Gửi status report về PC */
    if ((now - last_status_tick) >= APP_STATUS_INTERVAL_MS) {
        last_status_tick = now;
        PC_Protocol_SendStatus();
    }

    /* (3) Xử lý nút nhấn với debounce */
    {
        uint8_t start_raw = read_btn_start();
        uint8_t stop_raw  = read_btn_stop();

        /* BTN_START: rising edge (nhấn) */
        if (start_raw && !btn_start_prev && (now - btn_start_last) > APP_BTN_DEBOUNCE_MS) {
            btn_start_last = now;
            LOG("App_Loop: Nhan nut START -> Khoi dong tat ca module\r\n");
            /* Start tất cả module */
            MXR_StartAll();
        }
        btn_start_prev = start_raw;

        /* BTN_STOP: rising edge (nhấn) */
        if (stop_raw && !btn_stop_prev && (now - btn_stop_last) > APP_BTN_DEBOUNCE_MS) {
            btn_stop_last = now;
            LOG("App_Loop: Nhan nut STOP -> Dung tat ca module\r\n");
            /* Stop tất cả module */
            MXR_StopAll();
        }
        btn_stop_prev = stop_raw;
    }

    /* (4) Cập nhật LED */
    if ((now - last_led_tick) >= APP_LED_INTERVAL_MS) {
        last_led_tick = now;

        MXR_SystemSummary_t sum;
        MXR_GetSystemSummary(&sum);

        /* LED_RUN: sáng khi có module online & đang sạc */
        if (sum.modules_online > 0 && PC_Protocol_IsCharging()) {
            led_run_on();
        } else {
            led_run_off();
        }

        /* LED_FAULT: sáng khi có lỗi hoặc mất kết nối với toàn bộ module */
        if (sum.any_critical || sum.modules_fault > 0 || sum.modules_online == 0) {
            led_fault_on();
        } else {
            led_fault_off();
        }
    }
}

/* ============== CAN RX Callback ============== */

void App_CAN_RxCallback(void)
{
    CAN_RxHeaderTypeDef header;
    uint8_t data[8];

    if (HAL_CAN_GetRxMessage(&hcan1, CAN_RX_FIFO0, &header, data) == HAL_OK) {
        if (header.IDE == CAN_ID_EXT) {
            LOG("[CAN RX] ID:%08lX Data:%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                header.ExtId, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
            MXR_FeedCanFrame(header.ExtId, data, (uint8_t)header.DLC);
        }
    }
}

void App_CAN2_RxCallback(void)
{
    extern CAN_HandleTypeDef hcan2;
    CAN_RxHeaderTypeDef header;
    uint8_t data[8];

    if (HAL_CAN_GetRxMessage(&hcan2, CAN_RX_FIFO0, &header, data) == HAL_OK) {
        if (header.IDE == CAN_ID_EXT) {
            extern void Mock_CAN2_Process_RX(uint32_t ext_id, const uint8_t *data, uint8_t dlc);
            Mock_CAN2_Process_RX(header.ExtId, data, (uint8_t)header.DLC);
        }
    }
}
