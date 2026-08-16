/*
 * KTX 1.0 demuxer for ASTC
 * Copyright (c) 2026 Jun Zhao
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
 * KTX 1.0 demuxer for ASTC textures.
 *
 * Parses the KTX 1.0 header, recovers the ASTC block size from the GL
 * internal format enum, and synthesizes a 16-byte .astc-style extradata
 * for the decoder. The raw ASTC bitstream is emitted as a single packet.
 */

#include "config_components.h"

#include <inttypes.h>
#include <limits.h>

#include "libavcodec/defs.h"
#include "avformat.h"
#include "avio.h"
#include "avio_internal.h"
#include "demux.h"
#include "internal.h"
#include "libavutil/display.h"
#include "libavutil/intreadwrite.h"

#define ASTC_HEADER_SIZE 16
static const uint8_t astc_magic[4] = { 0x13, 0xAB, 0xA1, 0x5C };
static const uint8_t ktx_magic[12] = {
    0xAB, 'K', 'T', 'X', ' ', '1', '1', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
};

/* ASTC GL internal format enums (2D), linear base 0x93B0, sRGB = +0x20. */
static const int astc_gl_linear[14] = {
    0x93B0, 0x93B1, 0x93B2, 0x93B3, 0x93B4, 0x93B5, 0x93B6, 0x93B7,
    0x93B8, 0x93B9, 0x93BA, 0x93BB, 0x93BC, 0x93BD
};
static const int astc_bx[14] = { 4, 5, 5, 6, 6, 8, 8, 8, 10, 10, 10, 10, 12, 12 };
static const int astc_by[14] = { 4, 4, 5, 5, 6, 5, 6, 8, 5, 6, 8, 10, 10, 12 };

typedef struct KTXDemuxerContext {
    uint32_t image_size;
    int      truncated;
} KTXDemuxerContext;

/* Map a GL internal format enum to a 2D ASTC block size. Returns 1 on success. */
static int gl_enum_to_block(uint32_t e, int *bx, int *by)
{
    int base = (e >= 0x93D0 && e <= 0x93DD) ? (e - 0x20) : e;
    for (int i = 0; i < 14; i++) {
        if (astc_gl_linear[i] == (int)base) {
            *bx = astc_bx[i];
            *by = astc_by[i];
            return 1;
        }
    }
    return 0;
}

static int ktx_probe(const AVProbeData *p)
{
    int bx, by;

    if (p->buf_size < 32 || memcmp(p->buf, ktx_magic, sizeof(ktx_magic)))
        return 0;

    /* KTX 1.0 carries ETC, uncompressed and other payloads this demuxer cannot
     * read, so only claim the file once glInternalFormat is an ASTC format we
     * actually support. */
    if (!gl_enum_to_block(AV_RL32(p->buf + 28), &bx, &by))
        return 0;

    /* Just below the maximum: this is a KTX file, but only the ASTC subset of
     * the container is implemented here. */
    return AVPROBE_SCORE_MAX - 1;
}

/* Map a KTXorientation value such as "S=r,T=d" onto the flips needed to
 * bring the stored rows back to the top-down, left-to-right order the
 * decoder produces. The R component is irrelevant for 2D textures. */
static void ktx_orientation_flips(const char *value, int *hflip, int *vflip)
{
    const char *p = value;

    while (p && *p) {
        if (p[0] == 'S' && p[1] == '=' && p[2])
            *hflip = p[2] == 'l';
        else if (p[0] == 'T' && p[1] == '=' && p[2])
            *vflip = p[2] == 'u';
        p = strchr(p, ',');
        if (p)
            p++;
    }
}

/* Walk the key/value block, picking up KTXorientation. Unknown or oversized
 * entries are skipped rather than buffered wholesale. */
