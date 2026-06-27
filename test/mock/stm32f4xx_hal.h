#ifndef MOCK_STM32F4XX_HAL_H
#define MOCK_STM32F4XX_HAL_H

#include <stdint.h>

typedef struct {
    uint32_t ExtId;
    uint32_t IDE;
    uint32_t RTR;
    uint32_t DLC;
    uint32_t TransmitGlobalTime;
} CAN_TxHeaderTypeDef;

typedef struct {
    int dummy;
} CAN_HandleTypeDef;

typedef struct {
    int dummy;
} UART_HandleTypeDef;

#define CAN_ID_EXT 4
#define CAN_RTR_DATA 0
#define DISABLE 0
#define HAL_OK 0

extern uint32_t HAL_GetTick(void);

extern int HAL_CAN_AddTxMessage(CAN_HandleTypeDef *hcan, CAN_TxHeaderTypeDef *pHeader, uint8_t aData[], uint32_t *pTxMailbox);
extern int HAL_UART_Transmit(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size, uint32_t Timeout);

#endif
