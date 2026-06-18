/**
 * @file    demo_main.h
 * @brief   Demo application interface
 */

#ifndef DEMO_MAIN_H
#define DEMO_MAIN_H

#include "bsp_can.h"

/** Khởi tạo demo: CAN, LED, Maxwell driver. Gọi 1 lần sau khi USB init. */
void Demo_Init(void);

/** Vòng lặp chính demo. Gọi liên tục trong while(1). */
void Demo_Loop(void);

/** CAN RX callback - được đăng ký với BSP_CAN. */
void Demo_OnCanFrame(BSP_CAN_Frame_t *frame);

#endif /* DEMO_MAIN_H */
