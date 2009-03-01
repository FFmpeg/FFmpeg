/*
 * FFplay : Simple Media Player based on the ffmpeg libraries
 * Copyright (c) 2003 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <math.h>
#include <limits.h>
#include "libavutil/avstring.h"
#include "libavformat/avformat.h"
#include "libavformat/rtsp.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavcodec/audioconvert.h"
#include "libavcodec/opt.h"

#include "cmdutils.h"

#include <SDL.h>
#include <SDL_thread.h>

#ifdef __MINGW32__
#undef main /* We don't want SDL to override our main() */
#endif

#undef exit

const char program_name[] = "FFplay";
const int program_birth_year = 2003;

//#define DEBUG_SYNC

#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)
#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_SUBTITLEQ_SIZE (5 * 16 * 1024)

/* SDL audio buffer size, in samples. Should be small to have precise
   A/V sync as SDL does not have hardware buffer fullness info. */
#define SDL_AUDIO_BUFFER_SIZE 1024

/* no AV sync correction is done if below the AV sync threshold */
#define AV_SYNC_THRESHOLD 0.01
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 10

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20

/* NOTE: the size must be big enough to compensate the hardware audio buffersize size */
#define SAMPLE_ARRAY_SIZE (2*65536)

static int sws_flags = SWS_BICUBIC;

typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    int abort_request;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

#define VIDEO_PICTURE_QUEUE_SIZE 1
#define SUBPICTURE_QUEUE_SIZE 4

typedef struct VideoPicture {
    double pts;                                  ///<presentation time stamp for this picture
    SDL_Overlay *bmp;
    int width, height; /* source height & width */
    int allocated;
} VideoPicture;

typedef struct SubPicture {
    double pts; /* presentation time stamp for this picture */
    AVSubtitle sub;
} SubPicture;

enum {
    AV_SYNC_AUDIO_MASTER, /* default choice */
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};

typedef struct VideoState {
    SDL_Thread *parse_tid;
    SDL_Thread *video_tid;
    AVInputFormat *iformat;
    int no_background;
    int abort_request;
    int paused;
    int last_paused;
    int seek_req;
    int seek_flags;
    int64_t seek_pos;
    AVFormatContext *ic;
    int dtg_active_format;

    int audio_stream;

    int av_sync_type;
    double external_clock; /* external clock base */
    int64_t external_clock_time;

    double audio_clock;
    double audio_diff_cum; /* used for AV difference average computation */
    double audio_diff_avg_coef;
    double audio_diff_threshold;
    int audio_diff_avg_count;
    AVStream *audio_st;
    PacketQueue audioq;
    int audio_hw_buf_size;
    /* samples output by the codec. we reserve more space for avsync
       compensation */
    DECLARE_ALIGNED(16,uint8_t,audio_buf1[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2]);
    DECLARE_ALIGNED(16,uint8_t,audio_buf2[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2]);
    uint8_t *audio_buf;
    unsigned int audio_buf_size; /* in bytes */
    int audio_buf_index; /* in bytes */
    AVPacket audio_pkt;
    uint8_t *audio_pkt_data;
    int audio_pkt_size;
    enum SampleFormat audio_src_fmt;
    AVAudioConvert *reformat_ctx;

    int show_audio; /* if true, display audio samples */
    int16_t sample_array[SAMPLE_ARRAY_SIZE];
    int sample_array_index;
    int last_i_start;

    SDL_Thread *subtitle_tid;
    int subtitle_stream;
    int subtitle_stream_changed;
    AVStream *subtitle_st;
    PacketQueue subtitleq;
    SubPicture subpq[SUBPICTURE_QUEUE_SIZE];
    int subpq_size, subpq_rindex, subpq_windex;
    SDL_mutex *subpq_mutex;
    SDL_cond *subpq_cond;

    double frame_timer;
    double frame_last_pts;
    double frame_last_delay;
    double video_clock;                          ///<pts of last decoded frame / predicted pts of next decoded frame
    int video_stream;
    AVStream *video_st;
    PacketQueue videoq;
    double video_current_pts;                    ///<current displayed pts (different from video_clock if frame fifos are used)
    int64_t video_current_pts_time;              ///<time (av_gettime) at which we updated video_current_pts - used to have running video pts
    VideoPicture pictq[VIDEO_PICTURE_QUEUE_SIZE];
    int pictq_size, pictq_rindex, pictq_windex;
    SDL_mutex *pictq_mutex;
    SDL_cond *pictq_cond;

    //    QETimer *video_timer;
    char filename[1024];
    int width, height, xleft, ytop;
} VideoState;

static void show_help(void);
static int audio_write_get_buf_size(VideoState *is);

/* options specified by the user */
static AVInputFormat *file_iformat;
static const char *input_filename;
static int fs_screen_width;
static int fs_screen_height;
static int screen_width = 0;
static int screen_height = 0;
static int frame_width = 0;
static int frame_height = 0;
static enum PixelFormat frame_pix_fmt = PIX_FMT_NONE;
static int audio_disable;
static int video_disable;
static int wanted_audio_stream= 0;
static int wanted_video_stream= 0;
static int wanted_subtitle_stream= -1;
static int seek_by_bytes;
static int display_disable;
static int show_status;
static int av_sync_type = AV_SYNC_AUDIO_MASTER;
static int64_t start_time = AV_NOPTS_VALUE;
static int debug = 0;
static int debug_mv = 0;
static int step = 0;
static int thread_count = 1;
static int workaround_bugs = 1;
static int fast = 0;
static int genpts = 0;
static int lowres = 0;
static int idct = FF_IDCT_AUTO;
static enum AVDiscard skip_frame= AVDISCARD_DEFAULT;
static enum AVDiscard skip_idct= AVDISCARD_DEFAULT;
static enum AVDiscard skip_loop_filter= AVDISCARD_DEFAULT;
static int error_recognition = FF_ER_CAREFUL;
static int error_concealment = 3;
static int decoder_reorder_pts= 0;

/* current context */
static int is_full_screen;
static VideoState *cur_stream;
static int64_t audio_callback_time;

static AVPacket flush_pkt;

#define FF_ALLOC_EVENT   (SDL_USEREVENT)
#define FF_REFRESH_EVENT (SDL_USEREVENT + 1)
#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)

static SDL_Surface *screen;

/* packet queue handling */
static void packet_queue_init(PacketQueue *q)
{
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}

static void packet_queue_flush(PacketQueue *q)
{
    AVPacketList *pkt, *pkt1;

    SDL_LockMutex(q->mutex);
    for(pkt = q->first_pkt; pkt != NULL; pkt = pkt1) {
        pkt1 = pkt->next;
        av_free_packet(&pkt->pkt);
        av_freep(&pkt);
    }
    q->last_pkt = NULL;
    q->first_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    SDL_UnlockMutex(q->mutex);
}

static void packet_queue_end(PacketQueue *q)
{
    packet_queue_flush(q);
    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

static int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    AVPacketList *pkt1;

    /* duplicate the packet */
    if (pkt!=&flush_pkt && av_dup_packet(pkt) < 0)
        return -1;

    pkt1 = av_malloc(sizeof(AVPacketList));
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;


    SDL_LockMutex(q->mutex);

    if (!q->last_pkt)

        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size + sizeof(*pkt1);
    /* XXX: should duplicate packet data in DV case */
    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
    return 0;
}

static void packet_queue_abort(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);

    q->abort_request = 1;

    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
    AVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);

    for(;;) {
        if (q->abort_request) {
            ret = -1;
            break;
        }

        pkt1 = q->first_pkt;
        if (pkt1) {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size + sizeof(*pkt1);
            *pkt = pkt1->pkt;
            av_free(pkt1);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}

static inline void fill_rectangle(SDL_Surface *screen,
                                  int x, int y, int w, int h, int color)
{
    SDL_Rect rect;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    SDL_FillRect(screen, &rect, color);
}

#if 0
/* draw only the border of a rectangle */
void fill_border(VideoState *s, int x, int y, int w, int h, int color)
{
    int w1, w2, h1, h2;

    /* fill the background */
    w1 = x;
    if (w1 < 0)
        w1 = 0;
    w2 = s->width - (x + w);
    if (w2 < 0)
        w2 = 0;
    h1 = y;
    if (h1 < 0)
        h1 = 0;
    h2 = s->height - (y + h);
    if (h2 < 0)
        h2 = 0;
    fill_rectangle(screen,
                   s->xleft, s->ytop,
                   w1, s->height,
                   color);
    fill_rectangle(screen,
                   s->xleft + s->width - w2, s->ytop,
                   w2, s->height,
                   color);
    fill_rectangle(screen,
                   s->xleft + w1, s->ytop,
                   s->width - w1 - w2, h1,
                   color);
    fill_rectangle(screen,
                   s->xleft + w1, s->ytop + s->height - h2,
                   s->width - w1 - w2, h2,
                   color);
}
#endif



#define SCALEBITS 10
#define ONE_HALF  (1 << (SCALEBITS - 1))
#define FIX(x)    ((int) ((x) * (1<<SCALEBITS) + 0.5))

#define RGB_TO_Y_CCIR(r, g, b) \
((FIX(0.29900*219.0/255.0) * (r) + FIX(0.58700*219.0/255.0) * (g) + \
  FIX(0.11400*219.0/255.0) * (b) + (ONE_HALF + (16 << SCALEBITS))) >> SCALEBITS)

#define RGB_TO_U_CCIR(r1, g1, b1, shift)\
(((- FIX(0.16874*224.0/255.0) * r1 - FIX(0.33126*224.0/255.0) * g1 +         \
     FIX(0.50000*224.0/255.0) * b1 + (ONE_HALF << shift) - 1) >> (SCALEBITS + shift)) + 128)

#define RGB_TO_V_CCIR(r1, g1, b1, shift)\
(((FIX(0.50000*224.0/255.0) * r1 - FIX(0.41869*224.0/255.0) * g1 -           \
   FIX(0.08131*224.0/255.0) * b1 + (ONE_HALF << shift) - 1) >> (SCALEBITS + shift)) + 128)

#define ALPHA_BLEND(a, oldp, newp, s)\
((((oldp << s) * (255 - (a))) + (newp * (a))) / (255 << s))

#define RGBA_IN(r, g, b, a, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    a = (v >> 24) & 0xff;\
    r = (v >> 16) & 0xff;\
    g = (v >> 8) & 0xff;\
    b = v & 0xff;\
}

