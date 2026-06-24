/**
 * @file driver_maxwell.h
 * @brief Maxwell MXR Series Charging Module Driver Interface
 * @note Protocol V1.50 - CAN 2.0B Extended Frame, 125Kbps
 *
 * Usage:
 *   1. Register driver: CHG_RegisterDriver(CHG_DRIVER_MAXWELL, CHG_MaxwellDriverOps());
 *   2. Select driver: CHG_SelectDriver(CHG_DRIVER_MAXWELL);
 *   3. Initialize: CHG_Init();
 *   4. Add module: CHG_AddModule(module_addr, group);
 *
 * For detailed protocol specification, see driver_maxwell.c
 */

#ifndef DRIVER_MAXWELL_H
#define DRIVER_MAXWELL_H

#include "charger_core.h"

/**
 * @brief Get driver operations table for Maxwell module
 * @return Pointer to CHG_DriverOps_t
 */
const CHG_DriverOps_t *CHG_MaxwellDriverOps(void);

#endif /* DRIVER_MAXWELL_H */
