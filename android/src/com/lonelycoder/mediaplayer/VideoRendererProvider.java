package com.lonelycoder.mediaplayer;

interface VideoRendererProvider {
    public VideoRenderer createVideoRenderer()  throws Exception;
    public void destroyVideoRenderer(VideoRenderer vr);

    public void disableScreenSaver();
    public void enableScreenSaver();
}
