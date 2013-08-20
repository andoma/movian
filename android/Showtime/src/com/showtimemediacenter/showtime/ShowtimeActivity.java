package com.showtimemediacenter.showtime;

import android.os.Handler;

import android.os.Bundle;
import android.content.Intent;
import android.app.Activity;
import android.view.Menu;
import android.view.KeyEvent;


import android.util.Log;

// import com.showtimemediacenter.showtime.Subscription;

public class ShowtimeActivity extends Activity {

    GLWView mView;
    private Subscription s, s2;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Log.d("Showtime", "alpha");
        super.onCreate(savedInstanceState);
        Log.d("Showtime", "alpha-2");
        startService(new Intent(this, ShowtimeService.class));
        Log.d("Showtime", "alpha-3");
        mView = new GLWView(getApplication());
        setContentView(mView);

        final Handler handler = new Handler();
        handler.postDelayed(new Runnable() {
                @Override
                public void run() {

                    s = new Subscription("navigators.current.currentpage.url",
                                         new SubCallback() {
                                             public void set(String str) {
                                                 Log.d("Showtime", "The URL is " + str);
                                             }
                                         });

                    s2 = new Subscription("navigators.current.canGoBack",
                                          new SubCallback() {
                                              public void set(String str) {
                                                  Log.d("Showtime", "Can go back(str) = " + str);
                                              }
                                          });
                }
            }, 100);

    }

    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if(STCore.glwKeyDown(keyCode))
            return true;
        return super.onKeyDown(keyCode, event);
    }
}
