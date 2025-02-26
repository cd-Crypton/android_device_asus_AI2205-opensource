/******************************************************************************
 *
 *  Copyright (C) 1999-2012 Broadcom Corporation
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
 *  This file contains the L2CAP channel state machine
 *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bt_common.h"
#include "bt_target.h"
#include "btm_int.h"
#include "btu.h"
#include "hcidefs.h"
#include "hcimsgs.h"
#include "l2c_int.h"
#include "l2cdefs.h"
#include "device/include/interop.h"

/******************************************************************************/
/*            L O C A L    F U N C T I O N     P R O T O T Y P E S            */
/******************************************************************************/
static void l2c_csm_closed(tL2C_CCB* p_ccb, uint16_t event, void* p_data);
static void l2c_csm_orig_w4_sec_comp(tL2C_CCB* p_ccb, uint16_t event,
                                     void* p_data);
static void l2c_csm_term_w4_sec_comp(tL2C_CCB* p_ccb, uint16_t event,
                                     void* p_data);
static void l2c_csm_w4_l2cap_connect_rsp(tL2C_CCB* p_ccb, uint16_t event,
                                         void* p_data);
static void l2c_csm_w4_l2ca_connect_rsp(tL2C_CCB* p_ccb, uint16_t event,
                                        void* p_data);
static void l2c_csm_config(tL2C_CCB* p_ccb, uint16_t event, void* p_data);
static void l2c_csm_open(tL2C_CCB* p_ccb, uint16_t event, void* p_data);
static void l2c_csm_w4_l2cap_disconnect_rsp(tL2C_CCB* p_ccb, uint16_t event,
                                            void* p_data);
static void l2c_csm_w4_l2ca_disconnect_rsp(tL2C_CCB* p_ccb, uint16_t event,
                                           void* p_data);

static const char* l2c_csm_get_event_name(uint16_t event);

/*******************************************************************************
 *
 * Function         l2c_csm_execute
 *
 * Description      This function executes the state machine.
 *
 * Returns          void
 *
 ******************************************************************************/
void l2c_csm_execute(tL2C_CCB* p_ccb, uint16_t event, void* p_data) {
  if ((!l2cu_is_ccb_active(p_ccb)) || (p_ccb->p_lcb == NULL)) {
    L2CAP_TRACE_WARNING("%s CCB not in use, event (%d) cannot be processed",
                        __func__, event);
    return;
  }

  switch (p_ccb->chnl_state) {
    case CST_CLOSED:
      l2c_csm_closed(p_ccb, event, p_data);
      break;

    case CST_ORIG_W4_SEC_COMP:
      l2c_csm_orig_w4_sec_comp(p_ccb, event, p_data);
      break;

    case CST_TERM_W4_SEC_COMP:
      l2c_csm_term_w4_sec_comp(p_ccb, event, p_data);
      break;

    case CST_W4_L2CAP_CONNECT_RSP:
      l2c_csm_w4_l2cap_connect_rsp(p_ccb, event, p_data);
      break;

    case CST_W4_L2CA_CONNECT_RSP:
      l2c_csm_w4_l2ca_connect_rsp(p_ccb, event, p_data);
      break;

    case CST_CONFIG:
      l2c_csm_config(p_ccb, event, p_data);
      break;

    case CST_OPEN:
      l2c_csm_open(p_ccb, event, p_data);
      break;

    case CST_W4_L2CAP_DISCONNECT_RSP:
      l2c_csm_w4_l2cap_disconnect_rsp(p_ccb, event, p_data);
      break;

    case CST_W4_L2CA_DISCONNECT_RSP:
      l2c_csm_w4_l2ca_disconnect_rsp(p_ccb, event, p_data);
      break;

    default:
      L2CAP_TRACE_DEBUG("Unhandled event! event = %d", event);
      break;
  }
}

/*******************************************************************************
 *
 * Function         l2c_csm_closed
 *
 * Description      This function handles events when the channel is in
 *                  CLOSED state. This state exists only when the link is
 *                  being initially established.
 *
 * Returns          void
 *
 ******************************************************************************/
static void l2c_csm_closed(tL2C_CCB* p_ccb, uint16_t event, void* p_data) {
  tL2C_CONN_INFO* p_ci = (tL2C_CONN_INFO*)p_data;
  uint16_t local_cid = p_ccb->local_cid;
  tL2CA_DISCONNECT_IND_CB* disconnect_ind;
  tL2CA_CONNECT_CFM_CB* connect_cfm;
  uint16_t sr_cids[5] = {0};

  if (p_ccb->p_rcb == NULL) {
    L2CAP_TRACE_ERROR("L2CAP - LCID: 0x%04x  st: CLOSED  evt: %s p_rcb == NULL",
                      p_ccb->local_cid, l2c_csm_get_event_name(event));
    return;
  }

#if (L2CAP_UCD_INCLUDED == TRUE)
  if (local_cid == L2CAP_CONNECTIONLESS_CID) {
    /* check if this event can be processed by UCD */
    if (l2c_ucd_process_event(p_ccb, event, p_data)) {
      /* The event is processed by UCD state machine */
      return;
    }
  }
#endif

  disconnect_ind = p_ccb->p_rcb->api.pL2CA_DisconnectInd_Cb;
  connect_cfm = p_ccb->p_rcb->api.pL2CA_ConnectCfm_Cb;

  L2CAP_TRACE_WARNING("L2CAP - LCID: 0x%04x  st: CLOSED  evt: %s",
                    p_ccb->local_cid, l2c_csm_get_event_name(event));
  //TODO print other chnl_ids if its in ECFC mode

  switch (event) {
    case L2CEVT_LP_DISCONNECT_IND: /* Link was disconnected */
      if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        L2CAP_TRACE_API(
            "L2CAP - Calling Disconnect_Ind_Cb(), CID: 0x%04x  No Conf Needed",
            p_ccb->local_cid);
        l2cu_release_ccb(p_ccb);
        (*disconnect_ind)(local_cid, false);
      } else {
        L2CAP_TRACE_API(
          "L2CAP - Calling ECFC Disconnect_Ind_Cb(), CID: 0x%04x"
          "No Conf Needed", p_ccb->local_cid);
        uint8_t num_chnls = p_ccb->coc_cmd_info.num_coc_chnls;
        tL2CA_DISCONNECT_IND_CB* disconnect_ind =
          p_ccb->p_rcb->coc_api.pL2CA_DisconnectInd_Cb;
        l2cu_get_coc_local_cids(sr_cids, p_ccb);
        l2cu_release_coc_ccbs(p_ccb);
        l2cu_send_disconnect_ind(sr_cids, num_chnls, disconnect_ind);
      }
      break;

    case L2CEVT_LP_CONNECT_CFM: /* Link came up         */
      if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        if (p_ccb->p_lcb->transport == BT_TRANSPORT_LE) {
          p_ccb->chnl_state = CST_ORIG_W4_SEC_COMP;
          l2ble_sec_access_req(p_ccb->p_lcb->remote_bd_addr, p_ccb->p_rcb->psm,
                              true, &l2c_link_sec_comp2, p_ccb);
        } else {
          p_ccb->chnl_state = CST_ORIG_W4_SEC_COMP;
          btm_sec_l2cap_access_req(p_ccb->p_lcb->remote_bd_addr,
                                  p_ccb->p_rcb->psm, p_ccb->p_lcb->handle, true,
                                  &l2c_link_sec_comp, p_ccb);
        }
      } else {
        l2cu_set_ecfc_grp_status(p_ccb, L2CAP_ECFC_LINK_CONNECT_CFM_CB);
        l2cu_set_coc_chnl_state(p_ccb, CST_ORIG_W4_SEC_COMP);
        if (p_ccb->p_lcb->transport == BT_TRANSPORT_LE) {
          l2ble_sec_access_req(p_ccb->p_lcb->remote_bd_addr, p_ccb->p_rcb->psm,
                               true, &l2c_link_sec_comp2, p_ccb);
        } else {
          btm_sec_l2cap_access_req(p_ccb->p_lcb->remote_bd_addr,
                                p_ccb->p_rcb->psm, p_ccb->p_lcb->handle, true,
                                &l2c_link_sec_comp, p_ccb);
        }
      }
      break;

    case L2CEVT_LP_CONNECT_CFM_NEG: /* Link failed          */
      /* Disconnect unless ACL collision and upper layer wants to handle it */
      if (((p_ci->status != HCI_ERR_CONNECTION_EXISTS) && (p_ci->status != HCI_ERR_CONTROLLER_BUSY))
          || interop_match_addr_or_name(INTEROP_DISABLE_CONNECTION_AFTER_COLLISION, &p_ccb->p_lcb->remote_bd_addr)
          || !btm_acl_notif_conn_collision(p_ccb->p_lcb->remote_bd_addr)) {
        if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
          L2CAP_TRACE_API(
              "L2CAP - Calling ConnectCfm_Cb(), CID: 0x%04x  Status: %d",
              p_ccb->local_cid, p_ci->status);
          l2cu_release_ccb(p_ccb);
          (*connect_cfm)(local_cid, p_ci->status);
        } else {
          tL2CA_COC_CONNECT_CFM_CB *coc_connect_cfm
            = p_ccb->p_rcb->coc_api.pL2CA_CocConnectCfm_Cb;
          RawAddress p_bd_addr = p_ccb->p_lcb->remote_bd_addr;
          tL2CAP_COC_CHMAP_INFO chmap_info = {};
          l2cu_release_coc_ccbs(p_ccb);
          (*coc_connect_cfm)(p_bd_addr, &chmap_info, 0, p_ci->l2cap_result,
              p_ci->status);
        }
      }
      break;

    case L2CEVT_L2CA_CONNECT_REQ: /* API connect request  */
      if (p_ccb->p_lcb->transport == BT_TRANSPORT_LE) {
        p_ccb->chnl_state = CST_ORIG_W4_SEC_COMP;
        l2ble_sec_access_req(p_ccb->p_lcb->remote_bd_addr, p_ccb->p_rcb->psm,
                             true, &l2c_link_sec_comp2, p_ccb);
      } else {
        /* Cancel sniff mode if needed */
        {
          tBTM_PM_PWR_MD settings;
          memset((void*)&settings, 0, sizeof(settings));
          settings.mode = BTM_PM_MD_ACTIVE;

          BTM_SetPowerMode(BTM_PM_SET_ONLY_ID, p_ccb->p_lcb->remote_bd_addr,
                           &settings);
        }

        /* If sec access does not result in started SEC_COM or COMP_NEG are
         * already processed */
        if (btm_sec_l2cap_access_req(p_ccb->p_lcb->remote_bd_addr,
                                     p_ccb->p_rcb->psm, p_ccb->p_lcb->handle,
                                     true, &l2c_link_sec_comp,
                                     p_ccb) == BTM_CMD_STARTED)
          p_ccb->chnl_state = CST_ORIG_W4_SEC_COMP;
      }
      break;

    case L2CEVT_SEC_COMP:
      if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        p_ccb->chnl_state = CST_W4_L2CAP_CONNECT_RSP;

        /* Wait for the info resp in this state before sending connect req (if
         * needed) */
        if (!p_ccb->p_lcb->w4_info_rsp) {
          /* Need to have at least one compatible channel to continue */
          if (!l2c_fcr_chk_chan_modes(p_ccb)) {
            l2cu_release_ccb(p_ccb);
            (*p_ccb->p_rcb->api.pL2CA_ConnectCfm_Cb)(local_cid,
                                                     L2CAP_CONN_NO_LINK);
          } else {
            l2cu_send_peer_connect_req(p_ccb);
            alarm_set_on_mloop(p_ccb->l2c_ccb_timer,
                               L2CAP_CHNL_CONNECT_TIMEOUT_MS,
                               l2c_ccb_timer_timeout, p_ccb);
          }
        }
      } else {
        l2cu_set_coc_chnl_state(p_ccb, CST_W4_L2CAP_CONNECT_RSP);
        /* Wait for the info resp in this state before sending connect req (if
         * needed) */
        if (!p_ccb->p_lcb->w4_info_rsp) {
          /* Need to have ECFC compatible channel to continue */
          if (!l2c_fcr_chk_remote_ecfc_support(p_ccb)) {
            tL2CA_COC_CONNECT_CFM_CB *coc_connect_cfm
              = p_ccb->p_rcb->coc_api.pL2CA_CocConnectCfm_Cb;
            RawAddress p_bd_addr = p_ccb->p_lcb->remote_bd_addr;
            tL2CAP_COC_CHMAP_INFO chmap_info = {};
            l2cu_release_coc_ccbs(p_ccb);
            (*coc_connect_cfm)(p_bd_addr, &chmap_info, 0, L2CAP_CONN_NO_LINK,
              L2CAP_CONN_NO_LINK);
          } else {
            alarm_t* ecfc_conn_alarm = alarm_new("ecfc_conn_alarm");
            l2cu_set_alarm_ecfc_grp(p_ccb, ecfc_conn_alarm);
            alarm_set_on_mloop(p_ccb->coc_cmd_info.ecfc_conn_alarm,
                              L2CAP_CHNL_CONNECT_TIMEOUT_MS,
                              l2c_ccb_timer_timeout, p_ccb);
            if(!l2cu_send_peer_credit_ecfc_connect_req(p_ccb)) {
              L2CAP_TRACE_ERROR("%s l2cu_send_peer_credit_ecfc_connect_req "
                                "failed ", __func__);
              alarm_cancel(ecfc_conn_alarm);
              alarm_free(ecfc_conn_alarm);
              tL2CA_COC_CONNECT_CFM_CB *coc_connect_cfm
              = p_ccb->p_rcb->coc_api.pL2CA_CocConnectCfm_Cb;
              RawAddress p_bd_addr = p_ccb->p_lcb->remote_bd_addr;
              tL2CAP_COC_CHMAP_INFO chmap_info = {};
              l2cu_release_coc_ccbs(p_ccb);
              (*coc_connect_cfm)(p_bd_addr, &chmap_info, 0, L2CAP_CONN_NO_PSM,
                L2CAP_CONN_NO_PSM);
            }
          }
        }
      }
      break;

    case L2CEVT_SEC_COMP_NEG: /* something is really bad with security */
    if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
      L2CAP_TRACE_API(
          "L2CAP - Calling ConnectCfm_Cb(), CID: 0x%04x  Status: %d",
          p_ccb->local_cid, L2CAP_CONN_TIMEOUT);
      l2cu_release_ccb(p_ccb);
      (*connect_cfm)(local_cid, L2CAP_CONN_SECURITY_BLOCK);
    } else {
      tL2CA_COC_CONNECT_CFM_CB *coc_connect_cfm
             = p_ccb->p_rcb->coc_api.pL2CA_CocConnectCfm_Cb;
      RawAddress p_bd_addr = p_ccb->p_lcb->remote_bd_addr;
      tL2CAP_COC_CHMAP_INFO chmap_info = {};
      l2cu_release_coc_ccbs(p_ccb);
      (*coc_connect_cfm)(p_bd_addr, &chmap_info, 0, p_ci->l2cap_result,
                    L2CAP_CONN_SECURITY_BLOCK);
    }
    break;

    case L2CEVT_L2CAP_CONNECT_REQ: /* Peer connect request */
      /* stop link timer to avoid race condition between A2MP, Security, and
       * L2CAP */
      alarm_cancel(p_ccb->p_lcb->l2c_lcb_timer);

      if (p_ccb->p_lcb->transport == BT_TRANSPORT_LE) {
        p_ccb->chnl_state = CST_TERM_W4_SEC_COMP;
        tL2CAP_LE_RESULT_CODE result;
        if (l2cb.cert_failure) {
          result = l2cb.cert_failure;
          L2CAP_TRACE_ERROR("%s PTS FAILURE MODE IN EFFECT (CASE %d) ", __func__,
            result);
        } else {
          result = l2ble_sec_access_req(p_ccb->p_lcb->remote_bd_addr,
            p_ccb->p_rcb->psm, false, &l2c_link_sec_comp2, p_ccb);
        }

        switch (result) {
          case L2CAP_LE_RESULT_INSUFFICIENT_AUTHENTICATION:
          case L2CAP_LE_RESULT_INSUFFICIENT_AUTHORIZATION:
          case L2CAP_LE_RESULT_INSUFFICIENT_ENCRYP_KEY_SIZE:
          case L2CAP_LE_RESULT_INSUFFICIENT_ENCRYP:
            l2cu_reject_ble_connection(p_ccb->p_lcb, p_ccb->remote_id, result);
            l2cu_release_ccb(p_ccb);
            break;
            // TODO: Handle the other return codes
        }
      } else {
        /* Cancel sniff mode if needed */
        {
          tBTM_PM_PWR_MD settings;
          memset((void*)&settings, 0, sizeof(settings));
          settings.mode = BTM_PM_MD_ACTIVE;

          BTM_SetPowerMode(BTM_PM_SET_ONLY_ID, p_ccb->p_lcb->remote_bd_addr,
                           &settings);
        }

        p_ccb->chnl_state = CST_TERM_W4_SEC_COMP;
        if (btm_sec_l2cap_access_req(p_ccb->p_lcb->remote_bd_addr,
                                     p_ccb->p_rcb->psm, p_ccb->p_lcb->handle,
                                     false, &l2c_link_sec_comp,
                                     p_ccb) == BTM_CMD_STARTED) {
          /* started the security process, tell the peer to set a longer timer
           */
          l2cu_send_peer_connect_rsp(p_ccb, L2CAP_CONN_PENDING, 0);
        }
      }
      break;

    case L2CEVT_TIMEOUT:
      if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        L2CAP_TRACE_API(
            "L2CAP - Calling ConnectCfm_Cb(), CID: 0x%04x  Status: %d",
            p_ccb->local_cid, L2CAP_CONN_TIMEOUT);
        l2cu_release_ccb(p_ccb);
        (*connect_cfm)(local_cid, L2CAP_CONN_TIMEOUT);
      } else {
        tL2CAP_COC_CHMAP_INFO chmap_info = {};
        tL2CA_COC_CONNECT_CFM_CB *coc_connect_cfm
            = p_ccb->p_rcb->coc_api.pL2CA_CocConnectCfm_Cb;
        RawAddress p_bd_addr = p_ccb->p_lcb->remote_bd_addr;
        l2cu_release_coc_ccbs(p_ccb);
        (*coc_connect_cfm)(p_bd_addr, &chmap_info, 0, p_ci->l2cap_result,
                    L2CAP_CONN_TIMEOUT);
      }
      break;

    case L2CEVT_L2CAP_DATA:      /* Peer data packet rcvd    */
    case L2CEVT_L2CA_DATA_WRITE: /* Upper layer data to send */
      osi_free(p_data);
      break;

    case L2CEVT_L2CA_DISCONNECT_REQ: /* Upper wants to disconnect */
      L2CAP_TRACE_WARNING("%s Disconnecting CID %d ", __func__,
                          p_ccb->local_cid);
      if (p_ccb->peer_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        l2cu_release_ccb(p_ccb);
      } else {
        if (p_ccb->coc_cmd_info.num_coc_chnls > 1) {
          l2cu_reset_sr_cid(p_ccb);
        } else {
          l2cu_release_ccb(p_ccb);
        }
      }
      break;

    case L2CEVT_L2CA_COC_CONNECT_REQ: /*Upper layer wants to connect */
    {
      if (p_ccb->p_lcb->transport == BT_TRANSPORT_LE) {
        l2cu_set_coc_chnl_state(p_ccb, CST_ORIG_W4_SEC_COMP);
        l2ble_sec_access_req(p_ccb->p_lcb->remote_bd_addr, p_ccb->p_rcb->psm,
                             true, &l2c_link_sec_comp2, p_ccb);
      } else {
        /* Cancel sniff mode if needed */
        {
          tBTM_PM_PWR_MD settings;
          memset((void*)&settings, 0, sizeof(settings));
          settings.mode = BTM_PM_MD_ACTIVE;
          BTM_SetPowerMode(BTM_PM_SET_ONLY_ID, p_ccb->p_lcb->remote_bd_addr,
                           &settings);
        }

        /* If sec access does not result in started SEC_COM or COMP_NEG are
         * already processed */
        if (btm_sec_l2cap_access_req(p_ccb->p_lcb->remote_bd_addr,
                                     p_ccb->p_rcb->psm, p_ccb->p_lcb->handle,
                                     true, &l2c_link_sec_comp,
                                     p_ccb) == BTM_CMD_STARTED)
          l2cu_set_coc_chnl_state(p_ccb, CST_ORIG_W4_SEC_COMP);
      }
    }
      break;

    case L2CEVT_L2CAP_COC_CONNECT_REQ:
    {
      /* stop link timer to avoid race condition between A2MP, Security, and
       * L2CAP */
      alarm_cancel(p_ccb->p_lcb->l2c_lcb_timer);
      if (p_ccb->p_lcb->transport == BT_TRANSPORT_LE) {
        l2cu_set_coc_chnl_state(p_ccb, CST_TERM_W4_SEC_COMP);
        uint16_t result;
        /* errata id = 14605 */
        p_ccb->coc_cmd_info.connection_pending = CONNECTION_PENDING ;
        result = l2ble_sec_access_req(p_ccb->p_lcb->remote_bd_addr,
            p_ccb->p_rcb->psm, false, &l2c_link_sec_comp2, p_ccb);
        switch (result) {
          case L2CAP_ECFC_ALL_CONNS_REFUSED_INSUFF_AUTHENTICATION:
          case L2CAP_ECFC_ALL_CONNS_REFUSED_INSUFF_AUTHERIZATION:
          case L2CAP_ECFC_ALL_CONNS_REFUSED_INSUFF_ENCRYPTION_KEY_SIZE:
          case L2CAP_ECFC_ALL_CONNS_REFUSED_INSUFF_ENCRYPTION:
            /* errata id = 14605 */
            p_ccb->coc_cmd_info.connection_pending =
              CONNECTION_PENDING_RESET;
            l2cu_reject_coc_connection(p_ccb->p_lcb, p_ccb->remote_id,result,
            p_ccb->coc_cmd_info.num_coc_chnls);
            l2cu_release_coc_ccbs(p_ccb);
            break;
        }
      } else {
        /* Cancel sniff mode if needed */
        {
          tBTM_PM_PWR_MD settings;
          memset((void*)&settings, 0, sizeof(settings));
          settings.mode = BTM_PM_MD_ACTIVE;

          BTM_SetPowerMode(BTM_PM_SET_ONLY_ID, p_ccb->p_lcb->remote_bd_addr,
                           &settings);
        }

        l2cu_set_coc_chnl_state(p_ccb, CST_TERM_W4_SEC_COMP);
        if (btm_sec_l2cap_access_req(p_ccb->p_lcb->remote_bd_addr,
                                     p_ccb->p_rcb->psm, p_ccb->p_lcb->handle,
                                     false, &l2c_link_sec_comp,
                                     p_ccb) == BTM_CMD_STARTED) {
          /* started the security process, tell the peer to set a longer timer
           */
          l2cu_reject_coc_connection(p_ccb->p_lcb, p_ccb->remote_id,
            L2CAP_ECFC_ALL_CONNS_REFUSED_INSUFF_AUTHENTICATION,
            p_ccb->coc_cmd_info.num_coc_chnls);
          l2cu_release_coc_ccbs(p_ccb);
        }
      }
    }
      break;
  }
}

