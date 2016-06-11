#ifndef _AMLDEC_H_
#define _AMLDEC_H_

#include "amlqueue.h"
#include "amlion.h"
#include <amcodec/codec.h>
#include <amcodec/codec.h>
#include <time.h>

#define TRICKMODE_NONE  0x00
#define TRICKMODE_I     0x01
#define TRICKMODE_FFFB  0x02

#define EXTERNAL_PTS    1
#define SYNC_OUTSIDE    2

#define PTS_FREQ       90000
#define AV_SYNC_THRESH PTS_FREQ * 1

#define MIN_FRAME_QUEUE_SIZE  16
#define MAX_WRITE_QUEUE_SIZE  1

#define MAX_HEADER_SIZE 4096

typedef struct {
  char *data[MAX_HEADER_SIZE];
  int size;
} AMLHeader;

typedef struct
{
  double pts;
} AMLFramePrivate;

typedef struct {
  AVClass *av_class;
  codec_para_t codec;
  int first_packet;
  double last_checkin_pts;
  AVBSFContext *bsf;
  PacketQueue writequeue;
  PacketQueue framequeue;
  struct buf_status buffer_status;
  struct vdec_status decoder_status;
  AMLHeader header;
  AMLIonContext ion_context;
  int64_t last_pts;
  int running;
  unsigned long last_decode_time;
} AMLDecodeContext;

// Functions prototypes
int ffmal_init_bitstream(AVCodecContext *avctx);
int ffaml_write_pkt_data(AVCodecContext *avctx, AVPacket *avpkt);
void ffaml_checkin_packet_pts(AVCodecContext *avctx, AVPacket *avpkt);
void ffaml_create_prefeed_header(AVCodecContext *avctx, char *extradata, int extradatasize);
void ffaml_log_decoder_info(AVCodecContext *avctx);

#endif /* _AMLDEC_H_ */
