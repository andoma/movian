package com.lonelycoder.mediaplayer;

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
    protected void onStart() {
        super.onStart();

        Handler h = new Handler(new Handler.Callback() {
                public boolean handleMessage(Message msg) {
                    startGLW();
                    return true;
                }
            });

        h.sendEmptyMessage(0);
    }

    @Override
    protected void onStop() {
        super.onStop();
        mGLWView.destroy();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);

        startService(new Intent(this, CoreService.class));
    }

    public boolean onKeyUp(int keyCode, KeyEvent event) {

        if(mGLWView.keyUp(keyCode, event))
            return true;
        return super.onKeyUp(keyCode, event);
    }


    public boolean onKeyDown(int keyCode, KeyEvent event) {

        if(mGLWView.keyDown(keyCode, event))
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


    @Override
    public void disableScreenSaver() {
        runOnUiThread(new Runnable() {
                public void run() {
                    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                }
            });
    }

    @Override
    public void enableScreenSaver() {
        runOnUiThread(new Runnable() {
                public void run() {
                    getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                }
            });
    }

}
