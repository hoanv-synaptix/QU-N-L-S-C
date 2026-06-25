/**
 * @file    flash_storage.h
 * @brief   Flash storage for configuration persistence
 * @note    Uses STM32 Flash to store charger configuration
 *          Config stored in last 2 pages of Flash
 */

#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H

#include <stdint.h>
#include <stdbool.h>

/* ============== Configuration Storage ============== */

#define FLASH_CONFIG_PAGE_ADDR   0x0803E800  /* Last 2 pages */
#define FLASH_PAGE_SIZE          0x00000800  /* 2KB per page */
#define FLASH_CONFIG_MAGIC       0xDEADBEEF
#define FLASH_CONFIG_VERSION    0x00010000

/* ============== Storage Structure ============== */

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;           /* Validation magic (0xDEADBEEF) */
    uint32_t version;         /* Config version */
    uint32_t crc32;          /* CRC32 of data */
    uint32_t timestamp;      /* Last save timestamp */
    uint16_t data_len;       /* Length of config data */
    uint8_t  reserved[2];
    uint8_t  data[2000];     /* Config data */
} FlashStorage_t;
#pragma pack(pop)

/* ============== API ============== */

void FlashStorage_Init(void);
bool FlashStorage_Load(uint8_t *data, uint16_t *len);
bool FlashStorage_Save(const uint8_t *data, uint16_t len);
bool FlashStorage_IsValid(void);
bool FlashStorage_Erase(void);
uint32_t FlashStorage_GetTimestamp(void);

#endif /* FLASH_STORAGE_H */
