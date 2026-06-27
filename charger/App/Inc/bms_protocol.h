/**
 * @file    bms_protocol.h
 * @brief   BMS CAN Protocol - Message IDs, Data Types, and Parsing
 * @note    Protocol: CAN 2.0A/B, 250Kbps, Little-Endian
 *
 * Physical Interface:
 *   - CAN2: 250Kbps (prescaler=21, BS1=13, BS2=2, SJW=1)
 *   - CAN2 GPIO: PB12=RX, PB13=TX
 *
 * Message Overview:
 *   Standard frames (11-bit): BATT_ST1, CELL_VOLT, CELL_TEMP, ALM_INFO
 *   Extended frames (29-bit): BATT_ST2, ChgRequest, Ctrl_INFO, BmsSwSta,
 *                              CELL_VOLT_FULL (8 frames), CELL_TEMP_FULL
 *
 * CAN ID Layout (Standard):
 *   - 0x02F4: BATT_ST1    (BMS → Charger, 20ms)
 *   - 0x04F4: CELL_VOLT   (BMS → Charger, 100ms)
 *   - 0x05F4: CELL_TEMP   (BMS → Charger, 500ms)
 *   - 0x07F4: ALM_INFO    (BMS → Charger, event-triggered)
 *
 * CAN ID Layout (Extended 29-bit):
 *   - 0x18F128F4: BATT_ST2         (BMS → Charger, 100ms)
 *   - 0x18F0F428: Ctrl_INFO        (Charger → BMS, on-demand)
 *   - 0x18F0F472: ChgRequest_INFO  (BMS → Charger, 1000ms)
 *   - 0x18F528F4: BmsSwSta         (BMS → Charger, 500ms)
 *   - 0x18E0XXF4: CELL_VOLT_FULL   (BMS → Charger, 1000ms) XX = 0..7
 *   - 0x18F228F4: CELL_TEMP_FULL   (BMS → Charger, 1000ms)
 *
 * References:
 *   - "CAN BMS_BB_PKG V1.0.md"
 */

#ifndef BMS_PROTOCOL_H
#define BMS_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== CAN Message IDs ============== */

/** Standard Frame IDs (11-bit) */
#define BMS_ID_BATT_ST1   0x02F4U
#define BMS_ID_CELL_VOLT  0x04F4U
#define BMS_ID_CELL_TEMP  0x05F4U
#define BMS_ID_ALM_INFO   0x07F4U

/** Extended Frame IDs (29-bit) */
#define BMS_ID_BATT_ST2          0x18F128F4UL
#define BMS_ID_CTRL_INFO         0x18F0F428UL  /* Charger → BMS */
#define BMS_ID_CHG_REQUEST       0x18F0F472UL
#define BMS_ID_BMS_SW_STA        0x18F528F4UL
#define BMS_ID_CELL_VOLT_FULL(n) (0x18E00000UL | (((uint32_t)(n) & 0x07U) << 16) | 0xF4U)
#define BMS_ID_CELL_TEMP_FULL    0x18F228F4UL

/* ============== Parse Results ============== */

/**
 * @brief   Parse result for each CAN message.
 *          All raw values are stored as-is (no scaling applied).
 *          Apply resolution/offset only when reading.
 */
typedef bool BMS_ParseResult_t;

/* ---- BATT_ST1 (0x02F4) ---- */
typedef struct {
    BMS_ParseResult_t valid;
    uint16_t raw_volt;    /* Total battery voltage: raw = V × 10   (0~1000 => 0~100.0V) */
    int16_t  raw_curr;    /* Total current: raw = (I + 400) × 10  (-400~1000A) */
    uint8_t  soc;         /* SOC 0~100% */
} BMS_BattSt1_t;

/* ---- CELL_VOLT (0x04F4) ---- */
typedef struct {
    BMS_ParseResult_t valid;
    uint16_t max_cell_volt;  /* mV, max cell voltage */
    uint8_t  max_cv_no;      /* 1-based position of max cell */
    uint16_t min_cell_volt;  /* mV, min cell voltage */
    uint8_t  min_cv_no;     /* 1-based position of min cell */
} BMS_CellVolt_t;

/* ---- CELL_TEMP (0x05F4) ---- */
typedef struct {
    BMS_ParseResult_t valid;
    uint8_t max_cell_temp;  /* raw = Temp + 50 (0~250, range -50~200°C) */
    uint8_t max_ct_no;      /* 1-based position of max temp sensor */
    uint8_t min_cell_temp;  /* raw = Temp + 50 */
    uint8_t min_ct_no;      /* 1-based position of min temp sensor */
    uint8_t avg_cell_temp;  /* raw = Avg + 50 */
} BMS_CellTemp_t;

/* ---- ALM_INFO (0x07F4) ---- */
typedef struct {
    BMS_ParseResult_t valid;
    /* Each alarm: 0=none, 1=warning, 2=fault, 3=severe */
    uint8_t low_pack_volt;          /* bit 0-1 */
    uint8_t low_cell_volt;          /* bit 2-3 */
    uint8_t high_pack_volt;         /* bit 4-5 */
    uint8_t high_cell_volt;         /* bit 6-7 */
    uint8_t temp_cell_high_chg;     /* bit 8-9 */
    uint8_t temp_cell_high_dchg;    /* bit 10-11 */
    uint8_t temp_cell_low_chg;      /* bit 12-13 */
    uint8_t temp_cell_low_dchg;     /* bit 14-15 */
    uint8_t temp_relay_high;        /* bit 16-17 */
    uint8_t over_chg_curr;          /* bit 18-19 */
    uint8_t over_dchg_curr;         /* bit 20-21 */
    uint8_t cell_volt_diff;         /* bit 22-23 */
    uint8_t low_soc;                /* bit 24-25 */
} BMS_AlmInfo_t;

