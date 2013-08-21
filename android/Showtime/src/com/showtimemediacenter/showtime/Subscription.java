package com.showtimemediacenter.showtime;

import java.io.File;

import android.content.Context;

import android.util.Log;

public class Subscription {

    private int subid;

    public Subscription(String path, SubCallback cb) {
        Log.d("Showtime", "About to subscribe");
        subid = STCore.sub(0, path, cb);
    }

}
