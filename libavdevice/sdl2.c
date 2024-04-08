/*
 * Copyright (c) 2016 Josh de Kock
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
 * libSDL2 output device
 */

#include <SDL.h>
#include <SDL_thread.h>

#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavformat/mux.h"

typedef struct {
    AVClass *class;
    SDL_Window *window;
    SDL_Renderer *renderer;
    char *window_title;
    int window_width, window_height;  /**< size of the window */
    int window_x, window_y;           /**< position of the window */
    int window_fullscreen;
    int window_borderless;
    int enable_quit_action;

    SDL_Texture *texture;
    int texture_fmt;
    SDL_Rect texture_rect;

    int inited;
    int warned;
} SDLContext;

static const struct sdl_texture_format_entry {
    enum AVPixelFormat format; int texture_fmt;
} sdl_texture_format_map[] = {
    /*
     * Not implemented in FFmpeg, but leaving here for completeness.
     * { AV_PIX_FMT_NONE, SDL_PIXELFORMAT_ARGB4444 },
     * { AV_PIX_FMT_NONE, SDL_PIXELFORMAT_RGBA4444 },
     * { AV_PIX_FMT_NONE, SDL_PIXELFORMAT_ABGR4444 },
     * { AV_PIX_FMT_NONE, SDL_PIXELFORMAT_BGRA4444 },
     * { AV_PIX_FMT_NONE, SDL_PIXELFORMAT_ARGB1555 },
     * { AV_PIX_FMT_NONE, SDL_PIXELFORMAT_RGBA5551 },
     * { AV_PIX_FMT_NONE, SDL_PIXELFORMAT_ABGR1555 },
     * { AV_PIX_FMT_NONE, SDL_PIXELFORMAT_BGRA5551 },
     * { AV_PIX_FMT_NONE, SDL_PIXELFORMAT_ARGB2101010 },
    */
    { AV_PIX_FMT_RGB8,    SDL_PIXELFORMAT_RGB332 },
    { AV_PIX_FMT_RGB444,  SDL_PIXELFORMAT_RGB444 },
    { AV_PIX_FMT_RGB555,  SDL_PIXELFORMAT_RGB555 },
    { AV_PIX_FMT_BGR555,  SDL_PIXELFORMAT_BGR555 },
    { AV_PIX_FMT_RGB565,  SDL_PIXELFORMAT_RGB565 },
    { AV_PIX_FMT_BGR565,  SDL_PIXELFORMAT_BGR565 },
    { AV_PIX_FMT_RGB24,   SDL_PIXELFORMAT_RGB24 },
    { AV_PIX_FMT_BGR24,   SDL_PIXELFORMAT_BGR24 },
    { AV_PIX_FMT_0RGB32,  SDL_PIXELFORMAT_RGB888 },
    { AV_PIX_FMT_0BGR32,  SDL_PIXELFORMAT_BGR888 },
#if HAVE_BIGENDIAN
    { AV_PIX_FMT_RGB0,    SDL_PIXELFORMAT_RGBX8888 },
    { AV_PIX_FMT_BGR0,    SDL_PIXELFORMAT_BGRX8888 },
#else
    { AV_PIX_FMT_0BGR,    SDL_PIXELFORMAT_RGBX8888 },
    { AV_PIX_FMT_0RGB,    SDL_PIXELFORMAT_BGRX8888 },
#endif
    { AV_PIX_FMT_RGB32,   SDL_PIXELFORMAT_ARGB8888 },
    { AV_PIX_FMT_RGB32_1, SDL_PIXELFORMAT_RGBA8888 },
    { AV_PIX_FMT_BGR32,   SDL_PIXELFORMAT_ABGR8888 },
    { AV_PIX_FMT_BGR32_1, SDL_PIXELFORMAT_BGRA8888 },
    { AV_PIX_FMT_YUV420P, SDL_PIXELFORMAT_IYUV },
    { AV_PIX_FMT_YUYV422, SDL_PIXELFORMAT_YUY2 },
    { AV_PIX_FMT_UYVY422, SDL_PIXELFORMAT_UYVY },
    { AV_PIX_FMT_NONE,    0                },
};

