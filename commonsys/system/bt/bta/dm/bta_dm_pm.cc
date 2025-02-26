/******************************************************************************
 *
 *  Copyright (C) 2003-2012 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

/******************************************************************************
 *
 *  This file contains the action functions for device manager state
 *  machine.
 *
 ******************************************************************************/

#include <base/logging.h>
#include <string.h>

#include <mutex>

#include "bt_common.h"
#include "bta_api.h"
#include "bta_dm_api.h"
#include "bta_dm_int.h"
#include "bta_ag_int.h"
#include "bta_sys.h"
#include "btm_api.h"

#include "device/include/interop.h"
#include "btif/include/btif_storage.h"

extern fixed_queue_t* btu_bta_alarm_queue;

static void bta_dm_pm_cback(tBTA_SYS_CONN_STATUS status, uint8_t id,
                            uint8_t app_id, const RawAddress& peer_addr);
static void bta_dm_pm_set_mode(const RawAddress& peer_addr,
                               tBTA_DM_PM_ACTION pm_mode,
                               tBTA_DM_PM_REQ pm_req);
static void bta_dm_pm_timer_cback(void* data);
static void bta_dm_pm_btm_cback(const RawAddress& bd_addr,
                                tBTM_PM_STATUS status, uint16_t value,
                                uint8_t hci_status);
static bool bta_dm_pm_park(const RawAddress& peer_addr);
static bool bta_dm_pm_sniff(tBTA_DM_PEER_DEVICE* p_peer_dev, uint8_t index);
static bool bta_dm_pm_is_sco_active();
static int bta_dm_get_sco_index();
static void bta_dm_pm_hid_check(bool bScoActive);
static void bta_dm_pm_set_sniff_policy(tBTA_DM_PEER_DEVICE* p_dev,
                                       bool bDisable);
static void bta_dm_pm_stop_timer_by_index(tBTA_PM_TIMER* p_timer,
                                          uint8_t timer_idx);

#if (BTM_SSR_INCLUDED == TRUE)
#if (BTA_HH_INCLUDED == TRUE)
#include "../hh/bta_hh_int.h"
/* BTA_DM_PM_SSR1 will be dedicated for HH SSR setting entry, no other profile
 * can use it */
#define BTA_DM_PM_SSR_HH BTA_DM_PM_SSR1
#endif
static void bta_dm_pm_ssr(const RawAddress& peer_addr);
#endif

tBTA_DM_CONNECTED_SRVCS bta_dm_conn_srvcs;
static std::recursive_mutex pm_timer_schedule_mutex;
static std::recursive_mutex pm_timer_state_mutex;

/*******************************************************************************
 *
 * Function         bta_dm_init_pm
 *
 * Description      Initializes the BT low power manager
 *
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_dm_init_pm(void) {
  memset(&bta_dm_conn_srvcs, 0x00, sizeof(bta_dm_conn_srvcs));

  /* if there are no power manger entries, so not register */
  if (p_bta_dm_pm_cfg[0].app_id != 0) {
    bta_sys_pm_register((tBTA_SYS_CONN_CBACK*)bta_dm_pm_cback);

    BTM_PmRegister((BTM_PM_REG_SET | BTM_PM_REG_NOTIF), &bta_dm_cb.pm_id,
                   bta_dm_pm_btm_cback);
  }

  /* Need to initialize all PM timer service IDs */
  for (int i = 0; i < BTA_DM_NUM_PM_TIMER; i++) {
    for (int j = 0; j < BTA_DM_PM_MODE_TIMER_MAX; j++)
      bta_dm_cb.pm_timer[i].srvc_id[j] = BTA_ID_MAX;
  }
}

/*******************************************************************************
 *
 * Function         bta_dm_disable_pm
 *
 * Description      Disable PM
 *
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_dm_disable_pm(void) {
  BTM_PmRegister(BTM_PM_DEREG, &bta_dm_cb.pm_id, NULL);

  /*
   * Deregister the PM callback from the system handling to prevent
   * re-enabling the PM timers after this call if the callback is invoked.
   */
  bta_sys_pm_register((tBTA_SYS_CONN_CBACK*)NULL);

  /* Need to stop all active timers. */
  for (int i = 0; i < BTA_DM_NUM_PM_TIMER; i++) {
    for (int j = 0; j < BTA_DM_PM_MODE_TIMER_MAX; j++) {
      bta_dm_pm_stop_timer_by_index(&bta_dm_cb.pm_timer[i], j);
      bta_dm_cb.pm_timer[i].pm_action[j] = BTA_DM_PM_NO_ACTION;
    }
  }
}

/*******************************************************************************
 *
 * Function         bta_dm_get_av_count
 *
 * Description      Get the number of connected AV
 *
 *
 * Returns          number of av connections
 *
 ******************************************************************************/
uint8_t bta_dm_get_av_count(void) {
  uint8_t count = 0;
  for (int i = 0; i < bta_dm_conn_srvcs.count && i < BTA_DM_NUM_CONN_SRVS; i++) {
    if (bta_dm_conn_srvcs.conn_srvc[i].id == BTA_ID_AV) ++count;
  }
  return count;
}

/*******************************************************************************
 *
 * Function         bta_dm_pm_stop_timer
 *
 * Description      stop a PM timer
 *
 *
 * Returns          void
 *
 ******************************************************************************/
static void bta_dm_pm_stop_timer(const RawAddress& peer_addr) {
  APPL_TRACE_DEBUG("%s: ", __func__);

  for (int i = 0; i < BTA_DM_NUM_PM_TIMER; i++) {
    if (bta_dm_cb.pm_timer[i].in_use &&
        bta_dm_cb.pm_timer[i].peer_bdaddr == peer_addr) {
      for (int j = 0; j < BTA_DM_PM_MODE_TIMER_MAX; j++) {
        bta_dm_pm_stop_timer_by_index(&bta_dm_cb.pm_timer[i], j);
        /*
         * TODO: For now, stopping the timer does not reset
         * pm_action[j].
         * The reason is because some of the internal logic that
         * (re)assigns the pm_action[] values is taking into account
         * the older value; e.g., see the pm_action[] assignment in
         * function bta_dm_pm_start_timer().
         * Such subtlety in the execution logic is error prone, and
         * should be eliminiated in the future.
         */
      }
      break;
    }
  }
}

/*******************************************************************************
 *
 * Function         bta_pm_action_to_timer_idx
 *
 * Description      convert power mode into timer index for each connected
 *                  device
 *
 *
 * Returns          index of the power mode delay timer
 *
 ******************************************************************************/
static uint8_t bta_pm_action_to_timer_idx(uint8_t pm_action) {
  if (pm_action == BTA_DM_PM_SUSPEND)
    return BTA_DM_PM_SUSPEND_TIMER_IDX;
  else if (pm_action == BTA_DM_PM_PARK)
    return BTA_DM_PM_PARK_TIMER_IDX;
  else if ((pm_action & BTA_DM_PM_SNIFF) == BTA_DM_PM_SNIFF)
    return BTA_DM_PM_SNIFF_TIMER_IDX;

  /* Active, no preference, no action and retry */
  return BTA_DM_PM_MODE_TIMER_MAX;
}

