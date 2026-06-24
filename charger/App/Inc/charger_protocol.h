/**
 * @file    charger_protocol.h
 * @brief   Canonical Charger Protocol - Common types and helpers
 * @note    Shared data conversion utilities for all charger drivers.
 *          Each driver (Maxwell/Lianming/TonHe) implements its own CAN ID builder.
 *
 * Architecture:
 *   - Each driver is independent and implements CHG_DriverOps_t interface
 *   - CAN ID builders are driver-specific (no shared implementation)
 *   - Data format converters are shared (Float/U32/U16 helpers)
 *
 * Historical Note:
 *   - CHG_ProtocolBuildCanId was removed as each module uses different CAN ID formats:
 *     - Maxwell: Proprietary 29-bit format (PROTNO + PTP + DST + SRC + Group)
 *     - Lianming: Custom Command ID format (Command + Module Address)
 *     - TonHe: J1939 format (Priority + PF + PS + SA)
 *
 * Dependencies:
 *   - driver_maxwell.c: Uses Float helpers (IEEE 754)
 *   - driver_lianming.c: Uses U16 helpers (0.1V, 0.01A)
 *   - driver_tonhe.c: Uses its own conversion (0.1V, 0.01A)
 */

#ifndef CHARGER_PROTOCOL_H
#define CHARGER_PROTOCOL_H

#include <stdint.h>

/* =========================================================
 * CANONICAL CHARGER REGISTERS
 * Standard register map dùng chung cho tất cả module
 * (Định nghĩa theo Maxwell, các module khác map tương ứng)
 * ========================================================= */
#define CHG_REG_VOLTAGE                 0x0001  /* Đọc điện áp output */
#define CHG_REG_CURRENT                0x0002  /* Đọc dòng output */
#define CHG_REG_CURR_LIMIT             0x0003  /* Đọc điểm giới hạn dòng */
#define CHG_REG_TEMP_DCDC              0x0004  /* Đọc nhiệt độ board DCDC */
#define CHG_REG_TEMP_AMBIENT           0x000B  /* Đọc nhiệt độ môi trường */
#define CHG_REG_RATED_POWER            0x0011  /* Công suất định mức */
#define CHG_REG_RATED_CURRENT          0x0012  /* Dòng định mức */
#define CHG_REG_SET_POWER              0x0020  /* Cài đặt công suất */
#define CHG_REG_SET_VOLTAGE            0x0021  /* Cài đặt điện áp */
#define CHG_REG_SET_CURR_LIMIT         0x0022  /* Cài đặt giới hạn dòng */
#define CHG_REG_SET_OVP                0x0023  /* Cài đặt OVP */
#define CHG_REG_ON_OFF                 0x0030  /* Bật/Tắt module */
#define CHG_REG_ALARM_STATUS           0x0040  /* Trạng thái alarm */
#define CHG_REG_GROUP_ADDR             0x0043  /* Địa chỉ group */
#define CHG_REG_SHORT_RESET            0x0044  /* Reset lỗi short */
#define CHG_REG_INPUT_MODE_SET         0x0046  /* Cài đặt chế độ input */
#define CHG_REG_INPUT_POWER            0x0048  /* Công suất input */
#define CHG_REG_INPUT_MODE_RD          0x004B  /* Đọc chế độ input */

/* An toàn: giới hạn công suất đầu vào hợp lý */
#define CHG_INPUT_POWER_MAX_W           100000U

/* =========================================================
 * DATA CONVERSION HELPERS
 * ========================================================= */

/*