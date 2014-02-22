/*
 * Copyright (c) 2011 Stefano Sabatini
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

/**
 * @file
 * libSDL output device
 */

#include <SDL.h>
#include <SDL_thread.h>

#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"
#include "avdevice.h"

typedef struct {
    AVClass *class;
    SDL_Surface *surface;
    SDL_Overlay *overlay;
    char *window_title;
    char *icon_title;
    int window_width,  window_height;  /**< size of the window */
    int window_fullscreen;

    SDL_Rect overlay_rect;
    int overlay_fmt;

    int sdl_was_already_inited;
    SDL_Thread *event_thread;
    SDL_mutex *mutex;
    SDL_cond *init_cond;
    int init_ret; /* return code used to signal initialization errors */
    int inited;
    int quit;
} SDLContext;

static const struct sdl_overlay_pix_fmt_entry {
    enum AVPixelFormat pix_fmt; int overlay_fmt;
} sdl_overlay_pix_fmt_map[] = {
    { AV_PIX_FMT_YUV420P, SDL_IYUV_OVERLAY },
    { AV_PIX_FMT_YUYV422, SDL_YUY2_OVERLAY },
    { AV_PIX_FMT_UYVY422, SDL_UYVY_OVERLAY },
    { AV_PIX_FMT_NONE,    0                },
};

static int sdl_write_trailer(AVFormatContext *s)
{
    SDLContext *sdl = s->priv_data;

    sdl->quit = 1;

    if (sdl->overlay)
        SDL_FreeYUVOverlay(sdl->overlay);
    sdl->overlay = NULL;
    if (sdl->event_thread)
        SDL_WaitThread(sdl->event_thread, NULL);
    sdl->event_thread = NULL;
    if (sdl->mutex)
        SDL_DestroyMutex(sdl->mutex);
    sdl->mutex = NULL;
    if (sdl->init_cond)
        SDL_DestroyCond(sdl->init_cond);
    sdl->init_cond = NULL;

    if (!sdl->sdl_was_already_inited)
        SDL_Quit();

    return 0;
}

static void compute_overlay_rect(AVFormatContext *s)
{
    AVRational sar, dar; /* sample and display aspect ratios */
    SDLContext *sdl = s->priv_data;
    AVStream *st = s->streams[0];
    AVCodecContext *encctx = st->codec;
    SDL_Rect *overlay_rect = &sdl->overlay_rect;

    /* compute overlay width and height from the codec context information */
    sar = st->sample_aspect_ratio.num ? st->sample_aspect_ratio : (AVRational){ 1, 1 };
    dar = av_mul_q(sar, (AVRational){ encctx->width, encctx->height });

    /* we suppose the screen has a 1/1 sample aspect ratio */
    if (sdl->window_width && sdl->window_height) {
        /* fit in the window */
        if (av_cmp_q(dar, (AVRational){ sdl->window_width, sdl->window_height }) > 0) {
            /* fit in width */
            overlay_rect->w = sdl->window_width;
            overlay_rect->h = av_rescale(overlay_rect->w, dar.den, dar.num);
        } else {
            /* fit in height */
            overlay_rect->h = sdl->window_height;
            overlay_rect->w = av_rescale(overlay_rect->h, dar.num, dar.den);
        }
    } else {
        if (sar.num > sar.den) {
            overlay_rect->w = encctx->width;
            overlay_rect->h = av_rescale(overlay_rect->w, dar.den, dar.num);
        } else {
            overlay_rect->h = encctx->height;
            overlay_rect->w = av_rescale(overlay_rect->h, dar.num, dar.den);
        }
        sdl->window_width  = overlay_rect->w;
        sdl->window_height = overlay_rect->h;
    }

    overlay_rect->x = (sdl->window_width  - overlay_rect->w) / 2;
    overlay_rect->y = (sdl->window_height - overlay_rect->h) / 2;
}

#define SDL_BASE_FLAGS (SDL_SWSURFACE|SDL_RESIZABLE)