/*******************************************************************************
 *
 * Function         bta_dm_pm_stop_timer_by_mode
 *
 * Description      stop a PM timer
 *
 *
 * Returns          void
 *
 ******************************************************************************/
static void bta_dm_pm_stop_timer_by_mode(const RawAddress& peer_addr,
                                         uint8_t power_mode) {
  const uint8_t timer_idx = bta_pm_action_to_timer_idx(power_mode);
  if (timer_idx == BTA_DM_PM_MODE_TIMER_MAX) return;

  for (int i = 0; i < BTA_DM_NUM_PM_TIMER; i++) {
    if (bta_dm_cb.pm_timer[i].in_use &&
        bta_dm_cb.pm_timer[i].peer_bdaddr == peer_addr) {
      if (bta_dm_cb.pm_timer[i].srvc_id[timer_idx] != BTA_ID_MAX) {
        bta_dm_pm_stop_timer_by_index(&bta_dm_cb.pm_timer[i], timer_idx);
        /*
         * TODO: Intentionally setting pm_action[timer_idx].
         * This assignment should be eliminated in the future - see the
         * pm_action[] related comment inside function
         * bta_dm_pm_stop_timer().
         */
        bta_dm_cb.pm_timer[i].pm_action[timer_idx] = power_mode;
      }
      break;
    }
  }
}

/*******************************************************************************
 *
 * Function         bta_dm_pm_stop_timer_by_srvc_id
 *
 * Description      stop all timer started by the service ID.
 *
 *
 * Returns          index of the power mode delay timer
 *
 ******************************************************************************/
static void bta_dm_pm_stop_timer_by_srvc_id(const RawAddress& peer_addr,
                                            uint8_t srvc_id) {
  for (int i = 0; i < BTA_DM_NUM_PM_TIMER; i++) {
    if (bta_dm_cb.pm_timer[i].in_use &&
        bta_dm_cb.pm_timer[i].peer_bdaddr == peer_addr) {
      for (int j = 0; j < BTA_DM_PM_MODE_TIMER_MAX; j++) {
        if (bta_dm_cb.pm_timer[i].srvc_id[j] == srvc_id) {
          bta_dm_pm_stop_timer_by_index(&bta_dm_cb.pm_timer[i], j);
          bta_dm_cb.pm_timer[i].pm_action[j] = BTA_DM_PM_NO_ACTION;
          break;
        }
      }
    }
  }
}

/*******************************************************************************
 *
 * Function         bta_dm_pm_start_timer
 *
 * Description      start a PM timer
 *
 *
 * Returns          void
 *
 ******************************************************************************/
static void bta_dm_pm_start_timer(tBTA_PM_TIMER* p_timer, uint8_t timer_idx,
                                  period_ms_t timeout_ms, uint8_t srvc_id,
                                  uint8_t pm_action) {
  std::unique_lock<std::recursive_mutex> schedule_lock(pm_timer_schedule_mutex);
  std::unique_lock<std::recursive_mutex> state_lock(pm_timer_state_mutex);
  p_timer->in_use = true;

  if (p_timer->srvc_id[timer_idx] == BTA_ID_MAX) p_timer->active++;

  if (p_timer->pm_action[timer_idx] < pm_action)
    p_timer->pm_action[timer_idx] = pm_action;

  p_timer->srvc_id[timer_idx] = srvc_id;
  state_lock.unlock();

  alarm_set_on_mloop(p_timer->timer[timer_idx], timeout_ms,
                     bta_dm_pm_timer_cback, p_timer->timer[timer_idx]);
}

/*******************************************************************************
 *
 * Function         bta_dm_pm_stop_timer_by_index
 *
 * Description      stop a PM timer
 *
 *
 * Returns          void
 *
 ******************************************************************************/
static void bta_dm_pm_stop_timer_by_index(tBTA_PM_TIMER* p_timer,
                                          uint8_t timer_idx) {
  if ((p_timer == NULL) || (timer_idx >= BTA_DM_PM_MODE_TIMER_MAX)) return;

  std::unique_lock<std::recursive_mutex> schedule_lock(pm_timer_schedule_mutex);
  std::unique_lock<std::recursive_mutex> state_lock(pm_timer_state_mutex);
  if (p_timer->srvc_id[timer_idx] == BTA_ID_MAX)
    return; /* The timer was not scheduled */

  CHECK(p_timer->in_use && (p_timer->active > 0));

  p_timer->srvc_id[timer_idx] = BTA_ID_MAX;
  /* NOTE: pm_action[timer_idx] intentionally not reset */

  p_timer->active--;
  if (p_timer->active == 0) p_timer->in_use = false;
  state_lock.unlock();

  alarm_cancel(p_timer->timer[timer_idx]);
}

/*******************************************************************************
 *
 * Function         bta_dm_pm_cback
 *
 * Description      Conn change callback from sys for low power management
 *
 *
 * Returns          void
 *
 ******************************************************************************/
