/*
 * KTX 1.0 muxer for ASTC
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
 * KTX 1.0 muxer for ASTC textures.
 *
 * Writes the KTX 1.0 header (64 bytes) followed by a uint32 image size and
 * the raw ASTC bitstream. The block size is recovered from the encoder's
 * .astc-style extradata; the sRGB vs linear GL enum is chosen by the
 * "srgb" muxer option.
 */

#include "avformat.h"
#include "avio.h"
#include "mux.h"
#include "libavcodec/defs.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"

#define ASTC_HEADER_SIZE 16
static const uint8_t astc_magic[4] = { 0x13, 0xAB, 0xA1, 0x5C };

/* ASTC GL internal format enums (2D), linear base 0x93B0, sRGB = +0x20.
 * Order matches the astc-encoder ASTC_FORMATS table. */
static const int astc_gl_linear[14] = {
    0x93B0, 0x93B1, 0x93B2, 0x93B3, 0x93B4, 0x93B5, 0x93B6, 0x93B7,
    0x93B8, 0x93B9, 0x93BA, 0x93BB, 0x93BC, 0x93BD
};
static const int astc_gl_srgb[14] = {
    0x93D0, 0x93D1, 0x93D2, 0x93D3, 0x93D4, 0x93D5, 0x93D6, 0x93D7,
    0x93D8, 0x93D9, 0x93DA, 0x93DB, 0x93DC, 0x93DD
};
/* Block sizes paired with the enum tables above. */
static const int astc_bx[14] = { 4, 5, 5, 6, 6, 8, 8, 8, 10, 10, 10, 10, 12, 12 };
static const int astc_by[14] = { 4, 4, 5, 5, 6, 5, 6, 8, 5, 6, 8, 10, 10, 12 };

typedef struct KTXMuxerContext {
    AVClass *class;
    int srgb;        /* -1 = auto (follow the encoder profile) */
    int wrote_image;
} KTXMuxerContext;

static const char ktx_orientation_key[] = "KTXorientation";

/* Derive the stored row order from the stream's display matrix. The demuxer
 * converts KTXorientation into exactly such a matrix, so a stream copied from
 * a flipped KTX keeps its orientation instead of being relabelled. Any other
 * transform is rejected rather than silently dropped: KTXorientation only has
 * room for axis flips, and applying a rotation or a scale to the pixels would
 * mean decoding and re-encoding the texture. */
static int ktx_orientation_from_side_data(AVFormatContext *s, AVStream *st,
                                          int *hflip, int *vflip)
{
    const AVPacketSideData *sd;
    const int32_t *matrix;

    *hflip = *vflip = 0;
    sd = av_packet_side_data_get(st->codecpar->coded_side_data,
                                 st->codecpar->nb_coded_side_data,
                                 AV_PKT_DATA_DISPLAYMATRIX);
    if (!sd || sd->size < 9 * sizeof(*matrix))
        return 0;

    matrix = (const int32_t *)sd->data;
    /* The layout av_display_matrix_flip() produces: the 2x2 part (a, b, c, d)
     * is diagonal with unit scales, negative when the axis is flipped, the
     * translation (x, y) is zero, and the perspective terms (u, v, w) are the
     * identity ones. Note that a, b, c, d, x and y are 16.16 fixed-point while
     * u, v and w are 2.30. */
    if (matrix[1] || matrix[2] || matrix[3] || matrix[5] ||
        matrix[6] || matrix[7] || matrix[8] != 1 << 30 ||
        FFABSU(matrix[0]) != 65536 || FFABSU(matrix[4]) != 65536) {
        av_log(s, AV_LOG_ERROR, "Unsupported display matrix: KTXorientation "
               "only records axis flips, not rotations, scales, translations "
               "or perspective.\n");
        return AVERROR(EINVAL);
    }

    *hflip = matrix[0] < 0;
    *vflip = matrix[4] < 0;
    return 0;
}

