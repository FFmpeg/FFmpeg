#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/avutil.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "uavs3d.h"
#include "libavutil/imgutils.h"
#include "internal.h"

#define UAVS3D_MAX_FRAME_THREADS 48 

static const int color_primaries_tab[10] = {
    AVCOL_PRI_RESERVED0   ,    // 0
    AVCOL_PRI_BT709       ,    // 1
    AVCOL_PRI_UNSPECIFIED ,    // 2
    AVCOL_PRI_RESERVED    ,    // 3
    AVCOL_PRI_BT470M	  ,    // 4
    AVCOL_PRI_BT470BG	  ,    // 5
    AVCOL_PRI_SMPTE170M   ,    // 6
    AVCOL_PRI_SMPTE240M   ,    // 7
    AVCOL_PRI_FILM        ,    // 8
    AVCOL_PRI_BT2020           // 9  
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

static const enum AVPictureType IMGTYPE[8] = {
    AV_PICTURE_TYPE_NONE,
    AV_PICTURE_TYPE_I,
    AV_PICTURE_TYPE_P,
    AV_PICTURE_TYPE_B
};

typedef struct UAVS3DContext {
    AVCodecContext  *avctx;
    void            *dec_handle;
    int              frame_threads;
    int              got_seqhdr;
    uavs3d_io_frm_t  dec_frame;
} UAVS3DContext;


static int find_next_start_code(const unsigned char *bs_data, int bs_len, int *left)
{
    const unsigned char *data_ptr = bs_data + 4;
    int count = bs_len - 4;

    while (count >= 4 &&
        ((*(unsigned int *)data_ptr) != 0xB6010000) && /* P/B picture */
        ((*(unsigned int *)data_ptr) != 0xB3010000) && /* I   picture */
        ((*(unsigned int *)data_ptr) != 0xB0010000) && /* sequence header */
        ((*(unsigned int *)data_ptr) != 0x00010000) && /* first slice */
        ((*(unsigned int *)data_ptr) != 0xB1010000)) { /* sequence end */
        data_ptr++;
        count--;
    }

    if (count >= 4) {
        *left = count; 
        return 1;
    }

    return 0;
}

static void ff_output_callback(uavs3d_io_frm_t *dec_frame) {
    uavs3d_io_frm_t frm_out;
    AVFrame *frm = (AVFrame *)dec_frame->priv;
    int i;

    if (frm == NULL) {
        return;
    }
    
    frm->pts       = dec_frame->pts;
    frm->pkt_dts   = dec_frame->dts;
    frm->pict_type = IMGTYPE[dec_frame->type];
    frm->key_frame = (frm->pict_type == AV_PICTURE_TYPE_I);

    for (i = 0; i < 3; i++) {
        frm_out.width [i] = dec_frame->width[i];
        frm_out.height[i] = dec_frame->height[i];
        frm_out.stride[i] = frm->linesize[i];
        frm_out.buffer[i] = frm->data[i]; 
    }

    uavs3d_img_cpy_cvt(&frm_out, dec_frame, dec_frame->bit_depth);
}

static int libuavs3d_init(AVCodecContext *avctx)
{
    UAVS3DContext *h = avctx->priv_data;
    uavs3d_cfg_t cdsc;

    cdsc.frm_threads = FFMIN(h->frame_threads > 0 ? h->frame_threads : av_cpu_count(), UAVS3D_MAX_FRAME_THREADS);
    cdsc.check_md5 = 0;
    h->dec_handle = uavs3d_create(&cdsc, ff_output_callback, NULL);
    h->got_seqhdr = 0;
 
    return 0;
}

static int libuavs3d_end(AVCodecContext *avctx)
{
    UAVS3DContext *h = avctx->priv_data;
    
    if (h->dec_handle) {
    	uavs3d_flush(h->dec_handle, NULL);
        uavs3d_delete(h->dec_handle);
        h->dec_handle = NULL;
    }
    h->got_seqhdr = 0;
    
    return 0;
}

static void libuavs3d_flush(AVCodecContext * avctx)
{
    UAVS3DContext *h = avctx->priv_data;
    uavs3d_cfg_t cdsc;
    cdsc.frm_threads = FFMIN(h->frame_threads > 0 ? h->frame_threads : av_cpu_count(), UAVS3D_MAX_FRAME_THREADS);
    cdsc.check_md5 = 0;

    if (h->dec_handle) {
    	uavs3d_flush(h->dec_handle, NULL);
        uavs3d_delete(h->dec_handle);
    }
     
    h->dec_handle = uavs3d_create(&cdsc, ff_output_callback, NULL);
    h->got_seqhdr = 0;
}

static int libuavs3d_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{
    UAVS3DContext *h = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    const uint8_t *buf_end;
    const uint8_t *buf_ptr;
    AVFrame *frm = (AVFrame*)data;
    int left_bytes;
    int ret, finish = 0;

    *got_frame = 0;
    frm->pts = -1;
    frm->pict_type = AV_PICTURE_TYPE_NONE;

    if (h->got_seqhdr) {
        if (frm->data[0] == NULL && (ret = ff_get_buffer(avctx, frm, 0)) < 0) {
            return ret;
        } 
        h->dec_frame.priv = data;   // AVFrame
    }

    if (buf_size == 0) {
        do {
            ret = uavs3d_flush(h->dec_handle, &h->dec_frame);
        } while (ret > 0 && !h->dec_frame.got_pic);
    } else {
        buf_ptr = buf;
        buf_end = buf + buf_size;
  
        while (finish == 0) {
            int bs_len;
            uavs3d_io_frm_t *frm_dec = &h->dec_frame;

            if (find_next_start_code(buf_ptr, buf_end - buf_ptr, &left_bytes)) {
                bs_len = buf_end - buf_ptr - left_bytes;
            } else { 
                bs_len = buf_end - buf_ptr;
                finish = 1;
            }
            frm_dec->bs = (unsigned char *)buf_ptr;
            frm_dec->bs_len = bs_len;
            frm_dec->pts = avpkt->pts;
            frm_dec->dts = avpkt->dts;
            uavs3d_decode(h->dec_handle, frm_dec);
            buf_ptr += bs_len;

            if (frm_dec->nal_type == NAL_SEQ_HEADER) {
                static const int avs2_fps_num[9] = {0, 240000, 24, 25, 30000, 30, 50, 60000, 60 };
                static const int avs2_fps_den[9] = {1,   1001,  1,  1,  1001,  1,  1,  1001,  1 };
                avctx->framerate.num = avs2_fps_num[frm_dec->seqhdr->frame_rate_code];
                avctx->framerate.den = avs2_fps_den[frm_dec->seqhdr->frame_rate_code];
                avctx->has_b_frames = 1;
                avctx->pix_fmt = frm_dec->seqhdr->bit_depth_internal == 8 ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_YUV420P10LE;
                ff_set_dimensions(avctx, frm_dec->seqhdr->horizontal_size, frm_dec->seqhdr->vertical_size);
                h->got_seqhdr = 1;
            }
            if (frm_dec->got_pic) {
                break;
            }
        }
    }

    *got_frame = h->dec_frame.got_pic;

    return buf_ptr - buf;
}

#define OFFSET(x) offsetof(UAVS3DContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "frame_threads",      "number of frame-level threads ", OFFSET(frame_threads),  AV_OPT_TYPE_INT,    {.i64 =  0 }, 0, UAVS3D_MAX_FRAME_THREADS, VE },
    { NULL }
};
static const AVClass libuavs3d_class = {
    .class_name = "uavs3d_class",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_libuavs3d_decoder = {
    .name           = "libuavs3d",
    .long_name      = NULL_IF_CONFIG_SMALL("Decoder for Chinese AVS3"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AVS3,
    .priv_data_size = sizeof(UAVS3DContext),
    .priv_class     = &libuavs3d_class,
    .init           = libuavs3d_init,
    .close          = libuavs3d_end,
    .decode         = libuavs3d_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY,
    .flush          = libuavs3d_flush,
    .pix_fmts	    = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10LE, AV_PIX_FMT_NONE },
    
    .wrapper_name   = "libuavs3d",
};