static void bta_dm_pm_cback(tBTA_SYS_CONN_STATUS status, uint8_t id,
                            uint8_t app_id, const RawAddress& peer_addr) {
  uint8_t i, j;
  uint8_t* p = NULL;
  tBTA_DM_PEER_DEVICE* p_dev;
  tBTA_DM_PM_REQ pm_req = BTA_DM_PM_NEW_REQ;

#if (BTM_SSR_INCLUDED == TRUE)
  int index = BTA_DM_PM_SSR0;
#endif

  APPL_TRACE_DEBUG("bta_dm_pm_cback: st(%d), id(%d), app(%d)", status, id,
                   app_id);

  p_dev = bta_dm_find_peer_device(peer_addr);

  /* find if there is an power mode entry for the service */
  for (i = 1; i <= p_bta_dm_pm_cfg[0].app_id; i++) {
    if ((p_bta_dm_pm_cfg[i].id == id) &&
        ((p_bta_dm_pm_cfg[i].app_id == BTA_ALL_APP_ID) ||
         (p_bta_dm_pm_cfg[i].app_id == app_id)))
      break;
  }

  /* if no entries are there for the app_id and subsystem in p_bta_dm_pm_spec*/
  if (i > p_bta_dm_pm_cfg[0].app_id) return;

  bta_dm_pm_stop_timer_by_srvc_id(peer_addr, id);
/*p_dev = bta_dm_find_peer_device(peer_addr);*/

#if (BTM_SSR_INCLUDED == TRUE)
  /* set SSR parameters on SYS CONN OPEN */
  if ((BTA_SYS_CONN_OPEN == status) && p_dev &&
      (p_dev->info & BTA_DM_DI_USE_SSR)) {
    index = p_bta_dm_pm_spec[p_bta_dm_pm_cfg[i].spec_idx].ssr;
  }
#endif

  /* if no action for the event */
  if (p_bta_dm_pm_spec[p_bta_dm_pm_cfg[i].spec_idx]
          .actn_tbl[status][0]
          .power_mode == BTA_DM_PM_NO_ACTION) {
#if (BTM_SSR_INCLUDED == TRUE)
    if (BTA_DM_PM_SSR0 == index) /* and do not need to set SSR, return. */
#endif
      return;
  }

  for (j = 0; j < bta_dm_conn_srvcs.count && j < BTA_DM_NUM_CONN_SRVS; j++) {
    /* check if an entry already present */
    if ((bta_dm_conn_srvcs.conn_srvc[j].id == id) &&
        (bta_dm_conn_srvcs.conn_srvc[j].app_id == app_id) &&
        bta_dm_conn_srvcs.conn_srvc[j].peer_bdaddr == peer_addr) {
      bta_dm_conn_srvcs.conn_srvc[j].new_request = true;
      break;
    }
  }

  /* if subsystem has no more preference on the power mode remove
 the cb */
  if (p_bta_dm_pm_spec[p_bta_dm_pm_cfg[i].spec_idx]
          .actn_tbl[status][0]
          .power_mode == BTA_DM_PM_NO_PREF) {
    if (j != bta_dm_conn_srvcs.count) {
      bta_dm_conn_srvcs.count--;
      APPL_TRACE_DEBUG("%s: Removed power mode entry for service id = %d, count = %d",
                       __func__, p_bta_dm_pm_cfg[i].id, bta_dm_conn_srvcs.count);

      for (; j < bta_dm_conn_srvcs.count && j < BTA_DM_NUM_CONN_SRVS - 1; j++) {
        memcpy(&bta_dm_conn_srvcs.conn_srvc[j],
               &bta_dm_conn_srvcs.conn_srvc[j + 1],
               sizeof(bta_dm_conn_srvcs.conn_srvc[j]));
      }
    } else {
      APPL_TRACE_WARNING("bta_dm_act no entry for connected service cbs");
      return;
    }
  } else if (j == bta_dm_conn_srvcs.count) {
    /* check if we have more connected service that cbs */
    if (bta_dm_conn_srvcs.count == BTA_DM_NUM_CONN_SRVS) {
      APPL_TRACE_WARNING("bta_dm_act no more connected service cbs");
      return;
    }

    /* fill in a new cb */
    bta_dm_conn_srvcs.conn_srvc[j].id = id;
    bta_dm_conn_srvcs.conn_srvc[j].app_id = app_id;
    bta_dm_conn_srvcs.conn_srvc[j].new_request = true;
    bta_dm_conn_srvcs.conn_srvc[j].peer_bdaddr = peer_addr;

    APPL_TRACE_WARNING("new conn_srvc id:%d, app_id:%d", id, app_id);

    bta_dm_conn_srvcs.count++;
    bta_dm_conn_srvcs.conn_srvc[j].state = status;
    APPL_TRACE_WARNING("%s: new conn_srvc id:%d, app_id:%d count:%d", __func__,
                           id, app_id, bta_dm_conn_srvcs.count);
  } else {
    if (j < BTA_DM_NUM_CONN_SRVS) {
      /* no service is added or removed. only updating status. */
      bta_dm_conn_srvcs.conn_srvc[j].state = status;
    }
  }

  /* stop timer */
  bta_dm_pm_stop_timer(peer_addr);
  if (bta_dm_conn_srvcs.count > 0) {
    pm_req = BTA_DM_PM_RESTART;
    APPL_TRACE_DEBUG(
        "%s bta_dm_pm_stop_timer for current service, restart other "
        "service timers: count = %d",
        __func__, bta_dm_conn_srvcs.count);
  }

  if (p_dev) {
    p_dev->pm_mode_attempted = 0;
    p_dev->pm_mode_failed = 0;
  }

#if (BTM_SSR_INCLUDED == TRUE)
  if (p_bta_dm_ssr_spec[index].max_lat
#if (BTA_HH_INCLUDED == TRUE)
      || index == BTA_DM_PM_SSR_HH
#endif
    ) {
      if(((NULL != (p = BTM_ReadLocalFeatures ())) &&
          HCI_SNIFF_SUB_RATE_SUPPORTED(p)) &&
          ((NULL != (p = BTM_ReadRemoteFeatures (peer_addr))) &&
          HCI_SNIFF_SUB_RATE_SUPPORTED(p))) {
        /* If HID connection open is received and SCO is already active.
           This will handle the case where HID connects when SCO already active */
        if ((status == BTA_SYS_CONN_OPEN) && (id == BTA_ID_HH) &&
            bta_dm_pm_is_sco_active()) {
          APPL_TRACE_DEBUG("%s: SCO is Active, disabling "
              "SSR on HID link", __func__)
          BTM_SetSsrParams(peer_addr, 2, 0, 0);
        } else {
          bta_dm_pm_ssr(peer_addr);
        }
      }
    } else {
        if(((NULL != (p = BTM_ReadLocalFeatures ())) &&
            HCI_SNIFF_SUB_RATE_SUPPORTED(p)) &&
            ((NULL != (p = BTM_ReadRemoteFeatures (peer_addr))) &&
            HCI_SNIFF_SUB_RATE_SUPPORTED(p)) && (index == BTA_DM_PM_SSR0)) {
          if (status == BTA_SYS_SCO_OPEN) {
            APPL_TRACE_DEBUG("%s: SCO inactive, reset SSR to zero", __func__);
            BTM_SetSsrParams (peer_addr, 2,0,0 );
          } else if (status == BTA_SYS_SCO_CLOSE) {
            APPL_TRACE_DEBUG("%s: SCO active, back to old SSR", __func__);
            bta_dm_pm_ssr(peer_addr);
          }
        }
    }

    /* If HID connection open is received and SCO is already active.
       disable sniff link policy for some devices */
    if ((status == BTA_SYS_CONN_OPEN) && (id == BTA_ID_HH) &&
        bta_dm_pm_is_sco_active()) {
      /* Check if DUT is slave on SCO Link to decide if sniff needs to be disabled or not */
      uint8_t role_on_sco_link;
      int index = bta_dm_get_sco_index();

      if (index != -1) {
        BTM_GetRole(bta_dm_conn_srvcs.conn_srvc[index].peer_bdaddr,
          &role_on_sco_link);
        APPL_TRACE_DEBUG("%s: Role on SCO Link = %d", __func__, role_on_sco_link);
        if (role_on_sco_link == BTM_ROLE_SLAVE) {
          uint16_t manufacturer = 0;
          uint16_t  lmp_sub_version = 0;
          uint8_t lmp_version = 0;
          tBTA_DM_PEER_DEVICE *p_rem_dev = NULL;
          if (BTM_ReadRemoteVersion(peer_addr, &lmp_version,
              &manufacturer, &lmp_sub_version) == BTM_SUCCESS) {
            p_rem_dev = bta_dm_find_peer_device(peer_addr);
            /* Disable sniff policy on the HID link since SCO is Up on Slave Link */
            if ((p_rem_dev) && (interop_match_addr_or_name(
                INTEROP_DISABLE_SNIFF_DURING_SCO,
                &peer_addr) ||
                interop_match_manufacturer(
                INTEROP_DISABLE_SNIFF_DURING_SCO, manufacturer))) {
                APPL_TRACE_DEBUG("%s: disable sniff for manufacturer:%d "
                    "addr = %s", __func__, manufacturer, peer_addr.ToString().c_str());
                bta_dm_pm_set_sniff_policy(p_rem_dev, true);
            }
          }
        }
      }
    }

    /* If SCO up/down event is received, then
       1. Enable/disable SSR on active HID link
       2. Disable sniff mode for some HFP devices when SCO is active*/
    if (status == BTA_SYS_SCO_OPEN || status == BTA_SYS_SCO_CLOSE) {
       const bool bScoActive = (status == BTA_SYS_SCO_OPEN);
       APPL_TRACE_DEBUG("%s: bta_dm_pm_hid_check with bScoActive = %d", __func__, bScoActive);
       bta_dm_pm_hid_check(bScoActive);
       uint16_t manufacturer = 0;
       uint16_t lmp_sub_version = 0;
       uint8_t  lmp_version = 0;
       if (BTM_ReadRemoteVersion(peer_addr, &lmp_version,
           &manufacturer, &lmp_sub_version) == BTM_SUCCESS) {
          bool is_blacklisted =
             (interop_match_addr_or_name(INTEROP_DISABLE_SNIFF_LINK_DURING_SCO, &peer_addr) ||
              interop_match_manufacturer(INTEROP_DISABLE_SNIFF_LINK_DURING_SCO, manufacturer));
          bool is_blacklisted_for_call =
             interop_match_addr_or_name(INTEROP_DISABLE_SNIFF_DURING_CALL, &peer_addr);
          if ((id == BTA_ID_AG) && is_blacklisted &&
               !(is_blacklisted_for_call && bta_ag_is_call_present(&peer_addr))) {
             APPL_TRACE_IMP("%s: The device %s is blacklisted to disable sniff mode during SCO",
                             __func__, peer_addr.ToString().c_str());
             tBTA_DM_PEER_DEVICE *p_rem_dev = NULL;
             p_rem_dev = bta_dm_find_peer_device(peer_addr);
             if(p_rem_dev) {
                APPL_TRACE_IMP("%s: BTA_ID_AG with bScoActive = %d", __func__, bScoActive);
                bta_dm_pm_set_sniff_policy(p_rem_dev, bScoActive);
             }
          }
       }
    }
#endif

    bta_dm_pm_set_mode(peer_addr, BTA_DM_PM_NO_ACTION, pm_req);
}