/*******************************************************************************
 *
 * Function         l2c_csm_orig_w4_sec_comp
 *
 * Description      This function handles events when the channel is in
 *                  CST_ORIG_W4_SEC_COMP state.
 *
 * Returns          void
 *
 ******************************************************************************/
static void l2c_csm_orig_w4_sec_comp(tL2C_CCB* p_ccb, uint16_t event,
                                     void* p_data) {

  tL2CA_DISCONNECT_IND_CB* disconnect_ind = NULL;
  tL2CA_CONNECT_CFM_CB* connect_cfm = NULL;

  uint16_t local_cid = p_ccb->local_cid;
  uint16_t sr_cids[L2C_MAX_ECFC_CHNLS_PER_CONN] = {0};

  if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
    disconnect_ind = p_ccb->p_rcb->api.pL2CA_DisconnectInd_Cb;
    connect_cfm = p_ccb->p_rcb->api.pL2CA_ConnectCfm_Cb;
  }

  if (p_ccb->p_rcb) {
    disconnect_ind = p_ccb->p_rcb->api.pL2CA_DisconnectInd_Cb;
    connect_cfm = p_ccb->p_rcb->api.pL2CA_ConnectCfm_Cb;
  } else {
    L2CAP_TRACE_WARNING(
        "%s: L2CAP - LCID: 0x%04x  st: ORIG_W4_SEC_COMP  evt: %s: p_ccb->p_rcb = null",
        __func__, p_ccb->local_cid, l2c_csm_get_event_name(event));
  }
  L2CAP_TRACE_EVENT(
      "%s: %sL2CAP - LCID: 0x%04x  st: ORIG_W4_SEC_COMP  evt: %s", __func__,
      ((p_ccb->p_lcb->transport == BT_TRANSPORT_LE)) ? "LE ": "",
      p_ccb->local_cid, l2c_csm_get_event_name(event));

#if (L2CAP_UCD_INCLUDED == TRUE)
  if (local_cid == L2CAP_CONNECTIONLESS_CID) {
    /* check if this event can be processed by UCD */
    if (l2c_ucd_process_event(p_ccb, event, p_data)) {
      /* The event is processed by UCD state machine */
      return;
    }
  }
