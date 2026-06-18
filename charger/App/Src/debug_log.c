/**
 * @file    debug_log.c
 * @brief   Debug logging implementation qua USART1
 */

#include "debug_log.h"
#include "usart.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define LOG_BUF_SIZE  128

void LOG(const char *fmt, ...)
{
    char buf[LOG_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0) {
        if (len > (int)(sizeof(buf) - 1)) len = sizeof(buf) - 1;
        HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)len, 50);
    }
}

void LOG_Banner(void)
{
    LOG("\r\n\r\n");
    LOG("========================================\r\n");
    LOG("  Charger Controller v2.0\r\n");
    LOG("  STM32F407VET6 + Maxwell MXR\r\n");
    LOG("  CAN1: PD0/PD1 @ 125Kbps\r\n");
    LOG("  USB CDC: PA11/PA12\r\n");
    LOG("  UART1 Debug: PA9 TX @ 115200\r\n");
    LOG("========================================\r\n");
}