/*******************************************************************************
 *
 * Function         bta_dm_pm_set_mode
 *
 * Description      Set the power mode for the device
 *
 *
 * Returns          void
 *
 ******************************************************************************/

static void bta_dm_pm_set_mode(const RawAddress& peer_addr,
                               tBTA_DM_PM_ACTION pm_request,
                               tBTA_DM_PM_REQ pm_req) {
  tBTA_DM_PM_ACTION pm_action = BTA_DM_PM_NO_ACTION;
  period_ms_t timeout_ms = 0;
  uint8_t i, j;
  tBTA_DM_PM_ACTION failed_pm = 0;
  tBTA_DM_PEER_DEVICE* p_peer_device = NULL;
  tBTA_DM_PM_ACTION allowed_modes = 0;
  tBTA_DM_PM_ACTION pref_modes = 0;
  tBTA_DM_PM_CFG* p_pm_cfg;
  tBTA_DM_PM_SPEC* p_pm_spec;
  tBTA_DM_PM_ACTN *p_act0, *p_act1;
  tBTA_DM_SRVCS* p_srvcs = NULL;
  bool timer_started = false;
  uint8_t timer_idx, available_timer = BTA_DM_PM_MODE_TIMER_MAX;
  period_ms_t remaining_ms = 0;

  if (!bta_dm_cb.device_list.count) return;

  /* see if any attempt to put device in low power mode failed */
  p_peer_device = bta_dm_find_peer_device(peer_addr);
  /* if no peer device found return */
  if (p_peer_device == NULL) return;

  failed_pm = p_peer_device->pm_mode_failed;

  for (i = 0; i < bta_dm_conn_srvcs.count && i < BTA_DM_NUM_CONN_SRVS; i++) {
    p_srvcs = &bta_dm_conn_srvcs.conn_srvc[i];
    if (p_srvcs->peer_bdaddr == peer_addr) {
      /* p_bta_dm_pm_cfg[0].app_id is the number of entries */
      for (j = 1; j <= p_bta_dm_pm_cfg[0].app_id; j++) {
        if ((p_bta_dm_pm_cfg[j].id == p_srvcs->id) &&
            ((p_bta_dm_pm_cfg[j].app_id == BTA_ALL_APP_ID) ||
             (p_bta_dm_pm_cfg[j].app_id == p_srvcs->app_id)))
          break;
      }

      p_pm_cfg = &p_bta_dm_pm_cfg[j];
      p_pm_spec = &p_bta_dm_pm_spec[p_pm_cfg->spec_idx];
      p_act0 = &p_pm_spec->actn_tbl[p_srvcs->state][0];
      p_act1 = &p_pm_spec->actn_tbl[p_srvcs->state][1];

      APPL_TRACE_DEBUG("bta_dm_pm_set_mode: srvcsid: %d, state: %d, j: %d",
                       p_srvcs->id, p_srvcs->state, j);
      allowed_modes |= p_pm_spec->allow_mask;

      /* PM actions are in the order of strictness */

      /* first check if the first preference is ok */
      if (!(failed_pm & p_act0->power_mode)) {
        pref_modes |= p_act0->power_mode;

        if (p_act0->power_mode >= pm_action) {
          pm_action = p_act0->power_mode;

          if (pm_req != BTA_DM_PM_NEW_REQ || p_srvcs->new_request) {
            p_srvcs->new_request = false;
            timeout_ms = p_act0->timeout;
          }
        }
      }
      /* if first preference has already failed, try second preference */
      else if (!(failed_pm & p_act1->power_mode)) {
        pref_modes |= p_act1->power_mode;

        if (p_act1->power_mode > pm_action) {
          pm_action = p_act1->power_mode;
          timeout_ms = p_act1->timeout;
        }
      }
    }
  }

  if (pm_action & (BTA_DM_PM_PARK | BTA_DM_PM_SNIFF)) {
    /* some service don't like the mode */
    if (!(allowed_modes & pm_action)) {
      /* select the other mode if its allowed and preferred, otherwise 0 which
       * is BTA_DM_PM_NO_ACTION */
      pm_action =
          (allowed_modes & (BTA_DM_PM_PARK | BTA_DM_PM_SNIFF) & pref_modes);

      /* no timeout needed if no action is required */
      if (pm_action == BTA_DM_PM_NO_ACTION) {
        timeout_ms = 0;
      }
    }
  }
  /* if need to start a timer */
  if ((pm_req != BTA_DM_PM_EXECUTE) && (timeout_ms > 0)) {
    for (i = 0; i < BTA_DM_NUM_PM_TIMER; i++) {
      if (bta_dm_cb.pm_timer[i].in_use &&
          bta_dm_cb.pm_timer[i].peer_bdaddr == peer_addr) {
        timer_idx = bta_pm_action_to_timer_idx(pm_action);
        if (timer_idx != BTA_DM_PM_MODE_TIMER_MAX) {
          remaining_ms =
              alarm_get_remaining_ms(bta_dm_cb.pm_timer[i].timer[timer_idx]);
          if (remaining_ms < timeout_ms) {
            /* Cancel and restart the timer */
            /*
             * TODO: The value of pm_action[timer_idx] is
             * conditionally updated between the two function
             * calls below when the timer is restarted.
             * This logic is error-prone and should be eliminated
             * in the future.
             */
            bta_dm_pm_stop_timer_by_index(&bta_dm_cb.pm_timer[i], timer_idx);
            bta_dm_pm_start_timer(&bta_dm_cb.pm_timer[i], timer_idx, timeout_ms,
                                  p_srvcs->id, pm_action);
          }
          timer_started = true;
        }
        break;
      } else if (!bta_dm_cb.pm_timer[i].in_use) {
        APPL_TRACE_DEBUG("%s dm_pm_timer:%d, %d ms", __func__, i, timeout_ms);
        if (available_timer == BTA_DM_PM_MODE_TIMER_MAX) available_timer = i;
      }
    }
    /* new power mode for a new active connection */
    if (!timer_started) {
      if (available_timer != BTA_DM_PM_MODE_TIMER_MAX) {
        bta_dm_cb.pm_timer[available_timer].peer_bdaddr = peer_addr;
        timer_idx = bta_pm_action_to_timer_idx(pm_action);
        if (timer_idx != BTA_DM_PM_MODE_TIMER_MAX) {
          bta_dm_pm_start_timer(&bta_dm_cb.pm_timer[available_timer], timer_idx,
                                timeout_ms, p_srvcs->id, pm_action);
          timer_started = true;
        }
      }
      /* no more timers */
      else {
        APPL_TRACE_WARNING("bta_dm_act dm_pm_timer no more");
      }
    }
    return;
  }
  /* if pending power mode timer expires, and currecnt link is in a
     lower power mode than current profile requirement, igonre it */
  if (pm_req == BTA_DM_PM_EXECUTE && pm_request < pm_action) {
    APPL_TRACE_ERROR("Ignore the power mode request: %d", pm_request)
    return;
  }
  if (pm_action == BTA_DM_PM_PARK) {
    p_peer_device->pm_mode_attempted = BTA_DM_PM_PARK;
    bta_dm_pm_park(peer_addr);
  } else if (pm_action & BTA_DM_PM_SNIFF) {
    /* dont initiate SNIFF, if link_policy has it disabled */
    if (p_peer_device->link_policy & HCI_ENABLE_SNIFF_MODE) {
      p_peer_device->pm_mode_attempted = BTA_DM_PM_SNIFF;
      bta_dm_pm_sniff(p_peer_device, (uint8_t)(pm_action & 0x0F));
    } else {
      APPL_TRACE_DEBUG(
          "bta_dm_pm_set_mode: Link policy disallows SNIFF, ignore request");
    }
  } else if (pm_action == BTA_DM_PM_ACTIVE) {
    bta_dm_pm_active(peer_addr);
  }
}
/*******************************************************************************
 *
 * Function         bta_ag_pm_park
 *
 * Description      Switch to park mode.
 *
 *
 * Returns          true if park attempted, false otherwise.
 *
 ******************************************************************************/
