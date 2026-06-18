/**
 * @file    main_demo.c
 * @brief   Demo application - Test CAN communication with Maxwell MXR module
 * @note    Bare-metal super-loop. Kết nối:
 *          - STM32F407VET6 kit
 *          - SN65HVD230 CAN transceiver on PB8(RX)/PB9(TX)
 *          - USART1 PA9(TX) -> USB-TTL -> PC terminal (115200 baud)
 *          - Maxwell MXR module on CAN bus (125Kbps)
 *
 * Demo flow:
 *   1. Init peripherals
 *   2. Set output voltage = 48V (hoặc giá trị bạn muốn)
 *   3. Set current limit = 0.5 (50% rated current)
 *   4. Start module
 *   5. Poll và in V/I/Status mỗi 500ms
 *   6. Sau 30s -> Stop module
 */

#include "stm32f4xx_hal.h"
#include "bsp_can.h"
#include "bsp_uart.h"
#include "maxwell_charger.h"

/* ============== Configuration ============== */

#define DEMO_OUTPUT_VOLTAGE     48.0f    /* Điện áp đầu ra (V) - THAY ĐỔI THEO PIN */
#define DEMO_CURRENT_LIMIT      0.5f     /* 50% dòng định mức */
#define DEMO_MODULE_ADDR        0x00     /* Địa chỉ module (xem DIP switch) */
#define DEMO_GROUP              0        /* Group 0 */
#define DEMO_RUN_TIME_MS        30000    /* Chạy 30 giây rồi dừng */
#define DEMO_POLL_INTERVAL_MS   500      /* Poll mỗi 500ms */

/* ============== Private ============== */

static MXR_ModuleStatus_t module_status = {0};
static volatile uint32_t sys_tick = 0;

/* ============== System Clock Config ============== */

/**
 * @brief  Cấu hình clock: HSE 8MHz -> PLL -> 168MHz SYSCLK
 *         APB1 = 42MHz (cho CAN), APB2 = 84MHz
 */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM       = 8;     /* HSE/8 = 1MHz */
    osc.PLL.PLLN       = 336;   /* VCO = 336MHz */
    osc.PLL.PLLP       = RCC_PLLP_DIV2;  /* SYSCLK = 168MHz */
    osc.PLL.PLLQ       = 7;
    HAL_RCC_OscConfig(&osc);

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                         RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;    /* HCLK = 168MHz */
    clk.APB1CLKDivider = RCC_HCLK_DIV4;      /* APB1 = 42MHz */
    clk.APB2CLKDivider = RCC_HCLK_DIV2;      /* APB2 = 84MHz */
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5);
}

/* ============== CAN RX Handler ============== */

static void CAN_RxHandler(BSP_CAN_Frame_t *frame)
{
    MXR_Response_t resp;

    if (MXR_ParseResponse(frame->ext_id, frame->data, frame->dlc, &resp)) {
        MXR_UpdateStatus(&module_status, &resp);
        module_status.last_update_tick = HAL_GetTick();
    }
}

/* ============== Print Helpers ============== */

static void print_status(void)
{
    BSP_UART_Printf("\r\n===== Module Status =====\r\n");
    BSP_UART_Printf("Voltage : %.1f V\r\n", module_status.voltage);
    BSP_UART_Printf("Current : %.2f A\r\n", module_status.current);
    BSP_UART_Printf("Limit   : %.1f %%\r\n", module_status.current_limit * 100.0f);
    BSP_UART_Printf("Temp DC : %.1f C\r\n", module_status.temp_dcdc);
    BSP_UART_Printf("Temp Amb: %.1f C\r\n", module_status.temp_ambient);
    BSP_UART_Printf("Power In: %lu W\r\n", module_status.input_power);
    BSP_UART_Printf("Alarm   : 0x%08lX", module_status.alarm_status);

    if (module_status.alarm_status & MXR_ALARM_MODULE_FAULT)
        BSP_UART_Printf(" [FAULT]");
    if (module_status.alarm_status & MXR_ALARM_DCDC_OV)
        BSP_UART_Printf(" [OV]");
    if (module_status.alarm_status & MXR_ALARM_DCDC_OVERTEMP)
        BSP_UART_Printf(" [OVERTEMP]");
    if (module_status.alarm_status & MXR_ALARM_DCDC_OFF)
        BSP_UART_Printf(" [OFF]");
    if (module_status.alarm_status & MXR_ALARM_FAN_FAILURE)
        BSP_UART_Printf(" [FAN]");

    BSP_UART_Printf("\r\nOnline  : %s\r\n", module_status.is_online ? "YES" : "NO");
    BSP_UART_Printf("========================\r\n");
}

