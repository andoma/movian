package com.lonelycoder.mediaplayer;

import java.io.File;
import android.net.Uri;

import android.app.Service;
import android.content.Intent;
import android.os.Bundle;
import android.os.IBinder;
import android.content.IntentFilter;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.net.ConnectivityManager;
import android.os.Build;
import android.util.Log;

import android.media.AudioManager;

public class CoreService extends Service {

    final IntentFilter networkChecker = new IntentFilter();

    private BroadcastReceiver networkCheckerAction = new BroadcastReceiver() {
            @Override
            public void onReceive(final Context context, final Intent intent) {
                Core.networkStatusChanged();
            }
        };

    @Override
    public void onCreate() {
        Log.d("Movian", "Coreservice Init");

        networkChecker.addAction(android.net.ConnectivityManager.CONNECTIVITY_ACTION);
        Core.init(this);
        registerReceiver(networkCheckerAction, networkChecker);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    public int getSystemAudioSampleRate() {

        if(Build.VERSION.SDK_INT >= 21) {
            AudioManager audioManager = (AudioManager) this.getSystemService(Context.AUDIO_SERVICE);
            String sr = audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE);
            return Integer.parseInt(sr);
        }
        return 0;
    }

    public int getSystemAudioFramesPerBuffer() {

        if(Build.VERSION.SDK_INT >= 21) {
            AudioManager audioManager = (AudioManager) this.getSystemService(Context.AUDIO_SERVICE);
            String sr = audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER);
            return Integer.parseInt(sr);
        }
        return 0;
    }
}