#endif

  switch (event) {
    case L2CEVT_LP_DISCONNECT_IND: /* Link was disconnected */
      if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        L2CAP_TRACE_API(
            "L2CAP - Calling Disconnect_Ind_Cb(), CID: 0x%04x  No Conf Needed",
            p_ccb->local_cid);
        if (disconnect_ind) {
          (*disconnect_ind)(local_cid, false);
        }
      } else {
        uint8_t num_chnls = p_ccb->coc_cmd_info.num_coc_chnls;
        tL2CA_DISCONNECT_IND_CB* disconnect_ind =
           p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_DisconnectInd_Cb : NULL;
        l2cu_get_coc_local_cids(sr_cids, p_ccb);
        l2cu_release_coc_ccbs(p_ccb);
        if (disconnect_ind) {
          l2cu_send_disconnect_ind(sr_cids, num_chnls, disconnect_ind);
        }
      }
      break;

    case L2CEVT_SEC_RE_SEND_CMD: /* BTM has enough info to proceed */
      l2cu_set_ecfc_grp_status(p_ccb, L2CAP_ECFC_SEC_PENDING_CB);
      if ((p_ccb->p_lcb->transport == BT_TRANSPORT_BR_EDR) &&
          (p_ccb->p_lcb->handle == HCI_INVALID_HANDLE) &&
          (p_ccb->p_lcb->link_state = LST_CONNECTING)) {
        L2CAP_TRACE_WARNING("%s reconnecting link,wait for linkup %d ", __func__,
        p_ccb->local_cid);
        break;
      }
      FALLTHROUGH;
    case L2CEVT_LP_CONNECT_CFM:  /* Link came up         */

      if (p_ccb->p_rcb == NULL) {
        L2CAP_TRACE_WARNING(
        "%s: L2CAP - LCID: 0x%04x  st: ORIG_W4_SEC_COMP  evt: %s: p_rcb == NULL", __func__,
        p_ccb->local_cid, l2c_csm_get_event_name(event));
        break;
      }
      if (p_ccb->p_lcb->transport == BT_TRANSPORT_LE) {
        l2ble_sec_access_req(p_ccb->p_lcb->remote_bd_addr, p_ccb->p_rcb->psm,
                             false, &l2c_link_sec_comp2, p_ccb);
      } else {
        btm_sec_l2cap_access_req(p_ccb->p_lcb->remote_bd_addr,
                                 p_ccb->p_rcb->psm, p_ccb->p_lcb->handle, true,
                                 &l2c_link_sec_comp, p_ccb);
      }
      break;

    case L2CEVT_SEC_COMP: /* Security completed success */
      if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
      /* Wait for the info resp in this state before sending connect req (if
       * needed) */
        p_ccb->chnl_state = CST_W4_L2CAP_CONNECT_RSP;
        if (p_ccb->p_lcb->transport == BT_TRANSPORT_LE) {
          alarm_set_on_mloop(p_ccb->l2c_ccb_timer, L2CAP_CHNL_CONNECT_TIMEOUT_MS,
                             l2c_ccb_timer_timeout, p_ccb);
          l2cble_credit_based_conn_req(p_ccb); /* Start Connection     */
        } else {
          if (!p_ccb->p_lcb->w4_info_rsp) {
            /* Need to have at least one compatible channel to continue */
            if (!l2c_fcr_chk_chan_modes(p_ccb)) {
              l2cu_release_ccb(p_ccb);
              if (connect_cfm) {
                (*connect_cfm)(local_cid, L2CAP_CONN_NO_LINK);
              }
            } else {
              alarm_set_on_mloop(p_ccb->l2c_ccb_timer,
                                 L2CAP_CHNL_CONNECT_TIMEOUT_MS,
                                 l2c_ccb_timer_timeout, p_ccb);
              l2cu_send_peer_connect_req(p_ccb); /* Start Connection     */
            }
          }
        }
      } else {
        l2cu_set_coc_chnl_state(p_ccb, CST_W4_L2CAP_CONNECT_RSP);
        if (p_ccb->p_lcb->transport == BT_TRANSPORT_LE) {
          alarm_t* ecfc_conn_alarm = alarm_new("ecfc_conn_alarm");
          l2cu_set_alarm_ecfc_grp(p_ccb, ecfc_conn_alarm);
          alarm_set_on_mloop(p_ccb->coc_cmd_info.ecfc_conn_alarm,
                             L2CAP_CHNL_CONNECT_TIMEOUT_MS,
                             l2c_ccb_timer_timeout, p_ccb);
          if(!l2cu_send_peer_credit_ecfc_connect_req(p_ccb)) {
            L2CAP_TRACE_ERROR("%s l2cu_send_peer_credit_ecfc_connect_req "
                             "failed ", __func__);
            alarm_cancel(ecfc_conn_alarm);
            alarm_free(ecfc_conn_alarm);
            tL2CA_COC_CONNECT_CFM_CB *coc_connect_cfm
              = p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_CocConnectCfm_Cb : NULL;
            RawAddress p_bd_addr = p_ccb->p_lcb->remote_bd_addr;
            tL2CAP_COC_CHMAP_INFO chmap_info = {};
            l2cu_release_coc_ccbs(p_ccb);
            if (coc_connect_cfm) {
              (*coc_connect_cfm)(p_bd_addr, &chmap_info, 0, L2CAP_CONN_NO_PSM,
                L2CAP_CONN_NO_PSM);
            }
          }
        } else {
          if (!p_ccb->p_lcb->w4_info_rsp) {
            /* Need to have at least one compatible channel to continue */
            if (!l2c_fcr_chk_remote_ecfc_support(p_ccb)) {
              tL2CA_COC_CONNECT_CFM_CB *coc_connect_cfm
                = p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_CocConnectCfm_Cb: NULL;
              RawAddress p_bd_addr = p_ccb->p_lcb->remote_bd_addr;
              tL2CAP_COC_CHMAP_INFO chmap_info = {};
              l2cu_release_coc_ccbs(p_ccb);
              if (coc_connect_cfm) {
                (*coc_connect_cfm)(p_bd_addr, &chmap_info, 0,
                                 L2CAP_ECFC_ALL_CONNS_REFUSED_SPSM_NOT_SUPPORTED,
                                 L2CAP_CONN_NO_LINK);
              }
            } else {
              alarm_t* ecfc_conn_alarm = alarm_new("ecfc_conn_alarm");
              l2cu_set_alarm_ecfc_grp(p_ccb, ecfc_conn_alarm);
              alarm_set_on_mloop(p_ccb->coc_cmd_info.ecfc_conn_alarm,
                                 L2CAP_CHNL_CONNECT_TIMEOUT_MS,
                                 l2c_ccb_timer_timeout, p_ccb);
              if(!l2cu_send_peer_credit_ecfc_connect_req(p_ccb)) {
                L2CAP_TRACE_ERROR("%s l2cu_send_peer_credit_ecfc_connect_req "
                                "failed ", __func__);
                alarm_cancel(ecfc_conn_alarm);
                alarm_free(ecfc_conn_alarm);
                tL2CA_COC_CONNECT_CFM_CB *coc_connect_cfm
                  = p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_CocConnectCfm_Cb: NULL;
                RawAddress p_bd_addr = p_ccb->p_lcb->remote_bd_addr;
                tL2CAP_COC_CHMAP_INFO chmap_info = {};
                l2cu_release_coc_ccbs(p_ccb);
                if (coc_connect_cfm) {
                  (*coc_connect_cfm)(p_bd_addr, &chmap_info, 0,
                                    L2CAP_CONN_NO_PSM,
                                    L2CAP_CONN_NO_PSM);
                }
              }
            }
          }
        }
      }
      break;

    case L2CEVT_SEC_COMP_NEG:
      if (p_ccb->peer_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        L2CAP_TRACE_API(
            "L2CAP - Calling ConnectCfm_Cb(), CID: 0x%04x  Status: %d",
            p_ccb->local_cid, HCI_ERR_AUTH_FAILURE);

        /* If last channel immediately disconnect the ACL for better security.
           Also prevents a race condition between BTM and L2CAP */
        if ((p_ccb == p_ccb->p_lcb->ccb_queue.p_first_ccb) &&
            (p_ccb == p_ccb->p_lcb->ccb_queue.p_last_ccb)) {
          p_ccb->p_lcb->idle_timeout = 0;
        }
        l2cu_release_ccb(p_ccb);
        if (connect_cfm) {
          (*connect_cfm)(local_cid, HCI_ERR_AUTH_FAILURE);
        }
      } else {
        //TODO Move these common code into Separate API
        tL2CA_COC_CONNECT_CFM_CB *coc_connect_cfm
            = p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_CocConnectCfm_Cb: NULL;
        RawAddress p_bd_addr = p_ccb->p_lcb->remote_bd_addr;
        tL2CAP_COC_CHMAP_INFO chmap_info = {};
        l2cu_release_coc_ccbs(p_ccb);
        if (coc_connect_cfm) {
          (*coc_connect_cfm)(p_bd_addr, &chmap_info, 0,
                            L2CAP_ECFC_ALL_CONNS_REFUSED_INSUFF_AUTHENTICATION,
                            HCI_ERR_AUTH_FAILURE);
        }
      }
      break;

    case L2CEVT_L2CA_DATA_WRITE: /* Upper layer data to send */
    case L2CEVT_L2CAP_DATA:      /* Peer data packet rcvd    */
      osi_free(p_data);
      break;

    case L2CEVT_L2CA_DISCONNECT_REQ: /* Upper wants to disconnect */
      /* Tell security manager to abort */
      btm_sec_abort_access_req(p_ccb->p_lcb->remote_bd_addr);
      if (p_ccb->peer_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        l2cu_release_ccb(p_ccb);
      } else {
        if (p_ccb->coc_cmd_info.num_coc_chnls > 1) {
          l2cu_reset_sr_cid(p_ccb);
        } else {
          l2cu_release_ccb(p_ccb);
        }
      }
      break;
  }
}

/*******************************************************************************
 *
 * Function         l2c_csm_term_w4_sec_comp
 *
 * Description      This function handles events when the channel is in
 *                  CST_TERM_W4_SEC_COMP state.
 *
 * Returns          void
 *
 ******************************************************************************/
static void l2c_csm_term_w4_sec_comp(tL2C_CCB* p_ccb, uint16_t event,
                                     void* p_data) {
  L2CAP_TRACE_WARNING("L2CAP - LCID: 0x%04x  st: TERM_W4_SEC_COMP  evt: %s",
                    p_ccb->local_cid, l2c_csm_get_event_name(event));

  if (p_ccb->p_rcb == NULL) {
    L2CAP_TRACE_WARNING("L2CAP - LCID: 0x%04x  st: TERM_W4_SEC_COMP  evt: %s:"
       "p_ccb->p_rcb = null ", p_ccb->local_cid, l2c_csm_get_event_name(event));
  }

#if (L2CAP_UCD_INCLUDED == TRUE)
  if (p_ccb->local_cid == L2CAP_CONNECTIONLESS_CID) {
    /* check if this event can be processed by UCD */
    if (l2c_ucd_process_event(p_ccb, event, p_data)) {
      /* The event is processed by UCD state machine */
      return;
    }
  }
#endif

  switch (event) {
    case L2CEVT_LP_DISCONNECT_IND: /* Link was disconnected */
      /* Tell security manager to abort */
      btm_sec_abort_access_req(p_ccb->p_lcb->remote_bd_addr);

      if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        l2cu_release_ccb(p_ccb);
      } else {
        l2cu_release_coc_ccbs(p_ccb);
      }
      break;

    case L2CEVT_SEC_COMP:
      if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        p_ccb->chnl_state = CST_W4_L2CA_CONNECT_RSP;

      /* Wait for the info resp in next state before sending connect ind (if
       * needed) */
        if ((!p_ccb->p_lcb->w4_info_rsp)||(p_ccb->p_rcb && (p_ccb->p_rcb->psm == BT_PSM_SDP))) {
          /* Don't need to get info from peer or already retrieved so continue */
          alarm_set_on_mloop(p_ccb->l2c_ccb_timer, L2CAP_CHNL_CONNECT_TIMEOUT_MS,
                             l2c_ccb_timer_timeout, p_ccb);
          L2CAP_TRACE_API("L2CAP - Calling Connect_Ind_Cb(), CID: 0x%04x",
                          p_ccb->local_cid);
          if (p_ccb->p_rcb && p_ccb->p_rcb->api.pL2CA_ConnectInd_Cb) {
            (*p_ccb->p_rcb->api.pL2CA_ConnectInd_Cb)(
                p_ccb->p_lcb->remote_bd_addr, p_ccb->local_cid, p_ccb->p_rcb->psm,
                p_ccb->remote_id);
          }
        } else {
          /*
          ** L2CAP Connect Response will be sent out by 3 sec timer expiration
          ** because Bluesoleil doesn't respond to L2CAP Information Request.
          ** Bluesoleil seems to disconnect ACL link as failure case, because
          ** it takes too long (4~7secs) to get response.
          ** product version : Bluesoleil 2.1.1.0 EDR Release 060123
          ** stack version   : 05.04.11.20060119
          */

          /* Cancel ccb timer as security complete. waiting for w4_info_rsp
          ** once info rsp received, connection rsp timer will be started
          ** while sending connection ind to profiles
          */
          alarm_cancel(p_ccb->l2c_ccb_timer);

          /* Waiting for the info resp, tell the peer to set a longer timer */
          l2cu_send_peer_connect_rsp(p_ccb, L2CAP_CONN_PENDING, 0);
        } 
      } else {
        if (!p_ccb->p_lcb->w4_info_rsp) {
          l2cu_set_coc_chnl_state(p_ccb, CST_W4_L2CA_CONNECT_RSP);
          tL2CAP_COC_CONN_REQ p_conn_req;
          p_conn_req.mtu = p_ccb->peer_conn_cfg.mtu;
          p_conn_req.p_bd_addr = p_ccb->p_lcb->remote_bd_addr;
          p_conn_req.num_chnls = p_ccb->coc_cmd_info.num_coc_chnls;
          p_conn_req.psm = p_ccb->p_rcb ? p_ccb->p_rcb->psm: 0;
          p_conn_req.transport = p_ccb->p_lcb->transport;
          l2cu_reset_ecfc_cids(&p_conn_req.sr_cids[0]);
          l2cu_get_coc_local_cids(&p_conn_req.sr_cids[0], p_ccb);
          alarm_t* ecfc_conn_alarm = alarm_new("ecfc_conn_alarm");
          l2cu_set_alarm_ecfc_grp(p_ccb, ecfc_conn_alarm);
          alarm_set_on_mloop(p_ccb->coc_cmd_info.ecfc_conn_alarm,
                            L2CAP_CHNL_CONNECT_TIMEOUT_MS,
                            l2c_ccb_timer_timeout, p_ccb);
          if (p_ccb->p_rcb) {
            (*p_ccb->p_rcb->coc_api.pL2CA_CocConnectInd_Cb)(&p_conn_req,
                  p_ccb->remote_id,
                  p_ccb->coc_cmd_info.ecfc_conn_result,
                  L2CAP_CONN_OK);
            /* errata id = 14605 */
            p_ccb->coc_cmd_info.authorization_pending =
              L2CAP_AUTHORIZATION_PENDING ;
          }
        }
        p_ccb->coc_cmd_info.connection_pending =
          CONNECTION_PENDING_RESET;
      }
      break;

    case L2CEVT_SEC_COMP_NEG:
      if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        if (((tL2C_CONN_INFO*)p_data)->status == BTM_DELAY_CHECK) {
          /* start a timer - encryption change not received before L2CAP connect
           * req */
          alarm_set_on_mloop(p_ccb->l2c_ccb_timer,
                             L2CAP_DELAY_CHECK_SM4_TIMEOUT_MS,
                             l2c_ccb_timer_timeout, p_ccb);
        } else {
          if (p_ccb->p_lcb->transport == BT_TRANSPORT_LE)
            l2cu_reject_ble_connection(
                p_ccb->p_lcb, p_ccb->remote_id,
                L2CAP_LE_RESULT_INSUFFICIENT_AUTHENTICATION);
          else
            l2cu_send_peer_connect_rsp(p_ccb, L2CAP_CONN_SECURITY_BLOCK, 0);
          l2cu_release_ccb(p_ccb);
      }
    } else {
      /* errata id = 14605 */
      p_ccb->coc_cmd_info.connection_pending =  CONNECTION_PENDING_RESET;
      l2cu_reject_coc_connection(p_ccb->p_lcb, p_ccb->remote_id,
                              L2CAP_ECFC_ALL_CONNS_REFUSED_INSUFF_AUTHENTICATION,
                              p_ccb->coc_cmd_info.num_coc_chnls);
      l2cu_release_coc_ccbs(p_ccb);
    }
      break;

    case L2CEVT_L2CA_DATA_WRITE: /* Upper layer data to send */
    case L2CEVT_L2CAP_DATA:      /* Peer data packet rcvd    */
      osi_free(p_data);
      break;

    case L2CEVT_L2CA_DISCONNECT_REQ: /* Upper wants to disconnect */
      if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        l2cu_release_ccb(p_ccb);
      } else {
        l2cu_release_coc_ccbs(p_ccb);
      }
      break;

    case L2CEVT_L2CAP_DISCONNECT_REQ: /* Peer disconnected request */
      l2cu_send_peer_disc_rsp(p_ccb->p_lcb, p_ccb->remote_id, p_ccb->local_cid,
                              p_ccb->remote_cid);

      /* Tell security manager to abort */
      btm_sec_abort_access_req(p_ccb->p_lcb->remote_bd_addr);

      if (p_ccb->peer_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        l2cu_release_ccb(p_ccb);
      } else {
        if (p_ccb->coc_cmd_info.num_coc_chnls > 1) {
          l2cu_reset_sr_cid(p_ccb);
        }
        l2cu_release_ccb(p_ccb);
      }
      break;

    case L2CEVT_TIMEOUT:
      /* SM4 related. */
      btsnd_hcic_disconnect(p_ccb->p_lcb->handle, HCI_ERR_AUTH_FAILURE);
      break;

    case L2CEVT_SEC_RE_SEND_CMD: /* BTM has enough info to proceed */

      if (p_ccb->p_rcb == NULL) {
        L2CAP_TRACE_WARNING("L2CAP - LCID: 0x%04x st: TERM_W4_SEC_COMP evt: %s:"
            "p_ccb->p_rcb = null", p_ccb->local_cid, l2c_csm_get_event_name(event));
        break;
      }
      btm_sec_l2cap_access_req(p_ccb->p_lcb->remote_bd_addr, p_ccb->p_rcb->psm,
                               p_ccb->p_lcb->handle, false, &l2c_link_sec_comp,
                               p_ccb);
      break;
  }
}

/*******************************************************************************
 *
 * Function         l2c_csm_w4_l2cap_connect_rsp
 *
 * Description      This function handles events when the channel is in
 *                  CST_W4_L2CAP_CONNECT_RSP state.
 *
 * Returns          void
 *
 ******************************************************************************/
