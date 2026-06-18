/**
 * @file    bsp_uart.h
 * @brief   UART Debug output - printf redirect
 * @note    USART1 PA9(TX)/PA10(RX), 115200 baud
 */

#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief  Khởi tạo USART1 cho debug output
 */
bool BSP_UART_Init(void);

/**
 * @brief  Gửi string qua UART (blocking)
 */
void BSP_UART_Print(const char *str);

/**
 * @brief  Printf-style output qua UART
 */
void BSP_UART_Printf(const char *fmt, ...);

#endif /* BSP_UART_H */