static int ktx_read_orientation(AVFormatContext *s, uint32_t kvdata,
                                int *hflip, int *vflip)
{
    AVIOContext *pb = s->pb;
    uint8_t buf[256];
    uint32_t left = kvdata;

    *hflip = *vflip = 0;

    while (left >= 4) {
        uint32_t entry = avio_rl32(pb);
        uint32_t padded = (entry + 3) & ~3u;

        left -= 4;
        if (!entry || entry > left || padded > left) {
            av_log(s, AV_LOG_ERROR, "Invalid KTX key/value data.\n");
            return AVERROR_INVALIDDATA;
        }

        if (entry >= sizeof(buf)) {
            /* Nothing interesting is this large. */
            if (avio_skip(pb, padded) < 0)
                return AVERROR_INVALIDDATA;
        } else {
            const char *key, *value;

            if (ffio_read_size(pb, buf, entry) < 0)
                return AVERROR_INVALIDDATA;
            buf[entry] = 0;

            key = (const char *)buf;
            value = key + strlen(key) + 1;
            if (value < (const char *)buf + entry) {
                if (!strcmp(key, "KTXorientation"))
                    ktx_orientation_flips(value, hflip, vflip);
                else
                    av_log(s, AV_LOG_VERBOSE, "Ignoring KTX key '%s'.\n", key);
            }
            if (avio_skip(pb, padded - entry) < 0)
                return AVERROR_INVALIDDATA;
        }
        left -= padded;
    }

    /* Every entry occupies a padded, four byte aligned slot, so a leftover
     * tail means the declared block size is not aligned and the image payload
     * does not start where the header claims it does. */
    if (left) {
        av_log(s, AV_LOG_ERROR, "Invalid KTX key/value data.\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int ktx_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    KTXDemuxerContext *ktx = s->priv_data;
    AVStream *st;
    uint8_t hdr[64];
    uint32_t endian, gl_type, gl_type_size, gl_format, gl_internal;
    uint32_t gl_base_internal, w, h, depth, array, faces, mips;
    uint32_t kvdata, image_size;
    uint8_t image_size_buf[4];
    int bx = 0, by = 0;
    int hflip = 0, vflip = 0;
    uint8_t extra[16];

    if (ffio_read_size(pb, hdr, sizeof(hdr)) < 0)
        return AVERROR_INVALIDDATA;
    if (memcmp(hdr, ktx_magic, sizeof(ktx_magic))) {
        av_log(s, AV_LOG_ERROR, "Not a KTX file (bad magic).\n");
        return AVERROR_INVALIDDATA;
    }

    endian          = AV_RL32(hdr + 12);
    gl_type         = AV_RL32(hdr + 16);
    gl_type_size    = AV_RL32(hdr + 20);
    gl_format       = AV_RL32(hdr + 24);
    gl_internal     = AV_RL32(hdr + 28);
    gl_base_internal = AV_RL32(hdr + 32);
    w               = AV_RL32(hdr + 36);
    h               = AV_RL32(hdr + 40);
    depth           = AV_RL32(hdr + 44);
    array           = AV_RL32(hdr + 48);
    faces           = AV_RL32(hdr + 52);
    mips            = AV_RL32(hdr + 56);
    kvdata          = AV_RL32(hdr + 60);

    if (endian != 0x04030201) {
        av_log(s, AV_LOG_ERROR, "KTX big-endian / unknown endianness not supported.\n");
        return AVERROR_INVALIDDATA;
    }
    if (gl_type || gl_type_size != 1 || gl_format || gl_base_internal != 0x1908 ||
        !w || !h || w > 0xFFFFFF || h > 0xFFFFFF ||
        depth || array || faces != 1 || mips != 1) {
        av_log(s, AV_LOG_ERROR, "Unsupported KTX texture layout.\n");
        return AVERROR_INVALIDDATA;
    }

    if (!gl_enum_to_block(gl_internal, &bx, &by)) {
        av_log(s, AV_LOG_ERROR, "Unsupported ASTC GL format 0x%X in KTX.\n", gl_internal);
        return AVERROR_INVALIDDATA;
    }

    if (ktx_read_orientation(s, kvdata, &hflip, &vflip) < 0)
        return AVERROR_INVALIDDATA;
    if (ffio_read_size(pb, image_size_buf, sizeof(image_size_buf)) < 0)
        return AVERROR_INVALIDDATA;
    image_size = AV_RL32(image_size_buf);
    if (!image_size || image_size > INT_MAX)
        return AVERROR_INVALIDDATA;
    /* The payload must hold exactly the blocks the texture geometry needs;
     * anything else would either truncate the image or smuggle in trailing
     * data that the decoder would ignore. */
    {
        uint64_t expected = (((uint64_t)w + bx - 1) / bx) *
                            (((uint64_t)h + by - 1) / by) * 16;
        if (image_size != expected) {
            av_log(s, AV_LOG_ERROR,
                   "KTX image size %u does not match the %"PRIu64" bytes "
                   "required for a %ux%u texture with %dx%d blocks.\n",
                   image_size, expected, w, h, bx, by);
            return AVERROR_INVALIDDATA;
        }
    }
    ktx->image_size = image_size;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_ASTC;
    st->codecpar->width      = w;
    st->codecpar->height     = h;
    /* The GL internal format fixes the colour space but not the endpoint
     * format: an sRGB texture is LDR, while a linear one may hold LDR, HDR RGB
     * with LDR alpha, or full HDR endpoints. Report that distinction so the
     * decoder does not assume LDR for a linear texture; a linear texture is
     * sampled with HDR precision by default (LINEAR_ANY), so the stream format
     * is the matching half-float one rather than 8-bit RGBA. */
    st->codecpar->profile    = gl_internal >= 0x93D0 && gl_internal <= 0x93DD ?
                               AV_PROFILE_ASTC_LDR_SRGB : AV_PROFILE_ASTC_LINEAR_ANY;
    st->codecpar->format     = st->codecpar->profile == AV_PROFILE_ASTC_LDR_SRGB ?
                               AV_PIX_FMT_RGBA : AV_PIX_FMT_RGBAF16;

    /* Synthesize the .astc-style extradata for the decoder. Bytes [13-15] hold
     * the image depth (dim_z), which is always 1 here: KTX 1.0 2D only. */
    memset(extra, 0, sizeof(extra));
    memcpy(extra, astc_magic, sizeof(astc_magic));
    extra[4] = (uint8_t)bx;
    extra[5] = (uint8_t)by;
    extra[6] = 1;
    AV_WL24(extra + 7,  w);
    AV_WL24(extra + 10, h);
    extra[13] = 1;
    if (ff_alloc_extradata(st->codecpar, ASTC_HEADER_SIZE) < 0)
        return AVERROR(ENOMEM);
    memcpy(st->codecpar->extradata, extra, ASTC_HEADER_SIZE);

    /* Carry the stored row order to the caller instead of silently returning
     * a mirrored image. */
    if (hflip || vflip) {
        AVPacketSideData *sd;
        int32_t *matrix;

        sd = av_packet_side_data_new(&st->codecpar->coded_side_data,
                                     &st->codecpar->nb_coded_side_data,
                                     AV_PKT_DATA_DISPLAYMATRIX,
                                     9 * sizeof(*matrix), 0);
        if (!sd)
            return AVERROR(ENOMEM);
        matrix = (int32_t *)sd->data;
        av_display_rotation_set(matrix, 0);
        av_display_matrix_flip(matrix, hflip, vflip);
        av_log(s, AV_LOG_VERBOSE,
               "Applying KTXorientation (hflip %d, vflip %d).\n", hflip, vflip);
    }

    avpriv_set_pts_info(st, 64, 1, 1);
    return 0;
}

static int ktx_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    KTXDemuxerContext *ktx = s->priv_data;
    int ret;

    /* Keep reporting a detected truncation instead of falling through to EOF:
     * a caller that stops at the first error must not see the broken image as
     * a clean end of stream. */
    if (ktx->truncated)
        return AVERROR_INVALIDDATA;

    if (!ktx->image_size)
        return AVERROR_EOF;

    /* A payload that is missing entirely reads as EOF, which has to be
     * reported as the truncation it is rather than as the end of a valid
     * stream. */
    ret = av_get_packet(s->pb, pkt, ktx->image_size);
    if (ret == AVERROR_EOF)
        ret = 0;
    if (ret < 0)
        return ret;
    if (ret != ktx->image_size) {
        av_log(s, AV_LOG_ERROR,
               "Truncated KTX image: got %d of %u bytes.\n",
               ret, ktx->image_size);
        av_packet_unref(pkt);
        ktx->truncated = 1;
        return AVERROR_INVALIDDATA;
    }

    pkt->stream_index = 0;
    pkt->flags |= AV_PKT_FLAG_KEY;
    ktx->image_size = 0; /* single mip */
    return 0;
}

const FFInputFormat ff_ktx_demuxer = {
    .p.name         = "ktx",
    .p.long_name    = NULL_IF_CONFIG_SMALL("KTX 1.0 (Khronos Texture) for ASTC"),
    .p.mime_type    = "image/ktx",
    .p.extensions   = "ktx",
    .p.flags        = AVFMT_NOTIMESTAMPS,
    .priv_data_size = sizeof(KTXDemuxerContext),
    .read_probe     = ktx_probe,
    .read_header    = ktx_read_header,
    .read_packet    = ktx_read_packet,
};
