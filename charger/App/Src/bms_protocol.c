/**
 * @file    bms_protocol.c
 * @brief   BMS CAN Protocol - Parse and Build implementations
 * @note    Protocol: CAN 2.0A/B, 250Kbps, Little-Endian
 *
 * Hardware Interface:
 *   - CAN2: 250Kbps, PB12=RX, PB13=TX
 *   - Extended + Standard frames
 *
 * References:
 *   - "CAN BMS_BB_PKG V1.0.md"
 */

#include "bms_protocol.h"
#include <string.h>

/* ============== Private: helpers ============== */

static inline uint16_t get_u16_le(const uint8_t *d, uint8_t pos)
{
    return ((uint16_t)d[pos]       ) |
           ((uint16_t)d[pos + 1] << 8);
}

static inline uint32_t get_u32_le(const uint8_t *d, uint8_t pos)
{
    return ((uint32_t)d[pos]       ) |
           ((uint32_t)d[pos + 1] << 8)  |
           ((uint32_t)d[pos + 2] << 16) |
           ((uint32_t)d[pos + 3] << 24);
}

/* ============== Private: parse individual messages ============== */

/* ---- BATT_ST1: ID=0x02F4, 20ms ---- */
static void parse_batt_st1(const uint8_t *d, BMS_BattSt1_t *out)
{
    out->raw_volt = get_u16_le(d, 0);
    out->raw_curr = (int16_t)get_u16_le(d, 2);
    out->soc      = d[4];
    out->valid    = true;
}

/* ---- CELL_VOLT: ID=0x04F4, 100ms ---- */
static void parse_cell_volt(const uint8_t *d, BMS_CellVolt_t *out)
{
    out->max_cell_volt = get_u16_le(d, 0);
    out->max_cv_no     = d[2];
    out->min_cell_volt = get_u16_le(d, 3);
    out->min_cv_no     = d[5];
    out->valid         = true;
}

/* ---- CELL_TEMP: ID=0x05F4, 500ms ---- */
static void parse_cell_temp(const uint8_t *d, BMS_CellTemp_t *out)
{
    out->max_cell_temp = (int8_t)d[0];
    out->max_ct_no     = d[1];
    out->min_cell_temp = (int8_t)d[2];
    out->min_ct_no     = d[3];
    out->avg_cell_temp = (int8_t)d[4];
    out->valid         = true;
}

/* ---- ALM_INFO: ID=0x07F4, event-triggered ---- */
static void parse_alm_info(const uint8_t *d, BMS_AlmInfo_t *out)
{
    uint32_t raw = get_u32_le(d, 0);  /* Only 26 bits used (4 bytes), little-endian */

    out->low_pack_volt      = (uint8_t)((raw >> 0)  & 0x03U);
    out->low_cell_volt      = (uint8_t)((raw >> 2)  & 0x03U);
    out->high_pack_volt     = (uint8_t)((raw >> 4)  & 0x03U);
    out->high_cell_volt     = (uint8_t)((raw >> 6)  & 0x03U);
    out->temp_cell_high_chg = (uint8_t)((raw >> 8)  & 0x03U);
    out->temp_cell_high_dchg= (uint8_t)((raw >> 10) & 0x03U);
    out->temp_cell_low_chg  = (uint8_t)((raw >> 12) & 0x03U);
    out->temp_cell_low_dchg = (uint8_t)((raw >> 14) & 0x03U);
    out->temp_relay_high    = (uint8_t)((raw >> 16) & 0x03U);
    out->over_chg_curr      = (uint8_t)((raw >> 18) & 0x03U);
    out->over_dchg_curr     = (uint8_t)((raw >> 20) & 0x03U);
    out->cell_volt_diff     = (uint8_t)((raw >> 22) & 0x03U);
    out->low_soc            = (uint8_t)((raw >> 24) & 0x03U);
    out->valid = true;
}

/* ---- BATT_ST2: ID=0x18F128F4, 100ms ---- */
static void parse_batt_st2(const uint8_t *d, BMS_BattSt2_t *out)
{
    out->cap_remain   = get_u16_le(d, 0);
    out->rate_cap     = get_u16_le(d, 2);
    out->cycle_count  = get_u16_le(d, 4);
    out->soh          = d[6];
    out->valid        = true;
}

