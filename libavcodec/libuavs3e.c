#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <float.h>
//#include <dlfcn.h>
#include "avcodec.h"
#include "internal.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"
//#include "mxnet_api.h"

#include "uavs3e/uavs3e.h" 

#define MAX_BUMP_FRM_CNT           (8 <<1)
#define MAX_BS_BUF                 (32*1024*1024)

static const int color_primaries_tab[10] = {
    AVCOL_PRI_RESERVED0   , // 0
    AVCOL_PRI_BT709       ,	// 1
    AVCOL_PRI_UNSPECIFIED , // 2
    AVCOL_PRI_RESERVED    , // 3
    AVCOL_PRI_BT470M	  , // 4
    AVCOL_PRI_BT470BG	  ,	// 5
    AVCOL_PRI_SMPTE170M   ,	// 6
    AVCOL_PRI_SMPTE240M   , // 7
    AVCOL_PRI_FILM        ,	// 8
    AVCOL_PRI_BT2020      	// 9  
};
	
static const int color_transfer_tab[15] = {
    AVCOL_TRC_RESERVED0    , // 0
    AVCOL_TRC_BT709        , // 1
    AVCOL_TRC_UNSPECIFIED  , // 2
    AVCOL_TRC_RESERVED     , // 3
    AVCOL_TRC_GAMMA22      , // 4
    AVCOL_TRC_GAMMA28      , // 5
    AVCOL_TRC_SMPTE170M    , // 6
    AVCOL_TRC_SMPTE240M    , // 7
    AVCOL_TRC_LINEAR       , // 8
    AVCOL_TRC_LOG          , // 9
    AVCOL_TRC_LOG_SQRT     , // 10
    AVCOL_TRC_BT2020_12    , // 11
    AVCOL_TRC_SMPTE2084    , // 12
    AVCOL_TRC_UNSPECIFIED  , // 13
    AVCOL_TRC_ARIB_STD_B67   // 14
};

static const int color_matrix_tab[12] = {
    AVCOL_SPC_RESERVED     , // 0
    AVCOL_SPC_BT709        , // 1
    AVCOL_SPC_UNSPECIFIED  , // 2
    AVCOL_SPC_RESERVED     , // 3
    AVCOL_SPC_FCC          , // 4
    AVCOL_SPC_BT470BG      , // 5
    AVCOL_SPC_SMPTE170M    , // 6
    AVCOL_SPC_SMPTE240M    , // 7
    AVCOL_SPC_BT2020_NCL   , // 8
    AVCOL_SPC_BT2020_CL    , // 9
    AVCOL_SPC_UNSPECIFIED  , // 10
    AVCOL_SPC_UNSPECIFIED    // 11
}; 

typedef struct UAVS3EContext {
    AVClass        *class;
    void*         handle;
    enc_cfg_t     avs3_cfg;
	
    /* configuration */
    int threads_wpp;
    int threads_frm;
    int baseQP;  
	int baseCRF;
    int speed_level;
    int intra_period;
    int hdr;
    int close_gop;
    char* hdr_ext;
    int rc_type;
} UAVS3EContext;

