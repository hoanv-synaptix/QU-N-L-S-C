/**
 * @file    bms_core.c
 * @brief   BMS Driver Core - Implementation
 * @note    State machine, timeout watchdog, Ctrl_INFO transmission,
 *          BMS_Data_t management, and public API.
 *
 * References:
 *   - "CAN BMS_BB_PKG V1.0.md"
 *   - "bms_core.h"
 */

#include "bms_core.h"
#include "bms_protocol.h"
#include "bsp_can.h"
#include "can.h"
#include "stm32f4xx_hal.h"
#include "debug_log.h"
#include <string.h>

/* ============== Private state ============== */

static BMS_Data_t      g_bms_data;
static BMS_View_t      g_bms_view;
static BMS_State_t      g_bms_state;
static uint32_t        g_last_valid_rx_tick;
static uint32_t        g_last_ctrl_tx_tick;
static BMS_ChargeCtrl_t g_charge_ctrl;
static bool            g_initialized;

/* ============== Private: raw → physical ============== */

static void update_view_from_data(void)
{
    BMS_View_t *v = &g_bms_view;

    /* BATT_ST1 */
    if (g_bms_data.batt_st1.valid) {
        v->batt_voltage = BMS_RAW_TO_VOLT(g_bms_data.batt_st1.raw_volt);
        v->batt_current = BMS_RAW_TO_CURR(g_bms_data.batt_st1.raw_curr);
        v->soc = g_bms_data.batt_st1.soc;
    }

    /* BATT_ST2 */
    if (g_bms_data.batt_st2.valid) {
        v->cap_remain  = g_bms_data.batt_st2.cap_remain;
        v->rate_cap    = g_bms_data.batt_st2.rate_cap;
        v->cycle_count = g_bms_data.batt_st2.cycle_count;
        v->soh         = g_bms_data.batt_st2.soh;
    }

    /* CELL_VOLT */
    if (g_bms_data.cell_volt.valid) {
        v->max_cell_volt = g_bms_data.cell_volt.max_cell_volt;
        v->min_cell_volt = g_bms_data.cell_volt.min_cell_volt;
        v->max_cv_no     = g_bms_data.cell_volt.max_cv_no;
        v->min_cv_no     = g_bms_data.cell_volt.min_cv_no;
    }

    /* CELL_TEMP: raw = Temp + 50, physical = raw - 50 */
    if (g_bms_data.cell_temp.valid) {
        v->max_cell_temp = BMS_RAW_TO_TEMP(g_bms_data.cell_temp.max_cell_temp);
        v->min_cell_temp = BMS_RAW_TO_TEMP(g_bms_data.cell_temp.min_cell_temp);
        v->avg_cell_temp = BMS_RAW_TO_TEMP(g_bms_data.cell_temp.avg_cell_temp);
        v->max_ct_no     = g_bms_data.cell_temp.max_ct_no;
        v->min_ct_no     = g_bms_data.cell_temp.min_ct_no;
    }

    /* CELL_TEMP_FULL: detailed temperature (relay, shunt, 6 cells) */
    if (g_bms_data.cell_temp_full.valid) {
        v->temp_relay   = BMS_RAW_TO_TEMP(g_bms_data.cell_temp_full.temp_relay);
        v->temp_shunt   = BMS_RAW_TO_TEMP(g_bms_data.cell_temp_full.temp_shunt);
        for (uint8_t i = 0U; i < 6U; i++) {
            v->cell_temp[i] = BMS_RAW_TO_TEMP(g_bms_data.cell_temp_full.cell_temp[i]);
        }
    }

    /* ChgRequest: Volt/Curr request = raw × 0.1 */
    if (g_bms_data.chg_request.valid) {
        v->chg_volt_request = BMS_RAW_TO_VOLT(g_bms_data.chg_request.batt_volt_req);
        v->chg_curr_request = BMS_RAW_TO_REQ_CURR(g_bms_data.chg_request.batt_curr_req);
    }

    /* BmsSwSta */
    if (g_bms_data.bms_sw_sta.valid) {
        v->charge_relay_closed    = g_bms_data.bms_sw_sta.charge_sta;
        v->discharge_relay_closed = g_bms_data.bms_sw_sta.discharge_sta;
    }
}

/* ============== Private: map alarm severity ============== */

static BMS_AlarmFlag_t map_alarm_field(uint8_t sev, BMS_AlarmFlag_t flag)
{
    /* Severity 2 or 3 = fault/warning; 0 = no alarm; 1 = minor */
    if (sev >= 2U) {
        return flag;
    }
    return BMS_ALARM_NONE;
}

