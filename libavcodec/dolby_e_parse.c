/*
 * Copyright (C) 2017 foo86
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

#include "libavutil/avassert.h"
#include "get_bits.h"
#include "put_bits.h"
#include "dolby_e.h"

static const uint8_t nb_programs_tab[MAX_PROG_CONF + 1] = {
    2, 3, 2, 3, 4, 5, 4, 5, 6, 7, 8, 1, 2, 3, 3, 4, 5, 6, 1, 2, 3, 4, 1, 1
};

static const uint8_t nb_channels_tab[MAX_PROG_CONF + 1] = {
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 6, 6, 6, 6, 6, 6, 6, 4, 4, 4, 4, 8, 8
};

static const uint16_t sample_rate_tab[16] = {
    0, 42965, 43008, 44800, 53706, 53760
};

static int skip_input(DBEContext *s, int nb_words)
{
    if (nb_words > s->input_size) {
        return AVERROR_INVALIDDATA;
    }

    s->input      += nb_words * s->word_bytes;
    s->input_size -= nb_words;
    return 0;
}

static int parse_key(DBEContext *s)
{
    if (s->key_present) {
        const uint8_t *key = s->input;
        int      ret = skip_input(s, 1);
        if (ret < 0)
            return ret;
        return AV_RB24(key) >> 24 - s->word_bits;
    }
    return 0;
}

int ff_dolby_e_convert_input(DBEContext *s, int nb_words, int key)
{
    const uint8_t *src = s->input;
    uint8_t *dst = s->buffer;
    PutBitContext pb;
    int i;

    av_assert0(nb_words <= 1024u);

    if (nb_words > s->input_size) {
        if (s->avctx)
            av_log(s->avctx, AV_LOG_ERROR, "Packet too short\n");
        return AVERROR_INVALIDDATA;
    }

    switch (s->word_bits) {
    case 16:
        for (i = 0; i < nb_words; i++, src += 2, dst += 2)
            AV_WB16(dst, AV_RB16(src) ^ key);
        break;
    case 20:
        init_put_bits(&pb, s->buffer, sizeof(s->buffer));
        for (i = 0; i < nb_words; i++, src += 3)
            put_bits(&pb, 20, AV_RB24(src) >> 4 ^ key);
        flush_put_bits(&pb);
        break;
    case 24:
        for (i = 0; i < nb_words; i++, src += 3, dst += 3)
            AV_WB24(dst, AV_RB24(src) ^ key);
        break;
    default:
        av_unreachable("ff_dolby_e_parse_header() only sets 16, 20, 24 and errors out otherwise");
    }

    return init_get_bits(&s->gb, s->buffer, nb_words * s->word_bits);
}

int ff_dolby_e_parse_header(DBEContext *s, const uint8_t *buf, int buf_size)
{
    DolbyEHeaderInfo *const header = &s->metadata;
    int hdr, ret, key, mtd_size;

    if (buf_size < 3)
        return AVERROR_INVALIDDATA;

    hdr = AV_RB24(buf);
    if ((hdr & 0xfffffe) == 0x7888e) {
        s->word_bits = 24;
    } else if ((hdr & 0xffffe0) == 0x788e0) {
        s->word_bits = 20;
    } else if ((hdr & 0xfffe00) == 0x78e00) {
        s->word_bits = 16;
    } else {
        if (s->avctx)
            av_log(s->avctx, AV_LOG_ERROR, "Invalid frame header\n");
        return AVERROR_INVALIDDATA;
    }

    s->word_bytes  = s->word_bits + 7 >> 3;
    s->input       = buf + s->word_bytes;
    s->input_size  = buf_size / s->word_bytes - 1;
    s->key_present = hdr >> 24 - s->word_bits & 1;

    if ((key = parse_key(s)) < 0)
        return key;
    if ((ret = ff_dolby_e_convert_input(s, 1, key)) < 0)
        return ret;

    skip_bits(&s->gb, 4);
    mtd_size = get_bits(&s->gb, 10);
    if (!mtd_size) {
        if (s->avctx)
            av_log(s->avctx, AV_LOG_ERROR, "Invalid metadata size\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_dolby_e_convert_input(s, mtd_size, key)) < 0)
        return ret;

    skip_bits(&s->gb, 14);
    header->prog_conf = get_bits(&s->gb, 6);
    if (header->prog_conf > MAX_PROG_CONF) {
        if (s->avctx)
            av_log(s->avctx, AV_LOG_ERROR, "Invalid program configuration\n");
        return AVERROR_INVALIDDATA;
    }

    header->nb_channels = nb_channels_tab[header->prog_conf];
    header->nb_programs = nb_programs_tab[header->prog_conf];

    header->fr_code      = get_bits(&s->gb, 4);
    header->fr_code_orig = get_bits(&s->gb, 4);
    if (!(header->sample_rate = sample_rate_tab[header->fr_code]) ||
        !sample_rate_tab[header->fr_code_orig]) {
        if (s->avctx)
            av_log(s->avctx, AV_LOG_ERROR, "Invalid frame rate code\n");
        return AVERROR_INVALIDDATA;
    }

    skip_bits_long(&s->gb, 88);
    for (int i = 0; i < header->nb_channels; i++)
        header->ch_size[i] = get_bits(&s->gb, 10);
    header->mtd_ext_size = get_bits(&s->gb, 8);
    header->meter_size   = get_bits(&s->gb, 8);

    skip_bits_long(&s->gb, 10 * header->nb_programs);
    for (int i = 0; i < header->nb_channels; i++) {
        header->rev_id[i]     = get_bits(&s->gb,  4);
        skip_bits1(&s->gb);
        header->begin_gain[i] = get_bits(&s->gb, 10);
        header->end_gain[i]   = get_bits(&s->gb, 10);
    }

    if (get_bits_left(&s->gb) < 0) {
        if (s->avctx)
            av_log(s->avctx, AV_LOG_ERROR, "Read past end of metadata\n");
        return AVERROR_INVALIDDATA;
    }

    return skip_input(s, mtd_size + 1);
}
