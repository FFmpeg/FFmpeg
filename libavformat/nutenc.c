/*
 * nut muxer
 * Copyright (c) 2004-2007 Michael Niedermayer
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

#include <stdint.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/tree.h"
#include "libavutil/dict.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"
#include "libavutil/opt.h"
#include "libavcodec/bytestream.h"
#include "libavcodec/mpegaudiodata.h"
#include "nut.h"
#include "internal.h"
#include "avio_internal.h"
#include "riff.h"

static int find_expected_header(AVCodecParameters *p, int size, int key_frame,
                                uint8_t out[64])
{
    int sample_rate = p->sample_rate;

    if (size > 4096)
        return 0;

    AV_WB24(out, 1);

    if (p->codec_id == AV_CODEC_ID_MPEG4) {
        if (key_frame) {
            return 3;
        } else {
            out[3] = 0xB6;
            return 4;
        }
    } else if (p->codec_id == AV_CODEC_ID_MPEG1VIDEO ||
               p->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        return 3;
    } else if (p->codec_id == AV_CODEC_ID_H264) {
        return 3;
    } else if (p->codec_id == AV_CODEC_ID_MP3 ||
               p->codec_id == AV_CODEC_ID_MP2) {
        int lsf, mpeg25, sample_rate_index, bitrate_index, frame_size;
        int layer           = p->codec_id == AV_CODEC_ID_MP3 ? 3 : 2;
        unsigned int header = 0xFFF00000;

        lsf           = sample_rate < (24000 + 32000) / 2;
        mpeg25        = sample_rate < (12000 + 16000) / 2;
        sample_rate <<= lsf + mpeg25;
        if      (sample_rate < (32000 + 44100) / 2) sample_rate_index = 2;
        else if (sample_rate < (44100 + 48000) / 2) sample_rate_index = 0;
        else                                        sample_rate_index = 1;

        sample_rate = ff_mpa_freq_tab[sample_rate_index] >> (lsf + mpeg25);

        for (bitrate_index = 2; bitrate_index < 30; bitrate_index++) {
            frame_size =
                ff_mpa_bitrate_tab[lsf][layer - 1][bitrate_index >> 1];
            frame_size = (frame_size * 144000) / (sample_rate << lsf) +
                (bitrate_index & 1);

            if (frame_size == size)
                break;
        }

        header |= (!lsf) << 19;
        header |= (4 - layer) << 17;
        header |= 1 << 16; //no crc
        AV_WB32(out, header);
        if (size <= 0)
            return 2;  //we guess there is no crc, if there is one the user clearly does not care about overhead
        if (bitrate_index == 30)
            return -1;  //something is wrong ...

        header |= (bitrate_index >> 1) << 12;
        header |= sample_rate_index << 10;
        header |= (bitrate_index & 1) << 9;

        return 2; //FIXME actually put the needed ones in build_elision_headers()
        //return 3; //we guess that the private bit is not set
//FIXME the above assumptions should be checked, if these turn out false too often something should be done
    }
    return 0;
}

static int find_header_idx(AVFormatContext *s, AVCodecParameters *p, int size,
                           int frame_type)
{
    NUTContext *nut = s->priv_data;
    uint8_t out[64];
    int i;
    int len = find_expected_header(p, size, frame_type, out);

    for (i = 1; i < nut->header_count; i++) {
        if (len == nut->header_len[i] && !memcmp(out, nut->header[i], len)) {
            return i;
        }
    }

    return 0;
}

static void build_elision_headers(AVFormatContext *s)
{
    NUTContext *nut = s->priv_data;
    int i;
    //FIXME this is lame
    //FIXME write a 2pass mode to find the maximal headers
    static const uint8_t headers[][5] = {
        { 3, 0x00, 0x00, 0x01 },
        { 4, 0x00, 0x00, 0x01, 0xB6},
        { 2, 0xFF, 0xFA }, //mp3+crc
        { 2, 0xFF, 0xFB }, //mp3
        { 2, 0xFF, 0xFC }, //mp2+crc
        { 2, 0xFF, 0xFD }, //mp2
    };

    nut->header_count = 7;
    for (i = 1; i < nut->header_count; i++) {
        nut->header_len[i] = headers[i - 1][0];
        nut->header[i]     = &headers[i - 1][1];
    }
}

static void build_frame_code(AVFormatContext *s)
{
    NUTContext *nut = s->priv_data;
    int key_frame, index, pred, stream_id;
    int start = 1;
    int end   = 254;
    int keyframe_0_esc = s->nb_streams > 2;
    int pred_table[10];
    FrameCode *ft;

    ft            = &nut->frame_code[start];
    ft->flags     = FLAG_CODED;
    ft->size_mul  = 1;
    ft->pts_delta = 1;
    start++;

    if (keyframe_0_esc) {
        /* keyframe = 0 escape */
        FrameCode *ft = &nut->frame_code[start];
        ft->flags    = FLAG_STREAM_ID | FLAG_SIZE_MSB | FLAG_CODED_PTS;
        ft->size_mul = 1;
        start++;
    }

    for (stream_id = 0; stream_id < s->nb_streams; stream_id++) {
        int start2 = start + (end - start) * stream_id       / s->nb_streams;
        int end2   = start + (end - start) * (stream_id + 1) / s->nb_streams;
        AVCodecParameters *par        = s->streams[stream_id]->codecpar;
        int is_audio                  = par->codec_type == AVMEDIA_TYPE_AUDIO;
        int intra_only        = /*codec->intra_only || */ is_audio;
        int pred_count;
        int frame_size = 0;

        if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            frame_size = av_get_audio_frame_duration2(par, 0);
            if (par->codec_id == AV_CODEC_ID_VORBIS && !frame_size)
                frame_size = 64;
        } else {
            AVRational f = av_div_q(av_inv_q(s->streams[stream_id]->avg_frame_rate), *nut->stream[stream_id].time_base);
            if (f.den == 1 && f.num>0)
                frame_size = f.num;
        }
        if (!frame_size)
            frame_size = 1;

        for (key_frame = 0; key_frame < 2; key_frame++) {
            if (!intra_only || !keyframe_0_esc || key_frame != 0) {
                FrameCode *ft = &nut->frame_code[start2];
                ft->flags     = FLAG_KEY * key_frame;
                ft->flags    |= FLAG_SIZE_MSB | FLAG_CODED_PTS;
                ft->stream_id = stream_id;
                ft->size_mul  = 1;
                if (is_audio)
                    ft->header_idx = find_header_idx(s, par, -1, key_frame);
                start2++;
            }
        }

        key_frame = intra_only;
