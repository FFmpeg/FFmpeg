/*
 * Test for HEVC mp4toannexb bitstream filter parameter set deduplication
 *
 * Copyright (c) 2026 FFmpeg developers
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

#include <stdio.h>
#include <string.h>

#include "libavutil/error.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/bsf.h"
#include "libavcodec/packet.h"

/* Valid 64x64 HEVC parameter sets and slice from x265 */
static const uint8_t vps_bytes[] = {
    0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x03, 0x70,
    0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x03, 0x00, 0x1e, 0x95, 0x94, 0x09
};

static const uint8_t sps_bytes[] = {
    0x42, 0x01, 0x01, 0x03, 0x70, 0x00, 0x00, 0x03,
    0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03,
    0x00, 0x1e, 0xa0, 0x20, 0x81, 0x05, 0x96, 0x56,
    0x54, 0xa4, 0xc2, 0xe0, 0x10, 0x00, 0x00, 0x3e,
    0x80, 0x00, 0x00, 0x3e, 0x80, 0x80
};

/* SPS 1 differs only in sps_seq_parameter_set_id: ue(v) 1 becomes 010.
 * Repacked trailing bits and reinserted emulation-prevention bytes. */
static const uint8_t sps1_bytes[] = {
    0x42, 0x01, 0x01, 0x03, 0x70, 0x00, 0x00, 0x03,
    0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03,
    0x00, 0x1e, 0x48, 0x08, 0x20, 0x41, 0x65, 0x95,
    0x95, 0x29, 0x30, 0xb8, 0x04, 0x00, 0x00, 0x0f,
    0xa0, 0x00, 0x00, 0x0f, 0xa0, 0x20
};

/* PPS with pps_pic_parameter_set_id = 0 */
static const uint8_t pps0_bytes[] = {
    0x44, 0x01, 0xc0, 0x73, 0xc0, 0x89
};

/* PPS with pps_pic_parameter_set_id = 1 */
static const uint8_t pps1_bytes[] = {
    0x44, 0x01, 0x50, 0x1c, 0xf0, 0x22, 0x40
};

/* Prefix SEI (filler payload type 3 to verify SEI NAL retention and ordering) */
static const uint8_t prefix_sei_bytes[] = {
    0x4e, 0x01, 0x03, 0x01, 0xff, 0x80
};

/* Valid 2-byte Prefix SEI header for cap-testing */
static const uint8_t dummy_sei_bytes[] = {
    0x4e, 0x01
};

/* Valid IDR slice referencing PPS 0 */
static const uint8_t idr_slice_bytes[] = {
    0x28, 0x01, 0xac, 0x4e, 0xd7, 0x1f, 0xff, 0xf5,
    0xde, 0x9c, 0xaf, 0xea, 0xf8
};

static const uint8_t hvcc_header[22] = {
    1,             /* configurationVersion = 1 */
    0x01,          /* general_profile_space / tier / profile_idc */
    0x60, 0, 0, 0, /* general_profile_compatibility_flags */
    0x90, 0, 0, 0, 0, 0, /* general_constraint_indicator_flags */
    0x5d,          /* general_level_idc */
    0xf0, 0x00,    /* min_spatial_segmentation_idc (1111 0000) */
    0xfc,          /* parallelismType */
    0xfd,          /* chroma_format_idc (3 = 4:2:0) */
    0xf8,          /* bit_depth_luma_minus8 */
    0xf8,          /* bit_depth_chroma_minus8 */
    0x00, 0x00,    /* avgFrameRate */
    0x03           /* lengthSizeMinusOne = 3 (4-byte length prefix) */
};

typedef struct HVCCNALDesc {
    int            type;
    const uint8_t *data;
    size_t         size;
    int            count;
} HVCCNALDesc;

static void print_packet_hex(const char *label, const AVPacket *pkt)
{
    printf("%s: size %d, hex: ", label, pkt->size);
    for (int i = 0; i < pkt->size; i++)
        printf("%02x", pkt->data[i]);
    printf("\n");
}

