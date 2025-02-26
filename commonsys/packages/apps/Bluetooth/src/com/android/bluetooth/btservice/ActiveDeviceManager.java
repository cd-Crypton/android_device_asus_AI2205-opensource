/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.bluetooth.btservice;
import static android.Manifest.permission.BLUETOOTH_CONNECT;

import android.annotation.RequiresPermission;
import android.annotation.SuppressLint;
import android.bluetooth.BluetoothA2dp;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothHeadset;
import android.bluetooth.BluetoothHearingAid;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.BluetoothLeAudio;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.Message;
import android.util.Log;
import com.android.bluetooth.a2dp.A2dpService;

import com.android.bluetooth.apm.ApmConstIntf;
import com.android.bluetooth.apm.ActiveDeviceManagerServiceIntf;
import com.android.bluetooth.apm.CallAudioIntf;

import com.android.bluetooth.hearingaid.HearingAidService;
import com.android.bluetooth.hfp.HeadsetService;
import com.android.bluetooth.ba.BATService;
import com.android.internal.annotations.VisibleForTesting;
import com.android.bluetooth.le_audio.LeAudioService;
import com.android.bluetooth.CsipWrapper;
import com.android.bluetooth.Utils;

import java.lang.reflect.*;

import java.util.LinkedList;
import java.util.List;
import java.util.Objects;

/**
 * The active device manager is responsible for keeping track of the
 * connected A2DP/HFP/AVRCP/HearingAid devices and select which device is
 * active (for each profile).
 *
 * Current policy (subject to change):
 * 1) If the maximum number of connected devices is one, the manager doesn't
 *    do anything. Each profile is responsible for automatically selecting
 *    the connected device as active. Only if the maximum number of connected
 *    devices is more than one, the rules below will apply.
 * 2) The selected A2DP active device is the one used for AVRCP as well.
 * 3) The HFP active device might be different from the A2DP active device.
 * 4) The Active Device Manager always listens for ACTION_ACTIVE_DEVICE_CHANGED
 *    broadcasts for each profile:
 *    - BluetoothA2dp.ACTION_ACTIVE_DEVICE_CHANGED for A2DP
 *    - BluetoothHeadset.ACTION_ACTIVE_DEVICE_CHANGED for HFP
 *    - BluetoothHearingAid.ACTION_ACTIVE_DEVICE_CHANGED for HearingAid
 *    If such broadcast is received (e.g., triggered indirectly by user
 *    action on the UI), the device in the received broacast is marked
 *    as the current active device for that profile.
 * 5) If there is a HearingAid active device, then A2DP and HFP active devices
 *    must be set to null (i.e., A2DP and HFP cannot have active devices).
 *    The reason is because A2DP or HFP cannot be used together with HearingAid.
 * 6) If there are no connected devices (e.g., during startup, or after all
 *    devices have been disconnected, the active device per profile
 *    (A2DP/HFP/HearingAid) is selected as follows:
 * 6.1) The last connected HearingAid device is selected as active.
 *      If there is an active A2DP or HFP device, those must be set to null.
 * 6.2) The last connected A2DP or HFP device is selected as active.
 *      However, if there is an active HearingAid device, then the
 *      A2DP or HFP active device is not set (must remain null).
 * 7) If the currently active device (per profile) is disconnected, the
 *    Active Device Manager just marks that the profile has no active device,
 *    but does not attempt to select a new one. Currently, the expectation is
 *    that the user will explicitly select the new active device.
 * 8) If there is already an active device, and the corresponding
 *    ACTION_ACTIVE_DEVICE_CHANGED broadcast is received, the device
 *    contained in the broadcast is marked as active. However, if
 *    the contained device is null, the corresponding profile is marked
 *    as having no active device.
 * 9) If a wired audio device is connected, the audio output is switched
 *    by the Audio Framework itself to that device. We detect this here,
 *    and the active device for each profile (A2DP/HFP/HearingAid) is set
 *    to null to reflect the output device state change. However, if the
 *    wired audio device is disconnected, we don't do anything explicit
 *    and apply the default behavior instead:
 * 9.1) If the wired headset is still the selected output device (i.e. the
 *      active device is set to null), the Phone itself will become the output
 *      device (i.e., the active device will remain null). If music was
 *      playing, it will stop.
 * 9.2) If one of the Bluetooth devices is the selected active device
 *      (e.g., by the user in the UI), disconnecting the wired audio device
 *      will have no impact. E.g., music will continue streaming over the
 *      active Bluetooth device.
 */
public class ActiveDeviceManager {
    private static final boolean DBG = true;
    private static final String TAG = "BluetoothActiveDeviceManager";

    // Message types for the handler
    private static final int MESSAGE_ADAPTER_ACTION_STATE_CHANGED = 1;
    private static final int MESSAGE_A2DP_ACTION_CONNECTION_STATE_CHANGED = 2;
    private static final int MESSAGE_A2DP_ACTION_ACTIVE_DEVICE_CHANGED = 3;
    private static final int MESSAGE_HFP_ACTION_CONNECTION_STATE_CHANGED = 4;
    private static final int MESSAGE_HFP_ACTION_ACTIVE_DEVICE_CHANGED = 5;
    private static final int MESSAGE_HEARING_AID_ACTION_ACTIVE_DEVICE_CHANGED = 6;
    private static final int MESSAGE_BAP_BROADCAST_ACTIVE_DEVICE_CHANGED = 7;
    private static final int MESSAGE_LE_AUDIO_ACTION_CONNECTION_STATE_CHANGED = 8;
    private static final int MESSAGE_LE_AUDIO_ACTION_ACTIVE_DEVICE_CHANGED = 9;
    private static final int MESSAGE_A2DP_ACTION_CONNECTION_STATE_CHANGED_DELAYED = 10;
    private static final int NO_ACTIVE_MEDIA_DISCONNECTION_EVENT_DELAY_MS = 100;

    private static final int Profile_HFP = 1;
    private static final int Profile_A2DP = 2;

    // Invalid CSIP Coodinator Set Id
    private static final int INVALID_SET_ID = 0x10;

    private final AdapterService mAdapterService;
    private final ServiceFactory mFactory;
    private HandlerThread mHandlerThread = null;
    private Handler mHandler = null;
    private final AudioManager mAudioManager;
    private final AudioManagerAudioDeviceCallback mAudioManagerAudioDeviceCallback;

    private final List<BluetoothDevice> mA2dpConnectedDevices = new LinkedList<>();
    private final List<BluetoothDevice> mHfpConnectedDevices = new LinkedList<>();
    /// Auto set Active device(Hfp, A2dp) if current active device disconnected (Android T)
    private final List<BluetoothDevice> mA2dpActiveDevice_History = new LinkedList<>();
    private final List<BluetoothDevice> mHfpActiveDevice_History = new LinkedList<>();