static void compute_texture_rect(AVFormatContext *s)
{
    AVRational sar, dar; /* sample and display aspect ratios */
    SDLContext *sdl = s->priv_data;
    AVStream *st = s->streams[0];
    AVCodecParameters *codecpar = st->codecpar;
    SDL_Rect *texture_rect = &sdl->texture_rect;

    /* compute texture width and height from the codec context information */
    sar = st->sample_aspect_ratio.num ? st->sample_aspect_ratio : (AVRational){ 1, 1 };
    dar = av_mul_q(sar, (AVRational){ codecpar->width, codecpar->height });

    /* we suppose the screen has a 1/1 sample aspect ratio */
    if (sdl->window_width && sdl->window_height) {
        /* fit in the window */
        if (av_cmp_q(dar, (AVRational){ sdl->window_width, sdl->window_height }) > 0) {
            /* fit in width */
            texture_rect->w = sdl->window_width;
            texture_rect->h = av_rescale(texture_rect->w, dar.den, dar.num);
        } else {
            /* fit in height */
            texture_rect->h = sdl->window_height;
            texture_rect->w = av_rescale(texture_rect->h, dar.num, dar.den);
        }
    } else {
        if (sar.num > sar.den) {
            texture_rect->w = codecpar->width;
            texture_rect->h = av_rescale(texture_rect->w, dar.den, dar.num);
        } else {
            texture_rect->h = codecpar->height;
            texture_rect->w = av_rescale(texture_rect->h, dar.num, dar.den);
        }
        sdl->window_width  = texture_rect->w;
        sdl->window_height = texture_rect->h;
    }

    texture_rect->x = (sdl->window_width  - texture_rect->w) / 2;
    texture_rect->y = (sdl->window_height - texture_rect->h) / 2;
}

static int sdl2_write_trailer(AVFormatContext *s)
{
    SDLContext *sdl = s->priv_data;

    if (sdl->texture)
        SDL_DestroyTexture(sdl->texture);
    sdl->texture = NULL;

    if (sdl->renderer)
        SDL_DestroyRenderer(sdl->renderer);
    sdl->renderer = NULL;

    if (sdl->window)
        SDL_DestroyWindow(sdl->window);
    sdl->window = NULL;

    if (!sdl->inited)
        SDL_Quit();

    return 0;
}

