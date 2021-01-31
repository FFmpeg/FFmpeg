/*
 * MOV CENC (Common Encryption) writer
 * Copyright (c) 2015 Eran Kornblau <erankor at gmail dot com>
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
#include "movenccenc.h"
#include "libavutil/intreadwrite.h"
#include "avio_internal.h"
#include "movenc.h"
#include "avc.h"

static int auxiliary_info_alloc_size(MOVMuxCencContext* ctx, int size)
{
    size_t new_alloc_size;

    if (ctx->auxiliary_info_size + size > ctx->auxiliary_info_alloc_size) {
        new_alloc_size = FFMAX(ctx->auxiliary_info_size + size, ctx->auxiliary_info_alloc_size * 2);
        if (av_reallocp(&ctx->auxiliary_info, new_alloc_size)) {
            return AVERROR(ENOMEM);
        }

        ctx->auxiliary_info_alloc_size = new_alloc_size;
    }

    return 0;
}

static int auxiliary_info_write(MOVMuxCencContext* ctx,
                                         const uint8_t *buf_in, int size)
{
    int ret;

    ret = auxiliary_info_alloc_size(ctx, size);
    if (ret) {
        return ret;
    }
    memcpy(ctx->auxiliary_info + ctx->auxiliary_info_size, buf_in, size);
    ctx->auxiliary_info_size += size;

    return 0;
}

static int auxiliary_info_add_subsample(MOVMuxCencContext* ctx,
    uint16_t clear_bytes, uint32_t encrypted_bytes)
{
    uint8_t* p;
    int ret;

    if (!ctx->use_subsamples) {
        return 0;
    }

    ret = auxiliary_info_alloc_size(ctx, 6);
    if (ret) {
        return ret;
    }

    p = ctx->auxiliary_info + ctx->auxiliary_info_size;

    AV_WB16(p, clear_bytes);
    p += sizeof(uint16_t);

    AV_WB32(p, encrypted_bytes);

    ctx->auxiliary_info_size += 6;
    ctx->subsample_count++;

    return 0;
}

/**
 * Encrypt the input buffer and write using avio_write
 */
static void mov_cenc_write_encrypted(MOVMuxCencContext* ctx, AVIOContext *pb,
                                     const uint8_t *buf_in, int size)
{
    uint8_t chunk[4096];
    const uint8_t* cur_pos = buf_in;
    int size_left = size;
    int cur_size;

    while (size_left > 0) {
        cur_size = FFMIN(size_left, sizeof(chunk));
        av_aes_ctr_crypt(ctx->aes_ctr, chunk, cur_pos, cur_size);
        avio_write(pb, chunk, cur_size);
        cur_pos += cur_size;
        size_left -= cur_size;
    }
}

/**
 * Start writing a packet
 */
static int mov_cenc_start_packet(MOVMuxCencContext* ctx)
{
    int ret;

    /* write the iv */
    ret = auxiliary_info_write(ctx, av_aes_ctr_get_iv(ctx->aes_ctr), AES_CTR_IV_SIZE);
    if (ret) {
        return ret;
    }

    if (!ctx->use_subsamples) {
        return 0;
    }

    /* write a zero subsample count */
    ctx->auxiliary_info_subsample_start = ctx->auxiliary_info_size;
    ctx->subsample_count = 0;
    ret = auxiliary_info_write(ctx, (uint8_t*)&ctx->subsample_count, sizeof(ctx->subsample_count));
    if (ret) {
        return ret;
    }

    return 0;
}

/**
 * Finalize a packet
 */
static int mov_cenc_end_packet(MOVMuxCencContext* ctx)
{
    size_t new_alloc_size;

    av_aes_ctr_increment_iv(ctx->aes_ctr);

    if (!ctx->use_subsamples) {
        ctx->auxiliary_info_entries++;
        return 0;
    }

    /* add the auxiliary info entry size*/
    if (ctx->auxiliary_info_entries >= ctx->auxiliary_info_sizes_alloc_size) {
        new_alloc_size = ctx->auxiliary_info_entries * 2 + 1;
        if (av_reallocp(&ctx->auxiliary_info_sizes, new_alloc_size)) {
            return AVERROR(ENOMEM);
        }

        ctx->auxiliary_info_sizes_alloc_size = new_alloc_size;
    }
    ctx->auxiliary_info_sizes[ctx->auxiliary_info_entries] =
        AES_CTR_IV_SIZE + ctx->auxiliary_info_size - ctx->auxiliary_info_subsample_start;
    ctx->auxiliary_info_entries++;

    /* update the subsample count*/
    AV_WB16(ctx->auxiliary_info + ctx->auxiliary_info_subsample_start, ctx->subsample_count);

    return 0;
}