#define YUVA_IN(y, u, v, a, s, pal)\
{\
    unsigned int val = ((const uint32_t *)(pal))[*(const uint8_t*)(s)];\
    a = (val >> 24) & 0xff;\
    y = (val >> 16) & 0xff;\
    u = (val >> 8) & 0xff;\
    v = val & 0xff;\
}

#define YUVA_OUT(d, y, u, v, a)\
{\
    ((uint32_t *)(d))[0] = (a << 24) | (y << 16) | (u << 8) | v;\
}


#define BPP 1

static void blend_subrect(AVPicture *dst, const AVSubtitleRect *rect, int imgw, int imgh)
{
    int wrap, wrap3, width2, skip2;
    int y, u, v, a, u1, v1, a1, w, h;
    uint8_t *lum, *cb, *cr;
    const uint8_t *p;
    const uint32_t *pal;
    int dstx, dsty, dstw, dsth;

    dstw = av_clip(rect->w, 0, imgw);
    dsth = av_clip(rect->h, 0, imgh);
    dstx = av_clip(rect->x, 0, imgw - dstw);
    dsty = av_clip(rect->y, 0, imgh - dsth);
    lum = dst->data[0] + dsty * dst->linesize[0];
    cb = dst->data[1] + (dsty >> 1) * dst->linesize[1];
    cr = dst->data[2] + (dsty >> 1) * dst->linesize[2];

    width2 = ((dstw + 1) >> 1) + (dstx & ~dstw & 1);
    skip2 = dstx >> 1;
    wrap = dst->linesize[0];
    wrap3 = rect->pict.linesize[0];
    p = rect->pict.data[0];
    pal = (const uint32_t *)rect->pict.data[1];  /* Now in YCrCb! */

    if (dsty & 1) {
        lum += dstx;
        cb += skip2;
        cr += skip2;

        if (dstx & 1) {
            YUVA_IN(y, u, v, a, p, pal);
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);
            cb[0] = ALPHA_BLEND(a >> 2, cb[0], u, 0);
            cr[0] = ALPHA_BLEND(a >> 2, cr[0], v, 0);
            cb++;
            cr++;
            lum++;
            p += BPP;
        }
        for(w = dstw - (dstx & 1); w >= 2; w -= 2) {
            YUVA_IN(y, u, v, a, p, pal);
            u1 = u;
            v1 = v;
            a1 = a;
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);

            YUVA_IN(y, u, v, a, p + BPP, pal);
            u1 += u;
            v1 += v;
            a1 += a;
            lum[1] = ALPHA_BLEND(a, lum[1], y, 0);
            cb[0] = ALPHA_BLEND(a1 >> 2, cb[0], u1, 1);
            cr[0] = ALPHA_BLEND(a1 >> 2, cr[0], v1, 1);
            cb++;
            cr++;
            p += 2 * BPP;
            lum += 2;
        }
        if (w) {
            YUVA_IN(y, u, v, a, p, pal);
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);
            cb[0] = ALPHA_BLEND(a >> 2, cb[0], u, 0);
            cr[0] = ALPHA_BLEND(a >> 2, cr[0], v, 0);
            p++;
            lum++;
        }
        p += wrap3 - dstw * BPP;
        lum += wrap - dstw - dstx;
        cb += dst->linesize[1] - width2 - skip2;
        cr += dst->linesize[2] - width2 - skip2;
    }
    for(h = dsth - (dsty & 1); h >= 2; h -= 2) {
        lum += dstx;
        cb += skip2;
        cr += skip2;

        if (dstx & 1) {
            YUVA_IN(y, u, v, a, p, pal);
            u1 = u;
            v1 = v;
            a1 = a;
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);
            p += wrap3;
            lum += wrap;
            YUVA_IN(y, u, v, a, p, pal);
            u1 += u;
            v1 += v;
            a1 += a;
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);
            cb[0] = ALPHA_BLEND(a1 >> 2, cb[0], u1, 1);
            cr[0] = ALPHA_BLEND(a1 >> 2, cr[0], v1, 1);
            cb++;
            cr++;
            p += -wrap3 + BPP;
            lum += -wrap + 1;
        }
        for(w = dstw - (dstx & 1); w >= 2; w -= 2) {
            YUVA_IN(y, u, v, a, p, pal);
            u1 = u;
            v1 = v;
            a1 = a;
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);

            YUVA_IN(y, u, v, a, p + BPP, pal);
            u1 += u;
            v1 += v;
            a1 += a;
            lum[1] = ALPHA_BLEND(a, lum[1], y, 0);
            p += wrap3;
            lum += wrap;

            YUVA_IN(y, u, v, a, p, pal);
            u1 += u;
            v1 += v;
            a1 += a;
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);

            YUVA_IN(y, u, v, a, p + BPP, pal);
            u1 += u;
            v1 += v;
            a1 += a;
            lum[1] = ALPHA_BLEND(a, lum[1], y, 0);

            cb[0] = ALPHA_BLEND(a1 >> 2, cb[0], u1, 2);
            cr[0] = ALPHA_BLEND(a1 >> 2, cr[0], v1, 2);

            cb++;
            cr++;
            p += -wrap3 + 2 * BPP;
            lum += -wrap + 2;
        }
        if (w) {
            YUVA_IN(y, u, v, a, p, pal);
            u1 = u;
            v1 = v;
            a1 = a;
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);
            p += wrap3;
            lum += wrap;
            YUVA_IN(y, u, v, a, p, pal);
            u1 += u;
            v1 += v;
            a1 += a;
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);
            cb[0] = ALPHA_BLEND(a1 >> 2, cb[0], u1, 1);
            cr[0] = ALPHA_BLEND(a1 >> 2, cr[0], v1, 1);
            cb++;
            cr++;
            p += -wrap3 + BPP;
            lum += -wrap + 1;
        }
        p += wrap3 + (wrap3 - dstw * BPP);
        lum += wrap + (wrap - dstw - dstx);
        cb += dst->linesize[1] - width2 - skip2;
        cr += dst->linesize[2] - width2 - skip2;
    }
    /* handle odd height */
    if (h) {
        lum += dstx;
        cb += skip2;
        cr += skip2;

        if (dstx & 1) {
            YUVA_IN(y, u, v, a, p, pal);
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);
            cb[0] = ALPHA_BLEND(a >> 2, cb[0], u, 0);
            cr[0] = ALPHA_BLEND(a >> 2, cr[0], v, 0);
            cb++;
            cr++;
            lum++;
            p += BPP;
        }
        for(w = dstw - (dstx & 1); w >= 2; w -= 2) {
            YUVA_IN(y, u, v, a, p, pal);
            u1 = u;
            v1 = v;
            a1 = a;
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);

            YUVA_IN(y, u, v, a, p + BPP, pal);
            u1 += u;
            v1 += v;
            a1 += a;
            lum[1] = ALPHA_BLEND(a, lum[1], y, 0);
            cb[0] = ALPHA_BLEND(a1 >> 2, cb[0], u, 1);
            cr[0] = ALPHA_BLEND(a1 >> 2, cr[0], v, 1);
            cb++;
            cr++;
            p += 2 * BPP;
            lum += 2;
        }
        if (w) {
            YUVA_IN(y, u, v, a, p, pal);
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);
            cb[0] = ALPHA_BLEND(a >> 2, cb[0], u, 0);
            cr[0] = ALPHA_BLEND(a >> 2, cr[0], v, 0);
        }
    }
}

static void free_subpicture(SubPicture *sp)
{
    int i;

    for (i = 0; i < sp->sub.num_rects; i++)
    {
        av_freep(&sp->sub.rects[i]->pict.data[0]);
        av_freep(&sp->sub.rects[i]->pict.data[1]);
        av_freep(&sp->sub.rects[i]);
    }

    av_free(sp->sub.rects);

    memset(&sp->sub, 0, sizeof(AVSubtitle));
}

static void video_image_display(VideoState *is)
{
    VideoPicture *vp;
    SubPicture *sp;
    AVPicture pict;
    float aspect_ratio;
    int width, height, x, y;
    SDL_Rect rect;
    int i;

    vp = &is->pictq[is->pictq_rindex];
    if (vp->bmp) {
        /* XXX: use variable in the frame */
        if (is->video_st->sample_aspect_ratio.num)
            aspect_ratio = av_q2d(is->video_st->sample_aspect_ratio);
        else if (is->video_st->codec->sample_aspect_ratio.num)
            aspect_ratio = av_q2d(is->video_st->codec->sample_aspect_ratio);
        else
            aspect_ratio = 0;
        if (aspect_ratio <= 0.0)
            aspect_ratio = 1.0;
        aspect_ratio *= (float)is->video_st->codec->width / is->video_st->codec->height;
        /* if an active format is indicated, then it overrides the
           mpeg format */
#if 0
        if (is->video_st->codec->dtg_active_format != is->dtg_active_format) {
            is->dtg_active_format = is->video_st->codec->dtg_active_format;
            printf("dtg_active_format=%d\n", is->dtg_active_format);
        }
#endif
#if 0
        switch(is->video_st->codec->dtg_active_format) {
        case FF_DTG_AFD_SAME:
        default:
            /* nothing to do */
            break;
        case FF_DTG_AFD_4_3:
            aspect_ratio = 4.0 / 3.0;
            break;
        case FF_DTG_AFD_16_9:
            aspect_ratio = 16.0 / 9.0;
            break;
        case FF_DTG_AFD_14_9:
            aspect_ratio = 14.0 / 9.0;
            break;
        case FF_DTG_AFD_4_3_SP_14_9:
            aspect_ratio = 14.0 / 9.0;
            break;
        case FF_DTG_AFD_16_9_SP_14_9:
            aspect_ratio = 14.0 / 9.0;
            break;
        case FF_DTG_AFD_SP_4_3:
            aspect_ratio = 4.0 / 3.0;
            break;
        }
#endif

        if (is->subtitle_st)
        {
            if (is->subpq_size > 0)
            {
                sp = &is->subpq[is->subpq_rindex];

                if (vp->pts >= sp->pts + ((float) sp->sub.start_display_time / 1000))
                {
                    SDL_LockYUVOverlay (vp->bmp);

                    pict.data[0] = vp->bmp->pixels[0];
                    pict.data[1] = vp->bmp->pixels[2];
                    pict.data[2] = vp->bmp->pixels[1];

                    pict.linesize[0] = vp->bmp->pitches[0];
                    pict.linesize[1] = vp->bmp->pitches[2];
                    pict.linesize[2] = vp->bmp->pitches[1];

                    for (i = 0; i < sp->sub.num_rects; i++)
                        blend_subrect(&pict, sp->sub.rects[i],
                                      vp->bmp->w, vp->bmp->h);

                    SDL_UnlockYUVOverlay (vp->bmp);
                }
            }
        }


        /* XXX: we suppose the screen has a 1.0 pixel ratio */
        height = is->height;
        width = ((int)rint(height * aspect_ratio)) & ~1;
        if (width > is->width) {
            width = is->width;
            height = ((int)rint(width / aspect_ratio)) & ~1;
        }
        x = (is->width - width) / 2;
        y = (is->height - height) / 2;
        if (!is->no_background) {
            /* fill the background */
            //            fill_border(is, x, y, width, height, QERGB(0x00, 0x00, 0x00));
        } else {
            is->no_background = 0;
        }
        rect.x = is->xleft + x;
        rect.y = is->ytop  + y;
        rect.w = width;
        rect.h = height;
        SDL_DisplayYUVOverlay(vp->bmp, &rect);
    } else {
#if 0
        fill_rectangle(screen,
                       is->xleft, is->ytop, is->width, is->height,
                       QERGB(0x00, 0x00, 0x00));
#endif
    }
}

