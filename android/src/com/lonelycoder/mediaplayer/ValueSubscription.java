package com.lonelycoder.mediaplayer;

import java.io.File;
import android.content.Context;
import android.util.Log;

public class ValueSubscription {

    private int id;

    public ValueSubscription(Prop p, String path, Callback callback) {
        id = Core.subValue((int)(p != null ? p.getPropId() : 0), path, callback);
    }

    public void stop() {
        if(id != 0) {
            Core.unSub(id);
            id = 0;
        }
    }

    protected void finalize() {
        if(id != 0)
            Core.unSub(id);
    }

    public interface Callback {
    }
}
