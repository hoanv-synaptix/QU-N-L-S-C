/**
 * @file    bsp_can.c
 * @brief   CAN BSP implementation - dùng hcan1 từ CubeMX (và hcan2 cho mock)
 */

#include "bsp_can.h"
#include "can.h"          /* CubeMX generated: hcan1, hcan2 */
#include "stm32f4xx_hal.h"
#include "debug_log.h"

/* ============== API ============== */

bool BSP_CAN_Start(void)
{
    /* Cấu hình filter: nhận TẤT CẢ extended frame (mask = 0) cho CAN1 */
    CAN_FilterTypeDef filter;
    filter.FilterBank           = 0;
    filter.FilterMode           = CAN_FILTERMODE_IDMASK;
    filter.FilterScale          = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh         = 0x0000;
    filter.FilterIdLow          = 0x0000;
    filter.FilterMaskIdHigh     = 0x0000;
    filter.FilterMaskIdLow      = 0x0000;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation     = ENABLE;
    filter.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&hcan1, &filter) != HAL_OK) {
        return false;
    }

    /* Start CAN */
    if (HAL_CAN_Start(&hcan1) != HAL_OK) {
        return false;
    }

    /* Enable RX FIFO0 message pending interrupt */
    if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
        return false;
    }

    return true;
}

bool BSP_CAN_Transmit(const BSP_CAN_Frame_t *frame)
{
    CAN_TxHeaderTypeDef header;
    uint32_t tx_mailbox;

    header.IDE   = CAN_ID_EXT;
    header.ExtId = frame->ext_id;
    header.RTR   = CAN_RTR_DATA;
    header.DLC   = frame->dlc;
    header.TransmitGlobalTime = DISABLE;

    if (HAL_CAN_AddTxMessage(&hcan1, &header, (uint8_t *)frame->data, &tx_mailbox) != HAL_OK) {
        LOG("[CAN TX FAIL] ID:%08lX\r\n", frame->ext_id);
        return false;
    }

    LOG("[CAN TX] ID:%08lX Data:%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
        frame->ext_id, frame->data[0], frame->data[1], frame->data[2], frame->data[3],
        frame->data[4], frame->data[5], frame->data[6], frame->data[7]);

    return true;
}

/* ============== CAN2 MOCK IMPLEMENTATION ============== */

extern CAN_HandleTypeDef hcan2;

bool BSP_CAN2_Start(void)
{
    /* Configure filter cho CAN2 (Bank 14 trở đi) */
    CAN_FilterTypeDef filter;
    filter.FilterBank           = 14; 
    filter.FilterMode           = CAN_FILTERMODE_IDMASK;
    filter.FilterScale          = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh         = 0x0000;
    filter.FilterIdLow          = 0x0000;
    filter.FilterMaskIdHigh     = 0x0000;
    filter.FilterMaskIdLow      = 0x0000;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation     = ENABLE;
    filter.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&hcan2, &filter) != HAL_OK) {
        return false;
    }

    if (HAL_CAN_Start(&hcan2) != HAL_OK) {
        return false;
    }

    if (HAL_CAN_ActivateNotification(&hcan2, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
        return false;
    }

    return true;
}

/* Biến trạng thái giả lập của Maxwell Module */
static float mock_voltage = 54.6f;
static float mock_current = 0.0f;
static float mock_limit = 1.0f;
static uint32_t mock_alarm = 0;
static float mock_temp = 35.5f;