static inline int compute_mod(int a, int b)
{
    a = a % b;
    if (a >= 0)
        return a;
    else
        return a + b;
}

static void video_audio_display(VideoState *s)
{
    int i, i_start, x, y1, y, ys, delay, n, nb_display_channels;
    int ch, channels, h, h2, bgcolor, fgcolor;
    int16_t time_diff;

    /* compute display index : center on currently output samples */
    channels = s->audio_st->codec->channels;
    nb_display_channels = channels;
    if (!s->paused) {
        n = 2 * channels;
        delay = audio_write_get_buf_size(s);
        delay /= n;

        /* to be more precise, we take into account the time spent since
           the last buffer computation */
        if (audio_callback_time) {
            time_diff = av_gettime() - audio_callback_time;
            delay += (time_diff * s->audio_st->codec->sample_rate) / 1000000;
        }

        delay -= s->width / 2;
        if (delay < s->width)
            delay = s->width;

        i_start= x = compute_mod(s->sample_array_index - delay * channels, SAMPLE_ARRAY_SIZE);

        h= INT_MIN;
        for(i=0; i<1000; i+=channels){
            int idx= (SAMPLE_ARRAY_SIZE + x - i) % SAMPLE_ARRAY_SIZE;
            int a= s->sample_array[idx];
            int b= s->sample_array[(idx + 4*channels)%SAMPLE_ARRAY_SIZE];
            int c= s->sample_array[(idx + 5*channels)%SAMPLE_ARRAY_SIZE];
            int d= s->sample_array[(idx + 9*channels)%SAMPLE_ARRAY_SIZE];
            int score= a-d;
            if(h<score && (b^c)<0){
                h= score;
                i_start= idx;
            }
        }

        s->last_i_start = i_start;
    } else {
        i_start = s->last_i_start;
    }

    bgcolor = SDL_MapRGB(screen->format, 0x00, 0x00, 0x00);
    fill_rectangle(screen,
                   s->xleft, s->ytop, s->width, s->height,
                   bgcolor);

    fgcolor = SDL_MapRGB(screen->format, 0xff, 0xff, 0xff);

    /* total height for one channel */
    h = s->height / nb_display_channels;
    /* graph height / 2 */
    h2 = (h * 9) / 20;
    for(ch = 0;ch < nb_display_channels; ch++) {
        i = i_start + ch;
        y1 = s->ytop + ch * h + (h / 2); /* position of center line */
        for(x = 0; x < s->width; x++) {
            y = (s->sample_array[i] * h2) >> 15;
            if (y < 0) {
                y = -y;
                ys = y1 - y;
            } else {
                ys = y1;
            }
            fill_rectangle(screen,
                           s->xleft + x, ys, 1, y,
                           fgcolor);
            i += channels;
            if (i >= SAMPLE_ARRAY_SIZE)
                i -= SAMPLE_ARRAY_SIZE;
        }
    }

    fgcolor = SDL_MapRGB(screen->format, 0x00, 0x00, 0xff);

    for(ch = 1;ch < nb_display_channels; ch++) {
        y = s->ytop + ch * h;
        fill_rectangle(screen,
                       s->xleft, y, s->width, 1,
                       fgcolor);
    }
    SDL_UpdateRect(screen, s->xleft, s->ytop, s->width, s->height);
}

static int video_open(VideoState *is){
    int flags = SDL_HWSURFACE|SDL_ASYNCBLIT|SDL_HWACCEL;
    int w,h;

    if(is_full_screen) flags |= SDL_FULLSCREEN;
    else               flags |= SDL_RESIZABLE;

    if (is_full_screen && fs_screen_width) {
        w = fs_screen_width;
        h = fs_screen_height;
    } else if(!is_full_screen && screen_width){
        w = screen_width;
        h = screen_height;
    }else if (is->video_st && is->video_st->codec->width){
        w = is->video_st->codec->width;
        h = is->video_st->codec->height;
    } else {
        w = 640;
        h = 480;
    }
#ifndef __APPLE__
    screen = SDL_SetVideoMode(w, h, 0, flags);
#else
    /* setting bits_per_pixel = 0 or 32 causes blank video on OS X */
    screen = SDL_SetVideoMode(w, h, 24, flags);
#endif
    if (!screen) {
        fprintf(stderr, "SDL: could not set video mode - exiting\n");
        return -1;
    }
    SDL_WM_SetCaption("FFplay", "FFplay");

    is->width = screen->w;
    is->height = screen->h;

    return 0;
}

/* display the current picture, if any */
static void video_display(VideoState *is)
{
    if(!screen)
        video_open(cur_stream);
    if (is->audio_st && is->show_audio)
        video_audio_display(is);
    else if (is->video_st)
        video_image_display(is);
}

static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque)
{
    SDL_Event event;
    event.type = FF_REFRESH_EVENT;
    event.user.data1 = opaque;
    SDL_PushEvent(&event);
    return 0; /* 0 means stop timer */
}

/* schedule a video refresh in 'delay' ms */
static void schedule_refresh(VideoState *is, int delay)
{
    if(!delay) delay=1; //SDL seems to be buggy when the delay is 0
    SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
}

/* get the current audio clock value */
static double get_audio_clock(VideoState *is)
{
    double pts;
    int hw_buf_size, bytes_per_sec;
    pts = is->audio_clock;
    hw_buf_size = audio_write_get_buf_size(is);
    bytes_per_sec = 0;
    if (is->audio_st) {
        bytes_per_sec = is->audio_st->codec->sample_rate *
            2 * is->audio_st->codec->channels;
    }
    if (bytes_per_sec)
        pts -= (double)hw_buf_size / bytes_per_sec;
    return pts;
}

/* get the current video clock value */
static double get_video_clock(VideoState *is)
{
    double delta;
    if (is->paused) {
        delta = 0;
    } else {
        delta = (av_gettime() - is->video_current_pts_time) / 1000000.0;
    }
    return is->video_current_pts + delta;
}

/* get the current external clock value */
static double get_external_clock(VideoState *is)
{
    int64_t ti;
    ti = av_gettime();
    return is->external_clock + ((ti - is->external_clock_time) * 1e-6);
}

/* get the current master clock value */
static double get_master_clock(VideoState *is)
{
    double val;

    if (is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
        if (is->video_st)
            val = get_video_clock(is);
        else
            val = get_audio_clock(is);
    } else if (is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
        if (is->audio_st)
            val = get_audio_clock(is);
        else
            val = get_video_clock(is);
    } else {
        val = get_external_clock(is);
    }
    return val;
}

/* seek in the stream */
static void stream_seek(VideoState *is, int64_t pos, int rel)
{
    if (!is->seek_req) {
        is->seek_pos = pos;
        is->seek_flags = rel < 0 ? AVSEEK_FLAG_BACKWARD : 0;
        if (seek_by_bytes)
            is->seek_flags |= AVSEEK_FLAG_BYTE;
        is->seek_req = 1;
    }
}

/* pause or resume the video */
static void stream_pause(VideoState *is)
{
    is->paused = !is->paused;
    if (!is->paused) {
        is->video_current_pts = get_video_clock(is);
        is->frame_timer += (av_gettime() - is->video_current_pts_time) / 1000000.0;
    }
}

static double compute_frame_delay(double frame_current_pts, VideoState *is)
{
    double actual_delay, delay, sync_threshold, ref_clock, diff;

    /* compute nominal delay */
    delay = frame_current_pts - is->frame_last_pts;
    if (delay <= 0 || delay >= 10.0) {
        /* if incorrect delay, use previous one */
        delay = is->frame_last_delay;
    } else {
        is->frame_last_delay = delay;
    }
    is->frame_last_pts = frame_current_pts;

    /* update delay to follow master synchronisation source */
    if (((is->av_sync_type == AV_SYNC_AUDIO_MASTER && is->audio_st) ||
         is->av_sync_type == AV_SYNC_EXTERNAL_CLOCK)) {
        /* if video is slave, we try to correct big delays by
           duplicating or deleting a frame */
        ref_clock = get_master_clock(is);
        diff = frame_current_pts - ref_clock;

        /* skip or repeat frame. We take into account the
           delay to compute the threshold. I still don't know
           if it is the best guess */
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD, delay);
        if (fabs(diff) < AV_NOSYNC_THRESHOLD) {
            if (diff <= -sync_threshold)
                delay = 0;
            else if (diff >= sync_threshold)
                delay = 2 * delay;
        }
    }

    is->frame_timer += delay;
    /* compute the REAL delay (we need to do that to avoid
       long term errors */
    actual_delay = is->frame_timer - (av_gettime() / 1000000.0);
    if (actual_delay < 0.010) {
        /* XXX: should skip picture */
        actual_delay = 0.010;
    }