#if 1
        if (is_audio) {
            int frame_bytes;
            int pts;

            if (par->block_align > 0) {
                frame_bytes = par->block_align;
            } else {
                int frame_size = av_get_audio_frame_duration2(par, 0);
                frame_bytes = frame_size * (int64_t)par->bit_rate / (8 * par->sample_rate);
            }

            for (pts = 0; pts < 2; pts++) {
                for (pred = 0; pred < 2; pred++) {
                    FrameCode *ft  = &nut->frame_code[start2];
                    ft->flags      = FLAG_KEY * key_frame;
                    ft->stream_id  = stream_id;
                    ft->size_mul   = frame_bytes + 2;
                    ft->size_lsb   = frame_bytes + pred;
                    ft->pts_delta  = pts * frame_size;
                    ft->header_idx = find_header_idx(s, par, frame_bytes + pred, key_frame);
                    start2++;
                }
            }
        } else {
            FrameCode *ft = &nut->frame_code[start2];
            ft->flags     = FLAG_KEY | FLAG_SIZE_MSB;
            ft->stream_id = stream_id;
            ft->size_mul  = 1;
            ft->pts_delta = frame_size;
            start2++;
        }
#endif

        if (par->video_delay) {
            pred_count    = 5;
            pred_table[0] = -2;
            pred_table[1] = -1;
            pred_table[2] = 1;
            pred_table[3] = 3;
            pred_table[4] = 4;
        } else if (par->codec_id == AV_CODEC_ID_VORBIS) {
            pred_count    = 3;
            pred_table[0] = 2;
            pred_table[1] = 9;
            pred_table[2] = 16;
        } else {
            pred_count    = 1;
            pred_table[0] = 1;
        }

        for (pred = 0; pred < pred_count; pred++) {
            int start3 = start2 + (end2 - start2) * pred / pred_count;
            int end3   = start2 + (end2 - start2) * (pred + 1) / pred_count;

            pred_table[pred] *= frame_size;

            for (index = start3; index < end3; index++) {
                FrameCode *ft = &nut->frame_code[index];
                ft->flags     = FLAG_KEY * key_frame;
                ft->flags    |= FLAG_SIZE_MSB;
                ft->stream_id = stream_id;
//FIXME use single byte size and pred from last
                ft->size_mul  = end3 - start3;
                ft->size_lsb  = index - start3;
                ft->pts_delta = pred_table[pred];
                if (is_audio)
                    ft->header_idx = find_header_idx(s, par, -1, key_frame);
            }
        }
    }
    memmove(&nut->frame_code['N' + 1], &nut->frame_code['N'], sizeof(FrameCode) * (255 - 'N'));
    nut->frame_code[0].flags       =
        nut->frame_code[255].flags =
        nut->frame_code['N'].flags = FLAG_INVALID;
}

/**
 * Get the length in bytes which is needed to store val as v.
 */
static int get_v_length(uint64_t val)
{
    int i = 1;

    while (val >>= 7)
        i++;

    return i;
}

/**
 * Put val using a variable number of bytes.
 */
static void put_v(AVIOContext *bc, uint64_t val)
{
    int i = get_v_length(val);

    while (--i > 0)
        avio_w8(bc, 128 | (uint8_t)(val >> (7*i)));

    avio_w8(bc, val & 127);
}

static void put_tt(NUTContext *nut, AVRational *time_base, AVIOContext *bc, uint64_t val)
{
    val *= nut->time_base_count;
    val += time_base - nut->time_base;
    put_v(bc, val);
}
/**
 * Store a string as vb.
 */
static void put_str(AVIOContext *bc, const char *string)
{
    size_t len = strlen(string);

    put_v(bc, len);
    avio_write(bc, string, len);
}

static void put_s(AVIOContext *bc, int64_t val)
{
    put_v(bc, 2 * FFABS(val) - (val > 0));
}

static void put_packet(NUTContext *nut, AVIOContext *bc, AVIOContext *dyn_bc,
                       uint64_t startcode)
{
    uint8_t *dyn_buf = NULL;
    int dyn_size     = avio_get_dyn_buf(dyn_bc, &dyn_buf);
    int forw_ptr     = dyn_size + 4;

    if (forw_ptr > 4096)
        ffio_init_checksum(bc, ff_crc04C11DB7_update, 0);
    avio_wb64(bc, startcode);
    put_v(bc, forw_ptr);
    if (forw_ptr > 4096)
        avio_wl32(bc, ffio_get_checksum(bc));

    ffio_init_checksum(bc, ff_crc04C11DB7_update, 0);
    avio_write(bc, dyn_buf, dyn_size);
    avio_wl32(bc, ffio_get_checksum(bc));

    ffio_reset_dyn_buf(dyn_bc);
}

