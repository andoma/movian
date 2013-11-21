#include <libavutil/mathematics.h> // XXX


#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>


#include "showtime.h"
#include "video_decoder.h"
#include "h264_annexb.h"
#include "misc/pixmap.h"

#include <libavutil/common.h>

#include <linux/amports/amstream.h>
#include <linux/amports/vformat.h>

#include "src/arch/meson/Amvideocap.h"

#include "ge2dfmt.h"

/**
 * Meson only supports one video decoder open at the same time.
 * So we just keep stuff global here
 */
static hts_mutex_t meson_video_mutex;
static int meson_video_fd = -1;
static int meson_ts_epoch;
static int meson_didwrite;


typedef struct meson_video_decoder {
  h264_annexb_ctx_t mvd_annexb;
  media_pipe_t *mvd_mp;
} meson_video_decoder_t;


static meson_video_decoder_t *mvd_current;

static int
set_tsync_enable(int enable)
{
  int fd;
  const char *path = "/sys/class/tsync/enable";
  char  bcmd[16];
  fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
  if (fd >= 0) {
    sprintf(bcmd, "%d", enable);
    write(fd, bcmd, strlen(bcmd));
    close(fd);
    return 0;
  }
  return -1;
}

const static AVRational mpeg_tc = {1, 90000};

/**
 *
 */
static void
meson_set_pcr(media_pipe_t *mp, int64_t pts, int epoch, int discont)
{
  if(epoch == meson_ts_epoch && !discont)
    return;

  meson_ts_epoch = epoch;

  pts = av_rescale_q(pts, AV_TIME_BASE_Q, mpeg_tc);
  int r = ioctl(meson_video_fd, AMSTREAM_IOC_SET_PCRSCR, pts);
  TRACE(TRACE_INFO, "MESON", "Audio clock set to %lld  r=%d", pts, r);
}


static void
snapshot_test(media_pipe_t *mp)
{
  int fd = open("/dev/amvideocap0",O_RDWR);
  int ret;

  int w = 512;
  int h = 512;

  if(fd < 0) {
    perror("Unable to open amvideocap");
    return;
  }

  ret = ioctl(fd, AMVIDEOCAP_IOW_SET_WANTFRAME_FORMAT, GE2D_FORMAT_S24_BGR);

  if(w>0){
    ret=ioctl(fd,AMVIDEOCAP_IOW_SET_WANTFRAME_WIDTH,w);
  }
  if(h>0){
    ret=ioctl(fd,AMVIDEOCAP_IOW_SET_WANTFRAME_HEIGHT,h);
  }

  ret=ioctl(fd, AMVIDEOCAP_IOW_SET_WANTFRAME_AT_FLAGS, CAP_FLAG_AT_CURRENT);

  pixmap_t *pm = calloc(1, sizeof(pixmap_t));
  pm->pm_refcount = 1;
  pm->pm_width = w;
  pm->pm_height = h;
#if 0
  pm->pm_linesize = w * 4;
  pm->pm_type = PIXMAP_BGR32;
#else
  pm->pm_linesize = w * 3;
  pm->pm_type = PIXMAP_RGB24;
#endif



  int size = pm->pm_linesize * h;

  pm->pm_data = malloc(size);
  ret = read(fd, pm->pm_data, size);

  printf("Read %d bytes (of %d)\n", ret, size);
#if 0
  ret = ioctl(fd,AMVIDEOCAP_IOR_GET_FRAME_WIDTH,&w);
  ret = ioctl(fd,AMVIDEOCAP_IOR_GET_FRAME_HEIGHT,&h);
#endif
  close(fd);

  // Snapshot here
  frame_info_t fi;
  memset(&fi, 0, sizeof(fi));
  fi.fi_type = 'PM';
  fi.fi_data[0] = (void *)pm;

  mp->mp_video_frame_deliver(&fi, mp->mp_video_frame_opaque);

  pixmap_release(pm);
}


/**
 *
 */
static void
stop_current(int snapshot)
{
  media_pipe_t *mp = mvd_current->mvd_mp;

  if(snapshot)
    snapshot_test(mp);


  printf("%s Stopped\n", mp->mp_name);

  hts_mutex_lock(&mp->mp_clock_mutex);
  mp->mp_set_audio_clock = NULL;
  hts_mutex_unlock(&mp->mp_clock_mutex);

  close(meson_video_fd);
  meson_video_fd = -1;
  meson_ts_epoch = 0;
  meson_didwrite = 0;
  mvd_current = NULL;
}


/**
 *
 */
static int
start_new(media_pipe_t *mp)
{
  meson_ts_epoch = 0;
  meson_didwrite = 0;
  TRACE(TRACE_DEBUG, "MESON", "%s: New stream", mp->mp_name);

  int fd = open("/dev/amstream_vbuf", O_RDWR);
  if(fd == -1) {
    perror("open(/dev/amstream_vbuf");
    return 1;
  }

  if(ioctl(fd, AMSTREAM_IOC_VFORMAT, VFORMAT_H264)) {
    perror("Unable to enable h264 in the hardware of love");
    close(fd);
    return 1;
  }
  meson_video_fd = fd;

  int64_t pts;

  pts = av_rescale_q(mp->mp_audio_clock_avtime, AV_TIME_BASE_Q, mpeg_tc);
  TRACE(TRACE_INFO, "MESON", "Audio initially set to %lld", pts);
  ioctl(meson_video_fd, AMSTREAM_IOC_SET_PCRSCR, pts);

  return 0;
}


/**
 *
 */