#if defined(DEBUG_SYNC)
    printf("video: delay=%0.3f actual_delay=%0.3f pts=%0.3f A-V=%f\n",
            delay, actual_delay, frame_current_pts, -diff);
#endif

    return actual_delay;
}

/* called to display each frame */
static void video_refresh_timer(void *opaque)
{
    VideoState *is = opaque;
    VideoPicture *vp;

    SubPicture *sp, *sp2;

    if (is->video_st) {
        if (is->pictq_size == 0) {
            /* if no picture, need to wait */
            schedule_refresh(is, 1);
        } else {
            /* dequeue the picture */
            vp = &is->pictq[is->pictq_rindex];

            /* update current video pts */
            is->video_current_pts = vp->pts;
            is->video_current_pts_time = av_gettime();

            /* launch timer for next picture */
            schedule_refresh(is, (int)(compute_frame_delay(vp->pts, is) * 1000 + 0.5));

            if(is->subtitle_st) {
                if (is->subtitle_stream_changed) {
                    SDL_LockMutex(is->subpq_mutex);

                    while (is->subpq_size) {
                        free_subpicture(&is->subpq[is->subpq_rindex]);

                        /* update queue size and signal for next picture */
                        if (++is->subpq_rindex == SUBPICTURE_QUEUE_SIZE)
                            is->subpq_rindex = 0;

                        is->subpq_size--;
                    }
                    is->subtitle_stream_changed = 0;

                    SDL_CondSignal(is->subpq_cond);
                    SDL_UnlockMutex(is->subpq_mutex);
                } else {
                    if (is->subpq_size > 0) {
                        sp = &is->subpq[is->subpq_rindex];

                        if (is->subpq_size > 1)
                            sp2 = &is->subpq[(is->subpq_rindex + 1) % SUBPICTURE_QUEUE_SIZE];
                        else
                            sp2 = NULL;

                        if ((is->video_current_pts > (sp->pts + ((float) sp->sub.end_display_time / 1000)))
                                || (sp2 && is->video_current_pts > (sp2->pts + ((float) sp2->sub.start_display_time / 1000))))
                        {
                            free_subpicture(sp);

                            /* update queue size and signal for next picture */
                            if (++is->subpq_rindex == SUBPICTURE_QUEUE_SIZE)
                                is->subpq_rindex = 0;

                            SDL_LockMutex(is->subpq_mutex);
                            is->subpq_size--;
                            SDL_CondSignal(is->subpq_cond);
                            SDL_UnlockMutex(is->subpq_mutex);
                        }
                    }
                }
            }

            /* display picture */
            video_display(is);

            /* update queue size and signal for next picture */
            if (++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE)
                is->pictq_rindex = 0;

            SDL_LockMutex(is->pictq_mutex);
            is->pictq_size--;
            SDL_CondSignal(is->pictq_cond);
            SDL_UnlockMutex(is->pictq_mutex);
        }
    } else if (is->audio_st) {
        /* draw the next audio frame */

        schedule_refresh(is, 40);

        /* if only audio stream, then display the audio bars (better
           than nothing, just to test the implementation */

        /* display picture */
        video_display(is);
    } else {
        schedule_refresh(is, 100);
    }
    if (show_status) {
        static int64_t last_time;
        int64_t cur_time;
        int aqsize, vqsize, sqsize;
        double av_diff;

        cur_time = av_gettime();
        if (!last_time || (cur_time - last_time) >= 500 * 1000) {
            aqsize = 0;
            vqsize = 0;
            sqsize = 0;
            if (is->audio_st)
                aqsize = is->audioq.size;
            if (is->video_st)
                vqsize = is->videoq.size;
            if (is->subtitle_st)
                sqsize = is->subtitleq.size;
            av_diff = 0;
            if (is->audio_st && is->video_st)
                av_diff = get_audio_clock(is) - get_video_clock(is);
            printf("%7.2f A-V:%7.3f aq=%5dKB vq=%5dKB sq=%5dB    \r",
                   get_master_clock(is), av_diff, aqsize / 1024, vqsize / 1024, sqsize);
            fflush(stdout);
            last_time = cur_time;
        }
    }
}

/* allocate a picture (needs to do that in main thread to avoid
   potential locking problems */
static void alloc_picture(void *opaque)
{
    VideoState *is = opaque;
    VideoPicture *vp;

    vp = &is->pictq[is->pictq_windex];

    if (vp->bmp)
        SDL_FreeYUVOverlay(vp->bmp);

#if 0
    /* XXX: use generic function */
    /* XXX: disable overlay if no hardware acceleration or if RGB format */
    switch(is->video_st->codec->pix_fmt) {
    case PIX_FMT_YUV420P:
    case PIX_FMT_YUV422P:
    case PIX_FMT_YUV444P:
    case PIX_FMT_YUYV422:
    case PIX_FMT_YUV410P:
    case PIX_FMT_YUV411P:
        is_yuv = 1;
        break;
    default:
        is_yuv = 0;
        break;
    }
#endif
    vp->bmp = SDL_CreateYUVOverlay(is->video_st->codec->width,
                                   is->video_st->codec->height,
                                   SDL_YV12_OVERLAY,
                                   screen);
    vp->width = is->video_st->codec->width;
    vp->height = is->video_st->codec->height;

    SDL_LockMutex(is->pictq_mutex);
    vp->allocated = 1;
    SDL_CondSignal(is->pictq_cond);
    SDL_UnlockMutex(is->pictq_mutex);
}

/**
 *
 * @param pts the dts of the pkt / pts of the frame and guessed if not known
 */
static int queue_picture(VideoState *is, AVFrame *src_frame, double pts)
{
    VideoPicture *vp;
    int dst_pix_fmt;
    AVPicture pict;
    static struct SwsContext *img_convert_ctx;

    /* wait until we have space to put a new picture */
    SDL_LockMutex(is->pictq_mutex);
    while (is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE &&
           !is->videoq.abort_request) {
        SDL_CondWait(is->pictq_cond, is->pictq_mutex);
    }
    SDL_UnlockMutex(is->pictq_mutex);

    if (is->videoq.abort_request)
        return -1;

    vp = &is->pictq[is->pictq_windex];

    /* alloc or resize hardware picture buffer */
    if (!vp->bmp ||
        vp->width != is->video_st->codec->width ||
        vp->height != is->video_st->codec->height) {
        SDL_Event event;

        vp->allocated = 0;

        /* the allocation must be done in the main thread to avoid
           locking problems */
        event.type = FF_ALLOC_EVENT;
        event.user.data1 = is;
        SDL_PushEvent(&event);

        /* wait until the picture is allocated */
        SDL_LockMutex(is->pictq_mutex);
        while (!vp->allocated && !is->videoq.abort_request) {
            SDL_CondWait(is->pictq_cond, is->pictq_mutex);
        }
        SDL_UnlockMutex(is->pictq_mutex);

        if (is->videoq.abort_request)
            return -1;
    }

    /* if the frame is not skipped, then display it */
    if (vp->bmp) {
        /* get a pointer on the bitmap */
        SDL_LockYUVOverlay (vp->bmp);

        dst_pix_fmt = PIX_FMT_YUV420P;
        pict.data[0] = vp->bmp->pixels[0];
        pict.data[1] = vp->bmp->pixels[2];
        pict.data[2] = vp->bmp->pixels[1];

        pict.linesize[0] = vp->bmp->pitches[0];
        pict.linesize[1] = vp->bmp->pitches[2];
        pict.linesize[2] = vp->bmp->pitches[1];
        sws_flags = av_get_int(sws_opts, "sws_flags", NULL);
        img_convert_ctx = sws_getCachedContext(img_convert_ctx,
            is->video_st->codec->width, is->video_st->codec->height,
            is->video_st->codec->pix_fmt,
            is->video_st->codec->width, is->video_st->codec->height,
            dst_pix_fmt, sws_flags, NULL, NULL, NULL);
        if (img_convert_ctx == NULL) {
            fprintf(stderr, "Cannot initialize the conversion context\n");
            exit(1);
        }
        sws_scale(img_convert_ctx, src_frame->data, src_frame->linesize,
                  0, is->video_st->codec->height, pict.data, pict.linesize);
        /* update the bitmap content */
        SDL_UnlockYUVOverlay(vp->bmp);

        vp->pts = pts;

        /* now we can update the picture count */
        if (++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE)
            is->pictq_windex = 0;
        SDL_LockMutex(is->pictq_mutex);
        is->pictq_size++;
        SDL_UnlockMutex(is->pictq_mutex);
    }
    return 0;
}

/**
 * compute the exact PTS for the picture if it is omitted in the stream
 * @param pts1 the dts of the pkt / pts of the frame
 */
static int output_picture2(VideoState *is, AVFrame *src_frame, double pts1)
{
    double frame_delay, pts;

    pts = pts1;

    if (pts != 0) {
        /* update video clock with pts, if present */
        is->video_clock = pts;
    } else {
        pts = is->video_clock;
    }
    /* update video clock for next frame */
    frame_delay = av_q2d(is->video_st->codec->time_base);
    /* for MPEG2, the frame can be repeated, so we update the
       clock accordingly */
    frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
    is->video_clock += frame_delay;

#if defined(DEBUG_SYNC) && 0
    {
        int ftype;
        if (src_frame->pict_type == FF_B_TYPE)
            ftype = 'B';
        else if (src_frame->pict_type == FF_I_TYPE)
            ftype = 'I';
        else
            ftype = 'P';
        printf("frame_type=%c clock=%0.3f pts=%0.3f\n",
               ftype, pts, pts1);
    }
#endif
    return queue_picture(is, src_frame, pts);
}

