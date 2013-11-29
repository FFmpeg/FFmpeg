/*
 * DV decoder
 * Copyright (c) 2002 Fabrice Bellard
 * Copyright (c) 2004 Roman Shaposhnik
 *
 * 50 Mbps (DVCPRO50) support
 * Copyright (c) 2006 Daniel Maas <dmaas@maasdigital.com>
 *
 * 100 Mbps (DVCPRO HD) support
 * Initial code by Daniel Maas <dmaas@maasdigital.com> (funded by BBC R&D)
 * Final code by Roman Shaposhnik
 *
 * Many thanks to Dan Dennedy <dan@dennedy.org> for providing wealth
 * of DV technical info.
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * DV decoder
 */

#include "libavutil/internal.h"
#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "internal.h"
#include "get_bits.h"
#include "put_bits.h"
#include "simple_idct.h"
#include "dvdata.h"
#include "dv.h"

typedef struct BlockInfo {
    const uint32_t *factor_table;
    const uint8_t *scan_table;
    uint8_t pos; /* position in block */
    void (*idct_put)(uint8_t *dest, int line_size, int16_t *block);
    uint8_t partial_bit_count;
    uint32_t partial_bit_buffer;
    int shift_offset;
} BlockInfo;

static const int dv_iweight_bits = 14;

/* decode AC coefficients */
static void dv_decode_ac(GetBitContext *gb, BlockInfo *mb, int16_t *block)
{
    int last_index = gb->size_in_bits;
    const uint8_t  *scan_table   = mb->scan_table;
    const uint32_t *factor_table = mb->factor_table;
    int pos               = mb->pos;
    int partial_bit_count = mb->partial_bit_count;
    int level, run, vlc_len, index;

    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);

    /* if we must parse a partial VLC, we do it here */
    if (partial_bit_count > 0) {
        re_cache = re_cache >> partial_bit_count | mb->partial_bit_buffer;
        re_index -= partial_bit_count;
        mb->partial_bit_count = 0;
    }

    /* get the AC coefficients until last_index is reached */
    for (;;) {
        av_dlog(NULL, "%2d: bits=%04x index=%d\n", pos, SHOW_UBITS(re, gb, 16),
                re_index);
        /* our own optimized GET_RL_VLC */
        index   = NEG_USR32(re_cache, TEX_VLC_BITS);
        vlc_len = ff_dv_rl_vlc[index].len;
        if (vlc_len < 0) {
            index = NEG_USR32((unsigned)re_cache << TEX_VLC_BITS, -vlc_len) +
                    ff_dv_rl_vlc[index].level;
            vlc_len = TEX_VLC_BITS - vlc_len;
        }
        level = ff_dv_rl_vlc[index].level;
        run   = ff_dv_rl_vlc[index].run;

        /* gotta check if we're still within gb boundaries */
        if (re_index + vlc_len > last_index) {
            /* should be < 16 bits otherwise a codeword could have been parsed */
            mb->partial_bit_count = last_index - re_index;
            mb->partial_bit_buffer = re_cache & ~(-1u >> mb->partial_bit_count);
            re_index = last_index;
            break;
        }
        re_index += vlc_len;

        av_dlog(NULL, "run=%d level=%d\n", run, level);
        pos += run;
        if (pos >= 64)
            break;

        level = (level * factor_table[pos] + (1 << (dv_iweight_bits - 1))) >> dv_iweight_bits;
        block[scan_table[pos]] = level;

        UPDATE_CACHE(re, gb);
    }
    CLOSE_READER(re, gb);
    mb->pos = pos;
}

static inline void bit_copy(PutBitContext *pb, GetBitContext *gb)
{
    int bits_left = get_bits_left(gb);
    while (bits_left >= MIN_CACHE_BITS) {
        put_bits(pb, MIN_CACHE_BITS, get_bits(gb, MIN_CACHE_BITS));
        bits_left -= MIN_CACHE_BITS;
    }
    if (bits_left > 0) {
        put_bits(pb, bits_left, get_bits(gb, bits_left));
    }
}