static void write_mainheader(NUTContext *nut, AVIOContext *bc)
{
    int i, j, tmp_pts, tmp_flags, tmp_stream, tmp_mul, tmp_size, tmp_fields,
        tmp_head_idx;
    int64_t tmp_match;

    put_v(bc, nut->version);
    if (nut->version > 3)
        put_v(bc, nut->minor_version = 1);
    put_v(bc, nut->avf->nb_streams);
    put_v(bc, nut->max_distance);
    put_v(bc, nut->time_base_count);

    for (i = 0; i < nut->time_base_count; i++) {
        put_v(bc, nut->time_base[i].num);
        put_v(bc, nut->time_base[i].den);
    }

    tmp_pts      = 0;
    tmp_mul      = 1;
    tmp_stream   = 0;
    tmp_match    = 1 - (1LL << 62);
    tmp_head_idx = 0;
    for (i = 0; i < 256; ) {
        tmp_fields = 0;
        tmp_size   = 0;
//        tmp_res=0;
        if (tmp_pts      != nut->frame_code[i].pts_delta ) tmp_fields = 1;
        if (tmp_mul      != nut->frame_code[i].size_mul  ) tmp_fields = 2;
        if (tmp_stream   != nut->frame_code[i].stream_id ) tmp_fields = 3;
        if (tmp_size     != nut->frame_code[i].size_lsb  ) tmp_fields = 4;
//        if (tmp_res    != nut->frame_code[i].res            ) tmp_fields=5;
        if (tmp_head_idx != nut->frame_code[i].header_idx) tmp_fields = 8;

        tmp_pts    = nut->frame_code[i].pts_delta;
        tmp_flags  = nut->frame_code[i].flags;
        tmp_stream = nut->frame_code[i].stream_id;
        tmp_mul    = nut->frame_code[i].size_mul;
        tmp_size   = nut->frame_code[i].size_lsb;
//        tmp_res   = nut->frame_code[i].res;
        tmp_head_idx = nut->frame_code[i].header_idx;

        for (j = 0; i < 256; j++, i++) {
            if (i == 'N') {
                j--;
                continue;
            }
            if (nut->frame_code[i].pts_delta  != tmp_pts      ||
                nut->frame_code[i].flags      != tmp_flags    ||
                nut->frame_code[i].stream_id  != tmp_stream   ||
                nut->frame_code[i].size_mul   != tmp_mul      ||
                nut->frame_code[i].size_lsb   != tmp_size + j ||
//              nut->frame_code[i].res        != tmp_res      ||
                nut->frame_code[i].header_idx != tmp_head_idx)
                break;
        }
        if (j != tmp_mul - tmp_size)
            tmp_fields = 6;

        put_v(bc, tmp_flags);
        put_v(bc, tmp_fields);
        if (tmp_fields > 0) put_s(bc, tmp_pts);
        if (tmp_fields > 1) put_v(bc, tmp_mul);
        if (tmp_fields > 2) put_v(bc, tmp_stream);
        if (tmp_fields > 3) put_v(bc, tmp_size);
        if (tmp_fields > 4) put_v(bc, 0 /*tmp_res*/);
        if (tmp_fields > 5) put_v(bc, j);
        if (tmp_fields > 6) put_v(bc, tmp_match);
        if (tmp_fields > 7) put_v(bc, tmp_head_idx);
    }
    put_v(bc, nut->header_count - 1);
    for (i = 1; i < nut->header_count; i++) {
        put_v(bc, nut->header_len[i]);
        avio_write(bc, nut->header[i], nut->header_len[i]);
    }
    // flags had been effectively introduced in version 4
    if (nut->version > 3)
        put_v(bc, nut->flags);
}

static int write_streamheader(AVFormatContext *avctx, AVIOContext *bc,
                              AVStream *st, int i)
{
    NUTContext *nut       = avctx->priv_data;
    AVCodecParameters *par = st->codecpar;

    put_v(bc, i);
    switch (par->codec_type) {
    case AVMEDIA_TYPE_VIDEO:    put_v(bc, 0); break;
    case AVMEDIA_TYPE_AUDIO:    put_v(bc, 1); break;
    case AVMEDIA_TYPE_SUBTITLE: put_v(bc, 2); break;
    default:                    put_v(bc, 3); break;
    }
    put_v(bc, 4);

    if (par->codec_tag) {
        avio_wl32(bc, par->codec_tag);
    } else {
        av_log(avctx, AV_LOG_ERROR, "No codec tag defined for stream %d\n", i);
        return AVERROR(EINVAL);
    }

    put_v(bc, nut->stream[i].time_base - nut->time_base);
    put_v(bc, nut->stream[i].msb_pts_shift);
    put_v(bc, nut->stream[i].max_pts_distance);
    put_v(bc, par->video_delay);
    avio_w8(bc, 0); /* flags: 0x1 - fixed_fps, 0x2 - index_present */

    put_v(bc, par->extradata_size);
    avio_write(bc, par->extradata, par->extradata_size);

    switch (par->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        put_v(bc, par->sample_rate);
        put_v(bc, 1);
        put_v(bc, par->channels);
        break;
    case AVMEDIA_TYPE_VIDEO:
        put_v(bc, par->width);
        put_v(bc, par->height);

        if (st->sample_aspect_ratio.num <= 0 ||
            st->sample_aspect_ratio.den <= 0) {
            put_v(bc, 0);
            put_v(bc, 0);
        } else {
            put_v(bc, st->sample_aspect_ratio.num);
            put_v(bc, st->sample_aspect_ratio.den);
        }
        put_v(bc, 0); /* csp type -- unknown */
        break;
    default:
        break;
    }
    return 0;
}

static int add_info(AVIOContext *bc, const char *type, const char *value)
{
    put_str(bc, type);
    put_s(bc, -1);
    put_str(bc, value);
    return 1;
}

