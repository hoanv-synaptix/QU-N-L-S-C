/**
 * @file    bsp_can.h
 * @brief   CAN Bus Abstraction Layer for STM32F407
 * @note    Wrapper trên HAL CAN, cung cấp interface chung cho upper layer.
 *          Dễ port sang MCU khác bằng cách thay file này.
 */

#ifndef BSP_CAN_H
#define BSP_CAN_H

#include <stdint.h>
#include <stdbool.h>

/* ============== Configuration ============== */
#define BSP_CAN_INSTANCE       CAN1
#define BSP_CAN_BAUDRATE       125000U   /* 125 Kbps cho Maxwell module */
#define BSP_CAN_TX_PIN         GPIO_PIN_9
#define BSP_CAN_TX_PORT        GPIOB
#define BSP_CAN_RX_PIN         GPIO_PIN_8
#define BSP_CAN_RX_PORT        GPIOB

/* ============== Types ============== */

typedef struct {
    uint32_t ext_id;        /* 29-bit extended identifier */
    uint8_t  data[8];       /* Data payload */
    uint8_t  dlc;           /* Data length code (0-8) */
} BSP_CAN_Frame_t;

/* Callback khi nhận frame */
typedef void (*BSP_CAN_RxCallback_t)(BSP_CAN_Frame_t *frame);

/* ============== API ============== */

/**
 * @brief  Khởi tạo CAN peripheral (125Kbps, extended frame)
 * @retval true nếu thành công
 */
bool BSP_CAN_Init(void);

/**
 * @brief  Đăng ký callback nhận frame
 * @param  cb  Function pointer được gọi từ ISR/polling context
 */
void BSP_CAN_RegisterRxCallback(BSP_CAN_RxCallback_t cb);

/**
 * @brief  Gửi 1 CAN frame
 * @param  frame  Con trỏ tới frame cần gửi
 * @retval true nếu gửi thành công (đưa vào TX mailbox)
 */
bool BSP_CAN_Transmit(const BSP_CAN_Frame_t *frame);

/**
 * @brief  Cấu hình filter cho extended ID
 * @param  filter_id   ID muốn nhận
 * @param  filter_mask Mask (bit=1 phải match)
 * @retval true nếu thành công
 */
bool BSP_CAN_SetFilter(uint32_t filter_id, uint32_t filter_mask);

/**
 * @brief  Polling receive (dùng khi chưa có RTOS)
 * @param  frame  Buffer nhận frame
 * @param  timeout_ms  Thời gian chờ tối đa
 * @retval true nếu nhận được frame
 */
bool BSP_CAN_Receive(BSP_CAN_Frame_t *frame, uint32_t timeout_ms);

#endif /* BSP_CAN_H */
