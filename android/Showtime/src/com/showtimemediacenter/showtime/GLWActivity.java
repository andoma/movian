package com.showtimemediacenter.showtime;

import java.util.concurrent.FutureTask;
import java.util.concurrent.RunnableFuture;
import java.util.concurrent.Callable;

import android.os.Handler;

import android.os.Bundle;
import android.os.Message;
import android.content.Intent;
import android.app.Activity;
import android.view.Menu;
import android.view.KeyEvent;
import android.view.SurfaceView;
import android.view.Window;
import android.view.WindowManager;
import android.util.Log;

import android.widget.FrameLayout;

import android.util.Log;

public class GLWActivity extends Activity implements VideoRendererProvider {

    GLWView mGLWView;
    FrameLayout mRoot;
    SurfaceView sv;

    private void startGLW() {
        // remove title
        mRoot = new FrameLayout(this);

        mGLWView = new GLWView(getApplication(), this);
        mRoot.addView(mGLWView);

       setContentView(mRoot);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
            WindowManager.LayoutParams.FLAG_FULLSCREEN);

        startService(new Intent(this, ShowtimeService.class));

        Handler h = new Handler(new Handler.Callback() {
                public boolean handleMessage(Message msg) {
                    startGLW();
                    return true;
                }
            });

        h.sendEmptyMessage(0);
    }

    public boolean onKeyDown(int keyCode, KeyEvent event) {

        if(mGLWView.keyDown(keyCode))
            return true;
        return super.onKeyDown(keyCode, event);
    }

    @Override
    protected void onResume() {
        super.onResume();
    }

    @Override
    protected void onPause() {
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        mGLWView.destroy();
    }

    // These does not execute on the main ui thread so we need to dispatch

    @Override
    public VideoRenderer createVideoRenderer() throws Exception {

        RunnableFuture<VideoRenderer> f = new FutureTask<VideoRenderer>(new Callable<VideoRenderer>() {
                public VideoRenderer call() {
                    VideoRenderer vr = new VideoRenderer(GLWActivity.this);
                    mRoot.addView(vr);
                    return vr;
                }
            });

        runOnUiThread(f);
        return f.get();
    }

    @Override
    public void destroyVideoRenderer(final VideoRenderer vr) {

        runOnUiThread(new Runnable() {
                public void run() {
                    mRoot.removeView(vr);
                }
            });
    }
}
