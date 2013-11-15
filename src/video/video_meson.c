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

#include <libavutil/common.h>

#include <linux/amports/amstream.h>
#include <linux/amports/vformat.h>


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


/**
 *
 */
static void
meson_set_pcr(media_pipe_t *mp, int64_t pts, int epoch, int discont)
{
  const static AVRational mpeg_tc = {1, 90000};

  if(epoch == meson_ts_epoch && !discont)
    return;

  meson_ts_epoch = epoch;

  pts = av_rescale_q(pts, AV_TIME_BASE_Q, mpeg_tc);

  int r = ioctl(meson_video_fd, AMSTREAM_IOC_SET_PCRSCR, pts);
  printf("Audio clock set to %lld  r=%d\n", pts, r);
}


/**
 *
 */
static void
stop_current(void)
{
  media_pipe_t *mp = mvd_current->mvd_mp;

  // Snapshot here
  frame_info_t fi;
  memset(&fi, 0, sizeof(fi));
  fi.fi_type = 'ZERO';
  mp->mp_video_frame_deliver(&fi, mp->mp_video_frame_opaque);

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
  printf("%s: New stream\n", mp->mp_name);
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
      stop_current();
    hts_mutex_unlock(&meson_video_mutex);
  }
  mvd->mvd_annexb.extradata_injected = 0;
}


/**
 *
 */
static void
submit_au(meson_video_decoder_t *mvd, void *data, size_t len,
	  int drop_non_ref, video_decoder_t *vd, int64_t pts)
{
  const static AVRational mpeg_tc = {1, 90000};

  if(pts != PTS_UNSET) {
    pts = av_rescale_q(pts, AV_TIME_BASE_Q, mpeg_tc);
    ioctl(meson_video_fd, AMSTREAM_IOC_TSTAMP, pts);
  }

  while(len > 0) {
    if(!meson_didwrite) {
      printf("video write\n");
      meson_didwrite = 1;
    }
    int r = write(meson_video_fd, data, len);
    if(r == -1) {
      if(errno == EAGAIN) {
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
meson_video_decode(struct media_codec *mc, struct video_decoder *vd,
                   struct media_queue *mq, struct media_buf *mb, int reqsize)
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
      stop_current();


    start_new(mp);
    mvd_current = mvd;

    hts_mutex_lock(&mp->mp_clock_mutex);
    mp->mp_set_audio_clock = meson_set_pcr;
    hts_mutex_unlock(&mp->mp_clock_mutex);
  }

  if(mvd->mvd_annexb.extradata != NULL &&
     mvd->mvd_annexb.extradata_injected == 0) {
    submit_au(mvd,  mvd->mvd_annexb.extradata,
	      mvd->mvd_annexb.extradata_size, 0, vd, PTS_UNSET);
    mvd->mvd_annexb.extradata_injected = 1;
  }

  uint8_t *data = mb->mb_data;
  size_t size = mb->mb_size;

  h264_to_annexb(&mvd->mvd_annexb, &data, &size);
  submit_au(mvd, data, size, mb->mb_skip == 1, vd, mb->mb_pts);
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
    stop_current();
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
  mc->decode = meson_video_decode;
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
