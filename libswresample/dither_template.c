/*
 * This file is part of libswresample
 *
 * libswresample is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * libswresample is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libswresample; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#if defined(TEMPLATE_DITHER_DBL)
#    define RENAME(N) N ## _double
#    define DELEM  double
#    define CLIP(v) while(0)

#elif defined(TEMPLATE_DITHER_FLT)
#    define RENAME(N) N ## _float
#    define DELEM  float
#    define CLIP(v) while(0)

#elif defined(TEMPLATE_DITHER_S32)
#    define RENAME(N) N ## _int32
#    define DELEM  int32_t
#    define CLIP(v) v = FFMAX(FFMIN(v, INT32_MAX), INT32_MIN)

#elif defined(TEMPLATE_DITHER_S16)
#    define RENAME(N) N ## _int16
#    define DELEM  int16_t
#    define CLIP(v) v = FFMAX(FFMIN(v, INT16_MAX), INT16_MIN)

#else
ERROR
#endif

void RENAME(swri_noise_shaping)(SwrContext *s, AudioData *dsts, const AudioData *srcs, const AudioData *noises, int count){
    int pos = s->dither.ns_pos;
    int i, j, ch;
    int taps  = s->dither.ns_taps;
    float S   = s->dither.ns_scale;
    float S_1 = s->dither.ns_scale_1;

    av_assert2((taps&3) != 2);
    av_assert2((taps&3) != 3 || s->dither.ns_coeffs[taps] == 0);

    for (ch=0; ch<srcs->ch_count; ch++) {
        const float *noise = ((const float *)noises->ch[ch]) + s->dither.noise_pos;
        const DELEM *src = (const DELEM*)srcs->ch[ch];
        DELEM *dst = (DELEM*)dsts->ch[ch];
        float *ns_errors = s->dither.ns_errors[ch];
        const float *ns_coeffs = s->dither.ns_coeffs;
        pos  = s->dither.ns_pos;
        for (i=0; i<count; i++) {
            double d1, d = src[i]*S_1;
            for(j=0; j<taps-2; j+=4) {
                d -= ns_coeffs[j    ] * ns_errors[pos + j    ]
                    +ns_coeffs[j + 1] * ns_errors[pos + j + 1]
                    +ns_coeffs[j + 2] * ns_errors[pos + j + 2]
                    +ns_coeffs[j + 3] * ns_errors[pos + j + 3];
            }
            if(j < taps)
                d -= ns_coeffs[j] * ns_errors[pos + j];
            pos = pos ? pos - 1 : taps - 1;
            d1 = rint(d + noise[i]);
            ns_errors[pos + taps] = ns_errors[pos] = d1 - d;
            d1 *= S;
            CLIP(d1);
            dst[i] = d1;
        }
    }

    s->dither.ns_pos = pos;
}

#undef RENAME
#undef DELEM
#undef CLIP
