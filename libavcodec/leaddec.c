/*
 * LEAD MCMP decoder
 *
 * Copyright (c) 2023 Peter Ross
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

#include "avcodec.h"
#include "blockdsp.h"
#include "codec_internal.h"
#include "copy_block.h"
#include "decode.h"
#include "get_bits.h"
#include "idctdsp.h"
#include "jpegquanttables.h"
#include "jpegtables.h"
#include "leaddata.h"
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"
#include "libavutil/thread.h"

#define LUMA_DC_BITS 9
#define CHROMA_DC_BITS 11
#define LUMA_AC_BITS 10
#define CHROMA_AC_BITS 10

static VLCElem luma_dc_vlc[1 << LUMA_DC_BITS];
static VLCElem chroma_dc_vlc[1 << CHROMA_DC_BITS];
static VLCElem luma_ac_vlc[1160];
static VLCElem chroma_ac_vlc[1160];

static av_cold void lead_init_static_data(void)
{
    VLC_INIT_STATIC_TABLE_FROM_LENGTHS(luma_dc_vlc, LUMA_DC_BITS, FF_ARRAY_ELEMS(luma_dc_len),
                                       luma_dc_len, 1,
                                       NULL, 0, 0,
                                       0, 0);
    VLC_INIT_STATIC_TABLE_FROM_LENGTHS(chroma_dc_vlc, CHROMA_DC_BITS, FF_ARRAY_ELEMS(chroma_dc_len),
                                       chroma_dc_len, 1,
                                       NULL, 0, 0,
                                       0, 0);
    VLC_INIT_STATIC_TABLE_FROM_LENGTHS(luma_ac_vlc, LUMA_AC_BITS, FF_ARRAY_ELEMS(luma_ac_len),
                                       luma_ac_len, 1,
                                       ff_mjpeg_val_ac_luminance, 1, 1,
                                       0, 0);
    VLC_INIT_STATIC_TABLE_FROM_LENGTHS(chroma_ac_vlc, CHROMA_AC_BITS, FF_ARRAY_ELEMS(chroma_ac_len),
                                       chroma_ac_len, 1,
                                       ff_mjpeg_val_ac_chrominance, 1, 1,
                                       0, 0);
}

typedef struct LeadContext {
    uint8_t *bitstream_buf;
    unsigned int bitstream_buf_size;
    BlockDSPContext bdsp;
    IDCTDSPContext idsp;
    uint8_t permutated_scantable[64];
} LeadContext;

static av_cold int lead_decode_init(AVCodecContext * avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    LeadContext *s = avctx->priv_data;

    if (avctx->extradata_size < 20)
        return AVERROR_INVALIDDATA;

    ff_blockdsp_init(&s->bdsp);
    ff_idctdsp_init(&s->idsp, avctx);
    ff_permute_scantable(s->permutated_scantable, ff_zigzag_direct, s->idsp.idct_permutation);

    ff_thread_once(&init_static_once, lead_init_static_data);

    return 0;
}

static void calc_dequant(uint16_t * dequant, const uint8_t * quant_tbl, int q)
{
    for (int i = 0; i < 64; i++)
        dequant[i] = av_clip(q * quant_tbl[ff_zigzag_direct[i]] / 50, 2, 32767);
}

static int decode_block(LeadContext * s, GetBitContext * gb,
                        const VLCElem * dc_table, int dc_bits, const VLCElem * ac_table, int ac_bits,
                        int16_t * dc_pred, const uint16_t * dequant,
                        uint8_t * dst, int stride)
{
    DECLARE_ALIGNED(32, int16_t, block)[64];
    int size;

    s->bdsp.clear_block(block);

    if (get_bits_left(gb) <= 0)
        return AVERROR_INVALIDDATA;

    size = get_vlc2(gb, dc_table, dc_bits, 1);
    if (size < 0)
        return AVERROR_INVALIDDATA;

    if (size)
        *dc_pred += get_xbits(gb, size);

    block[0] = (1 << 10) + *dc_pred * dequant[0];

    for (int i = 1; i < 64; i++) {
        int symbol = get_vlc2(gb, ac_table, ac_bits, 2);
        if (symbol < 0)
            return AVERROR_INVALIDDATA;

        if (!symbol)
            break;

        i += symbol >> 4;
        if (i >= 64)
            return AVERROR_INVALIDDATA;

        size = symbol & 0xF;
        if (size)
            block[s->permutated_scantable[i]] = get_xbits(gb, size) * dequant[i];
    }

    s->idsp.idct_put(dst, stride, block);
    return 0;
}

static int lead_decode_frame(AVCodecContext *avctx, AVFrame * frame,
                             int * got_frame, AVPacket * avpkt)
{
    LeadContext *s = avctx->priv_data;
    const uint8_t * buf = avpkt->data;
    int ret, format, zero = 0, yuv20p_half = 0, fields = 1, q, size;
    GetBitContext gb;
    int16_t dc_pred[3] = {0, 0, 0};
    uint16_t dequant[2][64];

    if (avpkt->size < 8)
        return AVERROR_INVALIDDATA;

    format = AV_RL16(buf + 4);
    switch(format) {
    case 0x0:
        zero = 1;
        avctx->pix_fmt = AV_PIX_FMT_YUV420P;
        break;
    case 0x8000:
        yuv20p_half = 1;
        // fall-through
    case 0x1000:
        avctx->pix_fmt = AV_PIX_FMT_YUV420P;
        break;
    case 0x2000:
        avctx->pix_fmt = AV_PIX_FMT_YUV444P;
        break;
    case 0x2006:
        avctx->pix_fmt = AV_PIX_FMT_YUV444P;
        fields = 2;
        break;
    default:
        avpriv_request_sample(avctx, "unsupported format 0x%x", format);
        return AVERROR_PATCHWELCOME;
    }

    q = AV_RL16(buf + 6);
    calc_dequant(dequant[0], ff_mjpeg_std_luminance_quant_tbl, q);
    calc_dequant(dequant[1], ff_mjpeg_std_chrominance_quant_tbl, q);

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    av_fast_padded_malloc(&s->bitstream_buf, &s->bitstream_buf_size, avpkt->size - 8);
    if (!s->bitstream_buf)
        return AVERROR(ENOMEM);

    size = 0;
    for (int i = 8; i < avpkt->size; i++) {
        int src = buf[i] ^ 0x80;
        s->bitstream_buf[size++] = src;
        if (src == 0xFF && i + 1 < avpkt->size && (buf[i + 1] ^ 0x80) == 0x00)
            i++;
    }

    ret = init_get_bits8(&gb, s->bitstream_buf, size);
    if (ret < 0)
        return ret;

    if (avctx->pix_fmt == AV_PIX_FMT_YUV420P && zero) {
        for (int mb_y = 0; mb_y < avctx->height / 8; mb_y++)
            for (int mb_x = 0; mb_x < avctx->width / 16; mb_x++)
                for (int b = 0; b < 4; b++) {
                    int luma_block = 2;
                    const VLCElem * dc_vlc = b < luma_block ? luma_dc_vlc : chroma_dc_vlc;
                    int dc_bits            = b < luma_block ? LUMA_DC_BITS : CHROMA_DC_BITS;
                    const VLCElem * ac_vlc = b < luma_block ? luma_ac_vlc : chroma_ac_vlc;
                    int ac_bits            = b < luma_block ? LUMA_AC_BITS : CHROMA_AC_BITS;
                    int plane              = b < luma_block ? 0 : b - 1;
                    int x, y, yclip;

                    if (b < luma_block) {
                        y = 8*mb_y + 8*(b >> 1);
                        x = 16*mb_x + 8*(b & 1);
                        yclip = 0;
                    } else {
                        y = 4*mb_y;
                        x = 8*mb_x;
                        yclip = y + 8 >= avctx->height / 2;
                    }

                    if (yclip) {
                        uint8_t tmp[64];
                        ret = decode_block(s, &gb, dc_vlc, dc_bits, ac_vlc, ac_bits,
                            dc_pred + plane, dequant[!(b < 4)], tmp, 8);
                        for (int yy = 0; yy < 8 && y + yy < avctx->height / 2; yy++)
                            memcpy(frame->data[plane] + (y+yy)*frame->linesize[plane] + x, tmp + yy, 8);
                    } else {
                        ret = decode_block(s, &gb, dc_vlc, dc_bits, ac_vlc, ac_bits,
                            dc_pred + plane, dequant[!(b < 4)],
                            frame->data[plane] + y*frame->linesize[plane] + x,
                            frame->linesize[plane]);
                    }
                    if (ret < 0)
                        return ret;
                }
    } else if (avctx->pix_fmt == AV_PIX_FMT_YUV420P) {
        for (int mb_y = 0; mb_y < (avctx->height + 15) / 16; mb_y++)
            for (int mb_x = 0; mb_x < (avctx->width + 15) / 16; mb_x++)
                for (int b = 0; b < (yuv20p_half ? 4 : 6); b++) {
                    int luma_block = yuv20p_half ? 2 : 4;
                    const VLCElem * dc_vlc = b < luma_block ? luma_dc_vlc : chroma_dc_vlc;
                    int dc_bits            = b < luma_block ? LUMA_DC_BITS : CHROMA_DC_BITS;
                    const VLCElem * ac_vlc = b < luma_block ? luma_ac_vlc : chroma_ac_vlc;
                    int ac_bits            = b < luma_block ? LUMA_AC_BITS : CHROMA_AC_BITS;
                    int plane              = b < luma_block ? 0 : b - (yuv20p_half ? 1 : 3);
                    int x, y;

                    if (b < luma_block) {
                        y = 16*mb_y + 8*(b >> 1);
                        x = 16*mb_x + 8*(b & 1);
                    } else {
                        y = 8*mb_y;
                        x = 8*mb_x;
                    }

                    ret = decode_block(s, &gb, dc_vlc, dc_bits, ac_vlc, ac_bits,
                        dc_pred + plane, dequant[!(b < 4)],
                        frame->data[plane] + y*frame->linesize[plane] + x,
                        (yuv20p_half && b < 2 ? 2 : 1) * frame->linesize[plane]);
                    if (ret < 0)
                        return ret;

                    if (yuv20p_half && b < 2)
                        copy_block8(frame->data[plane] + (y + 1)*frame->linesize[plane] + x,
                                    frame->data[plane] + y*frame->linesize[plane] + x,
                                    2*frame->linesize[plane], 2*frame->linesize[plane], 8);
                }
    } else {
        for (int f = 0; f < fields; f++)
            for (int j = 0; j < (avctx->height + 7) / fields / 8; j++)
                for (int i = 0; i < (avctx->width + 7) / 8; i++)
                    for (int plane = 0; plane < 3; plane++) {
                        const VLCElem * dc_vlc = !plane ? luma_dc_vlc : chroma_dc_vlc;
                        int dc_bits            = !plane ? LUMA_DC_BITS : CHROMA_DC_BITS;
                        const VLCElem * ac_vlc = !plane ? luma_ac_vlc : chroma_ac_vlc;
                        int ac_bits            = !plane ? LUMA_AC_BITS : CHROMA_AC_BITS;

                        ret = decode_block(s, &gb, dc_vlc, dc_bits, ac_vlc, ac_bits,
                            dc_pred + plane, dequant[!!plane],
                            frame->data[plane] + (f + 8*j*fields)*frame->linesize[plane] + 8*i,
                            fields * frame->linesize[plane]);
                        if (ret < 0)
                            return ret;
                    }
    }

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int lead_decode_end(AVCodecContext * avctx)
{
    LeadContext *s = avctx->priv_data;

    av_freep(&s->bitstream_buf);

    return 0;
}

const FFCodec ff_lead_decoder = {
    .p.name         = "lead",
    CODEC_LONG_NAME("LEAD MCMP"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_LEAD,
    .priv_data_size = sizeof(LeadContext),
    .init           = lead_decode_init,
    .close          = lead_decode_end,
    FF_CODEC_DECODE_CB(lead_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
