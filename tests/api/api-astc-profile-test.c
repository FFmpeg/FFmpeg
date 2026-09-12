/*
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

/*
 * ASTC profile test.
 *
 * Encodes a constant-colour image through AVCodecContext.profile, the path a
 * command line cannot reach because the encoder's private "profile" option
 * takes precedence there, and prints the profile the encoder resolved
 * together with the decoded result. The interesting part is an out-of-range
 * (HDR) alpha: only AV_PROFILE_ASTC_HDR can carry it, so a request that is
 * silently substituted for another profile shows up as a decoded value of 1.0
 * instead of 2.5.
 *
 * The decoded values are constant-colour blocks, so they are exact and do not
 * depend on the astcenc version the way a compressed-payload hash would.
 */

#include <stdio.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/defs.h"
#include "libavutil/common.h"
#include "libavutil/error.h"
#include "libavutil/intfloat.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"

#define SIZE 16

/* The names of the packed float formats carry a host endianness suffix, so
 * print the canonical name instead to keep the reference endian-independent. */
static const char *fmt_label(enum AVPixelFormat pix_fmt)
{
    switch (pix_fmt) {
    case AV_PIX_FMT_RGBA:    return "rgba";
    case AV_PIX_FMT_RGBAF32: return "rgbaf32";
    case AV_PIX_FMT_RGBAF16: return "rgbaf16";
    default:                 return "unexpected";
    }
}

/* Convert an IEEE-754 binary16 value held in a native uint16_t to a float.
 * The decoder output is native-endian, so going through a float keeps the
 * printed value independent of the host byte order. */
static float half_to_float(uint16_t h)
{
    uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
    uint32_t exp  = (h & 0x7C00u) >> 10;
    uint32_t mant = h & 0x03FFu;
    uint32_t f;

    if (exp == 0x1F) {                     /* infinity or NaN */
        f = 0x7F800000u | (mant << 13);
    } else if (exp == 0) {                 /* zero or subnormal */
        if (!mant)
            return av_int2float(sign);
        exp = 1;
        while (!(mant & 0x0400u)) {
            mant <<= 1;
            exp--;
        }
        mant &= 0x03FFu;
        f = ((uint32_t)(exp + 112) << 23) | (mant << 13);
    } else {
        f = ((exp + 112) << 23) | (mant << 13);
    }

    return av_int2float(f | sign);
}

static int fill_frame(const AVFrame *frame)
{
    int y, x;

    for (y = 0; y < frame->height; y++) {
        if (frame->format == AV_PIX_FMT_RGBA) {
            uint8_t *row = frame->data[0] + (size_t)y * frame->linesize[0];
            for (x = 0; x < frame->width; x++) {
                row[4 * x + 0] = 0x80;
                row[4 * x + 1] = 0x80;
                row[4 * x + 2] = 0x80;
                row[4 * x + 3] = 0xff;
            }
        } else if (frame->format == AV_PIX_FMT_RGBAF32) {
            float *row = (float *)(frame->data[0] + (size_t)y * frame->linesize[0]);
            for (x = 0; x < frame->width; x++) {
                row[4 * x + 0] = 2.5f;
                row[4 * x + 1] = 2.5f;
                row[4 * x + 2] = 2.5f;
                row[4 * x + 3] = 2.5f;
            }
        } else {
            return AVERROR(EINVAL);
        }
    }

    return 0;
}

static void print_texel(const AVFrame *frame)
{
    const uint8_t *rgba;
    const uint16_t *half;

    if (frame->format == AV_PIX_FMT_RGBA) {
        rgba = frame->data[0];
        printf("decoded %s: 0x%02x 0x%02x 0x%02x 0x%02x",
               fmt_label(frame->format),
               rgba[0], rgba[1], rgba[2], rgba[3]);
    } else if (frame->format == AV_PIX_FMT_RGBAF16) {
        half = (const uint16_t *)frame->data[0];
        printf("decoded %s: %.3f %.3f %.3f %.3f",
               fmt_label(frame->format),
               half_to_float(half[0]), half_to_float(half[1]),
               half_to_float(half[2]), half_to_float(half[3]));
    } else {
        printf("decoded %s: unexpected format",
               fmt_label(frame->format));
    }
}