/* ============== Main ============== */

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    /* Init peripherals */
    BSP_UART_Init();
    BSP_UART_Printf("\r\n\r\n");
    BSP_UART_Printf("================================\r\n");
    BSP_UART_Printf("  Maxwell MXR CAN Demo v1.0\r\n");
    BSP_UART_Printf("  STM32F407VET6 + SN65HVD230\r\n");
    BSP_UART_Printf("================================\r\n");

    /* Init CAN */
    if (!BSP_CAN_Init()) {
        BSP_UART_Printf("[ERROR] CAN Init failed!\r\n");
        while (1) {}
    }
    BSP_UART_Printf("[OK] CAN1 initialized (125Kbps)\r\n");

    /* Register RX callback */
    BSP_CAN_RegisterRxCallback(CAN_RxHandler);

    /* Init Maxwell driver */
    MXR_Init(DEMO_MODULE_ADDR, DEMO_GROUP);
    BSP_UART_Printf("[OK] Maxwell driver init (addr=%d, group=%d)\r\n",
                    DEMO_MODULE_ADDR, DEMO_GROUP);

    /* Step 1: Set voltage */
    HAL_Delay(100);
    if (MXR_SetVoltage(DEMO_OUTPUT_VOLTAGE)) {
        BSP_UART_Printf("[OK] Set voltage = %.1f V\r\n", DEMO_OUTPUT_VOLTAGE);
    } else {
        BSP_UART_Printf("[ERROR] Set voltage failed\r\n");
    }

    /* Step 2: Set current limit */
    HAL_Delay(50);
    if (MXR_SetCurrentLimit(DEMO_CURRENT_LIMIT)) {
        BSP_UART_Printf("[OK] Set current limit = %.0f%%\r\n", DEMO_CURRENT_LIMIT * 100);
    } else {
        BSP_UART_Printf("[ERROR] Set current limit failed\r\n");
    }

    /* Step 3: Start module */
    HAL_Delay(50);
    if (MXR_Start()) {
        BSP_UART_Printf("[OK] Module START command sent\r\n");
    } else {
        BSP_UART_Printf("[ERROR] Start command failed\r\n");
    }

    BSP_UART_Printf("\r\n[INFO] Running for %d seconds...\r\n", DEMO_RUN_TIME_MS / 1000);

    /* Main loop: poll status */
    uint32_t start_tick = HAL_GetTick();
    uint32_t last_poll  = 0;
    uint32_t last_print = 0;
    bool     stopped    = false;

    while (1) {
        uint32_t now = HAL_GetTick();

        /* Poll module mỗi 100ms (7 registers -> ~700ms cho full cycle) */
        if ((now - last_poll) >= 100) {
            last_poll = now;
            MXR_PollStatus(&module_status);

            /* Check response bằng polling (backup cho interrupt) */
            BSP_CAN_Frame_t rx_frame;
            if (BSP_CAN_Receive(&rx_frame, 5)) {
                CAN_RxHandler(&rx_frame);
            }
        }

        /* In status mỗi 500ms */
        if ((now - last_print) >= DEMO_POLL_INTERVAL_MS) {
            last_print = now;
            print_status();
        }

        /* Dừng sau DEMO_RUN_TIME_MS */
        if (!stopped && (now - start_tick) >= DEMO_RUN_TIME_MS) {
            BSP_UART_Printf("\r\n[INFO] Time's up! Stopping module...\r\n");
            MXR_Stop();
            stopped = true;
            BSP_UART_Printf("[OK] Module STOP command sent\r\n");
            BSP_UART_Printf("[INFO] Demo complete. Status continues polling.\r\n");
        }
    }
}

/* ============== IRQ Handlers (cần thiết cho HAL) ============== */

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void CAN1_RX0_IRQHandler(void)
{
    extern CAN_HandleTypeDef hcan1;
    HAL_CAN_IRQHandler(&hcan1);
}
