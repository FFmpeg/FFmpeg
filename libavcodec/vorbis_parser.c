/*
 * Copyright (c) 2012 Justin Ruggles
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
 * Vorbis audio parser
 *
 * Determines the duration for each packet.
 */

#include "libavutil/log.h"

#include "get_bits.h"
#include "parser.h"
#include "xiph.h"
#include "vorbis_parser_internal.h"

static const AVClass vorbis_parser_class = {
    .class_name = "Vorbis parser",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int parse_id_header(AVVorbisParseContext *s,
                           const uint8_t *buf, int buf_size)
{
    /* Id header should be 30 bytes */
    if (buf_size < 30) {
        av_log(s, AV_LOG_ERROR, "Id header is too short\n");
        return AVERROR_INVALIDDATA;
    }

    /* make sure this is the Id header */
    if (buf[0] != 1) {
        av_log(s, AV_LOG_ERROR, "Wrong packet type in Id header\n");
        return AVERROR_INVALIDDATA;
    }

    /* check for header signature */
    if (memcmp(&buf[1], "vorbis", 6)) {
        av_log(s, AV_LOG_ERROR, "Invalid packet signature in Id header\n");
        return AVERROR_INVALIDDATA;
    }

    if (!(buf[29] & 0x1)) {
        av_log(s, AV_LOG_ERROR, "Invalid framing bit in Id header\n");
        return AVERROR_INVALIDDATA;
    }

    s->blocksize[0] = 1 << (buf[28] & 0xF);
    s->blocksize[1] = 1 << (buf[28] >>  4);

    return 0;
}

static int parse_setup_header(AVVorbisParseContext *s,
                              const uint8_t *buf, int buf_size)
{
    GetBitContext gb, gb0;
    uint8_t *rev_buf;
    int i, ret = 0;
    int got_framing_bit, mode_count, got_mode_header, last_mode_count = 0;

    /* avoid overread */
    if (buf_size < 7) {
        av_log(s, AV_LOG_ERROR, "Setup header is too short\n");
        return AVERROR_INVALIDDATA;
    }

    /* make sure this is the Setup header */
    if (buf[0] != 5) {
        av_log(s, AV_LOG_ERROR, "Wrong packet type in Setup header\n");
        return AVERROR_INVALIDDATA;
    }

    /* check for header signature */
    if (memcmp(&buf[1], "vorbis", 6)) {
        av_log(s, AV_LOG_ERROR, "Invalid packet signature in Setup header\n");
        return AVERROR_INVALIDDATA;
    }

    /* reverse bytes so we can easily read backwards with get_bits() */
    if (!(rev_buf = av_malloc(buf_size))) {
        av_log(s, AV_LOG_ERROR, "Out of memory\n");
        return AVERROR(ENOMEM);
    }
    for (i = 0; i < buf_size; i++)
        rev_buf[i] = buf[buf_size - 1 - i];
    init_get_bits(&gb, rev_buf, buf_size * 8);

    got_framing_bit = 0;
    while (get_bits_left(&gb) > 97) {
        if (get_bits1(&gb)) {
            got_framing_bit = get_bits_count(&gb);
            break;
        }
    }
    if (!got_framing_bit) {
        av_log(s, AV_LOG_ERROR, "Invalid Setup header\n");
        ret = AVERROR_INVALIDDATA;
        goto bad_header;
    }

    /* Now we search backwards to find possible valid mode counts. This is not
     * fool-proof because we could have false positive matches and read too
     * far, but there isn't really any way to be sure without parsing through
     * all the many variable-sized fields before the modes. This approach seems
     * to work well in testing, and it is similar to how it is handled in
     * liboggz. */
    mode_count = 0;
    got_mode_header = 0;
    while (get_bits_left(&gb) >= 97) {
        if (get_bits(&gb, 8) > 63 || get_bits(&gb, 16) || get_bits(&gb, 16))
            break;
        skip_bits(&gb, 1);
        mode_count++;
        if (mode_count > 64)
            break;
        gb0 = gb;
        if (get_bits(&gb0, 6) + 1 == mode_count) {
            got_mode_header = 1;
            last_mode_count = mode_count;
        }
    }
    if (!got_mode_header) {
        av_log(s, AV_LOG_ERROR, "Invalid Setup header\n");
        ret = AVERROR_INVALIDDATA;
        goto bad_header;
    }
    /* All samples I've seen use <= 2 modes, so ask for a sample if we find
     * more than that, as it is most likely a false positive. If we get any
     * we may need to approach this the long way and parse the whole Setup
     * header, but I hope very much that it never comes to that. */
    if (last_mode_count > 2) {
        avpriv_request_sample(s,
                              "%d modes (either a false positive or a "
                              "sample from an unknown encoder)",
                              last_mode_count);
    }
    /* We're limiting the mode count to 63 so that we know that the previous
     * block flag will be in the first packet byte. */
    if (last_mode_count > 63) {
        av_log(s, AV_LOG_ERROR, "Unsupported mode count: %d\n",
               last_mode_count);
        ret = AVERROR_INVALIDDATA;
        goto bad_header;
    }
    s->mode_count = mode_count = last_mode_count;
    /* Determine the number of bits required to code the mode and turn that
     * into a bitmask to directly access the mode from the first frame byte. */
    s->mode_mask = ((1 << (av_log2(mode_count - 1) + 1)) - 1) << 1;
    /* The previous window flag is the next bit after the mode */
    s->prev_mask = (s->mode_mask | 0x1) + 1;

    init_get_bits(&gb, rev_buf, buf_size * 8);
    skip_bits_long(&gb, got_framing_bit);
    for (i = mode_count - 1; i >= 0; i--) {
        skip_bits_long(&gb, 40);
        s->mode_blocksize[i] = get_bits1(&gb);
    }

bad_header:
    av_free(rev_buf);
    return ret;
}

static int vorbis_parse_init(AVVorbisParseContext *s,
                             const uint8_t *extradata, int extradata_size)
{
    uint8_t *header_start[3];
    int header_len[3];
    int ret;

    s->class = &vorbis_parser_class;
    s->extradata_parsed = 1;

    if ((ret = avpriv_split_xiph_headers(extradata,
                                         extradata_size, 30,
                                         header_start, header_len)) < 0) {
        av_log(s, AV_LOG_ERROR, "Extradata corrupt.\n");
        return ret;
    }

    if ((ret = parse_id_header(s, header_start[0], header_len[0])) < 0)
        return ret;

    if ((ret = parse_setup_header(s, header_start[2], header_len[2])) < 0)
        return ret;

    s->valid_extradata = 1;
    s->previous_blocksize = s->blocksize[s->mode_blocksize[0]];

    return 0;
}

int av_vorbis_parse_frame_flags(AVVorbisParseContext *s, const uint8_t *buf,
                                int buf_size, int *flags)
{
    int duration = 0;

    if (s->valid_extradata && buf_size > 0) {
        int mode, current_blocksize;
        int previous_blocksize = s->previous_blocksize;

        if (buf[0] & 1) {
            /* If the user doesn't care about special packets, it's a bad one. */
            if (!flags)
                goto bad_packet;

            /* Set the flag for which kind of special packet it is. */
            if (buf[0] == 1)
                *flags |= VORBIS_FLAG_HEADER;
            else if (buf[0] == 3)
                *flags |= VORBIS_FLAG_COMMENT;
            else
                goto bad_packet;

            /* Special packets have no duration. */
            return 0;

bad_packet:
            av_log(s, AV_LOG_ERROR, "Invalid packet\n");
            return AVERROR_INVALIDDATA;
        }
        if (s->mode_count == 1)
            mode = 0;
        else
            mode = (buf[0] & s->mode_mask) >> 1;
        if (mode >= s->mode_count) {
            av_log(s, AV_LOG_ERROR, "Invalid mode in packet\n");
            return AVERROR_INVALIDDATA;
        }
        if(s->mode_blocksize[mode]){
            int flag = !!(buf[0] & s->prev_mask);
            previous_blocksize = s->blocksize[flag];
        }
        current_blocksize     = s->blocksize[s->mode_blocksize[mode]];
        duration              = (previous_blocksize + current_blocksize) >> 2;
        s->previous_blocksize = current_blocksize;
    }

    return duration;
}

int av_vorbis_parse_frame(AVVorbisParseContext *s, const uint8_t *buf,
                          int buf_size)
{
    return av_vorbis_parse_frame_flags(s, buf, buf_size, NULL);
}

void av_vorbis_parse_reset(AVVorbisParseContext *s)
{
    if (s->valid_extradata)
        s->previous_blocksize = s->blocksize[0];
}

void av_vorbis_parse_free(AVVorbisParseContext **s)
{
    av_freep(s);
}

AVVorbisParseContext *av_vorbis_parse_init(const uint8_t *extradata,
                                           int extradata_size)
{
    AVVorbisParseContext *s = av_mallocz(sizeof(*s));
    int ret;

    if (!s)
        return NULL;

    ret = vorbis_parse_init(s, extradata, extradata_size);
    if (ret < 0) {
        av_vorbis_parse_free(&s);
        return NULL;
    }

    return s;
}

#if LIBAVCODEC_VERSION_MAJOR < 57
int avpriv_vorbis_parse_extradata(AVCodecContext *avctx, AVVorbisParseContext *s)
{
    return vorbis_parse_init(s, avctx->extradata, avctx->extradata_size);
}
void avpriv_vorbis_parse_reset(AVVorbisParseContext *s)
{
    av_vorbis_parse_reset(s);
}
int avpriv_vorbis_parse_frame(AVVorbisParseContext *s, const uint8_t *buf,
                              int buf_size)
{
    return av_vorbis_parse_frame(s, buf, buf_size);
}
int avpriv_vorbis_parse_frame_flags(AVVorbisParseContext *s, const uint8_t *buf,
                                    int buf_size, int *flags)
{
    return av_vorbis_parse_frame_flags(s, buf, buf_size, flags);
}
#endif

#if CONFIG_VORBIS_PARSER

typedef struct VorbisParseContext {
    AVVorbisParseContext *vp;
} VorbisParseContext;

static int vorbis_parse(AVCodecParserContext *s1, AVCodecContext *avctx,
                        const uint8_t **poutbuf, int *poutbuf_size,
                        const uint8_t *buf, int buf_size)
{
    VorbisParseContext *s = s1->priv_data;
    int duration;

    if (!s->vp && avctx->extradata && avctx->extradata_size) {
        s->vp = av_vorbis_parse_init(avctx->extradata, avctx->extradata_size);
    }
    if (!s->vp)
        goto end;

    if ((duration = av_vorbis_parse_frame(s->vp, buf, buf_size)) >= 0)
        s1->duration = duration;

end:
    /* always return the full packet. this parser isn't doing any splitting or
       combining, only packet analysis */
    *poutbuf      = buf;
    *poutbuf_size = buf_size;
    return buf_size;
}

static void vorbis_parser_close(AVCodecParserContext *ctx)
{
    VorbisParseContext *s = ctx->priv_data;
    av_vorbis_parse_free(&s->vp);
}

AVCodecParser ff_vorbis_parser = {
    .codec_ids      = { AV_CODEC_ID_VORBIS },
    .priv_data_size = sizeof(VorbisParseContext),
    .parser_parse   = vorbis_parse,
    .parser_close   = vorbis_parser_close,
};
#endif /* CONFIG_VORBIS_PARSER */