static int write_globalinfo(NUTContext *nut, AVIOContext *bc)
{
    AVFormatContext *s   = nut->avf;
    AVDictionaryEntry *t = NULL;
    AVIOContext *dyn_bc;
    uint8_t *dyn_buf = NULL;
    int count        = 0, dyn_size;
    int ret          = avio_open_dyn_buf(&dyn_bc);
    if (ret < 0)
        return ret;

    ff_standardize_creation_time(s);
    while ((t = av_dict_get(s->metadata, "", t, AV_DICT_IGNORE_SUFFIX)))
        count += add_info(dyn_bc, t->key, t->value);

    put_v(bc, 0); //stream_if_plus1
    put_v(bc, 0); //chapter_id
    put_v(bc, 0); //timestamp_start
    put_v(bc, 0); //length

    put_v(bc, count);

    dyn_size = avio_close_dyn_buf(dyn_bc, &dyn_buf);
    avio_write(bc, dyn_buf, dyn_size);
    av_free(dyn_buf);
    return 0;
}

static int write_streaminfo(NUTContext *nut, AVIOContext *bc, int stream_id) {
    AVFormatContext *s= nut->avf;
    AVStream* st = s->streams[stream_id];
    AVDictionaryEntry *t = NULL;
    AVIOContext *dyn_bc;
    uint8_t *dyn_buf=NULL;
    int count=0, dyn_size, i;
    int ret = avio_open_dyn_buf(&dyn_bc);
    if (ret < 0)
        return ret;

    while ((t = av_dict_get(st->metadata, "", t, AV_DICT_IGNORE_SUFFIX)))
        count += add_info(dyn_bc, t->key, t->value);
    for (i=0; ff_nut_dispositions[i].flag; ++i) {
        if (st->disposition & ff_nut_dispositions[i].flag)
            count += add_info(dyn_bc, "Disposition", ff_nut_dispositions[i].str);
    }
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        uint8_t buf[256];
        if (st->r_frame_rate.num>0 && st->r_frame_rate.den>0)
            snprintf(buf, sizeof(buf), "%d/%d", st->r_frame_rate.num, st->r_frame_rate.den);
        else
            snprintf(buf, sizeof(buf), "%d/%d", st->avg_frame_rate.num, st->avg_frame_rate.den);
        count += add_info(dyn_bc, "r_frame_rate", buf);
    }
    dyn_size = avio_close_dyn_buf(dyn_bc, &dyn_buf);

    if (count) {
        put_v(bc, stream_id + 1); //stream_id_plus1
        put_v(bc, 0); //chapter_id
        put_v(bc, 0); //timestamp_start
        put_v(bc, 0); //length

        put_v(bc, count);

        avio_write(bc, dyn_buf, dyn_size);
    }

    av_free(dyn_buf);
    return count;
}

static int write_chapter(NUTContext *nut, AVIOContext *bc, int id)
{
    AVIOContext *dyn_bc;
    uint8_t *dyn_buf     = NULL;
    AVDictionaryEntry *t = NULL;
    AVChapter *ch        = nut->avf->chapters[id];
    int ret, dyn_size, count = 0;

    ret = avio_open_dyn_buf(&dyn_bc);
    if (ret < 0)
        return ret;

    put_v(bc, 0);                                           // stream_id_plus1
    put_s(bc, id + 1);                                      // chapter_id
    put_tt(nut, nut->chapter[id].time_base, bc, ch->start); // chapter_start
    put_v(bc, ch->end - ch->start);                         // chapter_len

    while ((t = av_dict_get(ch->metadata, "", t, AV_DICT_IGNORE_SUFFIX)))
        count += add_info(dyn_bc, t->key, t->value);

    put_v(bc, count);

    dyn_size = avio_close_dyn_buf(dyn_bc, &dyn_buf);
    avio_write(bc, dyn_buf, dyn_size);
    av_freep(&dyn_buf);
    return 0;
}

static int write_index(NUTContext *nut, AVIOContext *bc) {
    int i;
    Syncpoint dummy= { .pos= 0 };
    Syncpoint *next_node[2] = { NULL };
    int64_t startpos = avio_tell(bc);
    int64_t payload_size;

    put_tt(nut, nut->max_pts_tb, bc, nut->max_pts);

    put_v(bc, nut->sp_count);

    for (i=0; i<nut->sp_count; i++) {
        av_tree_find(nut->syncpoints, &dummy, ff_nut_sp_pos_cmp, (void**)next_node);
        put_v(bc, (next_node[1]->pos >> 4) - (dummy.pos>>4));
        dummy.pos = next_node[1]->pos;
    }

    for (i=0; i<nut->avf->nb_streams; i++) {
        StreamContext *nus= &nut->stream[i];
        int64_t last_pts= -1;
        int j, k;
        for (j=0; j<nut->sp_count; j++) {
            int flag;
            int n = 0;

            if (j && nus->keyframe_pts[j] == nus->keyframe_pts[j-1]) {
                av_log(nut->avf, AV_LOG_WARNING, "Multiple keyframes with same PTS\n");
                nus->keyframe_pts[j] = AV_NOPTS_VALUE;
            }

            flag = (nus->keyframe_pts[j] != AV_NOPTS_VALUE) ^ (j+1 == nut->sp_count);
            for (; j<nut->sp_count && (nus->keyframe_pts[j] != AV_NOPTS_VALUE) == flag; j++)
                n++;

            put_v(bc, 1 + 2 * flag + 4 * n);
            for (k= j - n; k<=j && k<nut->sp_count; k++) {
                if (nus->keyframe_pts[k] == AV_NOPTS_VALUE)
                    continue;
                av_assert0(nus->keyframe_pts[k] > last_pts);
                put_v(bc, nus->keyframe_pts[k] - last_pts);
                last_pts = nus->keyframe_pts[k];
            }
        }
    }

    payload_size = avio_tell(bc) - startpos + 8 + 4;

    avio_wb64(bc, 8 + payload_size + av_log2(payload_size) / 7 + 1 + 4*(payload_size > 4096));

    return 0;
}

