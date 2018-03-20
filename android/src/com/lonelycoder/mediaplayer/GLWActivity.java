package com.lonelycoder.mediaplayer;

import java.util.concurrent.FutureTask;
import java.util.concurrent.RunnableFuture;
import java.util.concurrent.Callable;

import android.os.Handler;

import android.os.Bundle;
import android.os.Message;
import android.os.IBinder;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.Context;
import android.content.ComponentName;
import android.app.Activity;
import android.view.Menu;
import android.view.KeyEvent;
import android.view.SurfaceView;
import android.view.Window;
import android.view.WindowManager;
import android.util.Log;

import android.widget.FrameLayout;

import com.lonelycoder.mediaplayer.CoreService.LocalBinder;

import android.util.Log;

public class GLWActivity extends Activity implements VideoRendererProvider {

    CoreService mService;

    GLWView mGLWView;
    FrameLayout mRoot;
    SurfaceView sv;
    boolean mBound;

    private void startGLW() {
        if(mGLWView != null)
            return;

        if(!mBound)
            return;

        mGLWView = new GLWView(getApplication(), GLWActivity.this);
        mRoot.addView(mGLWView);
    }

    private void stopGLW() {
        if(mGLWView != null) {
            mGLWView.destroy();
            mGLWView = null;
        }
    }

    @Override
    protected void onStart() {
        Log.d("Movian", "GLWActivity onStart");
        super.onStart();
        startGLW();
    }

    @Override
    protected void onStop() {
        Log.d("Movian", "GLWActivity onStop");
        super.onStop();
        stopGLW();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Log.d("Movian", "GLWActivity onCreate");
        super.onCreate(savedInstanceState);

        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);

        mRoot = new FrameLayout(this);
        setContentView(mRoot);

        Intent intent = new Intent(this, CoreService.class);
        bindService(intent, mConnection, Context.BIND_AUTO_CREATE);
    }

    @Override
    protected void onResume() {
        Log.d("Movian", "GLWActivity onResume");
        super.onResume();
        if(mGLWView != null)
            mGLWView.onResume();
    }

    @Override
    protected void onPause() {
        Log.d("Movian", "GLWActivity onPause");
        super.onPause();
        if(mGLWView != null)
            mGLWView.onPause();
    }

    @Override
    protected void onDestroy() {
        Log.d("Movian", "GLWActivity onDestroy");
        super.onDestroy();
    }


    private ServiceConnection mConnection = new ServiceConnection() {

        @Override
        public void onServiceConnected(ComponentName className,
                                       IBinder service) {
            Log.d("Movian", "GLWActivity onServiceConnected");
            mBound = true;
            startGLW();
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            Log.d("Movian", "GLWActivity onServiceDisconnected");
            mBound = false;
            stopGLW();
        }
    };



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