/* ---- BATT_ST2 (0x18F128F4) ---- */
typedef struct {
    BMS_ParseResult_t valid;
    uint16_t cap_remain;     /* Ah × 0.1  (0~1000 => 0~100.0Ah) */
    uint16_t rate_cap;       /* Ah × 0.1  (rated capacity) */
    uint16_t cycle_count;    /* Cycle count */
    uint8_t  soh;            /* SOH 0~100% */
} BMS_BattSt2_t;

/* ---- ChgRequest_INFO (0x18F0F472) ---- */
typedef struct {
    BMS_ParseResult_t valid;
    uint16_t batt_volt_req;   /* V × 0.1   (0~10000 => 0~1000.0V) */
    uint16_t batt_curr_req;  /* A × 0.1   (0~10000 => 0~1000.0A) */
} BMS_ChgRequest_t;

/* ---- BmsSwSta (0x18F528F4) ---- */
typedef struct {
    BMS_ParseResult_t valid;
    bool pre_discharge_sta;   /* Pre-discharge relay: 0=open, 1=closed */
    bool discharge_sta;       /* Discharge relay: 0=open, 1=closed */
    bool charge_sta;         /* Charge relay: 0=open, 1=closed */
} BMS_BmsSwSta_t;

/* ---- CELL_VOLT_FULL (0x18E0XXF4, XX=0..7) ---- */
/** Each frame carries 4 cell voltages. 8 frames × 4 = 32 cells max. */
typedef struct {
    BMS_ParseResult_t valid;
    uint16_t cell[4];        /* mV, 4 cells per frame */
} BMS_CellVoltFull_t;

/* ---- CELL_TEMP_FULL (0x18F228F4) ---- */
typedef struct {
    BMS_ParseResult_t valid;
    uint8_t temp_relay;     /* raw = T + 50 (range -50~200°C) */
    uint8_t temp_shunt;     /* raw = T + 50 */
    uint8_t cell_temp[6];   /* raw = T + 50, 6 cell temperatures */
} BMS_CellTempFull_t;

/* ============== Control Info (Charger → BMS) ============== */

/**
 * @brief   Ctrl_INFO frame payload to send to BMS.
 *          ID = 0x18F0F428 (Extended)
 *
 * MaskCode bit0: Charging control  (1=allow, 0=disable)
 * MaskCode bit1: Discharge control (1=allow, 0=disable)
 * ChgSw:   0=Off, 1=On
 * DchgSw:  0=Off, 1=On
 */
typedef struct {
    uint8_t mask_code;   /* bit0=charge ctrl, bit1=discharge ctrl */
    uint8_t chg_sw;      /* 0=Off, 1=On */
    uint8_t dchg_sw;     /* 0=Off, 1=On */
    uint8_t rsv[5];      /* Reserved, send as 0 */
} BMS_CtrlInfo_t;

/* ============== Master BMS Data Container ============== */

#define BMS_MAX_CELL_VOLT_FRAMES  8U   /* 8 frames × 4 cells = 32 cells */
#define BMS_MAX_CELL_TEMP_SENSORS 8U   /* Relay, Shunt + 6 cell temps */

typedef struct {
    BMS_BattSt1_t        batt_st1;
    BMS_CellVolt_t      cell_volt;
    BMS_CellTemp_t      cell_temp;
    BMS_AlmInfo_t       alm_info;
    BMS_BattSt2_t       batt_st2;
    BMS_ChgRequest_t    chg_request;
    BMS_BmsSwSta_t      bms_sw_sta;
    BMS_CellVoltFull_t  cell_volt_full[BMS_MAX_CELL_VOLT_FRAMES];
    BMS_CellTempFull_t  cell_temp_full;
} BMS_Data_t;

/* ============== Convenience Accessors ============== */

/* Raw → Physical conversions */
#define BMS_RAW_TO_VOLT(raw)        (((float)(raw)) * 0.1f)
#define BMS_RAW_TO_CURR(raw)        (((float)((int16_t)(raw))) * 0.1f - 400.0f)
#define BMS_RAW_TO_TEMP(raw)        (((float)((uint8_t)(raw))) - 50.0f)
#define BMS_RAW_TO_CAP_AH(raw)      (((float)(raw)) * 0.1f)
#define BMS_RAW_TO_REQ_CURR(raw)    (((float)(raw)) * 0.1f)

/* ============== Parsing API ============== */

/**
 * @brief   Parse a raw CAN frame from BMS.
 * @note    Dispatches to the correct parser based on CAN ID.
 *          Stores result in the provided BMS_Data_t.
 * @param   ext_id     CAN extended ID (use 0 for standard frame)
 * @param   std_id     CAN standard ID (11-bit, 0 if extended)
 * @param   data       8-byte payload (little-endian)
 * @param   dlc        Data length (0-8)
 * @param   bms        Pointer to BMS data structure to fill
 */
void BMS_ParseFrame(uint32_t ext_id, uint32_t std_id,
                    const uint8_t *data, uint8_t dlc,
                    BMS_Data_t *bms);

/* ============== Control Info API ============== */

/**
 * @brief   Build Ctrl_INFO payload to send to BMS.
 * @param   out    8-byte output buffer
 * @param   ctrl   Control information
 */
void BMS_BuildCtrlInfo(uint8_t out[8], const BMS_CtrlInfo_t *ctrl);

#ifdef __cplusplus
}
#endif

#endif /* BMS_PROTOCOL_H */
