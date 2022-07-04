/*
 * Cryo Interactive Entertainment HNM4 video decoder
 *
 * Copyright (c) 2012 David Kment
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

#include <string.h>

#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "internal.h"

#define HNM4_CHUNK_ID_PL 19536
#define HNM4_CHUNK_ID_IZ 23113
#define HNM4_CHUNK_ID_IU 21833
#define HNM4_CHUNK_ID_SD 17491

typedef struct Hnm4VideoContext {
    uint8_t version;
    int width;
    int height;
    uint8_t *current;
    uint8_t *previous;
    uint8_t *buffer1;
    uint8_t *buffer2;
    uint8_t *processed;
    uint32_t palette[256];
} Hnm4VideoContext;

static int getbit(GetByteContext *gb, uint32_t *bitbuf, int *bits)
{
    int ret;

    if (!*bits) {
        *bitbuf = bytestream2_get_le32(gb);
        *bits = 32;
    }

    ret = *bitbuf >> 31;
    *bitbuf <<= 1;
    (*bits)--;

    return ret;
}

static void unpack_intraframe(AVCodecContext *avctx, const uint8_t *src,
                              uint32_t size)
{
    Hnm4VideoContext *hnm = avctx->priv_data;
    GetByteContext gb;
    uint32_t bitbuf = 0, writeoffset = 0, count = 0;
    uint16_t word;
    int32_t offset;
    int bits = 0;

    bytestream2_init(&gb, src, size);

    while (bytestream2_tell(&gb) < size) {
        if (getbit(&gb, &bitbuf, &bits)) {
            if (writeoffset >= hnm->width * hnm->height) {
                av_log(avctx, AV_LOG_ERROR,
                       "Attempting to write out of bounds\n");
                break;
            }
            hnm->current[writeoffset++] = bytestream2_get_byte(&gb);
        } else {
            if (getbit(&gb, &bitbuf, &bits)) {
                word   = bytestream2_get_le16(&gb);
                count  = word & 0x07;
                offset = (word >> 3) - 0x2000;
                if (!count)
                    count = bytestream2_get_byte(&gb);
                if (!count)
                    return;
            } else {
                count  = getbit(&gb, &bitbuf, &bits) * 2;
                count += getbit(&gb, &bitbuf, &bits);
                offset = bytestream2_get_byte(&gb) - 0x0100;
            }
            count  += 2;
            offset += writeoffset;
            if (offset < 0 || offset + count >= hnm->width * hnm->height) {
                av_log(avctx, AV_LOG_ERROR, "Attempting to read out of bounds\n");
                break;
            } else if (writeoffset + count >= hnm->width * hnm->height) {
                av_log(avctx, AV_LOG_ERROR,
                       "Attempting to write out of bounds\n");
                break;
            }
            while (count--) {
                hnm->current[writeoffset++] = hnm->current[offset++];
            }
        }
    }
}

static void postprocess_current_frame(AVCodecContext *avctx)
{
    Hnm4VideoContext *hnm = avctx->priv_data;
    uint32_t x, y, src_y;
    int width = hnm->width;

    for (y = 0; y < hnm->height; y++) {
        uint8_t *dst = hnm->processed + y * width;
        const uint8_t *src = hnm->current;
        src_y = y - (y % 2);
        src += src_y * width + (y % 2);
        for (x = 0; x < width; x++) {
            dst[x] = *src;
            src += 2;
        }
    }
}

static void copy_processed_frame(AVCodecContext *avctx, AVFrame *frame)
{
    Hnm4VideoContext *hnm = avctx->priv_data;
    uint8_t *src = hnm->processed;
    uint8_t *dst = frame->data[0];
    int y;

    for (y = 0; y < hnm->height; y++) {
        memcpy(dst, src, hnm->width);
        src += hnm->width;
        dst += frame->linesize[0];
    }
}

static int decode_interframe_v4(AVCodecContext *avctx, const uint8_t *src, uint32_t size)
{
    Hnm4VideoContext *hnm = avctx->priv_data;
    GetByteContext gb;
    uint32_t writeoffset = 0;
    int count, left, offset;
    uint8_t tag, previous, backline, backward, swap;

    bytestream2_init(&gb, src, size);

    while (bytestream2_tell(&gb) < size) {
        count = bytestream2_peek_byte(&gb) & 0x1F;
        if (count == 0) {
            tag = bytestream2_get_byte(&gb) & 0xE0;
            tag = tag >> 5;

            if (tag == 0) {
                if (writeoffset + 2 > hnm->width * hnm->height) {
                    av_log(avctx, AV_LOG_ERROR, "writeoffset out of bounds\n");
                    return AVERROR_INVALIDDATA;
                }
                hnm->current[writeoffset++] = bytestream2_get_byte(&gb);
                hnm->current[writeoffset++] = bytestream2_get_byte(&gb);
            } else if (tag == 1) {
                writeoffset += bytestream2_get_byte(&gb) * 2;
            } else if (tag == 2) {
                count = bytestream2_get_le16(&gb);
                count *= 2;
                writeoffset += count;
            } else if (tag == 3) {
                count = bytestream2_get_byte(&gb) * 2;
                if (writeoffset + count > hnm->width * hnm->height) {
                    av_log(avctx, AV_LOG_ERROR, "writeoffset out of bounds\n");
                    return AVERROR_INVALIDDATA;
                }
                while (count > 0) {
                    hnm->current[writeoffset++] = bytestream2_peek_byte(&gb);
                    count--;
                }
                bytestream2_skip(&gb, 1);
            } else {
                break;
            }
            if (writeoffset > hnm->width * hnm->height) {
                av_log(avctx, AV_LOG_ERROR, "writeoffset out of bounds\n");
                return AVERROR_INVALIDDATA;
            }
        } else {
            previous = bytestream2_peek_byte(&gb) & 0x20;
            backline = bytestream2_peek_byte(&gb) & 0x40;
            backward = bytestream2_peek_byte(&gb) & 0x80;
            bytestream2_skip(&gb, 1);
            swap   = bytestream2_peek_byte(&gb) & 0x01;
            offset = bytestream2_get_le16(&gb);
            offset = (offset >> 1) & 0x7FFF;
            offset = writeoffset + (offset * 2) - 0x8000;

            left = count;

            if (!backward && offset + 2*count > hnm->width * hnm->height) {
                av_log(avctx, AV_LOG_ERROR, "Attempting to read out of bounds\n");
                return AVERROR_INVALIDDATA;
            } else if (backward && offset + 1 >= hnm->width * hnm->height) {
                av_log(avctx, AV_LOG_ERROR, "Attempting to read out of bounds\n");
                return AVERROR_INVALIDDATA;
            } else if (writeoffset + 2*count > hnm->width * hnm->height) {
                av_log(avctx, AV_LOG_ERROR,
                       "Attempting to write out of bounds\n");
                return AVERROR_INVALIDDATA;

            }
            if(backward) {
                if (offset < (!!backline)*(2 * hnm->width - 1) + 2*(left-1)) {
                    av_log(avctx, AV_LOG_ERROR, "Attempting to read out of bounds\n");
                    return AVERROR_INVALIDDATA;
                }
            } else {
                if (offset < (!!backline)*(2 * hnm->width - 1)) {
                    av_log(avctx, AV_LOG_ERROR, "Attempting to read out of bounds\n");
                    return AVERROR_INVALIDDATA;
                }
            }

            if (previous) {
                while (left > 0) {
                    if (backline) {
                        hnm->current[writeoffset++] = hnm->previous[offset - (2 * hnm->width) + 1];
                        hnm->current[writeoffset++] = hnm->previous[offset++];
                        offset++;
                    } else {
                        hnm->current[writeoffset++] = hnm->previous[offset++];
                        hnm->current[writeoffset++] = hnm->previous[offset++];
                    }
                    if (backward)
                        offset -= 4;
                    left--;
                }
            } else {
                while (left > 0) {
                    if (backline) {
                        hnm->current[writeoffset++] = hnm->current[offset - (2 * hnm->width) + 1];
                        hnm->current[writeoffset++] = hnm->current[offset++];
                        offset++;
                    } else {
                        hnm->current[writeoffset++] = hnm->current[offset++];
                        hnm->current[writeoffset++] = hnm->current[offset++];
                    }
                    if (backward)
                        offset -= 4;
                    left--;
                }
            }

            if (swap) {
                left         = count;
                writeoffset -= count * 2;
                while (left > 0) {
                    swap = hnm->current[writeoffset];
                    hnm->current[writeoffset] = hnm->current[writeoffset + 1];
                    hnm->current[writeoffset + 1] = swap;
                    left--;
                    writeoffset += 2;
                }
            }
        }
    }
    return 0;
}

static void decode_interframe_v4a(AVCodecContext *avctx, const uint8_t *src,
                                  uint32_t size)
{
    Hnm4VideoContext *hnm = avctx->priv_data;
    GetByteContext gb;
    uint32_t writeoffset = 0, offset;
    uint8_t tag, count, previous, delta;

    bytestream2_init(&gb, src, size);

    while (bytestream2_tell(&gb) < size) {
        count = bytestream2_peek_byte(&gb) & 0x3F;
        if (count == 0) {
            tag = bytestream2_get_byte(&gb) & 0xC0;
            tag = tag >> 6;
            if (tag == 0) {
                writeoffset += bytestream2_get_byte(&gb);
            } else if (tag == 1) {
                if (writeoffset + hnm->width >= hnm->width * hnm->height) {
                    av_log(avctx, AV_LOG_ERROR, "writeoffset out of bounds\n");
                    break;
                }
                hnm->current[writeoffset]              = bytestream2_get_byte(&gb);
                hnm->current[writeoffset + hnm->width] = bytestream2_get_byte(&gb);
                writeoffset++;
            } else if (tag == 2) {
                writeoffset += hnm->width;
            } else if (tag == 3) {
                break;
            }
            if (writeoffset > hnm->width * hnm->height) {
                av_log(avctx, AV_LOG_ERROR, "writeoffset out of bounds\n");
                break;
            }
        } else {
            delta    = bytestream2_peek_byte(&gb) & 0x80;
            previous = bytestream2_peek_byte(&gb) & 0x40;
            bytestream2_skip(&gb, 1);

            offset  = writeoffset;
            offset += bytestream2_get_le16(&gb);

            if (delta) {
                if (offset < 0x10000) {
                    av_log(avctx, AV_LOG_ERROR, "Attempting to read out of bounds\n");
                    break;
                }
                offset -= 0x10000;
            }

            if (offset + hnm->width + count >= hnm->width * hnm->height) {
                av_log(avctx, AV_LOG_ERROR, "Attempting to read out of bounds\n");
                break;
            } else if (writeoffset + hnm->width + count >= hnm->width * hnm->height) {
                av_log(avctx, AV_LOG_ERROR, "Attempting to write out of bounds\n");
                break;
            }

            if (previous) {
                while (count > 0) {
                    hnm->current[writeoffset]              = hnm->previous[offset];
                    hnm->current[writeoffset + hnm->width] = hnm->previous[offset + hnm->width];
                    writeoffset++;
                    offset++;
                    count--;
                }
            } else {
                while (count > 0) {
                    hnm->current[writeoffset]              = hnm->current[offset];
                    hnm->current[writeoffset + hnm->width] = hnm->current[offset + hnm->width];
                    writeoffset++;
                    offset++;
                    count--;
                }
            }
        }
    }
}

static void hnm_update_palette(AVCodecContext *avctx, const uint8_t *src,
                               uint32_t size)
{
    Hnm4VideoContext *hnm = avctx->priv_data;
    GetByteContext gb;
    uint8_t start, writeoffset;
    uint16_t count;
    int eight_bit_colors;

    eight_bit_colors = src[7] & 0x80 && hnm->version == 0x4a;

    // skip first 8 bytes
    bytestream2_init(&gb, src + 8, size - 8);

    while (bytestream2_tell(&gb) < size - 8) {
        start = bytestream2_get_byte(&gb);
        count = bytestream2_get_byte(&gb);
        if (start == 255 && count == 255)
            break;
        if (count == 0)
            count = 256;
        writeoffset = start;
        while (count > 0) {
            hnm->palette[writeoffset] = bytestream2_get_be24(&gb);
            if (!eight_bit_colors)
                hnm->palette[writeoffset] <<= 2;
            hnm->palette[writeoffset] |= (0xFFU << 24);
            count--;
            writeoffset++;
        }
    }
}

static int hnm_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame, AVPacket *avpkt)
{
    Hnm4VideoContext *hnm = avctx->priv_data;
    int ret;
    uint16_t chunk_id;

    if (avpkt->size < 8) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    chunk_id = AV_RL16(avpkt->data + 4);

    if (chunk_id == HNM4_CHUNK_ID_PL) {
        hnm_update_palette(avctx, avpkt->data, avpkt->size);
    } else if (chunk_id == HNM4_CHUNK_ID_IZ) {
        if (avpkt->size < 12) {
            av_log(avctx, AV_LOG_ERROR, "packet too small\n");
            return AVERROR_INVALIDDATA;
        }
        if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
            return ret;

        unpack_intraframe(avctx, avpkt->data + 12, avpkt->size - 12);
        memcpy(hnm->previous, hnm->current, hnm->width * hnm->height);
        if (hnm->version == 0x4a)
            memcpy(hnm->processed, hnm->current, hnm->width * hnm->height);
        else
            postprocess_current_frame(avctx);
        copy_processed_frame(avctx, frame);
        frame->pict_type = AV_PICTURE_TYPE_I;
        frame->key_frame = 1;
        memcpy(frame->data[1], hnm->palette, 256 * 4);
        *got_frame = 1;
    } else if (chunk_id == HNM4_CHUNK_ID_IU) {
        if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
            return ret;

        if (hnm->version == 0x4a) {
            decode_interframe_v4a(avctx, avpkt->data + 8, avpkt->size - 8);
            memcpy(hnm->processed, hnm->current, hnm->width * hnm->height);
        } else {
            int ret = decode_interframe_v4(avctx, avpkt->data + 8, avpkt->size - 8);
            if (ret < 0)
                return ret;
            postprocess_current_frame(avctx);
        }
        copy_processed_frame(avctx, frame);
        frame->pict_type = AV_PICTURE_TYPE_P;
        frame->key_frame = 0;
        memcpy(frame->data[1], hnm->palette, 256 * 4);
        *got_frame = 1;
        FFSWAP(uint8_t *, hnm->current, hnm->previous);
    } else {
        av_log(avctx, AV_LOG_ERROR, "invalid chunk id: %d\n", chunk_id);
        return AVERROR_INVALIDDATA;
    }

    return avpkt->size;
}

static av_cold int hnm_decode_init(AVCodecContext *avctx)
{
    Hnm4VideoContext *hnm = avctx->priv_data;
    int ret;

    if (avctx->extradata_size < 1) {
        av_log(avctx, AV_LOG_ERROR,
               "Extradata missing, decoder requires version number\n");
        return AVERROR_INVALIDDATA;
    }

    ret = av_image_check_size(avctx->width, avctx->height, 0, avctx);
    if (ret < 0)
        return ret;
    if (avctx->height & 1)
        return AVERROR(EINVAL);

    hnm->version   = avctx->extradata[0];
    avctx->pix_fmt = AV_PIX_FMT_PAL8;
    hnm->width     = avctx->width;
    hnm->height    = avctx->height;
    hnm->buffer1   = av_mallocz(avctx->width * avctx->height);
    hnm->buffer2   = av_mallocz(avctx->width * avctx->height);
    hnm->processed = av_mallocz(avctx->width * avctx->height);

    if (!hnm->buffer1 || !hnm->buffer2 || !hnm->processed) {
        av_log(avctx, AV_LOG_ERROR, "av_mallocz() failed\n");
        return AVERROR(ENOMEM);
    }

    hnm->current  = hnm->buffer1;
    hnm->previous = hnm->buffer2;

    return 0;
}

static av_cold int hnm_decode_end(AVCodecContext *avctx)
{
    Hnm4VideoContext *hnm = avctx->priv_data;

    av_freep(&hnm->buffer1);
    av_freep(&hnm->buffer2);
    av_freep(&hnm->processed);

    return 0;
}

const FFCodec ff_hnm4_video_decoder = {
    .p.name         = "hnm4video",
    .p.long_name    = NULL_IF_CONFIG_SMALL("HNM 4 video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_HNM4_VIDEO,
    .priv_data_size = sizeof(Hnm4VideoContext),
    .init           = hnm_decode_init,
    .close          = hnm_decode_end,
    FF_CODEC_DECODE_CB(hnm_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
