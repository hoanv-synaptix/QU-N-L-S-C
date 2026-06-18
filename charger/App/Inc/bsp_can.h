/**
 * @file    bsp_can.h
 * @brief   CAN BSP wrapper - sử dụng trực tiếp HAL CAN từ CubeMX
 * @note    Adapter giữa HAL CAN (hcan1) và maxwell_charger driver.
 *          Cung cấp interface BSP_CAN_Transmit mà driver cần.
 */

#ifndef BSP_CAN_H
#define BSP_CAN_H

#include <stdint.h>
#include <stdbool.h>

/* ============== Types ============== */

typedef struct {
    uint32_t ext_id;        /* 29-bit extended identifier */
    uint8_t  data[8];       /* Data payload */
    uint8_t  dlc;           /* Data length code (0-8) */
} BSP_CAN_Frame_t;

/* ============== API ============== */

/**
 * @brief  Khởi tạo CAN filter + start CAN + enable RX interrupt
 *         Gọi SAU MX_CAN1_Init() (CubeMX đã cấu hình 125Kbps)
 * @retval true nếu thành công
 */
bool BSP_CAN_Start(void);

/**
 * @brief  Gửi 1 CAN frame (extended ID)
 * @param  frame  Pointer tới frame cần gửi
 * @retval true nếu đưa vào TX mailbox thành công
 */
bool BSP_CAN_Transmit(const BSP_CAN_Frame_t *frame);

/**
 * @brief  Khởi tạo CAN2 (dùng cho Mock)
 */
bool BSP_CAN2_Start(void);

/**
 * @brief  Xử lý gói tin nhận được từ CAN1 qua CAN2, và phản hồi lại bằng CAN2
 */
void Mock_CAN2_Process_RX(uint32_t ext_id, const uint8_t *data, uint8_t dlc);

#endif /* BSP_CAN_H */