static int video_thread(void *arg)
{
    VideoState *is = arg;
    AVPacket pkt1, *pkt = &pkt1;
    int len1, got_picture;
    AVFrame *frame= avcodec_alloc_frame();
    double pts;

    for(;;) {
        while (is->paused && !is->videoq.abort_request) {
            SDL_Delay(10);
        }
        if (packet_queue_get(&is->videoq, pkt, 1) < 0)
            break;

        if(pkt->data == flush_pkt.data){
            avcodec_flush_buffers(is->video_st->codec);
            continue;
        }

        /* NOTE: ipts is the PTS of the _first_ picture beginning in
           this packet, if any */
        is->video_st->codec->reordered_opaque= pkt->pts;
        len1 = avcodec_decode_video(is->video_st->codec,
                                    frame, &got_picture,
                                    pkt->data, pkt->size);

        if(   (decoder_reorder_pts || pkt->dts == AV_NOPTS_VALUE)
           && frame->reordered_opaque != AV_NOPTS_VALUE)
            pts= frame->reordered_opaque;
        else if(pkt->dts != AV_NOPTS_VALUE)
            pts= pkt->dts;
        else
            pts= 0;
        pts *= av_q2d(is->video_st->time_base);

//            if (len1 < 0)
//                break;
        if (got_picture) {
            if (output_picture2(is, frame, pts) < 0)
                goto the_end;
        }
        av_free_packet(pkt);
        if (step)
            if (cur_stream)
                stream_pause(cur_stream);
    }
 the_end:
    av_free(frame);
    return 0;
}

static int subtitle_thread(void *arg)
{
    VideoState *is = arg;
    SubPicture *sp;
    AVPacket pkt1, *pkt = &pkt1;
    int len1, got_subtitle;
    double pts;
    int i, j;
    int r, g, b, y, u, v, a;

    for(;;) {
        while (is->paused && !is->subtitleq.abort_request) {
            SDL_Delay(10);
        }
        if (packet_queue_get(&is->subtitleq, pkt, 1) < 0)
            break;

        if(pkt->data == flush_pkt.data){
            avcodec_flush_buffers(is->subtitle_st->codec);
            continue;
        }
        SDL_LockMutex(is->subpq_mutex);
        while (is->subpq_size >= SUBPICTURE_QUEUE_SIZE &&
               !is->subtitleq.abort_request) {
            SDL_CondWait(is->subpq_cond, is->subpq_mutex);
        }
        SDL_UnlockMutex(is->subpq_mutex);

        if (is->subtitleq.abort_request)
            goto the_end;

        sp = &is->subpq[is->subpq_windex];

       /* NOTE: ipts is the PTS of the _first_ picture beginning in
           this packet, if any */
        pts = 0;
        if (pkt->pts != AV_NOPTS_VALUE)
            pts = av_q2d(is->subtitle_st->time_base)*pkt->pts;

        len1 = avcodec_decode_subtitle(is->subtitle_st->codec,
                                    &sp->sub, &got_subtitle,
                                    pkt->data, pkt->size);
//            if (len1 < 0)
//                break;
        if (got_subtitle && sp->sub.format == 0) {
            sp->pts = pts;

            for (i = 0; i < sp->sub.num_rects; i++)
            {
                for (j = 0; j < sp->sub.rects[i]->nb_colors; j++)
                {
                    RGBA_IN(r, g, b, a, (uint32_t*)sp->sub.rects[i]->pict.data[1] + j);
                    y = RGB_TO_Y_CCIR(r, g, b);
                    u = RGB_TO_U_CCIR(r, g, b, 0);
                    v = RGB_TO_V_CCIR(r, g, b, 0);
                    YUVA_OUT((uint32_t*)sp->sub.rects[i]->pict.data[1] + j, y, u, v, a);
                }
            }

            /* now we can update the picture count */
            if (++is->subpq_windex == SUBPICTURE_QUEUE_SIZE)
                is->subpq_windex = 0;
            SDL_LockMutex(is->subpq_mutex);
            is->subpq_size++;
            SDL_UnlockMutex(is->subpq_mutex);
        }
        av_free_packet(pkt);
//        if (step)
//            if (cur_stream)
//                stream_pause(cur_stream);
    }
 the_end:
    return 0;
}

/* copy samples for viewing in editor window */
static void update_sample_display(VideoState *is, short *samples, int samples_size)
{
    int size, len, channels;

    channels = is->audio_st->codec->channels;

    size = samples_size / sizeof(short);
    while (size > 0) {
        len = SAMPLE_ARRAY_SIZE - is->sample_array_index;
        if (len > size)
            len = size;
        memcpy(is->sample_array + is->sample_array_index, samples, len * sizeof(short));
        samples += len;
        is->sample_array_index += len;
        if (is->sample_array_index >= SAMPLE_ARRAY_SIZE)
            is->sample_array_index = 0;
        size -= len;
    }
}

/* return the new audio buffer size (samples can be added or deleted
   to get better sync if video or external master clock) */
static int synchronize_audio(VideoState *is, short *samples,
                             int samples_size1, double pts)
{
    int n, samples_size;
    double ref_clock;

    n = 2 * is->audio_st->codec->channels;
    samples_size = samples_size1;

    /* if not master, then we try to remove or add samples to correct the clock */
    if (((is->av_sync_type == AV_SYNC_VIDEO_MASTER && is->video_st) ||
         is->av_sync_type == AV_SYNC_EXTERNAL_CLOCK)) {
        double diff, avg_diff;
        int wanted_size, min_size, max_size, nb_samples;

        ref_clock = get_master_clock(is);
        diff = get_audio_clock(is) - ref_clock;

        if (diff < AV_NOSYNC_THRESHOLD) {
            is->audio_diff_cum = diff + is->audio_diff_avg_coef * is->audio_diff_cum;
            if (is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
                /* not enough measures to have a correct estimate */
                is->audio_diff_avg_count++;
            } else {
                /* estimate the A-V difference */
                avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);

                if (fabs(avg_diff) >= is->audio_diff_threshold) {
                    wanted_size = samples_size + ((int)(diff * is->audio_st->codec->sample_rate) * n);
                    nb_samples = samples_size / n;

                    min_size = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX)) / 100) * n;
                    max_size = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX)) / 100) * n;
                    if (wanted_size < min_size)
                        wanted_size = min_size;
                    else if (wanted_size > max_size)
                        wanted_size = max_size;

                    /* add or remove samples to correction the synchro */
                    if (wanted_size < samples_size) {
                        /* remove samples */
                        samples_size = wanted_size;
                    } else if (wanted_size > samples_size) {
                        uint8_t *samples_end, *q;
                        int nb;

                        /* add samples */
                        nb = (samples_size - wanted_size);
                        samples_end = (uint8_t *)samples + samples_size - n;
                        q = samples_end + n;
                        while (nb > 0) {
                            memcpy(q, samples_end, n);
                            q += n;
                            nb -= n;
                        }
                        samples_size = wanted_size;
                    }
                }
#if 0
                printf("diff=%f adiff=%f sample_diff=%d apts=%0.3f vpts=%0.3f %f\n",
                       diff, avg_diff, samples_size - samples_size1,
                       is->audio_clock, is->video_clock, is->audio_diff_threshold);
#endif
            }
        } else {
            /* too big difference : may be initial PTS errors, so
               reset A-V filter */
            is->audio_diff_avg_count = 0;
            is->audio_diff_cum = 0;
        }
    }

    return samples_size;
}

/* decode one audio frame and returns its uncompressed size */
static int audio_decode_frame(VideoState *is, double *pts_ptr)
{
    AVPacket *pkt = &is->audio_pkt;
    AVCodecContext *dec= is->audio_st->codec;
    int n, len1, data_size;
    double pts;

    for(;;) {
        /* NOTE: the audio packet can contain several frames */
        while (is->audio_pkt_size > 0) {
            data_size = sizeof(is->audio_buf1);
            len1 = avcodec_decode_audio2(dec,
                                        (int16_t *)is->audio_buf1, &data_size,
                                        is->audio_pkt_data, is->audio_pkt_size);
            if (len1 < 0) {
                /* if error, we skip the frame */
                is->audio_pkt_size = 0;
                break;
            }

            is->audio_pkt_data += len1;
            is->audio_pkt_size -= len1;
            if (data_size <= 0)
                continue;

            if (dec->sample_fmt != is->audio_src_fmt) {
                if (is->reformat_ctx)
                    av_audio_convert_free(is->reformat_ctx);
                is->reformat_ctx= av_audio_convert_alloc(SAMPLE_FMT_S16, 1,
                                                         dec->sample_fmt, 1, NULL, 0);
                if (!is->reformat_ctx) {
                    fprintf(stderr, "Cannot convert %s sample format to %s sample format\n",
                        avcodec_get_sample_fmt_name(dec->sample_fmt),
                        avcodec_get_sample_fmt_name(SAMPLE_FMT_S16));
                        break;
                }
                is->audio_src_fmt= dec->sample_fmt;
            }

            if (is->reformat_ctx) {
                const void *ibuf[6]= {is->audio_buf1};
                void *obuf[6]= {is->audio_buf2};
                int istride[6]= {av_get_bits_per_sample_format(dec->sample_fmt)/8};
                int ostride[6]= {2};
                int len= data_size/istride[0];
                if (av_audio_convert(is->reformat_ctx, obuf, ostride, ibuf, istride, len)<0) {
                    printf("av_audio_convert() failed\n");
                    break;
                }
                is->audio_buf= is->audio_buf2;
                /* FIXME: existing code assume that data_size equals framesize*channels*2
                          remove this legacy cruft */
                data_size= len*2;
            }else{
                is->audio_buf= is->audio_buf1;
            }

            /* if no pts, then compute it */
            pts = is->audio_clock;
            *pts_ptr = pts;
            n = 2 * dec->channels;
            is->audio_clock += (double)data_size /
                (double)(n * dec->sample_rate);
#if defined(DEBUG_SYNC)
            {
                static double last_clock;
                printf("audio: delay=%0.3f clock=%0.3f pts=%0.3f\n",
                       is->audio_clock - last_clock,
                       is->audio_clock, pts);
                last_clock = is->audio_clock;
            }
#endif
            return data_size;
        }

        /* free the current packet */
        if (pkt->data)
            av_free_packet(pkt);

        if (is->paused || is->audioq.abort_request) {
            return -1;
        }

        /* read next packet */
        if (packet_queue_get(&is->audioq, pkt, 1) < 0)
            return -1;
        if(pkt->data == flush_pkt.data){
            avcodec_flush_buffers(dec);
            continue;
        }

        is->audio_pkt_data = pkt->data;
        is->audio_pkt_size = pkt->size;

        /* if update the audio clock with the pts */
        if (pkt->pts != AV_NOPTS_VALUE) {
            is->audio_clock = av_q2d(is->audio_st->time_base)*pkt->pts;
        }
    }
}

