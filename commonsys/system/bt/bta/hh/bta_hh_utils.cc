/******************************************************************************
 *
 *  Copyright (C) 2005-2012 Broadcom Corporation
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
#include <string.h>

#include "bt_target.h"
#if (BTA_HH_INCLUDED == TRUE)

#include "bta_hh_int.h"
#include "osi/include/osi.h"
#include "device/include/interop.h"
#include "device/include/interop_config.h"
#include "btif/include/btif_storage.h"
/* if SSR max latency is not defined by remote device, set the default value
   as half of the link supervision timeout */
#define BTA_HH_GET_DEF_SSR_MAX_LAT(x) ((x) >> 1)

/*****************************************************************************
 *  Constants
 ****************************************************************************/
#define BTA_HH_KB_CTRL_MASK 0x11
#define BTA_HH_KB_SHIFT_MASK 0x22
#define BTA_HH_KB_ALT_MASK 0x44
#define BTA_HH_KB_GUI_MASK 0x88

#define BTA_HH_KB_CAPS_LOCK 0x39 /* caps lock */
#define BTA_HH_KB_NUM_LOCK 0x53  /* num lock */

#define BTA_HH_MAX_RPT_CHARS 8

static const uint8_t bta_hh_mod_key_mask[BTA_HH_MOD_MAX_KEY] = {
    BTA_HH_KB_CTRL_MASK, BTA_HH_KB_SHIFT_MASK, BTA_HH_KB_ALT_MASK,
    BTA_HH_KB_GUI_MASK};

/*******************************************************************************
**      Function       blacklist_adjust_sniff_subrate
**
**      Description    It's used to update SSR parameter such as max latency,
**                     if device is found in HID blacklist.
**
**      Returns        None
*******************************************************************************/
static void blacklist_adjust_sniff_subrate(RawAddress peer_dev, UINT16 *ssr_max_lat)
{
    UINT16 old_ssr_max_lat = *ssr_max_lat;
    RawAddress remote_bdaddr = peer_dev;
    if (interop_match_addr_get_max_lat(INTEROP_UPDATE_HID_SSR_MAX_LAT, &remote_bdaddr,
        ssr_max_lat)) {
        APPL_TRACE_WARNING("%s: Device in blacklist for ssr, max latency changed "
            "from %d to %d", __func__, old_ssr_max_lat, *ssr_max_lat);
    }
}


/*******************************************************************************
 *
 * Function         bta_hh_find_cb
 *
 * Description      Find best available control block according to BD address.
 *
 *
 * Returns          void
 *
 ******************************************************************************/
uint8_t bta_hh_find_cb(const RawAddress& bda) {
  uint8_t xx;

  /* See how many active devices there are. */
  for (xx = 0; xx < BTA_HH_MAX_DEVICE; xx++) {
    /* check if any active/known devices is a match */
    if ((bda == bta_hh_cb.kdev[xx].addr && !bda.IsEmpty())) {
#if (BTA_HH_DEBUG == TRUE)
      APPL_TRACE_DEBUG("found kdev_cb[%d] hid_handle = %d ", xx,
                       bta_hh_cb.kdev[xx].hid_handle)
#endif
      return xx;
    }
#if (BTA_HH_DEBUG == TRUE)
    else
      APPL_TRACE_DEBUG("in_use ? [%d] kdev[%d].hid_handle = %d state = [%d]",
                       bta_hh_cb.kdev[xx].in_use, xx,
                       bta_hh_cb.kdev[xx].hid_handle, bta_hh_cb.kdev[xx].state);
#endif
  }

  /* if no active device match, find a spot for it */
  for (xx = 0; xx < BTA_HH_MAX_DEVICE; xx++) {
    if (!bta_hh_cb.kdev[xx].in_use) {
      bta_hh_cb.kdev[xx].addr = bda;
      break;
    }
  }
/* If device list full, report BTA_HH_IDX_INVALID */
#if (BTA_HH_DEBUG == TRUE)
  APPL_TRACE_DEBUG("bta_hh_find_cb:: index = %d while max = %d", xx,
                   BTA_HH_MAX_DEVICE);
#endif

  if (xx == BTA_HH_MAX_DEVICE) xx = BTA_HH_IDX_INVALID;

  return xx;
}

