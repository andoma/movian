package com.lonelycoder.mediaplayer;

import java.io.File;

import android.os.Handler;
import android.os.Message;
import android.os.Environment;
import android.content.Context;
import android.util.Log;
import android.graphics.Bitmap;
import android.app.Activity;
import android.view.SurfaceView;
import android.view.SurfaceHolder;
import android.graphics.PixelFormat;
import android.graphics.Canvas;
import android.text.format.DateFormat;
import android.widget.FrameLayout;
import android.content.pm.PackageManager;

import android.media.MediaCodec;
import android.media.MediaFormat;

import android.provider.Settings.Secure;


public class Core {


    private static Activity currentActivity;
    private static SurfaceView sv;
    private static CoreService mService;

    static {
        System.loadLibrary("avutil");
        System.loadLibrary("avresample");
        System.loadLibrary("avcodec");
        System.loadLibrary("avformat");
        System.loadLibrary("swscale");
        System.loadLibrary("core");
    }

    public static native void coreInit(String settingsdir, String cachedir,
                                       String sdcard, String aid,
                                       int clock_24hrs,
                                       String music,
                                       String pictures,
                                       String movies,
                                       int audio_sample_rate,
                                       int audio_frames_per_buffer);

    public static native void openUri(String uri);

    // These two GLW methods should be called on UI thread

    public static native int glwCreate(VideoRendererProvider vrp);
    public static native void glwDestroy(int id);

    // These four GLW methods should be called on OpenGL renderer thread

    public static native void glwInit(int id);
    public static native void glwFini(int id);
    public static native void glwResize(int id, int width, int height);
    public static native void glwStep(int id);
    public static native void glwFlush(int id);

    // The thread for those are not so important I think...

    public static native void glwMotion(int id, int source, int event, int x, int y, long timestamp);
    public static native boolean glwKeyDown(int id, int code, int unicode,
                                            boolean shift);
    public static native boolean glwKeyUp(int id, int code);

    public static native void permissionResult(boolean ok);

    // Create / Destroy subscriptions

    public static native int subValue(int prop, String path, ValueSubscription.Callback cb);
    public static native int subNodes(int prop, String path, NodeSubscriptionCallback cb);
    public static native int unSub(int id);

    // Properties

    public static native int propRetain(int id);

    public static native void propRelease(int id);

    // Dispatch a round of property updates, should only be called on UI thread
    public static native void pollCourier();

    public static native void networkStatusChanged();

    public static void init(CoreService svc) {

        mService = svc;

        int clock_24hrs = DateFormat.is24HourFormat(svc) ? 1 : 0;

        coreInit(svc.getFilesDir().getPath(),
                 svc.getCacheDir().getPath(),
                 Environment.getExternalStorageDirectory().toString(),
                 Secure.getString(svc.getContentResolver(), Secure.ANDROID_ID),
                 clock_24hrs,
                 Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MUSIC).toString(),
                 Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_PICTURES).toString(),
                 Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MOVIES).toString(),
                 svc.getSystemAudioSampleRate(),
                 svc.getSystemAudioFramesPerBuffer());
    }

    public static Bitmap createBitmap(int width, int height) {
        return Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
    }

    public static boolean checkPermission(String permission) {
        return mService.checkSelfPermission(permission) ==
            PackageManager.PERMISSION_GRANTED;
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

    public static native void vdInputAvailable(int opaque, int buf);
    public static native void vdOutputAvailable(int opaque, int buf,
                                                long pts);
    public static native void vdOutputFormatChanged(int opaque,
                                                    MediaFormat format);
    public static native void vdError(int opaque);


    public static void setVideoDecoderWrapper(MediaCodec codec,
                                              final int opaque) {

        codec.setCallback(new MediaCodec.Callback() {

                @Override
                public void onInputBufferAvailable(MediaCodec mc, int buf) {
                    vdInputAvailable(opaque, buf);
                }

                @Override
                public void onOutputBufferAvailable(MediaCodec mc, int buf,
                                                    MediaCodec.BufferInfo info) {
                    vdOutputAvailable(opaque, buf, info.presentationTimeUs);
                }

                @Override
                public void onOutputFormatChanged(MediaCodec mc,
                                                  MediaFormat format) {
                    vdOutputFormatChanged(opaque, format);
                }

                @Override
                public void onError(MediaCodec mc, MediaCodec.CodecException e) {
                    vdError(opaque);
                }
            });
    }
}