/* get the current audio output buffer size, in samples. With SDL, we
   cannot have a precise information */
static int audio_write_get_buf_size(VideoState *is)
{
    return is->audio_buf_size - is->audio_buf_index;
}


/* prepare a new audio buffer */
static void sdl_audio_callback(void *opaque, Uint8 *stream, int len)
{
    VideoState *is = opaque;
    int audio_size, len1;
    double pts;

    audio_callback_time = av_gettime();

    while (len > 0) {
        if (is->audio_buf_index >= is->audio_buf_size) {
           audio_size = audio_decode_frame(is, &pts);
           if (audio_size < 0) {
                /* if error, just output silence */
               is->audio_buf = is->audio_buf1;
               is->audio_buf_size = 1024;
               memset(is->audio_buf, 0, is->audio_buf_size);
           } else {
               if (is->show_audio)
                   update_sample_display(is, (int16_t *)is->audio_buf, audio_size);
               audio_size = synchronize_audio(is, (int16_t *)is->audio_buf, audio_size,
                                              pts);
               is->audio_buf_size = audio_size;
           }
           is->audio_buf_index = 0;
        }
        len1 = is->audio_buf_size - is->audio_buf_index;
        if (len1 > len)
            len1 = len;
        memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }
}

/* open a given stream. Return 0 if OK */
static int stream_component_open(VideoState *is, int stream_index)
{
    AVFormatContext *ic = is->ic;
    AVCodecContext *enc;
    AVCodec *codec;
    SDL_AudioSpec wanted_spec, spec;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return -1;
    enc = ic->streams[stream_index]->codec;

    /* prepare audio output */
    if (enc->codec_type == CODEC_TYPE_AUDIO) {
        if (enc->channels > 0) {
            enc->request_channels = FFMIN(2, enc->channels);
        } else {
            enc->request_channels = 2;
        }
    }

    codec = avcodec_find_decoder(enc->codec_id);
    enc->debug_mv = debug_mv;
    enc->debug = debug;
    enc->workaround_bugs = workaround_bugs;
    enc->lowres = lowres;
    if(lowres) enc->flags |= CODEC_FLAG_EMU_EDGE;
    enc->idct_algo= idct;
    if(fast) enc->flags2 |= CODEC_FLAG2_FAST;
    enc->skip_frame= skip_frame;
    enc->skip_idct= skip_idct;
    enc->skip_loop_filter= skip_loop_filter;
    enc->error_recognition= error_recognition;
    enc->error_concealment= error_concealment;

    set_context_opts(enc, avctx_opts[enc->codec_type], 0);

    if (!codec ||
        avcodec_open(enc, codec) < 0)
        return -1;

    /* prepare audio output */
    if (enc->codec_type == CODEC_TYPE_AUDIO) {
        wanted_spec.freq = enc->sample_rate;
        wanted_spec.format = AUDIO_S16SYS;
        wanted_spec.channels = enc->channels;
        wanted_spec.silence = 0;
        wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
        wanted_spec.callback = sdl_audio_callback;
        wanted_spec.userdata = is;
        if (SDL_OpenAudio(&wanted_spec, &spec) < 0) {
            fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
            return -1;
        }
        is->audio_hw_buf_size = spec.size;
        is->audio_src_fmt= SAMPLE_FMT_S16;
    }

    if(thread_count>1)
        avcodec_thread_init(enc, thread_count);
    enc->thread_count= thread_count;
    ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;
    switch(enc->codec_type) {
    case CODEC_TYPE_AUDIO:
        is->audio_stream = stream_index;
        is->audio_st = ic->streams[stream_index];
        is->audio_buf_size = 0;
        is->audio_buf_index = 0;

        /* init averaging filter */
        is->audio_diff_avg_coef = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
        is->audio_diff_avg_count = 0;
        /* since we do not have a precise anough audio fifo fullness,
           we correct audio sync only if larger than this threshold */
        is->audio_diff_threshold = 2.0 * SDL_AUDIO_BUFFER_SIZE / enc->sample_rate;

        memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
        packet_queue_init(&is->audioq);
        SDL_PauseAudio(0);
        break;
    case CODEC_TYPE_VIDEO:
        is->video_stream = stream_index;
        is->video_st = ic->streams[stream_index];

        is->frame_last_delay = 40e-3;
        is->frame_timer = (double)av_gettime() / 1000000.0;
        is->video_current_pts_time = av_gettime();

        packet_queue_init(&is->videoq);
        is->video_tid = SDL_CreateThread(video_thread, is);
        break;
    case CODEC_TYPE_SUBTITLE:
        is->subtitle_stream = stream_index;
        is->subtitle_st = ic->streams[stream_index];
        packet_queue_init(&is->subtitleq);

        is->subtitle_tid = SDL_CreateThread(subtitle_thread, is);
        break;
    default:
        break;
    }
    return 0;
}

static void stream_component_close(VideoState *is, int stream_index)
{
    AVFormatContext *ic = is->ic;
    AVCodecContext *enc;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return;
    enc = ic->streams[stream_index]->codec;

    switch(enc->codec_type) {
    case CODEC_TYPE_AUDIO:
        packet_queue_abort(&is->audioq);

        SDL_CloseAudio();

        packet_queue_end(&is->audioq);
        if (is->reformat_ctx)
            av_audio_convert_free(is->reformat_ctx);
        break;
    case CODEC_TYPE_VIDEO:
        packet_queue_abort(&is->videoq);

        /* note: we also signal this mutex to make sure we deblock the
           video thread in all cases */
        SDL_LockMutex(is->pictq_mutex);
        SDL_CondSignal(is->pictq_cond);
        SDL_UnlockMutex(is->pictq_mutex);

        SDL_WaitThread(is->video_tid, NULL);

        packet_queue_end(&is->videoq);
        break;
    case CODEC_TYPE_SUBTITLE:
        packet_queue_abort(&is->subtitleq);

        /* note: we also signal this mutex to make sure we deblock the
           video thread in all cases */
        SDL_LockMutex(is->subpq_mutex);
        is->subtitle_stream_changed = 1;

        SDL_CondSignal(is->subpq_cond);
        SDL_UnlockMutex(is->subpq_mutex);

        SDL_WaitThread(is->subtitle_tid, NULL);

        packet_queue_end(&is->subtitleq);
        break;
    default:
        break;
    }

    ic->streams[stream_index]->discard = AVDISCARD_ALL;
    avcodec_close(enc);
    switch(enc->codec_type) {
    case CODEC_TYPE_AUDIO:
        is->audio_st = NULL;
        is->audio_stream = -1;
        break;
    case CODEC_TYPE_VIDEO:
        is->video_st = NULL;
        is->video_stream = -1;
        break;
    case CODEC_TYPE_SUBTITLE:
        is->subtitle_st = NULL;
        is->subtitle_stream = -1;
        break;
    default:
        break;
    }
}

static void dump_stream_info(const AVFormatContext *s)
{
    AVMetadataTag *tag = NULL;
    while ((tag=av_metadata_get(s->metadata,"",tag,AV_METADATA_IGNORE_SUFFIX)))
        fprintf(stderr, "%s: %s\n", tag->key, tag->value);
}

/* since we have only one decoding thread, we can use a global
   variable instead of a thread local variable */
static VideoState *global_video_state;

static int decode_interrupt_cb(void)
{
    return (global_video_state && global_video_state->abort_request);
}