/* mb_x and mb_y are in units of 8 pixels */
static int dv_decode_video_segment(AVCodecContext *avctx, void *arg)
{
    DVVideoContext *s = avctx->priv_data;
    DVwork_chunk *work_chunk = arg;
    int quant, dc, dct_mode, class1, j;
    int mb_index, mb_x, mb_y, last_index;
    int y_stride, linesize;
    int16_t *block, *block1;
    int c_offset;
    uint8_t *y_ptr;
    const uint8_t *buf_ptr;
    PutBitContext pb, vs_pb;
    GetBitContext gb;
    BlockInfo mb_data[5 * DV_MAX_BPM], *mb, *mb1;
    LOCAL_ALIGNED_16(int16_t, sblock, [5*DV_MAX_BPM], [64]);
    LOCAL_ALIGNED_16(uint8_t, mb_bit_buffer, [  80 + FF_INPUT_BUFFER_PADDING_SIZE]); /* allow some slack */
    LOCAL_ALIGNED_16(uint8_t, vs_bit_buffer, [5*80 + FF_INPUT_BUFFER_PADDING_SIZE]); /* allow some slack */
    const int log2_blocksize = 3;
    int is_field_mode[5];

    assert((((int)mb_bit_buffer) & 7) == 0);
    assert((((int)vs_bit_buffer) & 7) == 0);

    memset(sblock, 0, 5*DV_MAX_BPM*sizeof(*sblock));

    /* pass 1: read DC and AC coefficients in blocks */
    buf_ptr = &s->buf[work_chunk->buf_offset*80];
    block1  = &sblock[0][0];
    mb1     = mb_data;
    init_put_bits(&vs_pb, vs_bit_buffer, 5 * 80);
    for (mb_index = 0; mb_index < 5; mb_index++, mb1 += s->sys->bpm, block1 += s->sys->bpm * 64) {
        /* skip header */
        quant = buf_ptr[3] & 0x0f;
        buf_ptr += 4;
        init_put_bits(&pb, mb_bit_buffer, 80);
        mb    = mb1;
        block = block1;
        is_field_mode[mb_index] = 0;
        for (j = 0; j < s->sys->bpm; j++) {
            last_index = s->sys->block_sizes[j];
            init_get_bits(&gb, buf_ptr, last_index);

            /* get the DC */
            dc       = get_sbits(&gb, 9);
            dct_mode = get_bits1(&gb);
            class1   = get_bits(&gb, 2);
            if (DV_PROFILE_IS_HD(s->sys)) {
                mb->idct_put     = s->idct_put[0];
                mb->scan_table   = s->dv_zigzag[0];
                mb->factor_table = &s->sys->idct_factor[(j >= 4)*4*16*64 + class1*16*64 + quant*64];
                is_field_mode[mb_index] |= !j && dct_mode;
            } else {
                mb->idct_put     = s->idct_put[dct_mode && log2_blocksize == 3];
                mb->scan_table   = s->dv_zigzag[dct_mode];
                mb->factor_table = &s->sys->idct_factor[(class1 == 3)*2*22*64 + dct_mode*22*64 +
                                                        (quant + ff_dv_quant_offset[class1])*64];
            }
            dc = dc << 2;
            /* convert to unsigned because 128 is not added in the
               standard IDCT */
            dc += 1024;
            block[0] = dc;
            buf_ptr += last_index >> 3;
            mb->pos               = 0;
            mb->partial_bit_count = 0;

            av_dlog(avctx, "MB block: %d, %d ", mb_index, j);
            dv_decode_ac(&gb, mb, block);

            /* write the remaining bits in a new buffer only if the
               block is finished */
            if (mb->pos >= 64)
                bit_copy(&pb, &gb);

            block += 64;
            mb++;
        }

        /* pass 2: we can do it just after */
        av_dlog(avctx, "***pass 2 size=%d MB#=%d\n", put_bits_count(&pb), mb_index);
        block = block1;
        mb    = mb1;
        init_get_bits(&gb, mb_bit_buffer, put_bits_count(&pb));
        put_bits32(&pb, 0); // padding must be zeroed
        flush_put_bits(&pb);
        for (j = 0; j < s->sys->bpm; j++, block += 64, mb++) {
            if (mb->pos < 64 && get_bits_left(&gb) > 0) {
                dv_decode_ac(&gb, mb, block);
                /* if still not finished, no need to parse other blocks */
                if (mb->pos < 64)
                    break;
            }
        }
        /* all blocks are finished, so the extra bytes can be used at
           the video segment level */
        if (j >= s->sys->bpm)
            bit_copy(&vs_pb, &gb);
    }

    /* we need a pass over the whole video segment */
    av_dlog(avctx, "***pass 3 size=%d\n", put_bits_count(&vs_pb));
    block = &sblock[0][0];
    mb    = mb_data;
    init_get_bits(&gb, vs_bit_buffer, put_bits_count(&vs_pb));
    put_bits32(&vs_pb, 0); // padding must be zeroed
    flush_put_bits(&vs_pb);
    for (mb_index = 0; mb_index < 5; mb_index++) {
        for (j = 0; j < s->sys->bpm; j++) {
            if (mb->pos < 64) {
                av_dlog(avctx, "start %d:%d\n", mb_index, j);
                dv_decode_ac(&gb, mb, block);
            }
            if (mb->pos >= 64 && mb->pos < 127)
                av_log(avctx, AV_LOG_ERROR, "AC EOB marker is absent pos=%d\n", mb->pos);
            block += 64;
            mb++;
        }
    }

    /* compute idct and place blocks */
    block = &sblock[0][0];
    mb    = mb_data;
    for (mb_index = 0; mb_index < 5; mb_index++) {
        dv_calculate_mb_xy(s, work_chunk, mb_index, &mb_x, &mb_y);

        /* idct_put'ting luminance */
        if ((s->sys->pix_fmt == AV_PIX_FMT_YUV420P) ||
            (s->sys->pix_fmt == AV_PIX_FMT_YUV411P && mb_x >= (704 / 8)) ||
            (s->sys->height >= 720 && mb_y != 134)) {
            y_stride = (s->frame->linesize[0] << ((!is_field_mode[mb_index]) * log2_blocksize));
        } else {
            y_stride = (2 << log2_blocksize);
        }
        y_ptr = s->frame->data[0] + ((mb_y * s->frame->linesize[0] + mb_x) << log2_blocksize);
        linesize = s->frame->linesize[0] << is_field_mode[mb_index];
        mb[0]    .idct_put(y_ptr                                   , linesize, block + 0*64);
        if (s->sys->video_stype == 4) { /* SD 422 */
            mb[2].idct_put(y_ptr + (1 << log2_blocksize)           , linesize, block + 2*64);
        } else {
            mb[1].idct_put(y_ptr + (1 << log2_blocksize)           , linesize, block + 1*64);
            mb[2].idct_put(y_ptr                         + y_stride, linesize, block + 2*64);
            mb[3].idct_put(y_ptr + (1 << log2_blocksize) + y_stride, linesize, block + 3*64);
        }
        mb += 4;
        block += 4*64;

        /* idct_put'ting chrominance */
        c_offset = (((mb_y >>  (s->sys->pix_fmt == AV_PIX_FMT_YUV420P)) * s->frame->linesize[1] +
                     (mb_x >> ((s->sys->pix_fmt == AV_PIX_FMT_YUV411P) ? 2 : 1))) << log2_blocksize);
        for (j = 2; j; j--) {
            uint8_t *c_ptr = s->frame->data[j] + c_offset;
            if (s->sys->pix_fmt == AV_PIX_FMT_YUV411P && mb_x >= (704 / 8)) {
                  uint64_t aligned_pixels[64/8];
                  uint8_t *pixels = (uint8_t*)aligned_pixels;
                  uint8_t *c_ptr1, *ptr1;
                  int x, y;
                  mb->idct_put(pixels, 8, block);
                  for (y = 0; y < (1 << log2_blocksize); y++, c_ptr += s->frame->linesize[j], pixels += 8) {
                      ptr1   = pixels + (1 << (log2_blocksize - 1));
                      c_ptr1 = c_ptr + (s->frame->linesize[j] << log2_blocksize);
                      for (x = 0; x < (1 << (log2_blocksize - 1)); x++) {
                          c_ptr[x]  = pixels[x];
                          c_ptr1[x] = ptr1[x];
                      }
                  }
                  block += 64; mb++;
            } else {
                  y_stride = (mb_y == 134) ? (1 << log2_blocksize) :
                                             s->frame->linesize[j] << ((!is_field_mode[mb_index]) * log2_blocksize);
                  linesize = s->frame->linesize[j] << is_field_mode[mb_index];
                  (mb++)->    idct_put(c_ptr           , linesize, block); block += 64;
                  if (s->sys->bpm == 8) {
                      (mb++)->idct_put(c_ptr + y_stride, linesize, block); block += 64;
                  }
            }
        }
    }
    return 0;
}