static int run_case(const AVCodec *enc, const AVCodec *dec,
                    int req_profile, enum AVPixelFormat pix_fmt,
                    const char *priv_profile, int dec_profile)
{
    AVCodecContext *enc_ctx = NULL, *dec_ctx = NULL;
    AVDictionary *opts = NULL;
    AVFrame *in_frame = NULL, *out_frame = NULL;
    AVPacket *pkt = NULL;
    int ret;

    printf("requested profile %d", req_profile);
    if (priv_profile)
        printf(", private profile %s", priv_profile);
    printf(", input %s -> ", fmt_label(pix_fmt));

    enc_ctx = avcodec_alloc_context3(enc);
    if (!enc_ctx)
        return AVERROR(ENOMEM);
    enc_ctx->width     = SIZE;
    enc_ctx->height    = SIZE;
    enc_ctx->time_base = (AVRational){ 1, 1 };
    enc_ctx->pix_fmt   = pix_fmt;
    enc_ctx->profile   = req_profile;
    if (priv_profile)
        av_dict_set(&opts, "profile", priv_profile, 0);

    ret = avcodec_open2(enc_ctx, enc, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        /* Record the actual result; FATE checks it against the reference. */
        printf("open failed: %s\n", av_err2str(ret));
        ret = 0;
        goto fail;
    }
    printf("resolved profile %d, ", enc_ctx->profile);

    in_frame = av_frame_alloc();
    if (!in_frame) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    in_frame->format = pix_fmt;
    in_frame->width  = SIZE;
    in_frame->height = SIZE;
    ret = av_frame_get_buffer(in_frame, 0);
    if (ret < 0)
        goto fail;
    ret = fill_frame(in_frame);
    if (ret < 0)
        goto fail;

    pkt = av_packet_alloc();
    if (!pkt) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = avcodec_send_frame(enc_ctx, in_frame);
    if (ret < 0)
        goto fail;
    ret = avcodec_receive_packet(enc_ctx, pkt);
    if (ret < 0)
        goto fail;

    /* The decoder needs the .astc header the encoder published as extradata,
     * and the profile the container would have recorded. */
    dec_ctx = avcodec_alloc_context3(dec);
    if (!dec_ctx) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    dec_ctx->profile = dec_profile;
    if (enc_ctx->extradata_size) {
        dec_ctx->extradata = av_mallocz(enc_ctx->extradata_size +
                                        AV_INPUT_BUFFER_PADDING_SIZE);
        if (!dec_ctx->extradata) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        memcpy(dec_ctx->extradata, enc_ctx->extradata, enc_ctx->extradata_size);
        dec_ctx->extradata_size = enc_ctx->extradata_size;
    }
    ret = avcodec_open2(dec_ctx, dec, NULL);
    if (ret < 0) {
        printf("decoder open failed: %s (UNEXPECTED)\n", av_err2str(ret));
        goto fail;
    }

    out_frame = av_frame_alloc();
    if (!out_frame) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        printf("send packet failed: %s (UNEXPECTED)\n", av_err2str(ret));
        goto fail;
    }
    ret = avcodec_receive_frame(dec_ctx, out_frame);
    if (ret < 0) {
        printf("receive frame failed: %s (UNEXPECTED)\n", av_err2str(ret));
        goto fail;
    }

    print_texel(out_frame);
    printf("\n");
    ret = 0;

fail:
    av_frame_free(&in_frame);
    av_frame_free(&out_frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec_ctx);
    avcodec_free_context(&enc_ctx);
    return ret;
}

int main(void)
{
    const AVCodec *enc, *dec;
    int ret, i;

    struct {
        int req_profile;
        enum AVPixelFormat pix_fmt;
        const char *priv_profile;
        int dec_profile;
    } cases[] = {
        /* AV_PROFILE_UNKNOWN keeps the documented default. */
        { AV_PROFILE_UNKNOWN,            AV_PIX_FMT_RGBA,   NULL,          AV_PROFILE_UNKNOWN },
        { AV_PROFILE_ASTC_LDR_SRGB,      AV_PIX_FMT_RGBA,   NULL,          AV_PROFILE_ASTC_LDR_SRGB },
        { AV_PROFILE_ASTC_LDR,           AV_PIX_FMT_RGBA,   NULL,          AV_PROFILE_ASTC_LDR },
        /* An HDR profile cannot be driven by 8-bit input. */
        { AV_PROFILE_ASTC_HDR_RGB_LDR_A, AV_PIX_FMT_RGBA,   NULL,          AV_PROFILE_ASTC_HDR_RGB_LDR_A },
        { AV_PROFILE_ASTC_HDR,           AV_PIX_FMT_RGBA,   NULL,          AV_PROFILE_ASTC_HDR },
        /* LINEAR_ANY is stream metadata, not an encoding profile. */
        { AV_PROFILE_ASTC_LINEAR_ANY,    AV_PIX_FMT_RGBA,   NULL,          AV_PROFILE_ASTC_LINEAR_ANY },
        /* Only full HDR keeps an out-of-range alpha; hdr-ldr-a clamps it. */
        { AV_PROFILE_ASTC_HDR,           AV_PIX_FMT_RGBAF32, NULL,         AV_PROFILE_ASTC_HDR },
        { AV_PROFILE_ASTC_HDR_RGB_LDR_A, AV_PIX_FMT_RGBAF32, NULL,         AV_PROFILE_ASTC_HDR_RGB_LDR_A },
        /* LDR input is auto-promoted, so it loses the HDR alpha as well. */
        { AV_PROFILE_ASTC_LDR,           AV_PIX_FMT_RGBAF32, NULL,         AV_PROFILE_ASTC_HDR_RGB_LDR_A },
        /* An explicit private option overrides the public field. */
        { AV_PROFILE_ASTC_HDR,           AV_PIX_FMT_RGBAF32, "hdr-ldr-a",  AV_PROFILE_ASTC_HDR_RGB_LDR_A },
        /* Same payload decoded as a linear texture: LINEAR_ANY samples it
         * with HDR precision, so the output is half-float. */
        { AV_PROFILE_ASTC_LDR_SRGB,      AV_PIX_FMT_RGBA,   NULL,          AV_PROFILE_ASTC_LINEAR_ANY },
    };

    enc = avcodec_find_encoder(AV_CODEC_ID_ASTC);
    if (!enc) {
        av_log(NULL, AV_LOG_ERROR, "Can't find encoder\n");
        return 1;
    }
    dec = avcodec_find_decoder(AV_CODEC_ID_ASTC);
    if (!dec) {
        av_log(NULL, AV_LOG_ERROR, "Can't find decoder\n");
        return 1;
    }

    for (i = 0; i < FF_ARRAY_ELEMS(cases); i++) {
        ret = run_case(enc, dec, cases[i].req_profile, cases[i].pix_fmt,
                       cases[i].priv_profile, cases[i].dec_profile);
        if (ret < 0)
            return 1;
    }

    return 0;
}