static bool bta_dm_pm_park(const RawAddress& peer_addr) {
  tBTM_PM_MODE mode = BTM_PM_STS_ACTIVE;

  /* if not in park mode, switch to park */
  BTM_ReadPowerMode(peer_addr, &mode);

  if (mode != BTM_PM_MD_PARK) {
    BTM_SetPowerMode(bta_dm_cb.pm_id, peer_addr,
                     &p_bta_dm_pm_md[BTA_DM_PM_PARK_IDX]);
  }
  return true;
}

/*******************************************************************************
 *
 * Function         bta_ag_pm_sniff
 *
 * Description      Switch to sniff mode.
 *
 *
 * Returns          true if sniff attempted, false otherwise.
 *
 ******************************************************************************/
static bool bta_dm_pm_sniff(tBTA_DM_PEER_DEVICE* p_peer_dev, uint8_t index) {
  tBTM_PM_MODE mode = BTM_PM_STS_ACTIVE;
  tBTM_PM_PWR_MD pwr_md;
  tBTM_STATUS status;
#if (BTM_SSR_INCLUDED == TRUE)
  uint8_t* p_rem_feat = NULL;
#endif

  BTM_ReadPowerMode(p_peer_dev->peer_bdaddr, &mode);
  p_rem_feat = BTM_ReadRemoteFeatures(p_peer_dev->peer_bdaddr);
#if (BTM_SSR_INCLUDED == TRUE)
  APPL_TRACE_DEBUG("bta_dm_pm_sniff cur:%d, idx:%d, info:x%x", mode, index,
                   p_peer_dev->info);
  if (mode != BTM_PM_MD_SNIFF ||
      (HCI_SNIFF_SUB_RATE_SUPPORTED(BTM_ReadLocalFeatures()) && p_rem_feat &&
       HCI_SNIFF_SUB_RATE_SUPPORTED(p_rem_feat) &&
       !(p_peer_dev->info & BTA_DM_DI_USE_SSR)))
#else
  APPL_TRACE_DEBUG("bta_dm_pm_sniff cur:%d, idx:%d", mode, index);
  if (mode != BTM_PM_MD_SNIFF)
#endif
  {
#if (BTM_SSR_INCLUDED == TRUE)
    /* Dont initiate Sniff if controller has alreay accepted
     * remote sniff params. This avoid sniff loop issue with
     * some agrresive headsets who use sniff latencies more than
     * DUT supported range of Sniff intervals.*/
    if ((mode == BTM_PM_MD_SNIFF) && (p_peer_dev->info & BTA_DM_DI_ACP_SNIFF)) {
      APPL_TRACE_DEBUG("%s: already in remote initiate sniff", __func__);
      return true;
    }
#endif
    /* if the current mode is not sniff, issue the sniff command.
     * If sniff, but SSR is not used in this link, still issue the command */
    memcpy(&pwr_md, &p_bta_dm_pm_md[index], sizeof(tBTM_PM_PWR_MD));
    if (p_peer_dev->info & BTA_DM_DI_INT_SNIFF) {
      pwr_md.mode |= BTM_PM_MD_FORCE;
    }
    status =
        BTM_SetPowerMode(bta_dm_cb.pm_id, p_peer_dev->peer_bdaddr, &pwr_md);
    if (status == BTM_CMD_STORED || status == BTM_CMD_STARTED) {
      p_peer_dev->info &= ~(BTA_DM_DI_INT_SNIFF | BTA_DM_DI_ACP_SNIFF);
      p_peer_dev->info |= BTA_DM_DI_SET_SNIFF;
    } else if (status == BTM_SUCCESS) {
      APPL_TRACE_DEBUG(
          "bta_dm_pm_sniff BTM_SetPowerMode() returns BTM_SUCCESS");
      p_peer_dev->info &=
          ~(BTA_DM_DI_INT_SNIFF | BTA_DM_DI_ACP_SNIFF | BTA_DM_DI_SET_SNIFF);
    } else /* error */
    {
      APPL_TRACE_ERROR(
          "bta_dm_pm_sniff BTM_SetPowerMode() returns ERROR status=%d", status);
      p_peer_dev->info &=
          ~(BTA_DM_DI_INT_SNIFF | BTA_DM_DI_ACP_SNIFF | BTA_DM_DI_SET_SNIFF);
    }
  }
  return true;
}
/*******************************************************************************
 *
 * Function         bta_dm_pm_ssr
 *
 * Description      checks and sends SSR parameters
 *
 * Returns          void
 *
 ******************************************************************************/
