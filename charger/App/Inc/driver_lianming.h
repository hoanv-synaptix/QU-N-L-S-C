/**
 * @file driver_lianming.h
 * @brief Lianming Power Digital Charging Module Driver Interface
 * @note Protocol V2.0 - CAN 2.0B Extended Frame, 125Kbps
 *
 * Usage:
 *   1. Register driver: CHG_RegisterDriver(CHG_DRIVER_LIANMING, CHG_LianmingDriverOps());
 *   2. Select driver: CHG_SelectDriver(CHG_DRIVER_LIANMING);
 *   3. Initialize: CHG_Init();
 *   4. Add module: CHG_AddModule(module_addr, group);
 *
 * For detailed protocol specification, see driver_lianming.c
 */

#ifndef DRIVER_LIANMING_H
#define DRIVER_LIANMING_H

#include "charger_core.h"

/**
 * @brief Get driver operations table for Lianming module
 * @return Pointer to CHG_DriverOps_t
 */
const CHG_DriverOps_t *CHG_LianmingDriverOps(void);

#endif /* DRIVER_LIANMING_H */
