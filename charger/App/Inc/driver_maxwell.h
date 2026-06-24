/**
 * @file driver_maxwell.h
 * @brief Maxwell MXR Series Charging Module Driver Interface
 * @note Protocol V1.50 - CAN 2.0B Extended Frame, 125Kbps
 *
 * Usage:
 *   1. Register driver: CHG_RegisterDriver(CHG_DRIVER_MAXWELL, CHG_MaxwellDriverOps());
 *   2. Select driver: CHG_SelectDriver(CHG_DRIVER_MAXWELL);
 *   3. Initialize: CHG_Init();
 *  