static int ktx_write_header(AVFormatContext *s)
{
    KTXMuxerContext *ctx = s->priv_data;
    AVStream *st = s->streams[0];
    const uint8_t *ed = st->codecpar->extradata;
    uint8_t bx = 0, by = 0;
    int glfmt = 0, i, srgb, profile_srgb, ret;
    int hflip, vflip, kv_size, kv_padded;
    uint8_t hdr[64];
    char kv[32];
    char orientation[8];

    if (st->codecpar->extradata_size < ASTC_HEADER_SIZE) {
        av_log(s, AV_LOG_ERROR, ".ktx muxer requires 16-byte extradata "
               "(block size) from the encoder.\n");
        return AVERROR(EINVAL);
    }
    if (st->codecpar->width <= 0 || st->codecpar->height <= 0 ||
        st->codecpar->width > 0xFFFFFF || st->codecpar->height > 0xFFFFFF) {
        av_log(s, AV_LOG_ERROR, "Invalid image dimensions %dx%d.\n",
               st->codecpar->width, st->codecpar->height);
        return AVERROR(EINVAL);
    }
    if (memcmp(ed, astc_magic, sizeof(astc_magic)) ||
        AV_RL24(ed + 7) != st->codecpar->width ||
        AV_RL24(ed + 10) != st->codecpar->height ||
        AV_RL24(ed + 13) != 1) {
        av_log(s, AV_LOG_ERROR, ".ktx muxer received invalid ASTC extradata.\n");
        return AVERROR_INVALIDDATA;
    }
    bx = ed[4];
    by = ed[5];

    /* ASTC bitstreams do not record whether they hold LDR or HDR endpoints,
     * and the KTX 1.0 GL enums do not express it either: the linear ASTC
     * formats can also carry HDR endpoint encodings. Writing HDR through this
     * muxer is simply not implemented, so reject the profiles that name an HDR
     * endpoint format and point at the .astc container. A stream that only
     * reports a linear colour space is not known to be HDR and is still
     * written as a linear texture. */
    if (st->codecpar->profile == AV_PROFILE_ASTC_HDR_RGB_LDR_A ||
        st->codecpar->profile == AV_PROFILE_ASTC_HDR) {
        av_log(s, AV_LOG_ERROR,
               "Writing HDR ASTC to KTX 1.0 is not supported; use the .astc "
               "container for HDR output.\n");
        return AVERROR(EINVAL);
    }
    /* codecpar->profile publishes the colour space (and, when it is known, the
     * endpoint format) of the stream. Only the profiles that name a colour
     * space determine the GL enum here; a stream without profile information
     * leaves it undetermined. */
    switch (st->codecpar->profile) {
    case AV_PROFILE_ASTC_LDR_SRGB:
        profile_srgb = 1;
        break;
    case AV_PROFILE_ASTC_LDR:
    case AV_PROFILE_ASTC_LINEAR_ANY:
        profile_srgb = 0;
        break;
    default:
        profile_srgb = -1;
        break;
    }

    /* Auto keeps the color space the encoder was configured with, so that
     * remuxing a linear KTX does not silently flip it to sRGB; a stream
     * without profile information falls back to the sRGB ASTC format. */
    srgb = ctx->srgb >= 0 ? ctx->srgb :
           profile_srgb >= 0 ? profile_srgb : 1;

    /* An explicit request must not contradict a known encoder profile. */
    if (ctx->srgb >= 0 && profile_srgb >= 0 && profile_srgb != ctx->srgb) {
        av_log(s, AV_LOG_ERROR,
               "KTX srgb option does not match the encoder profile.\n");
        return AVERROR(EINVAL);
    }

    /* Writing 3D ASTC blocks is not implemented: only the 2D GL internal
     * formats are emitted. */
    if (ed[6] != 1) {
        av_log(s, AV_LOG_ERROR,
               "Writing 3D ASTC blocks to KTX is not supported; use the .astc "
               "container for 3D block output.\n");
        return AVERROR(EINVAL);
    }

    for (i = 0; i < 14; i++) {
        if (astc_bx[i] == bx && astc_by[i] == by) {
            glfmt = srgb ? astc_gl_srgb[i] : astc_gl_linear[i];
            break;
        }
    }
    if (!glfmt) {
        av_log(s, AV_LOG_ERROR, "Unsupported ASTC block size %dx%d for KTX.\n", bx, by);
        return AVERROR(EINVAL);
    }

    /* The stored rows are passed through unchanged, so describe them as they
     * are: the encoder writes them top-down, left-to-right, unless the input
     * brings its own orientation from a previous KTX. */
    ret = ktx_orientation_from_side_data(s, st, &hflip, &vflip);
    if (ret < 0)
        return ret;
    memcpy(orientation, "S=r,T=d", sizeof(orientation));
    orientation[2] = hflip ? 'l' : 'r';
    orientation[6] = vflip ? 'u' : 'd';

    memcpy(kv, ktx_orientation_key, sizeof(ktx_orientation_key));
    memcpy(kv + sizeof(ktx_orientation_key), orientation, sizeof(orientation));
    kv_size   = sizeof(ktx_orientation_key) + sizeof(orientation);
    kv_padded = (kv_size + 3) & ~3;

    memset(hdr, 0, sizeof(hdr));
    /* KTX 1.0 file identifier: 0xAB 'K' 'T' 'X' ' ' '1' '1' 0xBB 0x0D 0x0A 0x1A 0x0A */
    hdr[0] = 0xAB; hdr[1] = 'K'; hdr[2] = 'T'; hdr[3] = 'X';
    hdr[4] = ' ';  hdr[5] = '1'; hdr[6] = '1'; hdr[7] = 0xBB;
    hdr[8] = 0x0D; hdr[9] = 0x0A; hdr[10] = 0x1A; hdr[11] = 0x0A;
    AV_WL32(hdr + 12, 0x04030201);  /* endianness */
    /* gl_type(16)=0, gl_type_size(20)=1, gl_format(24)=0 */
    AV_WL32(hdr + 20, 1);
    AV_WL32(hdr + 28, glfmt);       /* gl_internal_format */
    AV_WL32(hdr + 32, 0x1908);      /* gl_base_internal_format = GL_RGBA */
    AV_WL32(hdr + 36, st->codecpar->width);   /* pixel_width */
    AV_WL32(hdr + 40, st->codecpar->height);  /* pixel_height */
    /* pixel_depth(44)=0, array(48)=0 */
    AV_WL32(hdr + 52, 1); /* number_of_faces */
    AV_WL32(hdr + 56, 1); /* number_of_mipmap_levels */
    AV_WL32(hdr + 60, 4 + kv_padded); /* bytes_of_key_value_data */

    avio_write(s->pb, hdr, sizeof(hdr));

    /* Self-describe the row convention so readers do not have to guess
     * whether the texture is stored top-down or bottom-up. */
    avio_wl32(s->pb, kv_size);
    avio_write(s->pb, kv, kv_size);
    for (i = kv_size; i % 4; i++)
        avio_w8(s->pb, 0);

    return 0;
}

