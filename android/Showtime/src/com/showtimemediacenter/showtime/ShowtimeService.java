package com.showtimemediacenter.showtime;

import android.app.Service;
import android.content.Intent;
import android.os.Bundle;
import android.os.IBinder;

import android.util.Log;

public class ShowtimeService extends Service {

    @Override
    public void onCreate() {
        Log.d("Showtime", "core init starting");
        STCore.init(getApplicationContext());
        Log.d("Showtime", "core init done");
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

}
