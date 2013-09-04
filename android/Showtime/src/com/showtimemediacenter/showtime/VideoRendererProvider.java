package com.showtimemediacenter.showtime;

interface VideoRendererProvider {
    public VideoRenderer createVideoRenderer()  throws Exception;
    public void destroyVideoRenderer(VideoRenderer vr);
}