static void
meson_video_flush(struct media_codec *mc, struct video_decoder *vd, int lasting)
{
  meson_video_decoder_t *mvd = mc->opaque;
  media_pipe_t *mp = mc->mp;

  if(lasting) {
    printf("%s: Long flush\n", mp->mp_name);
    hts_mutex_lock(&meson_video_mutex);
    if(mvd == mvd_current)
      stop_current(1);
    hts_mutex_unlock(&meson_video_mutex);
  }
  mvd->mvd_annexb.extradata_injected = 0;
}


/**
 *
 */
static void
submit_au(meson_video_decoder_t *mvd, void *data, size_t len,
	  int drop_non_ref, video_decoder_t *vd, int64_t pts,
          media_pipe_t *mp)
{
  const static AVRational mpeg_tc = {1, 90000};


  while(1) {

    // Wait for buffer availability

    struct am_io_param aip;

    if(vd->vd_run == 0)
      return;  // We are not supposed to run anymore so bail out directly

    if(ioctl(meson_video_fd, AMSTREAM_IOC_VB_STATUS, &aip)) {
      TRACE(TRACE_ERROR, "MESON", "ioctl(AMSTREAM_IOC_VB_STATUS) failed");
      break; // HUH?
    }

    /*
     * We don't wanna keep to much data in the kernel side video buffer
     * so limit to 1MB
     */

    if(aip.status.data_len < 1000000)
      break;

    /* Sleep for 100ms (worst case unless we wake up for other reason)
     * 1MB worth of data for 100ms is equiv to a bitrate of 80MBit/s
     * so it should be somewhat safe
     */
    hts_cond_wait_timeout(&mp->mp_video.mq_avail, &mp->mp_mutex, 100);
  }

  if(pts != PTS_UNSET) {
    pts = av_rescale_q(pts, AV_TIME_BASE_Q, mpeg_tc);
    ioctl(meson_video_fd, AMSTREAM_IOC_TSTAMP, pts);
  }

  while(len > 0) {
    if(!meson_didwrite) {
      TRACE(TRACE_DEBUG, "MESON", "video write");
      meson_didwrite = 1;
    }
    int r = write(meson_video_fd, data, len);
    if(r == -1) {
      if(errno == EAGAIN) {
        TRACE(TRACE_ERROR, "MESON", "video write blocked for 100ms this is bad");
        usleep(10000);
        continue;
      }

      perror("Write error");
      break;
    }
    len -= r;
    data += r;
  }
}


/**
 *
 */
static void
meson_video_decode_locked(struct media_codec *mc, struct video_decoder *vd,
                          struct media_queue *mq, struct media_buf *mb)
{
  meson_video_decoder_t *mvd = mc->opaque;
  media_pipe_t *mp = mc->mp;

  hts_mutex_lock(&meson_video_mutex);

  if(mvd != mvd_current) {
    
    int cnt = 0;
    while(mvd_current != NULL) {
      hts_mutex_unlock(&meson_video_mutex);
      usleep(10000);
      hts_mutex_lock(&meson_video_mutex);
      cnt++;
      if(cnt == 50)
	break;
    }

    if(mvd_current != NULL)
      stop_current(0);


    start_new(mp);
    mvd_current = mvd;

    hts_mutex_lock(&mp->mp_clock_mutex);
    mp->mp_set_audio_clock = meson_set_pcr;
    hts_mutex_unlock(&mp->mp_clock_mutex);
  }

  if(mvd->mvd_annexb.extradata != NULL &&
     mvd->mvd_annexb.extradata_injected == 0) {
    submit_au(mvd,  mvd->mvd_annexb.extradata,
	      mvd->mvd_annexb.extradata_size, 0, vd, PTS_UNSET, mp);
    mvd->mvd_annexb.extradata_injected = 1;
  }

  uint8_t *data = mb->mb_data;
  size_t size = mb->mb_size;

  h264_to_annexb(&mvd->mvd_annexb, &data, &size);
  submit_au(mvd, data, size, mb->mb_skip == 1, vd, mb->mb_pts, mp);
  mp->mp_set_video_codec('mesn', mc, mp->mp_video_frame_opaque);
  hts_mutex_unlock(&meson_video_mutex);
}



/**
 *
 */
static void
meson_video_close(struct media_codec *mc)
{
  meson_video_decoder_t *mvd = mc->opaque;
  ///  media_pipe_t *mp = mc->mp;

  hts_mutex_lock(&meson_video_mutex);
  if(mvd == mvd_current)
    stop_current(0);
  hts_mutex_unlock(&meson_video_mutex);

  h264_to_annexb_cleanup(&mvd->mvd_annexb);
  free(mvd);
}


/**
 *
 */
static int
meson_video_open(media_codec_t *mc, const media_codec_params_t *mcp,
		 media_pipe_t *mp)
{
  if(mc->codec_id != CODEC_ID_H264)
    return 1;

  meson_video_decoder_t *mvd = calloc(1, sizeof(meson_video_decoder_t));
  mvd->mvd_mp = mp;

  if(mc->codec_id == CODEC_ID_H264 && mcp != NULL && mcp->extradata_size)
    h264_to_annexb_init(&mvd->mvd_annexb, mcp->extradata, mcp->extradata_size);

  mc->opaque = mvd;
  mc->decode_locked = meson_video_decode_locked;
  mc->close  = meson_video_close;
  mc->flush  = meson_video_flush;

  return 0;
}


/**
 *
 */
static void
meson_video_init(void)
{
  hts_mutex_init(&meson_video_mutex);
  set_tsync_enable(1);
}

REGISTER_CODEC(meson_video_init, meson_video_open, 10);