#if (BTM_SSR_INCLUDED == TRUE)
static void bta_dm_pm_ssr(const RawAddress& peer_addr) {
  tBTA_DM_SSR_SPEC *p_spec, *p_spec_cur;
  uint8_t i, j;
  int ssr = BTA_DM_PM_SSR0;
  char remote_name[BTM_MAX_REM_BD_NAME_LEN] = "";
  btif_storage_get_stored_remote_name(peer_addr, remote_name);

  /* go through the connected services */
  for (i = 0; i < bta_dm_conn_srvcs.count && i < BTA_DM_NUM_CONN_SRVS; i++) {
    if (bta_dm_conn_srvcs.conn_srvc[i].peer_bdaddr == peer_addr) {
      /* p_bta_dm_pm_cfg[0].app_id is the number of entries */
      for (j = 1; j <= p_bta_dm_pm_cfg[0].app_id; j++) {
        /* find the associated p_bta_dm_pm_cfg */
        if ((p_bta_dm_pm_cfg[j].id == bta_dm_conn_srvcs.conn_srvc[i].id) &&
            ((p_bta_dm_pm_cfg[j].app_id == BTA_ALL_APP_ID) ||
             (p_bta_dm_pm_cfg[j].app_id ==
              bta_dm_conn_srvcs.conn_srvc[i].app_id))) {
          APPL_TRACE_WARNING("bta_dm_pm_ssr conn_srvc id:%d, app_id:%d",
                             bta_dm_conn_srvcs.conn_srvc[i].id,
                             bta_dm_conn_srvcs.conn_srvc[i].app_id);
          break;
        }
      }

      /* find the ssr index with the smallest max latency. */
      p_spec_cur =
          &p_bta_dm_ssr_spec[p_bta_dm_pm_spec[p_bta_dm_pm_cfg[j].spec_idx].ssr];
      p_spec = &p_bta_dm_ssr_spec[ssr];

#if (BTA_HH_INCLUDED == TRUE)
      /* HH has the per connection SSR preference, already read the SSR params
       * from BTA HH */
      if (p_bta_dm_pm_spec[p_bta_dm_pm_cfg[j].spec_idx].ssr ==
          BTA_DM_PM_SSR_HH) {
        if (bta_hh_read_ssr_param(peer_addr, &p_spec_cur->max_lat,
                                  &p_spec_cur->min_rmt_to) == BTA_HH_ERR)
          continue;
        if (p_spec_cur->max_lat == BTA_HH_SSR_MAX_LATENCY_ZERO) {
          APPL_TRACE_WARNING("%s: Max latency is 0, not sending"
              "SSR command as device is blacklisted", __func__);
          return;
        } else if (p_spec_cur->max_lat == BTA_HH_SSR_DISABLE_SSR) {
          APPL_TRACE_WARNING("%s: Need to disable SSR"
              "as device is blacklisted", __func__);
          BTM_SetSsrParams (peer_addr, 2, 0, 0);
          return;
        } else if (p_spec_cur->max_lat < BTA_HH_SSR_MAX_LATENCY_MIN_OPTIMAL &&
            !(interop_match_name(INTEROP_HID_HOST_LIMIT_SNIFF_INTERVAL, remote_name) ||
              interop_match_addr(INTEROP_HID_HOST_LIMIT_SNIFF_INTERVAL, &peer_addr)) ) {
            p_spec_cur->max_lat = BTA_HH_SSR_MAX_LATENCY_MIN_OPTIMAL;
        }
        APPL_TRACE_DEBUG("%s: New Max Latency = %d", __func__, p_spec_cur->max_lat);
      }
#endif
      if (p_spec_cur->max_lat < p_spec->max_lat ||
          (ssr == BTA_DM_PM_SSR0 &&
           p_bta_dm_pm_spec[p_bta_dm_pm_cfg[j].spec_idx].ssr !=
               BTA_DM_PM_SSR0)) {
        ssr = p_bta_dm_pm_spec[p_bta_dm_pm_cfg[j].spec_idx].ssr;
      }
    }
  }

  p_spec = &p_bta_dm_ssr_spec[ssr];
  APPL_TRACE_WARNING("%s ssr:%d, lat:%d", __func__, ssr, p_spec->max_lat);

  if (p_spec->max_lat) {
    /* Avoid SSR reset on device which has SCO connected */
    if (bta_dm_pm_is_sco_active()) {
      int idx = bta_dm_get_sco_index();
      if (idx != -1) {
        if (bta_dm_conn_srvcs.conn_srvc[idx].peer_bdaddr == peer_addr) {
          APPL_TRACE_WARNING("%s SCO is active on device, ignore SSR",
                             __func__);
          return;
        }
      }
    }

    /* set the SSR parameters. */
    BTM_SetSsrParams(peer_addr, p_spec->max_lat, p_spec->min_rmt_to,
                     p_spec->min_loc_to);
  }
}
#endif
/*******************************************************************************
 *
 * Function         bta_dm_pm_active
 *
 * Description      Brings connection to active mode
 *
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_dm_pm_active(const RawAddress& peer_addr) {
  tBTM_PM_PWR_MD pm;

  memset((void*)&pm, 0, sizeof(pm));

  /* switch to active mode */
  pm.mode = BTM_PM_MD_ACTIVE;
  BTM_SetPowerMode(bta_dm_cb.pm_id, peer_addr, &pm);
}

/*******************************************************************************
 *
 * Function         bta_dm_pm_btm_cback
 *
 * Description      BTM power manager callback.
 *
 *
 * Returns          void
 *
 ******************************************************************************/
static void bta_dm_pm_btm_cback(const RawAddress& bd_addr,
                                tBTM_PM_STATUS status, uint16_t value,
                                uint8_t hci_status) {
  tBTA_DM_PM_BTM_STATUS* p_buf =
      (tBTA_DM_PM_BTM_STATUS*)osi_malloc(sizeof(tBTA_DM_PM_BTM_STATUS));

  p_buf->hdr.event = BTA_DM_PM_BTM_STATUS_EVT;
  p_buf->status = status;
  p_buf->value = value;
  p_buf->hci_status = hci_status;
  p_buf->bd_addr = bd_addr;

  bta_sys_sendmsg(p_buf);
}

/*******************************************************************************
 *
 * Function         bta_dm_pm_timer_cback
 *
 * Description      Power management timer callback.
 *
 *
 * Returns          void
 *
 ******************************************************************************/
