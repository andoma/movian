package com.lonelycoder.mediaplayer;

import java.io.File;

import android.content.Context;

public class Prop {

    private int id;

    public Prop(int propertyId) {
        id = Core.propRetain(propertyId);
    }

    public int getPropId() {
        return id;
    }

    protected void finalize() {
        Core.propRelease(id);
    }
}
