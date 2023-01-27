/*
* Copyright (c) 2013 Nicolas George
*
* This file is part of FFmpeg.
*
* FFmpeg is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public License
* as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* FFmpeg is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include "libavutil/adler32.h"
#include "libavutil/avassert.h"
#include "libavutil/bprint.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "libavformat/mux.h"
#include "avformat.h"
#include "internal.h"

/* Identical to Adler32 when the type is uint8_t. */
#define DEFINE_CKSUM_LINE(name, type, conv) \
static void cksum_line_ ## name(unsigned *cksum, void *data, unsigned size) \
{ \
    type *p = data; \
    unsigned a = *cksum & 0xFFFF, b = *cksum >> 16; \
    for (; size > 0; size--, p++) { \
        a = (a + (unsigned)(conv)) % 65521; \
        b = (b + a) % 65521; \
    } \
    *cksum = a | (b << 16); \
}

DEFINE_CKSUM_LINE(u8,  uint8_t, *p)
DEFINE_CKSUM_LINE(s16, int16_t, *p + 0x8000)
DEFINE_CKSUM_LINE(s32, int32_t, *p + 0x80000000)
DEFINE_CKSUM_LINE(flt, float,   *p * 0x80000000 + 0x80000000)
DEFINE_CKSUM_LINE(dbl, double,  *p * 0x80000000 + 0x80000000)

static void video_frame_cksum(AVBPrint *bp, AVFrame *frame)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(frame->format);
    int i, y;
    uint8_t *data;
    int linesize[5] = { 0 };

    av_bprintf(bp, ", %d x %d", frame->width, frame->height);
    if (!desc) {
        av_bprintf(bp, ", unknown");
        return;
    }
    if (av_image_fill_linesizes(linesize, frame->format, frame->width) < 0)
        return;
    av_bprintf(bp, ", %s", desc->name);
    for (i = 0; linesize[i]; i++) {
        unsigned cksum = 0;
        int h = frame->height;
        if ((i == 1 || i == 2) && desc->nb_components >= 3)
            h = AV_CEIL_RSHIFT(h, desc->log2_chroma_h);
        data = frame->data[i];
        for (y = 0; y < h; y++) {
            cksum = av_adler32_update(cksum, data, linesize[i]);
            data += frame->linesize[i];
        }
        av_bprintf(bp, ", 0x%08x", cksum);
    }
}

static void audio_frame_cksum(AVBPrint *bp, AVFrame *frame)
{
    int nb_planes, nb_samples, p;
    const char *name;

    nb_planes  = frame->ch_layout.nb_channels;
    nb_samples = frame->nb_samples;
    if (!av_sample_fmt_is_planar(frame->format)) {
        nb_samples *= nb_planes;
        nb_planes = 1;
    }
    name = av_get_sample_fmt_name(frame->format);
    av_bprintf(bp, ", %d samples", frame->nb_samples);
    av_bprintf(bp, ", %s", name ? name : "unknown");
    for (p = 0; p < nb_planes; p++) {
        uint32_t cksum = 0;
        void *d = frame->extended_data[p];
        switch (frame->format) {
        case AV_SAMPLE_FMT_U8:
        case AV_SAMPLE_FMT_U8P:
            cksum_line_u8(&cksum, d, nb_samples);
            break;
        case AV_SAMPLE_FMT_S16:
        case AV_SAMPLE_FMT_S16P:
            cksum_line_s16(&cksum, d, nb_samples);
            break;
        case AV_SAMPLE_FMT_S32:
        case AV_SAMPLE_FMT_S32P:
            cksum_line_s32(&cksum, d, nb_samples);
            break;
        case AV_SAMPLE_FMT_FLT:
        case AV_SAMPLE_FMT_FLTP:
            cksum_line_flt(&cksum, d, nb_samples);
            break;
        case AV_SAMPLE_FMT_DBL:
        case AV_SAMPLE_FMT_DBLP:
            cksum_line_dbl(&cksum, d, nb_samples);
            break;
        default:
            av_assert0(!"reached");
        }
        av_bprintf(bp, ", 0x%08"PRIx32, cksum);
    }
}

static int write_header(struct AVFormatContext *s)
{
    return ff_framehash_write_header(s);
}

static int write_frame(struct AVFormatContext *s, int stream_index,
                       AVFrame **frame, unsigned flags)
{
    AVBPrint bp;
    int ret = 0;
    enum AVMediaType type;
    const char *type_name;

    if ((flags & AV_WRITE_UNCODED_FRAME_QUERY))
        return 0;

    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprintf(&bp, "%d, %10"PRId64"",
               stream_index, (*frame)->pts);
    type = s->streams[stream_index]->codecpar->codec_type;
    type_name = av_get_media_type_string(type);
    av_bprintf(&bp, ", %s", type_name ? type_name : "unknown");
    switch (type) {
        case AVMEDIA_TYPE_VIDEO:
            video_frame_cksum(&bp, *frame);
            break;
        case AVMEDIA_TYPE_AUDIO:
            audio_frame_cksum(&bp, *frame);
            break;
    }

    av_bprint_chars(&bp, '\n', 1);
    if (av_bprint_is_complete(&bp))
        avio_write(s->pb, bp.str, bp.len);
    else
        ret = AVERROR(ENOMEM);
    av_bprint_finalize(&bp, NULL);
    return ret;
}

static int write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    return AVERROR(ENOSYS);
}

const FFOutputFormat ff_uncodedframecrc_muxer = {
    .p.name              = "uncodedframecrc",
    .p.long_name         = NULL_IF_CONFIG_SMALL("uncoded framecrc testing"),
    .p.audio_codec       = AV_CODEC_ID_PCM_S16LE,
    .p.video_codec       = AV_CODEC_ID_RAWVIDEO,
    .p.flags             = AVFMT_VARIABLE_FPS | AVFMT_TS_NONSTRICT |
                           AVFMT_TS_NEGATIVE,
    .write_header      = write_header,
    .write_packet      = write_packet,
    .write_uncoded_frame = write_frame,
};