static void l2c_csm_w4_l2cap_connect_rsp(tL2C_CCB* p_ccb, uint16_t event,
                                         void* p_data) {
  tL2C_CONN_INFO* p_ci = (tL2C_CONN_INFO*)p_data;
  tL2CA_DISCONNECT_IND_CB* disconnect_ind = NULL;
  tL2CA_CONNECT_CFM_CB* connect_cfm = NULL;
  tL2CA_DISCONNECT_CFM_CB* disconnect_cfm = NULL;
  uint16_t local_cid = p_ccb->local_cid;

  if (p_ccb->p_rcb) {
    disconnect_ind = p_ccb->p_rcb->api.pL2CA_DisconnectInd_Cb;
    connect_cfm = p_ccb->p_rcb->api.pL2CA_ConnectCfm_Cb;
  } else {
    L2CAP_TRACE_WARNING("L2CAP - LCID: 0x%04x  st: W4_L2CAP_CON_RSP  evt: %s:"
         "p_ccb->p_rcb =null", p_ccb->local_cid, l2c_csm_get_event_name(event));
  }

  L2CAP_TRACE_WARNING("L2CAP - LCID: 0x%04x  st: W4_L2CAP_CON_RSP  evt: %s",
                    p_ccb->local_cid, l2c_csm_get_event_name(event));

  switch (event) {
    case L2CEVT_LP_DISCONNECT_IND: /* Link was disconnected */

      if (p_ccb->peer_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        p_ccb->chnl_state = CST_CLOSED;
        L2CAP_TRACE_API(
            "L2CAP - Calling Disconnect_Ind_Cb(), CID: 0x%04x  No Conf Needed",
            p_ccb->local_cid);
        l2cu_release_ccb(p_ccb);
        if (disconnect_ind) {
          (*disconnect_ind)(local_cid, false);
        }
      } else {
        uint16_t sr_cids[L2C_MAX_ECFC_CHNLS_PER_CONN] = {0};
        uint8_t num_chnls = p_ccb->coc_cmd_info.num_coc_chnls;
        tL2CA_DISCONNECT_IND_CB* disconnect_ind =
          p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_DisconnectInd_Cb: NULL;
        alarm_cancel(p_ccb->coc_cmd_info.ecfc_conn_alarm);
        alarm_free(p_ccb->coc_cmd_info.ecfc_conn_alarm);
        l2cu_get_coc_local_cids(sr_cids, p_ccb);
        l2cu_release_coc_ccbs(p_ccb);
        if (disconnect_ind) {
          l2cu_send_disconnect_ind(sr_cids, num_chnls, disconnect_ind);
        }
      }
      break;

    case L2CEVT_L2CAP_CONNECT_RSP: /* Got peer connect confirm */
      p_ccb->remote_cid = p_ci->remote_cid;
      if (p_ccb->p_lcb->transport == BT_TRANSPORT_LE) {
        /* Connection is completed */
        alarm_cancel(p_ccb->l2c_ccb_timer);
        p_ccb->chnl_state = CST_OPEN;
      } else {
        p_ccb->chnl_state = CST_CONFIG;
        alarm_set_on_mloop(p_ccb->l2c_ccb_timer, L2CAP_CHNL_CFG_TIMEOUT_MS,
                           l2c_ccb_timer_timeout, p_ccb);
      }
      L2CAP_TRACE_API("L2CAP - Calling Connect_Cfm_Cb(), CID: 0x%04x, Success",
                      p_ccb->local_cid);
      if (p_ccb->p_rcb && p_ccb->p_rcb->api.pL2CA_ConnectCfm_Cb) {
        (*p_ccb->p_rcb->api.pL2CA_ConnectCfm_Cb)(local_cid, L2CAP_CONN_OK);
      }
      break;

    case L2CEVT_L2CAP_CONNECT_RSP_PND: /* Got peer connect pending */
      p_ccb->remote_cid = p_ci->remote_cid;
      alarm_set_on_mloop(p_ccb->l2c_ccb_timer,
                         L2CAP_CHNL_CONNECT_EXT_TIMEOUT_MS,
                         l2c_ccb_timer_timeout, p_ccb);
      if (p_ccb->p_rcb && p_ccb->p_rcb->api.pL2CA_ConnectPnd_Cb) {
        L2CAP_TRACE_API("L2CAP - Calling Connect_Pnd_Cb(), CID: 0x%04x",
                        p_ccb->local_cid);
        (*p_ccb->p_rcb->api.pL2CA_ConnectPnd_Cb)(p_ccb->local_cid);
      }
      break;

    case L2CEVT_L2CAP_CONNECT_RSP_NEG: /* Peer rejected connection */
      L2CAP_TRACE_API(
          "L2CAP - Calling Connect_Cfm_Cb(), CID: 0x%04x, Failure Code: %d",
          p_ccb->local_cid, p_ci->l2cap_result);
      l2cu_release_ccb(p_ccb);
      if (connect_cfm) {
        (*connect_cfm)(local_cid, p_ci->l2cap_result);
      }
      break;

    case L2CEVT_TIMEOUT:
      if (p_ccb->peer_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        L2CAP_TRACE_API("L2CAP - Calling Connect_Cfm_Cb(), CID: 0x%04x, Timeout",
                      p_ccb->local_cid);
        l2cu_release_ccb(p_ccb);
        if (connect_cfm) {
          (*connect_cfm)(local_cid, L2CAP_CONN_TIMEOUT);
        }
      } else {
        tL2CA_COC_CONNECT_CFM_CB *coc_connect_cfm
          = p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_CocConnectCfm_Cb: NULL;
        RawAddress p_bd_addr = p_ccb->p_lcb->remote_bd_addr;
        tL2CAP_COC_CHMAP_INFO chmap_info;
        chmap_info.num_chnls = 0;
        alarm_free(p_ccb->coc_cmd_info.ecfc_conn_alarm);
        l2cu_release_coc_ccbs(p_ccb);
        if (coc_connect_cfm) {
          (*coc_connect_cfm)(p_bd_addr, &chmap_info, 0, L2CAP_CONN_TIMEOUT,
                    L2CAP_CONN_TIMEOUT);
        }
      }
      break;

    case L2CEVT_L2CA_DISCONNECT_REQ: /* Upper wants to disconnect */
      if (p_ccb->peer_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        /* If we know peer CID from connect pending, we can send disconnect */
        if (p_ccb->remote_cid != 0) {
          l2cu_send_peer_disc_req(p_ccb);
          p_ccb->chnl_state = CST_W4_L2CAP_DISCONNECT_RSP;
          alarm_set_on_mloop(p_ccb->l2c_ccb_timer,
                             L2CAP_CHNL_DISCONNECT_TIMEOUT_MS,
                             l2c_ccb_timer_timeout, p_ccb);
        } else {
          if (p_ccb->p_rcb) {
            disconnect_cfm =
                p_ccb->p_rcb->api.pL2CA_DisconnectCfm_Cb;
          }

          l2cu_release_ccb(p_ccb);
          if (disconnect_cfm) {
            L2CAP_TRACE_API("%s: L2CAP - Calling DisconnectCfm_Cb(),"
                            "CID: 0x%04x", __func__, local_cid);
            (*disconnect_cfm)(local_cid, L2CAP_CONN_NO_LINK);
          }
        }
      } else {
        /* If we know peer CID from connect pending, we can send disconnect */
        if (p_ccb->remote_cid != 0) {
          l2cu_send_peer_disc_req(p_ccb);
          p_ccb->chnl_state = CST_W4_L2CAP_DISCONNECT_RSP;

          //No need to set for particular ecfc_conn_alarm here
          //Because it belongs to particular CID
          alarm_set_on_mloop(p_ccb->l2c_ccb_timer,
                             L2CAP_CHNL_DISCONNECT_TIMEOUT_MS,
                             l2c_ccb_timer_timeout, p_ccb);
        } else {
          tL2CA_DISCONNECT_CFM_CB* disconnect_coc_cfm =
               p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_DisconnectCfm_Cb: NULL;
          if (p_ccb->coc_cmd_info.num_coc_chnls > 1) {
            l2cu_reset_sr_cid(p_ccb);
            //Alarm will still maintain for remaining CCB's
          } else {
            alarm_cancel(p_ccb->coc_cmd_info.ecfc_conn_alarm);
            alarm_free(p_ccb->coc_cmd_info.ecfc_conn_alarm);
            l2cu_release_ccb(p_ccb);
          }
          if (disconnect_coc_cfm) {
            L2CAP_TRACE_API("%s: L2CAP - Calling DisconnectCfm_Cb(),"
                            "CID: 0x%04x", __func__, local_cid);
            (*disconnect_coc_cfm)(local_cid, L2CAP_CONN_NO_LINK);
          }
        }
      }
      break;

    case L2CEVT_L2CA_DATA_WRITE: /* Upper layer data to send */
    case L2CEVT_L2CAP_DATA:      /* Peer data packet rcvd    */
      osi_free(p_data);
      break;

    case L2CEVT_L2CAP_INFO_RSP:
      if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        /* Need to have at least one compatible channel to continue */
        if (!l2c_fcr_chk_chan_modes(p_ccb)) {
          l2cu_release_ccb(p_ccb);
          if (connect_cfm) {
            (*connect_cfm)(local_cid, L2CAP_CONN_NO_LINK);
          }
        } else {
          /* We have feature info, so now send peer connect request */
          alarm_set_on_mloop(p_ccb->l2c_ccb_timer, L2CAP_CHNL_CONNECT_TIMEOUT_MS,
                             l2c_ccb_timer_timeout, p_ccb);
          l2cu_send_peer_connect_req(p_ccb); /* Start Connection     */
        }
      } else {
        /* Need to have ECFC mode channel to continue */
        l2cu_set_ecfc_grp_status(p_ccb, L2CAP_ECFC_INFO_RSP_EVT);
        if (!l2c_fcr_chk_remote_ecfc_support(p_ccb)) {
          tL2CA_COC_CONNECT_CFM_CB *coc_connect_cfm
            = p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_CocConnectCfm_Cb: NULL;
          RawAddress p_bd_addr = p_ccb->p_lcb->remote_bd_addr;
          tL2CAP_COC_CHMAP_INFO chmap_info;
          chmap_info.num_chnls = 0;
          l2cu_release_coc_ccbs(p_ccb);
          if (coc_connect_cfm) {
            (*coc_connect_cfm)(p_bd_addr, &chmap_info, 0,
                              L2CAP_ECFC_ALL_CONNS_REFUSED_SPSM_NOT_SUPPORTED,
                              L2CAP_CONN_NO_LINK);
          }
        } else {
          alarm_t* ecfc_conn_alarm = alarm_new("ecfc_conn_alarm");
          l2cu_set_alarm_ecfc_grp(p_ccb, ecfc_conn_alarm);
          alarm_set_on_mloop(p_ccb->coc_cmd_info.ecfc_conn_alarm,
                              L2CAP_CHNL_CONNECT_TIMEOUT_MS,
                              l2c_ccb_timer_timeout, p_ccb);
          if(!l2cu_send_peer_credit_ecfc_connect_req(p_ccb)) {
            L2CAP_TRACE_ERROR("%s l2cu_send_peer_credit_ecfc_connect_req "
                              "failed ", __func__);
            alarm_cancel(ecfc_conn_alarm);
            alarm_free(ecfc_conn_alarm);
            tL2CA_COC_CONNECT_CFM_CB *coc_connect_cfm
              = p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_CocConnectCfm_Cb: NULL;
            RawAddress p_bd_addr = p_ccb->p_lcb->remote_bd_addr;
            tL2CAP_COC_CHMAP_INFO chmap_info = {};
            l2cu_release_coc_ccbs(p_ccb);
            if (coc_connect_cfm) {
              (*coc_connect_cfm)(p_bd_addr, &chmap_info, 0, L2CAP_CONN_NO_PSM,
                  L2CAP_CONN_NO_PSM);
            }
          }
        }
      }
      break;

    case L2CEVT_L2CAP_COC_CONNECT_RSP:
    {
      tL2CA_COC_CONNECT_CFM_CB *coc_connect_cfm
       = p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_CocConnectCfm_Cb: NULL;
      RawAddress p_bd_addr = p_ccb->p_lcb->remote_bd_addr;
      tL2CAP_COC_CHMAP_INFO chmap_info;
      /* Connection is completed */
      alarm_cancel(p_ccb->coc_cmd_info.ecfc_conn_alarm);
      alarm_free(p_ccb->coc_cmd_info.ecfc_conn_alarm);
      l2cu_set_coc_chnl_state(p_ccb, CST_OPEN);
      L2CAP_TRACE_API("L2CAP - Connect_Cfm_Cb(),CID:0 x%04x, Mtu: %d Success",
                      p_ccb->local_cid, p_ccb->peer_conn_cfg.mtu); //TODO print all IDS and Chnl num's
      chmap_info.num_chnls = p_ccb->coc_cmd_info.num_coc_chnls;
      l2cu_get_coc_local_cids(&chmap_info.sr_cids[0], p_ccb);
      if (coc_connect_cfm) {
        (*coc_connect_cfm) (p_bd_addr, &chmap_info, p_ccb->peer_conn_cfg.mtu,
                          p_ci->l2cap_result, L2CAP_CONN_OK);
      }
    }
      break;
    case L2CEVT_L2CAP_COC_CONNECT_NEG_RSP:
    {
      tL2CA_COC_CONNECT_CFM_CB *coc_connect_cfm
       = p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_CocConnectCfm_Cb: NULL;
      RawAddress p_bd_addr = p_ccb->p_lcb->remote_bd_addr;
      alarm_cancel(p_ccb->coc_cmd_info.ecfc_conn_alarm);
      alarm_free(p_ccb->coc_cmd_info.ecfc_conn_alarm);
      tL2CAP_COC_CHMAP_INFO chmap_info = {};
      l2cu_release_coc_ccbs(p_ccb);
      if (coc_connect_cfm) {
        (*coc_connect_cfm) (p_bd_addr, &chmap_info, 0,
          p_ci->l2cap_result, p_ci->l2cap_result);
      }
    }
    break;
  }
}

/*******************************************************************************
 *
 * Function         l2c_csm_w4_l2ca_connect_rsp
 *
 * Description      This function handles events when the channel is in
 *                  CST_W4_L2CA_CONNECT_RSP state.
 *
 * Returns          void
 *
 ******************************************************************************/
