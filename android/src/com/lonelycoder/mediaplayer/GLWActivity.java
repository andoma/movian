package com.lonelycoder.mediaplayer;

import java.util.concurrent.FutureTask;
import java.util.concurrent.RunnableFuture;
import java.util.concurrent.Callable;

import android.os.Handler;

import android.net.Uri;

import android.os.Bundle;
import android.os.Message;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.Context;
import android.content.ComponentName;
import android.content.pm.PackageManager;
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

import android.os.Environment;
import android.content.ContentUris;
import android.content.Context;
import android.provider.DocumentsContract;
import android.provider.MediaStore;
import android.provider.MediaStore.MediaColumns;
import android.database.Cursor;

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
        Log.d("Movian", "onCreate");
        super.onCreate(savedInstanceState);

        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);

        startService(new Intent(this, CoreService.class));
    }

    public boolean onKeyUp(int keyCode, KeyEvent event) {

        if(mGLWView != null && mGLWView.keyUp(keyCode, event))
            return true;
        return super.onKeyUp(keyCode, event);
    }


    public boolean onKeyDown(int keyCode, KeyEvent event) {

        if(mGLWView != null && mGLWView.keyDown(keyCode, event))
            return true;
        return super.onKeyDown(keyCode, event);
    }

    @Override
    protected void onResume() {
        Log.d("Movian", "onResume");
        super.onResume();

        Handler h = new Handler(new Handler.Callback() {
                public boolean handleMessage(Message msg) {
                    Intent intent = getIntent();
                    Uri uri = intent.getData();
                    if(uri == null)
                        uri = intent.getParcelableExtra("uri");

                    if(uri != null) {
                        String u = getRealPathFromUri(uri);
                        Core.openUri(u != null ? u : uri.toString());
                    }
                    return true;
                }
            });

        h.sendEmptyMessage(0);
    }

    @Override
    protected void onPause() {
        Log.d("Movian", "onPause");
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        Log.d("Movian", "onDestroy");
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

    @Override
    public void sysHome() {
        runOnUiThread(new Runnable() {
                public void run() {
                    Intent startMain = new Intent(Intent.ACTION_MAIN);
                    startMain.addCategory(Intent.CATEGORY_HOME);
                    startMain.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                    startActivity(startMain);
                }
            });
    }

    @Override
    public void askPermission(final String permission) {
        runOnUiThread(new Runnable() {
                public void run() {
                    requestPermissions(new String[] {permission}, 1);
                }
            });
    }

    @Override
    public void onRequestPermissionsResult(int requestCode,
                                           String permissions[],
                                           int[] grantResults) {
        Core.permissionResult(grantResults[0] ==
                              PackageManager.PERMISSION_GRANTED);
    }

    public String getRealPathFromUri(final Uri uri) {
        // DocumentProvider
        if (DocumentsContract.isDocumentUri(this, uri)) {
            // ExternalStorageProvider
            if (isExternalStorageDocument(uri)) {
                final String docId = DocumentsContract.getDocumentId(uri);
                final String[] split = docId.split(":");
                final String type = split[0];

                if ("primary".equalsIgnoreCase(type)) {
                    return "es:///" + split[1];
                }
            }
            // DownloadsProvider
            else if (isDownloadsDocument(uri)) {

                final String id = DocumentsContract.getDocumentId(uri);
                final Uri contentUri = ContentUris.withAppendedId(
                        Uri.parse("content://downloads/public_downloads"), Long.valueOf(id));

                return getDataColumn(this, contentUri, null, null);
            }
            // MediaProvider
            else if (isMediaDocument(uri)) {
                final String docId = DocumentsContract.getDocumentId(uri);
                final String[] split = docId.split(":");
                final String type = split[0];

                Uri contentUri = null;
                if ("image".equals(type)) {
                    contentUri = MediaStore.Images.Media.EXTERNAL_CONTENT_URI;
                } else if ("video".equals(type)) {
                    contentUri = MediaStore.Video.Media.EXTERNAL_CONTENT_URI;
                } else if ("audio".equals(type)) {
                    contentUri = MediaStore.Audio.Media.EXTERNAL_CONTENT_URI;
                }

                final String selection = "_id=?";
                final String[] selectionArgs = new String[]{
                        split[1]
                };

                return getDataColumn(this, contentUri, selection, selectionArgs);
            }
        }
        // MediaStore (and general)
        else if ("content".equalsIgnoreCase(uri.getScheme())) {

            // Return the remote address
            if (isGooglePhotosUri(uri))
                return uri.getLastPathSegment();

            return getDataColumn(this, uri, null, null);
        }
        // File
        else if ("file".equalsIgnoreCase(uri.getScheme())) {
            return uri.getPath();
        }

        return null;
    }

    private String getDataColumn(Context context, Uri uri, String selection,
                                 String[] selectionArgs) {

        Cursor cursor = null;
        final String column = "_data";
        final String[] projection = {
                column
        };

        try {
            cursor = context.getContentResolver().query(uri, projection, selection, selectionArgs,
                    null);
            if (cursor != null && cursor.moveToFirst()) {
                final int index = cursor.getColumnIndexOrThrow(column);
                return cursor.getString(index);
            }
        } finally {
            if (cursor != null)
                cursor.close();
        }
        return null;
    }

    private boolean isExternalStorageDocument(Uri uri) {
        return "com.android.externalstorage.documents".equals(uri.getAuthority());
    }

    private boolean isDownloadsDocument(Uri uri) {
        return "com.android.providers.downloads.documents".equals(uri.getAuthority());
    }

    private boolean isMediaDocument(Uri uri) {
        return "com.android.providers.media.documents".equals(uri.getAuthority());
    }

    private boolean isGooglePhotosUri(Uri uri) {
        return "com.google.android.apps.photos.content".equals(uri.getAuthority());
    }
}