static int uavs3e_init(AVCodecContext *avctx)
{
    UAVS3EContext *ec = avctx->priv_data;
    uavs3e_load_default_cfg(&ec->avs3_cfg);

    if (avctx->pix_fmt == AV_PIX_FMT_YUV420P) {
        ec->avs3_cfg.bit_depth_input    = 8;
        ec->avs3_cfg.bit_depth_internal = 8;
    } else if (avctx->pix_fmt == AV_PIX_FMT_YUV420P10LE){
#if (BIT_DEPTH == 10)
        ec->avs3_cfg.bit_depth_input    = 10;
        ec->avs3_cfg.bit_depth_internal = 10;
#else
        return -1;
#endif
    } else {
        return -1;
    }

    ec->avs3_cfg.horizontal_size    = avctx->coded_width;
    ec->avs3_cfg.vertical_size      = avctx->coded_height;
    ec->avs3_cfg.fps_num            = avctx->time_base.den;
    ec->avs3_cfg.fps_den            = avctx->time_base.num;
    ec->avs3_cfg.wpp_threads        = ec->threads_wpp;
    ec->avs3_cfg.frm_threads        = ec->threads_frm;
    ec->avs3_cfg.qp                 = ec->baseQP;
    ec->avs3_cfg.rc_crf             = ec->baseCRF;
    ec->avs3_cfg.rc_type            = ec->rc_type;
    ec->avs3_cfg.i_period           = ec->intra_period;
    ec->avs3_cfg.close_gop          = ec->close_gop;
    ec->avs3_cfg.speed_level        = ec->speed_level;

    if (avctx->bit_rate) {
        ec->avs3_cfg.rc_type = 2;
        ec->avs3_cfg.rc_bitrate = avctx->bit_rate / 1000;
        ec->avs3_cfg.rc_max_bitrate = ec->avs3_cfg.rc_bitrate * 2;
        ec->avs3_cfg.rc_min_qp =  16;
        ec->avs3_cfg.rc_max_qp =  63;
    }

    av_log(NULL, AV_LOG_INFO, "uavs3e cfg: %dx%d %d/%dfps gop:%d\n", ec->avs3_cfg.pic_width, ec->avs3_cfg.pic_height,
                                                                     ec->avs3_cfg.fps_num, ec->avs3_cfg.fps_den, ec->avs3_cfg.i_period); 
    if (avctx->bit_rate) {
        av_log(NULL, AV_LOG_INFO, "uavs3e cfg: bitrate: %d kbps\n", ec->avs3_cfg.rc_bitrate);
    } else {
        av_log(NULL, AV_LOG_INFO, "uavs3e cfg: %s: %d\n", ec->avs3_cfg.rc_type == 0 ? "CQP" : "CRF", ec->avs3_cfg.rc_type == 0 ? ec->avs3_cfg.qp : ec->avs3_cfg.rc_crf);
    }
    av_log(NULL, AV_LOG_INFO, "uavs3e cfg: wpp_thread:%d  frm_thread:%d\n", ec->avs3_cfg.wpp_threads, ec->avs3_cfg.frm_threads);		

	ec->handle = uavs3e_create(&ec->avs3_cfg, NULL);
	
    return 0;
}

static void __imgb_cpy_plane(void *src, void *dst, int bw, int h, int s_src, int s_dst)
{
    int i;
    unsigned char *s, *d;
    s = (unsigned char *)src;
    d = (unsigned char *)dst;
    for (i = 0; i < h; i++) {
        memcpy(d, s, bw);
        s += s_src;
        d += s_dst;
    }
}

static void uavs3e_image_copy_pic(void *dst[4], int i_dst[4], unsigned char *const src[4], const int i_src[4],  enum AVPixelFormat pix_fmts, int width, int height)
{
    __imgb_cpy_plane(src[0], dst[0], width,      height,      i_src[0], i_dst[0]);
	__imgb_cpy_plane(src[1], dst[1], width >> 1, height >> 1, i_src[1], i_dst[1]);
	__imgb_cpy_plane(src[2], dst[2], width >> 1, height >> 1, i_src[2], i_dst[2]);
}

static int uavs3e_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                      const AVFrame *frame, int *got_packet)
{
    UAVS3EContext *ec = avctx->priv_data;
	enc_stat_t stat = {0};
	com_img_t *img_enc = NULL;

	int ret;

	if (ff_alloc_packet2(avctx, pkt, MAX_BS_BUF, 0) < 0) {
        return -1;
    }

    if (frame) {
        uavs3e_get_img(ec->handle, &img_enc);
		img_enc->pts = frame->pts;
        uavs3e_image_copy_pic(img_enc->planes, img_enc->stride, frame->data, frame->linesize, avctx->pix_fmt, img_enc->width[0], img_enc->height[0]);
    }

	ret = uavs3e_enc(ec->handle, &stat, img_enc);

    if (ret == COM_OK) {
        *got_packet = 1;
        memcpy(pkt->data, stat.buf, stat.bytes);
        pkt->size = stat.bytes;
        pkt->pts  = stat.pts;
        pkt->dts  = stat.dts - 4 * avctx->time_base.num;

		if (stat.type == SLICE_I) {
            pkt->flags |= AV_PKT_FLAG_KEY;
        } else { 
            pkt->flags &= ~AV_PKT_FLAG_KEY;
        }
				
#if FF_API_CODED_FRAME 
	FF_DISABLE_DEPRECATION_WARNINGS
			avctx->coded_frame->pts = stat.pts;
			switch (stat.type) {
			case SLICE_I:
				avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
				break;
			case SLICE_P:
				avctx->coded_frame->pict_type = AV_PICTURE_TYPE_P;
				break;
			case SLICE_B:
				avctx->coded_frame->pict_type = AV_PICTURE_TYPE_B;
				break;
			default:
				avctx->coded_frame->pict_type = AV_PICTURE_TYPE_NONE;
			}
			avctx->coded_frame->key_frame = (stat.type == SLICE_I);
	FF_ENABLE_DEPRECATION_WARNINGS
#endif
    } else {
		*got_packet = 0;		
    }

    return 0;
}

