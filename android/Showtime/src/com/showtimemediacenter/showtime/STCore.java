package com.showtimemediacenter.showtime;

import java.io.File;

import android.content.Context;

public class STCore {

    static {
        System.loadLibrary("showtime");
    }

    public static native void coreInit(String settingsdir, String cachedir);

    public static native void glwInit();
    public static native void glwResize(int width, int height);
    public static native void glwStep();
    public static native void glwMotion(int event, int x, int y);
    public static native boolean glwKeyDown(int code);

    public static native int sub(int prop, String path, SubCallback cb);

    public static void init(Context ctx) {
        coreInit(ctx.getFilesDir().getPath(), ctx.getCacheDir().getPath());
    }
}
