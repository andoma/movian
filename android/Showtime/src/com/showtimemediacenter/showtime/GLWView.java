package com.showtimemediacenter.showtime;


import android.content.Context;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.opengl.GLSurfaceView;
import android.util.AttributeSet;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;


import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

class GLWView extends GLSurfaceView {

    public GLWView(Context ctx) {
        super(ctx);

        setEGLContextClientVersion(2);
        setEGLConfigChooser(8,8,8,8,0,0);
        setRenderer(new Renderer());
        getHolder().setFormat(PixelFormat.TRANSLUCENT);
    }

    @Override
    public boolean onTouchEvent(final MotionEvent e) {
        queueEvent(new Runnable() {
                public void run() {
                    Rect r = new Rect();
                    if(getGlobalVisibleRect(r))
                        STCore.glwMotion(e.getAction(),
                                         (int)e.getX() - r.left,
                                         (int)e.getY() - r.top);
                }
            });
        return true;
    }

    private static class Renderer implements GLSurfaceView.Renderer {
        public void onDrawFrame(GL10 gl) {
            STCore.glwStep();
        }

        public void onSurfaceChanged(GL10 gl, int width, int height) {
            STCore.glwResize(width, height);
        }

        public void onSurfaceCreated(GL10 gl, EGLConfig config) {
            STCore.glwInit();
        }
    }
}
