/**
 * @file driver_tonhe.h
 * @brief TonHe V1.3 Charging Module Driver Interface - J1939-based CAN protocol
 * @note Protocol: SAE J1939-21, CAN 2.0B Extended Frame, 125Kbps, isolated CAN
 *
 * Usage:
 *   1. Register driver: CHG_RegisterDriver(CHG_DRIVER_TONHE, CHG_TonheDriverOps());
 *   2. Select driver: CHG_SelectDriver(CHG_DRIVER_TONHE);
 *   3. Initialize: CHG_Init();
 *   4. Add module: CHG_AddModule(module_addr, group);
 *
 * For detailed protocol specification, see driver_tonhe.c
 */

#ifndef DRIVER_TONHE_H
#define DRIVER_TONHE_H

#include "charger_core.h"

/* ============== CAN Address ============== */

#define TONHE_ADDR_CONTROLLER   0xA0U
#define TONHE_ADDR_BROADCAST  0xFFU
#define TONHE_MODULE_MIN_ADDR  1
#define TONHE_MODULE_MAX_ADDR 240

/* ============== PGN (Parameter Group Numbers) ============== */
/* Uplink: Module -> Master */
#define TONHE_PGN_STATUS      0x000100U  /* M_C_1: Charging module status */
#define TONHE_PGN_CONFIRM    0x000200U  /* M_C_2: Start/stop confirm */
#define TONHE_PGN_AC_PHASE   0x000B00U  /* M_C_3: AC phase information */
#define TONHE_PGN_EXTENDED   0x009100U  /* M_C_4: Extended status/fault */

/* Downlink: Master -> Module */
#define TONHE_PGN_BROADCAST_CMD   0x000300U  /* C_M_1: Broadcast start/stop */
#define TONHE_PGN_PARAM_SET       0x000400U  /* C_M_2: Broadcast parameter */
#define TONHE_PGN_TIMING          0x000500U  /* C_M_3: Timing command */
#define TONHE_PGN_SPECIFIC_CMD    0x000600U  /* C_M_24: Specific module start/stop */
#define TONHE_PGN_ADDR_SET        0x000900U  /* C_M_23: Address setting */
#define TONHE_PGN_ADDR_MODE       0x009000U  /* C_M_12: Address mode selection */

/* ============== Priority ============== */

#define TONHE_PRIORITY_STATUS   6
#define TONHE_PRIORITY_CMD     2
#define TONHE_PRIORITY_EXTENDED 7

/* ============== Scale Factors ============== */

#define TONHE_VOLTAGE_SCALE   0.1f      /* V per bit */
#define TONHE_CURRENT_SCALE   0.01f     /* A per bit */
#define TONHE_TEMP_SCALE      1.0f      /* °C per bit */

/* ============== Limits ============== */

#define TONHE_MAX_OUTPUT_VOLTAGE_V  750.0f
#define TONHE_MAX_OUTPUT_CURRENT_A 500.0f
#define TONHE_MAX_MODULES          8

/* ============== Timeouts ============== */

#define TONHE_OFFLINE_TIMEOUT_MS    2000U
#define TONHE_RECOVERY_DELAY_MS   3000U
#define TONHE_CONFIRM_TIMEOUT_MS   1000U
#define TONHE_POLL_INTERVAL_MS     20U

/* ============== Command Values ============== */

#define TONHE_CMD_STOP    0x55U
#define TONHE_CMD_START  0xAAU
#define TONHE_MODE_STANDBY 0x00U

/* ============== Status Values ============== */

#define TO