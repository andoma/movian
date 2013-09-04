package com.showtimemediacenter.showtime;

import java.io.File;

import android.os.Handler;
import android.os.Message;
import android.content.Context;
import android.util.Log;
import android.graphics.Bitmap;
import android.app.Activity;
import android.view.SurfaceView;
import android.view.SurfaceHolder;
import android.graphics.PixelFormat;
import android.graphics.Canvas;

import android.widget.FrameLayout;

public class STCore {


    private static Activity currentActivity;
    private static SurfaceView sv;
    private static Handler courier;

    static {
        System.loadLibrary("showtime");
    }

    public static native void coreInit(String settingsdir, String cachedir);

    // These two GLW methods should be called on UI thread

    public static native int glwCreate(VideoRendererProvider vrp);
    public static native void glwDestroy(int id);

    // These four GLW methods should be called on OpenGL renderer thread

    public static native void glwInit(int id);
    public static native void glwFini(int id);
    public static native void glwResize(int id, int width, int height);
    public static native void glwStep(int id);

    // The thread for those are not so important I think...

    public static native void glwMotion(int id, int event, int x, int y);
    public static native boolean glwKeyDown(int id, int code);

    // Create / Destroy subscriptions

    public static native int subScalar(int prop, String path, Subscription.Callback cb);
    public static native int unSub(int id);

    // Release a property

    public static native void propRelease(int id);

    // Dispatch a round of property updates, should only be called on UI thread

    public static native void pollCourier();

    public static void init(Context ctx) {
        courier = new Handler(ctx.getMainLooper(), new Handler.Callback() {
                public boolean handleMessage(Message msg) {
                    pollCourier();
                    return true;
                }
            });

        coreInit(ctx.getFilesDir().getPath(), ctx.getCacheDir().getPath());
    }

    public static void wakeupMainDispatcher() {
        courier.sendEmptyMessage(0);
    }


    public static Bitmap createBitmap(int width, int height) {
        return Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
    }

    public static void pushBitmap(final Bitmap b) {

        currentActivity.runOnUiThread(new Runnable() {
                public void run() {

                    SurfaceHolder sh = sv.getHolder();

                    sh.setFormat(PixelFormat.RGBA_8888);

                    Canvas c = sh.lockCanvas();
                    c.drawBitmap(b, null, sh.getSurfaceFrame(), null);
                    sh.unlockCanvasAndPost(c);
                }
            });
    }
}