void Mock_CAN2_Process_RX(uint32_t ext_id, const uint8_t *data, uint8_t dlc)
{
    /* Trích xuất destination address từ frame CAN1 gửi tới (bits 9-16) */
    uint8_t dst_addr = (uint8_t)((ext_id >> 9) & 0xFF);

    /* CHỈ NHẬN MODULE 0 VÀ 1 (Test hardware filtering) */
    if (dst_addr != 0x00 && dst_addr != 0x01) {
        return; /* Bỏ qua gói tin, giả lập module không tồn tại */
    }

    /* Tạo ID phản hồi từ Module (SRC) về Controller (DST) */
    uint32_t rx_id = 0;
    rx_id |= ((uint32_t)0x060 & 0x1FF) << 20;    /* PROTNO */
    rx_id |= ((uint32_t)1 & 0x01) << 19;         /* PTP=1 */
    rx_id |= ((uint32_t)0xF0 & 0xFF) << 9;      /* DST = Controller */
    rx_id |= ((uint32_t)dst_addr & 0xFF) << 1;   /* SRC = Module Address */
    rx_id |= 0;                                   /* GRP=0 */           
    
    uint8_t rx_data[8] = {0};
    rx_data[1] = 0xF0; /* OK Error code */
    rx_data[2] = data[2]; /* Reg High */
    rx_data[3] = data[3]; /* Reg Low */
    
    uint16_t reg = ((uint16_t)data[2] << 8) | data[3];
    uint8_t func = data[0]; 
    
    if (func == 0x03) { /* SET */
        if (reg == 0x0021) { /* Set Voltage */
            union { float f; uint8_t b[4]; } conv;
            conv.b[3] = data[4]; conv.b[2] = data[5];
            conv.b[1] = data[6]; conv.b[0] = data[7];
            mock_voltage = conv.f;
            rx_data[0] = 0x41;
            rx_data[4] = data[4]; rx_data[5] = data[5];
            rx_data[6] = data[6]; rx_data[7] = data[7];
        } else if (reg == 0x0022) { /* Set Current Limit */
            union { float f; uint8_t b[4]; } conv;
            conv.b[3] = data[4]; conv.b[2] = data[5];
            conv.b[1] = data[6]; conv.b[0] = data[7];
            mock_limit = conv.f;
            rx_data[0] = 0x41;
            rx_data[4] = data[4]; rx_data[5] = data[5];
            rx_data[6] = data[6]; rx_data[7] = data[7];
        } else if (reg == 0x0030) { /* Start / Stop */
            uint32_t val = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) | ((uint32_t)data[6] << 8) | data[7];
            if (val == 0) { mock_current = 15.0f * mock_limit; } /* START */
            else { mock_current = 0.0f; } /* STOP */
            rx_data[0] = 0x42;
            rx_data[4] = data[4]; rx_data[5] = data[5];
            rx_data[6] = data[6]; rx_data[7] = data[7];
        }
    } else if (func == 0x10) { /* READ */
        union { float f; uint8_t b[4]; } conv;
        uint32_t uval = 0;
        bool is_float = true;
        if (reg == 0x0001) conv.f = mock_voltage;
        else if (reg == 0x0002) conv.f = mock_current;
        else if (reg == 0x0003) conv.f = mock_limit;
        else if (reg == 0x0004 || reg == 0x000B) conv.f = mock_temp;
        else if (reg == 0x0040) { uval = mock_alarm; is_float = false; }
        else if (reg == 0x0048) { uval = (uint32_t)(mock_voltage * mock_current); is_float = false; }
        
        rx_data[0] = is_float ? 0x41 : 0x42;
        if (is_float) {
            rx_data[4] = conv.b[3]; rx_data[5] = conv.b[2];
            rx_data[6] = conv.b[1]; rx_data[7] = conv.b[0];
        } else {
            rx_data[4] = (uint8_t)(uval >> 24); rx_data[5] = (uint8_t)(uval >> 16);
            rx_data[6] = (uint8_t)(uval >> 8);  rx_data[7] = (uint8_t)(uval);
        }
    }
    
    /* Phát phản hồi lại qua CAN2 Mailbox */
    CAN_TxHeaderTypeDef header;
    uint32_t tx_mailbox;

    header.IDE   = CAN_ID_EXT;
    header.ExtId = rx_id;
    header.RTR   = CAN_RTR_DATA;
    header.DLC   = 8;
    header.TransmitGlobalTime = DISABLE;

    HAL_CAN_AddTxMessage(&h