/* ---- ChgRequest_INFO: ID=0x18F0F472, 1000ms ---- */
static void parse_chg_request(const uint8_t *d, BMS_ChgRequest_t *out)
{
    out->batt_volt_req = get_u16_le(d, 0);
    out->batt_curr_req = get_u16_le(d, 2);
    out->valid         = true;
}

/* ---- BmsSwSta: ID=0x18F528F4, 500ms ---- */
static void parse_bms_sw_sta(const uint8_t *d, BMS_BmsSwSta_t *out)
{
    uint8_t byte0 = d[0];
    out->pre_discharge_sta = ((byte0 & 0x01U) != 0U);
    out->discharge_sta    = ((byte0 & 0x02U) != 0U);
    out->charge_sta        = ((byte0 & 0x04U) != 0U);
    out->valid             = true;
}

/* ---- CELL_VOLT_FULL: ID=0x18E0XXF4, 1000ms, XX=0..7 ---- */
static void parse_cell_volt_full(const uint8_t *d, uint8_t frame_idx,
                                BMS_CellVoltFull_t *out)
{
    if (frame_idx >= BMS_MAX_CELL_VOLT_FRAMES) {
        return;
    }

    /* Each frame contains 4 cells, 16 bits each (little-endian) */
    for (uint8_t i = 0U; i < 4U; i++) {
        out[frame_idx].cell[i] = get_u16_le(d, (uint8_t)(i * 2U));
    }
    out[frame_idx].valid = true;
}

/* ---- CELL_TEMP_FULL: ID=0x18F228F4, 1000ms ---- */
static void parse_cell_temp_full(const uint8_t *d, BMS_CellTempFull_t *out)
{
    out->temp_relay        = (int8_t)d[0];
    out->temp_shunt        = (int8_t)d[1];
    out->cell_temp[0]     = (int8_t)d[2];
    out->cell_temp[1]     = (int8_t)d[3];
    out->cell_temp[2]     = (int8_t)d[4];
    out->cell_temp[3]     = (int8_t)d[5];
    out->cell_temp[4]     = (int8_t)d[6];
    out->cell_temp[5]     = (int8_t)d[7];
    out->valid            = true;
}

/* ============== Public: dispatcher ============== */

void BMS_ParseFrame(uint32_t ext_id, uint32_t std_id,
                    const uint8_t *data, uint8_t dlc,
                    BMS_Data_t *bms)
{
    (void)dlc;  /* All frames are 8 bytes */

    if (std_id != 0U) {
        /* Standard frame (11-bit) */
        switch (std_id) {
            case BMS_ID_BATT_ST1:
                parse_batt_st1(data, &bms->batt_st1);
                break;

            case BMS_ID_CELL_VOLT:
                parse_cell_volt(data, &bms->cell_volt);
                break;

            case BMS_ID_CELL_TEMP:
                parse_cell_temp(data, &bms->cell_temp);
                break;

            case BMS_ID_ALM_INFO:
                parse_alm_info(data, &bms->alm_info);
                break;

            default:
                break;
        }
    } else {
        /* Extended frame (29-bit) */
        if (ext_id == BMS_ID_BATT_ST2) {
            parse_batt_st2(data, &bms->batt_st2);
        }
        else if (ext_id == BMS_ID_CHG_REQUEST) {
            parse_chg_request(data, &bms->chg_request);
        }
        else if (ext_id == BMS_ID_BMS_SW_STA) {
            parse_bms_sw_sta(data, &bms->bms_sw_sta);
        }
        else if (ext_id == BMS_ID_CELL_TEMP_FULL) {
            parse_cell_temp_full(data, &bms->cell_temp_full);
        }
        else if ((ext_id & 0xFFFF0000UL) == 0x18E00000UL) {
            /* CELL_VOLT_FULL: extract frame index from bits 16-18 */
            uint8_t frame_idx = (uint8_t)((ext_id >> 16U) & 0x07U);
            parse_cell_volt_full(data, frame_idx, bms->cell_volt_full);
        }
        /* BMS_ID_CTRL_INFO is sent FROM us TO BMS, not received */
    }
}

/* ============== Public: build Ctrl_INFO ============== */

void BMS_BuildCtrlInfo(uint8_t out[8], const BMS_CtrlInfo_t *ctrl)
{
    memset(out, 0, 8U);
    out[0] = ctrl->mask_code;
    out[1] = ctrl->chg_sw;
    out[2] = ctrl->dchg_sw;
    /* bytes 3-7 remain 0 (reserved) */
}
