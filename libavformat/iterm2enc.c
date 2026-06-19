/*
 * This file is part of FFmpeg.
 *
 * Copyright (c) 2026 Zhao Zhili <quinkblack@foxmail.com>
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

#include <string.h>

#include "libavutil/base64.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "mux.h"

/* iTerm2 inline image protocol: https://iterm2.com/documentation-images.html */

#define ESC "\033"

#define SYNC_BEGIN    ESC "[?2026h"
#define SYNC_END      ESC "[?2026l"
#define CURSOR_SAVE   ESC "7"
#define CURSOR_RESTORE ESC "8"
#define CURSOR_HOME   ESC "[H"
#define CURSOR_BOTTOM ESC "[999H"

#define OSC_START     ESC "]1337;"
#define BEL           "\a"
#define ST            ESC "\\"

/* tmux requires DCS passthrough with ST termination and ESC doubling */
#define TMUX_DCS ESC "Ptmux;"

/* iTerm2 and tmux silently drop a single OSC sequence >= 1 MiB, so split the
 * image into chunks below that limit. Old tmux capped a sequence at 256 bytes,
 * but the tiny chunks that would require flood the tmux parser and freeze the
 * terminal, so we do not support such versions. */
#define FILEPART_CHUNK   ((1 << 20) - 4096)

#define WRITE_LITERAL(pb, str) avio_write(pb, (const unsigned char *)(str), \
                                           sizeof(str) - 1)

typedef struct ITerm2Context {
    const AVClass *class;
    char *display_width;
    char *display_height;
    int  keep_aspect;
    int  tmux;
    char *b64;
    unsigned b64_size;
} ITerm2Context;

static void osc_open(ITerm2Context *c, AVIOContext *pb)
{
    if (c->tmux)
        WRITE_LITERAL(pb, TMUX_DCS ESC);
    WRITE_LITERAL(pb, OSC_START);
}

static void osc_close(ITerm2Context *c, AVIOContext *pb)
{
    WRITE_LITERAL(pb, BEL);
    if (c->tmux)
        WRITE_LITERAL(pb, ST);
}

static void write_image(ITerm2Context *c, AVIOContext *pb, int size)
{
    size_t b64_len = strlen(c->b64);

    osc_open(c, pb);
    WRITE_LITERAL(pb, "MultipartFile=");
    avio_printf(pb, "inline=1;size=%d", size);
    if (c->display_width && c->display_width[0])
        avio_printf(pb, ";width=%s", c->display_width);
    if (c->display_height && c->display_height[0])
        avio_printf(pb, ";height=%s", c->display_height);
    if (!c->keep_aspect)
        WRITE_LITERAL(pb, ";preserveAspectRatio=0");
    osc_close(c, pb);

    for (size_t off = 0; off < b64_len; off += FILEPART_CHUNK) {
        size_t n = FFMIN(FILEPART_CHUNK, b64_len - off);

        osc_open(c, pb);
        WRITE_LITERAL(pb, "FilePart=");
        avio_write(pb, c->b64 + off, n);
        osc_close(c, pb);
    }

    osc_open(c, pb);
    WRITE_LITERAL(pb, "FileEnd");
    osc_close(c, pb);
}

static int iterm2_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ITerm2Context *c = s->priv_data;

    av_fast_malloc(&c->b64, &c->b64_size, AV_BASE64_SIZE(pkt->size));
    if (!c->b64)
        return AVERROR(ENOMEM);
    if (!av_base64_encode(c->b64, c->b64_size, pkt->data, pkt->size))
        return AVERROR(EINVAL);

    /* Synchronized output swaps the frame in atomically. */
    WRITE_LITERAL(s->pb, SYNC_BEGIN CURSOR_SAVE CURSOR_HOME);

    write_image(c, s->pb, pkt->size);

    WRITE_LITERAL(s->pb, CURSOR_RESTORE SYNC_END);

    avio_flush(s->pb);

    return 0;
}

static int iterm2_write_trailer(AVFormatContext *s)
{
    WRITE_LITERAL(s->pb, CURSOR_BOTTOM "\n");

    return 0;
}

static av_cold void iterm2_deinit(AVFormatContext *s)
{
    ITerm2Context *c = s->priv_data;
    av_freep(&c->b64);
}

#define OFFSET(x) offsetof(ITerm2Context, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM

static const AVOption options[] = {
    { "display_width", "on-screen width (auto, N cells, Npx, N%%)",
        OFFSET(display_width),  AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, ENC },
    { "display_height", "on-screen height (auto, N cells, Npx, N%%)",
        OFFSET(display_height), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, ENC },
    { "keep_aspect", "preserve aspect ratio when scaling",
        OFFSET(keep_aspect), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, ENC },
    { "tmux", "wrap image in tmux DCS passthrough, requires tmux set -g allow-passthrough on",
        OFFSET(tmux), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, ENC },
    { NULL },
};

static const AVClass iterm2_class = {
    .class_name = "iTerm2 muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_MUXER,
};

const FFOutputFormat ff_iterm2_muxer = {
    .p.name         = "iterm2",
    .p.long_name    = NULL_IF_CONFIG_SMALL("iTerm2 inline image protocol"),
    .priv_data_size = sizeof(ITerm2Context),
    .p.audio_codec  = AV_CODEC_ID_NONE,
    .p.video_codec  = AV_CODEC_ID_MJPEG,
    .write_packet   = iterm2_write_packet,
    .write_trailer  = iterm2_write_trailer,
    .deinit         = iterm2_deinit,
    .flags_internal = FF_OFMT_FLAG_MAX_ONE_OF_EACH,
    .p.flags        = AVFMT_NOTIMESTAMPS | AVFMT_NODIMENSIONS,
    .p.priv_class   = &iterm2_class,
};
