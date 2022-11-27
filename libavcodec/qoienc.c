/*
 * QOI image format encoder
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

#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "encode.h"
#include "qoi.h"

static int qoi_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                            const AVFrame *pict, int *got_packet)
{
    const int channels = 3 + (avctx->pix_fmt == AV_PIX_FMT_RGBA);
    uint8_t px_prev[4] = { 0, 0, 0, 255 };
    uint8_t px[4] = { 0, 0, 0, 255 };
    uint8_t index[64][4] = { 0 };
    int64_t packet_size;
    uint8_t *buf;
    const uint8_t *src;
    int ret, run = 0;

    packet_size = avctx->width * avctx->height * (channels + 1LL) + 14LL + 8LL;
    if ((ret = ff_alloc_packet(avctx, pkt, packet_size)) < 0)
        return ret;

    buf = pkt->data;
    src = pict->data[0];

    bytestream_put_buffer(&buf, "qoif", 4);
    bytestream_put_be32(&buf, avctx->width);
    bytestream_put_be32(&buf, avctx->height);
    bytestream_put_byte(&buf, channels);
    bytestream_put_byte(&buf, avctx->color_trc == AVCOL_TRC_LINEAR);

    for (int y = 0; y < avctx->height; y++) {
        for (int x = 0; x < avctx->width; x++) {
            memcpy(px, src + x * channels, channels);

            if (!memcmp(px, px_prev, 4)) {
                run++;
                if (run == 62) {
                    bytestream_put_byte(&buf, QOI_OP_RUN | (run - 1));
                    run = 0;
                }
            } else {
                int index_pos;

                if (run > 0) {
                    bytestream_put_byte(&buf, QOI_OP_RUN | (run - 1));
                    run = 0;
                }

                index_pos = QOI_COLOR_HASH(px) & 63;

                if (!memcmp(index[index_pos], px, 4)) {
                    bytestream_put_byte(&buf, QOI_OP_INDEX | index_pos);
                } else {
                    memcpy(index[index_pos], px, 4);

                    if (px[3] == px_prev[3]) {
                        int8_t vr = px[0] - px_prev[0];
                        int8_t vg = px[1] - px_prev[1];
                        int8_t vb = px[2] - px_prev[2];

                        int8_t vg_r = vr - vg;
                        int8_t vg_b = vb - vg;

                        if (vr > -3 && vr < 2 &&
                            vg > -3 && vg < 2 &&
                            vb > -3 && vb < 2) {
                            bytestream_put_byte(&buf, QOI_OP_DIFF | (vr + 2) << 4 | (vg + 2) << 2 | (vb + 2));
                        } else if (vg_r >  -9 && vg_r <  8 &&
                                   vg   > -33 && vg   < 32 &&
                                   vg_b >  -9 && vg_b <  8) {
                            bytestream_put_byte(&buf, QOI_OP_LUMA     | (vg   + 32));
                            bytestream_put_byte(&buf, (vg_r + 8) << 4 | (vg_b +  8));
                        } else {
                            bytestream_put_byte(&buf, QOI_OP_RGB);
                            bytestream_put_byte(&buf, px[0]);
                            bytestream_put_byte(&buf, px[1]);
                            bytestream_put_byte(&buf, px[2]);
                        }
                    } else {
                        bytestream_put_byte(&buf, QOI_OP_RGBA);
                        bytestream_put_byte(&buf, px[0]);
                        bytestream_put_byte(&buf, px[1]);
                        bytestream_put_byte(&buf, px[2]);
                        bytestream_put_byte(&buf, px[3]);
                    }
                }
            }

            memcpy(px_prev, px, 4);
        }

        src += pict->linesize[0];
    }

    if (run)
        bytestream_put_byte(&buf, QOI_OP_RUN | (run - 1));

    bytestream_put_be64(&buf, 0x01);

    pkt->size = buf - pkt->data;

    *got_packet = 1;

    return 0;
}

const FFCodec ff_qoi_encoder = {
    .p.name         = "qoi",
    CODEC_LONG_NAME("QOI (Quite OK Image format) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_QOI,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    FF_CODEC_ENCODE_CB(qoi_encode_frame),
    .p.pix_fmts     = (const enum AVPixelFormat[]){
        AV_PIX_FMT_RGBA, AV_PIX_FMT_RGB24,
        AV_PIX_FMT_NONE
    },
};