static void bta_dm_pm_timer_cback(void* data) {
  uint8_t i, j;
  alarm_t* alarm = (alarm_t*)data;

  std::unique_lock<std::recursive_mutex> state_lock(pm_timer_state_mutex);
  for (i = 0; i < BTA_DM_NUM_PM_TIMER; i++) {
    APPL_TRACE_DEBUG("dm_pm_timer[%d] in use? %d", i,
                     bta_dm_cb.pm_timer[i].in_use);
    if (bta_dm_cb.pm_timer[i].in_use) {
      for (j = 0; j < BTA_DM_PM_MODE_TIMER_MAX; j++) {
        if (bta_dm_cb.pm_timer[i].timer[j] == alarm) {
          bta_dm_cb.pm_timer[i].active--;
          bta_dm_cb.pm_timer[i].srvc_id[j] = BTA_ID_MAX;
          APPL_TRACE_DEBUG("dm_pm_timer[%d] expires, timer_idx=%d", i, j);
          break;
        }
      }
      if (bta_dm_cb.pm_timer[i].active == 0)
        bta_dm_cb.pm_timer[i].in_use = false;
      if (j < BTA_DM_PM_MODE_TIMER_MAX) break;
    }
  }
  state_lock.unlock();

  /* no more timers */
  if (i == BTA_DM_NUM_PM_TIMER) return;

  tBTA_DM_PM_TIMER* p_buf =
      (tBTA_DM_PM_TIMER*)osi_malloc(sizeof(tBTA_DM_PM_TIMER));
  p_buf->hdr.event = BTA_DM_PM_TIMER_EVT;
  p_buf->pm_request = bta_dm_cb.pm_timer[i].pm_action[j];
  p_buf->bd_addr = bta_dm_cb.pm_timer[i].peer_bdaddr;

  bta_sys_sendmsg(p_buf);
}

/*******************************************************************************
 *
 * Function         bta_dm_pm_btm_status
 *
 * Description      Process pm status event from btm
 *
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_dm_pm_btm_status(tBTA_DM_MSG* p_data) {
  APPL_TRACE_DEBUG("%s status: %d", __func__, p_data->pm_status.status);

  tBTA_DM_PEER_DEVICE* p_dev =
      bta_dm_find_peer_device(p_data->pm_status.bd_addr);
  if (NULL == p_dev) return;

  tBTA_DM_DEV_INFO info = p_dev->info;
  /* check new mode */
  switch (p_data->pm_status.status) {
    case BTM_PM_STS_ACTIVE:
      /* if our sniff or park attempt failed
      we should not try it again*/
      if (p_data->pm_status.hci_status != 0) {
        APPL_TRACE_ERROR("%s hci_status=%d", __func__,
                         p_data->pm_status.hci_status);
        p_dev->info &=
            ~(BTA_DM_DI_INT_SNIFF | BTA_DM_DI_ACP_SNIFF | BTA_DM_DI_SET_SNIFF);

        if (p_dev->pm_mode_attempted & (BTA_DM_PM_PARK | BTA_DM_PM_SNIFF)) {
          p_dev->pm_mode_failed |=
              ((BTA_DM_PM_PARK | BTA_DM_PM_SNIFF) & p_dev->pm_mode_attempted);
          bta_dm_pm_stop_timer_by_mode(p_data->pm_status.bd_addr,
                                       p_dev->pm_mode_attempted);
          bta_dm_pm_set_mode(p_data->pm_status.bd_addr, BTA_DM_PM_NO_ACTION,
                             BTA_DM_PM_RESTART);
        }
      } else {
#if (BTM_SSR_INCLUDED == TRUE)
        if (p_dev->prev_low) {
          /* need to send the SSR paramaters to controller again */
          bta_dm_pm_ssr(p_dev->peer_bdaddr);
        }
        p_dev->prev_low = BTM_PM_STS_ACTIVE;
#endif
        /* link to active mode, need to restart the timer for next low power
         * mode if needed */
        bta_dm_pm_stop_timer(p_data->pm_status.bd_addr);
        bta_dm_pm_set_mode(p_data->pm_status.bd_addr, BTA_DM_PM_NO_ACTION,
                           BTA_DM_PM_RESTART);
      }
      break;

#if (BTM_SSR_INCLUDED == TRUE)
    case BTM_PM_STS_PARK:
    case BTM_PM_STS_HOLD:
      /* save the previous low power mode - for SSR.
       * SSR parameters are sent to controller on "conn open".
       * the numbers stay good until park/hold/detach */
      if (p_dev->info & BTA_DM_DI_USE_SSR)
        p_dev->prev_low = p_data->pm_status.status;
      break;

    case BTM_PM_STS_SSR:
      if (p_data->pm_status.value)
        p_dev->info |= BTA_DM_DI_USE_SSR;
      else
        p_dev->info &= ~BTA_DM_DI_USE_SSR;
      break;
#endif
    case BTM_PM_STS_SNIFF:
      if (p_data->pm_status.hci_status == 0) {
        /* Stop PM timer now if already active for
         * particular device since link is already
         * put in sniff mode by remote device, and
         * PM timer sole purpose is to put the link
         * in sniff mode from host side.
         */
        bta_dm_pm_stop_timer(p_data->pm_status.bd_addr);
      } else {
        p_dev->info &=
            ~(BTA_DM_DI_SET_SNIFF | BTA_DM_DI_INT_SNIFF | BTA_DM_DI_ACP_SNIFF);
        if (info & BTA_DM_DI_SET_SNIFF)
          p_dev->info |= BTA_DM_DI_INT_SNIFF;
        else
          p_dev->info |= BTA_DM_DI_ACP_SNIFF;
      }
      break;

    case BTM_PM_STS_ERROR:
      p_dev->info &= ~BTA_DM_DI_SET_SNIFF;
      break;

    default:
      break;
  }
}

/*******************************************************************************
 *
 * Function         bta_dm_pm_timer
 *
 * Description      Process pm timer event from btm
 *
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_dm_pm_timer(tBTA_DM_MSG* p_data) {
  APPL_TRACE_EVENT("%s", __func__);
  bta_dm_pm_set_mode(p_data->pm_timer.bd_addr, p_data->pm_timer.pm_request,
                     BTA_DM_PM_EXECUTE);
}

/*******************************************************************************
 *
 * Function         bta_dm_find_peer_device
 *
 * Description      Given an address, find the associated control block.
 *
 * Returns          tBTA_DM_PEER_DEVICE
 *
 ******************************************************************************/
tBTA_DM_PEER_DEVICE* bta_dm_find_peer_device(const RawAddress& peer_addr) {
  tBTA_DM_PEER_DEVICE* p_dev = NULL;

  for (int i = 0; i < bta_dm_cb.device_list.count; i++) {
    if (bta_dm_cb.device_list.peer_device[i].peer_bdaddr == peer_addr) {
      p_dev = &bta_dm_cb.device_list.peer_device[i];
      break;
    }
  }
  return p_dev;
}

