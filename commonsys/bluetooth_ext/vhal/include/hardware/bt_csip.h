/******************************************************************************
 *  Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *      * Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *      * Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials provided
 *        with the distribution.
 *      * Neither the name of The Linux Foundation nor the names of its
 *        contributors may be used to endorse or promote products derived
 *        from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 *  BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *  WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 *  OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 *  IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/
 /******************************************************************************
 *  Changes from Qualcomm Innovation Center are provided under the following license:

 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *      * Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *
 *      * Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials provided
 *        with the distribution.
 *
 *      * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *        contributors may be used to endorse or promote products derived
 *        from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE

*****************************************************************************/

#ifndef ANDROID_INCLUDE_BT_CSIP_H
#define ANDROID_INCLUDE_BT_CSIP_H

#include <stdint.h>
#include <bluetooth/uuid.h>
#include <vector>

__BEGIN_DECLS

#define BT_PROFILE_CSIP_CLIENT_ID "csip_client"

/** Callback when app has registered with CSIP Client module
 */
typedef void (* btcsip_csip_app_registered_callback)(uint8_t status, uint8_t app_id,
                                                     const bluetooth::Uuid& app_uuid);

/** Callback when connection state is changed for CSIP Profile
 */
typedef void (* btcsip_csip_connection_state_callback)(uint8_t app_id, RawAddress& bd_addr,
                                                       uint8_t state, uint8_t status);

/** Callback when new set has been identified on remote device
 */
typedef void (* btcsip_new_set_found_callback) (uint8_t set_id, RawAddress& bd_addr,
                                                uint8_t size, uint8_t* sirk,
                                                const bluetooth::Uuid& p_srvc_uuid,
                                                bool lock_support);

/** Callback when new set member has been identified
 */
typedef void (* btcsip_new_set_member_found_callback) (uint8_t set_id,
                                                       RawAddress& bd_addr);

/** Callback for lock status changed event to requesting client
 */
typedef void (* btcsip_lock_state_changed_callback) (uint8_t app_id, uint8_t set_id,
                                                     uint8_t value, uint8_t status,
                                                     std::vector<RawAddress> addr);

/** Callback when lock is available on earlier denying set member
 */
typedef void (* btcsip_lock_available_callback) (uint8_t app_id, uint8_t set_id,
                                                 RawAddress& bd_addr);

/** Callback when size of coordinated set has been changed
 */
typedef void (* btcsip_set_size_changed_callback) (uint8_t set_id, uint8_t size,
                                                   RawAddress& bd_addr);

/** Callback when SIRK of coordinated set has been changed
 */
typedef void (* btcsip_set_sirk_changed_callback) (uint8_t set_id, uint8_t* sirk,
                                                   RawAddress& bd_addr);

/** Callback when RSI data of coordinated set has been found
 */
typedef void (* btcsip_on_rsi_data_found) (uint8_t* rsi,
                                                   const RawAddress& bd_addr);

/** BT-CSIP callback structure. */
typedef struct {
    size_t size;
    btcsip_csip_app_registered_callback   app_registered_cb;
    btcsip_csip_connection_state_callback conn_state_cb;
    btcsip_new_set_found_callback         new_set_found_cb;
    btcsip_new_set_member_found_callback  new_set_member_cb;
    btcsip_lock_state_changed_callback    lock_status_cb;
    btcsip_lock_available_callback        lock_available_cb;
    btcsip_set_size_changed_callback      size_changed_cb;
    btcsip_set_sirk_changed_callback      sirk_changed_cb;
    btcsip_on_rsi_data_found              rsi_data_found_cb;
} btcsip_callbacks_t;

/** Represents the standard BT-CSIP interface. */
typedef struct {

    /** set to sizeof(BtCsipInterface) */
    size_t size;

    /** Register the BtCsipInterface callbacks
     */
    bt_status_t (*init) (btcsip_callbacks_t* callbacks);

    /** CSIP opportunistic gatt client connection*/
    bt_status_t (*connect) (uint8_t app_id, RawAddress *bd_addr);

    /** disconnect csip gatt connection */
    bt_status_t (*disconnect) (uint8_t app_id, RawAddress *bd_addr );

    /** register app/module with CSIP profile*/
    bt_status_t (*register_csip_app) (const bluetooth::Uuid& app_uuid);

    /** unregister app/module with CSIP profile */
    bt_status_t (*unregister_csip_app) (uint8_t app_id);

    /** change lock value */
    bt_status_t (*set_lock_value) (uint8_t app_id, uint8_t set_id, uint8_t lock_value,
                                  std::vector<RawAddress> devices);

    void (*set_opportunistic_scan) (bool is_start);

    /** Closes the interface. */
    void  (*cleanup) (void);

} btcsip_interface_t;
__END_DECLS

#endif /* ANDROID_INCLUDE_BT_CSIP_H */
