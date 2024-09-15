/*
 * Copyright 2018 The Android Open Source Project
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

package com.android.bluetooth.hfp;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.content.Context;
import android.content.SharedPreferences;
import android.media.AudioDeviceAttributes;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.util.Log;

import java.util.HashMap;
import java.util.Map;
import java.util.Objects;

class HeadsetVolumeManager extends AudioDeviceCallback {
    public static final String TAG = "HeadsetVolumeManager";
    public static final boolean DEBUG = true;

    // All volumes are stored for hfp volume values
    private static final String VOLUME_MAP = "bluetooth_volume_map_hfp";
    private static final int HFP_MAX_VOL = 15;
    private static final int NEW_DEVICE_VOLUME = 7;

    Context mContext;
    HeadsetNativeInterface mNativeInterface;

    HashMap<BluetoothDevice, Boolean> mDeviceMap = new HashMap();
    HashMap<BluetoothDevice, Integer> mVolumeMap = new HashMap();
    BluetoothDevice mCurrentDevice = null;


    private SharedPreferences getVolumeMap() {
        return mContext.getSharedPreferences(VOLUME_MAP, Context.MODE_PRIVATE);
    }

    HeadsetVolumeManager(Context context, //AudioManager audioManager,
            HeadsetNativeInterface nativeInterface) {
        mContext = context;
        mNativeInterface = nativeInterface;

        // Load the stored volume preferences into a hash map since shared preferences are slow
        // to poll and update. If the device has been unbonded since last start remove it from
        // the map.
        Map<String, ?> allKeys = getVolumeMap().getAll();
        SharedPreferences.Editor volumeMapEditor = getVolumeMap().edit();
        for (Map.Entry<String, ?> entry : allKeys.entrySet()) {
            String key = entry.getKey();
            Object value = entry.getValue();
            BluetoothDevice d = BluetoothAdapter.getDefaultAdapter().getRemoteDevice(key);

            if (value instanceof Integer && d.getBondState() == BluetoothDevice.BOND_BONDED) {
                mVolumeMap.put(d, (Integer) value);
            } else {
                d("Removing " + key + " from the volume map");
                volumeMapEditor.remove(key);
            }
        }
        volumeMapEditor.apply();
    }

    synchronized void storeVolumeForDevice(@NonNull BluetoothDevice device, int Volume) {
        if (device.getBondState() != BluetoothDevice.BOND_BONDED) {
            return;
        }
        SharedPreferences.Editor pref = getVolumeMap().edit();
        int storeVolume =  Volume;
        Log.i(TAG, "storeVolume: Storing HFP volume level for device " + device
                + " : " + storeVolume);
        mVolumeMap.put(device, storeVolume);
        pref.putInt(device.getAddress(), storeVolume);
        // Always use apply() since it is asynchronous, otherwise the call can hang waiting for
        // storage to be written.
        pref.apply();
    }

    synchronized void removeStoredVolumeForDevice(@NonNull BluetoothDevice device) {
        if (device.getBondState() != BluetoothDevice.BOND_NONE) {
            return;
        }
        SharedPreferences.Editor pref = getVolumeMap().edit();
        Log.i(TAG, "RemoveStoredVolume: Remove stored stream volume level for device " + device);
        mVolumeMap.remove(device);
        pref.remove(device.getAddress());
        // Always use apply() since it is asynchronous, otherwise the call can hang waiting for
        // storage to be written.
        pref.apply();
    }

    synchronized int getVolume(@NonNull BluetoothDevice device, int defaultValue) {
        if (!mVolumeMap.containsKey(device)) {
            Log.w(TAG, "getVolume: Couldn't find volume preference for device: " + device);
            return defaultValue;
        }

        d("getVolume: Returning volume " + mVolumeMap.get(device));
        return mVolumeMap.get(device);
    }

    public int getNewDeviceVolume() {
        return NEW_DEVICE_VOLUME;
    }

    static void d(String msg) {
        if (DEBUG) {
            Log.d(TAG, msg);
        }
    }
}
