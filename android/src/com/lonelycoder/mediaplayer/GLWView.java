package com.lonelycoder.mediaplayer;


import android.content.Context;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.opengl.GLSurfaceView;
import android.util.AttributeSet;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.KeyCharacterMap;

import android.text.method.MetaKeyKeyListener;

import android.widget.FrameLayout;

import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

class GLWView extends GLSurfaceView {

    private int glwId;
    private long keyMetaState;
    private StringBuilder composer = new StringBuilder();


    public GLWView(Context ctx, VideoRendererProvider vrp) {
        super(ctx);

        glwId = Core.glwCreate(vrp);

        setEGLContextClientVersion(2);
        setEGLConfigChooser(8,8,8,8,0,0);
        setRenderer(new Renderer());
        //        setRenderMode(RENDERMODE_WHEN_DIRTY);
        getHolder().setFormat(PixelFormat.TRANSLUCENT);
        setZOrderMediaOverlay(true);

        setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View v, MotionEvent e) {

                final int source = e.getSource();
                final int action = e.getAction();
                final float x = e.getX();
                final float y = e.getY();
                final long ts = e.getEventTime();

                queueEvent(new Runnable() {
                        public void run() {
                            Core.glwMotion(glwId, source, action, (int)x, (int)y, ts);
                        }
                    });
                return true;
            }
        });
    }


    public boolean keyDown(int keyCode, KeyEvent event) {

        keyMetaState = MetaKeyKeyListener.handleKeyDown(keyMetaState,
                                                        keyCode,
                                                        event);

        int c = event.getUnicodeChar(MetaKeyKeyListener.getMetaState(keyMetaState));
        keyMetaState = MetaKeyKeyListener.adjustMetaAfterKeypress(keyMetaState);

        boolean dead = false;
        if((c & KeyCharacterMap.COMBINING_ACCENT) != 0) {
            dead = true;
            c &= KeyCharacterMap.COMBINING_ACCENT_MASK;
        }

        if(composer.length() > 0) {
            char accent = composer.charAt(composer.length() - 1);
            int composed = KeyEvent.getDeadChar(accent, c);
            if(composed != 0) {
                c = composed;
                composer.setLength(composer.length() - 1);
            }
        }
        return Core.glwKeyDown(glwId, keyCode, dead ? 0 : c,
                               event.isShiftPressed());
    }

    public boolean keyUp(int keyCode, KeyEvent event) {
        return Core.glwKeyUp(glwId, keyCode);
    }

    public void destroy() {

        onPause();

        queueEvent(new Runnable() {
                public void run() {
                    Core.glwFini(glwId);
                }
            });
        Core.glwDestroy(glwId);
        glwId = 0;
    }

    @Override
    public void onResume() {
        Core.glwFlush(glwId);
        super.onResume();
    }

    private class Renderer implements GLSurfaceView.Renderer {

        public void onDrawFrame(GL10 gl) {
            Core.glwStep(glwId);
        }

        public void onSurfaceChanged(GL10 gl, int width, int height) {
            Core.glwResize(glwId, width, height);
        }

        public void onSurfaceCreated(GL10 gl, EGLConfig config) {
            Core.glwInit(glwId);
        }
    }
}
