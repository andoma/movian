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

typedef struct meson_video_decoder {

  int mvd_fd;

} meson_video_decoder_t;


/**
 *
 */
static void
meson_video_flush(struct media_codec *mc, struct video_decoder *vd, int lasting)
{

}

/**
 *
 */
static void
meson_video_decode(struct media_codec *mc, struct video_decoder *vd,
                   struct media_queue *mq, struct media_buf *mb, int reqsize)
{
}


/**
 *
 */
static void
meson_video_close(struct media_codec *mc)
{
  meson_video_decoder_t *mvd = mc->opaque;
  close(mvd->mvd_fd);
  free(mvd);
}


/**
 *
 */
static int
meson_video_open(media_codec_t *mc, const const media_codec_params_t *mcp,
		 media_pipe_t *mp)
{
  switch(mc->codec_id) {
  default:
    return 1;

  case CODEC_ID_H264:
    break;
  }


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


  meson_video_decoder_t *mvd = calloc(1, sizeof(meson_video_decoder_t));
  //  snprintf(cd->cd_name, sizeof(cd->cd_name), "%s", mp->mp_name);
  //  cd->mcd_ve = ve;
  mvd->mvd_fd = fd;

  printf("meson: Opened FD\n");

  mc->opaque = mvd;
  mc->decode = meson_video_decode;
  mc->close  = meson_video_close;
  mc->flush  = meson_video_flush;
  return 0;
}


REGISTER_CODEC(NULL, meson_video_open, 10);
