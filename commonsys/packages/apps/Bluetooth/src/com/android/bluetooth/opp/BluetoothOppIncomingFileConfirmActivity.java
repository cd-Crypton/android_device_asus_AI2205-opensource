/*
 * Copyright (c) 2008-2009, Motorola, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * - Neither the name of the Motorola, Inc. nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

package com.android.bluetooth.opp;

import android.bluetooth.AlertActivity;
import android.content.BroadcastReceiver;
import android.content.ContentValues;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.text.format.Formatter;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.widget.TextView;
import android.widget.Toast;

import com.android.bluetooth.R;
import com.android.bluetooth.ThemeUtils;
import com.asus.commonres.AsusResUtils;

/**
 * This class is designed to ask user to confirm if accept incoming file;
 */
public class BluetoothOppIncomingFileConfirmActivity extends AlertActivity
        implements DialogInterface.OnClickListener {
    private static final String TAG = "BluetoothIncomingFileConfirmActivity";
    private static final boolean D = Constants.DEBUG;
    private static final boolean V = Constants.VERBOSE;

    private static final int DISMISS_TIMEOUT_DIALOG = 0;

    private static final int DISMISS_TIMEOUT_DIALOG_VALUE = 2000;

    private static final String PREFERENCE_USER_TIMEOUT = "user_timeout";

    private BluetoothOppTransferInfo mTransInfo;

    private Uri mUri;

    private ContentValues mUpdateValues;

    private boolean mTimeout = false;

    private Vibrator vibrator;
    private VibrationEffect vibrationEffect;

    private BroadcastReceiver mReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (!BluetoothShare.USER_CONFIRMATION_TIMEOUT_ACTION.equals(intent.getAction())) {
                return;
            }
            onTimeout();
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
//        setTheme(R.style.Theme_Material_Settings_Floating);
        if (V) {
            Log.d(TAG, "onCreate(): action = " + getIntent().getAction());
        }
        ThemeUtils.loadThemeColorCodeFromProvider(this);
        super.onCreate(savedInstanceState);

        //[Timothy][TT-339725]
        // IMS add awinic vibration for bluetooth transferring
        vibrator = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
        vibrationEffect = VibrationEffect.createOneShot(200, VibrationEffect.DEFAULT_AMPLITUDE);
        // Support FEATURE_ASUS_VIBRATION_LIB = "asus.vibration.lib"
        if (getPackageManager().hasSystemFeature("asus.vibration.lib")) {
            vibrationEffect = VibrationEffect.createOneShot(0, 90005);
        }

        Intent intent = getIntent();
        mUri = intent.getData();
        mTransInfo = new BluetoothOppTransferInfo();
        mTransInfo = BluetoothOppUtility.queryRecord(this, mUri);
        if (mTransInfo == null) {
            if (V) {
                Log.e(TAG, "Error: Can not get data from db");
            }
            finish();
            return;
        }

        mAlertBuilder.setTitle(getString(R.string.incoming_file_confirm_content));
        mAlertBuilder.setView(createView());
        mAlertBuilder.setPositiveButton(R.string.incoming_file_confirm_ok, this);
        mAlertBuilder.setNegativeButton(R.string.incoming_file_confirm_cancel, this);

        setupAlert();
        if (V) {
            Log.v(TAG, "mTimeout: " + mTimeout);
        }
        if (mTimeout) {
            onTimeout();
        }

        if (V) {
            Log.v(TAG, "BluetoothIncomingFileConfirmActivity: Got uri:" + mUri);
        }

        registerReceiver(mReceiver,
                new IntentFilter(BluetoothShare.USER_CONFIRMATION_TIMEOUT_ACTION));
    }

    private View createView() {
        View view = getLayoutInflater().inflate(R.layout.incoming_dialog, null);

        TextView fromTitle = (TextView) view.findViewById(R.id.from_title);
        TextView fileNameTitle = (TextView) view.findViewById(R.id.filename_title);
        TextView sizeTitle = (TextView) view.findViewById(R.id.size_title);
        TextView fromView = (TextView) view.findViewById(R.id.from_content);
        TextView fileNameView = (TextView) view.findViewById(R.id.filename_content);
        TextView sizeView = (TextView) view.findViewById(R.id.size_content);

        fromView.setText(mTransInfo.mDeviceName);
        fileNameView.setText(mTransInfo.mFileName);
        sizeView.setText(Formatter.formatFileSize(this, mTransInfo.mTotalBytes));

        fromTitle.setTextColor(ThemeUtils.getTextColorCode(this));
        fromTitle.setAlpha(0.6f);
        fileNameTitle.setTextColor(ThemeUtils.getTextColorCode(this));
        fileNameTitle.setAlpha(0.6f);
        sizeTitle.setTextColor(ThemeUtils.getTextColorCode(this));
        sizeTitle.setAlpha(0.6f);

        fromView.setTextColor(ThemeUtils.getTextColorCode(this));
        fileNameView.setTextColor(ThemeUtils.getTextColorCode(this));
        sizeView.setTextColor(ThemeUtils.getTextColorCode(this));

        return view;
    }

    @Override
    public void onClick(DialogInterface dialog, int which) {
        switch (which) {
            case DialogInterface.BUTTON_POSITIVE:
                if (!mTimeout) {
                    // Update database
                    mUpdateValues = new ContentValues();
                    mUpdateValues.put(BluetoothShare.USER_CONFIRMATION,
                            BluetoothShare.USER_CONFIRMATION_CONFIRMED);
                    this.getContentResolver().update(mUri, mUpdateValues, null, null);
                    if (D) Log.v(TAG, " Confirmed :" + mUri);
                    Toast.makeText(this, getString(R.string.bt_toast_1), Toast.LENGTH_SHORT).show();
                    if(vibrator != null && vibrationEffect != null){
                        vibrator.vibrate(vibrationEffect);
                    }
                }
                break;

            case DialogInterface.BUTTON_NEGATIVE:
                // Update database
                mUpdateValues = new ContentValues();
                mUpdateValues.put(BluetoothShare.USER_CONFIRMATION,
                        BluetoothShare.USER_CONFIRMATION_DENIED);
                this.getContentResolver().update(mUri, mUpdateValues, null, null);
                if (D) Log.v(TAG, " Denied :" + mUri);
                break;
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            if (D) {
                Log.d(TAG, "onKeyDown() called; Key: back key");
            }
            finish();
            return true;
        }
        return false;
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        unregisterReceiver(mReceiver);
    }

    @Override
    protected void onRestoreInstanceState(Bundle savedInstanceState) {
        super.onRestoreInstanceState(savedInstanceState);
        mTimeout = savedInstanceState.getBoolean(PREFERENCE_USER_TIMEOUT);
        if (V) {
            Log.v(TAG, "onRestoreInstanceState() mTimeout: " + mTimeout);
        }
        if (mTimeout) {
            onTimeout();
        }
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        if (V) {
            Log.v(TAG, "onSaveInstanceState() mTimeout: " + mTimeout);
        }
        outState.putBoolean(PREFERENCE_USER_TIMEOUT, mTimeout);
    }

    private void onTimeout() {
        mTimeout = true;

        changeTitle(getString(
                R.string.incoming_file_confirm_timeout_content,
                mTransInfo.mDeviceName));
        changeButtonVisibility(DialogInterface.BUTTON_NEGATIVE, View.GONE);
        changeButtonText(
                DialogInterface.BUTTON_POSITIVE,
                getString(R.string.incoming_file_confirm_timeout_ok));

        mTimeoutHandler.sendMessageDelayed(mTimeoutHandler.obtainMessage(DISMISS_TIMEOUT_DIALOG),
                DISMISS_TIMEOUT_DIALOG_VALUE);
    }

    private final Handler mTimeoutHandler = new Handler() {
        @Override
        public void handleMessage(Message msg) {
            switch (msg.what) {
                case DISMISS_TIMEOUT_DIALOG:
                    if (D) {
                        Log.v(TAG, "Received DISMISS_TIMEOUT_DIALOG msg.");
                    }
                    finish();
                    break;
                default:
                    break;
            }
        }
    };
}
