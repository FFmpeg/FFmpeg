/*
 * FFplay : Simple Media Player based on the ffmpeg libraries
 * Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#define HAVE_AV_CONFIG_H
#include "common.h"
#include "avformat.h"

#include "cmdutils.h"

#include <SDL.h>
#include <SDL_thread.h>

#ifdef CONFIG_WIN32
#undef main /* We don't want SDL to override our main() */
#endif

#if defined(__linux__)
#define HAVE_X11
#endif

#ifdef HAVE_X11
#include <X11/Xlib.h>
#endif

#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)
#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)

/* NOTE: the size must be big enough to compensate the hardware audio buffersize size */
#define SAMPLE_ARRAY_SIZE (2*65536)

typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    int abort_request;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

#define VIDEO_PICTURE_QUEUE_SIZE 1

typedef struct VideoPicture {
    int delay; /* delay before showing the next picture */
    SDL_Overlay *bmp;
    int width, height; /* source height & width */
    int allocated;
} VideoPicture;

enum {
    AV_SYNC_AUDIO_MASTER, /* default choice */
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_CLOCK, /* if external clock, then you must update external_clock yourself */
};

typedef struct VideoState {
    SDL_Thread *parse_tid;
    SDL_Thread *video_tid;
    int no_background;
    int abort_request;
    int paused;
    AVFormatContext *ic;
    int dtg_active_format;

    int audio_stream;
    
    int av_sync_type;
    double external_clock; /* external clock */

    double audio_clock;  /* current audio clock value */
    AVStream *audio_st;
    PacketQueue audioq;
    int audio_hw_buf_size;
    /* samples output by the codec. we reserve more space for avsync
       compensation */
    uint8_t audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2]; 
    int audio_buf_size; /* in bytes */
    int audio_buf_index; /* in bytes */
    AVPacket audio_pkt;
    uint8_t *audio_pkt_data;
    int audio_pkt_size;
    int64_t audio_pkt_ipts;
    
    int show_audio; /* if true, display audio samples */
    int16_t sample_array[SAMPLE_ARRAY_SIZE];
    int sample_array_index;
    int last_i_start;
    
    double video_clock; /* current video clock value */
    int video_stream;
    AVStream *video_st;
    PacketQueue videoq;

    VideoPicture pictq[VIDEO_PICTURE_QUEUE_SIZE];
    int pictq_size, pictq_rindex, pictq_windex;
    SDL_mutex *pictq_mutex;
    SDL_cond *pictq_cond;
    
    //    QETimer *video_timer;
    char filename[1024];
    int width, height, xleft, ytop;
} VideoState;

void show_help(void);
int audio_write_get_buf_size(VideoState *is);

/* options specified by the user */
static AVInputFormat *file_iformat;
static const char *input_filename;
static int fs_screen_width;
static int fs_screen_height;
static int screen_width = 640;
static int screen_height = 480;
static int audio_disable;
static int video_disable;
static int display_disable;
static int show_status;

/* current context */
static int is_full_screen;
static VideoState *cur_stream;
static int64_t audio_callback_time;

#define FF_ALLOC_EVENT   (SDL_USEREVENT)
#define FF_REFRESH_EVENT (SDL_USEREVENT + 1)

SDL_Surface *screen;

/* packet queue handling */
static void packet_queue_init(PacketQueue *q)
{
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}