/*******************************************************************************
 *
 * Function         bta_hh_clean_up_kdev
 *
 * Description      Clean up device control block when device is removed from
 *                  manitainace list, and update control block index map.
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_hh_clean_up_kdev(tBTA_HH_DEV_CB* p_cb) {
  uint8_t index;

  if (p_cb->hid_handle != BTA_HH_INVALID_HANDLE) {
#if (BTA_HH_LE_INCLUDED == TRUE)
    if (p_cb->is_le_device)
      bta_hh_cb.le_cb_index[BTA_HH_GET_LE_CB_IDX(p_cb->hid_handle)] =
          BTA_HH_IDX_INVALID;
    else if (p_cb->hid_handle < BTA_HH_MAX_KNOWN)
#endif
      bta_hh_cb.cb_index[p_cb->hid_handle] = BTA_HH_IDX_INVALID;
  }

  /* reset device control block */
  index = p_cb->index; /* Preserve index for this control block */

  /* Free buffer for report descriptor info */
  osi_free_and_reset((void**)&p_cb->dscp_info.descriptor.dsc_list);

  memset(p_cb, 0, sizeof(tBTA_HH_DEV_CB)); /* Reset control block */

  p_cb->index = index; /* Restore index for this control block */
  p_cb->state = BTA_HH_IDLE_ST;
  p_cb->hid_handle = BTA_HH_INVALID_HANDLE;
}
/*******************************************************************************
 *
 * Function         bta_hh_update_di_info
 *
 * Description      Maintain a known device list for BTA HH.
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_hh_update_di_info(tBTA_HH_DEV_CB* p_cb, uint16_t vendor_id,
                           uint16_t product_id, uint16_t version,
#if (BTA_HH_LE_INCLUDED == TRUE)
                           uint8_t flag)
#else
                           UNUSED_ATTR uint8_t flag)
#endif
{
#if (BTA_HH_DEBUG == TRUE)
  APPL_TRACE_DEBUG("vendor_id = 0x%2x product_id = 0x%2x version = 0x%2x",
                   vendor_id, product_id, version);
#endif
  p_cb->dscp_info.vendor_id = vendor_id;
  p_cb->dscp_info.product_id = product_id;
  p_cb->dscp_info.version = version;
#if (BTA_HH_LE_INCLUDED == TRUE)
  p_cb->dscp_info.flag = flag;
#endif
}
/*******************************************************************************
 *
 * Function         bta_hh_add_device_to_list
 *
 * Description      Maintain a known device list for BTA HH.
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_hh_add_device_to_list(tBTA_HH_DEV_CB* p_cb, uint8_t handle,
                               uint16_t attr_mask,
                               tHID_DEV_DSCP_INFO* p_dscp_info,
                               uint8_t sub_class, uint16_t ssr_max_latency,
                               uint16_t ssr_min_tout, uint8_t app_id) {
#if (BTA_HH_DEBUG == TRUE)
  APPL_TRACE_DEBUG("subclass = 0x%2x", sub_class);
#endif

  p_cb->hid_handle = handle;
  p_cb->in_use = true;
  p_cb->attr_mask = attr_mask;

  p_cb->sub_class = sub_class;
  p_cb->app_id = app_id;

  p_cb->dscp_info.ssr_max_latency = ssr_max_latency;
  p_cb->dscp_info.ssr_min_tout = ssr_min_tout;
  blacklist_adjust_sniff_subrate(p_cb->addr, &(p_cb->dscp_info.ssr_max_latency));

  /* store report descriptor info */
  if (p_dscp_info) {
    osi_free_and_reset((void**)&p_cb->dscp_info.descriptor.dsc_list);

    if (p_dscp_info->dl_len) {
      p_cb->dscp_info.descriptor.dsc_list =
          (uint8_t*)osi_malloc(p_dscp_info->dl_len);
      p_cb->dscp_info.descriptor.dl_len = p_dscp_info->dl_len;
      memcpy(p_cb->dscp_info.descriptor.dsc_list, p_dscp_info->dsc_list,
             p_dscp_info->dl_len);
    }
  }
}

/*******************************************************************************
 *
 * Function         bta_hh_tod_spt
 *
 * Description      Check to see if this type of device is supported
 *
 * Returns
 *
 ******************************************************************************/