static int write_headers(AVFormatContext *avctx, AVIOContext *bc)
{
    NUTContext *nut = avctx->priv_data;
    AVIOContext *dyn_bc;
    int i, ret;

    ff_metadata_conv_ctx(avctx, ff_nut_metadata_conv, NULL);

    ret = avio_open_dyn_buf(&dyn_bc);
    if (ret < 0)
        return ret;
    write_mainheader(nut, dyn_bc);
    put_packet(nut, bc, dyn_bc, MAIN_STARTCODE);

    for (i = 0; i < nut->avf->nb_streams; i++) {
        ret = write_streamheader(avctx, dyn_bc, nut->avf->streams[i], i);
        if (ret < 0) {
            goto fail;
        }
        put_packet(nut, bc, dyn_bc, STREAM_STARTCODE);
    }

    write_globalinfo(nut, dyn_bc);
    put_packet(nut, bc, dyn_bc, INFO_STARTCODE);

    for (i = 0; i < nut->avf->nb_streams; i++) {
        ret = write_streaminfo(nut, dyn_bc, i);
        if (ret > 0)
            put_packet(nut, bc, dyn_bc, INFO_STARTCODE);
        else if (ret < 0) {
            goto fail;
        }
    }

    for (i = 0; i < nut->avf->nb_chapters; i++) {
        ret = write_chapter(nut, dyn_bc, i);
        if (ret < 0) {
            goto fail;
        }
        put_packet(nut, bc, dyn_bc, INFO_STARTCODE);
    }

    nut->last_syncpoint_pos = INT_MIN;
    nut->header_count++;

    ret = 0;
fail:
    ffio_free_dyn_buf(&dyn_bc);

    return ret;
}

static int nut_write_header(AVFormatContext *s)
{
    NUTContext *nut = s->priv_data;
    AVIOContext *bc = s->pb;
    int i, j, ret;

    nut->avf = s;

    nut->version = FFMAX(NUT_STABLE_VERSION, 3 + !!nut->flags);
    if (nut->version > 3 && s->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
        av_log(s, AV_LOG_ERROR,
               "The additional syncpoint modes require version %d, "
               "that is currently not finalized, "
               "please set -f_strict experimental in order to enable it.\n",
               nut->version);
        return AVERROR_EXPERIMENTAL;
    }

    nut->stream   = av_calloc(s->nb_streams,  sizeof(*nut->stream ));
    nut->chapter  = av_calloc(s->nb_chapters, sizeof(*nut->chapter));
    nut->time_base= av_calloc(s->nb_streams +
                              s->nb_chapters, sizeof(*nut->time_base));
    if (!nut->stream || !nut->chapter || !nut->time_base)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        int ssize;
        AVRational time_base;
        ff_parse_specific_params(st, &time_base.den, &ssize, &time_base.num);

        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && st->codecpar->sample_rate) {
            time_base = (AVRational) {1, st->codecpar->sample_rate};
        } else {
            time_base = ff_choose_timebase(s, st, 48000);
        }

        avpriv_set_pts_info(st, 64, time_base.num, time_base.den);

        for (j = 0; j < nut->time_base_count; j++)
            if (!memcmp(&time_base, &nut->time_base[j], sizeof(AVRational))) {
                break;
            }
        nut->time_base[j]        = time_base;
        nut->stream[i].time_base = &nut->time_base[j];
        if (j == nut->time_base_count)
            nut->time_base_count++;

        if (INT64_C(1000) * time_base.num >= time_base.den)
            nut->stream[i].msb_pts_shift = 7;
        else
            nut->stream[i].msb_pts_shift = 14;
        nut->stream[i].max_pts_distance =
            FFMAX(time_base.den, time_base.num) / time_base.num;
    }

    for (i = 0; i < s->nb_chapters; i++) {
        AVChapter *ch = s->chapters[i];

        for (j = 0; j < nut->time_base_count; j++)
            if (!memcmp(&ch->time_base, &nut->time_base[j], sizeof(AVRational)))
                break;

        nut->time_base[j]         = ch->time_base;
        nut->chapter[i].time_base = &nut->time_base[j];
        if (j == nut->time_base_count)
            nut->time_base_count++;
    }

    nut->max_distance = MAX_DISTANCE;
    build_elision_headers(s);
    build_frame_code(s);
    av_assert0(nut->frame_code['N'].flags == FLAG_INVALID);

    avio_write(bc, ID_STRING, strlen(ID_STRING));
    avio_w8(bc, 0);

    if ((ret = write_headers(s, bc)) < 0)
        return ret;

    if (s->avoid_negative_ts < 0)
        s->avoid_negative_ts = 1;

    return 0;
}

static int get_needed_flags(NUTContext *nut, StreamContext *nus, FrameCode *fc,
                            AVPacket *pkt)
{
    int flags = 0;

    if (pkt->flags & AV_PKT_FLAG_KEY)
        flags |= FLAG_KEY;
    if (pkt->stream_index != fc->stream_id)
        flags |= FLAG_STREAM_ID;
    if (pkt->size / fc->size_mul)
        flags |= FLAG_SIZE_MSB;
    if (pkt->pts - nus->last_pts != fc->pts_delta)
        flags |= FLAG_CODED_PTS;
    if (pkt->side_data_elems && nut->version > 3)
        flags |= FLAG_SM_DATA;
    if (pkt->size > 2 * nut->max_distance)
        flags |= FLAG_CHECKSUM;
    if (FFABS(pkt->pts - nus->last_pts) > nus->max_pts_distance)
        flags |= FLAG_CHECKSUM;
    if (fc->header_idx)
        if (pkt->size < nut->header_len[fc->header_idx] ||
            pkt->size > 4096                            ||
            memcmp(pkt->data, nut->header    [fc->header_idx],
                              nut->header_len[fc->header_idx]))
            flags |= FLAG_HEADER_IDX;

    return flags | (fc->flags & FLAG_CODED);
}

