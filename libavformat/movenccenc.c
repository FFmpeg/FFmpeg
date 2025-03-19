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
#include "libavcodec/av1_parse.h"
#include "libavcodec/bytestream.h"
#include "libavcodec/cbs_av1.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "avio_internal.h"
#include "movenc.h"
#include "avc.h"
#include "nal.h"

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
    nal_start = ff_nal_find_startcode(p, end);
    for (;;) {
        while (nal_start < end && !*(nal_start++));
        if (nal_start == end)
            break;

        nal_end = ff_nal_find_startcode(nal_start, end);

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

static int write_tiles(AVFormatContext *s, MOVMuxCencContext *ctx, AVIOContext *pb, AV1_OBU_Type type,
                       const AV1RawFrameHeader *frame_header, const uint8_t *fh_data, size_t fh_data_size,
                       const AV1RawTileGroup *tile_group)
{
    GetByteContext gb;
    size_t tgh_data_size = tile_group->data_size;
    int cur_tile_num = frame_header->tile_cols * frame_header->tile_rows;
    int total = 0;

    // Get the Frame Header size
    if (type == AV1_OBU_FRAME)
        fh_data_size -= tgh_data_size;
    // Get the Tile Group Header size
    tgh_data_size -= tile_group->tile_data.data_size;

    if (ctx->tile_num < cur_tile_num) {
        int ret = av_reallocp_array(&ctx->tile_group_sizes, cur_tile_num,
                                    sizeof(*ctx->tile_group_sizes));
        if (ret < 0) {
            ctx->tile_num = 0;
            return ret;
        }
    }
    ctx->tile_num = cur_tile_num;

    total = fh_data_size + tgh_data_size;
    ctx->clear_bytes += total;

    bytestream2_init(&gb, tile_group->tile_data.data, tile_group->tile_data.data_size);

    // Build a table with block sizes for encrypted bytes and clear bytes
    for (unsigned tile_num = tile_group->tg_start; tile_num <= tile_group->tg_end; tile_num++) {
        uint32_t encrypted_bytes, tile_size_bytes, tile_size = 0;

        if (tile_num == tile_group->tg_end) {
            tile_size = bytestream2_get_bytes_left(&gb);
            encrypted_bytes = tile_size & ~0xFU;
            ctx->clear_bytes += tile_size & 0xFU;

            ctx->tile_group_sizes[tile_num].encrypted_bytes   = encrypted_bytes;
            ctx->tile_group_sizes[tile_num].aux_clear_bytes   = encrypted_bytes ? ctx->clear_bytes : 0;
            ctx->tile_group_sizes[tile_num].write_clear_bytes = tile_size & 0xFU;

            if (encrypted_bytes)
                ctx->clear_bytes = 0;
            total += tile_size;

            break;
        }

        tile_size_bytes = frame_header->tile_size_bytes_minus1 + 1;
        if (bytestream2_get_bytes_left(&gb) < tile_size_bytes)
            return AVERROR_INVALIDDATA;

        for (int i = 0; i < tile_size_bytes; i++)
            tile_size |= bytestream2_get_byteu(&gb) << 8 * i;
        if (bytestream2_get_bytes_left(&gb) <= tile_size)
            return AVERROR_INVALIDDATA;
        tile_size++;

        // The spec requires encrypted bytes to be in blocks multiple of 16
        encrypted_bytes   =  tile_size & ~0xFU;
        ctx->clear_bytes += (tile_size &  0xFU) + tile_size_bytes;

        ctx->tile_group_sizes[tile_num].encrypted_bytes   = encrypted_bytes;
        ctx->tile_group_sizes[tile_num].aux_clear_bytes   = encrypted_bytes ? ctx->clear_bytes : 0;
        ctx->tile_group_sizes[tile_num].write_clear_bytes = (tile_size & 0xFU) + tile_size_bytes;

        if (encrypted_bytes)
            ctx->clear_bytes = 0;

        total += tile_size + tile_size_bytes;
        bytestream2_skipu(&gb, tile_size);
    }

    bytestream2_init(&gb, tile_group->tile_data.data, tile_group->tile_data.data_size);

    avio_write(pb, fh_data, fh_data_size);
    avio_write(pb, tile_group->data, tgh_data_size);

    for (unsigned tile_num = tile_group->tg_start; tile_num <= tile_group->tg_end; tile_num++) {
        const struct MOVMuxCencAV1TGInfo *sizes = &ctx->tile_group_sizes[tile_num];

        avio_write(pb, gb.buffer, sizes->write_clear_bytes);
        bytestream2_skipu(&gb, sizes->write_clear_bytes);
        mov_cenc_write_encrypted(ctx, pb, gb.buffer, sizes->encrypted_bytes);
        bytestream2_skipu(&gb, sizes->encrypted_bytes);
        if (sizes->encrypted_bytes) {
            unsigned clear_bytes = sizes->aux_clear_bytes;
            if (clear_bytes > UINT16_MAX) {
                auxiliary_info_add_subsample(ctx, UINT16_MAX, 0);
                clear_bytes -= UINT16_MAX;
            }
            auxiliary_info_add_subsample(ctx, clear_bytes, sizes->encrypted_bytes);
        }
    }

    return total;
}

int ff_mov_cenc_av1_write_obus(AVFormatContext *s, MOVMuxCencContext* ctx,
                               AVIOContext *pb, const AVPacket *pkt)
{
    CodedBitstreamFragment *td = &ctx->temporal_unit;
    const CodedBitstreamAV1Context *av1 = ctx->cbc->priv_data;
    const AV1RawFrameHeader *frame_header = NULL;
    const uint8_t *fh_data = NULL;
    size_t fh_data_size;
    int out_size = 0, ret;

    ret = mov_cenc_start_packet(ctx);
    if (ret) {
        return ret;
    }

    ret = ff_lavf_cbs_read_packet(ctx->cbc, td, pkt);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "CENC-AV1: Failed to parse temporal unit.\n");
        return ret;
    }

    if (!av1->sequence_header) {
        av_log(s, AV_LOG_ERROR, "CENC-AV1: No sequence header available\n");
        ret = AVERROR_INVALIDDATA;
        goto end;
    }

    for (int i = 0; i < td->nb_units; i++) {
        const CodedBitstreamUnit *unit = &td->units[i];
        const AV1RawOBU *obu = unit->content;

        switch (unit->type) {
        case AV1_OBU_FRAME_HEADER:
            if (!obu->obu.frame_header.show_existing_frame) {
                frame_header = &obu->obu.frame_header;
                fh_data      = unit->data;
                fh_data_size = unit->data_size;
                break;
            }
        // fall-through
        case AV1_OBU_SEQUENCE_HEADER:
        case AV1_OBU_METADATA:
            avio_write(pb, unit->data, unit->data_size);
            ctx->clear_bytes += unit->data_size;
            out_size += unit->data_size;
            break;
        case AV1_OBU_FRAME:
            frame_header = &obu->obu.frame.header;
            fh_data      = unit->data;
            fh_data_size = unit->data_size;
        // fall-through
        case AV1_OBU_TILE_GROUP:
            {
                const AV1RawTileGroup *tile_group;

                if (!frame_header){
                    ret = AVERROR_INVALIDDATA;
                    goto end;
                }

                if (unit->type == AV1_OBU_FRAME)
                    tile_group = &obu->obu.frame.tile_group;
                else
                    tile_group = &obu->obu.tile_group;

                ret = write_tiles(s, ctx, pb, unit->type,
                                  frame_header, fh_data, fh_data_size, tile_group);
                if (ret < 0) {
                    av_log(s, AV_LOG_ERROR, "CENC-AV1: Failed to write tiles\n");
                    goto end;
                }
                av_assert0(ret == unit->data_size);
                out_size += unit->data_size;
                frame_header = NULL;
            }
            break;
        default:
            break;
        }
    }

    if (ctx->clear_bytes)
        auxiliary_info_add_subsample(ctx, ctx->clear_bytes, 0);
    ctx->clear_bytes = 0;

    ret = mov_cenc_end_packet(ctx);
    if (ret) {
        ret = AVERROR_INVALIDDATA;
        goto end;
    }

    ret = out_size;