int ff_mov_cenc_write_packet(MOVMuxCencContext* ctx, AVIOContext *pb,
                          const uint8_t *buf_in, int size)
{
    int ret;

    ret = mov_cenc_start_packet(ctx);
    if (ret) {
        return ret;
    }

    ret = auxiliary_info_add_subsample(ctx, 0, size);
    if (ret) {
        return ret;
    }

    mov_cenc_write_encrypted(ctx, pb, buf_in, size);

    ret = mov_cenc_end_packet(ctx);
    if (ret) {
        return ret;
    }

    return 0;
}

int ff_mov_cenc_avc_parse_nal_units(MOVMuxCencContext* ctx, AVIOContext *pb,
                                 const uint8_t *buf_in, int size)
{
    const uint8_t *p = buf_in;
    const uint8_t *end = p + size;
    const uint8_t *nal_start, *nal_end;
    int ret;

    ret = mov_cenc_start_packet(ctx);
    if (ret) {
        return ret;
    }

    size = 0;
    nal_start = ff_avc_find_startcode(p, end);
    for (;;) {
        while (nal_start < end && !*(nal_start++));
        if (nal_start == end)
            break;

        nal_end = ff_avc_find_startcode(nal_start, end);

        avio_wb32(pb, nal_end - nal_start);
        avio_w8(pb, *nal_start);
        mov_cenc_write_encrypted(ctx, pb, nal_start + 1, nal_end - nal_start - 1);

        auxiliary_info_add_subsample(ctx, 5, nal_end - nal_start - 1);

        size += 4 + nal_end - nal_start;
        nal_start = nal_end;
    }

    ret = mov_cenc_end_packet(ctx);
    if (ret) {
        return ret;
    }

    return size;
}

int ff_mov_cenc_avc_write_nal_units(AVFormatContext *s, MOVMuxCencContext* ctx,
    int nal_length_size, AVIOContext *pb, const uint8_t *buf_in, int size)
{
    int nalsize;
    int ret;
    int j;

    ret = mov_cenc_start_packet(ctx);
    if (ret) {
        return ret;
    }

    while (size > 0) {
        /* parse the nal size */
        if (size < nal_length_size + 1) {
            av_log(s, AV_LOG_ERROR, "CENC-AVC: remaining size %d smaller than nal length+type %d\n",
                size, nal_length_size + 1);
            return -1;
        }

        avio_write(pb, buf_in, nal_length_size + 1);

        nalsize = 0;
        for (j = 0; j < nal_length_size; j++) {
            nalsize = (nalsize << 8) | *buf_in++;
        }
        size -= nal_length_size;

        /* encrypt the nal body */
        if (nalsize <= 0 || nalsize > size) {
            av_log(s, AV_LOG_ERROR, "CENC-AVC: nal size %d remaining %d\n", nalsize, size);
            return -1;
        }

        mov_cenc_write_encrypted(ctx, pb, buf_in + 1, nalsize - 1);
        buf_in += nalsize;
        size -= nalsize;

        auxiliary_info_add_subsample(ctx, nal_length_size + 1, nalsize - 1);
    }

    ret = mov_cenc_end_packet(ctx);
    if (ret) {
        return ret;
    }

    return 0;
}

/* TODO: reuse this function from movenc.c */
static int64_t update_size(AVIOContext *pb, int64_t pos)
{
    int64_t curpos = avio_tell(pb);
    avio_seek(pb, pos, SEEK_SET);
    avio_wb32(pb, curpos - pos); /* rewrite size */
    avio_seek(pb, curpos, SEEK_SET);

    return curpos - pos;
}

