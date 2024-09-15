package com.android.bluetooth.btservice;

import android.util.Log;
import java.io.FileWriter;
import java.io.BufferedWriter;
import java.io.IOException;
import android.os.DropBoxManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;

import java.text.SimpleDateFormat;
import java.util.Calendar;
import java.util.TimeZone;

public class AsusBluetoothEvtLog {
    private static final String TAG = "AsusBluetoothEvtLog";
    private static final String DROP_BOX_TAG_Conn = "conn_evt";
    private static final String DROP_BOX_TAG_BT_Conn = "bluetooth_conn_evt";
    private static final String DROP_BOX_TAG_BT_Drop = "bluetooth_drop_evt";
    private static final String DROP_BOX_TAG_BT_Default = "bluetooth_default_evt";
    //private static final Object mAsusEvtLock = new Object();

    private static DropBoxManager mDropBoxManager;
    private static Context mContext;

    public static final int AsusBluetoothConnEvt = 1;
    public static final int AsusBluetoothDropEvt = 2;

    public AsusBluetoothEvtLog (Context startcontext) {
        mContext = startcontext;
        mDropBoxManager = (DropBoxManager) mContext.getSystemService(Context.DROPBOX_SERVICE);
    }

    public static void logToDropBox( int evt, String device_addr, String msg) {   //Modify to public for data used.
        switch (evt) {
            case AsusBluetoothConnEvt:
                Log.d(TAG, "logToDropBox: AsusBluetoothConnEvt");
                //UpdateToDropBox ( DROP_BOX_TAG_Conn, DROP_BOX_TAG_BT_Conn + "Vendor Mac: " + ShrinkDeviceAddr(device_addr) + " " +  msg);
            break;
            case AsusBluetoothDropEvt:
                Log.d(TAG, "logToDropBox: AsusBluetoothDropEvt");
                UpdateToDropBox ( DROP_BOX_TAG_Conn, DROP_BOX_TAG_BT_Drop + " mac: " + ShrinkDeviceAddr(device_addr) + " " +  msg);
            break;
            default:
                Log.d(TAG, "logToDropBox: AsusBluetoothDefaultEvt");
                UpdateToDropBox ( DROP_BOX_TAG_Conn, DROP_BOX_TAG_BT_Default + msg);
        }
    }

    private static String ShrinkDeviceAddr (String addr) {
        Log.d(TAG, "ShrinkDeviceAddr: before " + addr);
        String vendor_addr = addr.substring(0, 8);
        Log.d(TAG, "ShrinkDeviceAddr: after " + vendor_addr);
        return vendor_addr;
    }

    private static void UpdateToDropBox( String tag, String msg) {
        if (mDropBoxManager != null) {
            SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");
            mDropBoxManager.addText(tag, sdf.format(Calendar.getInstance(TimeZone.getDefault()).getTime()) + " " + msg);
        }
    }
}