    private BluetoothDevice mA2dpActiveDevice = null;
    private BluetoothDevice mHfpActiveDevice = null;
    private BluetoothDevice mHearingAidActiveDevice = null;
    private BluetoothDevice mLeAudioActiveDevice = null;
    private boolean mTwsPlusSwitch = false;

    Object mBroadcastService = null;
    Method mBroadcastIsActive = null;
    Method mBroadcastNotifyState = null;
    Method mBroadcastGetAddr = null;
    Method mBroadcastDevice = null;

    // Broadcast receiver for all changes
    private final BroadcastReceiver mReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (action == null) {
                Log.e(TAG, "Received intent with null action");
                return;
            }

            Log.d(TAG, "onReceive(): action: " + action);
            switch (action) {
                case BluetoothAdapter.ACTION_STATE_CHANGED:
                    mHandler.obtainMessage(MESSAGE_ADAPTER_ACTION_STATE_CHANGED,
                                           intent).sendToTarget();
                    break;
                case BluetoothA2dp.ACTION_CONNECTION_STATE_CHANGED:
                    int prevState = intent.getIntExtra(BluetoothProfile.EXTRA_PREVIOUS_STATE, -1);
                    if (prevState == BluetoothProfile.STATE_CONNECTED) {
                        if (mA2dpActiveDevice == null && mLeAudioActiveDevice == null) {
                            // if receive disconnection event but no media device is active
                            // maybe the previous setActive was still in process, delay some time
                            // or the disconnection event will be ignored and the active device will not be removed
                            Log.d(TAG, "Receive disconnection but no media device active, delay 100 ms");
                            mHandler.sendMessageDelayed(
                                mHandler.obtainMessage(MESSAGE_A2DP_ACTION_CONNECTION_STATE_CHANGED_DELAYED, intent),
                                NO_ACTIVE_MEDIA_DISCONNECTION_EVENT_DELAY_MS);
                            break;
                        }
                    }
                    mHandler.obtainMessage(MESSAGE_A2DP_ACTION_CONNECTION_STATE_CHANGED,
                                           intent).sendToTarget();
                    break;
                case BluetoothA2dp.ACTION_ACTIVE_DEVICE_CHANGED:
                    if(!ApmConstIntf.getQtiLeAudioEnabled()) {
                        mHandler.obtainMessage(MESSAGE_A2DP_ACTION_ACTIVE_DEVICE_CHANGED,
                                           intent).sendToTarget();
                    }
                    break;
                case BluetoothHeadset.ACTION_CONNECTION_STATE_CHANGED:
                    mHandler.obtainMessage(MESSAGE_HFP_ACTION_CONNECTION_STATE_CHANGED,
                                           intent).sendToTarget();
                    break;
                case BluetoothHeadset.ACTION_ACTIVE_DEVICE_CHANGED:
                    if(!ApmConstIntf.getQtiLeAudioEnabled()) {
                        mHandler.obtainMessage(MESSAGE_HFP_ACTION_ACTIVE_DEVICE_CHANGED,
                                           intent).sendToTarget();
                    }
                    break;
                case BluetoothHearingAid.ACTION_ACTIVE_DEVICE_CHANGED:
                    mHandler.obtainMessage(MESSAGE_HEARING_AID_ACTION_ACTIVE_DEVICE_CHANGED,
                            intent).sendToTarget();
                    break;
                case BluetoothLeAudio.ACTION_LE_AUDIO_CONNECTION_STATE_CHANGED:
                    mHandler.obtainMessage(MESSAGE_LE_AUDIO_ACTION_CONNECTION_STATE_CHANGED,
                                           intent).sendToTarget();
                    break;
                case BluetoothLeAudio.ACTION_LE_AUDIO_ACTIVE_DEVICE_CHANGED:
                    mHandler.obtainMessage(MESSAGE_LE_AUDIO_ACTION_ACTIVE_DEVICE_CHANGED,
                                           intent).sendToTarget();
                    break;
                default:
                    Log.e(TAG, "Received unexpected intent, action=" + action);
                    break;
            }
        }
    };

    class ActiveDeviceManagerHandler extends Handler {
        ActiveDeviceManagerHandler(Looper looper) {
            super(looper);
        }

        @Override
        public void handleMessage(Message msg) {
            switch (msg.what) {
                case MESSAGE_ADAPTER_ACTION_STATE_CHANGED: {
                    Intent intent = (Intent) msg.obj;
                    int newState = intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, -1);
                    if (DBG) {
                        Log.d(TAG,
                          "handleMessage(MESSAGE_ADAPTER_ACTION_STATE_CHANGED): newState="
                             + newState);
                    }
                    if (newState == BluetoothAdapter.STATE_ON) {
                        resetState();
                    }
                } break;

                case MESSAGE_LE_AUDIO_ACTION_CONNECTION_STATE_CHANGED:{
                    Intent intent = (Intent) msg.obj;
                    BluetoothDevice device =
                            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);

                    int prevState = intent.getIntExtra(BluetoothProfile.EXTRA_PREVIOUS_STATE, -1);
                    int nextState = intent.getIntExtra(BluetoothProfile.EXTRA_STATE, -1);
                    if (prevState == nextState) {
                        // Nothing has changed
                        break;
                    }

                    if (nextState == BluetoothProfile.STATE_CONNECTED) {
                        // Device connected
                        if (DBG) {
                            Log.d(TAG,
                             "handleMessage(MESSAGE_LE_AUDIO_ACTION_CONNECTION_STATE_CHANGED):"
                             + " device " + device + " connected");
                        }

                        //setHfpActiveDevice(null);
                        //setA2dpActiveDevice(null);
                        setLeAudioActiveDevice(device);
                        break;
                    }

                    if (prevState == BluetoothProfile.STATE_CONNECTED) {
                        // Device disconnected
                        if (DBG) {
                            Log.d(TAG,
                             "handleMessage(MESSAGE_LE_AUDIO_ACTION_CONNECTION_STATE_CHANGED):"
                             + " device " + device + " disconnected");
                        }
                        int mMediaProfile =
                            getCurrentActiveProfile(ApmConstIntf.AudioFeatures.MEDIA_AUDIO);
                        if (mMediaProfile == ApmConstIntf.AudioProfiles.A2DP) {
                           if (DBG) {
                              Log.d(TAG, "cuurent active profile is A2DP"
                              + "Not setting active device null for LEAUDIO");
                            } break;
                        } else {
                           if (DBG) {
                              Log.d(TAG, "BAP_MEDIA Profile is active");
                            }
                        }

                        if (Objects.equals(mLeAudioActiveDevice, device)) {
                            final LeAudioService leAudioService = mFactory.getLeAudioService();
                            int groupId = leAudioService.getGroupId(device);
                            BluetoothDevice peerLeAudioDevice = null;
                            if (groupId != BluetoothLeAudio.GROUP_ID_INVALID &&
                                    groupId != INVALID_SET_ID) {
                                List<BluetoothDevice> leAudioConnectedDevice =
                                        leAudioService.getConnectedDevices();
                                for (BluetoothDevice peerDevice : leAudioConnectedDevice) {
                                    if (leAudioService.getGroupId(peerDevice) == groupId) {
                                        peerLeAudioDevice = peerDevice;
                                        break;
                                    }
                                }
                            }
                            if (peerLeAudioDevice != null) {
                                Log.w(TAG, "Set leAudio active device to connected peer device");
                                if (!setLeAudioActiveDevice(peerLeAudioDevice)) {
                                    Log.w(TAG, "Set leAudio active device failed");
                                    setLeAudioActiveDevice(null);
                                }
                            } else {
                                Log.w(TAG, "Set leAudio active device to null");
                                setLeAudioActiveDevice(null);
                            }
                        }
                    }
                } break;

                case MESSAGE_A2DP_ACTION_CONNECTION_STATE_CHANGED_DELAYED:
                case MESSAGE_A2DP_ACTION_CONNECTION_STATE_CHANGED: {
                    Intent intent = (Intent) msg.obj;
                    BluetoothDevice device =
                            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                    if (device.getAddress().equals(BATService.mBAAddress)) {
                        Log.d(TAG," Update from BA, bail out");
                        break;
                    }

                    int prevState = intent.getIntExtra(BluetoothProfile.EXTRA_PREVIOUS_STATE, -1);
                    int nextState = intent.getIntExtra(BluetoothProfile.EXTRA_STATE, -1);
                    if (prevState == nextState) {
                        // Nothing has changed
                        break;
                    }

                    if (nextState == BluetoothProfile.STATE_CONNECTED) {
                        // Device connected
                        if (DBG) {
                            Log.d(TAG,
                               "handleMessage(MESSAGE_A2DP_ACTION_CONNECTION_STATE_CHANGED):"
                               + " device " + device + " connected");
                        }

                        if (mA2dpConnectedDevices.contains(device)) {
                            break;      // The device is already connected
                        }

                        mA2dpConnectedDevices.add(device);
                        if (mHearingAidActiveDevice == null) {
                            // New connected device: select it as active
                            setA2dpActiveDevice(device);
                            break;
                        } else {
                            if (!ApmConstIntf.getQtiLeAudioEnabled()) {
                               setHearingAidActiveDevice(null);
                               setA2dpActiveDevice(device);
                            }
                        }
                        break;
                    }

                    if (prevState == BluetoothProfile.STATE_CONNECTED) {
                        // Device disconnected
                        if (DBG) {
                            Log.d(TAG,
                                "handleMessage(MESSAGE_A2DP_ACTION_CONNECTION_STATE_CHANGED):"
                                + " device " + device + " disconnected");
                        }

                        mA2dpConnectedDevices.remove(device);
                        mA2dpActiveDevice_History.remove(device);    ///also remove device from active list
                        if (ApmConstIntf.getAospLeaEnabled()) {
                           int mMediaProfile =
                               getCurrentActiveProfile(ApmConstIntf.AudioFeatures.MEDIA_AUDIO);
                           if (mMediaProfile == ApmConstIntf.AudioProfiles.A2DP) {
                              if (DBG) {
                                 Log.d(TAG, "current active profile is A2DP");
                               }
                           } else {
                              if (DBG) {
                                 Log.d(TAG, "BAP_MEDIA Profile is active"
                                 + "Not setting active device null A2DP");
                              } break;
                           }
                        }
                        if (Objects.equals(mA2dpActiveDevice, device)) {
                            final A2dpService mA2dpService = mFactory.getA2dpService();
                            BluetoothDevice mDevice = null;
                            if (mAdapterService.isTwsPlusDevice(device) && !mTwsPlusSwitch &&
                                !mA2dpConnectedDevices.isEmpty()) {
                                for (BluetoothDevice connected_device: mA2dpConnectedDevices) {
                                    if (mAdapterService.isTwsPlusDevice(connected_device) &&
                                        mA2dpService.getConnectionState(connected_device) ==
                                        BluetoothProfile.STATE_CONNECTED) {
                                        mDevice = connected_device;
                                        break;
                                    }
                                }
                            } else if (device.isTwsPlusDevice() && mTwsPlusSwitch) {
                                Log.d(TAG, "Resetting mTwsPlusSwitch");
                                mTwsPlusSwitch = false;
                            }
                            if (!setA2dpActiveDevice(mDevice) && (mDevice != null) &&
                                mAdapterService.isTwsPlusDevice(mDevice)) {
                                Log.w(TAG, "Switch A2dp active device to peer earbud failed");
                                setA2dpActiveDevice(null);
                            }
                        }
                        /*ASUS_BSP+++ Yunchung "Auto set Active device(Hfp, A2dp) if current active device disconnected" (Android T) */
                        final A2dpService mA2dpService = mFactory.getA2dpService();
                        /// if current active is null or equal to current active device
                        if (mA2dpService == null) {
                            Log.w(TAG, "no mA2dpService, FATAL");
                            return;
                        }
                        if (!mA2dpConnectedDevices.isEmpty() && (mA2dpActiveDevice == null || Objects.equals(mA2dpActiveDevice, device))) {
                            /// try to get last active device
                            if ((mA2dpService.getConnectionState(getLastActiveDevice(Profile_A2DP)) == BluetoothProfile.STATE_CONNECTED) && setA2dpActiveDevice(getLastActiveDevice(Profile_A2DP))) {
                                Log.d(TAG, "Auto set A2dp active device to " + getLastActiveDevice(Profile_A2DP).toString());
                            } else {
                                Log.d(TAG, "Auto set Last active device fail, set A2dp active to null");
                                setA2dpActiveDevice(null);
                            }
                        }
                        /*ASUS_BSP--- Yunchung "Auto set Active device(Hfp, A2dp) if current active device disconnected"'*/
                    }
                } break;

                case MESSAGE_LE_AUDIO_ACTION_ACTIVE_DEVICE_CHANGED:{
                    Intent intent = (Intent) msg.obj;
                    BluetoothDevice device =
                            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                    if (DBG) {
                        Log.d(TAG,
                           "handleMessage(MESSAGE_LE_AUDIO_ACTION_ACTIVE_DEVICE_CHANGED): "
                                + "device= " + device);
                    }

                    // Just assign locally the new value
                    if (device != null && !Objects.equals(mLeAudioActiveDevice, device)) {
                        //Made this chnage as latest.
                        //setA2dpActiveDevice(null);
                        //setHfpActiveDevice(null);
                        setHearingAidActiveDevice(null);
                    }
                    mLeAudioActiveDevice = device;
                } break;

                case MESSAGE_A2DP_ACTION_ACTIVE_DEVICE_CHANGED: {
                    Intent intent = (Intent) msg.obj;
                    BluetoothDevice device =
                            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                    if (DBG) {
                        Log.d(TAG,
                           "handleMessage(MESSAGE_A2DP_ACTION_ACTIVE_DEVICE_CHANGED): "
                                + "device= " + device);
                    }

                    boolean is_broadcast_active = false;
                    String broadcastBDA = null;
                    if (device != null &&
                        mBroadcastService != null &&
                        mBroadcastIsActive != null &&
                        mBroadcastGetAddr != null) {
                        try {
                            is_broadcast_active =
                                  (boolean) mBroadcastIsActive.invoke(mBroadcastService);
                        } catch(IllegalAccessException e) {
                            Log.e(TAG, "Broadcast:IsActive IllegalAccessException");
                        } catch (InvocationTargetException e) {
                            Log.e(TAG, "Broadcast:IsActive InvocationTargetException");
                        }
                        try {
                            broadcastBDA = (String) mBroadcastGetAddr.invoke(mBroadcastService);
                        } catch(IllegalAccessException e) {
                            Log.e(TAG, "Broadcast:GetAddr IllegalAccessException");
                        } catch (InvocationTargetException e) {
                            Log.e(TAG, "Broadcast:GetAddr InvocationTargetException");
                        }
                    }
                    if (is_broadcast_active && device != null && broadcastBDA != null) {
                        if (device.getAddress().equals(broadcastBDA)) {
                            Log.d(TAG," Update from BA, bail out");
                            break;
                        }
                    }

                    if (device != null && !Objects.equals(mA2dpActiveDevice, device)) {
                        setHearingAidActiveDevice(null);
                        //if(!ApmConstIntf.getQtiLeAudioEnabled()) {
                        //  setLeAudioActiveDevice(null);
                        //}
                    }

                    // Just assign locally the new value
                    if (DBG) {
                        Log.d(TAG, "set mA2dpActiveDevice to " + device);
                    }
                    mA2dpActiveDevice = device;
                    /*ASUS_BSP+++ Yunchung "Auto set Active device(Hfp, A2dp) if current active device disconnected"' (Android S)*/
                    if (mA2dpActiveDevice !=null){
                        if (mA2dpActiveDevice_History.contains(mA2dpActiveDevice)) {
                            Log.d(TAG, "Target A2dp Device already in active list History, change sequence");
                            mA2dpActiveDevice_History.remove(mA2dpActiveDevice);
                            mA2dpActiveDevice_History.add(mA2dpActiveDevice);
                        } else{
                            Log.d(TAG, "New A2dp Active Device. Add in active list History");
                            mA2dpActiveDevice_History.add(mA2dpActiveDevice);
                        }
                    }
                    /*ASUS_BSP--- Yunchung "Auto set Active device(Hfp, A2dp) if current active device disconnected"' (Android T)*/
                } break;

                case MESSAGE_HFP_ACTION_CONNECTION_STATE_CHANGED: {
                    Intent intent = (Intent) msg.obj;
                    BluetoothDevice device =
                            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                    int prevState = intent.getIntExtra(BluetoothProfile.EXTRA_PREVIOUS_STATE, -1);
                    int nextState = intent.getIntExtra(BluetoothProfile.EXTRA_STATE, -1);
                    if (prevState == nextState) {
                        // Nothing has changed
                        break;
                    }
                    if (nextState == BluetoothProfile.STATE_CONNECTED) {
                        // Device connected
                        if (DBG) {
                            Log.d(TAG,
                               "handleMessage(MESSAGE_HFP_ACTION_CONNECTION_STATE_CHANGED): "
                               + "device " + device + " connected");
                        }
                        if (mHfpConnectedDevices.contains(device)) {
                            break;      // The device is already connected
                        }
                        mHfpConnectedDevices.add(device);
                        if (mHearingAidActiveDevice == null) {
                            // New connected device: select it as active
                            setHfpActiveDevice(device);
                            /// Re-check mHfpActiveDevice_History contains the target device, timing issue
                            if (device != null && Objects.equals(mHfpActiveDevice, device) && !mHfpConnectedDevices.contains(device)){
                                Log.d(TAG, "Target HFP Device not in active list History, re-add it");
                                mHfpActiveDevice_History.add(mHfpActiveDevice);
                            }
                            break;
                        }
                        break;
                    }
                    if (prevState == BluetoothProfile.STATE_CONNECTED) {
                        // Device disconnected
                        if (DBG) {
                            Log.d(TAG,
                               "handleMessage(MESSAGE_HFP_ACTION_CONNECTION_STATE_CHANGED): "
                               + "device " + device + " disconnected");
                        }
                        final HeadsetService hfpService = mFactory.getHeadsetService();

                        mHfpConnectedDevices.remove(device);
                        /*ASUS_BSP+++ Yunchung "Auto set Active device(Hfp, A2dp) if current active device disconnected"'*/
                        mHfpActiveDevice_History.remove(device);
                        if (ApmConstIntf.getAospLeaEnabled()) {
                           int mCallProfile =
                               getCurrentActiveProfile(ApmConstIntf.AudioFeatures.CALL_AUDIO);
                           if (mCallProfile == ApmConstIntf.AudioProfiles.HFP) {
                              if (DBG) {
                                 Log.d(TAG, "cuurent active profile is HFP");
                              }
                           } else {
                              if (DBG) {
                                 Log.d(TAG, "BAP_CALL Profile is active"
                                 + "Not setting active device null HFP");
                              } break;
                           }
                        } else if (ApmConstIntf.getQtiLeAudioEnabled()) {
                            int groupId = getGroupId(device);
                            int activeGroupId = INVALID_SET_ID;
                            BluetoothDevice peerHfpDevice = null;
                            boolean isGrpDevice = false;

                            if (hfpService == null) {
                                    Log.e(TAG, "no headsetService, FATAL");
                                    return;
                            }

                            if (mHfpActiveDevice != null &&
                                mHfpActiveDevice.getAddress().contains(ApmConstIntf.groupAddress)) {
                                byte[] addrByte = Utils.getByteAddress(mHfpActiveDevice);
                                activeGroupId = addrByte[5];
                            }

                            Log.d(TAG, "groupId: " + groupId + ", activeGroupId: " + activeGroupId);

                            if (groupId != INVALID_SET_ID && groupId == activeGroupId &&
                                !mHfpConnectedDevices.isEmpty()) {
                                for (BluetoothDevice peerDevice : mHfpConnectedDevices) {
                                    if (getGroupId(peerDevice) == groupId) {
                                        isGrpDevice = true;
                                        peerHfpDevice = peerDevice;
                                        break;
                                    }
                                }
                            }

                            Log.d(TAG, "isGrpDevice: " + isGrpDevice + ", peerHfpDevice: " + peerHfpDevice);
                            if (isGrpDevice) {
                                CallAudioIntf mCallAudio = CallAudioIntf.get();
                                 if (peerHfpDevice != null && mCallAudio != null &&
                                     mCallAudio.getConnectionState(peerHfpDevice)
                                                       == BluetoothProfile.STATE_CONNECTED) {
                                   Log.d(TAG, "calling set Active dev: " + peerHfpDevice);
                                   if (!setHfpActiveDevice(peerHfpDevice)) {
                                       Log.w(TAG, "Set hfp active device failed");
                                       setHfpActiveDevice(null);
                                   }
                                } else {
                                   Log.d(TAG, "No Active device Switch" +
                                          "as there is no Connected hfp peer");
                                   setHfpActiveDevice(null);
                                }
                                break;
                            }
                        }

                        if (Objects.equals(mHfpActiveDevice, device)) {
                            if (mAdapterService.isTwsPlusDevice(device) &&
                                !mHfpConnectedDevices.isEmpty()) {
                                if (hfpService == null) {
                                    Log.e(TAG, "no headsetService, FATAL");
                                    return;
                                }
                                BluetoothDevice peerTwsDevice =
                                 hfpService.getTwsPlusConnectedPeer(device);
                                if (peerTwsDevice != null &&
                                    hfpService.getConnectionState(peerTwsDevice)
                                    == BluetoothProfile.STATE_CONNECTED) {
                                   Log.d(TAG, "calling set Active dev: "
                                      + peerTwsDevice);
                                   if (!setHfpActiveDevice(peerTwsDevice)) {
                                       Log.w(TAG, "Set hfp active device failed");
                                       setHfpActiveDevice(null);
                                   }
                                } else {
                                   Log.d(TAG, "No Active device Switch" +
                                          "as there is no Connected TWS+ peer");
                                   setHfpActiveDevice(null);
                                }
                            } else {
                               setHfpActiveDevice(null);
                            }
                        }
                        /// enter auto set active function, case: current active device = null, disconnected active device
                        if (hfpService == null) {
                            Log.w(TAG, "no headsetService, FATAL");
                            return;
                        }
                        /// if current active is null or equal to current active device
                        if (!mHfpConnectedDevices.isEmpty() &&(mHfpActiveDevice == null || Objects.equals(mHfpActiveDevice, device))) {
                        /// try to get last active device
                            if ((hfpService.getConnectionState(getLastActiveDevice(Profile_HFP)) == BluetoothProfile.STATE_CONNECTED) && setHfpActiveDevice(getLastActiveDevice(Profile_HFP))) {
                                Log.d(TAG, "Auto set HFP active device to " + getLastActiveDevice(Profile_HFP).toString());
                            } else {
                                Log.d(TAG, "set Last active device fail/empty, set HFP active to null");
                                setHfpActiveDevice(null);
                            }
                        }
                        /*ASUS_BSP--- Yunchung "Auto set Active device(Hfp, A2dp) if current active device disconnected"'*/
                    }
                } break;

                case MESSAGE_HFP_ACTION_ACTIVE_DEVICE_CHANGED: {
                    Intent intent = (Intent) msg.obj;
                    BluetoothDevice device =
                            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                    if (DBG) {
                        Log.d(TAG,
                           "handleMessage(MESSAGE_HFP_ACTION_ACTIVE_DEVICE_CHANGED): "
                           + "device= " + device);
                    }
                    if (device != null && !Objects.equals(mHfpActiveDevice, device)) {
                        setHearingAidActiveDevice(null);
                        //if(!ApmConstIntf.getQtiLeAudioEnabled()) {
                        //  setLeAudioActiveDevice(null);
                        //}
                    }

                    if (DBG) {
                        Log.d(TAG, "set mHfpActiveDevice to " + device);
                    }
                    // Just assign locally the new value
                    mHfpActiveDevice = device;
                    /*ASUS_BSP+++ Yunchung "Auto set Active device(Hfp, A2dp) if current active device disconnected"'(Android T) */
                    if (mHfpActiveDevice !=null){
                        if (mHfpActiveDevice_History.contains(mHfpActiveDevice)) {
                            Log.d(TAG, "Target HFP Device already in active list History, change sequence");
                            mHfpActiveDevice_History.remove(mHfpActiveDevice);
                            mHfpActiveDevice_History.add(mHfpActiveDevice);
                        } else {
                            Log.d(TAG, "New HFP Active Device. Add in active list History");
                            mHfpActiveDevice_History.add(mHfpActiveDevice);
                        }
                    }
                    /*ASUS_BSP--- Yunchung "Auto set Active device(Hfp, A2dp) if current active device disconnected"'(Android T) */
                } break;

                case MESSAGE_HEARING_AID_ACTION_ACTIVE_DEVICE_CHANGED: {
                    Intent intent = (Intent) msg.obj;
                    BluetoothDevice device =
                            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                    if (DBG) {
                        Log.d(TAG,
                           "handleMessage(MESSAGE_HA_ACTION_ACTIVE_DEVICE_CHANGED): "
                           + "device= " + device);
                    }
                    // Just assign locally the new value
                    mHearingAidActiveDevice = device;
                    if (device != null && (!ApmConstIntf.getAospLeaEnabled() &&
                                !ApmConstIntf.getQtiLeAudioEnabled())) {
                        setA2dpActiveDevice(null);
                        setHfpActiveDevice(null);
                    }
                } break;
            }
        }
    }

    /** Notifications of audio device connection and disconnection events. */
    @SuppressLint("AndroidFrameworkRequiresPermission")
    private class AudioManagerAudioDeviceCallback extends AudioDeviceCallback {
        private boolean isWiredAudioHeadset(AudioDeviceInfo deviceInfo) {
            switch (deviceInfo.getType()) {
                case AudioDeviceInfo.TYPE_WIRED_HEADSET:
                case AudioDeviceInfo.TYPE_WIRED_HEADPHONES:
                case AudioDeviceInfo.TYPE_USB_HEADSET:
                    return true;
                default:
                    break;
            }
            return false;
        }

    private void broadcastLeActiveDeviceChange(BluetoothDevice device) {
        if (DBG) {
            Log.d(TAG, "broadcastLeActiveDeviceChange(" + device + ")");
        }

        Intent intent = new Intent(BluetoothLeAudio.ACTION_LE_AUDIO_ACTIVE_DEVICE_CHANGED);
        intent.putExtra(BluetoothDevice.EXTRA_DEVICE, device);
        intent.addFlags(Intent.FLAG_RECEIVER_REGISTERED_ONLY_BEFORE_BOOT
                        | Intent.FLAG_RECEIVER_INCLUDE_BACKGROUND);

        LeAudioService mLeAudioService = LeAudioService.getLeAudioService();
        if(mLeAudioService == null) {
            Log.e(TAG, "Le Audio Service not ready");
            return;
        }
        mLeAudioService.sendBroadcast(intent, BLUETOOTH_CONNECT);
    }

        @Override
        public void onAudioDevicesAdded(AudioDeviceInfo[] addedDevices) {
            if (DBG) {
                Log.d(TAG, "onAudioDevicesAdded");
            }
            boolean hasAddedWiredDevice = false;
            boolean hasAddedBleDevice = false;
            AudioDeviceInfo bleDeviceInfo = null;
            for (AudioDeviceInfo deviceInfo : addedDevices) {
                if (DBG) {
                    Log.d(TAG, "Audio device added: " + deviceInfo.getProductName() + " type: "
                            + deviceInfo.getType());
                }
                if (isWiredAudioHeadset(deviceInfo)) {
                    hasAddedWiredDevice = true;
                    //break;
                }

                if (deviceInfo.getType() == AudioDeviceInfo.TYPE_BLE_HEADSET) {
                   hasAddedBleDevice = true;
                   bleDeviceInfo = deviceInfo;
                }
            }
            if (hasAddedWiredDevice) {
                wiredAudioDeviceConnected();
            }
            Log.d(TAG, "BLE Device info: " + bleDeviceInfo);
            if (hasAddedBleDevice && bleDeviceInfo != null) {
                Log.d(TAG, "LEA device is source : " + bleDeviceInfo.isSource());
                BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
                BluetoothDevice dev = adapter.getRemoteDevice(bleDeviceInfo.getAddress());
                ActiveDeviceManagerServiceIntf activeDeviceManager = ActiveDeviceManagerServiceIntf.get();
                if (activeDeviceManager != null) {
                    BluetoothDevice AbsDevice = activeDeviceManager.getActiveAbsoluteDevice(ApmConstIntf.AudioFeatures.CALL_AUDIO);
                    BluetoothDevice activeDevice = activeDeviceManager.getActiveDevice(ApmConstIntf.AudioFeatures.CALL_AUDIO);
                    Log.d(TAG, "  LEA active dev: " + dev + "absolute device:" + AbsDevice);
                    Log.d(TAG, "current  active dev:" + activeDevice);
                    if (Objects.equals(dev,activeDevice) && bleDeviceInfo.isSource()) {
                        Log.d(TAG, " broadcast LEA device address: " + activeDevice);
                        broadcastLeActiveDeviceChange(AbsDevice);
                        onLeActiveDeviceChange(AbsDevice);
                        mLeAudioActiveDevice = AbsDevice;
                    }
                }
            }
        }

        @Override
        public void onAudioDevicesRemoved(AudioDeviceInfo[] removedDevices) {
            if (DBG) {
                Log.d(TAG, "onAudioDevicesAdded");
            }
            boolean hasRemovedWiredDevice = false;
            for (AudioDeviceInfo deviceInfo : removedDevices) {
                if (DBG) {
                    Log.d(TAG, "Audio device removed: " + deviceInfo.getProductName() + " type: "
                            + deviceInfo.getType());
                }
                if (isWiredAudioHeadset(deviceInfo)) {
                    hasRemovedWiredDevice = true;
                    break;
                }
            }
            if (hasRemovedWiredDevice) {
                wiredAudioDeviceDisconnected();
            }

        }
    }

    ActiveDeviceManager(AdapterService service, ServiceFactory factory) {
        mAdapterService = service;
        mFactory = factory;
        mAudioManager = (AudioManager) service.getSystemService(Context.AUDIO_SERVICE);
        mAudioManagerAudioDeviceCallback = new AudioManagerAudioDeviceCallback();
    }

    void start() {
        if (DBG) {
            Log.d(TAG, "start()");
        }

        mHandlerThread = new HandlerThread("BluetoothActiveDeviceManager");
        mHandlerThread.start();
        mHandler = new ActiveDeviceManagerHandler(mHandlerThread.getLooper());

        IntentFilter filter = new IntentFilter();
        filter.addAction(BluetoothAdapter.ACTION_STATE_CHANGED);
        filter.addAction(BluetoothA2dp.ACTION_CONNECTION_STATE_CHANGED);
        filter.addAction(BluetoothHeadset.ACTION_CONNECTION_STATE_CHANGED);
        filter.addAction(BluetoothLeAudio.ACTION_LE_AUDIO_CONNECTION_STATE_CHANGED);
        if(!ApmConstIntf.getQtiLeAudioEnabled()) {
            /*APM will send callback with Active Device update*/;
            Log.d(TAG, "start(): Registering for the active device changed intents");
            //filter.addAction(BluetoothLeAudio.ACTION_LE_AUDIO_ACTIVE_DEVICE_CHANGED);
            filter.addAction(BluetoothA2dp.ACTION_ACTIVE_DEVICE_CHANGED);
            filter.addAction(BluetoothHeadset.ACTION_ACTIVE_DEVICE_CHANGED);
        }
        filter.addAction(BluetoothHearingAid.ACTION_ACTIVE_DEVICE_CHANGED);
        mAdapterService.registerReceiver(mReceiver, filter);

        mAudioManager.registerAudioDeviceCallback(mAudioManagerAudioDeviceCallback, mHandler);
    }

    void init_broadcast_ref() {
        mBroadcastService = mAdapterService.getBroadcastService();
        mBroadcastIsActive = mAdapterService.getBroadcastActive();
        mBroadcastGetAddr = mAdapterService.getBroadcastAddress();
        mBroadcastNotifyState = mAdapterService.getBroadcastNotifyState();
    }
    void cleanup() {
        if (DBG) {
            Log.d(TAG, "cleanup()");
        }

        mAudioManager.unregisterAudioDeviceCallback(mAudioManagerAudioDeviceCallback);
        mAdapterService.unregisterReceiver(mReceiver);
        if (mHandlerThread != null) {
            mHandlerThread.quit();
            mHandlerThread = null;
        }
        resetState();
    }
    public void notify_active_device_unbonding(BluetoothDevice device) {
        if (device.isTwsPlusDevice() && Objects.equals(mA2dpActiveDevice, device)) {
            Log.d(TAG,"TWS+ active device is getting unpaired, avoid switch to pair");
            mTwsPlusSwitch = true;
        }
    }
    /**
     * Get the {@link Looper} for the handler thread. This is used in testing and helper
     * objects
     *
     * @return {@link Looper} for the handler thread
     */
    @VisibleForTesting
    public Looper getHandlerLooper() {
        if (mHandlerThread == null) {
            return null;
        }
        return mHandlerThread.getLooper();
    }

    @RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
    private boolean setA2dpActiveDevice(BluetoothDevice device) {
        if (DBG) {
            Log.d(TAG, "setA2dpActiveDevice(" + device + ")");
        }
        final A2dpService a2dpService = mFactory.getA2dpService();
        if (a2dpService == null) {
            return false;
        }
        if (!a2dpService.setActiveDevice(device)) {
            return false;
        }
        mA2dpActiveDevice = device;
        return true;
    }

    @RequiresPermission(allOf = {
            android.Manifest.permission.BLUETOOTH_CONNECT,
            android.Manifest.permission.MODIFY_PHONE_STATE,
    })
    private boolean setHfpActiveDevice(BluetoothDevice device) {
        if (DBG) {
            Log.d(TAG, "setHfpActiveDevice(" + device + ")");
        }
        final HeadsetService headsetService = mFactory.getHeadsetService();
        if (headsetService == null) {
            return false;
        }
        if (!headsetService.setActiveDevice(device)) {
            return false;
        }
        if (device != null) {
           if ((ApmConstIntf.getQtiLeAudioEnabled() ||
                ApmConstIntf.getAospLeaEnabled()) &&
                mAdapterService.isGroupDevice(device)) {
                Log.d(TAG, "setHfpActiveDevice(" + device + ")" + "is a group device, ignore");
                return true;
           }
        }
        mHfpActiveDevice = device;
        return true;
    }

    @RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
    private void setHearingAidActiveDevice(BluetoothDevice device) {
        if (DBG) {
            Log.d(TAG, "setHearingAidActiveDevice(" + device + ")");
        }
        final HearingAidService hearingAidService = mFactory.getHearingAidService();
        if (hearingAidService == null) {
            return;
        }
        if (!hearingAidService.setActiveDevice(device)) {
            return;
        }
        mHearingAidActiveDevice = device;
    }

    private void setBroadcastActiveDevice(BluetoothDevice device) {
        if (DBG) {
            Log.d(TAG, "setBroadcastActiveDevice(" + device + ")");
        }
        if (mBroadcastService != null && mBroadcastNotifyState != null) {
            try {
                mBroadcastNotifyState.invoke(mBroadcastService, false);
            } catch (IllegalAccessException e) {
                Log.e(TAG, "Broadcast:NotifyState IllegalAccessException");
            } catch (InvocationTargetException e) {
                Log.e(TAG, "Broadcast:NotifyState InvocationTargetException");
            }
        }
    }

    @RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
    private boolean setLeAudioActiveDevice(BluetoothDevice device) {
        if (DBG) {
            Log.d(TAG, "setLeAudioActiveDevice(" + device + ")");
        }
        final LeAudioService leAudioService = mFactory.getLeAudioService();
        if (leAudioService == null) {
            return false;
        }
        if (!leAudioService.setActiveDevice(device)) {
            return false;
        }
        mLeAudioActiveDevice = device;
        return true;
    }
    private int getCurrentActiveProfile(int mAudioType) {
        if (DBG) {
            Log.d(TAG, "getCurrentActiveProfile for (" + mAudioType + ")");
        }
        ActiveDeviceManagerServiceIntf service = ActiveDeviceManagerServiceIntf.get();
        ActiveDeviceManagerServiceIntf mActiveDeviceManager =
        ActiveDeviceManagerServiceIntf.get();
        int mActiveProfile =
               mActiveDeviceManager.getActiveProfile(mAudioType);
        return mActiveProfile;
    }

    private void resetState() {
        mA2dpConnectedDevices.clear();
        mA2dpActiveDevice_History.clear();
        mA2dpActiveDevice = null;

        mHfpConnectedDevices.clear();
        mHfpActiveDevice_History.clear();
        mHfpActiveDevice = null;

        mHearingAidActiveDevice = null;
        mLeAudioActiveDevice = null;
    }

    private int getGroupId(BluetoothDevice device) {
        int groupId = -1;
        CsipWrapper csipWrapper = CsipWrapper.getInstance();
        if (device != null) {
            groupId = csipWrapper.getRemoteDeviceGroupId(device, null);
        } else {
            groupId = INVALID_SET_ID;
        }

        if (DBG) {
            Log.d(TAG, "getGroupId for device: " + device + " groupId: " + groupId);
        }
        return groupId;
    }

    @VisibleForTesting
    BroadcastReceiver getBroadcastReceiver() {
        return mReceiver;
    }

    @VisibleForTesting
    BluetoothDevice getA2dpActiveDevice() {
        return mA2dpActiveDevice;
    }

    @VisibleForTesting
    BluetoothDevice getHfpActiveDevice() {
        return mHfpActiveDevice;
    }

    @VisibleForTesting
    BluetoothDevice getHearingAidActiveDevice() {
        return mHearingAidActiveDevice;
    }

    @VisibleForTesting
    BluetoothDevice getLeAudioActiveDevice() {
        return mLeAudioActiveDevice;
    }

    /**
     * Called when a wired audio device is connected.
     * It might be called multiple times each time a wired audio device is connected.
     */
    @VisibleForTesting
    @RequiresPermission(allOf = {
            android.Manifest.permission.BLUETOOTH_CONNECT,
            android.Manifest.permission.MODIFY_PHONE_STATE,
    })

    void wiredAudioDeviceConnected() {
        if (DBG) {
            Log.d(TAG, "wiredAudioDeviceConnected");
        }
        setA2dpActiveDevice(null);
        setHfpActiveDevice(null);
        setHearingAidActiveDevice(null);
        setBroadcastActiveDevice(null);
        if(!ApmConstIntf.getQtiLeAudioEnabled()) {
          setLeAudioActiveDevice(null);
        }
    }

    void wiredAudioDeviceDisconnected() {
        if (DBG) {
            Log.d(TAG, "wiredAudioDeviceDisconnected");
        }

        /// Check A2dp status
        if (getA2dpActiveDevice() == null) {
            final A2dpService mA2dpService = mFactory.getA2dpService();
            if (mA2dpService == null) {
                Log.w(TAG, "no mA2dpService, FATAL");
                return;
            }
            if (getLastActiveDevice(Profile_A2DP) != null) {
                if ((mA2dpService.getConnectionState(getLastActiveDevice(Profile_A2DP)) == BluetoothProfile.STATE_CONNECTED) && setA2dpActiveDevice(getLastActiveDevice(Profile_A2DP))) {
                    Log.d(TAG, "WiredAudioDevice disconnected, Auto set A2dp active device to " + getLastActiveDevice(Profile_A2DP).toString());
                } else {
                    Log.d(TAG, "WiredAudioDevice disconnected, set Last active device fail/empty, set A2dp active to null");
                    setA2dpActiveDevice(null);
                }
            }
        }

        /// Check Hfp status
        if (getHfpActiveDevice() == null) {
            final HeadsetService hfpService = mFactory.getHeadsetService();
            if (hfpService == null) {
                Log.w(TAG, "no mA2dpService, FATAL");
                return;
            }
            if (getLastActiveDevice(Profile_HFP) != null) {
                if ((hfpService.getConnectionState(getLastActiveDevice(Profile_HFP)) == BluetoothProfile.STATE_CONNECTED) && setHfpActiveDevice(getLastActiveDevice(Profile_HFP))) {
                    Log.d(TAG, "WiredAudioDevice disconnected, Auto set HFP active device to " + getLastActiveDevice(Profile_HFP).toString());
                } else {
                    Log.d(TAG, "WiredAudioDevice disconnected, set Last active device fail/empty, set HFP active to null");
                    setHfpActiveDevice(null);
                }
            }
        }
    }

    BluetoothDevice getLastActiveDevice (int Profile){
        switch (Profile) {
            case Profile_HFP: {
                if(!mHfpActiveDevice_History.isEmpty()) {
                    for (int i = 0; i<mHfpActiveDevice_History.size(); i++) {
                        Log.d(TAG, "List Hfp active device history: get: "+i+ " device= " + mHfpActiveDevice_History.get(i).toString());
                    }
                    return mHfpActiveDevice_History.get(mHfpActiveDevice_History.size()-1);
                } else {
                    Log.d(TAG, "No more Hfp active device");
                    return null;
                }
            }
            case Profile_A2DP: {
                if(!mA2dpActiveDevice_History.isEmpty()) {
                    for (int i = 0; i<mA2dpActiveDevice_History.size(); i++) {
                         Log.d(TAG, "List A2dp active device history: get: "+i+ " device= " + mA2dpActiveDevice_History.get(i).toString());
                    }
                    return mA2dpActiveDevice_History.get(mA2dpActiveDevice_History.size()-1);
                } else {
                    Log.d(TAG, "No more A2dp active device");
                    return null;
                }
            }
            default: {
                Log.w(TAG, "Profile not matching, return rull device");
                return null;
            }
        }
  	}

    public void onActiveDeviceChange(BluetoothDevice device, int audioType) {
        Log.d(TAG, "onActiveDeviceChange: audioType: " + audioType +
                   " for device " + device);
        if(audioType == ApmConstIntf.AudioFeatures.CALL_AUDIO) {
            Intent intent = new Intent(BluetoothHeadset.ACTION_ACTIVE_DEVICE_CHANGED);
            intent.putExtra(BluetoothDevice.EXTRA_DEVICE, device);
            mHandler.obtainMessage(MESSAGE_HFP_ACTION_ACTIVE_DEVICE_CHANGED,
                        intent).sendToTarget();
        } else if(audioType == ApmConstIntf.AudioFeatures.MEDIA_AUDIO) {
            Intent intent = new Intent(BluetoothA2dp.ACTION_ACTIVE_DEVICE_CHANGED);
            intent.putExtra(BluetoothDevice.EXTRA_DEVICE, device);
            mHandler.obtainMessage(MESSAGE_A2DP_ACTION_ACTIVE_DEVICE_CHANGED,
                        intent).sendToTarget();
        }
    }

    public void onLeActiveDeviceChange(BluetoothDevice device) {
        Log.d(TAG, "onLeActiveDeviceChange: for device " + device);
        Intent intent =
              new Intent(BluetoothLeAudio.ACTION_LE_AUDIO_ACTIVE_DEVICE_CHANGED);
        intent.putExtra(BluetoothDevice.EXTRA_DEVICE, device);
        mHandler.obtainMessage(MESSAGE_LE_AUDIO_ACTION_ACTIVE_DEVICE_CHANGED,
                                    intent).sendToTarget();
    }

    //Below API is called only for CSIP devices.
    public void onDeviceConnStateChange(BluetoothDevice device, int state,
                                        int prevState, int audioType) {
        Log.d(TAG, "onDeviceConnStateChange: audioType: " + audioType +
                   " state: " + state + ", prevState: " + prevState +
                   " for device " + device);
        if (audioType == ApmConstIntf.AudioFeatures.CALL_AUDIO &&
                         state == BluetoothProfile.STATE_DISCONNECTED) {
            Intent intent = new Intent(BluetoothHeadset.ACTION_CONNECTION_STATE_CHANGED);
            intent.putExtra(BluetoothProfile.EXTRA_PREVIOUS_STATE, prevState);
            intent.putExtra(BluetoothProfile.EXTRA_STATE, state);
            intent.putExtra(BluetoothDevice.EXTRA_DEVICE, device);
            mHandler.obtainMessage(MESSAGE_HFP_ACTION_CONNECTION_STATE_CHANGED,
                        intent).sendToTarget();
        } else if(audioType == ApmConstIntf.AudioFeatures.MEDIA_AUDIO &&
                  prevState == BluetoothProfile.STATE_CONNECTED) {
            Intent intent = new Intent(BluetoothA2dp.ACTION_CONNECTION_STATE_CHANGED);
            intent.putExtra(BluetoothProfile.EXTRA_PREVIOUS_STATE, prevState);
            intent.putExtra(BluetoothProfile.EXTRA_STATE, state);
            intent.putExtra(BluetoothDevice.EXTRA_DEVICE, device);
            mHandler.obtainMessage(MESSAGE_A2DP_ACTION_CONNECTION_STATE_CHANGED,
                        intent).sendToTarget();
        }
    }
}
