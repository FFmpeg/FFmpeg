/*
 * Radiance HDR image format
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

#include "libavutil/mem.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "encode.h"

typedef struct HDREncContext {
    uint8_t *scanline;
} HDREncContext;

static av_cold int hdr_encode_init(AVCodecContext *avctx)
{
    HDREncContext *s = avctx->priv_data;

    s->scanline = av_calloc(avctx->width * 4, sizeof(*s->scanline));
    if (!s->scanline)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int hdr_encode_close(AVCodecContext *avctx)
{
    HDREncContext *s = avctx->priv_data;

    av_freep(&s->scanline);

    return 0;
}

static void bytestream_put_str(uint8_t **buf, const char *const line)
{
    bytestream_put_buffer(buf, line, strlen(line));
}

static void float2rgbe(uint8_t *rgbe, float red, float green, float blue)
{
    float v;
    int e;

    v = FFMAX3(red, green, blue);

    if (v < 1e-32f) {
        rgbe[0] = rgbe[1] = rgbe[2] = rgbe[3] = 0;
    } else {
        v = frexpf(v, &e) * 256.f / v;

        rgbe[0] = av_clip_uint8(red * v);
        rgbe[1] = av_clip_uint8(green * v);
        rgbe[2] = av_clip_uint8(blue * v);
        rgbe[3] = av_clip_uint8(e + 128);
    }
}

static void rle(uint8_t **buffer, const uint8_t *data, int width)
{
#define MIN_RLE 4
    int cur = 0;

    while (cur < width) {
        int run_count = 0, old_run_count = 0;
        int beg_run = cur;
        uint8_t buf[2];

        while (run_count < MIN_RLE && beg_run < width) {
            beg_run += run_count;
            old_run_count = run_count;
            run_count = 1;
            while ((beg_run + run_count < width) && (run_count < 127) &&
                   (data[beg_run * 4] == data[(beg_run + run_count) * 4]))
                run_count++;
        }

        if ((old_run_count > 1) && (old_run_count == beg_run - cur)) {
            buf[0] = 128 + old_run_count;
            buf[1] = data[cur * 4];
            bytestream_put_buffer(buffer, buf, sizeof(buf));
            cur = beg_run;
        }

        while (cur < beg_run) {
            int nonrun_count = FFMIN(128, beg_run - cur);
            buf[0] = nonrun_count;
            bytestream_put_byte(buffer, buf[0]);
            for (int n = 0; n < nonrun_count; n++)
                bytestream_put_byte(buffer, data[(cur + n) * 4]);
            cur += nonrun_count;
        }

        if (run_count >= MIN_RLE) {
            buf[0] = 128 + run_count;
            buf[1] = data[beg_run * 4];
            bytestream_put_buffer(buffer, buf, sizeof(buf));
            cur += run_count;
        }
    }
}

static int hdr_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                            const AVFrame *frame, int *got_packet)
{
    HDREncContext *s = avctx->priv_data;
    int64_t packet_size;
    uint8_t *buf;
    int ret;

    packet_size = avctx->height * 4LL + avctx->width * avctx->height * 8LL + 1024LL;
    if ((ret = ff_get_encode_buffer(avctx, pkt, packet_size, 0)) < 0)
        return ret;

    buf = pkt->data;
    bytestream_put_str(&buf, "#?RADIANCE\n");
    bytestream_put_str(&buf, "SOFTWARE=lavc\n");
    ret = snprintf(buf, 32, "PIXASPECT=%f\n", av_q2d(av_inv_q(avctx->sample_aspect_ratio)));
    if (ret > 0)
        buf += ret;
    bytestream_put_str(&buf, "FORMAT=32-bit_rle_rgbe\n\n");
    ret = snprintf(buf, 32, "-Y %d +X %d\n", avctx->height, avctx->width);
    if (ret > 0)
        buf += ret;

    for (int y = 0; y < avctx->height; y++) {
        const float *red   = (const float *)(frame->data[2] + y * frame->linesize[2]);
        const float *green = (const float *)(frame->data[0] + y * frame->linesize[0]);
        const float *blue  = (const float *)(frame->data[1] + y * frame->linesize[1]);

        if (avctx->width < 8 || avctx->width > 0x7fff) {
            for (int x = 0; x < avctx->width; x++) {
                float2rgbe(buf, red[x], green[x], blue[x]);
                buf += 4;
            }
        } else {
            bytestream_put_byte(&buf, 2);
            bytestream_put_byte(&buf, 2);
            bytestream_put_byte(&buf, avctx->width >> 8);
            bytestream_put_byte(&buf, avctx->width & 0xFF);

            for (int x = 0; x < avctx->width; x++)
                float2rgbe(s->scanline + 4 * x, red[x], green[x], blue[x]);
            for (int p = 0; p < 4; p++)
                rle(&buf, s->scanline + p, avctx->width);
        }
    }

    pkt->flags |= AV_PKT_FLAG_KEY;

    av_shrink_packet(pkt, buf - pkt->data);

    *got_packet = 1;

    return 0;
}

const FFCodec ff_hdr_encoder = {
    .p.name         = "hdr",
    CODEC_LONG_NAME("HDR (Radiance RGBE format) image"),
    .priv_data_size = sizeof(HDREncContext),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_RADIANCE_HDR,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .init           = hdr_encode_init,
    FF_CODEC_ENCODE_CB(hdr_encode_frame),
    .close          = hdr_encode_close,
    .p.pix_fmts     = (const enum AVPixelFormat[]){
        AV_PIX_FMT_GBRPF32,
        AV_PIX_FMT_NONE
    },
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