static void l2c_csm_w4_l2ca_connect_rsp(tL2C_CCB* p_ccb, uint16_t event,
                                        void* p_data) {
  tL2C_CONN_INFO* p_ci;
  tL2CA_DISCONNECT_IND_CB* disconnect_ind = NULL;
  uint16_t local_cid = p_ccb->local_cid;
  if (p_ccb->p_rcb) {
    disconnect_ind =
        p_ccb->p_rcb->api.pL2CA_DisconnectInd_Cb;
  } else {
    L2CAP_TRACE_WARNING("L2CAP - LCID: 0x%04x  st: W4_L2CA_CON_RSP  evt: %s:"
          "p_ccb->p_rcb = null", p_ccb->local_cid, l2c_csm_get_event_name(event));
  }
  L2CAP_TRACE_WARNING("L2CAP - LCID: 0x%04x  st: W4_L2CA_CON_RSP  evt: %s",
                    p_ccb->local_cid, l2c_csm_get_event_name(event));

  switch (event) {
    case L2CEVT_LP_DISCONNECT_IND: /* Link was disconnected */
      if (p_ccb->peer_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        L2CAP_TRACE_API(
            "L2CAP - Calling Disconnect_Ind_Cb(), CID: 0x%04x  No Conf Needed",
            p_ccb->local_cid);
        l2cu_release_ccb(p_ccb);
        if (disconnect_ind) {
          (*disconnect_ind)(local_cid, false);
        }
      } else {
        uint16_t sr_cids[L2C_MAX_ECFC_CHNLS_PER_CONN] = {0};
        uint8_t num_chnls = p_ccb->coc_cmd_info.num_coc_chnls;
        tL2CA_DISCONNECT_IND_CB* disconnect_ind =
          p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_DisconnectInd_Cb: NULL;
        alarm_cancel(p_ccb->coc_cmd_info.ecfc_conn_alarm);
        alarm_free(p_ccb->coc_cmd_info.ecfc_conn_alarm);
        l2cu_get_coc_local_cids(sr_cids, p_ccb);
        l2cu_release_coc_ccbs(p_ccb);
        if (disconnect_ind) {
          l2cu_send_disconnect_ind(sr_cids, num_chnls, disconnect_ind);
        }
      }
      break;

    case L2CEVT_L2CA_CONNECT_RSP:
      p_ci = (tL2C_CONN_INFO*)p_data;
      if (p_ccb->p_lcb->transport == BT_TRANSPORT_LE) {
        /* Result should be OK or Reject */
        if ((!p_ci) || (p_ci->l2cap_result == L2CAP_CONN_OK)) {
          l2cble_credit_based_conn_res(p_ccb, L2CAP_CONN_OK);
          p_ccb->chnl_state = CST_OPEN;
          alarm_cancel(p_ccb->l2c_ccb_timer);
        } else {
          l2cble_credit_based_conn_res(p_ccb, p_ci->l2cap_result);
          l2cu_release_ccb(p_ccb);
        }
      } else {
        /* Result should be OK or PENDING */
        if ((!p_ci) || (p_ci->l2cap_result == L2CAP_CONN_OK)) {
          l2cu_send_peer_connect_rsp(p_ccb, L2CAP_CONN_OK, 0);
          p_ccb->chnl_state = CST_CONFIG;
          alarm_set_on_mloop(p_ccb->l2c_ccb_timer, L2CAP_CHNL_CFG_TIMEOUT_MS,
                             l2c_ccb_timer_timeout, p_ccb);
        } else {
          /* If pending, stay in same state and start extended timer */
          l2cu_send_peer_connect_rsp(p_ccb, p_ci->l2cap_result,
                                     p_ci->l2cap_status);
          alarm_set_on_mloop(p_ccb->l2c_ccb_timer,
                             L2CAP_CHNL_CONNECT_EXT_TIMEOUT_MS,
                             l2c_ccb_timer_timeout, p_ccb);
        }
      }
      break;

    case L2CEVT_L2CA_CONNECT_RSP_NEG:
      p_ci = (tL2C_CONN_INFO*)p_data;
      if (p_ccb->p_lcb->transport == BT_TRANSPORT_LE)
        l2cble_credit_based_conn_res(p_ccb, p_ci->l2cap_result);
      else
        l2cu_send_peer_connect_rsp(p_ccb, p_ci->l2cap_result,
                                   p_ci->l2cap_status);
      l2cu_release_ccb(p_ccb);
      break;

    case L2CEVT_TIMEOUT:
      if (p_ccb->peer_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        l2cu_send_peer_connect_rsp(p_ccb, L2CAP_CONN_NO_PSM, 0);
        L2CAP_TRACE_API(
            "L2CAP - Calling Disconnect_Ind_Cb(), CID: 0x%04x  No Conf Needed",
            p_ccb->local_cid);
        l2cu_release_ccb(p_ccb);
        if (disconnect_ind) {
          (*disconnect_ind)(local_cid, false);
        }
      } else {
        //TODO Discuss below error code
        l2cu_send_peer_credit_based_conn_res(p_ccb,
            L2CAP_ECFC_ALL_CONNS_REFUSED_INSUFF_AUTHERIZATION);
        uint16_t sr_cids[L2C_MAX_ECFC_CHNLS_PER_CONN] = {0};
        uint8_t num_chnls = p_ccb->coc_cmd_info.num_coc_chnls;
        tL2CA_DISCONNECT_IND_CB* disconnect_ind =
          p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_DisconnectInd_Cb: NULL;
        alarm_free(p_ccb->coc_cmd_info.ecfc_conn_alarm);
        l2cu_get_coc_local_cids(sr_cids, p_ccb);
        l2cu_release_coc_ccbs(p_ccb);
        if (disconnect_ind) {
          l2cu_send_disconnect_ind(sr_cids, num_chnls, disconnect_ind);
        }
      }
      break;

    case L2CEVT_L2CA_DATA_WRITE: /* Upper layer data to send */
    case L2CEVT_L2CAP_DATA:      /* Peer data packet rcvd    */
      osi_free(p_data);
      break;

    case L2CEVT_L2CA_DISCONNECT_REQ: /* Upper wants to disconnect */
      l2cu_send_peer_disc_req(p_ccb);
      p_ccb->chnl_state = CST_W4_L2CAP_DISCONNECT_RSP;
      alarm_set_on_mloop(p_ccb->l2c_ccb_timer, L2CAP_CHNL_DISCONNECT_TIMEOUT_MS,
                         l2c_ccb_timer_timeout, p_ccb);
      if (p_ccb->peer_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        if (p_ccb->coc_cmd_info.num_coc_chnls > 1) {
          l2cu_reset_sr_cid(p_ccb);
        }
      }
      break;

    case L2CEVT_L2CAP_INFO_RSP:
      if (p_ccb->peer_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        /* We have feature info, so now give the upper layer connect IND */
        alarm_set_on_mloop(p_ccb->l2c_ccb_timer, L2CAP_CHNL_CONNECT_TIMEOUT_MS,
                           l2c_ccb_timer_timeout, p_ccb);
        L2CAP_TRACE_API("L2CAP - Calling Connect_Ind_Cb(), CID: 0x%04x",
                        p_ccb->local_cid);
        if (p_ccb->p_rcb && p_ccb->p_rcb->api.pL2CA_ConnectInd_Cb) {
          (*p_ccb->p_rcb->api.pL2CA_ConnectInd_Cb)(
            p_ccb->p_lcb->remote_bd_addr, p_ccb->local_cid, p_ccb->p_rcb->psm,
            p_ccb->remote_id);
        }
      }
      break;
    case L2CEVT_L2CA_COC_CONNECT_RSP:
      alarm_cancel(p_ccb->coc_cmd_info.ecfc_conn_alarm);
      alarm_free(p_ccb->coc_cmd_info.ecfc_conn_alarm);
      /* errata id = 14605 */
      p_ccb->coc_cmd_info.authorization_pending =
        L2CAP_AUTHORIZATION_PENDING_RESET;
      /* Result should be OK */
      if (p_ccb->p_lcb->transport == BT_TRANSPORT_LE) {
        if(!l2cu_set_data_length_ext(p_ccb)) {
          L2CAP_TRACE_ERROR("%s Data Length Extension not support ", __func__);
        }
      }
      l2cu_send_peer_credit_based_conn_res(p_ccb,
        p_ccb->coc_cmd_info.ecfc_conn_result);
      l2cu_set_coc_chnl_state(p_ccb, CST_OPEN);
      break;
    case L2CEVT_L2CA_COC_CONNECT_NEG_RSP:
      /* errata id = 14605 */
      p_ccb->coc_cmd_info.authorization_pending =
        L2CAP_AUTHORIZATION_PENDING_RESET;
      /* Result should be Reject */
      l2cu_reject_coc_connection(p_ccb->p_lcb, p_ccb->remote_id,
        p_ccb->coc_cmd_info.ecfc_conn_result,
        p_ccb->coc_cmd_info.num_coc_chnls);
      alarm_cancel(p_ccb->coc_cmd_info.ecfc_conn_alarm);
      alarm_free(p_ccb->coc_cmd_info.ecfc_conn_alarm);
      l2cu_release_coc_ccbs(p_ccb);
       break;
  }
}

/*******************************************************************************
 *
 * Function         l2c_csm_config
 *
 * Description      This function handles events when the channel is in
 *                  CONFIG state.
 *
 * Returns          void
 *
 ******************************************************************************/
static void l2c_csm_config(tL2C_CCB* p_ccb, uint16_t event, void* p_data) {
  tL2CAP_CFG_INFO* p_cfg = (tL2CAP_CFG_INFO*)p_data;
  tL2CA_DISCONNECT_IND_CB* disconnect_ind = NULL;
  uint16_t local_cid = p_ccb->local_cid;
  uint8_t cfg_result;
  tL2C_CFG_REQ_PARAM *rcfg = NULL;
  uint16_t* credit;
  uint64_t total_credits = 0;

  if (p_ccb->p_rcb) {
    disconnect_ind =
        p_ccb->p_rcb->api.pL2CA_DisconnectInd_Cb;
  } else {
    L2CAP_TRACE_WARNING("L2CAP - LCID: 0x%04x  st: CONFIG  evt: %s:p_ccb->p_rcb = null",
                    p_ccb->local_cid, l2c_csm_get_event_name(event));
  }

  L2CAP_TRACE_WARNING("L2CAP - LCID: 0x%04x  st: CONFIG  evt: %s",
                    p_ccb->local_cid, l2c_csm_get_event_name(event));

  switch (event) {
    case L2CEVT_LP_DISCONNECT_IND: /* Link was disconnected */
      L2CAP_TRACE_API(
          "L2CAP - Calling Disconnect_Ind_Cb(), CID: 0x%04x  No Conf Needed",
          p_ccb->local_cid);
      if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        l2cu_release_ccb(p_ccb);
        if (disconnect_ind) {
          (*disconnect_ind)(local_cid, false);
        }
      } else {
         tL2CA_DISCONNECT_IND_CB* disconnect_coc_ind
          = p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_DisconnectInd_Cb: NULL;
        l2cu_clear_reconfig_params(p_ccb);
        l2cu_release_ccb(p_ccb);
        if (disconnect_coc_ind) {
          (*disconnect_coc_ind)(local_cid, false);
        }
       }
      break;

    case L2CEVT_L2CAP_CONFIG_REQ: /* Peer config request   */

      cfg_result = l2cu_process_peer_cfg_req(p_ccb, p_cfg);
      if (cfg_result == L2CAP_PEER_CFG_OK) {
        L2CAP_TRACE_EVENT(
            "L2CAP - Calling Config_Req_Cb(), CID: 0x%04x, C-bit %d",
            p_ccb->local_cid, (p_cfg->flags & L2CAP_CFG_FLAGS_MASK_CONT));
        if (p_ccb->p_rcb && p_ccb->p_rcb->api.pL2CA_ConfigInd_Cb) {
          (*p_ccb->p_rcb->api.pL2CA_ConfigInd_Cb)(p_ccb->local_cid, p_cfg);
        }
      } else if (cfg_result == L2CAP_PEER_CFG_DISCONNECT) {
        /* Disconnect if channels are incompatible */
        L2CAP_TRACE_EVENT("L2CAP - incompatible configurations disconnect");
        l2cu_disconnect_chnl(p_ccb);
      } else /* Return error to peer so he can renegotiate if possible */
      {
        L2CAP_TRACE_EVENT(
            "L2CAP - incompatible configurations trying reconfig");
        l2cu_send_peer_config_rsp(p_ccb, p_cfg);
      }
      break;

    case L2CEVT_L2CAP_CONFIG_RSP: /* Peer config response  */
      l2cu_process_peer_cfg_rsp(p_ccb, p_cfg);

      if (p_cfg->result != L2CAP_CFG_PENDING) {
        /* TBD: When config options grow beyong minimum MTU (48 bytes)
         *      logic needs to be added to handle responses with
         *      continuation bit set in flags field.
         *       1. Send additional config request out until C-bit is cleared in
         * response
         */
        p_ccb->config_done |= OB_CFG_DONE;

        if (p_ccb->config_done & IB_CFG_DONE) {
          /* Verify two sides are in compatible modes before continuing */
          if (p_ccb->our_cfg.fcr.mode != p_ccb->peer_cfg.fcr.mode) {
            l2cu_send_peer_disc_req(p_ccb);
            L2CAP_TRACE_WARNING(
                "L2CAP - Calling Disconnect_Ind_Cb(Incompatible CFG), CID: "
                "0x%04x  No Conf Needed",
                p_ccb->local_cid);
            l2cu_release_ccb(p_ccb);
            if (disconnect_ind) {
              (*disconnect_ind)(local_cid, false);
            }
            break;
          }

          p_ccb->config_done |= RECONFIG_FLAG;
          p_ccb->chnl_state = CST_OPEN;
          l2c_link_adjust_chnl_allocation();
          alarm_cancel(p_ccb->l2c_ccb_timer);

          /* If using eRTM and waiting for an ACK, restart the ACK timer */
          if (p_ccb->fcrb.wait_ack) l2c_fcr_start_timer(p_ccb);

          /*
          ** check p_ccb->our_cfg.fcr.mon_tout and
          *p_ccb->our_cfg.fcr.rtrans_tout
          ** we may set them to zero when sending config request during
          *renegotiation
          */
          if ((p_ccb->our_cfg.fcr.mode == L2CAP_FCR_ERTM_MODE) &&
              ((p_ccb->our_cfg.fcr.mon_tout == 0) ||
               (p_ccb->our_cfg.fcr.rtrans_tout))) {
            l2c_fcr_adj_monitor_retran_timeout(p_ccb);
          }

#if (L2CAP_ERTM_STATS == TRUE)
          p_ccb->fcrb.connect_tick_count = time_get_os_boottime_ms();
#endif
          /* See if we can forward anything on the hold queue */
          if (!fixed_queue_is_empty(p_ccb->xmit_hold_q)) {
            l2c_link_check_send_pkts(p_ccb->p_lcb, NULL, NULL);
          }
        }
      }
      L2CAP_TRACE_WARNING("L2CAP-peer_Config_Rsp,Local CID: 0x%04x,Remote CID: 0x%04x,"
               "PSM: %d,peer MTU present: %d,peer MTU: %d", p_ccb->local_cid,p_ccb->remote_cid,
               p_ccb->p_rcb?p_ccb->p_rcb->psm:0,p_ccb->peer_cfg.mtu_present,p_ccb->peer_cfg.mtu);
      if (p_ccb->p_rcb && p_ccb->p_rcb->api.pL2CA_ConfigCfm_Cb) {
        (*p_ccb->p_rcb->api.pL2CA_ConfigCfm_Cb)(p_ccb->local_cid, p_cfg);
      }
      break;

    case L2CEVT_L2CAP_CONFIG_RSP_NEG: /* Peer config error rsp */
                                      /* Disable the Timer */
      alarm_cancel(p_ccb->l2c_ccb_timer);

      /* If failure was channel mode try to renegotiate */
      if (l2c_fcr_renegotiate_chan(p_ccb, p_cfg) == false) {
        L2CAP_TRACE_API(
            "L2CAP - Calling Config_Rsp_Cb(), CID: 0x%04x, Failure: %d",
            p_ccb->local_cid, p_cfg->result);
        if (p_ccb->p_rcb && p_ccb->p_rcb->api.pL2CA_ConfigCfm_Cb) {
          (*p_ccb->p_rcb->api.pL2CA_ConfigCfm_Cb)(p_ccb->local_cid, p_cfg);
        }
      }
      break;

    case L2CEVT_L2CAP_DISCONNECT_REQ: /* Peer disconnected request */
      alarm_set_on_mloop(p_ccb->l2c_ccb_timer, L2CAP_CHNL_DISCONNECT_TIMEOUT_MS,
                         l2c_ccb_timer_timeout, p_ccb);
      p_ccb->chnl_state = CST_W4_L2CA_DISCONNECT_RSP;
      L2CAP_TRACE_API(
          "L2CAP - Calling Disconnect_Ind_Cb(), CID: 0x%04x  Conf Needed",
          p_ccb->local_cid);

       if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        if (p_ccb->p_rcb && p_ccb->p_rcb->api.pL2CA_DisconnectInd_Cb) {
          (*p_ccb->p_rcb->api.pL2CA_DisconnectInd_Cb)(p_ccb->local_cid, true);
        }
      } else {
        if (p_ccb->p_rcb && p_ccb->p_rcb->coc_api.pL2CA_DisconnectInd_Cb) {
          (*p_ccb->p_rcb->coc_api.pL2CA_DisconnectInd_Cb)(p_ccb->local_cid,
            true);
         }
       }
      break;

    case L2CEVT_L2CA_CONFIG_REQ: /* Upper layer config req   */
      l2cu_process_our_cfg_req(p_ccb, p_cfg);
      l2cu_send_peer_config_req(p_ccb, p_cfg);
      alarm_set_on_mloop(p_ccb->l2c_ccb_timer, L2CAP_CHNL_CFG_TIMEOUT_MS,
                         l2c_ccb_timer_timeout, p_ccb);
      break;

    case L2CEVT_L2CA_CONFIG_RSP: /* Upper layer config rsp   */
      l2cu_process_our_cfg_rsp(p_ccb, p_cfg);

      /* Not finished if continuation flag is set */
      if ((p_cfg->flags & L2CAP_CFG_FLAGS_MASK_CONT) ||
          (p_cfg->result == L2CAP_CFG_PENDING)) {
        /* Send intermediate response; remain in cfg state */
        l2cu_send_peer_config_rsp(p_ccb, p_cfg);
        break;
      }

      /* Local config done; clear cached configuration in case reconfig takes
       * place later */
      p_ccb->peer_cfg.mtu_present = false;
      p_ccb->peer_cfg.flush_to_present = false;
      p_ccb->peer_cfg.qos_present = false;

      p_ccb->config_done |= IB_CFG_DONE;

      if (p_ccb->config_done & OB_CFG_DONE) {
        /* Verify two sides are in compatible modes before continuing */
        if (p_ccb->our_cfg.fcr.mode != p_ccb->peer_cfg.fcr.mode) {
          l2cu_send_peer_disc_req(p_ccb);
          L2CAP_TRACE_WARNING(
              "L2CAP - Calling Disconnect_Ind_Cb(Incompatible CFG), CID: "
              "0x%04x  No Conf Needed",
              p_ccb->local_cid);
          l2cu_release_ccb(p_ccb);
          if (disconnect_ind) {
            (*disconnect_ind)(local_cid, false);
          }
          break;
        }

        p_ccb->config_done |= RECONFIG_FLAG;
        p_ccb->chnl_state = CST_OPEN;
        l2c_link_adjust_chnl_allocation();
        alarm_cancel(p_ccb->l2c_ccb_timer);
      }

      l2cu_send_peer_config_rsp(p_ccb, p_cfg);

      /* If using eRTM and waiting for an ACK, restart the ACK timer */
      if (p_ccb->fcrb.wait_ack) l2c_fcr_start_timer(p_ccb);