/* NOTE: exactly one frame must be given (120000 bytes for NTSC,
   144000 bytes for PAL - or twice those for 50Mbps) */
static int dvvideo_decode_frame(AVCodecContext *avctx,
                                 void *data, int *got_frame,
                                 AVPacket *avpkt)
{
    uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    DVVideoContext *s = avctx->priv_data;
    const uint8_t* vsc_pack;
    int apt, is16_9, ret;

    s->sys = avpriv_dv_frame_profile(s->sys, buf, buf_size);
    if (!s->sys || buf_size < s->sys->frame_size || ff_dv_init_dynamic_tables(s->sys)) {
        av_log(avctx, AV_LOG_ERROR, "could not find dv frame profile\n");
        return -1; /* NOTE: we only accept several full frames */
    }

    s->frame            = data;
    s->frame->key_frame = 1;
    s->frame->pict_type = AV_PICTURE_TYPE_I;
    avctx->pix_fmt   = s->sys->pix_fmt;
    avctx->time_base = s->sys->time_base;

    ret = ff_set_dimensions(avctx, s->sys->width, s->sys->height);
    if (ret < 0)
        return ret;

    if (ff_get_buffer(avctx, s->frame, 0) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    s->frame->interlaced_frame = 1;
    s->frame->top_field_first  = 0;

    s->buf = buf;
    avctx->execute(avctx, dv_decode_video_segment, s->sys->work_chunks, NULL,
                   dv_work_pool_size(s->sys), sizeof(DVwork_chunk));

    emms_c();

    /* return image */
    *got_frame = 1;

    /* Determine the codec's sample_aspect ratio from the packet */
    vsc_pack = buf + 80*5 + 48 + 5;
    if ( *vsc_pack == dv_video_control ) {
        apt = buf[4] & 0x07;
        is16_9 = (vsc_pack && ((vsc_pack[2] & 0x07) == 0x02 || (!apt && (vsc_pack[2] & 0x07) == 0x07)));
        avctx->sample_aspect_ratio = s->sys->sar[is16_9];
    }

    return s->sys->frame_size;
}

AVCodec ff_dvvideo_decoder = {
    .name           = "dvvideo",
    .long_name      = NULL_IF_CONFIG_SMALL("DV (Digital Video)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_DVVIDEO,
    .priv_data_size = sizeof(DVVideoContext),
    .init           = ff_dvvideo_init,
    .decode         = dvvideo_decode_frame,
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_SLICE_THREADS,
};
