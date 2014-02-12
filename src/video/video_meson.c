#include <libavutil/mathematics.h> // XXX


#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

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
//static hts_mutex_t meson_video_mutex;

/**
 * meson_video_cond is waited on for decoders that want to get access to hardware
 */
//static hts_cond_t meson_video_cond;

static int meson_video_fd = -1;
static int meson_ts_epoch;

static prop_t *prop_active;
static prop_t *prop_buffer;

typedef struct meson_video_decoder {
  h264_annexb_ctx_t mvd_annexb;
  media_pipe_t *mvd_mp;

  //  int mvd_running;

  int mvd_fd;

} meson_video_decoder_t;


//static meson_video_decoder_t *mvd_current;


/**
 *
 */
static void
writeval(const char *str, int val)
{
  FILE *fp = fopen(str, "w");
  if(fp == NULL)
    return;

  fprintf(fp, "%d\n", val);
  fclose(fp);
}


static void
set_tsync_enable(int enable)
{
  writeval("/sys/class/tsync/enable", enable);
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
  TRACE(TRACE_INFO, "MESON", "%s: Audio clock set to %lld  r=%d",
        mp->mp_name, pts, r);
}


static void
snapshot_test(media_pipe_t *mp)
{
  int64_t ts = showtime_get_ts();
  int fd = open("/dev/amvideocap0",O_RDWR);
  int ret;
  int w = 256;
  int h = 256;

  if(fd < 0) {
    perror("Unable to open amvideocap");
    return;
  }

  pixmap_t *pm = calloc(1, sizeof(pixmap_t));
  pm->pm_refcount = 1;
  pm->pm_width = w;
  pm->pm_height = h;


#if 0
  pm->pm_linesize = w * 4;
  pm->pm_type = PIXMAP_BGR32;
  ioctl(fd, AMVIDEOCAP_IOW_SET_WANTFRAME_FORMAT, GE2D_FMT_S32_RGBA);
#else
  pm->pm_linesize = w * 3;
  pm->pm_type = PIXMAP_RGB24;
  ioctl(fd, AMVIDEOCAP_IOW_SET_WANTFRAME_FORMAT, GE2D_FORMAT_S24_BGR);
#endif

  ioctl(fd,AMVIDEOCAP_IOW_SET_WANTFRAME_WIDTH, w);
  ioctl(fd,AMVIDEOCAP_IOW_SET_WANTFRAME_HEIGHT, h);
  ioctl(fd, AMVIDEOCAP_IOW_SET_WANTFRAME_AT_FLAGS, CAP_FLAG_AT_CURRENT);


  int size = pm->pm_linesize * h;

  ret = ioctl(fd, AMVIDEOCAP_IOW_SET_START_CAPTURE, 10000);
  if(ret) {
    close(fd);
    pixmap_release(pm);
    return;
  }
  void *p = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);

  pm->pm_flags |= PIXMAP_MMAPED;
  pm->pm_data = p;
  close(fd);

  TRACE(TRACE_DEBUG, "SNAPSHOT", "Finished in %dus", (int)(showtime_get_ts() - ts));

  frame_info_t fi;
  memset(&fi, 0, sizeof(fi));
  fi.fi_type = 'PM';
  fi.fi_pts = PTS_UNSET;
  fi.fi_data[0] = (void *)pm;

  mp->mp_video_frame_deliver(&fi, mp->mp_video_frame_opaque);
  pixmap_release(pm);
}

#if 0
static void
zero_img(media_pipe_t *mp)
{
  // Snapshot here
  frame_info_t fi;
  memset(&fi, 0, sizeof(fi));
  fi.fi_type = 'ZERO';

  mp->mp_video_frame_deliver(&fi, mp->mp_video_frame_opaque);
}
#endif

/**
 *
 */
static void
amstream_stop(meson_video_decoder_t *mvd, int snapshot)
{
  media_pipe_t *mp = mvd->mvd_mp;

  TRACE(TRACE_DEBUG, "MESON", "%s: About to surrender video decoder",
        mp->mp_name);

  if(snapshot)
    snapshot_test(mp);

  assert(mvd->mvd_fd != -1);

  hts_mutex_lock(&mp->mp_clock_mutex);
  mp->mp_set_audio_clock = NULL;
  hts_mutex_unlock(&mp->mp_clock_mutex);

  prop_set_string(prop_active, "Closed");

  if(snapshot)
    usleep(40000);
  close(meson_video_fd);
  mvd->mvd_fd = -1;
  meson_video_fd = -1;
  meson_ts_epoch = 0;
  TRACE(TRACE_DEBUG, "MESON", "%s: Released video decoder", mp->mp_name);
}


/**
 *
 */