static void packet_queue_end(PacketQueue *q)
{
    AVPacketList *pkt, *pkt1;

    for(pkt = q->first_pkt; pkt != NULL; pkt = pkt1) {
        pkt1 = pkt->next;
        av_free_packet(&pkt->pkt);
    }
    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

static int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    AVPacketList *pkt1;

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
    q->size += pkt1->pkt.size;
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
            q->size -= pkt1->pkt.size;
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

static void video_image_display(VideoState *is)
{
    VideoPicture *vp;
    float aspect_ratio;
    int width, height, x, y;
    SDL_Rect rect;

    vp = &is->pictq[is->pictq_rindex];
    if (vp->bmp) {
        /* XXX: use variable in the frame */
        aspect_ratio = is->video_st->codec.aspect_ratio;
        if (aspect_ratio <= 0.0)
            aspect_ratio = (float)is->video_st->codec.width / 
                (float)is->video_st->codec.height;
        /* if an active format is indicated, then it overrides the
           mpeg format */
#if 0
        if (is->video_st->codec.dtg_active_format != is->dtg_active_format) {
            is->dtg_active_format = is->video_st->codec.dtg_active_format;
            printf("dtg_active_format=%d\n", is->dtg_active_format);
        }
#endif
#if 0
        switch(is->video_st->codec.dtg_active_format) {
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

        /* XXX: we suppose the screen has a 1.0 pixel ratio */
        height = is->height;
        width = ((int)rint(height * aspect_ratio)) & -3;
        if (width > is->width) {
            width = is->width;
            height = ((int)rint(width / aspect_ratio)) & -3;
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
        rect.y = is->xleft + y;
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
    channels = s->audio_st->codec.channels;
    nb_display_channels = channels;
    if (!s->paused) {
        n = 2 * channels;
        delay = audio_write_get_buf_size(s);
        delay /= n;
        
        /* to be more precise, we take into account the time spent since
           the last buffer computation */
        if (audio_callback_time) {
            time_diff = av_gettime() - audio_callback_time;
            delay += (time_diff * s->audio_st->codec.sample_rate) / 1000000;
        }
        
        delay -= s->width / 2;
        if (delay < s->width)
            delay = s->width;
        i_start = compute_mod(s->sample_array_index - delay * channels, SAMPLE_ARRAY_SIZE);
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

/* display the current picture, if any */
static void video_display(VideoState *is)
{
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
    SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
}

/* called to display each frame */
static void video_refresh_timer(void *opaque)
{
    VideoState *is = opaque;
    VideoPicture *vp;

    if (is->video_st) {
        if (is->pictq_size == 0) {
            /* if no picture, need to wait */
            schedule_refresh(is, 40);
        } else {
            vp = &is->pictq[is->pictq_rindex];
            
            /* launch timer for next picture */
            schedule_refresh(is, vp->delay);

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
        int aqsize, vqsize;
        
        cur_time = av_gettime();
        if (!last_time || (cur_time - last_time) >= 500 * 1000) {
            aqsize = 0;
            vqsize = 0;
            if (is->audio_st)
                aqsize = is->audioq.size;
            if (is->video_st)
                vqsize = is->videoq.size;
            printf("A:%7.2f V:%7.2f aq=%5dKB vq=%5dKB    \r", 
                   is->audio_clock, is->video_clock, aqsize / 1024, vqsize / 1024);
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
    int is_yuv;

    vp = &is->pictq[is->pictq_windex];

    if (vp->bmp)
        SDL_FreeYUVOverlay(vp->bmp);

    /* XXX: use generic function */
    switch(is->video_st->codec.pix_fmt) {
    case PIX_FMT_YUV420P:
    case PIX_FMT_YUV422P:
    case PIX_FMT_YUV444P:
    case PIX_FMT_YUV422:
    case PIX_FMT_YUV410P:
    case PIX_FMT_YUV411P:
        is_yuv = 1;
        break;
    default:
        is_yuv = 0;
        break;
    }

    if (is_yuv) {
        vp->bmp = SDL_CreateYUVOverlay(is->video_st->codec.width,
                                       is->video_st->codec.height,
                                       SDL_YV12_OVERLAY, 
                                       screen);
    } else {
#if 0
        vp->bmp = bmp_alloc(screen, 
                            is->video_st->codec.width,
                            is->video_st->codec.height,
                            screen->bitmap_format, 
                            0);
#endif
        vp->bmp = NULL;
    }
    vp->width = is->video_st->codec.width;
    vp->height = is->video_st->codec.height;

    SDL_LockMutex(is->pictq_mutex);
    vp->allocated = 1;
    SDL_CondSignal(is->pictq_cond);
    SDL_UnlockMutex(is->pictq_mutex);
}

#define VIDEO_CORRECTION_THRESHOLD 0.2

static int output_picture(VideoState *is, AVPicture *src_pict, double pts)
{
    VideoPicture *vp;
    int dst_pix_fmt;
    AVPicture pict;
    double delay, ref_clock, diff;
    
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
        vp->width != is->video_st->codec.width ||
        vp->height != is->video_st->codec.height) {
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
        
        img_convert(&pict, dst_pix_fmt, 
                    src_pict, is->video_st->codec.pix_fmt, 
                    is->video_st->codec.width, is->video_st->codec.height);
        /* update the bitmap content */
        SDL_UnlockYUVOverlay(vp->bmp);

        /* compute delay for the next frame and take into account the
           pts if needed to make a correction. Since we do not support
           correct MPEG B frame PTS, we put a high threshold */
        
        if (is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
            ref_clock = is->video_clock;
        } else if (is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
            /* cannot use audio master if no audio, so fall back to no sync */
            if (!is->audio_st)
                ref_clock = is->video_clock;
            else
                ref_clock = is->audio_clock;
        } else {
            ref_clock = is->external_clock;
        }
        diff = is->video_clock - ref_clock;
        delay = (double)is->video_st->codec.frame_rate_base / 
            (double)is->video_st->codec.frame_rate;
        if (fabs(diff) > VIDEO_CORRECTION_THRESHOLD) {
            /* if too big difference, then we adjust */
            delay += diff;
            /* compute the difference */
            if (delay < 0.01)
                delay = 0.01;
            else if (delay > 1.0)
                delay = 1.0;
        }
        vp->delay = (int)(delay * 1000 + 0.5);

        /* now we can update the picture count */
        if (++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE)
            is->pictq_windex = 0;
        SDL_LockMutex(is->pictq_mutex);
        is->pictq_size++;
        SDL_UnlockMutex(is->pictq_mutex);
    }

    /* update video clock */
    if (pts != 0) {
        is->video_clock = pts;
    } else {
        is->video_clock += (double)is->video_st->codec.frame_rate_base / 
            (double)is->video_st->codec.frame_rate;
    }
    return 0;
}

static int video_thread(void *arg)
{
    VideoState *is = arg;
    AVPacket pkt1, *pkt = &pkt1;
    unsigned char *ptr;
    int len, len1, got_picture, i;
    AVFrame frame;
    AVPicture pict;
    int64_t ipts;
    double pts;

    for(;;) {
        while (is->paused && !is->videoq.abort_request) {
            SDL_Delay(10);
        }
        if (packet_queue_get(&is->videoq, pkt, 1) < 0)
            break;
        ipts = pkt->pts;
        ptr = pkt->data;
        if (is->video_st->codec.codec_id == CODEC_ID_RAWVIDEO) {
            avpicture_fill(&pict, ptr, 
                           is->video_st->codec.pix_fmt,
                           is->video_st->codec.width,
                           is->video_st->codec.height);
            pts = 0;
            if (ipts != AV_NOPTS_VALUE)
                pts = (double)ipts * is->ic->pts_num / is->ic->pts_den;
            if (output_picture(is, &pict, pts) < 0)
                goto the_end;
        } else {
            len = pkt->size;
            while (len > 0) {
                len1 = avcodec_decode_video(&is->video_st->codec, 
                                            &frame, &got_picture, ptr, len);
                if (len1 < 0)
                    break;
                if (got_picture) {
                    for(i=0;i<4;i++) {
                        pict.data[i] = frame.data[i];
                        pict.linesize[i] = frame.linesize[i];
                    }
                    pts = 0;
                    if (ipts != AV_NOPTS_VALUE)
                        pts = (double)ipts * is->ic->pts_num / is->ic->pts_den;
                    ipts = AV_NOPTS_VALUE;
                    if (output_picture(is, &pict, pts) < 0)
                        goto the_end;
                }
                ptr += len1;
                len -= len1;
            }
        }
        av_free_packet(pkt);
    }
 the_end:
    return 0;
}

/* copy samples for viewing in editor window */
static void update_sample_display(VideoState *is, short *samples, int samples_size)
{
    int size, len, channels;

    channels = is->audio_st->codec.channels;

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

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 2

/* return the new audio buffer size (samples can be added or deleted
   to get better sync if video or external master clock) */
static int synchronize_audio(VideoState *is, short *samples, 
                             int samples_size, double pts)
{
    int n, delay;
    double ref_clock;
    
    n = 2 * is->audio_st->codec.channels;

    if (is->av_sync_type == AV_SYNC_EXTERNAL_CLOCK)
        ref_clock = is->external_clock;
    else if (is->av_sync_type == AV_SYNC_VIDEO_MASTER && is->video_st)
        ref_clock = is->video_clock;
    else
        ref_clock = is->audio_clock;
    
    /* if not master, then we try to remove or add samples to correct the clock */

    if (((is->av_sync_type == AV_SYNC_VIDEO_MASTER && is->video_st) ||
         is->av_sync_type == AV_SYNC_EXTERNAL_CLOCK) && pts != 0) {
        double diff;
        int wanted_size, min_size, max_size, nb_samples;
        delay = audio_write_get_buf_size(is);
        diff = pts - (double)delay / (double)(n * is->audio_st->codec.sample_rate) - ref_clock;
        wanted_size = (int)(diff * is->audio_st->codec.sample_rate) * n;
        nb_samples = samples_size / n;

        min_size = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX)) / 100) * n;
        max_size = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX)) / 100) * n;
        if (wanted_size < min_size)
            wanted_size = min_size;
        else if (wanted_size > max_size)
            wanted_size = max_size;
        
        /* do the correct */
        /* XXX: do it better with sample interpolation */
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

    /* update audio clock */
    if (is->av_sync_type == AV_SYNC_AUDIO_MASTER && pts != 0) {
        /* a pts is given: we update the audio clock precisely */
        delay = audio_write_get_buf_size(is);
        is->audio_clock = pts - (double)delay / (double)(n * is->audio_st->codec.sample_rate);
    } else {
        is->audio_clock += (double)samples_size / (double)(n * is->audio_st->codec.sample_rate);
    }
    return samples_size;
}

/* decode one audio frame and returns its uncompressed size */
static int audio_decode_frame(VideoState *is, uint8_t *audio_buf, double *pts_ptr)
{
    AVPacket *pkt = &is->audio_pkt;
    int len1, data_size;
    double pts;

    for(;;) {
        if (is->paused || is->audioq.abort_request) {
            return -1;
        }
        while (is->audio_pkt_size > 0) {
            len1 = avcodec_decode_audio(&is->audio_st->codec, 
                                        (int16_t *)audio_buf, &data_size, 
                                        is->audio_pkt_data, is->audio_pkt_size);
            if (len1 < 0)
                break;
            is->audio_pkt_data += len1;
            is->audio_pkt_size -= len1;
            if (data_size > 0) {
                pts = 0;
                if (is->audio_pkt_ipts != AV_NOPTS_VALUE)
                    pts = (double)is->audio_pkt_ipts * is->ic->pts_num / is->ic->pts_den;
                *pts_ptr = pts;
                is->audio_pkt_ipts = AV_NOPTS_VALUE;
                /* we got samples : we can exit now */
                return data_size;
            }
        }

        /* free previous packet if any */
        if (pkt->destruct)
            av_free_packet(pkt);

        /* read next packet */
        if (packet_queue_get(&is->audioq, pkt, 1) < 0)
            return -1;
        is->audio_pkt_data = pkt->data;
        is->audio_pkt_size = pkt->size;
        is->audio_pkt_ipts = pkt->pts;
    }
}

int audio_write_get_buf_size(VideoState *is)
{
    int delay;
    delay = is->audio_hw_buf_size; 
#if 0
    /* just a test to check if the estimated delay is OK */
    {
        int val;
        if (ioctl(sdl_audio_fd, SNDCTL_DSP_GETODELAY, &val) < 0) 
            perror("SNDCTL_DSP_GETODELAY");
        printf("real_delay=%d delay=%d\n", val, delay);
    }
#endif
    return delay;
}


/* prepare a new audio buffer */
void sdl_audio_callback(void *opaque, Uint8 *stream, int len)
{
    VideoState *is = opaque;
    int audio_size, len1;
    double pts;

    audio_callback_time = av_gettime();
    
    while (len > 0) {
        if (is->audio_buf_index >= is->audio_buf_size) {
           audio_size = audio_decode_frame(is, is->audio_buf, &pts);
           if (audio_size < 0) {
                /* if error, just output silence */
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
    enc = &ic->streams[stream_index]->codec;
    

    /* prepare audio output */
    if (enc->codec_type == CODEC_TYPE_AUDIO) {
        wanted_spec.freq = enc->sample_rate;
        wanted_spec.format = AUDIO_S16SYS;
        wanted_spec.channels = enc->channels;
        wanted_spec.silence = 0;
        wanted_spec.samples = 8192;
        wanted_spec.callback = sdl_audio_callback;
        wanted_spec.userdata = is;
        if (SDL_OpenAudio(&wanted_spec, &spec) < 0)
            return -1;
        is->audio_hw_buf_size = spec.size;
    }

    codec = avcodec_find_decoder(enc->codec_id);
    if (!codec ||
        avcodec_open(enc, codec) < 0)
        return -1;
        switch(enc->codec_type) {
    case CODEC_TYPE_AUDIO:
        is->audio_stream = stream_index;
        is->audio_st = ic->streams[stream_index];
        is->audio_buf_size = 0;
        is->audio_buf_index = 0;
        is->audio_pkt_size = 0;
        memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
        packet_queue_init(&is->audioq);
	SDL_PauseAudio(0);
        break;
    case CODEC_TYPE_VIDEO:
        is->video_stream = stream_index;
        is->video_st = ic->streams[stream_index];

        packet_queue_init(&is->videoq);
        is->video_tid = SDL_CreateThread(video_thread, is);
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
    
    enc = &ic->streams[stream_index]->codec;

    switch(enc->codec_type) {
    case CODEC_TYPE_AUDIO:
        packet_queue_abort(&is->audioq);

        SDL_CloseAudio();

        packet_queue_end(&is->audioq);
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
    default:
        break;
    }

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
    default:
        break;
    }
}


/* this thread gets the stream from the disk or the network */
static int decode_thread(void *arg)
{
    VideoState *is = arg;
    AVFormatContext *ic;
    int err, i, ret, video_index, audio_index;
    AVPacket pkt1, *pkt = &pkt1;

    video_index = -1;
    audio_index = -1;
    is->video_stream = -1;
    is->audio_stream = -1;

    err = av_open_input_file(&ic, is->filename, NULL, 0, NULL);
    if (err < 0)
        return 0;
    is->ic = ic;
    err = av_find_stream_info(ic);
    if (err < 0)
        goto fail;

    for(i = 0; i < ic->nb_streams; i++) {
        AVCodecContext *enc = &ic->streams[i]->codec;
        switch(enc->codec_type) {
        case CODEC_TYPE_AUDIO:
            if (audio_index < 0 && !audio_disable)
                audio_index = i;
            break;
        case CODEC_TYPE_VIDEO:
            if (video_index < 0 && !video_disable)
                video_index = i;
            break;
        default:
            break;
        }
    }
    if (show_status) {
        dump_format(ic, 0, is->filename, 0);
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

    if (is->video_stream < 0 && is->audio_stream < 0) {
        goto fail;
    }

    for(;;) {
        if (is->abort_request)
            break;
        /* if the queue are full, no need to read more */
        if (is->audioq.size > MAX_AUDIOQ_SIZE ||
            is->videoq.size > MAX_VIDEOQ_SIZE) {
            /* wait 10 ms */
            SDL_Delay(10);
            continue;
        }
        ret = av_read_packet(ic, pkt);
        if (ret < 0) {
            break;
        }
        if (pkt->stream_index == is->audio_stream) {
            packet_queue_put(&is->audioq, pkt);
        } else if (pkt->stream_index == is->video_stream) {
            packet_queue_put(&is->videoq, pkt);
        } else {
            av_free_packet(pkt);
        }
    }
    /* wait until the end */
    while (!is->abort_request) {
        SDL_Delay(100);
    }

 fail:
    /* close each stream */
    if (is->audio_stream >= 0)
        stream_component_close(is, is->audio_stream);
    if (is->video_stream >= 0)
        stream_component_close(is, is->video_stream);

    av_close_input_file(is->ic);
    is->ic = NULL; /* safety */
    return 0;
}

/* pause or resume the video */
static void stream_pause(VideoState *is)
{
    is->paused = !is->paused;
}

static VideoState *stream_open(const char *filename)
{
    VideoState *is;

    is = av_mallocz(sizeof(VideoState));
    if (!is)
        return NULL;
    pstrcpy(is->filename, sizeof(is->filename), filename);
    if (screen) {
        is->width = screen->w;
        is->height = screen->h;
    }
    is->ytop = 0;
    is->xleft = 0;

    /* start video display */
    is->pictq_mutex = SDL_CreateMutex();
    is->pictq_cond = SDL_CreateCond();

    /* add the refresh timer to draw the picture */
    schedule_refresh(is, 40);

    is->av_sync_type = AV_SYNC_AUDIO_MASTER;

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
}

void toggle_full_screen(void)
{
    int w, h, flags;
    is_full_screen = !is_full_screen;
    if (!fs_screen_width) {
        /* use default SDL method */
        SDL_WM_ToggleFullScreen(screen);
    } else {
        /* use the recorded resolution */
        flags = SDL_HWSURFACE|SDL_ASYNCBLIT|SDL_HWACCEL;
        if (is_full_screen) {
            w = fs_screen_width;
            h = fs_screen_height;
            flags |= SDL_FULLSCREEN;
        } else {
            w = screen_width;
            h = screen_height;
            flags |= SDL_RESIZABLE;
        }
        screen = SDL_SetVideoMode(w, h, 0, flags);
        cur_stream->width = w;
        cur_stream->height = h;
    }
}

void toggle_pause(void)
{
    if (cur_stream)
        stream_pause(cur_stream);
}

void do_exit(void)
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

void toggle_audio_display(void)
{
    if (cur_stream) {
        cur_stream->show_audio = !cur_stream->show_audio;
    }
}

/* handle an event sent by the GUI */
void event_loop(void)
{
    SDL_Event event;

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
            case SDLK_a:
                toggle_audio_display();
                break;
            default:
                break;
            }
            break;
        case SDL_VIDEORESIZE:
            if (cur_stream) {
                screen = SDL_SetVideoMode(event.resize.w, event.resize.h, 0, 
                                          SDL_HWSURFACE|SDL_RESIZABLE|SDL_ASYNCBLIT|SDL_HWACCEL);
                cur_stream->width = event.resize.w;
                cur_stream->height = event.resize.h;
            }
            break;
        case SDL_QUIT:
            do_exit();
            break;
        case FF_ALLOC_EVENT:
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

void opt_width(const char *arg)
{
    screen_width = atoi(arg);
}

void opt_height(const char *arg)
{
    screen_height = atoi(arg);
}

static void opt_format(const char *arg)
{
    file_iformat = av_find_input_format(arg);
    if (!file_iformat) {
        fprintf(stderr, "Unknown input format: %s\n", arg);
        exit(1);
    }
}

const OptionDef options[] = {
    { "h", 0, {(void*)show_help}, "show help" },
    { "x", HAS_ARG, {(void*)opt_width}, "force displayed width", "width" },
    { "y", HAS_ARG, {(void*)opt_height}, "force displayed height", "height" },
    { "an", OPT_BOOL, {(void*)&audio_disable}, "disable audio" },
    { "vn", OPT_BOOL, {(void*)&video_disable}, "disable video" },
    { "nodisp", OPT_BOOL, {(void*)&display_disable}, "disable graphical display" },
    { "f", HAS_ARG, {(void*)opt_format}, "force format", "fmt" },
    { "stats", OPT_BOOL | OPT_EXPERT, {(void*)&show_status}, "show status", "" },
    { NULL, },
};

void show_help(void)
{
    printf("usage: ffplay [options] input_file\n"
           "Simple media player\n");
    printf("\n");
    show_help_options(options);
    printf("\nWhile playing:\n"
           "q, ESC              quit\n"
           "f                   toggle full screen\n"
           "p, SPC              pause\n"
           "a                   show audio waves\n"
           );
    exit(1);
}

void parse_arg_file(const char *filename)
{
    input_filename = filename;
}

/* Called from the main */
int main(int argc, char **argv)
{
    int flags;
    
    /* register all codecs, demux and protocols */
    av_register_all();

    parse_options(argc, argv, options);

    if (!input_filename)
        show_help();

    if (display_disable) {
        video_disable = 1;
    }
    flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
#ifndef CONFIG_WIN32
    flags |= SDL_INIT_EVENTTHREAD; /* Not supported on win32 */
#endif
    if (SDL_Init (flags)) {
        fprintf(stderr, "Could not initialize SDL - exiting\n");
        exit(1);
    }

    if (!display_disable) {
        screen = SDL_SetVideoMode(screen_width, screen_height, 0, 
                                  SDL_HWSURFACE|SDL_RESIZABLE|SDL_ASYNCBLIT|SDL_HWACCEL);
        if (!screen) {
            fprintf(stderr, "SDL: could not set video mode - exiting\n");
            exit(1);
        }
        SDL_WM_SetCaption("FFplay", "FFplay");
#ifdef HAVE_X11
        /* save the screen resolution... SDL should allow full screen
           by resizing the window */
        {
            Display *dpy;
            dpy = XOpenDisplay(NULL);
            if (dpy) {
                fs_screen_width = DisplayWidth(dpy, DefaultScreen(dpy));
                fs_screen_height = DisplayHeight(dpy, DefaultScreen(dpy));
		XCloseDisplay(dpy);
            }
        }
#endif
    }

    SDL_EventState(SDL_ACTIVEEVENT, SDL_IGNORE);
    SDL_EventState(SDL_MOUSEMOTION, SDL_IGNORE);
    SDL_EventState(SDL_SYSWMEVENT, SDL_IGNORE);
    SDL_EventState(SDL_USEREVENT, SDL_IGNORE);

    cur_stream = stream_open(input_filename);

    event_loop();

    /* never returns */

    return 0;
}