bool bta_hh_tod_spt(tBTA_HH_DEV_CB* p_cb, uint8_t sub_class) {
  uint8_t xx;
  uint8_t cod = (sub_class >> 2); /* lower two bits are reserved */

  for (xx = 0; xx < p_bta_hh_cfg->max_devt_spt; xx++) {
    if (cod == (uint8_t)p_bta_hh_cfg->p_devt_list[xx].tod) {
      p_cb->app_id = p_bta_hh_cfg->p_devt_list[xx].app_id;
#if (BTA_HH_DEBUG == TRUE)
      APPL_TRACE_EVENT("bta_hh_tod_spt sub_class:0x%x supported", sub_class);
#endif
      return true;
    }
  }
#if (BTA_HH_DEBUG == TRUE)
  APPL_TRACE_EVENT("bta_hh_tod_spt sub_class:0x%x NOT supported", sub_class);
#endif
  return false;
}

/*******************************************************************************
 *
 * Function         bta_hh_parse_keybd_rpt
 *
 * Description      This utility function parse a boot mode keyboard report.
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_hh_parse_keybd_rpt(tBTA_HH_BOOT_RPT* p_kb_data, uint8_t* p_report,
                            uint16_t report_len) {
  tBTA_HH_KB_CB* p_kb = &bta_hh_cb.kb_cb;
  tBTA_HH_KEYBD_RPT* p_data = &p_kb_data->data_rpt.keybd_rpt;

  uint8_t this_char, ctl_shift;
  uint16_t xx, yy, key_idx = 0;
  uint8_t this_report[BTA_HH_MAX_RPT_CHARS];

#if (BTA_HH_DEBUG == TRUE)
  APPL_TRACE_DEBUG("bta_hh_parse_keybd_rpt:  (report=%p, report_len=%d) called",
                   p_report, report_len);
#endif

  if (report_len < 2) return;

  ctl_shift = *p_report++;
  report_len--;

  if (report_len > BTA_HH_MAX_RPT_CHARS) report_len = BTA_HH_MAX_RPT_CHARS;

  memset(this_report, 0, BTA_HH_MAX_RPT_CHARS);
  memset(p_data, 0, sizeof(tBTA_HH_KEYBD_RPT));
  memcpy(this_report, p_report, report_len);

  /* Take care of shift, control, GUI and alt, modifier keys  */
  for (xx = 0; xx < BTA_HH_MOD_MAX_KEY; xx++) {
    if (ctl_shift & bta_hh_mod_key_mask[xx]) {
      APPL_TRACE_DEBUG("Mod Key[%02x] pressed", bta_hh_mod_key_mask[xx]);
      p_kb->mod_key[xx] = true;
    } else if (p_kb->mod_key[xx]) {
      p_kb->mod_key[xx] = false;
    }
    /* control key flag is set */
    p_data->mod_key[xx] = p_kb->mod_key[xx];
  }

  /***************************************************************************/
  /*  First step is to remove all characters we saw in the last report       */
  /***************************************************************************/
  for (xx = 0; xx < report_len; xx++) {
    for (yy = 0; yy < BTA_HH_MAX_RPT_CHARS; yy++) {
      if (this_report[xx] == p_kb->last_report[yy]) {
        this_report[xx] = 0;
      }
    }
  }
  /***************************************************************************/
  /*  Now, process all the characters in the report, up to 6 keycodes        */
  /***************************************************************************/
  for (xx = 0; xx < report_len; xx++) {
#if (BTA_HH_DEBUG == TRUE)
    APPL_TRACE_DEBUG("this_char = %02x", this_report[xx]);
#endif
    this_char = this_report[xx];
    if (this_char == 0) continue;
    /* take the key code as the report data */
    if (this_report[xx] == BTA_HH_KB_CAPS_LOCK)
      p_kb->caps_lock = p_kb->caps_lock ? false : true;
    else if (this_report[xx] == BTA_HH_KB_NUM_LOCK)
      p_kb->num_lock = p_kb->num_lock ? false : true;
    else
      p_data->this_char[key_idx++] = this_char;

#if (BTA_HH_DEBUG == TRUE)
    APPL_TRACE_DEBUG("found keycode %02x ", this_report[xx]);
#endif
    p_data->caps_lock = p_kb->caps_lock;
    p_data->num_lock = p_kb->num_lock;
  }

  memset(p_kb->last_report, 0, BTA_HH_MAX_RPT_CHARS);
  memcpy(p_kb->last_report, p_report, report_len);

  return;
}