static int
amstream_start(media_pipe_t *mp, video_decoder_t *vd,
               meson_video_decoder_t *mvd)
{
  meson_ts_epoch = 0;

  int fd = open("/dev/amstream_vbuf", O_RDWR);
  if(fd == -1) {
    return 1;
  }

  if(ioctl(fd, AMSTREAM_IOC_VFORMAT, VFORMAT_H264)) {
    perror("Unable to enable h264 in the hardware of love");
    close(fd);
    return 1;
  }
  mvd->mvd_fd = fd;
  meson_video_fd = fd;
  static int opencnt;
  opencnt++;
  char buf[40];
  snprintf(buf, sizeof(buf), "Open-%d", opencnt);

  prop_set_string(prop_active, buf);

  int64_t pts;

  pts = av_rescale_q(mp->mp_audio_clock, AV_TIME_BASE_Q, mpeg_tc);
  TRACE(TRACE_INFO, "MESON", "%s: Audio initially set to %lld",
        mp->mp_name, pts);

  ioctl(mvd->mvd_fd, AMSTREAM_IOC_SET_PCRSCR, pts);

  hts_mutex_lock(&mp->mp_clock_mutex);
  mp->mp_set_audio_clock = meson_set_pcr;
  hts_mutex_unlock(&mp->mp_clock_mutex);

  return 0;
}


/**
 *
 */
static void
meson_video_flush(struct media_codec *mc, struct video_decoder *vd)
{
  meson_video_decoder_t *mvd = mc->opaque;
  media_pipe_t *mp = mc->mp;
  printf("%s: Flush\n", mp->mp_name);


  if(mvd->mvd_fd != -1) {
    // Fix flush code
    TRACE(TRACE_ERROR, "MESON", "XXX: Need to implement flush");
  }

  mvd->mvd_annexb.extradata_injected = 0;
}


/**
 *
 */
static int
submit_au(meson_video_decoder_t *mvd, void *data, size_t len,
	  int drop_non_ref, video_decoder_t *vd, int64_t pts,
          media_pipe_t *mp)
{
  const static AVRational mpeg_tc = {1, 90000};

  while(1) {

    // Wait for buffer availability

    struct am_io_param aip;

    if(vd->vd_activation >= VIDEO_ACTIVATION_PRELOAD) {
      // No longer in decoding mode, get out of here ASAP
      hts_mutex_unlock(&mp->mp_mutex);
      amstream_stop(mvd, 1);
      hts_mutex_lock(&mp->mp_mutex);
      return 1;
    }

    if(vd->vd_run == 0) {
      // We are not supposed to run anymore so bail out directly
      hts_mutex_unlock(&mp->mp_mutex);
      amstream_stop(mvd, 0);
      hts_mutex_lock(&mp->mp_mutex);
      return 1;
    }

    if(ioctl(meson_video_fd, AMSTREAM_IOC_VB_STATUS, &aip)) {
      TRACE(TRACE_ERROR, "MESON", "ioctl(AMSTREAM_IOC_VB_STATUS) failed");
      break;
    }

    /*
     * We don't wanna keep to much data in the kernel side video buffer
     * so limit to 1MB
     */

    if(0) prop_set_int(prop_buffer, aip.status.data_len);

    if(aip.status.data_len < 1000000)
      break;

    /*
     * Sleep for 100ms (worst case unless we wake up for other reason)
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
  return 0;
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

  int cnt = 0;

  while(mvd->mvd_fd == -1) {

    if(!vd->vd_run || vd->vd_activation >= VIDEO_ACTIVATION_PRELOAD)
      return; // Not supposed to run anymore, get out of here

    int r = amstream_start(mp, vd, mvd);
    if(r) {
      hts_mutex_unlock(&mp->mp_mutex);
      usleep(10000);
      cnt++;
      if(cnt == 20)
        printf("%s: Failed to acquire decoder\n", mp->mp_name);
      hts_mutex_lock(&mp->mp_mutex);
    }
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

  if(submit_au(mvd, data, size, mb->mb_skip == 1, vd, mb->mb_pts, mp))
    return;
  mp->mp_set_video_codec('mesn', mc, mp->mp_video_frame_opaque);
}



/**
 *
 */
static void
meson_video_close(struct media_codec *mc)
{
  meson_video_decoder_t *mvd = mc->opaque;

  if(mvd->mvd_fd != -1)
    amstream_stop(mvd, 0);

  h264_to_annexb_cleanup(&mvd->mvd_annexb);
  free(mvd);
}

/**
 *
 */
static void
meson_video_relinqush(struct media_codec *mc, struct video_decoder *vd)
{
  meson_video_decoder_t *mvd = mc->opaque;

  if(mvd->mvd_fd == -1)
    return;

  amstream_stop(mvd, 1);
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
  mvd->mvd_fd = -1;

  if(mc->codec_id == CODEC_ID_H264 && mcp != NULL && mcp->extradata_size)
    h264_to_annexb_init(&mvd->mvd_annexb, mcp->extradata, mcp->extradata_size);

  mc->opaque = mvd;
  mc->decode_locked = meson_video_decode_locked;
  mc->close  = meson_video_close;
  mc->flush  = meson_video_flush;
  mc->relinquish = meson_video_relinqush;
  return 0;
}


/**
 *
 */
static void
meson_video_init(void)
{

  prop_t *p = prop_get_global();
  p = prop_create(p, "meson");
  p = prop_create(p, "video");
  prop_active = prop_create(p, "active");
  prop_buffer = prop_create(p, "buffer");

  set_tsync_enable(0);
}

REGISTER_CODEC(meson_video_init, meson_video_open, 10);