#if (L2CAP_ERTM_STATS == TRUE)
      p_ccb->fcrb.connect_tick_count = time_get_os_boottime_ms();
#endif

      /* See if we can forward anything on the hold queue */
      if ((p_ccb->chnl_state == CST_OPEN) &&
          (!fixed_queue_is_empty(p_ccb->xmit_hold_q))) {
        l2c_link_check_send_pkts(p_ccb->p_lcb, NULL, NULL);
      }

      L2CAP_TRACE_WARNING("L2CAP-Upper layer Config_Rsp,Local CID: 0x%04x,"
                          "Remote CID: 0x%04x,PSM: %d,our MTU present:%d,our MTU:%d",
                          p_ccb->local_cid,p_ccb->remote_cid,
                          p_ccb->p_rcb ? p_ccb->p_rcb->psm: 0, p_ccb->our_cfg.mtu_present,
                          p_ccb->our_cfg.mtu);
      break;

    case L2CEVT_L2CA_CONFIG_RSP_NEG: /* Upper layer config reject */
      l2cu_send_peer_config_rsp(p_ccb, p_cfg);
      alarm_set_on_mloop(p_ccb->l2c_ccb_timer, L2CAP_CHNL_CFG_TIMEOUT_MS,
                         l2c_ccb_timer_timeout, p_ccb);
      break;

    case L2CEVT_L2CA_COC_RECONFIG_REQ:
      rcfg = (tL2C_CFG_REQ_PARAM *)p_data;
      l2cu_set_multi_coc_chnl_cfg(p_ccb->p_lcb, rcfg,
                                  CST_CONFIG, ~OB_CFG_DONE);

      l2cu_send_peer_reconfig_req(p_ccb, rcfg);
      // set alarm for upper layer response
      rcfg->l2c_cfg_timer = alarm_new("l2c_ecfc_out_reconfig_timer");
      alarm_set_on_mloop(rcfg->l2c_cfg_timer, L2CAP_CHNL_CFG_TIMEOUT_MS,
                         l2c_rcfg_timer_timeout, rcfg);
      break;

    case L2CEVT_L2CA_COC_RECONFIG_RSP:
      rcfg = (tL2C_CFG_REQ_PARAM *)p_data;
      l2cu_process_local_rcfg_rsp(rcfg);
      l2cu_send_peer_rcfg_rsp(p_ccb->p_lcb, p_ccb, rcfg);
      // send pending outgoing data in reconfig state
      l2c_link_check_send_pkts(p_ccb->p_lcb, NULL, NULL);
      break;

    case L2CEVT_L2CAP_COC_RECONFIG_REQ:
      rcfg = (tL2C_CFG_REQ_PARAM *)p_data;

      // check if peer reconfig request is with valid configurable parameters
      if (l2cu_process_peer_rcfg_req(rcfg)) {
        if ((p_ccb->p_rcb) && (p_ccb->p_rcb->coc_api.pL2CA_CocReconfigInd_Cb)) {
          // set alarm for upper layer response
          rcfg->l2c_cfg_timer = alarm_new("l2c_ecfc_inc_reconfig_timer");
          alarm_set_on_mloop(rcfg->l2c_cfg_timer, L2CAP_CHNL_CFG_TIMEOUT_MS,
                             l2c_rcfg_timer_timeout, rcfg);
          // give callback to upper layer
          (*p_ccb->p_rcb->coc_api.pL2CA_CocReconfigInd_Cb)
              (&rcfg->chnl_info, rcfg->cfg_params.mtu);
        }
      }
      break;

    case L2CEVT_L2CAP_COC_RECONFIG_RSP:
      rcfg = (tL2C_CFG_REQ_PARAM *)p_data;
      l2cu_process_peer_rcfg_rsp(rcfg);
      L2CAP_TRACE_EVENT(
          "L2CAP - Reconfig Rsp with result:%x (%s)."
          " , Values: MTU = %d MPS = %d", rcfg->result, l2cu_get_reconfig_result(rcfg->result),
          rcfg->cfg_params.mtu, rcfg->cfg_params.mps);
      // give confirmation callback
      if ((p_ccb->p_rcb) && (p_ccb->p_rcb->coc_api.pL2CA_CocReconfigCfm_Cb)) {
        (*p_ccb->p_rcb->coc_api.pL2CA_CocReconfigCfm_Cb)(&rcfg->chnl_info, rcfg->result);
      }
      // free reconfig request params
      osi_free(p_data);
      break;

    case L2CEVT_L2CA_DISCONNECT_REQ: /* Upper wants to disconnect */
      l2cu_send_peer_disc_req(p_ccb);
      p_ccb->chnl_state = CST_W4_L2CAP_DISCONNECT_RSP;
      alarm_set_on_mloop(p_ccb->l2c_ccb_timer, L2CAP_CHNL_DISCONNECT_TIMEOUT_MS,
                         l2c_ccb_timer_timeout, p_ccb);
      break;

    case L2CEVT_L2CAP_DATA: /* Peer data packet rcvd    */
      L2CAP_TRACE_API("L2CAP - Calling DataInd_Cb(), CID: 0x%04x",
                      p_ccb->local_cid);
      // send data ind callback to upper layer in ECBFC mode
      if (p_ccb->peer_cfg.fcr.mode == L2CAP_FCR_ECFC_MODE) {
        l2c_fcr_buffer_l2cap_coc_pdu(p_ccb, (BT_HDR*)p_data);
        l2cu_post_data_ind_cb_to_btu(p_ccb);
        break;
      }
#if (L2CAP_NUM_FIXED_CHNLS > 0)
      if (p_ccb->local_cid >= L2CAP_FIRST_FIXED_CHNL &&
          p_ccb->local_cid <= L2CAP_LAST_FIXED_CHNL) {
        if (p_ccb->local_cid < L2CAP_BASE_APPL_CID) {
          if (l2cb.fixed_reg[p_ccb->local_cid - L2CAP_FIRST_FIXED_CHNL]
                  .pL2CA_FixedData_Cb)
            (*l2cb.fixed_reg[p_ccb->local_cid - L2CAP_FIRST_FIXED_CHNL]
                  .pL2CA_FixedData_Cb)(p_ccb->local_cid,
                                       p_ccb->p_lcb->remote_bd_addr,
                                       (BT_HDR*)p_data);
          else
            osi_free(p_data);
          break;
        }
      }
#endif
      if (p_ccb->p_rcb && p_ccb->p_rcb->api.pL2CA_DataInd_Cb) {
        (*p_ccb->p_rcb->api.pL2CA_DataInd_Cb)(p_ccb->local_cid, (BT_HDR*)p_data);
      }
      break;

    case L2CEVT_L2CA_DATA_WRITE: /* Upper layer data to send */
      if (p_ccb->peer_cfg.fcr.mode == L2CAP_FCR_ECFC_MODE) {
        l2c_enqueue_peer_data(p_ccb, (BT_HDR*)p_data);
        // trigger to send pkts only if IB reconfig is not pending
        if (p_ccb->config_done & IB_CFG_DONE) {
          l2c_link_check_send_pkts(p_ccb->p_lcb, NULL, NULL);
        }
        break;
      }
      if (p_ccb->config_done & OB_CFG_DONE)
        l2c_enqueue_peer_data(p_ccb, (BT_HDR*)p_data);
      else
        osi_free(p_data);
      break;

    case L2CEVT_L2CAP_RECV_FLOW_CONTROL_CREDIT:
      credit = (uint16_t*)p_data;
      total_credits = p_ccb->peer_conn_cfg.credits + *credit;
      L2CAP_TRACE_DEBUG("%s Credits received %d, total credits = %ld", __func__,
                            *credit, total_credits);

      if ((p_ccb->peer_cfg.fcr.mode == L2CAP_FCR_ECFC_MODE &&
                (total_credits > L2CAP_COC_CREDIT_DEFAULT)) ||
          (p_ccb->peer_cfg.fcr.mode == L2CAP_FCR_LE_COC_MODE &&
                (total_credits > L2CAP_LE_CREDIT_MAX))) {
        /* we have received credits more than max coc credits,
         * so disconnecting the Le Coc Channel
         */
        L2CAP_TRACE_ERROR("%s total credits %d are more than max value", __func__, total_credits);
        l2cu_send_peer_coc_disc_req(p_ccb);

        p_ccb->chnl_state = CST_W4_L2CAP_DISCONNECT_RSP;
        alarm_set_on_mloop(p_ccb->l2c_ccb_timer, L2CAP_CHNL_DISCONNECT_TIMEOUT_MS,
                               l2c_ccb_timer_timeout, p_ccb);
      } else {
        p_ccb->peer_conn_cfg.credits += *credit;

        tL2CA_CREDITS_RECEIVED_CB* cr_cb = NULL;
        if (p_ccb->p_rcb) {
          cr_cb =
            (p_ccb->peer_cfg.fcr.mode == L2CAP_FCR_ECFC_MODE ?
            p_ccb->p_rcb->coc_api.pL2CA_CocCreditsReceived_Cb :
            p_ccb->p_rcb->api.pL2CA_CreditsReceived_Cb);
        }
        if ((p_ccb->p_lcb->transport == BT_TRANSPORT_LE ||
            p_ccb->peer_cfg.fcr.mode == L2CAP_FCR_LE_COC_MODE) && (cr_cb)) {
          (*cr_cb)(p_ccb->local_cid, *credit, p_ccb->peer_conn_cfg.credits);
        }
        l2c_link_check_send_pkts(p_ccb->p_lcb, NULL, NULL);
      }

      break;

    case L2CEVT_L2CA_SEND_FLOW_CONTROL_CREDIT:
      credit = (uint16_t*)p_data;
      l2cu_send_flow_control_credit(p_ccb, *credit);
      break;

    case L2CEVT_TIMEOUT:
      L2CAP_TRACE_API(
        "L2CAP - Calling Disconnect_Ind_Cb(), CID: 0x%04x  No Conf Needed",
         p_ccb->local_cid);
      l2cu_send_peer_disc_req(p_ccb);
      if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        l2cu_release_ccb(p_ccb);
        if (disconnect_ind) {
          (*disconnect_ind)(local_cid, false);
        }
      } else {
        tL2CA_DISCONNECT_IND_CB* disconnect_coc_ind
          = p_ccb->p_rcb->coc_api.pL2CA_DisconnectInd_Cb;
        //Its in config state, so no need to do for ecfc group.
        l2cu_release_ccb(p_ccb);
        if (disconnect_coc_ind) {
          (*disconnect_coc_ind)(local_cid, false);
        }
      }
      break;
  }
}

/*******************************************************************************
 *
 * Function         l2c_csm_open
 *
 * Description      This function handles events when the channel is in
 *                  OPEN state.
 *
 * Returns          void
 *
 ******************************************************************************/
