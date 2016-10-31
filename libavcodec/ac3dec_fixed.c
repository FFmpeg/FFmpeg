/*
 * Copyright (c) 2012
 *      MIPS Technologies, Inc., California.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the MIPS Technologies, Inc., nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE MIPS TECHNOLOGIES, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE MIPS TECHNOLOGIES, INC. BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Author:  Stanislav Ocovaj (socovaj@mips.com)
 *
 * AC3 fixed-point decoder for MIPS platforms
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

#define FFT_FLOAT 0
#define USE_FIXED 1
#define FFT_FIXED_32 1
#include "ac3dec.h"


static const int end_freq_inv_tab[8] =
{
    50529027, 44278013, 39403370, 32292987, 27356480, 23729101, 20951060, 18755316
};

static void scale_coefs (
    int32_t *dst,
    const int32_t *src,
    int dynrng,
    int len)
{
    int i, shift, round;
    int16_t mul;
    int temp, temp1, temp2, temp3, temp4, temp5, temp6, temp7;

    mul = (dynrng & 0x1f) + 0x20;
    shift = 4 - ((dynrng << 23) >> 28);
    if (shift > 0 ) {
      round = 1 << (shift-1);
      for (i=0; i<len; i+=8) {

          temp = src[i] * mul;
          temp1 = src[i+1] * mul;
          temp = temp + round;
          temp2 = src[i+2] * mul;

          temp1 = temp1 + round;
          dst[i] = temp >> shift;
          temp3 = src[i+3] * mul;
          temp2 = temp2 + round;

          dst[i+1] = temp1 >> shift;
          temp4 = src[i + 4] * mul;
          temp3 = temp3 + round;
          dst[i+2] = temp2 >> shift;

          temp5 = src[i+5] * mul;
          temp4 = temp4 + round;
          dst[i+3] = temp3 >> shift;
          temp6 = src[i+6] * mul;

          dst[i+4] = temp4 >> shift;
          temp5 = temp5 + round;
          temp7 = src[i+7] * mul;
          temp6 = temp6 + round;

          dst[i+5] = temp5 >> shift;
          temp7 = temp7 + round;
          dst[i+6] = temp6 >> shift;
          dst[i+7] = temp7 >> shift;

      }
    } else {
      shift = -shift;
      for (i=0; i<len; i+=8) {

          temp = src[i] * mul;
          temp1 = src[i+1] * mul;
          temp2 = src[i+2] * mul;

          dst[i] = temp << shift;
          temp3 = src[i+3] * mul;

          dst[i+1] = temp1 << shift;
          temp4 = src[i + 4] * mul;
          dst[i+2] = temp2 << shift;

          temp5 = src[i+5] * mul;
          dst[i+3] = temp3 << shift;
          temp6 = src[i+6] * mul;

          dst[i+4] = temp4 << shift;
          temp7 = src[i+7] * mul;

          dst[i+5] = temp5 << shift;
          dst[i+6] = temp6 << shift;
          dst[i+7] = temp7 << shift;

      }
    }
}

/**
 * Downmix samples from original signal to stereo or mono (this is for 16-bit samples
 * and fixed point decoder - original (for 32-bit samples) is in ac3dsp.c).
 */
static void ac3_downmix_c_fixed16(int16_t **samples, int16_t (*matrix)[2],
                                  int out_ch, int in_ch, int len)
{
    int i, j;
    int v0, v1;
    if (out_ch == 2) {
        for (i = 0; i < len; i++) {
            v0 = v1 = 0;
            for (j = 0; j < in_ch; j++) {
                v0 += samples[j][i] * matrix[j][0];
                v1 += samples[j][i] * matrix[j][1];
            }
            samples[0][i] = (v0+2048)>>12;
            samples[1][i] = (v1+2048)>>12;
        }
    } else if (out_ch == 1) {
        for (i = 0; i < len; i++) {
            v0 = 0;
            for (j = 0; j < in_ch; j++)
                v0 += samples[j][i] * matrix[j][0];
            samples[0][i] = (v0+2048)>>12;
        }
    }
}

#include "eac3dec.c"
#include "ac3dec.c"

static const AVOption options[] = {
    { "drc_scale", "percentage of dynamic range compression to apply", OFFSET(drc_scale), AV_OPT_TYPE_FLOAT, {.dbl = 1.0}, 0.0, 6.0, PAR },
    { "heavy_compr", "enable heavy dynamic range compression", OFFSET(heavy_compression), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, PAR },
    { NULL},
};

static const AVClass ac3_decoder_class = {
    .class_name = "Fixed-Point AC-3 Decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_ac3_fixed_decoder = {
    .name           = "ac3_fixed",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_AC3,
    .priv_data_size = sizeof (AC3DecodeContext),
    .init           = ac3_decode_init,
    .close          = ac3_decode_end,
    .decode         = ac3_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("ATSC A/52A (AC-3)"),
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16P,
                                                      AV_SAMPLE_FMT_NONE },
    .priv_class     = &ac3_decoder_class,
};