/*******************************************************************************
 *
 * Function         bta_hh_parse_mice_rpt
 *
 * Description      This utility function parse a boot mode mouse report.
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_hh_parse_mice_rpt(tBTA_HH_BOOT_RPT* p_mice_data, uint8_t* p_report,
                           uint16_t report_len) {
  tBTA_HH_MICE_RPT* p_data = &p_mice_data->data_rpt.mice_rpt;
#if (BTA_HH_DEBUG == TRUE)
  uint8_t xx;

  APPL_TRACE_DEBUG(
      "bta_hh_parse_mice_rpt:  bta_keybd_rpt_rcvd(report=%p, \
                report_len=%d) called",
      p_report, report_len);
#endif

  if (report_len < 3) return;

  if (report_len > BTA_HH_MAX_RPT_CHARS) report_len = BTA_HH_MAX_RPT_CHARS;

#if (BTA_HH_DEBUG == TRUE)
  for (xx = 0; xx < report_len; xx++) {
    APPL_TRACE_DEBUG("this_char = %02x", p_report[xx]);
  }
#endif

  /* only first bytes lower 3 bits valid */
  p_data->mouse_button = (p_report[0] & 0x07);

  /* x displacement */
  p_data->delta_x = p_report[1];

  /* y displacement */
  p_data->delta_y = p_report[2];

#if (BTA_HH_DEBUG == TRUE)
  APPL_TRACE_DEBUG("mice button: 0x%2x", p_data->mouse_button);
  APPL_TRACE_DEBUG("mice move: x = %d y = %d", p_data->delta_x,
                   p_data->delta_y);
#endif

  return;
}

/*******************************************************************************
 *
 * Function         bta_hh_read_ssr_param
 *
 * Description      Read the SSR Parameter for the remote device
 *
 * Returns          tBTA_HH_STATUS  operation status
 *
 ******************************************************************************/
tBTA_HH_STATUS bta_hh_read_ssr_param(const RawAddress& bd_addr,
                                     uint16_t* p_max_ssr_lat,
                                     uint16_t* p_min_ssr_tout) {
  tBTA_HH_STATUS status = BTA_HH_ERR;
  tBTA_HH_CB* p_cb = &bta_hh_cb;
  uint8_t i;
  uint16_t ssr_max_latency;
  char remote_name[BTM_MAX_REM_BD_NAME_LEN] = "";

  for (i = 0; i < BTA_HH_MAX_KNOWN; i++) {
    if (p_cb->kdev[i].addr == bd_addr) {
      /* if remote device does not have HIDSSRHostMaxLatency attribute in SDP,
      set SSR max latency default value here.  */
      if (p_cb->kdev[i].dscp_info.ssr_max_latency == HID_SSR_PARAM_INVALID) {
        /* The default is calculated as half of link supervision timeout.*/

        BTM_GetLinkSuperTout(p_cb->kdev[i].addr, &ssr_max_latency);
        ssr_max_latency = BTA_HH_GET_DEF_SSR_MAX_LAT(ssr_max_latency);

        /* per 1.1 spec, if the newly calculated max latency is greater than
        BTA_HH_SSR_MAX_LATENCY_DEF which is 500ms, use
        BTA_HH_SSR_MAX_LATENCY_DEF */
        if (ssr_max_latency > BTA_HH_SSR_MAX_LATENCY_DEF)
          ssr_max_latency = BTA_HH_SSR_MAX_LATENCY_DEF;

        btif_storage_get_stored_remote_name(bd_addr, remote_name);

        if (interop_match_addr(INTEROP_HID_HOST_LIMIT_SNIFF_INTERVAL, &bd_addr) ||
               interop_match_name(INTEROP_HID_HOST_LIMIT_SNIFF_INTERVAL, remote_name)) {
            APPL_TRACE_WARNING("%s: %s is in INTEROP_HID_HOST_LIMIT_SNIFF_INTERVAL list", __func__, remote_name);
           if (ssr_max_latency > 18 /* slots * 0.625ms */) ssr_max_latency = 18;
        }

        *p_max_ssr_lat = ssr_max_latency;
      } else
        *p_max_ssr_lat = p_cb->kdev[i].dscp_info.ssr_max_latency;

      if (p_cb->kdev[i].dscp_info.ssr_min_tout == HID_SSR_PARAM_INVALID)
        *p_min_ssr_tout = BTA_HH_SSR_MIN_TOUT_DEF;
      else
        *p_min_ssr_tout = p_cb->kdev[i].dscp_info.ssr_min_tout;

      status = BTA_HH_OK;

      break;
    }
  }

  return status;
}