static int uavs3e_close(AVCodecContext *avctx)
{ 
    UAVS3EContext *ec = avctx->priv_data;
    uavs3e_free(ec->handle);
	
    return 0;
} 

#define OFFSET(x) offsetof(UAVS3EContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

static const AVOption options[] = {
    { "thds_wpp",  "Wavefront threads",       OFFSET(threads_wpp ),  AV_OPT_TYPE_INT,    {.i64 =  8 }, 1, 256, VE }, 
    { "thds_frm",  "Frame threads",           OFFSET(threads_frm ),  AV_OPT_TYPE_INT,    {.i64 =  1 }, 1,  64, VE }, 
    { "qp",        "Quantization parameter",  OFFSET(baseQP      ),  AV_OPT_TYPE_INT,    {.i64 = 34 }, 1,  63, VE },
    { "crf",       "CRF parameter",           OFFSET(baseCRF     ),  AV_OPT_TYPE_INT,    {.i64 = 34 }, 1,  63, VE },
    { "speed",     "Speed level",             OFFSET(speed_level ),  AV_OPT_TYPE_INT,    {.i64 =  6 }, 0,   6, VE },
    { "iperiod",   "Intra period",            OFFSET(intra_period),  AV_OPT_TYPE_INT,    {.i64 = 64 }, 16,  1000, VE },
    { "close_gop", "Enable Close GOP",        OFFSET(close_gop   ),  AV_OPT_TYPE_INT,    {.i64 =  0 }, 0,   1, VE },
    { "rc_type",   "Rate Control Type",       OFFSET(rc_type     ),  AV_OPT_TYPE_INT,    {.i64 =  0 }, 0,   2, VE },
    
    { "hdr",       "Enable HDR(0:NULL, 1:SDR, 2:SMPTE2084, 3:HLG)",              
                                              OFFSET(hdr         ), AV_OPT_TYPE_INT,    {.i64 =  0 }, 0,   3, VE },
    { "hdr_ext",   "HDR extension data:[enable:pri_x1:pri_x2:pri_x3:pri_y1:pri_y2:pri_y3:white_x,white_y:max:min:content:picture]",      
                                              OFFSET(hdr_ext     ), AV_OPT_TYPE_STRING, {.str = "[0:0:0:0:0:0:0:0:0:0:0:0:0]" }, 0, 0, VE },
    { NULL },
};

static const AVClass uavs3e_class = {
    .class_name = "libuavs3e",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecDefault uavs3e_defaults[] = {
    { "b",                "0" },
    { NULL },
};

AVCodec ff_libuavs3e_encoder = {
    .name           = "libuavs3e",
    .long_name      = NULL_IF_CONFIG_SMALL("libuavs3e Chinese AVS3 (Audio Video Standard)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AVS3,
    .priv_data_size = sizeof(UAVS3EContext),
    .init           = uavs3e_init,
    .encode2        = uavs3e_encode_frame,
    .close          = uavs3e_close,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AUTO_THREADS,
#if (BIT_DEPTH == 10)
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10LE, AV_PIX_FMT_NONE },
#else
    .pix_fmts		= (const enum AVPixelFormat[]) { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE },
#endif
    .priv_class     = &uavs3e_class,
    .defaults       = uavs3e_defaults,
} ;