/* Helper to build a HEVCDecoderConfigurationRecord (hvcC) from array descriptors */
static uint8_t *build_hvcc(size_t *hvcc_size, const HVCCNALDesc *arrays, int nb_arrays)
{
    size_t total = sizeof(hvcc_header) + 1;
    uint8_t *buf, *p;

    for (int i = 0; i < nb_arrays; i++)
        total += 3 + (size_t)arrays[i].count * (2 + arrays[i].size);

    buf = av_mallocz(total + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!buf)
        return NULL;

    p = buf;
    memcpy(p, hvcc_header, sizeof(hvcc_header));
    p += sizeof(hvcc_header);
    *p++ = nb_arrays;

    for (int i = 0; i < nb_arrays; i++) {
        int cnt = arrays[i].count;
        size_t len = arrays[i].size;

        *p++ = arrays[i].type & 0x3f;
        *p++ = (cnt >> 8) & 0xff;
        *p++ = cnt & 0xff;
        for (int j = 0; j < cnt; j++) {
            *p++ = (len >> 8) & 0xff;
            *p++ = len & 0xff;
            if (arrays[i].data)
                memcpy(p, arrays[i].data, len);
            else
                memset(p, 0x80, len);
            p += len;
        }
    }

    *hvcc_size = p - buf;
    return buf;
}

/* Helper to build a packet with 4-byte length-prefixed NALUs */
static int build_packet(AVPacket *pkt,
                        const uint8_t *nalus[], const size_t lens[], int count)
{
    size_t total = 0;
    uint8_t *p;
    int ret;

    for (int i = 0; i < count; i++)
        total += 4 + lens[i];

    ret = av_new_packet(pkt, total);
    if (ret < 0)
        return ret;

    p = pkt->data;
    for (int i = 0; i < count; i++) {
        AV_WB32(p, lens[i]);
        memcpy(p + 4, nalus[i], lens[i]);
        p += 4 + lens[i];
    }

    return 0;
}

static int decode_packet(const AVPacket *pkt)
{
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    AVCodecContext *dec_ctx = NULL;
    AVFrame *frame = NULL;
    AVPacket *dec_pkt = NULL;
    int ret = 0, got_frame = 0;

    if (!codec) {
        fprintf(stderr, "HEVC decoder not found\n");
        return AVERROR_DECODER_NOT_FOUND;
    }

    dec_ctx = avcodec_alloc_context3(codec);
    frame = av_frame_alloc();
    dec_pkt = av_packet_alloc();
    if (!dec_ctx || !frame || !dec_pkt) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ret = avcodec_open2(dec_ctx, codec, NULL);
    if (ret < 0)
        goto end;

    ret = av_packet_ref(dec_pkt, pkt);
    if (ret < 0)
        goto end;

    ret = avcodec_send_packet(dec_ctx, dec_pkt);
    if (ret < 0)
        goto end;

    while (1) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0)
            goto end;
        got_frame++;
    }

    ret = avcodec_send_packet(dec_ctx, NULL);
    if (ret < 0 && ret != AVERROR_EOF)
        goto end;

    while (1) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR_EOF)
            break;
        if (ret < 0)
            goto end;
        got_frame++;
    }

    printf("Decoded frames: %d\n", got_frame);
    ret = 0;

end:
    av_packet_free(&dec_pkt);
    av_frame_free(&frame);
    avcodec_free_context(&dec_ctx);
    return ret;
}

static int run_test_case(const char *case_name,
                         const uint8_t *hvcc, size_t hvcc_size,
                         AVPacket *in_pkt, int init_only,
                         int check_decode)
{
    const AVBitStreamFilter *bsf;
    AVBSFContext *ctx = NULL;
    AVPacket *out_pkt = NULL;
    int ret = 0;

    printf("=== Test: %s ===\n", case_name);

    bsf = av_bsf_get_by_name("hevc_mp4toannexb");
    if (!bsf) {
        fprintf(stderr, "Bitstream filter hevc_mp4toannexb not found\n");
        return -1;
    }

    ret = av_bsf_alloc(bsf, &ctx);
    if (ret < 0)
        return ret;

    ctx->par_in->codec_type = AVMEDIA_TYPE_VIDEO;
    ctx->par_in->codec_id   = AV_CODEC_ID_HEVC;
    ctx->par_in->extradata  = av_memdup(hvcc, hvcc_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!ctx->par_in->extradata) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    ctx->par_in->extradata_size = hvcc_size;

    ret = av_bsf_init(ctx);
    if (init_only) {
        printf("av_bsf_init: %d\n", ret);
        ret = 0;
        goto end;
    } else if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "av_bsf_init failed: %s\n", errbuf);
        goto end;
    }

    out_pkt = av_packet_alloc();
    if (!out_pkt) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ret = av_bsf_send_packet(ctx, in_pkt);
    if (ret < 0) {
        fprintf(stderr, "av_bsf_send_packet failed: %d\n", ret);
        goto end;
    }

    ret = av_bsf_receive_packet(ctx, out_pkt);
    if (ret < 0) {
        fprintf(stderr, "av_bsf_receive_packet failed: %d\n", ret);
        goto end;
    }

    print_packet_hex("Output", out_pkt);

    if (check_decode) {
        ret = decode_packet(out_pkt);
        if (ret < 0) {
            fprintf(stderr, "%s: decoding output packet failed\n", case_name);
            goto end;
        }
        /* The decoder prints the actual frame count for FATE. */
    }

    ret = 0;