static int find_best_header_idx(NUTContext *nut, AVPacket *pkt)
{
    int i;
    int best_i   = 0;
    int best_len = 0;

    if (pkt->size > 4096)
        return 0;

    for (i = 1; i < nut->header_count; i++)
        if (pkt->size >= nut->header_len[i]
            && nut->header_len[i] > best_len
            && !memcmp(pkt->data, nut->header[i], nut->header_len[i])) {
            best_i   = i;
            best_len = nut->header_len[i];
        }
    return best_i;
}

static int write_sm_data(AVFormatContext *s, AVIOContext *bc, AVPacket *pkt, int is_meta)
{
    int ret, i, dyn_size;
    unsigned flags;
    AVIOContext *dyn_bc;
    int sm_data_count = 0;
    uint8_t tmp[256];
    uint8_t *dyn_buf;

    ret = avio_open_dyn_buf(&dyn_bc);
    if (ret < 0)
        return ret;

    for (i = 0; i<pkt->side_data_elems; i++) {
        const uint8_t *data = pkt->side_data[i].data;
        int size = pkt->side_data[i].size;
        const uint8_t *data_end = data + size;

        if (is_meta) {
            if (   pkt->side_data[i].type == AV_PKT_DATA_METADATA_UPDATE
                || pkt->side_data[i].type == AV_PKT_DATA_STRINGS_METADATA) {
                if (!size || data[size-1]) {
                    ret = AVERROR(EINVAL);
                    goto fail;
                }
                while (data < data_end) {
                    const uint8_t *key = data;
                    const uint8_t *val = data + strlen(key) + 1;

                    if(val >= data_end) {
                        ret = AVERROR(EINVAL);
                        goto fail;
                    }
                    put_str(dyn_bc, key);
                    put_s(dyn_bc, -1);
                    put_str(dyn_bc, val);
                    data = val + strlen(val) + 1;
                    sm_data_count++;
                }
            }
        } else {
            switch (pkt->side_data[i].type) {
            case AV_PKT_DATA_PALETTE:
            case AV_PKT_DATA_NEW_EXTRADATA:
            case AV_PKT_DATA_MATROSKA_BLOCKADDITIONAL:
            default:
                if (pkt->side_data[i].type == AV_PKT_DATA_PALETTE) {
                    put_str(dyn_bc, "Palette");
                } else if(pkt->side_data[i].type == AV_PKT_DATA_NEW_EXTRADATA) {
                    put_str(dyn_bc, "Extradata");
                } else if(pkt->side_data[i].type == AV_PKT_DATA_MATROSKA_BLOCKADDITIONAL) {
                    snprintf(tmp, sizeof(tmp), "CodecSpecificSide%"PRId64"", AV_RB64(data));
                    put_str(dyn_bc, tmp);
                } else {
                    snprintf(tmp, sizeof(tmp), "UserData%s-SD-%d",
                            (s->flags & AVFMT_FLAG_BITEXACT) ? "Lavf" : LIBAVFORMAT_IDENT,
                            pkt->side_data[i].type);
                    put_str(dyn_bc, tmp);
                }
                put_s(dyn_bc, -2);
                put_str(dyn_bc, "bin");
                put_v(dyn_bc, pkt->side_data[i].size);
                avio_write(dyn_bc, data, pkt->side_data[i].size);
                sm_data_count++;
                break;
            case AV_PKT_DATA_PARAM_CHANGE:
                flags = bytestream_get_le32(&data);
                if (flags & AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_COUNT) {
                    put_str(dyn_bc, "Channels");
                    put_s(dyn_bc, bytestream_get_le32(&data));
                    sm_data_count++;
                }
                if (flags & AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_LAYOUT) {
                    put_str(dyn_bc, "ChannelLayout");
                    put_s(dyn_bc, -2);
                    put_str(dyn_bc, "u64");
                    put_v(dyn_bc, 8);
                    avio_write(dyn_bc, data, 8); data+=8;
                    sm_data_count++;
                }
                if (flags & AV_SIDE_DATA_PARAM_CHANGE_SAMPLE_RATE) {
                    put_str(dyn_bc, "SampleRate");
                    put_s(dyn_bc, bytestream_get_le32(&data));
                    sm_data_count++;
                }
                if (flags & AV_SIDE_DATA_PARAM_CHANGE_DIMENSIONS) {
                    put_str(dyn_bc, "Width");
                    put_s(dyn_bc, bytestream_get_le32(&data));
                    put_str(dyn_bc, "Height");
                    put_s(dyn_bc, bytestream_get_le32(&data));
                    sm_data_count+=2;
                }
                break;
            case AV_PKT_DATA_SKIP_SAMPLES:
                if (AV_RL32(data)) {
                    put_str(dyn_bc, "SkipStart");
                    put_s(dyn_bc, (unsigned)AV_RL32(data));
                    sm_data_count++;
                }
                if (AV_RL32(data+4)) {
                    put_str(dyn_bc, "SkipEnd");
                    put_s(dyn_bc, (unsigned)AV_RL32(data+4));
                    sm_data_count++;
                }
                break;
            case AV_PKT_DATA_METADATA_UPDATE:
            case AV_PKT_DATA_STRINGS_METADATA:
            case AV_PKT_DATA_QUALITY_STATS:
                // belongs into meta, not side data
                break;
            }
        }
    }

fail:
    put_v(bc, sm_data_count);
    dyn_size = avio_close_dyn_buf(dyn_bc, &dyn_buf);
    avio_write(bc, dyn_buf, dyn_size);
    av_freep(&dyn_buf);

    return ret;
}

