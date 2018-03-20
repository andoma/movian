package com.lonelycoder.mediaplayer;

import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;


import android.util.Log;

import android.content.Context;
import android.app.Activity;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.Surface;

import android.widget.FrameLayout;
import android.widget.FrameLayout.LayoutParams;

class VideoRenderer extends SurfaceView implements SurfaceHolder.Callback {

    Activity mActivity;
    private Surface surface;
    Lock lock;

    public VideoRenderer(Activity activity) {
        super(activity);
        mActivity = activity;
        lock = new ReentrantLock();
        getHolder().addCallback(this);
    }

    public Surface getSurface() {
        lock.lock();
        return surface;
    }

    public Surface getSurfaceUnlocked() {
        return surface;
    }

    public void releaseSurface() {
        lock.unlock();
    }

    public void surfaceCreated(SurfaceHolder holder) {
        lock.lock();
        surface = holder.getSurface();
        lock.unlock();
    }

    public void surfaceChanged(SurfaceHolder holder, int format,
                                int width, int height) {
    }

    public void surfaceDestroyed(SurfaceHolder holder) {
        lock.lock();
        surface = null;
        lock.unlock();
   }

    @Override
    protected void onAttachedToWindow (){
        super.onAttachedToWindow();
        this.setKeepScreenOn(true);
    }

    @Override
    protected void onDetachedFromWindow(){
        super.onDetachedFromWindow();
        this.setKeepScreenOn(false);
    }

    public void setPosition(final int left, final int top,
                            final int width, final int height) {
        mActivity.runOnUiThread(new Runnable() {
                public void run() {
                    FrameLayout.LayoutParams params;

                    params = new FrameLayout.LayoutParams(width, height);
                    params.leftMargin = left;
                    params.topMargin = top;
                    VideoRenderer.this.setLayoutParams(params);
                }
            });
    }
}