static int mov_cenc_write_senc_tag(MOVMuxCencContext* ctx, AVIOContext *pb,
                                   int64_t* auxiliary_info_offset)
{
    int64_t pos = avio_tell(pb);

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "senc");
    avio_wb32(pb, ctx->use_subsamples ? 0x02 : 0); /* version & flags */
    avio_wb32(pb, ctx->auxiliary_info_entries); /* entry count */
    *auxiliary_info_offset = avio_tell(pb);
    avio_write(pb, ctx->auxiliary_info, ctx->auxiliary_info_size);
    return update_size(pb, pos);
}

static int mov_cenc_write_saio_tag(AVIOContext *pb, int64_t auxiliary_info_offset)
{
    int64_t pos = avio_tell(pb);
    uint8_t version;

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "saio");
    version = auxiliary_info_offset > 0xffffffff ? 1 : 0;
    avio_w8(pb, version);
    avio_wb24(pb, 0); /* flags */
    avio_wb32(pb, 1); /* entry count */
    if (version) {
        avio_wb64(pb, auxiliary_info_offset);
    } else {
        avio_wb32(pb, auxiliary_info_offset);
    }
    return update_size(pb, pos);
}

static int mov_cenc_write_saiz_tag(MOVMuxCencContext* ctx, AVIOContext *pb)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "saiz");
    avio_wb32(pb, 0); /* version & flags */
    avio_w8(pb, ctx->use_subsamples ? 0 : AES_CTR_IV_SIZE);    /* default size*/
    avio_wb32(pb, ctx->auxiliary_info_entries); /* entry count */
    if (ctx->use_subsamples) {
        avio_write(pb, ctx->auxiliary_info_sizes, ctx->auxiliary_info_entries);
    }
    return update_size(pb, pos);
}

void ff_mov_cenc_write_stbl_atoms(MOVMuxCencContext* ctx, AVIOContext *pb)
{
    int64_t auxiliary_info_offset;

    mov_cenc_write_senc_tag(ctx, pb, &auxiliary_info_offset);
    mov_cenc_write_saio_tag(pb, auxiliary_info_offset);
    mov_cenc_write_saiz_tag(ctx, pb);
}

static int mov_cenc_write_schi_tag(AVIOContext *pb, uint8_t* kid)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0);     /* size */
    ffio_wfourcc(pb, "schi");

    avio_wb32(pb, 32);    /* size */
    ffio_wfourcc(pb, "tenc");
    avio_wb32(pb, 0);     /* version & flags */
    avio_wb24(pb, 1);     /* is encrypted */
    avio_w8(pb, AES_CTR_IV_SIZE); /* iv size */
    avio_write(pb, kid, CENC_KID_SIZE);

    return update_size(pb, pos);
}

int ff_mov_cenc_write_sinf_tag(MOVTrack* track, AVIOContext *pb, uint8_t* kid)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "sinf");

    /* frma */
    avio_wb32(pb, 12);    /* size */
    ffio_wfourcc(pb, "frma");
    avio_wl32(pb, track->tag);

    /* schm */
    avio_wb32(pb, 20);    /* size */
    ffio_wfourcc(pb, "schm");
    avio_wb32(pb, 0); /* version & flags */
    ffio_wfourcc(pb, "cenc");    /* scheme type*/
    avio_wb32(pb, 0x10000); /* scheme version */

    /* schi */
    mov_cenc_write_schi_tag(pb, kid);

    return update_size(pb, pos);
}

int ff_mov_cenc_init(MOVMuxCencContext* ctx, uint8_t* encryption_key,
                     int use_subsamples, int bitexact)
{
    int ret;

    ctx->aes_ctr = av_aes_ctr_alloc();
    if (!ctx->aes_ctr) {
        return AVERROR(ENOMEM);
    }

    ret = av_aes_ctr_init(ctx->aes_ctr, encryption_key);
    if (ret != 0) {
        return ret;
    }

    if (!bitexact) {
        av_aes_ctr_set_random_iv(ctx->aes_ctr);
    }

    ctx->use_subsamples = use_subsamples;

    return 0;
}

void ff_mov_cenc_free(MOVMuxCencContext* ctx)
{
    av_aes_ctr_free(ctx->aes_ctr);
    av_freep(&ctx->auxiliary_info);
    av_freep(&ctx->auxiliary_info_sizes);
}