static int sdl2_write_header(AVFormatContext *s)
{
    SDLContext *sdl = s->priv_data;
    AVStream *st = s->streams[0];
    AVCodecParameters *codecpar = st->codecpar;
    int i, ret = 0;
    int flags  = 0;

    if (!sdl->warned) {
        av_log(sdl, AV_LOG_WARNING,
            "The sdl output device is deprecated due to being fundamentally incompatible with libavformat API. "
            "For monitoring purposes in ffmpeg you can output to a file or use pipes and a video player.\n"
            "Example: ffmpeg -i INPUT -f nut -c:v rawvideo - | ffplay -loglevel warning -vf setpts=0 -\n"
        );
        sdl->warned = 1;
    }

    if (!sdl->window_title)
        sdl->window_title = av_strdup(s->url);

    if (SDL_WasInit(SDL_INIT_VIDEO)) {
        av_log(s, AV_LOG_WARNING,
               "SDL video subsystem was already inited, you could have multiple SDL outputs. This may cause unknown behaviour.\n");
        sdl->inited = 1;
    }

    if (   s->nb_streams > 1
        || codecpar->codec_type != AVMEDIA_TYPE_VIDEO
        || codecpar->codec_id   != AV_CODEC_ID_RAWVIDEO) {
        av_log(s, AV_LOG_ERROR, "Only supports one rawvideo stream\n");
        goto fail;
    }

    for (i = 0; sdl_texture_format_map[i].format != AV_PIX_FMT_NONE; i++) {
        if (sdl_texture_format_map[i].format == codecpar->format) {
            sdl->texture_fmt = sdl_texture_format_map[i].texture_fmt;
            break;
        }
    }

    if (!sdl->texture_fmt) {
        av_log(s, AV_LOG_ERROR,
               "Unsupported pixel format '%s'.\n",
               av_get_pix_fmt_name(codecpar->format));
        goto fail;
    }

    /* resize texture to width and height from the codec context information */
    flags = SDL_WINDOW_HIDDEN |
            (sdl->window_fullscreen ? SDL_WINDOW_FULLSCREEN : 0) |
            (sdl->window_borderless ? SDL_WINDOW_BORDERLESS : SDL_WINDOW_RESIZABLE);

    /* initialization */
    if (!sdl->inited){
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            av_log(s, AV_LOG_ERROR, "Unable to initialize SDL: %s\n", SDL_GetError());
            goto fail;
        }
    }

    compute_texture_rect(s);

    if (SDL_CreateWindowAndRenderer(sdl->window_width, sdl->window_height,
                                    flags, &sdl->window, &sdl->renderer) != 0){
        av_log(sdl, AV_LOG_ERROR, "Couldn't create window and renderer: %s\n", SDL_GetError());
        goto fail;
    }

    SDL_SetWindowTitle(sdl->window, sdl->window_title);
    SDL_SetWindowPosition(sdl->window, sdl->window_x, sdl->window_y);
    SDL_ShowWindow(sdl->window);

    sdl->texture = SDL_CreateTexture(sdl->renderer, sdl->texture_fmt, SDL_TEXTUREACCESS_STREAMING,
                                     codecpar->width, codecpar->height);

    if (!sdl->texture) {
        av_log(sdl, AV_LOG_ERROR, "Unable to set create mode: %s\n", SDL_GetError());
        goto fail;
    }

    av_log(s, AV_LOG_VERBOSE, "w:%d h:%d fmt:%s -> w:%d h:%d\n",
           codecpar->width, codecpar->height, av_get_pix_fmt_name(codecpar->format),
           sdl->window_width, sdl->window_height);

    sdl->inited = 1;

    return 0;
fail:
    sdl2_write_trailer(s);
    return ret;
}

static int sdl2_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, quit = 0;
    SDLContext *sdl = s->priv_data;
    AVCodecParameters *codecpar = s->streams[0]->codecpar;
    uint8_t *data[4];
    int linesize[4];

    SDL_Event event;
    if (SDL_PollEvent(&event)){
        switch (event.type) {
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
            case SDLK_ESCAPE:
            case SDLK_q:
                quit = 1;
                break;
            default:
                break;
            }
            break;
        case SDL_QUIT:
            quit = 1;
            break;
        case SDL_WINDOWEVENT:
            switch(event.window.event){
            case SDL_WINDOWEVENT_RESIZED:
            case SDL_WINDOWEVENT_SIZE_CHANGED:
                sdl->window_width  = event.window.data1;
                sdl->window_height = event.window.data2;
                compute_texture_rect(s);
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }

    if (quit && sdl->enable_quit_action) {
        sdl2_write_trailer(s);
        return AVERROR(EIO);
    }

    av_image_fill_arrays(data, linesize, pkt->data, codecpar->format, codecpar->width, codecpar->height, 1);
    switch (sdl->texture_fmt) {
    /* case SDL_PIXELFORMAT_ARGB4444:
     * case SDL_PIXELFORMAT_RGBA4444:
     * case SDL_PIXELFORMAT_ABGR4444:
     * case SDL_PIXELFORMAT_BGRA4444:
     * case SDL_PIXELFORMAT_ARGB1555:
     * case SDL_PIXELFORMAT_RGBA5551:
     * case SDL_PIXELFORMAT_ABGR1555:
     * case SDL_PIXELFORMAT_BGRA5551:
     * case SDL_PIXELFORMAT_ARGB2101010:
     */
    case SDL_PIXELFORMAT_IYUV:
    case SDL_PIXELFORMAT_YUY2:
    case SDL_PIXELFORMAT_UYVY:
        ret = SDL_UpdateYUVTexture(sdl->texture, NULL,
                                   data[0], linesize[0],
                                   data[1], linesize[1],
                                   data[2], linesize[2]);
        break;
    case SDL_PIXELFORMAT_RGB332:
    case SDL_PIXELFORMAT_RGB444:
    case SDL_PIXELFORMAT_RGB555:
    case SDL_PIXELFORMAT_BGR555:
    case SDL_PIXELFORMAT_RGB565:
    case SDL_PIXELFORMAT_BGR565:
    case SDL_PIXELFORMAT_RGB24:
    case SDL_PIXELFORMAT_BGR24:
    case SDL_PIXELFORMAT_RGB888:
    case SDL_PIXELFORMAT_RGBX8888:
    case SDL_PIXELFORMAT_BGR888:
    case SDL_PIXELFORMAT_BGRX8888:
    case SDL_PIXELFORMAT_ARGB8888:
    case SDL_PIXELFORMAT_RGBA8888:
    case SDL_PIXELFORMAT_ABGR8888:
    case SDL_PIXELFORMAT_BGRA8888:
        ret = SDL_UpdateTexture(sdl->texture, NULL, data[0], linesize[0]);
        break;
    default:
        av_log(NULL, AV_LOG_FATAL, "Unsupported pixel format\n");
        ret = -1;
        break;
    }
    SDL_RenderClear(sdl->renderer);
    SDL_RenderCopy(sdl->renderer, sdl->texture, NULL, &sdl->texture_rect);
    SDL_RenderPresent(sdl->renderer);
    return ret;
}

