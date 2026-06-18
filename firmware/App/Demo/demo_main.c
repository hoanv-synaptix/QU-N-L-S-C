/**
 * @file    demo_main.c
 * @brief   Demo application - điều khiển module sạc Maxwell qua PC App
 * @note    Bare-metal super-loop. Kiến trúc tách lớp:
 *            PC App <--USB CDC--> [pc_protocol] <--> [maxwell_charger] <--CAN--> Module
 *
 *          Vòng lặp chính lo 3 việc:
 *            1. Poll module liên tục (keep-alive + đọc V/I/alarm)
 *            2. Nhận & parse response CAN, cập nhật status
 *            3. Gửi status report về PC định kỳ
 *
 *          Cần gọi từ main(): Demo_Init() một lần, rồi Demo_Loop() trong while(1).
 *          USB RX callback gọi PC_Protocol_FeedByte() cho mỗi byte.
 *          CAN RX callback gọi Demo_OnCanFrame().
 */

#include "bsp_can.h"
#include "maxwell_charger.h"
#include "pc_protocol.h"
#include "stm32f4xx_hal.h"

/* ============== Cấu hình demo ============== */
#define DEMO_MODULE_ADDR        0x00    /* Địa chỉ module (theo DIP switch) */
#define DEMO_MODULE_GROUP       0x00
#define DEMO_POLL_INTERVAL_MS   100     /* Khoảng cách giữa 2 lần poll (keep-alive) */
#define DEMO_STATUS_INTERVAL_MS 200     /* Gửi status về PC mỗi 200ms */
#define DEMO_OFFLINE_TIMEOUT_MS 1000    /* Quá lâu không có response -> module offline */

/* ============== LED định nghĩa (theo schematic: D1/D2/D3 trên PD) ============== */
/* Đèn xanh = đang sạc, Đèn đỏ = lỗi. Điều chỉnh chân thực tế khi test. */
#define LED_GREEN_PORT          GPIOD
#define LED_GREEN_PIN           GPIO_PIN_12
#define LED_RED_PORT            GPIOD
#define LED_RED_PIN             GPIO_PIN_13

/* ============== State ============== */
static MXR_ModuleStatus_t *p_status;
static uint32_t last_poll_tick   = 0;
static uint32_t last_status_tick = 0;

/* ============== LED helpers ============== */

static void led_init(void)
{
    __HAL_RCC_GPIOD_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin   = LED_GREEN_PIN | LED_RED_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &g);
    HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_RED_PORT,   LED_RED_PIN,   GPIO_PIN_RESET);
}

static void led_update(void)
{
    /* Đèn đỏ: có alarm hoặc module offline */
    bool fault = (p_status->alarm_status != 0) || !p_status->is_online;
    HAL_GPIO_WritePin(LED_RED_PORT, LED_RED_PIN,
                      fault ? GPIO_PIN_SET : GPIO_PIN_RESET);

    /* Đèn xanh: đang sạc và không lỗi */
    bool charging_ok = PC_Protocol_IsCharging() && !fault;
    HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN,
                      charging_ok ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* ============== CAN RX callback ============== */
/* Gọi từ HAL_CAN_RxFifo0MsgPendingCallback (qua BSP_CAN registered callback) */

void Demo_OnCanFrame(BSP_CAN_Frame_t *frame)
{
    MXR_Response_t resp;
    if (MXR_ParseResponse(frame->ext_id, frame->data, frame->dlc, &resp)) {
        MXR_UpdateStatus(p_status, &resp);
        p_status->last_update_tick = HAL_GetTick();
    }
}

/* ============== Init ============== */

void Demo_Init(void)
{
    led_init();

    BSP_CAN_Init();
    BSP_CAN_RegisterRxCallback(Demo_OnCanFrame);

    MXR_Init(DEMO_MODULE_ADDR, DEMO_MODULE_GROUP);

    p_status = PC_Protocol_GetStatusBuffer();
    p_status->is_online        = false;
    p_status->last_update_tick = 0;

    last_poll_tick   = HAL_GetTick();
    last_status_tick = HAL_GetTick();
}

/* ============== Main loop ============== */

void Demo_Loop(void)
{
    uint32_t now = HAL_GetTick();

    /* (1) Poll module liên tục - đây cũng là keep-alive giữ module không tự tắt.
     *     Mỗi DEMO_POLL_INTERVAL_MS gửi 1 read request (vòng qua các register). */
    if ((now - last_poll_tick) >= DEMO_POLL_INTERVAL_MS) {
        last_poll_tick = now;
        MXR_PollStatus(p_status);   /* gửi 1 read request mỗi lần gọi */
    }

    /* (2) Kiểm tra offline: nếu quá lâu không có response */
    if ((now - p_status->last_update_tick) > DEMO_OFFLINE_TIMEOUT_MS) {
        p_status->is_online = false;
    }

    /* (3) Gửi status report về PC định kỳ */
    if ((now - last_status_tick) >= DEMO_STATUS_INTERVAL_MS) {
        last_status_tick = now;
        PC_Protocol_SendStatus();
    }

    /* (4) Cập nhật đèn LED */
    led_update();

    /* (5) Safety: nếu phát hiện alarm nghiêm trọng -> tự động Stop */
    if (PC_Protocol_IsCharging() && p_status->is_online) {
        uint32_t critical = MXR_ALARM_MODULE_FAULT | MXR_ALARM_DCDC_OV |
                            MXR_ALARM_SHORT_CIRCUIT | MXR_ALARM_DCDC_OVERTEMP |
                            MXR_ALARM_DCDC_OUTPUT_OV;
        if (p_status->alarm_status & critical) {
            MXR_Stop();
            /* g_charging trong pc_protocol vẫn =1; PC sẽ thấy alarm và quyết định.
             * Ở đây chỉ cắt output để bảo vệ phần cứng. */
        }
    }
}