static void l2c_csm_open(tL2C_CCB* p_ccb, uint16_t event, void* p_data) {
  uint16_t local_cid = p_ccb->local_cid;
  tL2CAP_CFG_INFO* p_cfg;
  tL2C_CHNL_STATE tempstate;
  uint8_t tempcfgdone;
  uint8_t cfg_result;
  uint16_t* credit;
  uint64_t total_credits = 0;
  tL2C_CFG_REQ_PARAM *cfg_params;

  if (p_ccb->p_rcb == NULL) {
    L2CAP_TRACE_WARNING("L2CAP - LCID: 0x%04x  st: OPEN  evt: %s:p_ccb->p_rcb = null",
                    p_ccb->local_cid, l2c_csm_get_event_name(event));
  }

  L2CAP_TRACE_EVENT("L2CAP - LCID: 0x%04x  st: OPEN  evt: %s", p_ccb->local_cid,
                    l2c_csm_get_event_name(event));

#if (L2CAP_UCD_INCLUDED == TRUE)
  if (local_cid == L2CAP_CONNECTIONLESS_CID) {
    /* check if this event can be processed by UCD */
    if (l2c_ucd_process_event(p_ccb, event, p_data)) {
      /* The event is processed by UCD state machine */
      return;
    }
  }
#endif

  switch (event) {
    case L2CEVT_LP_DISCONNECT_IND: /* Link was disconnected */
      L2CAP_TRACE_API(
          "L2CAP - Calling Disconnect_Ind_Cb(), CID: 0x%04x  No Conf Needed",
          p_ccb->local_cid);

      if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        l2cu_release_ccb(p_ccb);
        if (p_ccb->p_rcb && p_ccb->p_rcb->api.pL2CA_DisconnectInd_Cb)
          (*p_ccb->p_rcb->api.pL2CA_DisconnectInd_Cb)(local_cid, false);
      } else {
        tL2CA_DISCONNECT_IND_CB* disconnect_coc_ind
          = p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_DisconnectInd_Cb: NULL;
        //Its in open state, so no need to do for ecfc group.
        l2cu_release_ccb(p_ccb);
        if (disconnect_coc_ind) {
          (*disconnect_coc_ind)(local_cid, false);
       }
      }
      break;

    case L2CEVT_LP_QOS_VIOLATION_IND: /* QOS violation         */
      /* Tell upper layer. If service guaranteed, then clear the channel   */
      if (p_ccb->p_rcb && p_ccb->p_rcb->api.pL2CA_QoSViolationInd_Cb)
        (*p_ccb->p_rcb->api.pL2CA_QoSViolationInd_Cb)(
            p_ccb->p_lcb->remote_bd_addr);
      break;

    case L2CEVT_L2CAP_CONFIG_REQ: /* Peer config request   */
      p_cfg = (tL2CAP_CFG_INFO*)p_data;

      tempstate = p_ccb->chnl_state;
      tempcfgdone = p_ccb->config_done;
      p_ccb->chnl_state = CST_CONFIG;
      p_ccb->config_done &= ~IB_CFG_DONE;

      alarm_set_on_mloop(p_ccb->l2c_ccb_timer, L2CAP_CHNL_CFG_TIMEOUT_MS,
                         l2c_ccb_timer_timeout, p_ccb);

      cfg_result = l2cu_process_peer_cfg_req(p_ccb, p_cfg);
      if (cfg_result == L2CAP_PEER_CFG_OK) {
        if (p_ccb->p_rcb && p_ccb->p_rcb->api.pL2CA_ConfigInd_Cb) {
          (*p_ccb->p_rcb->api.pL2CA_ConfigInd_Cb)(p_ccb->local_cid, p_cfg);
        }
      }

      /* Error in config parameters: reset state and config flag */
      else if (cfg_result == L2CAP_PEER_CFG_UNACCEPTABLE) {
        alarm_cancel(p_ccb->l2c_ccb_timer);
        p_ccb->chnl_state = tempstate;
        p_ccb->config_done = tempcfgdone;
        l2cu_send_peer_config_rsp(p_ccb, p_cfg);
      } else /* L2CAP_PEER_CFG_DISCONNECT */
      {
        /* Disconnect if channels are incompatible
         * Note this should not occur if reconfigure
         * since this should have never passed original config.
         */
        l2cu_disconnect_chnl(p_ccb);
      }
      break;

    case L2CEVT_L2CAP_DISCONNECT_REQ: /* Peer disconnected request */
      if (p_ccb->p_lcb->transport != BT_TRANSPORT_LE) {
        /* Make sure we are not in sniff mode */
        {
          tBTM_PM_PWR_MD settings;
          memset((void*)&settings, 0, sizeof(settings));
          settings.mode = BTM_PM_MD_ACTIVE;
          BTM_SetPowerMode(BTM_PM_SET_ONLY_ID, p_ccb->p_lcb->remote_bd_addr,
                           &settings);
        }
      }
      L2CAP_TRACE_API(
          "L2CAP - Calling Disconnect_Ind_Cb(), CID: 0x%04x  Conf Needed",
          p_ccb->local_cid);

      p_ccb->chnl_state = CST_W4_L2CA_DISCONNECT_RSP;
      alarm_set_on_mloop(p_ccb->l2c_ccb_timer, L2CAP_CHNL_DISCONNECT_TIMEOUT_MS,
                           l2c_ccb_timer_timeout, p_ccb);
      if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        if (p_ccb->p_rcb && p_ccb->p_rcb->api.pL2CA_DisconnectInd_Cb) {
          (*p_ccb->p_rcb->api.pL2CA_DisconnectInd_Cb)(p_ccb->local_cid, true);
        }
      } else {
        if (p_ccb->p_rcb && p_ccb->p_rcb->coc_api.pL2CA_DisconnectInd_Cb)
          (*p_ccb->p_rcb->coc_api.pL2CA_DisconnectInd_Cb)(p_ccb->local_cid,
            true);
      }
      break;

    case L2CEVT_L2CAP_DATA: /* Peer data packet rcvd    */
      // send data ind callback to upper layer in ECBFC mode
      if (p_ccb->peer_cfg.fcr.mode == L2CAP_FCR_ECFC_MODE) {
        l2c_fcr_buffer_l2cap_coc_pdu(p_ccb, (BT_HDR*)p_data);
        l2cu_post_data_ind_cb_to_btu(p_ccb);
        break;
      }
      if ((p_ccb->p_rcb) && (p_ccb->p_rcb->api.pL2CA_DataInd_Cb))
        (*p_ccb->p_rcb->api.pL2CA_DataInd_Cb)(p_ccb->local_cid,
                                              (BT_HDR*)p_data);
      break;

    case L2CEVT_L2CA_DISCONNECT_REQ: /* Upper wants to disconnect */
      if (p_ccb->p_lcb->transport != BT_TRANSPORT_LE) {
        /* Make sure we are not in sniff mode */
        {
          tBTM_PM_PWR_MD settings;
          memset((void*)&settings, 0, sizeof(settings));
          settings.mode = BTM_PM_MD_ACTIVE;
          BTM_SetPowerMode(BTM_PM_SET_ONLY_ID, p_ccb->p_lcb->remote_bd_addr,
                           &settings);
        }
      }

      if (p_ccb->p_lcb->transport == BT_TRANSPORT_LE)
        l2cu_send_peer_coc_disc_req(p_ccb);
      else
        l2cu_send_peer_disc_req(p_ccb);

      p_ccb->chnl_state = CST_W4_L2CAP_DISCONNECT_RSP;
      alarm_set_on_mloop(p_ccb->l2c_ccb_timer, L2CAP_CHNL_DISCONNECT_TIMEOUT_MS,
                         l2c_ccb_timer_timeout, p_ccb);
      break;

    case L2CEVT_L2CA_DATA_WRITE: /* Upper layer data to send */
      l2c_enqueue_peer_data(p_ccb, (BT_HDR*)p_data);
      l2c_link_check_send_pkts(p_ccb->p_lcb, NULL, NULL);
      break;

    case L2CEVT_L2CA_CONFIG_REQ: /* Upper layer config req   */
      p_ccb->chnl_state = CST_CONFIG;
      p_ccb->config_done &= ~CFG_DONE_MASK;
      l2cu_process_our_cfg_req(p_ccb, (tL2CAP_CFG_INFO*)p_data);
      l2cu_send_peer_config_req(p_ccb, (tL2CAP_CFG_INFO*)p_data);
      alarm_set_on_mloop(p_ccb->l2c_ccb_timer, L2CAP_CHNL_CFG_TIMEOUT_MS,
                         l2c_ccb_timer_timeout, p_ccb);
      break;

    case L2CEVT_TIMEOUT:
      /* Process the monitor/retransmission time-outs in flow control/retrans
       * mode */
      if (p_ccb->peer_cfg.fcr.mode == L2CAP_FCR_ERTM_MODE)
        l2c_fcr_proc_tout(p_ccb);
      break;

    case L2CEVT_ACK_TIMEOUT:
      l2c_fcr_proc_ack_tout(p_ccb);
      break;

    case L2CEVT_L2CA_SEND_FLOW_CONTROL_CREDIT:
      credit = (uint16_t*)p_data;
      l2cu_send_flow_control_credit(p_ccb, *credit);
      break;

    case L2CEVT_L2CAP_RECV_FLOW_CONTROL_CREDIT:
      credit = (uint16_t*)p_data;
      total_credits = p_ccb->peer_conn_cfg.credits + *credit;
      L2CAP_TRACE_DEBUG("%s Credits received %d, total credits = %ld", __func__,
                            *credit, total_credits);

      if ((p_ccb->peer_cfg.fcr.mode == L2CAP_FCR_ECFC_MODE &&
                (total_credits > L2CAP_COC_CREDIT_DEFAULT)) ||
          (p_ccb->peer_cfg.fcr.mode == L2CAP_FCR_LE_COC_MODE &&
                (total_credits > L2CAP_LE_CREDIT_MAX))) {
        /* we have received credits more than max coc credits,
         * so disconnecting the Le Coc Channel
         */
        L2CAP_TRACE_ERROR("%s total credits %d are more than max value", __func__, total_credits);
        l2cu_send_peer_coc_disc_req(p_ccb);

        p_ccb->chnl_state = CST_W4_L2CAP_DISCONNECT_RSP;
        alarm_set_on_mloop(p_ccb->l2c_ccb_timer, L2CAP_CHNL_DISCONNECT_TIMEOUT_MS,
                               l2c_ccb_timer_timeout, p_ccb);
      } else {
        p_ccb->peer_conn_cfg.credits += *credit;

        if ((p_ccb->p_lcb->transport == BT_TRANSPORT_LE ||
            p_ccb->peer_cfg.fcr.mode == L2CAP_FCR_LE_COC_MODE) && p_ccb->p_rcb) {
          tL2CA_CREDITS_RECEIVED_CB* cr_cb =
              (p_ccb->peer_cfg.fcr.mode == L2CAP_FCR_ECFC_MODE ?
              p_ccb->p_rcb->coc_api.pL2CA_CocCreditsReceived_Cb :
              p_ccb->p_rcb->api.pL2CA_CreditsReceived_Cb);
          if (cr_cb) {
            (*cr_cb)(p_ccb->local_cid, *credit, p_ccb->peer_conn_cfg.credits);
          }
          l2c_link_check_send_pkts(p_ccb->p_lcb, NULL, NULL);
        }
      }

      break;

    case L2CEVT_L2CA_COC_RECONFIG_REQ:
      cfg_params = (tL2C_CFG_REQ_PARAM *)p_data;
      l2cu_set_multi_coc_chnl_cfg(p_ccb->p_lcb, cfg_params,
                                  CST_CONFIG, ~OB_CFG_DONE);

      l2cu_send_peer_reconfig_req(p_ccb, cfg_params);
      // set alarm for upper layer response
      cfg_params->l2c_cfg_timer = alarm_new("l2c_ecfc_out_reconfig_timer");
      alarm_set_on_mloop(cfg_params->l2c_cfg_timer, L2CAP_CHNL_CFG_TIMEOUT_MS,
                         l2c_rcfg_timer_timeout, cfg_params);
      break;

    case L2CEVT_L2CAP_COC_RECONFIG_REQ:
      cfg_params = (tL2C_CFG_REQ_PARAM *)p_data;

      // check if peer reconfig request is with valid configurable parameters
      if (l2cu_process_peer_rcfg_req(cfg_params)) {
        if ((p_ccb->p_rcb) && (p_ccb->p_rcb->coc_api.pL2CA_CocReconfigInd_Cb)) {
          // set alarm for upper layer response
          cfg_params->l2c_cfg_timer = alarm_new("l2c_ecfc_inc_reconfig_timer");
          alarm_set_on_mloop(cfg_params->l2c_cfg_timer, L2CAP_CHNL_CFG_TIMEOUT_MS,
                             l2c_rcfg_timer_timeout, cfg_params);
          // give callback to upper layer
          (*p_ccb->p_rcb->coc_api.pL2CA_CocReconfigInd_Cb)
              (&cfg_params->chnl_info, cfg_params->cfg_params.mtu);
        }
      }
      break;

  }
}

/*******************************************************************************
 *
 * Function         l2c_csm_w4_l2cap_disconnect_rsp
 *
 * Description      This function handles events when the channel is in
 *                  CST_W4_L2CAP_DISCONNECT_RSP state.
 *
 * Returns          void
 *
 ******************************************************************************/
static void l2c_csm_w4_l2cap_disconnect_rsp(tL2C_CCB* p_ccb, uint16_t event,
                                            void* p_data) {
  tL2CA_DISCONNECT_CFM_CB* disconnect_cfm = NULL;
  uint16_t local_cid = p_ccb->local_cid;

  L2CAP_TRACE_WARNING("L2CAP - LCID: 0x%04x  st: W4_L2CAP_DISC_RSP  evt: %s",
                    p_ccb->local_cid, l2c_csm_get_event_name(event));
  if (p_ccb->p_rcb) {
    disconnect_cfm =
        p_ccb->p_rcb->api.pL2CA_DisconnectCfm_Cb;
  } else {
    L2CAP_TRACE_WARNING("L2CAP - LCID: 0x%04x  st: W4_L2CAP_DISC_RSP  evt: %s:"
          "p_ccb->p_rcb = null", p_ccb->local_cid, l2c_csm_get_event_name(event));
  }

  switch (event) {
    case L2CEVT_L2CAP_DISCONNECT_RSP: /* Peer disconnect response */
      L2CAP_TRACE_API("L2CAP - Calling DisconnectCfm_Cb(), CID: 0x%04x",
                      local_cid);
      if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        l2cu_release_ccb(p_ccb);
        if (disconnect_cfm) {
          (*disconnect_cfm)(local_cid, L2CAP_DISC_OK);
        }
      } else {
        tL2CA_DISCONNECT_CFM_CB* disconnect_coc_cfm =
          p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_DisconnectCfm_Cb: NULL;
        l2cu_release_ccb(p_ccb);
        //Its in Connection state, so no need to update in cid group array.
        if (disconnect_coc_cfm) {
          (*disconnect_coc_cfm)(local_cid, L2CAP_DISC_OK);
        }
      }
      break;

    case L2CEVT_L2CAP_DISCONNECT_REQ: /* Peer disconnect request  */
      l2cu_send_peer_disc_rsp(p_ccb->p_lcb, p_ccb->remote_id, p_ccb->local_cid,
                              p_ccb->remote_cid);
      if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        l2cu_release_ccb(p_ccb);
        if (disconnect_cfm) {
          L2CAP_TRACE_API("L2CAP - Calling DisconnectCfm_Cb(), CID: 0x%04x",
                          local_cid);
          (*disconnect_cfm)(local_cid, L2CAP_DISC_OK);
        }
      } else {
        tL2CA_DISCONNECT_CFM_CB* disconnect_coc_cfm
          = p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_DisconnectCfm_Cb: NULL;
        l2cu_release_ccb(p_ccb);
        //Its in Connection state, so no need to update in cid group array.
        if (disconnect_coc_cfm) {
          (*disconnect_coc_cfm)(local_cid, L2CAP_DISC_OK);
        }
      }
      break;

    case L2CEVT_LP_DISCONNECT_IND: /* Link was disconnected */
    case L2CEVT_TIMEOUT:           /* Timeout */
      L2CAP_TRACE_API("L2CAP - Calling DisconnectCfm_Cb(), CID: 0x%04x",
                      local_cid);
      if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        l2cu_release_ccb(p_ccb);
        if (disconnect_cfm) {
          (*disconnect_cfm)(local_cid, L2CAP_DISC_TIMEOUT);
        }
      } else {
        tL2CA_DISCONNECT_CFM_CB* disconnect_coc_cfm
          = p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_DisconnectCfm_Cb: NULL;
        l2cu_release_ccb(p_ccb);
        //Its in Connection state, so no need to update in cid group array.
        if (disconnect_coc_cfm) {
          (*disconnect_coc_cfm)(local_cid, L2CAP_DISC_TIMEOUT); //please change it
        }
      }
      break;

    case L2CEVT_L2CAP_DATA:      /* Peer data packet rcvd    */
    case L2CEVT_L2CA_DATA_WRITE: /* Upper layer data to send */
      osi_free(p_data);
      break;
  }
}