/* this thread gets the stream from the disk or the network */
static int decode_thread(void *arg)
{
    VideoState *is = arg;
    AVFormatContext *ic;
    int err, i, ret, video_index, audio_index, subtitle_index;
    AVPacket pkt1, *pkt = &pkt1;
    AVFormatParameters params, *ap = &params;

    video_index = -1;
    audio_index = -1;
    subtitle_index = -1;
    is->video_stream = -1;
    is->audio_stream = -1;
    is->subtitle_stream = -1;

    global_video_state = is;
    url_set_interrupt_cb(decode_interrupt_cb);

    memset(ap, 0, sizeof(*ap));

    ap->width = frame_width;
    ap->height= frame_height;
    ap->time_base= (AVRational){1, 25};
    ap->pix_fmt = frame_pix_fmt;

    err = av_open_input_file(&ic, is->filename, is->iformat, 0, ap);
    if (err < 0) {
        print_error(is->filename, err);
        ret = -1;
        goto fail;
    }
    is->ic = ic;

    if(genpts)
        ic->flags |= AVFMT_FLAG_GENPTS;

    err = av_find_stream_info(ic);
    if (err < 0) {
        fprintf(stderr, "%s: could not find codec parameters\n", is->filename);
        ret = -1;
        goto fail;
    }
    if(ic->pb)
        ic->pb->eof_reached= 0; //FIXME hack, ffplay maybe should not use url_feof() to test for the end

    /* if seeking requested, we execute it */
    if (start_time != AV_NOPTS_VALUE) {
        int64_t timestamp;

        timestamp = start_time;
        /* add the stream start time */
        if (ic->start_time != AV_NOPTS_VALUE)
            timestamp += ic->start_time;
        ret = av_seek_frame(ic, -1, timestamp, AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            fprintf(stderr, "%s: could not seek to position %0.3f\n",
                    is->filename, (double)timestamp / AV_TIME_BASE);
        }
    }

    for(i = 0; i < ic->nb_streams; i++) {
        AVCodecContext *enc = ic->streams[i]->codec;
        ic->streams[i]->discard = AVDISCARD_ALL;
        switch(enc->codec_type) {
        case CODEC_TYPE_AUDIO:
            if (wanted_audio_stream-- >= 0 && !audio_disable)
                audio_index = i;
            break;
        case CODEC_TYPE_VIDEO:
            if (wanted_video_stream-- >= 0 && !video_disable)
                video_index = i;
            break;
        case CODEC_TYPE_SUBTITLE:
            if (wanted_subtitle_stream-- >= 0 && !video_disable)
                subtitle_index = i;
            break;
        default:
            break;
        }
    }
    if (show_status) {
        dump_format(ic, 0, is->filename, 0);
        dump_stream_info(ic);
    }

    /* open the streams */
    if (audio_index >= 0) {
        stream_component_open(is, audio_index);
    }

    if (video_index >= 0) {
        stream_component_open(is, video_index);
    } else {
        if (!display_disable)
            is->show_audio = 1;
    }

    if (subtitle_index >= 0) {
        stream_component_open(is, subtitle_index);
    }

    if (is->video_stream < 0 && is->audio_stream < 0) {
        fprintf(stderr, "%s: could not open codecs\n", is->filename);
        ret = -1;
        goto fail;
    }

    for(;;) {
        if (is->abort_request)
            break;
        if (is->paused != is->last_paused) {
            is->last_paused = is->paused;
            if (is->paused)
                av_read_pause(ic);
            else
                av_read_play(ic);
        }
#if CONFIG_RTSP_DEMUXER
        if (is->paused && !strcmp(ic->iformat->name, "rtsp")) {
            /* wait 10 ms to avoid trying to get another packet */
            /* XXX: horrible */
            SDL_Delay(10);
            continue;
        }
#endif
        if (is->seek_req) {
            int stream_index= -1;
            int64_t seek_target= is->seek_pos;

            if     (is->   video_stream >= 0) stream_index= is->   video_stream;
            else if(is->   audio_stream >= 0) stream_index= is->   audio_stream;
            else if(is->subtitle_stream >= 0) stream_index= is->subtitle_stream;

            if(stream_index>=0){
                seek_target= av_rescale_q(seek_target, AV_TIME_BASE_Q, ic->streams[stream_index]->time_base);
            }

            ret = av_seek_frame(is->ic, stream_index, seek_target, is->seek_flags);
            if (ret < 0) {
                fprintf(stderr, "%s: error while seeking\n", is->ic->filename);
            }else{
                if (is->audio_stream >= 0) {
                    packet_queue_flush(&is->audioq);
                    packet_queue_put(&is->audioq, &flush_pkt);
                }
                if (is->subtitle_stream >= 0) {
                    packet_queue_flush(&is->subtitleq);
                    packet_queue_put(&is->subtitleq, &flush_pkt);
                }
                if (is->video_stream >= 0) {
                    packet_queue_flush(&is->videoq);
                    packet_queue_put(&is->videoq, &flush_pkt);
                }
            }
            is->seek_req = 0;
        }

        /* if the queue are full, no need to read more */
        if (is->audioq.size > MAX_AUDIOQ_SIZE ||
            is->videoq.size > MAX_VIDEOQ_SIZE ||
            is->subtitleq.size > MAX_SUBTITLEQ_SIZE) {
            /* wait 10 ms */
            SDL_Delay(10);
            continue;
        }
        if(url_feof(ic->pb)) {
            av_init_packet(pkt);
            pkt->data=NULL;
            pkt->size=0;
            pkt->stream_index= is->video_stream;
            packet_queue_put(&is->videoq, pkt);
            continue;
        }
        ret = av_read_frame(ic, pkt);
        if (ret < 0) {
            if (ret != AVERROR_EOF && url_ferror(ic->pb) == 0) {
                SDL_Delay(100); /* wait for user event */
                continue;
            } else
                break;
        }
        if (pkt->stream_index == is->audio_stream) {
            packet_queue_put(&is->audioq, pkt);
        } else if (pkt->stream_index == is->video_stream) {
            packet_queue_put(&is->videoq, pkt);
        } else if (pkt->stream_index == is->subtitle_stream) {
            packet_queue_put(&is->subtitleq, pkt);
        } else {
            av_free_packet(pkt);
        }
    }
    /* wait until the end */
    while (!is->abort_request) {
        SDL_Delay(100);
    }

    ret = 0;
 fail:
    /* disable interrupting */
    global_video_state = NULL;

    /* close each stream */
    if (is->audio_stream >= 0)
        stream_component_close(is, is->audio_stream);
    if (is->video_stream >= 0)
        stream_component_close(is, is->video_stream);
    if (is->subtitle_stream >= 0)
        stream_component_close(is, is->subtitle_stream);
    if (is->ic) {
        av_close_input_file(is->ic);
        is->ic = NULL; /* safety */
    }
    url_set_interrupt_cb(NULL);

    if (ret != 0) {
        SDL_Event event;

        event.type = FF_QUIT_EVENT;
        event.user.data1 = is;
        SDL_PushEvent(&event);
    }
    return 0;
}

static VideoState *stream_open(const char *filename, AVInputFormat *iformat)
{
    VideoState *is;

    is = av_mallocz(sizeof(VideoState));
    if (!is)
        return NULL;
    av_strlcpy(is->filename, filename, sizeof(is->filename));
    is->iformat = iformat;
    is->ytop = 0;
    is->xleft = 0;

    /* start video display */
    is->pictq_mutex = SDL_CreateMutex();
    is->pictq_cond = SDL_CreateCond();

    is->subpq_mutex = SDL_CreateMutex();
    is->subpq_cond = SDL_CreateCond();

    /* add the refresh timer to draw the picture */
    schedule_refresh(is, 40);

    is->av_sync_type = av_sync_type;
    is->parse_tid = SDL_CreateThread(decode_thread, is);
    if (!is->parse_tid) {
        av_free(is);
        return NULL;
    }
    return is;
}

static void stream_close(VideoState *is)
{
    VideoPicture *vp;
    int i;
    /* XXX: use a special url_shutdown call to abort parse cleanly */
    is->abort_request = 1;
    SDL_WaitThread(is->parse_tid, NULL);

    /* free all pictures */
    for(i=0;i<VIDEO_PICTURE_QUEUE_SIZE; i++) {
        vp = &is->pictq[i];
        if (vp->bmp) {
            SDL_FreeYUVOverlay(vp->bmp);
            vp->bmp = NULL;
        }
    }
    SDL_DestroyMutex(is->pictq_mutex);
    SDL_DestroyCond(is->pictq_cond);
    SDL_DestroyMutex(is->subpq_mutex);
    SDL_DestroyCond(is->subpq_cond);
}

static void stream_cycle_channel(VideoState *is, int codec_type)
{
    AVFormatContext *ic = is->ic;
    int start_index, stream_index;
    AVStream *st;

    if (codec_type == CODEC_TYPE_VIDEO)
        start_index = is->video_stream;
    else if (codec_type == CODEC_TYPE_AUDIO)
        start_index = is->audio_stream;
    else
        start_index = is->subtitle_stream;
    if (start_index < (codec_type == CODEC_TYPE_SUBTITLE ? -1 : 0))
        return;
    stream_index = start_index;
    for(;;) {
        if (++stream_index >= is->ic->nb_streams)
        {
            if (codec_type == CODEC_TYPE_SUBTITLE)
            {
                stream_index = -1;
                goto the_end;
            } else
                stream_index = 0;
        }
        if (stream_index == start_index)
            return;
        st = ic->streams[stream_index];
        if (st->codec->codec_type == codec_type) {
            /* check that parameters are OK */
            switch(codec_type) {
            case CODEC_TYPE_AUDIO:
                if (st->codec->sample_rate != 0 &&
                    st->codec->channels != 0)
                    goto the_end;
                break;
            case CODEC_TYPE_VIDEO:
            case CODEC_TYPE_SUBTITLE:
                goto the_end;
            default:
                break;
            }
        }
    }
 the_end:
    stream_component_close(is, start_index);
    stream_component_open(is, stream_index);
}


static void toggle_full_screen(void)
{
    is_full_screen = !is_full_screen;
    if (!fs_screen_width) {
        /* use default SDL method */
//        SDL_WM_ToggleFullScreen(screen);
    }
    video_open(cur_stream);
}

static void toggle_pause(void)
{
    if (cur_stream)
        stream_pause(cur_stream);
    step = 0;
}

static void step_to_next_frame(void)
{
    if (cur_stream) {
        /* if the stream is paused unpause it, then step */
        if (cur_stream->paused)
            stream_pause(cur_stream);
    }
    step = 1;
}

static void do_exit(void)
{
    if (cur_stream) {
        stream_close(cur_stream);
        cur_stream = NULL;
    }
    if (show_status)
        printf("\n");
    SDL_Quit();
    exit(0);
}

static void toggle_audio_display(void)
{
    if (cur_stream) {
        cur_stream->show_audio = !cur_stream->show_audio;
    }
}