static int event_thread(void *arg)
{
    AVFormatContext *s = arg;
    SDLContext *sdl = s->priv_data;
    int flags = SDL_BASE_FLAGS | (sdl->window_fullscreen ? SDL_FULLSCREEN : 0);
    AVStream *st = s->streams[0];
    AVCodecContext *encctx = st->codec;

    /* initialization */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        av_log(s, AV_LOG_ERROR, "Unable to initialize SDL: %s\n", SDL_GetError());
        sdl->init_ret = AVERROR(EINVAL);
        goto init_end;
    }

    SDL_WM_SetCaption(sdl->window_title, sdl->icon_title);
    sdl->surface = SDL_SetVideoMode(sdl->window_width, sdl->window_height,
                                    24, flags);
    if (!sdl->surface) {
        av_log(sdl, AV_LOG_ERROR, "Unable to set video mode: %s\n", SDL_GetError());
        sdl->init_ret = AVERROR(EINVAL);
        goto init_end;
    }

    sdl->overlay = SDL_CreateYUVOverlay(encctx->width, encctx->height,
                                        sdl->overlay_fmt, sdl->surface);
    if (!sdl->overlay || sdl->overlay->pitches[0] < encctx->width) {
        av_log(s, AV_LOG_ERROR,
               "SDL does not support an overlay with size of %dx%d pixels\n",
               encctx->width, encctx->height);
        sdl->init_ret = AVERROR(EINVAL);
        goto init_end;
    }

    sdl->init_ret = 0;
    av_log(s, AV_LOG_VERBOSE, "w:%d h:%d fmt:%s -> w:%d h:%d\n",
           encctx->width, encctx->height, av_get_pix_fmt_name(encctx->pix_fmt),
           sdl->overlay_rect.w, sdl->overlay_rect.h);

init_end:
    SDL_LockMutex(sdl->mutex);
    sdl->inited = 1;
    SDL_UnlockMutex(sdl->mutex);
    SDL_CondSignal(sdl->init_cond);

    if (sdl->init_ret < 0)
        return sdl->init_ret;

    /* event loop */
    while (!sdl->quit) {
        int ret;
        SDL_Event event;
        SDL_PumpEvents();
        ret = SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_ALLEVENTS);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Error when getting SDL event: %s\n", SDL_GetError());
            continue;
        }
        if (ret == 0) {
            SDL_Delay(10);
            continue;
        }

        switch (event.type) {
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
            case SDLK_ESCAPE:
            case SDLK_q:
                sdl->quit = 1;
                break;
            }
            break;
        case SDL_QUIT:
            sdl->quit = 1;
            break;

        case SDL_VIDEORESIZE:
            sdl->window_width  = event.resize.w;
            sdl->window_height = event.resize.h;

            SDL_LockMutex(sdl->mutex);
            sdl->surface = SDL_SetVideoMode(sdl->window_width, sdl->window_height, 24, SDL_BASE_FLAGS);
            if (!sdl->surface) {
                av_log(s, AV_LOG_ERROR, "Failed to set SDL video mode: %s\n", SDL_GetError());
                sdl->quit = 1;
            } else {
                compute_overlay_rect(s);
            }
            SDL_UnlockMutex(sdl->mutex);
            break;

        default:
            break;
        }
    }

    return 0;
}

