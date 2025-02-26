/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_DEBUG_H_
#define _DP_DEBUG_H_

#include "dp_panel.h"
#include "dp_ctrl.h"
#include "dp_link.h"
#include "dp_aux.h"
#include "dp_display.h"
#include "dp_pll.h"
#include <linux/ipc_logging.h>

#define DP_IPC_LOG(fmt, ...) \
	do {  \
		void *ipc_logging_context = get_ipc_log_context(); \
		ipc_log_string(ipc_logging_context, fmt, ##__VA_ARGS__); \
	} while (0)

#define DP_DEBUG(fmt, ...)                                                   \
	do {                                                                 \
		DP_IPC_LOG("[d][%-4d]"fmt, current->pid, ##__VA_ARGS__); \
		DP_DEBUG_V(fmt, ##__VA_ARGS__); \
	} while (0)

#define DP_INFO(fmt, ...)                                                   \
	do {                                                                 \
		DP_IPC_LOG("[i][%-4d]"fmt, current->pid, ##__VA_ARGS__); \
		DP_INFO_V(fmt, ##__VA_ARGS__); \
	} while (0)

#define DP_WARN(fmt, ...)                                                   \
	do {                                                                 \
		DP_IPC_LOG("[w][%-4d]"fmt, current->pid, ##__VA_ARGS__); \
		DP_WARN_V(fmt, ##__VA_ARGS__); \
	} while (0)

#define DP_ERR(fmt, ...)                                                   \
	do {                                                                 \
		DP_IPC_LOG("[e][%-4d]"fmt, current->pid, ##__VA_ARGS__); \
		DP_ERR_V(fmt, ##__VA_ARGS__); \
	} while (0)

#define DP_DEBUG_V(fmt, ...) \
	do { \
		if (drm_debug_enabled(DRM_UT_KMS))                        \
			DRM_DEBUG("[msm-dp-debug][%-4d]"fmt, current->pid,   \
					##__VA_ARGS__);                      \
		else                                                         \
			pr_debug("[drm:%s][msm-dp-debug][%-4d]"fmt, __func__,\
				       current->pid, ##__VA_ARGS__);         \
	} while (0)

#define DP_INFO_V(fmt, ...)                                                    \
	do {                                                                 \
		if (drm_debug_enabled(DRM_UT_KMS))                        \
			DRM_INFO("[msm-dp-info][%-4d]"fmt, current->pid,    \
					##__VA_ARGS__);                      \
		else                                                         \
			pr_info("[DP][drm:%s][msm-dp-info][%-4d]"fmt, __func__, \
				       current->pid, ##__VA_ARGS__);         \
	} while (0)

#define DP_WARN_V(fmt, ...)                                    \
		pr_warn("[DP][drm:%s][msm-dp-warn][%-4d]"fmt, __func__,  \
				current->pid, ##__VA_ARGS__)

#define DP_ERR_V(fmt, ...)                                    \
		pr_err("[DP][drm:%s][msm-dp-err][%-4d]"fmt, __func__,   \
				current->pid, ##__VA_ARGS__)

#define DP_LOG(fmt, ...)                                    \
    pr_err("[DP][msm-dp:%s] "fmt, __func__, ##__VA_ARGS__)

#define DEFAULT_DISCONNECT_DELAY_MS 0
#define MAX_DISCONNECT_DELAY_MS 10000
#define DEFAULT_CONNECT_NOTIFICATION_DELAY_MS 150
#define MAX_CONNECT_NOTIFICATION_DELAY_MS 5000

/**
 * struct dp_debug
 * @sim_mode: specifies whether sim mode enabled
 * @psm_enabled: specifies whether psm enabled
 * @hdcp_disabled: specifies if hdcp is disabled
 * @hdcp_wait_sink_sync: used to wait for sink synchronization before HDCP auth
 * @tpg_pattern: selects tpg pattern on the controller
 * @max_pclk_khz: max pclk supported
 * @force_encryption: enable/disable forced encryption for HDCP 2.2
 * @skip_uevent: skip hotplug uevent to the user space
 * @hdcp_status: string holding hdcp status information
 * @mst_sim_add_con: specifies whether new sim connector is to be added
 * @mst_sim_remove_con: specifies whether sim connector is to be removed
 * @mst_sim_remove_con_id: specifies id of sim connector to be removed
 * @connect_notification_delay_ms: time (in ms) to wait for any attention
 *              messages before sending the connect notification uevent
 * @disconnect_delay_ms: time (in ms) to wait before turning off the mainlink
 *              in response to HPD low of cable disconnect event
 */
struct dp_debug {
	bool sim_mode;
	bool psm_enabled;
	bool hdcp_disabled;
	bool hdcp_wait_sink_sync;
	u32 tpg_pattern;
	u32 max_pclk_khz;
	bool force_encryption;
	bool skip_uevent;
	char hdcp_status[SZ_128];
	bool mst_sim_add_con;
	bool mst_sim_remove_con;
	int mst_sim_remove_con_id;
	unsigned long connect_notification_delay_ms;
	u32 disconnect_delay_ms;

	void (*abort)(struct dp_debug *dp_debug);
	void (*set_mst_con)(struct dp_debug *dp_debug, int con_id);

	bool aux_err;
	bool swing_dbg_en;
	bool pre_emp_dbg_en;
	bool pre_emp_108_dbg_en;
};

/**
 * struct dp_debug_in
 * @dev: device instance of the caller
 * @panel: instance of panel module
 * @hpd: instance of hpd module
 * @link: instance of link module
 * @aux: instance of aux module
 * @connector: double pointer to display connector
 * @catalog: instance of catalog module
 * @parser: instance of parser module
 * @ctrl: instance of controller module
 * @pll: instance of pll module
 * @display: instance of display module
 */
struct dp_debug_in {
	struct device *dev;
	struct dp_panel *panel;
	struct dp_hpd *hpd;
	struct dp_link *link;
	struct dp_aux *aux;
	struct drm_connector **connector;
	struct dp_catalog *catalog;
	struct dp_parser *parser;
	struct dp_ctrl *ctrl;
	struct dp_pll *pll;
	struct dp_display *display;
};

/**
 * dp_debug_get() - configure and get the DisplayPlot debug module data
 *
 * @in: input structure containing data to initialize the debug module
 * return: pointer to allocated debug module data
 *
 * This function sets up the debug module and provides a way
 * for debugfs input to be communicated with existing modules
 */
struct dp_debug *dp_debug_get(struct dp_debug_in *in);

/**
 * dp_debug_put()
 *
 * Cleans up dp_debug instance
 *
 * @dp_debug: instance of dp_debug
 */
void dp_debug_put(struct dp_debug *dp_debug);
#endif /* _DP_DEBUG_H_ */