end:
    ff_lavf_cbs_fragment_reset(td);
    return ret;
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

void ff_mov_cenc_write_stbl_atoms(MOVMuxCencContext* ctx, AVIOContext *pb,
                                  int64_t moof_offset)
{
    int64_t auxiliary_info_offset;

    mov_cenc_write_senc_tag(ctx, pb, &auxiliary_info_offset);
    mov_cenc_write_saio_tag(pb, auxiliary_info_offset - moof_offset);
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

static const CodedBitstreamUnitType decompose_unit_types[] = {
    AV1_OBU_TEMPORAL_DELIMITER,
    AV1_OBU_SEQUENCE_HEADER,
    AV1_OBU_FRAME_HEADER,
    AV1_OBU_TILE_GROUP,
    AV1_OBU_FRAME,
};

int ff_mov_cenc_init(MOVMuxCencContext* ctx, uint8_t* encryption_key,
                     int use_subsamples, enum AVCodecID codec_id, int bitexact)
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

    if (codec_id == AV_CODEC_ID_AV1) {
        ret = ff_lavf_cbs_init(&ctx->cbc, codec_id, NULL);
        if (ret < 0)
            return ret;

        ctx->cbc->decompose_unit_types    = decompose_unit_types;
        ctx->cbc->nb_decompose_unit_types = FF_ARRAY_ELEMS(decompose_unit_types);
    }

    return 0;
}

void ff_mov_cenc_free(MOVMuxCencContext* ctx)
{
    av_aes_ctr_free(ctx->aes_ctr);
    av_freep(&ctx->auxiliary_info);
    av_freep(&ctx->auxiliary_info_sizes);

    av_freep(&ctx->tile_group_sizes);
    ff_lavf_cbs_fragment_free(&ctx->temporal_unit);
    ff_lavf_cbs_close(&ctx->cbc);
}
