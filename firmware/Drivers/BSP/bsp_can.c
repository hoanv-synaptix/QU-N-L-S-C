/**
 * @file    bsp_can.c
 * @brief   CAN Bus Abstraction Layer - STM32F407 HAL implementation
 * @note    CAN1 on PB8(RX)/PB9(TX), 125Kbps, Extended frame
 *          APB1 = 42MHz -> Prescaler=24, BS1=11, BS2=2 -> 125Kbps
 */

#include "bsp_can.h"
#include "stm32f4xx_hal.h"

/* ============== Private ============== */

static CAN_HandleTypeDef hcan1;
static BSP_CAN_RxCallback_t rx_callback = NULL;

/* ============== Implementation ============== */

bool BSP_CAN_Init(void)
{
    /* GPIO Clock */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_CAN1_CLK_ENABLE();

    /* Configure PB8=CAN1_RX, PB9=CAN1_TX as AF9 */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* CAN Init - 125Kbps
     * APB1 clock = 42 MHz (HCLK/4 with 168MHz SYSCLK)
     * BaudRate = APB1 / (Prescaler * (BS1 + BS2 + 1))
     * 125000 = 42000000 / (24 * (11 + 2 + 1))
     * 125000 = 42000000 / (24 * 14) = 42000000 / 336 = 125000 OK
     */
    hcan1.Instance                  = CAN1;
    hcan1.Init.Prescaler            = 24;
    hcan1.Init.Mode                 = CAN_MODE_NORMAL;
    hcan1.Init.SyncJumpWidth        = CAN_SJW_1TQ;
    hcan1.Init.TimeSeg1             = CAN_BS1_11TQ;
    hcan1.Init.TimeSeg2             = CAN_BS2_2TQ;
    hcan1.Init.TimeTriggeredMode    = DISABLE;
    hcan1.Init.AutoBusOff           = ENABLE;
    hcan1.Init.AutoWakeUp           = DISABLE;
    hcan1.Init.AutoRetransmission   = ENABLE;
    hcan1.Init.ReceiveFifoLocked    = DISABLE;
    hcan1.Init.TransmitFifoPriority = DISABLE;

    if (HAL_CAN_Init(&hcan1) != HAL_OK) {
        return false;
    }

    /* Default filter: accept all extended frames on FIFO0 */
    BSP_CAN_SetFilter(0x00000000, 0x00000000);

    /* Start CAN */
    if (HAL_CAN_Start(&hcan1) != HAL_OK) {
        return false;
    }

    /* Enable RX interrupt on FIFO0 */
    HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING);

    return true;
}

void BSP_CAN_RegisterRxCallback(BSP_CAN_RxCallback_t cb)
{
    rx_callback = cb;
}

bool BSP_CAN_Transmit(const BSP_CAN_Frame_t *frame)
{
    CAN_TxHeaderTypeDef tx_header;
    uint32_t tx_mailbox;

    tx_header.StdId = 0;
    tx_header.ExtId = frame->ext_id;
    tx_header.IDE   = CAN_ID_EXT;
    tx_header.RTR   = CAN_RTR_DATA;
    tx_header.DLC   = frame->dlc;
    tx_header.TransmitGlobalTime = DISABLE;

    if (HAL_CAN_AddTxMessage(&hcan1, &tx_header, (uint8_t *)frame->data, &tx_mailbox) != HAL_OK) {
        return false;
    }

    /* Chờ gửi xong (polling cho demo, sau chuyển sang async) */
    uint32_t tick = HAL_GetTick();
    while (HAL_CAN_IsTxMessagePending(&hcan1, tx_mailbox)) {
        if ((HAL_GetTick() - tick) > 100) {
            return false;  /* Timeout 100ms */
        }
    }

    return true;
}

bool BSP_CAN_SetFilter(uint32_t filter_id, uint32_t filter_mask)
{
    CAN_FilterTypeDef filter;

    filter.FilterBank           = 0;
    filter.FilterMode           = CAN_FILTERMODE_IDMASK;
    filter.FilterScale          = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh         = (uint16_t)((filter_id << 3) >> 16);
    filter.FilterIdLow          = (uint16_t)((filter_id << 3) & 0xFFFF) | CAN_ID_EXT;
    filter.FilterMaskIdHigh     = (uint16_t)((filter_mask << 3) >> 16);
    filter.FilterMaskIdLow      = (uint16_t)((filter_mask << 3) & 0xFFFF) | CAN_ID_EXT;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation     = ENABLE;
    filter.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&hcan1, &filter) != HAL_OK) {
        return false;
    }

    return true;
}

bool BSP_CAN_Receive(BSP_CAN_Frame_t *frame, uint32_t timeout_ms)
{
    uint32_t tick = HAL_GetTick();

    while ((HAL_GetTick() - tick) < timeout_ms) {
        if (HAL_CAN_GetRxFifoFillLevel(&hcan1, CAN_RX_FIFO0) > 0) {
            CAN_RxHeaderTypeDef rx_header;

            if (HAL_CAN_GetRxMessage(&hcan1, CAN_RX_FIFO0, &rx_header, frame->data) == HAL_OK) {
                frame->ext_id = rx_header.ExtId;
                frame->dlc    = rx_header.DLC;
                return true;
            }
        }
    }

    return false;
}

/* ============== ISR Handler ============== */

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    if (hcan->Instance == CAN1 && rx_callback != NULL) {
        BSP_CAN_Frame_t frame;
        CAN_RxHeaderTypeDef rx_header;

        if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, frame.data) == HAL_OK) {
            frame.ext_id = rx_header.ExtId;
            frame.dlc    = rx_header.DLC;
            rx_callback(&frame);
        }
    }
}