/* handle an event sent by the GUI */
static void event_loop(void)
{
    SDL_Event event;
    double incr, pos, frac;

    for(;;) {
        SDL_WaitEvent(&event);
        switch(event.type) {
        case SDL_KEYDOWN:
            switch(event.key.keysym.sym) {
            case SDLK_ESCAPE:
            case SDLK_q:
                do_exit();
                break;
            case SDLK_f:
                toggle_full_screen();
                break;
            case SDLK_p:
            case SDLK_SPACE:
                toggle_pause();
                break;
            case SDLK_s: //S: Step to next frame
                step_to_next_frame();
                break;
            case SDLK_a:
                if (cur_stream)
                    stream_cycle_channel(cur_stream, CODEC_TYPE_AUDIO);
                break;
            case SDLK_v:
                if (cur_stream)
                    stream_cycle_channel(cur_stream, CODEC_TYPE_VIDEO);
                break;
            case SDLK_t:
                if (cur_stream)
                    stream_cycle_channel(cur_stream, CODEC_TYPE_SUBTITLE);
                break;
            case SDLK_w:
                toggle_audio_display();
                break;
            case SDLK_LEFT:
                incr = -10.0;
                goto do_seek;
            case SDLK_RIGHT:
                incr = 10.0;
                goto do_seek;
            case SDLK_UP:
                incr = 60.0;
                goto do_seek;
            case SDLK_DOWN:
                incr = -60.0;
            do_seek:
                if (cur_stream) {
                    if (seek_by_bytes) {
                        pos = url_ftell(cur_stream->ic->pb);
                        if (cur_stream->ic->bit_rate)
                            incr *= cur_stream->ic->bit_rate / 60.0;
                        else
                            incr *= 180000.0;
                        pos += incr;
                        stream_seek(cur_stream, pos, incr);
                    } else {
                        pos = get_master_clock(cur_stream);
                        pos += incr;
                        stream_seek(cur_stream, (int64_t)(pos * AV_TIME_BASE), incr);
                    }
                }
                break;
            default:
                break;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (cur_stream) {
                int ns, hh, mm, ss;
                int tns, thh, tmm, tss;
                tns = cur_stream->ic->duration/1000000LL;
                thh = tns/3600;
                tmm = (tns%3600)/60;
                tss = (tns%60);
                frac = (double)event.button.x/(double)cur_stream->width;
                ns = frac*tns;
                hh = ns/3600;
                mm = (ns%3600)/60;
                ss = (ns%60);
                fprintf(stderr, "Seek to %2.0f%% (%2d:%02d:%02d) of total duration (%2d:%02d:%02d)       \n", frac*100,
                        hh, mm, ss, thh, tmm, tss);
                stream_seek(cur_stream, (int64_t)(cur_stream->ic->start_time+frac*cur_stream->ic->duration), 0);
            }
            break;
        case SDL_VIDEORESIZE:
            if (cur_stream) {
                screen = SDL_SetVideoMode(event.resize.w, event.resize.h, 0,
                                          SDL_HWSURFACE|SDL_RESIZABLE|SDL_ASYNCBLIT|SDL_HWACCEL);
                screen_width = cur_stream->width = event.resize.w;
                screen_height= cur_stream->height= event.resize.h;
            }
            break;
        case SDL_QUIT:
        case FF_QUIT_EVENT:
            do_exit();
            break;
        case FF_ALLOC_EVENT:
            video_open(event.user.data1);
            alloc_picture(event.user.data1);
            break;
        case FF_REFRESH_EVENT:
            video_refresh_timer(event.user.data1);
            break;
        default:
            break;
        }
    }
}

static void opt_frame_size(const char *arg)
{
    if (av_parse_video_frame_size(&frame_width, &frame_height, arg) < 0) {
        fprintf(stderr, "Incorrect frame size\n");
        exit(1);
    }
    if ((frame_width % 2) != 0 || (frame_height % 2) != 0) {
        fprintf(stderr, "Frame size must be a multiple of 2\n");
        exit(1);
    }
}

static int opt_width(const char *opt, const char *arg)
{
    screen_width = parse_number_or_die(opt, arg, OPT_INT64, 1, INT_MAX);
    return 0;
}

static int opt_height(const char *opt, const char *arg)
{
    screen_height = parse_number_or_die(opt, arg, OPT_INT64, 1, INT_MAX);
    return 0;
}

static void opt_format(const char *arg)
{
    file_iformat = av_find_input_format(arg);
    if (!file_iformat) {
        fprintf(stderr, "Unknown input format: %s\n", arg);
        exit(1);
    }
}

static void opt_frame_pix_fmt(const char *arg)
{
    frame_pix_fmt = avcodec_get_pix_fmt(arg);
}

static int opt_sync(const char *opt, const char *arg)
{
    if (!strcmp(arg, "audio"))
        av_sync_type = AV_SYNC_AUDIO_MASTER;
    else if (!strcmp(arg, "video"))
        av_sync_type = AV_SYNC_VIDEO_MASTER;
    else if (!strcmp(arg, "ext"))
        av_sync_type = AV_SYNC_EXTERNAL_CLOCK;
    else {
        fprintf(stderr, "Unknown value for %s: %s\n", opt, arg);
        exit(1);
    }
    return 0;
}

static int opt_seek(const char *opt, const char *arg)
{
    start_time = parse_time_or_die(opt, arg, 1);
    return 0;
}

static int opt_debug(const char *opt, const char *arg)
{
    av_log_set_level(99);
    debug = parse_number_or_die(opt, arg, OPT_INT64, 0, INT_MAX);
    return 0;
}

static int opt_vismv(const char *opt, const char *arg)
{
    debug_mv = parse_number_or_die(opt, arg, OPT_INT64, INT_MIN, INT_MAX);
    return 0;
}

static int opt_thread_count(const char *opt, const char *arg)
{
    thread_count= parse_number_or_die(opt, arg, OPT_INT64, 0, INT_MAX);
#if !HAVE_THREADS
    fprintf(stderr, "Warning: not compiled with thread support, using thread emulation\n");
#endif
    return 0;
}

static const OptionDef options[] = {
    { "h", OPT_EXIT, {(void*)show_help}, "show help" },
    { "version", OPT_EXIT, {(void*)show_version}, "show version" },
    { "L", OPT_EXIT, {(void*)show_license}, "show license" },
    { "formats", OPT_EXIT, {(void*)show_formats}, "show available formats, codecs, protocols, ..." },
    { "x", HAS_ARG | OPT_FUNC2, {(void*)opt_width}, "force displayed width", "width" },
    { "y", HAS_ARG | OPT_FUNC2, {(void*)opt_height}, "force displayed height", "height" },
    { "s", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_size}, "set frame size (WxH or abbreviation)", "size" },
    { "fs", OPT_BOOL, {(void*)&is_full_screen}, "force full screen" },
    { "an", OPT_BOOL, {(void*)&audio_disable}, "disable audio" },
    { "vn", OPT_BOOL, {(void*)&video_disable}, "disable video" },
    { "ast", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&wanted_audio_stream}, "select desired audio stream", "stream_number" },
    { "vst", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&wanted_video_stream}, "select desired video stream", "stream_number" },
    { "sst", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&wanted_subtitle_stream}, "select desired subtitle stream", "stream_number" },
    { "ss", HAS_ARG | OPT_FUNC2, {(void*)&opt_seek}, "seek to a given position in seconds", "pos" },
    { "bytes", OPT_BOOL, {(void*)&seek_by_bytes}, "seek by bytes" },
    { "nodisp", OPT_BOOL, {(void*)&display_disable}, "disable graphical display" },
    { "f", HAS_ARG, {(void*)opt_format}, "force format", "fmt" },
    { "pix_fmt", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_frame_pix_fmt}, "set pixel format", "format" },
    { "stats", OPT_BOOL | OPT_EXPERT, {(void*)&show_status}, "show status", "" },
    { "debug", HAS_ARG | OPT_FUNC2 | OPT_EXPERT, {(void*)opt_debug}, "print specific debug info", "" },
    { "bug", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&workaround_bugs}, "workaround bugs", "" },
    { "vismv", HAS_ARG | OPT_FUNC2 | OPT_EXPERT, {(void*)opt_vismv}, "visualize motion vectors", "" },
    { "fast", OPT_BOOL | OPT_EXPERT, {(void*)&fast}, "non spec compliant optimizations", "" },
    { "genpts", OPT_BOOL | OPT_EXPERT, {(void*)&genpts}, "generate pts", "" },
    { "drp", OPT_BOOL |OPT_EXPERT, {(void*)&decoder_reorder_pts}, "let decoder reorder pts", ""},
    { "lowres", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&lowres}, "", "" },
    { "skiploop", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&skip_loop_filter}, "", "" },
    { "skipframe", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&skip_frame}, "", "" },
    { "skipidct", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&skip_idct}, "", "" },
    { "idct", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&idct}, "set idct algo",  "algo" },
    { "er", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&error_recognition}, "set error detection threshold (0-4)",  "threshold" },
    { "ec", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&error_concealment}, "set error concealment options",  "bit_mask" },
    { "sync", HAS_ARG | OPT_FUNC2 | OPT_EXPERT, {(void*)opt_sync}, "set audio-video sync. type (type=audio/video/ext)", "type" },
    { "threads", HAS_ARG | OPT_FUNC2 | OPT_EXPERT, {(void*)opt_thread_count}, "thread count", "count" },
    { "default", OPT_FUNC2 | HAS_ARG | OPT_AUDIO | OPT_VIDEO | OPT_EXPERT, {(void*)opt_default}, "generic catch all option", "" },
    { NULL, },
};

static void show_help(void)
{
    printf("usage: ffplay [options] input_file\n"
           "Simple media player\n");
    printf("\n");
    show_help_options(options, "Main options:\n",
                      OPT_EXPERT, 0);
    show_help_options(options, "\nAdvanced options:\n",
                      OPT_EXPERT, OPT_EXPERT);
    printf("\nWhile playing:\n"
           "q, ESC              quit\n"
           "f                   toggle full screen\n"
           "p, SPC              pause\n"
           "a                   cycle audio channel\n"
           "v                   cycle video channel\n"
           "t                   cycle subtitle channel\n"
           "w                   show audio waves\n"
           "left/right          seek backward/forward 10 seconds\n"
           "down/up             seek backward/forward 1 minute\n"
           "mouse click         seek to percentage in file corresponding to fraction of width\n"
           );
}

static void opt_input_file(const char *filename)
{
    if (!strcmp(filename, "-"))
        filename = "pipe:";
    input_filename = filename;
}

/* Called from the main */
int main(int argc, char **argv)
{
    int flags, i;

    /* register all codecs, demux and protocols */
    avcodec_register_all();
    avdevice_register_all();
    av_register_all();

    for(i=0; i<CODEC_TYPE_NB; i++){
        avctx_opts[i]= avcodec_alloc_context2(i);
    }
    avformat_opts = avformat_alloc_context();
    sws_opts = sws_getContext(16,16,0, 16,16,0, sws_flags, NULL,NULL,NULL);

    show_banner();

    parse_options(argc, argv, options, opt_input_file);

    if (!input_filename) {
        fprintf(stderr, "An input file must be specified\n");
        exit(1);
    }

    if (display_disable) {
        video_disable = 1;
    }
    flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
#if !defined(__MINGW32__) && !defined(__APPLE__)
    flags |= SDL_INIT_EVENTTHREAD; /* Not supported on Windows or Mac OS X */
#endif
    if (SDL_Init (flags)) {
        fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
        exit(1);
    }

    if (!display_disable) {
#if HAVE_SDL_VIDEO_SIZE
        const SDL_VideoInfo *vi = SDL_GetVideoInfo();
        fs_screen_width = vi->current_w;
        fs_screen_height = vi->current_h;
#endif
    }

    SDL_EventState(SDL_ACTIVEEVENT, SDL_IGNORE);
    SDL_EventState(SDL_MOUSEMOTION, SDL_IGNORE);
    SDL_EventState(SDL_SYSWMEVENT, SDL_IGNORE);
    SDL_EventState(SDL_USEREVENT, SDL_IGNORE);

    av_init_packet(&flush_pkt);
    flush_pkt.data= "FLUSH";

    cur_stream = stream_open(input_filename, file_iformat);

    event_loop();

    /* never returns */

    return 0;
}
