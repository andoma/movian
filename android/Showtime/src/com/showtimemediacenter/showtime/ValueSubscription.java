package com.showtimemediacenter.showtime;

import java.io.File;
import android.content.Context;
import android.util.Log;

public class ValueSubscription {

    private int id;

    public ValueSubscription(Prop p, String path, Callback callback) {
        id = STCore.subValue((int)(p != null ? p.getPropId() : 0), path, callback);
    }

    public void stop() {
        if(id != 0) {
            STCore.unSub(id);
            id = 0;
        }
    }

    protected void finalize() {
        if(id != 0)
            STCore.unSub(id);
    }

    public interface Callback {
    }
}