static int nut_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    NUTContext *nut    = s->priv_data;
    StreamContext *nus = &nut->stream[pkt->stream_index];
    AVIOContext *bc    = s->pb, *dyn_bc, *sm_bc = NULL;
    FrameCode *fc;
    int64_t coded_pts;
    int best_length, frame_code, flags, needed_flags, i, header_idx;
    int best_header_idx;
    int key_frame = !!(pkt->flags & AV_PKT_FLAG_KEY);
    int store_sp  = 0;
    int ret = 0;
    int sm_size = 0;
    int data_size = pkt->size;
    uint8_t *sm_buf = NULL;

    if (pkt->pts < 0) {
        av_log(s, AV_LOG_ERROR,
               "Negative pts not supported stream %d, pts %"PRId64"\n",
               pkt->stream_index, pkt->pts);
        if (pkt->pts == AV_NOPTS_VALUE)
            av_log(s, AV_LOG_ERROR, "Try to enable the genpts flag\n");
        return AVERROR(EINVAL);
    }

    if (pkt->side_data_elems && nut->version > 3) {
        ret = avio_open_dyn_buf(&sm_bc);
        if (ret < 0)
            return ret;
        ret = write_sm_data(s, sm_bc, pkt, 0);
        if (ret >= 0)
            ret = write_sm_data(s, sm_bc, pkt, 1);
        sm_size = avio_close_dyn_buf(sm_bc, &sm_buf);
        if (ret < 0)
            goto fail;
        data_size += sm_size;
    }

    if (1LL << (20 + 3 * nut->header_count) <= avio_tell(bc))
        write_headers(s, bc);

    if (key_frame && !(nus->last_flags & FLAG_KEY))
        store_sp = 1;

    if (data_size + 30 /*FIXME check*/ + avio_tell(bc) >= nut->last_syncpoint_pos + nut->max_distance)
        store_sp = 1;

//FIXME: Ensure store_sp is 1 in the first place.

    if (store_sp &&
        (!(nut->flags & NUT_PIPE) || nut->last_syncpoint_pos == INT_MIN)) {
        int64_t sp_pos = INT64_MAX;

        ff_nut_reset_ts(nut, *nus->time_base, pkt->dts);
        for (i = 0; i < s->nb_streams; i++) {
            AVStream *st   = s->streams[i];
            FFStream *const sti = ffstream(st);
            int64_t dts_tb = av_rescale_rnd(pkt->dts,
                nus->time_base->num * (int64_t)nut->stream[i].time_base->den,
                nus->time_base->den * (int64_t)nut->stream[i].time_base->num,
                AV_ROUND_DOWN);
            int index = av_index_search_timestamp(st, dts_tb,
                                                  AVSEEK_FLAG_BACKWARD);
            if (index >= 0) {
                sp_pos = FFMIN(sp_pos, sti->index_entries[index].pos);
                if (!nut->write_index && 2*index > sti->nb_index_entries) {
                    memmove(sti->index_entries,
                            sti->index_entries + index,
                            sizeof(*sti->index_entries) * (sti->nb_index_entries - index));
                    sti->nb_index_entries -=  index;
                }
            }
        }

        nut->last_syncpoint_pos = avio_tell(bc);
        ret                     = avio_open_dyn_buf(&dyn_bc);
        if (ret < 0)
            goto fail;
        put_tt(nut, nus->time_base, dyn_bc, pkt->dts);
        put_v(dyn_bc, sp_pos != INT64_MAX ? (nut->last_syncpoint_pos - sp_pos) >> 4 : 0);

        if (nut->flags & NUT_BROADCAST) {
            put_tt(nut, nus->time_base, dyn_bc,
                   av_rescale_q(av_gettime(), AV_TIME_BASE_Q, *nus->time_base));
        }
        put_packet(nut, bc, dyn_bc, SYNCPOINT_STARTCODE);
        ffio_free_dyn_buf(&dyn_bc);

        if (nut->write_index) {
        if ((ret = ff_nut_add_sp(nut, nut->last_syncpoint_pos, 0 /*unused*/, pkt->dts)) < 0)
            goto fail;

        if ((1ll<<60) % nut->sp_count == 0)
            for (i=0; i<s->nb_streams; i++) {
                int j;
                StreamContext *nus = &nut->stream[i];
                av_reallocp_array(&nus->keyframe_pts, 2*nut->sp_count, sizeof(*nus->keyframe_pts));
                if (!nus->keyframe_pts) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                for (j=nut->sp_count == 1 ? 0 : nut->sp_count; j<2*nut->sp_count; j++)
                    nus->keyframe_pts[j] = AV_NOPTS_VALUE;
        }
        }
    }
    av_assert0(nus->last_pts != AV_NOPTS_VALUE);

    coded_pts = pkt->pts & ((1 << nus->msb_pts_shift) - 1);
    if (ff_lsb2full(nus, coded_pts) != pkt->pts)
        coded_pts = pkt->pts + (1 << nus->msb_pts_shift);

    best_header_idx = find_best_header_idx(nut, pkt);

    best_length = INT_MAX;
    frame_code  = -1;
    for (i = 0; i < 256; i++) {
        int length    = 0;
        FrameCode *fc = &nut->frame_code[i];
        int flags     = fc->flags;

        if (flags & FLAG_INVALID)
            continue;
        needed_flags = get_needed_flags(nut, nus, fc, pkt);

        if (flags & FLAG_CODED) {
            length++;
            flags = needed_flags;
        }

        if ((flags & needed_flags) != needed_flags)
            continue;

        if ((flags ^ needed_flags) & FLAG_KEY)
            continue;

        if (flags & FLAG_STREAM_ID)
            length += get_v_length(pkt->stream_index);

        if (data_size % fc->size_mul != fc->size_lsb)
            continue;
        if (flags & FLAG_SIZE_MSB)
            length += get_v_length(data_size / fc->size_mul);

        if (flags & FLAG_CHECKSUM)
            length += 4;

        if (flags & FLAG_CODED_PTS)
            length += get_v_length(coded_pts);

        if (   (flags & FLAG_CODED)
            && nut->header_len[best_header_idx] > nut->header_len[fc->header_idx] + 1) {
            flags |= FLAG_HEADER_IDX;
        }

        if (flags & FLAG_HEADER_IDX) {
            length += 1 - nut->header_len[best_header_idx];
        } else {
            length -= nut->header_len[fc->header_idx];
        }

        length *= 4;
        length += !(flags & FLAG_CODED_PTS);
        length += !(flags & FLAG_CHECKSUM);

        if (length < best_length) {
            best_length = length;
            frame_code  = i;
        }
    }
    av_assert0(frame_code != -1);

    fc           = &nut->frame_code[frame_code];
    flags        = fc->flags;
    needed_flags = get_needed_flags(nut, nus, fc, pkt);
    header_idx   = fc->header_idx;

    ffio_init_checksum(bc, ff_crc04C11DB7_update, 0);
    avio_w8(bc, frame_code);
    if (flags & FLAG_CODED) {
        put_v(bc, (flags ^ needed_flags) & ~(FLAG_CODED));
        flags = needed_flags;
    }
    if (flags & FLAG_STREAM_ID)  put_v(bc, pkt->stream_index);
    if (flags & FLAG_CODED_PTS)  put_v(bc, coded_pts);
    if (flags & FLAG_SIZE_MSB )  put_v(bc, data_size / fc->size_mul);
    if (flags & FLAG_HEADER_IDX) put_v(bc, header_idx = best_header_idx);

    if (flags & FLAG_CHECKSUM)   avio_wl32(bc, ffio_get_checksum(bc));
    else                         ffio_get_checksum(bc);

    if (flags & FLAG_SM_DATA) {
        avio_write(bc, sm_buf, sm_size);
    }
    avio_write(bc, pkt->data + nut->header_len[header_idx], pkt->size - nut->header_len[header_idx]);

    nus->last_flags = flags;
    nus->last_pts   = pkt->pts;

    //FIXME just store one per syncpoint
    if (flags & FLAG_KEY && !(nut->flags & NUT_PIPE)) {
        av_add_index_entry(
            s->streams[pkt->stream_index],
            nut->last_syncpoint_pos,
            pkt->pts,
            0,
            0,
            AVINDEX_KEYFRAME);
        if (nus->keyframe_pts && nus->keyframe_pts[nut->sp_count] == AV_NOPTS_VALUE)
            nus->keyframe_pts[nut->sp_count] = pkt->pts;
    }

    if (!nut->max_pts_tb || av_compare_ts(nut->max_pts, *nut->max_pts_tb, pkt->pts, *nus->time_base) < 0) {
        nut->max_pts = pkt->pts;
        nut->max_pts_tb = nus->time_base;
    }