#define OFFSET(x) offsetof(SDLContext,x)

static const AVOption options[] = {
    { "window_title",      "set SDL window title",       OFFSET(window_title), AV_OPT_TYPE_STRING,     { .str = NULL }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "window_size",       "set SDL window forced size", OFFSET(window_width), AV_OPT_TYPE_IMAGE_SIZE, { .str = NULL }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "window_x",          "set SDL window x position",  OFFSET(window_x),     AV_OPT_TYPE_INT,        { .i64 = SDL_WINDOWPOS_CENTERED }, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { "window_y",          "set SDL window y position",  OFFSET(window_y),     AV_OPT_TYPE_INT,        { .i64 = SDL_WINDOWPOS_CENTERED }, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { "window_fullscreen", "set SDL window fullscreen",  OFFSET(window_fullscreen), AV_OPT_TYPE_BOOL,  { .i64 = 0 },    0, 1, AV_OPT_FLAG_ENCODING_PARAM },
    { "window_borderless", "set SDL window border off",  OFFSET(window_borderless), AV_OPT_TYPE_BOOL,  { .i64 = 0 },    0, 1, AV_OPT_FLAG_ENCODING_PARAM },
    { "window_enable_quit", "set if quit action is available", OFFSET(enable_quit_action), AV_OPT_TYPE_INT, {.i64=1},   0, 1, AV_OPT_FLAG_ENCODING_PARAM },
    { NULL },
};

static const AVClass sdl2_class = {
    .class_name = "sdl2 outdev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT,
};

const FFOutputFormat ff_sdl2_muxer = {
    .p.name         = "sdl,sdl2",
    .p.long_name    = NULL_IF_CONFIG_SMALL("SDL2 output device"),
    .priv_data_size = sizeof(SDLContext),
    .p.audio_codec  = AV_CODEC_ID_NONE,
    .p.video_codec  = AV_CODEC_ID_RAWVIDEO,
    .write_header   = sdl2_write_header,
    .write_packet   = sdl2_write_packet,
    .write_trailer  = sdl2_write_trailer,
    .p.flags        = AVFMT_NOFILE | AVFMT_VARIABLE_FPS | AVFMT_NOTIMESTAMPS,
    .p.priv_class   = &sdl2_class,
};