/*******************************************************************************
 *
 * Function         l2c_csm_w4_l2ca_disconnect_rsp
 *
 * Description      This function handles events when the channel is in
 *                  CST_W4_L2CA_DISCONNECT_RSP state.
 *
 * Returns          void
 *
 ******************************************************************************/
static void l2c_csm_w4_l2ca_disconnect_rsp(tL2C_CCB* p_ccb, uint16_t event,
                                           void* p_data) {
  tL2CA_DISCONNECT_IND_CB* disconnect_ind = NULL;
  uint16_t local_cid = p_ccb->local_cid;

  L2CAP_TRACE_WARNING("L2CAP - LCID: 0x%04x  st: W4_L2CA_DISC_RSP  evt: %s",
                    p_ccb->local_cid, l2c_csm_get_event_name(event));
  if (p_ccb->p_rcb) {
    disconnect_ind =
        p_ccb->p_rcb->api.pL2CA_DisconnectInd_Cb;
  } else {
    L2CAP_TRACE_WARNING("L2CAP - LCID: 0x%04x  st: W4_L2CA_DISC_RSP  evt: %s:"
         "p_ccb->p_rcb =null", p_ccb->local_cid, l2c_csm_get_event_name(event));
  }

  switch (event) {
    case L2CEVT_LP_DISCONNECT_IND: /* Link was disconnected */
      L2CAP_TRACE_API(
          "L2CAP - Calling Disconnect_Ind_Cb(), CID: 0x%04x  No Conf Needed",
          p_ccb->local_cid);

      if (p_ccb->peer_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        l2cu_release_ccb(p_ccb);
        if (disconnect_ind) {
          (*disconnect_ind)(local_cid, false);
        }
      } else {
        tL2CA_DISCONNECT_IND_CB* disconnect_coc_ind
          = p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_DisconnectInd_Cb: NULL;
        l2cu_release_ccb(p_ccb);
        //Its in Connection state, so no need to update in cid group array.
        if (disconnect_coc_ind) {
          (*disconnect_coc_ind)(local_cid, false);
        }
      }
      break;

    case L2CEVT_TIMEOUT:
      l2cu_send_peer_disc_rsp(p_ccb->p_lcb, p_ccb->remote_id, p_ccb->local_cid,
                              p_ccb->remote_cid);
      L2CAP_TRACE_API(
          "L2CAP - Calling Disconnect_Ind_Cb(), CID: 0x%04x  No Conf Needed",
          p_ccb->local_cid);

      if (p_ccb->our_cfg.fcr.mode != L2CAP_FCR_ECFC_MODE) {
        l2cu_release_ccb(p_ccb);
        if (disconnect_ind) {
          (*disconnect_ind)(local_cid, false);
        }
      } else {
        tL2CA_DISCONNECT_IND_CB* disconnect_coc_ind
          = p_ccb->p_rcb ? p_ccb->p_rcb->coc_api.pL2CA_DisconnectInd_Cb: NULL;
        l2cu_release_ccb(p_ccb);
        //Its in Connection state, so no need to update in cid group array.
        if (disconnect_coc_ind) {
          (*disconnect_coc_ind)(local_cid, false);
        }
      }
      break;

    case L2CEVT_L2CA_DISCONNECT_REQ: /* Upper disconnect request */
    case L2CEVT_L2CA_DISCONNECT_RSP: /* Upper disconnect response */
      l2cu_send_peer_disc_rsp(p_ccb->p_lcb, p_ccb->remote_id, p_ccb->local_cid,
                              p_ccb->remote_cid);
      l2cu_release_ccb(p_ccb);
      break;

    case L2CEVT_L2CAP_DATA:      /* Peer data packet rcvd    */
    case L2CEVT_L2CA_DATA_WRITE: /* Upper layer data to send */
      osi_free(p_data);
      break;
  }
}

/*******************************************************************************
 *
 * Function         l2c_csm_get_event_name
 *
 * Description      This function returns the event name.
 *
 * NOTE             conditionally compiled to save memory.
 *
 * Returns          pointer to the name
 *
 ******************************************************************************/
static const char* l2c_csm_get_event_name(uint16_t event) {
  switch (event) {
    case L2CEVT_LP_CONNECT_CFM: /* Lower layer connect confirm          */
      return ("LOWER_LAYER_CONNECT_CFM");
    case L2CEVT_LP_CONNECT_CFM_NEG: /* Lower layer connect confirm (failed) */
      return ("LOWER_LAYER_CONNECT_CFM_NEG");
    case L2CEVT_LP_CONNECT_IND: /* Lower layer connect indication       */
      return ("LOWER_LAYER_CONNECT_IND");
    case L2CEVT_LP_DISCONNECT_IND: /* Lower layer disconnect indication    */
      return ("LOWER_LAYER_DISCONNECT_IND");
    case L2CEVT_LP_QOS_CFM: /* Lower layer QOS confirmation         */
      return ("LOWER_LAYER_QOS_CFM");
    case L2CEVT_LP_QOS_CFM_NEG: /* Lower layer QOS confirmation (failed)*/
      return ("LOWER_LAYER_QOS_CFM_NEG");
    case L2CEVT_LP_QOS_VIOLATION_IND: /* Lower layer QOS violation indication */
      return ("LOWER_LAYER_QOS_VIOLATION_IND");

    case L2CEVT_SEC_COMP: /* Security cleared successfully        */
      return ("SECURITY_COMPLETE");
    case L2CEVT_SEC_COMP_NEG: /* Security procedure failed            */
      return ("SECURITY_COMPLETE_NEG");

    case L2CEVT_L2CAP_CONNECT_REQ: /* Peer connection request              */
      return ("PEER_CONNECT_REQ");
    case L2CEVT_L2CAP_CONNECT_RSP: /* Peer connection response             */
      return ("PEER_CONNECT_RSP");
    case L2CEVT_L2CAP_CONNECT_RSP_PND: /* Peer connection response pending */
      return ("PEER_CONNECT_RSP_PND");
    case L2CEVT_L2CAP_CONNECT_RSP_NEG: /* Peer connection response (failed) */
      return ("PEER_CONNECT_RSP_NEG");
    case L2CEVT_L2CAP_CONFIG_REQ: /* Peer configuration request           */
      return ("PEER_CONFIG_REQ");
    case L2CEVT_L2CAP_CONFIG_RSP: /* Peer configuration response          */
      return ("PEER_CONFIG_RSP");
    case L2CEVT_L2CAP_CONFIG_RSP_NEG: /* Peer configuration response (failed) */
      return ("PEER_CONFIG_RSP_NEG");
    case L2CEVT_L2CAP_DISCONNECT_REQ: /* Peer disconnect request              */
      return ("PEER_DISCONNECT_REQ");
    case L2CEVT_L2CAP_DISCONNECT_RSP: /* Peer disconnect response             */
      return ("PEER_DISCONNECT_RSP");
    case L2CEVT_L2CAP_DATA: /* Peer data                            */
      return ("PEER_DATA");

    case L2CEVT_L2CA_CONNECT_REQ: /* Upper layer connect request          */
      return ("UPPER_LAYER_CONNECT_REQ");
    case L2CEVT_L2CA_CONNECT_RSP: /* Upper layer connect response         */
      return ("UPPER_LAYER_CONNECT_RSP");
    case L2CEVT_L2CA_CONNECT_RSP_NEG: /* Upper layer connect response (failed)*/
      return ("UPPER_LAYER_CONNECT_RSP_NEG");
    case L2CEVT_L2CA_CONFIG_REQ: /* Upper layer config request           */
      return ("UPPER_LAYER_CONFIG_REQ");
    case L2CEVT_L2CA_CONFIG_RSP: /* Upper layer config response          */
      return ("UPPER_LAYER_CONFIG_RSP");
    case L2CEVT_L2CA_CONFIG_RSP_NEG: /* Upper layer config response (failed) */
      return ("UPPER_LAYER_CONFIG_RSP_NEG");
    case L2CEVT_L2CA_DISCONNECT_REQ: /* Upper layer disconnect request       */
      return ("UPPER_LAYER_DISCONNECT_REQ");
    case L2CEVT_L2CA_DISCONNECT_RSP: /* Upper layer disconnect response      */
      return ("UPPER_LAYER_DISCONNECT_RSP");
    case L2CEVT_L2CA_DATA_READ: /* Upper layer data read                */
      return ("UPPER_LAYER_DATA_READ");
    case L2CEVT_L2CA_DATA_WRITE: /* Upper layer data write               */
      return ("UPPER_LAYER_DATA_WRITE");
    case L2CEVT_TIMEOUT: /* Timeout                              */
      return ("TIMEOUT");
    case L2CEVT_SEC_RE_SEND_CMD:
      return ("SEC_RE_SEND_CMD");
    case L2CEVT_L2CAP_INFO_RSP: /* Peer information response            */
      return ("L2CEVT_L2CAP_INFO_RSP");
    case L2CEVT_ACK_TIMEOUT:
      return ("L2CEVT_ACK_TIMEOUT");
    case L2CEVT_L2CA_SEND_FLOW_CONTROL_CREDIT: /* Upper layer send credit packet
                                                  */
      return ("SEND_FLOW_CONTROL_CREDIT");
    case L2CEVT_L2CAP_RECV_FLOW_CONTROL_CREDIT: /* Peer send credit packet */
      return ("RECV_FLOW_CONTROL_CREDIT");
    case L2CEVT_L2CA_COC_CONNECT_REQ:
      return ("UPPER_LAYER_COC_CONNECT_REQ");
    case L2CEVT_L2CA_COC_CONNECT_RSP:
      return ("UPPER_LAYER_COC_CONNECT_RSP");
    case L2CEVT_L2CA_COC_CONNECT_NEG_RSP:
      return ("UPPER_LAYER_COC_CONNECT_NEG_RSP");
    case L2CEVT_L2CAP_COC_CONNECT_REQ:
      return ("PEER_COC_CONNECT_REQ");
    case L2CEVT_L2CAP_COC_CONNECT_RSP:
      return ("PEER_COC_CONNECT_RSP");
    case L2CEVT_L2CAP_COC_CONNECT_NEG_RSP:
      return ("PEER_COC_CONNECT_NEG_RSP");
    case L2CEVT_L2CA_COC_RECONFIG_REQ:      /* upper layer reconfig request */
      return ("UPPER_LAYER_RECONFIG_REQ");
    case L2CEVT_L2CAP_COC_RECONFIG_REQ:     /* peer reconfig request */
      return ("PEER_COC_RECONFIG_REQ");
    case L2CEVT_L2CA_COC_RECONFIG_RSP:      /* upper layer reconfig response */
      return ("UPPER_LAYER_RECONFIG_RSP");
    case L2CEVT_L2CAP_COC_RECONFIG_RSP:     /* peer reconfig response */
      return ("PEER_COC_RECONFIG_RSP");

    default:
      return ("???? UNKNOWN EVENT");
  }
}

/*******************************************************************************
 *
 * Function         l2c_enqueue_peer_data
 *
 * Description      Enqueues data destined for the peer in the ccb. Handles
 *                  FCR segmentation and checks for congestion.
 *
 * Returns          void
 *
 ******************************************************************************/
void l2c_enqueue_peer_data(tL2C_CCB* p_ccb, BT_HDR* p_buf) {
  uint8_t* p;

  if (p_ccb->peer_cfg.fcr.mode != L2CAP_FCR_BASIC_MODE) {
    p_buf->event = 0;
  } else {
    /* Save the channel ID for faster counting */
    p_buf->event = p_ccb->local_cid;

    /* Step back to add the L2CAP header */
    p_buf->offset -= L2CAP_PKT_OVERHEAD;
    p_buf->len += L2CAP_PKT_OVERHEAD;

    /* Set the pointer to the beginning of the data */
    p = (uint8_t*)(p_buf + 1) + p_buf->offset;

    /* Now the L2CAP header */
    UINT16_TO_STREAM(p, p_buf->len - L2CAP_PKT_OVERHEAD);
    UINT16_TO_STREAM(p, p_ccb->remote_cid);
  }

  if (p_ccb->xmit_hold_q == NULL) {
    L2CAP_TRACE_ERROR(
        "%s: empty queue: p_ccb = %p p_ccb->in_use = %d p_ccb->chnl_state = %d "
        "p_ccb->local_cid = %u p_ccb->remote_cid = %u",
        __func__, p_ccb, p_ccb->in_use, p_ccb->chnl_state, p_ccb->local_cid,
        p_ccb->remote_cid);
  }
  fixed_queue_enqueue(p_ccb->xmit_hold_q, p_buf);

  l2cu_check_channel_congestion(p_ccb);

#if (L2CAP_ROUND_ROBIN_CHANNEL_SERVICE == TRUE)
  /* if new packet is higher priority than serving ccb and it is not overrun */
  if ((p_ccb->p_lcb->rr_pri > p_ccb->ccb_priority) &&
      (p_ccb->p_lcb->rr_serv[p_ccb->ccb_priority].quota > 0)) {
    /* send out higher priority packet */
    p_ccb->p_lcb->rr_pri = p_ccb->ccb_priority;
  }
#endif

  /* if we are doing a round robin scheduling, set the flag */
  if (p_ccb->p_lcb->link_xmit_quota == 0) l2cb.check_round_robin = true;
}