static int ktx_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    KTXMuxerContext *ctx = s->priv_data;

    /* The container holds a single image; FF_OFMT_FLAG_MAX_ONE_OF_EACH only
     * limits the number of streams, so packets must be rejected here. */
    if (ctx->wrote_image) {
        av_log(s, AV_LOG_ERROR,
               ".ktx muxer supports a single image per file.\n");
        return AVERROR(EINVAL);
    }
    ctx->wrote_image = 1;

    avio_wl32(s->pb, pkt->size);     /* imageSize */
    avio_write(s->pb, pkt->data, pkt->size);
    return 0;
}

#define OFFSET(x) offsetof(KTXMuxerContext, x)
#define VE AV_OPT_FLAG_ENCODING_PARAM
static const AVOption ktx_options[] = {
    { "srgb", "GL internal format color space", OFFSET(srgb),
      AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 1, VE, .unit = "srgb" },
    { "auto",   "Follow the encoder profile",   0, AV_OPT_TYPE_CONST, { .i64 = -1 }, 0, 0, VE, .unit = "srgb" },
    { "linear", "Write the linear ASTC format", 0, AV_OPT_TYPE_CONST, { .i64 =  0 }, 0, 0, VE, .unit = "srgb" },
    { "srgb",   "Write the sRGB ASTC format",   0, AV_OPT_TYPE_CONST, { .i64 =  1 }, 0, 0, VE, .unit = "srgb" },
    { NULL },
};

static const AVClass ktx_muxer_class = {
    .class_name = "ktx_muxer",
    .item_name  = av_default_item_name,
    .option     = ktx_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFOutputFormat ff_ktx_muxer = {
    .p.name         = "ktx",
    .p.long_name    = NULL_IF_CONFIG_SMALL("KTX 1.0 (Khronos Texture) for ASTC"),
    .p.mime_type    = "image/ktx",
    .p.extensions   = "ktx",
    .p.audio_codec  = AV_CODEC_ID_NONE,
    .p.video_codec  = AV_CODEC_ID_ASTC,
    .p.subtitle_codec = AV_CODEC_ID_NONE,
    .flags_internal = FF_OFMT_FLAG_MAX_ONE_OF_EACH |
                      FF_OFMT_FLAG_ONLY_DEFAULT_CODECS,
    .priv_data_size = sizeof(KTXMuxerContext),
    .p.priv_class   = &ktx_muxer_class,
    .write_header   = ktx_write_header,
    .write_packet   = ktx_write_packet,
    .p.flags        = AVFMT_NOTIMESTAMPS,
};