static void update_alarm_flags(void)
{
    BMS_AlarmFlag_t flags = BMS_ALARM_NONE;
    const BMS_AlmInfo_t *a = &g_bms_data.alm_info;

    if (!a->valid) {
        g_bms_view.alarm_flags = flags;
        return;
    }

    flags |= map_alarm_field(a->low_pack_volt,      BMS_ALARM_LOW_PACK_VOLT);
    flags |= map_alarm_field(a->low_cell_volt,      BMS_ALARM_LOW_CELL_VOLT);
    flags |= map_alarm_field(a->high_pack_volt,     BMS_ALARM_HIGH_PACK_VOLT);
    flags |= map_alarm_field(a->high_cell_volt,     BMS_ALARM_HIGH_CELL_VOLT);
    flags |= map_alarm_field(a->temp_cell_high_chg,  BMS_ALARM_TEMP_HIGH_CHG);
    flags |= map_alarm_field(a->temp_cell_high_dchg, BMS_ALARM_TEMP_HIGH_DCHG);
    flags |= map_alarm_field(a->temp_cell_low_chg,  BMS_ALARM_TEMP_LOW_CHG);
    flags |= map_alarm_field(a->temp_cell_low_dchg, BMS_ALARM_TEMP_LOW_DCHG);
    flags |= map_alarm_field(a->temp_relay_high,    BMS_ALARM_TEMP_RELAY_HIGH);
    flags |= map_alarm_field(a->over_chg_curr,      BMS_ALARM_OVER_CHG_CURR);
    flags |= map_alarm_field(a->over_dchg_curr,     BMS_ALARM_OVER_DCHG_CURR);
    flags |= map_alarm_field(a->cell_volt_diff,    BMS_ALARM_CELL_VOLT_DIFF);
    flags |= map_alarm_field(a->low_soc,            BMS_ALARM_LOW_SOC);

    g_bms_view.alarm_flags = flags;
}

/* ============== Private: CAN TX wrapper (forward decl) ============== */
static bool BSP_CAN2_Transmit(const BSP_CAN_Frame_t *frame);

/* ============== Private: send Ctrl_INFO ============== */

static void transmit_ctrl_info(void)
{
    BSP_CAN_Frame_t frame;
    BMS_CtrlInfo_t  ctrl;

    frame.ext_id = BMS_ID_CTRL_INFO;
    frame.dlc    = 8U;

    /* Mask: bit0=charge ctrl allowed, bit1=discharge ctrl allowed */
    ctrl.mask_code = 0U;
    if (g_charge_ctrl.allow_charge) {
        ctrl.mask_code |= 0x01U;
    }
    if (g_charge_ctrl.allow_discharge) {
        ctrl.mask_code |= 0x02U;
    }
    ctrl.chg_sw  = g_charge_ctrl.allow_charge ? 1U : 0U;
    ctrl.dchg_sw = g_charge_ctrl.allow_discharge ? 1U : 0U;

    BMS_BuildCtrlInfo(frame.data, &ctrl);
    (void)BSP_CAN2_Transmit(&frame);
}

/* ============== Private: CAN TX wrapper ============== */

static bool BSP_CAN2_Transmit(const BSP_CAN_Frame_t *frame)
{
    CAN_TxHeaderTypeDef header;
    uint32_t tx_mailbox;

    header.IDE   = CAN_ID_EXT;
    header.ExtId = frame->ext_id;
    header.RTR   = CAN_RTR_DATA;
    header.DLC   = frame->dlc;
    header.TransmitGlobalTime = DISABLE;

    if (HAL_CAN_AddTxMessage(&hcan2, &header,
                             (uint8_t *)frame->data, &tx_mailbox) != HAL_OK) {
        return false;
    }

    LOG("[BMS TX] ID:%08lX Data:%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
        frame->ext_id,
        frame->data[0], frame->data[1], frame->data[2], frame->data[3],
        frame->data[4], frame->data[5], frame->data[6], frame->data[7]);
    return true;
}

/* ============== Public API ============== */

void BMS_Init(void)
{
    memset(&g_bms_data, 0, sizeof(g_bms_data));
    memset(&g_bms_view, 0, sizeof(g_bms_view));
    memset(&g_charge_ctrl, 0, sizeof(g_charge_ctrl));

    g_bms_state          = BMS_STATE_OFFLINE;
    g_last_valid_rx_tick = HAL_GetTick();
    g_last_ctrl_tx_tick  = 0U;
    g_initialized        = true;

    LOG("BMS_Init: driver ready.\r\n");
}

