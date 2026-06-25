/**
 * @file    flash_storage.c
 * @brief   Flash storage implementation for configuration persistence
 * @note    Uses STM32F4xx HAL Flash API
 */

#include "flash_storage.h"
#include "stm32f4xx_hal.h"
#include <string.h>

/* ============== Private ============== */

#define FLASH_CONFIG_PAGE_ADDR   0x0803E800  /* Last 2 pages */

/* CRC32 implementation */
static uint32_t crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

/* ============== Flash Operations ============== */

static HAL_StatusTypeDef flash_erase_page(uint32_t page_address)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t page_error = 0;
    
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = FLASH_SECTOR_5;  /* Last sector */
    erase.NbSectors = 1;
    erase.VoltageRange = VOLTAGE_RANGE_3;
    
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase, &page_error);
    HAL_FLASH_Lock();
    
    return status;
}

static HAL_StatusTypeDef flash_write_data(uint32_t address, const uint8_t *data, uint32_t len)
{
    HAL_FLASH_Unlock();
    
    for (uint32_t i = 0; i < len; i += 4) {
        uint32_t word = 0;
        if (i + 3 < len) {
            word = (uint32_t)data[i] | 
                   ((uint32_t)data[i+1] << 8) |
                   ((uint32_t)data[i+2] << 16) |
                   ((uint32_t)data[i+3] << 24);
        } else {
            for (uint32_t j = 0; j < 4 && (i + j) < len; j++) {
                word |= ((uint32_t)data[i + j]) << (j * 8);
            }
        }
        
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + i, word) != HAL_OK) {
            HAL_FLASH_Lock();
            return HAL_ERROR;
        }
    }
    
    HAL_FLASH_Lock();
    return HAL_OK;
}

static void flash_read_data(uint32_t address, uint8_t *data, uint32_t len)
{
    memcpy(data, (void*)address, len);
}

/* ============== API ============== */

void FlashStorage_Init(void)
{
    /* Flash is ready to use */
}

bool FlashStorage_IsValid(void)
{
    FlashStorage_t header;
    flash_read_data(FLASH_CONFIG_PAGE_ADDR, (uint8_t*)&header, sizeof(header));
    
    if (header.magic != FLASH_CONFIG_MAGIC) {
        return false;
    }
    
    /* Verify CRC */
    uint32_t calc_crc = crc32(header.data, sizeof(header.data));
    return (calc_crc == header.crc32);
}

bool FlashStorage_Load(uint8_t *data, uint16_t *len)
{
    if (!FlashStorage_IsValid()) {
        return false;
    }
    
    FlashStorage_t header;
    flash_read_data(FLASH_CONFIG_PAGE_ADDR, (uint8_t*)&header, sizeof(header));
    
    uint16_t data_len = (header.data_len < *len) ? header.data_len : *len;
    memcpy(data, header.data, data_len);
    *len = data_len;
    
    return true;
}

bool FlashStorage_Save(const uint8_t *data, uint16_t len)
{
    if (len > sizeof(((FlashStorage_t*)0)->data)) {
        return false;
    }
    
    /* Erase page first */
    if (flash_erase_page(FLASH_CONFIG_PAGE_ADDR) != HAL_OK) {
        return false;
    }
    
    /* Build header */
    FlashStorage_t header;
    memset(&header, 0xFF, sizeof(header));
    header.magic = FLASH_CONFIG_MAGIC;
    header.version = FLASH_CONFIG_VERSION;
    header.data_len = len;
    header.timestamp = HAL_GetTick();
    memcpy(header.data, data, len);
    header.crc32 = crc32(header.data, len);
    
    /* Write to flash */
    if (flash_write_data(FLASH_CONFIG_PAGE_ADDR, (const uint8_t*)&header, sizeof(header)) != HAL_OK) {
        return false;
    }
    
    return true;
}

bool FlashStorage_Erase(void)
{
    return (flash_erase_page(FLASH_CONFIG_PAGE_ADDR) == HAL_OK);
}

uint32_t FlashStorage_GetTimestamp(void)
{
    FlashStorage_t header;
    flash_read_data(FLASH_CONFIG_PAGE_ADDR, (uint8_t*)&header, sizeof(header));
    
    if (header.magic == FLASH_CONFIG_MAGIC) {
        return header.timestamp;
    }
    return 0;
}