/*******************************************************************************
 *
 * Function         bta_dm_is_sco_active
 *
 * Description      Loop through connected services for HFP+State=SCO
 *
 * Returns          bool. true if SCO active, else false
 *
 ******************************************************************************/
static bool bta_dm_pm_is_sco_active() {
  int j;
  bool bScoActive = false;

  for (j = 0; j < bta_dm_conn_srvcs.count && j < BTA_DM_NUM_CONN_SRVS; j++) {
    /* check if an entry already present */
    if ((bta_dm_conn_srvcs.conn_srvc[j].id == BTA_ID_AG) &&
        (bta_dm_conn_srvcs.conn_srvc[j].state == BTA_SYS_SCO_OPEN)) {
      bScoActive = true;
      break;
    }
  }

  APPL_TRACE_DEBUG("bta_dm_is_sco_active: SCO active: %d", bScoActive);
  return bScoActive;
}

/*******************************************************************************
 *
 * Function        bta_dm_get_sco_index
 *
 * Description     Loop through connected services for HFP+State=SCO
 *
 * Returns         index at which SCO is connected, in absence of SCO return -1
 *
 ******************************************************************************/
static int bta_dm_get_sco_index() {
  for (int j = 0; j < bta_dm_conn_srvcs.count && j < BTA_DM_NUM_CONN_SRVS; j++) {
    /* check for SCO connected index */
    if ((bta_dm_conn_srvcs.conn_srvc[j].id == BTA_ID_AG) &&
        (bta_dm_conn_srvcs.conn_srvc[j].state == BTA_SYS_SCO_OPEN)) {
      return j;
    }
  }
  return -1;
}

/*******************************************************************************
 *
 * Function         bta_dm_pm_hid_check
 *
 * Description      Disables/Enables SSR based on SCO Up/Down
 *
 * Returns          None
 *
 ******************************************************************************/
static void bta_dm_pm_hid_check(bool bScoActive)
{
  uint8_t role_on_sco_link;
  int index = bta_dm_get_sco_index();

  if (index == -1)
    return;

  if (bScoActive) {
    BTM_GetRole(bta_dm_conn_srvcs.conn_srvc[index].peer_bdaddr,
      &role_on_sco_link);
    APPL_TRACE_DEBUG("%s: Role on SCO Link = %d", __func__, role_on_sco_link);
  }

  int j;

  for(j = 0; j < bta_dm_conn_srvcs.count && j < BTA_DM_NUM_CONN_SRVS; j++) {
    /* check if an entry already present */
    if(((bScoActive && (role_on_sco_link == BTM_ROLE_SLAVE)) ||
        !bScoActive) && bta_dm_conn_srvcs.conn_srvc[j].id == BTA_ID_HH) {
      uint16_t manufacturer = 0;
      uint16_t  lmp_sub_version = 0;
      uint8_t lmp_version = 0;
      tBTA_DM_PEER_DEVICE *p_rem_dev = NULL;
      uint8_t *p = BTM_ReadLocalFeatures();
      if (BTM_ReadRemoteVersion(bta_dm_conn_srvcs.conn_srvc[j].peer_bdaddr, &lmp_version,
          &manufacturer, &lmp_sub_version) == BTM_SUCCESS) {
          p_rem_dev = bta_dm_find_peer_device(bta_dm_conn_srvcs.conn_srvc[j].peer_bdaddr);
        /* Disable/Enable sniff policy on the HID link if SCO Up/Down*/
        if ((p_rem_dev) && (interop_match_addr_or_name(
            INTEROP_DISABLE_SNIFF_DURING_SCO,
            &(bta_dm_conn_srvcs.conn_srvc[j].peer_bdaddr)) ||
            interop_match_manufacturer(
            INTEROP_DISABLE_SNIFF_DURING_SCO, manufacturer))) {
          APPL_TRACE_DEBUG("%s: %s sniff for manufacturer:%d ",
              __func__, bScoActive ? "disable" : "enable",
              manufacturer);
          bta_dm_pm_set_sniff_policy(p_rem_dev, bScoActive);
           /* Put link in sniff with specific parameters since SCO is disconnected */
          if (!bScoActive)
            /*
             * Put HID link in sniff also with specific HID Sniff parameters as remote
             * device might not attempt sniff in case SCO is connected for longer time.
             */
            bta_dm_pm_sniff(p_rem_dev, (BTA_DM_PM_SNIFF7 & 0x0F));
        }
      }
      if((p != NULL && HCI_SNIFF_SUB_RATE_SUPPORTED(p))
          &&((NULL != (p = BTM_ReadRemoteFeatures (bta_dm_conn_srvcs.conn_srvc[j].peer_bdaddr)))
          && HCI_SNIFF_SUB_RATE_SUPPORTED(p))) {
        if (bScoActive) {
          APPL_TRACE_DEBUG("%s: SCO_OPEN, disabling SSR", __func__);
          BTM_SetSsrParams(bta_dm_conn_srvcs.conn_srvc[j].peer_bdaddr, 2, 0, 0);
        } else {
          APPL_TRACE_DEBUG("%s: SCO_CLOSE, enabling SSR", __func__);
          bta_dm_pm_ssr(bta_dm_conn_srvcs.conn_srvc[j].peer_bdaddr);
        }
      }
    }
  }
}

/*******************************************************************************
 *
 * Function         bta_dm_pm_set_sniff_policy
 *
 * Description      Disables/Enables sniff in link policy for the give device
 *
 * Returns          None
 *
 ******************************************************************************/
static void bta_dm_pm_set_sniff_policy(tBTA_DM_PEER_DEVICE* p_dev,
                                       bool bDisable) {
  uint16_t policy_setting;

  if (!p_dev) return;

  if (bDisable) {
    policy_setting =
        bta_dm_cb.cur_policy & (HCI_ENABLE_MASTER_SLAVE_SWITCH |
                                HCI_ENABLE_HOLD_MODE | HCI_ENABLE_PARK_MODE);

  } else {
    /*  allow sniff after sco is closed */
    policy_setting = bta_dm_cb.cur_policy;
  }

  /* if disabling SNIFF, make sure link is Active */
  if (bDisable) bta_dm_pm_active(p_dev->peer_bdaddr);

  /* update device record and set link policy */
  p_dev->link_policy = policy_setting;
  BTM_SetLinkPolicy(p_dev->peer_bdaddr, &policy_setting);
}

/*******************************************************************************
 *
 * Function         bta_dm_pm_obtain_controller_state
 *
 * Description      This function obtains the consolidated controller power
 *                  state
 *
 * Parameters:
 *
 ******************************************************************************/
tBTA_DM_CONTRL_STATE bta_dm_pm_obtain_controller_state(void) {
  /*   Did not use counts as it is not sure, how accurate the count values are
   *in
   **  bta_dm_cb.device_list.count > 0 || bta_dm_cb.device_list.le_count > 0 */

  tBTA_DM_CONTRL_STATE cur_state = BTA_DM_CONTRL_UNKNOWN;
  cur_state = BTM_PM_ReadControllerState();

  APPL_TRACE_DEBUG("bta_dm_pm_obtain_controller_state: %d", cur_state);
  return cur_state;
}