end:
    av_packet_free(&out_pkt);
    av_bsf_free(&ctx);
    return ret;
}

int main(void)
{
    uint8_t *hvcc_std = NULL, *hvcc_sei = NULL, *hvcc_dup = NULL, *hvcc_limit = NULL;
    size_t sz_std = 0, sz_sei = 0, sz_dup = 0, sz_limit = 0;
    AVPacket *pkt = av_packet_alloc();
    int ret = 0, err;

    const HVCCNALDesc std_arrays[] = {
        { 32, vps_bytes,  sizeof(vps_bytes),  1 },
        { 33, sps_bytes,  sizeof(sps_bytes),  1 },
        { 34, pps0_bytes, sizeof(pps0_bytes), 1 },
    };
    const HVCCNALDesc sei_arrays[] = {
        { 39, prefix_sei_bytes, sizeof(prefix_sei_bytes), 1 },
        { 32, vps_bytes,        sizeof(vps_bytes),        1 },
        { 33, sps_bytes,        sizeof(sps_bytes),        1 },
        { 34, pps0_bytes,       sizeof(pps0_bytes),       1 },
    };
    const HVCCNALDesc dup_arrays[] = {
        { 32, vps_bytes,  sizeof(vps_bytes),  1 },
        { 33, sps_bytes,  sizeof(sps_bytes),  2 },
        { 34, pps0_bytes, sizeof(pps0_bytes), 1 },
    };
    const HVCCNALDesc limit_arrays[] = {
        { 39, dummy_sei_bytes, sizeof(dummy_sei_bytes), 2049 },
    };

    if (!pkt)
        return 1;

    hvcc_std   = build_hvcc(&sz_std,   std_arrays,   FF_ARRAY_ELEMS(std_arrays));
    hvcc_sei   = build_hvcc(&sz_sei,   sei_arrays,   FF_ARRAY_ELEMS(sei_arrays));
    hvcc_dup   = build_hvcc(&sz_dup,   dup_arrays,   FF_ARRAY_ELEMS(dup_arrays));
    hvcc_limit = build_hvcc(&sz_limit, limit_arrays, FF_ARRAY_ELEMS(limit_arrays));

    if (!hvcc_std || !hvcc_sei || !hvcc_dup || !hvcc_limit) {
        ret = 1;
        goto cleanup;
    }

    /* Case 1: Standard insertion (extradata has VPS, SPS, PPS 0; packet only has IDR slice) */
    {
        const uint8_t *nalus[] = { idr_slice_bytes };
        const size_t lens[]    = { sizeof(idr_slice_bytes) };
        if (build_packet(pkt, nalus, lens, FF_ARRAY_ELEMS(nalus)) < 0) {
            ret = 1;
            goto cleanup;
        }
        err = run_test_case("Case 1 (Standard Insertion)", hvcc_std, sz_std, pkt, 0, 1);
        if (err < 0)
            ret = 1;
        av_packet_unref(pkt);
    }

    /* Case 2: Exact payload deduplication (extradata has VPS, SPS, PPS 0; packet has identical in-band VPS, SPS, PPS 0 + IDR slice) */
    {
        const uint8_t *nalus[] = { vps_bytes, sps_bytes, pps0_bytes, idr_slice_bytes };
        const size_t lens[]    = { sizeof(vps_bytes), sizeof(sps_bytes), sizeof(pps0_bytes), sizeof(idr_slice_bytes) };
        if (build_packet(pkt, nalus, lens, FF_ARRAY_ELEMS(nalus)) < 0) {
            ret = 1;
            goto cleanup;
        }
        err = run_test_case("Case 2 (Exact Payload Deduplication)", hvcc_std, sz_std, pkt, 0, 1);
        if (err < 0)
            ret = 1;
        av_packet_unref(pkt);
    }

    /* Case 3: Reviewer Fairy counterexample (extradata has VPS, SPS, PPS 0; packet has in-band VPS, SPS, PPS 1 + IDR slice) */
    {
        const uint8_t *nalus[] = { vps_bytes, sps_bytes, pps1_bytes, idr_slice_bytes };
        const size_t lens[]    = { sizeof(vps_bytes), sizeof(sps_bytes), sizeof(pps1_bytes), sizeof(idr_slice_bytes) };
        if (build_packet(pkt, nalus, lens, FF_ARRAY_ELEMS(nalus)) < 0) {
            ret = 1;
            goto cleanup;
        }
        err = run_test_case("Case 3 (Fairy Counterexample - Retain PPS 0)", hvcc_std, sz_std, pkt, 0, 1);
        if (err < 0)
            ret = 1;
        av_packet_unref(pkt);
    }

    /* Case 4: Filler SEI preservation (extradata has Prefix SEI, VPS, SPS, PPS 0; packet has identical VPS, SPS, PPS 0 + IDR slice) */
    {
        const uint8_t *nalus[] = { vps_bytes, sps_bytes, pps0_bytes, idr_slice_bytes };
        const size_t lens[]    = { sizeof(vps_bytes), sizeof(sps_bytes), sizeof(pps0_bytes), sizeof(idr_slice_bytes) };
        if (build_packet(pkt, nalus, lens, FF_ARRAY_ELEMS(nalus)) < 0) {
            ret = 1;
            goto cleanup;
        }
        err = run_test_case("Case 4 (Filler SEI Preservation)", hvcc_sei, sz_sei, pkt, 0, 1);
        if (err < 0)
            ret = 1;
        av_packet_unref(pkt);
    }

    /* Case 5: Many nonmatching in-band PPS copies before a matching PPS */
    {
        const uint8_t *nalus[36];
        size_t lens[36];
        int count = 0;

        nalus[count] = vps_bytes;
        lens[count++] = sizeof(vps_bytes);

        nalus[count] = sps_bytes;
        lens[count++] = sizeof(sps_bytes);

        for (int k = 0; k < 32; k++) {
            nalus[count] = pps1_bytes;
            lens[count++] = sizeof(pps1_bytes);
        }

        nalus[count] = pps0_bytes;
        lens[count++] = sizeof(pps0_bytes);

        nalus[count] = idr_slice_bytes;
        lens[count++] = sizeof(idr_slice_bytes);

        if (build_packet(pkt, nalus, lens, count) < 0) {
            ret = 1;
            goto cleanup;
        }
        err = run_test_case("Case 5 (Many Nonmatching In-band PPS Copies)", hvcc_std, sz_std, pkt, 0, 1);
        if (err < 0)
            ret = 1;
        av_packet_unref(pkt);
    }

    /* Case 6: In-band parameter set suppresses duplicate extradata entries
     * (extradata has 1xVPS, 2xSPS, 1xPPS 0; packet has 1xVPS, 1xSPS, 1xPPS 0 + IDR slice).
     * Verifies that matching in-band parameter sets suppress all identical
     * extradata copies, leaving only the in-band entry.
     */
    {
        const uint8_t *nalus[] = { vps_bytes, sps_bytes, pps0_bytes, idr_slice_bytes };
        const size_t lens[]    = { sizeof(vps_bytes), sizeof(sps_bytes), sizeof(pps0_bytes), sizeof(idr_slice_bytes) };
        if (build_packet(pkt, nalus, lens, FF_ARRAY_ELEMS(nalus)) < 0) {
            ret = 1;
            goto cleanup;
        }
        err = run_test_case("Case 6 (Duplicate Extradata PS Suppressed By In-Band Match)",
                            hvcc_dup, sz_dup, pkt, 0, 1);
        if (err < 0)
            ret = 1;
        av_packet_unref(pkt);
    }

    /* Case 7: Exceeds MAX_EXTRADATA_NALS limit rejection (hvcC carries >2048 NALUs) */
    err = run_test_case("Case 7 (Exceeds MAX_EXTRADATA_NALS Limit Rejection)",
                        hvcc_limit, sz_limit, NULL, 1, 0);
    if (err < 0)
        ret = 1;

    /* Case 8: Leading Prefix SEI before matching in-band VPS and SPS
     * (extradata has VPS, SPS, PPS 0; packet has Prefix SEI, matching VPS, matching SPS + IDR slice).
     * Verifies that extradata PPS 0 is not prematurely emitted before matching VPS/SPS.
     */
    {
        const uint8_t *nalus[] = { prefix_sei_bytes, vps_bytes, sps_bytes, idr_slice_bytes };
        const size_t lens[]    = { sizeof(prefix_sei_bytes), sizeof(vps_bytes), sizeof(sps_bytes), sizeof(idr_slice_bytes) };
        if (build_packet(pkt, nalus, lens, FF_ARRAY_ELEMS(nalus)) < 0) {
            ret = 1;
            goto cleanup;
        }
        err = run_test_case("Case 8 (Leading Prefix SEI Before Matching VPS/SPS)",
                            hvcc_std, sz_std, pkt, 0, 1);
        if (err < 0)
            ret = 1;
        av_packet_unref(pkt);
    }

    /* Case 9: In-band PPS before SPS reordering
     * (extradata has VPS, SPS, PPS 0; packet has in-band PPS 0, matching SPS + IDR slice).
     * Verifies that out-of-order in-band SPS does not suppress extradata SPS needed by earlier PPS.
     */
    {
        const uint8_t *nalus[] = { pps0_bytes, sps_bytes, idr_slice_bytes };
        const size_t lens[]    = { sizeof(pps0_bytes), sizeof(sps_bytes), sizeof(idr_slice_bytes) };
        if (build_packet(pkt, nalus, lens, FF_ARRAY_ELEMS(nalus)) < 0) {
            ret = 1;
            goto cleanup;
        }
        err = run_test_case("Case 9 (In-band PPS Before SPS Reordering)",
                            hvcc_std, sz_std, pkt, 0, 1);
        if (err < 0)
            ret = 1;
        av_packet_unref(pkt);
    }

    /* Case 10: Leading Prefix SEI with only VPS matched
     * (extradata has VPS 0, SPS 0, PPS 0; packet has Prefix SEI, matching VPS 0 + IDR slice).
     * Verifies that extradata SPS 0 and PPS 0 are not prematurely emitted before
     * in-band VPS 0 due to the Prefix SEI barrier, ensuring valid decoding topology.
     */
    {
        const uint8_t *nalus[] = { prefix_sei_bytes, vps_bytes, idr_slice_bytes };
        const size_t lens[]    = { sizeof(prefix_sei_bytes), sizeof(vps_bytes), sizeof(idr_slice_bytes) };
        if (build_packet(pkt, nalus, lens, FF_ARRAY_ELEMS(nalus)) < 0) {
            ret = 1;
            goto cleanup;
        }
        err = run_test_case("Case 10 (Leading Prefix SEI With Matching VPS Only)",
                            hvcc_std, sz_std, pkt, 0, 1);
        if (err < 0)
            ret = 1;
        av_packet_unref(pkt);
    }

    /* Case 11: Multiple SPS IDs across Prefix SEI
     * (extradata has VPS 0, SPS 0, PPS 0; packet has matching VPS 0, in-band SPS 1,
     * Prefix SEI, matching SPS 0 + IDR slice).
     * Verifies that seeing SPS 1 before Prefix SEI does not prematurely emit extradata PPS 0
     * which depends on SPS 0 that appears after the SEI.
     */
    {
        const uint8_t *nalus[] = { vps_bytes, sps1_bytes, prefix_sei_bytes, sps_bytes, idr_slice_bytes };
        const size_t lens[]    = { sizeof(vps_bytes), sizeof(sps1_bytes), sizeof(prefix_sei_bytes),
                                   sizeof(sps_bytes), sizeof(idr_slice_bytes) };
        if (build_packet(pkt, nalus, lens, FF_ARRAY_ELEMS(nalus)) < 0) {
            ret = 1;
            goto cleanup;
        }
        err = run_test_case("Case 11 (Multiple SPS IDs Across Prefix SEI)",
                            hvcc_std, sz_std, pkt, 0, 1);
        if (err < 0)
            ret = 1;
        av_packet_unref(pkt);
    }

cleanup:
    av_freep(&hvcc_std);
    av_freep(&hvcc_sei);
    av_freep(&hvcc_dup);
    av_freep(&hvcc_limit);
    av_packet_free(&pkt);
    return ret;
}
