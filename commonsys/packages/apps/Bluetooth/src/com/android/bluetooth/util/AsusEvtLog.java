package com.android.bluetooth.util;

import android.util.Log;
import java.io.FileWriter;
import java.io.BufferedWriter;
import java.io.IOException;

public class AsusEvtLog {

    private static final Object mAsusEvtLock = new Object();

    public static void writeAsusEvtLog(String TAG, String event) {
        String eventlog = "[BT] " + event + "\n";
        Log.d(TAG, eventlog);
        BufferedWriter bw = null;
        try {
                bw = new BufferedWriter(new FileWriter("/proc/asusevtlog", true));
                synchronized (mAsusEvtLock) {
                    bw.write(eventlog);
                }
        } catch(IOException ioe1) {
            ioe1.printStackTrace();
        } finally {
            if (bw != null) {
                try {
                    bw.close();
                }
                catch(IOException ioe2) {
                    ioe2.printStackTrace();
                }
            }
        }
    }
}