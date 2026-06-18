/**
 * @file    pc_link_usb.c
 * @brief   Cầu nối giữa USB CDC (CubeMX generated) và pc_protocol
 * @note    Đặt 2 đoạn code này vào usbd_cdc_if.c do CubeMX sinh ra:
 *
 *  1. Trong CDC_Receive_FS(), thêm dòng feed byte:
 *
 *      static int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len)
 *      {
 *          for (uint32_t i = 0; i < *Len; i++) {
 *              PC_Protocol_FeedByte(Buf[i]);     // <-- thêm dòng này
 *          }
 *          USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
 *          USBD_CDC_ReceivePacket(&hUsbDeviceFS);
 *          return (USBD_OK);
 *      }
 *
 *  2. Implement hàm pc_link_send() dùng CDC_Transmit_FS:
 *     (có thể để nguyên trong file này nếu link được, hoặc copy vào usbd_cdc_if.c)
 *
 *  File này cung cấp sẵn pc_link_send() — chỉ cần thêm vào project build.
 */

#include "pc_protocol.h"
#include "usbd_cdc_if.h"
#include "usbd_def.h"

/* CDC_Transmit_FS có thể busy nếu USB chưa gửi xong gói trước.
 * Retry vài lần với timeout ngắn cho demo. Production nên dùng ring buffer. */
void pc_link_send(const uint8_t *data, uint16_t len)
{
    uint32_t retry = 1000;
    while (retry--) {
        if (CDC_Transmit_FS((uint8_t *)data, len) == USBD_OK) {
            return;
        }
        /* USBD_BUSY: chờ một chút rồi thử lại */
    }
    /* Nếu vẫn fail thì bỏ gói này (status report sẽ gửi lại ở chu kỳ sau) */
}