static int sdl_write_header(AVFormatContext *s)
{
    SDLContext *sdl = s->priv_data;
    AVStream *st = s->streams[0];
    AVCodecContext *encctx = st->codec;
    int i, ret;

    if (!sdl->window_title)
        sdl->window_title = av_strdup(s->filename);
    if (!sdl->icon_title)
        sdl->icon_title = av_strdup(sdl->window_title);

    if (SDL_WasInit(SDL_INIT_VIDEO)) {
        av_log(s, AV_LOG_ERROR,
               "SDL video subsystem was already inited, aborting\n");
        sdl->sdl_was_already_inited = 1;
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if (   s->nb_streams > 1
        || encctx->codec_type != AVMEDIA_TYPE_VIDEO
        || encctx->codec_id   != AV_CODEC_ID_RAWVIDEO) {
        av_log(s, AV_LOG_ERROR, "Only supports one rawvideo stream\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    for (i = 0; sdl_overlay_pix_fmt_map[i].pix_fmt != AV_PIX_FMT_NONE; i++) {
        if (sdl_overlay_pix_fmt_map[i].pix_fmt == encctx->pix_fmt) {
            sdl->overlay_fmt = sdl_overlay_pix_fmt_map[i].overlay_fmt;
            break;
        }
    }

    if (!sdl->overlay_fmt) {
        av_log(s, AV_LOG_ERROR,
               "Unsupported pixel format '%s', choose one of yuv420p, yuyv422, or uyvy422\n",
               av_get_pix_fmt_name(encctx->pix_fmt));
        ret = AVERROR(EINVAL);
        goto fail;
    }

    /* compute overlay width and height from the codec context information */
    compute_overlay_rect(s);

    sdl->init_cond = SDL_CreateCond();
    if (!sdl->init_cond) {
        av_log(s, AV_LOG_ERROR, "Could not create SDL condition variable: %s\n", SDL_GetError());
        ret = AVERROR_EXTERNAL;
        goto fail;
    }
    sdl->mutex = SDL_CreateMutex();
    if (!sdl->mutex) {
        av_log(s, AV_LOG_ERROR, "Could not create SDL mutex: %s\n", SDL_GetError());
        ret = AVERROR_EXTERNAL;
        goto fail;
    }
    sdl->event_thread = SDL_CreateThread(event_thread, s);
    if (!sdl->event_thread) {
        av_log(s, AV_LOG_ERROR, "Could not create SDL event thread: %s\n", SDL_GetError());
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    /* wait until the video system has been inited */
    SDL_LockMutex(sdl->mutex);
    while (!sdl->inited) {
        SDL_CondWait(sdl->init_cond, sdl->mutex);
    }
    SDL_UnlockMutex(sdl->mutex);
    if (sdl->init_ret < 0) {
        ret = sdl->init_ret;
        goto fail;
    }
    return 0;

fail:
    sdl_write_trailer(s);
    return ret;
}

static int sdl_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    SDLContext *sdl = s->priv_data;
    AVCodecContext *encctx = s->streams[0]->codec;
    AVPicture pict;
    int i;

    if (sdl->quit) {
        sdl_write_trailer(s);
        return AVERROR(EIO);
    }
    avpicture_fill(&pict, pkt->data, encctx->pix_fmt, encctx->width, encctx->height);

    SDL_LockMutex(sdl->mutex);
    SDL_FillRect(sdl->surface, &sdl->surface->clip_rect,
                 SDL_MapRGB(sdl->surface->format, 0, 0, 0));
    SDL_LockYUVOverlay(sdl->overlay);
    for (i = 0; i < 3; i++) {
        sdl->overlay->pixels [i] = pict.data    [i];
        sdl->overlay->pitches[i] = pict.linesize[i];
    }
    SDL_DisplayYUVOverlay(sdl->overlay, &sdl->overlay_rect);
    SDL_UnlockYUVOverlay(sdl->overlay);

    SDL_UpdateRect(sdl->surface,
                   sdl->overlay_rect.x, sdl->overlay_rect.y,
                   sdl->overlay_rect.w, sdl->overlay_rect.h);
    SDL_UnlockMutex(sdl->mutex);

    return 0;
}

#define OFFSET(x) offsetof(SDLContext,x)

static const AVOption options[] = {
    { "window_title", "set SDL window title",           OFFSET(window_title), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "icon_title",   "set SDL iconified window title", OFFSET(icon_title)  , AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "window_size",  "set SDL window forced size",     OFFSET(window_width), AV_OPT_TYPE_IMAGE_SIZE, { .str = NULL }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "window_fullscreen", "set SDL window fullscreen", OFFSET(window_fullscreen), AV_OPT_TYPE_INT, { .i64 = 0 }, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { NULL },
};

static const AVClass sdl_class = {
    .class_name = "sdl outdev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT,
};

AVOutputFormat ff_sdl_muxer = {
    .name           = "sdl",
    .long_name      = NULL_IF_CONFIG_SMALL("SDL output device"),
    .priv_data_size = sizeof(SDLContext),
    .audio_codec    = AV_CODEC_ID_NONE,
    .video_codec    = AV_CODEC_ID_RAWVIDEO,
    .write_header   = sdl_write_header,
    .write_packet   = sdl_write_packet,
    .write_trailer  = sdl_write_trailer,
    .flags          = AVFMT_NOFILE | AVFMT_VARIABLE_FPS | AVFMT_NOTIMESTAMPS,
    .priv_class     = &sdl_class,
};