void BMS_FeedFrame(uint32_t ext_id, uint32_t std_id,
                   const uint8_t *data, uint8_t dlc)
{
    if (!g_initialized) {
        return;
    }

    /* Parse into g_bms_data */
    BMS_ParseFrame(ext_id, std_id, data, dlc, &g_bms_data);

    /* Mark as online on first valid frame */
    g_last_valid_rx_tick = HAL_GetTick();

    /* Refresh cached view */
    update_view_from_data();
    update_alarm_flags();
}

void BMS_Process(uint32_t now_tick)
{
    if (!g_initialized) {
        return;
    }

    uint32_t elapsed = now_tick - g_last_valid_rx_tick;

    /* ---- State Machine ---- */
    if (g_bms_state == BMS_STATE_OFFLINE) {
        /* Wait for first frame */
        if (elapsed < BMS_OFFLINE_TIMEOUT_MS) {
            /* Still offline but receiving — could transition */
        } else {
            /* Still no data after timeout — stay offline */
        }
        /* Transition to ONLINE once we have data */
        if (g_bms_data.batt_st1.valid) {
            g_bms_state = BMS_STATE_ONLINE;
            g_bms_view.online = true;
            LOG("BMS: ONLINE\r\n");
        }
    }
    else if (g_bms_state == BMS_STATE_ONLINE) {
        if (elapsed >= BMS_OFFLINE_TIMEOUT_MS) {
            g_bms_state = BMS_STATE_OFFLINE;
            g_bms_view.online = false;
            g_bms_view.alarm_flags |= BMS_ALARM_BMS_OFFLINE;
            /* Clear all parsed data so stale values are not used */
            memset(&g_bms_data, 0, sizeof(g_bms_data));
            LOG("BMS: OFFLINE (timeout)\r\n");
        } else {
            /* Update online status */
            g_bms_view.online = (elapsed < BMS_STALE_THRESHOLD_MS);
            if (elapsed >= BMS_STALE_THRESHOLD_MS) {
                g_bms_view.alarm_flags |= BMS_ALARM_STALE_DATA;
            }
        }

        /* Check for critical alarms */
        if (g_bms_view.alarm_flags != BMS_ALARM_NONE) {
            /* Could transition to FAULT here if needed */
        }
    }
    else if (g_bms_state == BMS_STATE_FAULT) {
        /* Fault recovery: auto-recover when alarms clear and data resumes */
        if ((g_bms_view.alarm_flags == BMS_ALARM_NONE) &&
            (elapsed < BMS_STALE_THRESHOLD_MS)) {
            g_bms_state = BMS_STATE_ONLINE;
            LOG("BMS: Recovered from FAULT -> ONLINE\r\n");
        }
    }

    /* ---- Periodic Ctrl_INFO transmission ---- */
    if ((now_tick - g_last_ctrl_tx_tick) >= BMS_CTRL_TX_INTERVAL_MS) {
        g_last_ctrl_tx_tick = now_tick;
        transmit_ctrl_info();
    }

    g_bms_view.state       = g_bms_state;
    g_bms_view.last_rx_tick = g_last_valid_rx_tick;
}

void BMS_SendCtrlInfo(const BMS_ChargeCtrl_t *ctrl)
{
    if (ctrl == NULL) {
        return;
    }
    g_charge_ctrl.allow_charge    = ctrl->allow_charge;
    g_charge_ctrl.allow_discharge = ctrl->allow_discharge;

    /* Transmit immediately */
    transmit_ctrl_info();
}

bool BMS_IsOnline(void)
{
    return (g_bms_state == BMS_STATE_ONLINE);
}

bool BMS_HasCriticalAlarm(void)
{
    /* Critical = BMS offline, stale data, or any fault-level alarm */
    BMS_AlarmFlag_t crit = (
        BMS_ALARM_HIGH_CELL_VOLT  |
        BMS_ALARM_TEMP_HIGH_CHG   |
        BMS_ALARM_OVER_CHG_CURR   |
        BMS_ALARM_BMS_OFFLINE    |
        BMS_ALARM_STALE_DATA
    );
    return ((g_bms_view.alarm_flags & crit) != 0U);
}

void BMS_GetView(BMS_View_t *view)
{
    if (view == NULL) {
        return;
    }
    *view = g_bms_view;
}

bool BMS_ShouldCloseChargeRelay(void)
{
    if (!BMS_IsOnline()) {
        return false;
    }

    const BMS_View_t *v = &g_bms_view;

    /* Charge relay in BMS must be closed */
    if (!v->charge_relay_closed) {
        return false;
    }

    /* Charger output voltage must be > 90% of BMS request */
    if (v->chg_volt_request > 0.0f) {
        /* The app will compare charger voltage against this threshold.
         * Here we just return true so the app can make the final decision.
         */
    }

    (void)v; /* Suppress unused warning if logging is disabled */
    return true;
}