fail:
    av_freep(&sm_buf);

    return ret;
}

static int nut_write_trailer(AVFormatContext *s)
{
    NUTContext *nut = s->priv_data;
    AVIOContext *bc = s->pb, *dyn_bc;
    int ret;

    while (nut->header_count < 3)
        write_headers(s, bc);

    if (!nut->sp_count)
        return 0;

    ret = avio_open_dyn_buf(&dyn_bc);
    if (ret >= 0) {
        av_assert1(nut->write_index); // sp_count should be 0 if no index is going to be written
        write_index(nut, dyn_bc);
        put_packet(nut, bc, dyn_bc, INDEX_STARTCODE);
        ffio_free_dyn_buf(&dyn_bc);
    }

    return 0;
}

static void nut_write_deinit(AVFormatContext *s)
{
    NUTContext *nut = s->priv_data;
    int i;

    ff_nut_free_sp(nut);
    if (nut->stream)
        for (i=0; i<s->nb_streams; i++)
            av_freep(&nut->stream[i].keyframe_pts);

    av_freep(&nut->stream);
    av_freep(&nut->chapter);
    av_freep(&nut->time_base);
}

#define OFFSET(x) offsetof(NUTContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "syncpoints",  "NUT syncpoint behaviour",                         OFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64 = 0},             INT_MIN, INT_MAX, E, "syncpoints" },
    { "default",     "",                                                0,             AV_OPT_TYPE_CONST, {.i64 = 0},             INT_MIN, INT_MAX, E, "syncpoints" },
    { "none",        "Disable syncpoints, low overhead and unseekable", 0,             AV_OPT_TYPE_CONST, {.i64 = NUT_PIPE},      INT_MIN, INT_MAX, E, "syncpoints" },
    { "timestamped", "Extend syncpoints with a wallclock timestamp",    0,             AV_OPT_TYPE_CONST, {.i64 = NUT_BROADCAST}, INT_MIN, INT_MAX, E, "syncpoints" },
    { "write_index", "Write index",                               OFFSET(write_index), AV_OPT_TYPE_BOOL,  {.i64 = 1},                   0,       1, E, },
    { NULL },
};

static const AVClass class = {
    .class_name = "nutenc",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const AVOutputFormat ff_nut_muxer = {
    .name           = "nut",
    .long_name      = NULL_IF_CONFIG_SMALL("NUT"),
    .mime_type      = "video/x-nut",
    .extensions     = "nut",
    .priv_data_size = sizeof(NUTContext),
    .audio_codec    = CONFIG_LIBVORBIS ? AV_CODEC_ID_VORBIS :
                      CONFIG_LIBMP3LAME ? AV_CODEC_ID_MP3 : AV_CODEC_ID_MP2,
    .video_codec    = AV_CODEC_ID_MPEG4,
    .write_header   = nut_write_header,
    .write_packet   = nut_write_packet,
    .write_trailer  = nut_write_trailer,
    .deinit         = nut_write_deinit,
    .flags          = AVFMT_GLOBALHEADER | AVFMT_VARIABLE_FPS,
    .codec_tag      = ff_nut_codec_tags,
    .priv_class     = &class,
};
