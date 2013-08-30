package com.showtimemediacenter.showtime;

import java.io.File;

import android.content.Context;

public class Prop {

    private int id;

    public Prop(int propertyId) {
        id = propertyId;
    }

    public int getPropId() {
        return id;
    }

    protected void finalize() {
        STCore.propRelease(id);
    }
}