/*******************************************************************************
 *
 * Function         bta_hh_cleanup_disable
 *
 * Description      when disable finished, cleanup control block and send
 *                  callback
 *
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_hh_cleanup_disable(tBTA_HH_STATUS status) {
  uint8_t xx;
  /* free buffer in CB holding report descriptors */
  for (xx = 0; xx < BTA_HH_MAX_DEVICE; xx++) {
    osi_free_and_reset(
        (void**)&bta_hh_cb.kdev[xx].dscp_info.descriptor.dsc_list);
  }

  if (bta_hh_cb.p_disc_db) {
    /* Cancel SDP if it had been started. */
    (void)SDP_CancelServiceSearch (bta_hh_cb.p_disc_db);
    osi_free_and_reset((void**)&bta_hh_cb.p_disc_db);
  }

  if (bta_hh_cb.p_cback) {
    tBTA_HH bta_hh;
    bta_hh.status = status;
    (*bta_hh_cb.p_cback)(BTA_HH_DISABLE_EVT, &bta_hh);
    /* all connections are down, no waiting for diconnect */
    memset(&bta_hh_cb, 0, sizeof(tBTA_HH_CB));
  }
}

/*******************************************************************************
 *
 * Function         bta_hh_dev_handle_to_cb_idx
 *
 * Description      convert a HID device handle to the device control block
 *                  index.
 *
 *
 * Returns          uint8_t: index of the device control block.
 *
 ******************************************************************************/
uint8_t bta_hh_dev_handle_to_cb_idx(uint8_t dev_handle) {
  uint8_t index = BTA_HH_IDX_INVALID;

#if (BTA_HH_LE_INCLUDED == TRUE)
  if (BTA_HH_IS_LE_DEV_HDL(dev_handle)) {
    if (BTA_HH_IS_LE_DEV_HDL_VALID(dev_handle))
      index = bta_hh_cb.le_cb_index[BTA_HH_GET_LE_CB_IDX(dev_handle)];
#if (BTA_HH_DEBUG == TRUE)
    APPL_TRACE_DEBUG("bta_hh_dev_handle_to_cb_idx dev_handle = %d index = %d",
                     dev_handle, index);
#endif
  } else
#endif
      /* regular HID device checking */
      if (dev_handle < BTA_HH_MAX_KNOWN)
    index = bta_hh_cb.cb_index[dev_handle];

  return index;
}
#if (BTA_HH_DEBUG == TRUE)
/*******************************************************************************
 *
 * Function         bta_hh_trace_dev_db
 *
 * Description      Check to see if this type of device is supported
 *
 * Returns
 *
 ******************************************************************************/
void bta_hh_trace_dev_db(void) {
  uint8_t xx;

  APPL_TRACE_DEBUG("bta_hh_trace_dev_db:: Device DB list********************");

  for (xx = 0; xx < BTA_HH_MAX_DEVICE; xx++) {
    APPL_TRACE_DEBUG("kdev[%d] in_use[%d]  handle[%d] ", xx,
                     bta_hh_cb.kdev[xx].in_use, bta_hh_cb.kdev[xx].hid_handle);

    APPL_TRACE_DEBUG(
        "\t\t\t attr_mask[%04x] state [%d] sub_class[%02x] index = %d",
        bta_hh_cb.kdev[xx].attr_mask, bta_hh_cb.kdev[xx].state,
        bta_hh_cb.kdev[xx].sub_class, bta_hh_cb.kdev[xx].index);
  }
  APPL_TRACE_DEBUG("*********************************************************");
}
#endif
#endif /* HL_INCLUDED */
