/*
 * Copyright (c) 2011 Mina Nagy Zaki
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
 * audio rematrixing functions, based on functions from libavcodec/resample.c
 */

#if defined(FLOATING)
# define DIV2 /2
#else
# define DIV2 >>1
#endif

REMATRIX_FUNC_SIG(stereo_to_mono_packed)
{
    while (nb_samples >= 4) {
        outp[0][0] = (inp[0][0] + inp[0][1]) DIV2;
        outp[0][1] = (inp[0][2] + inp[0][3]) DIV2;
        outp[0][2] = (inp[0][4] + inp[0][5]) DIV2;
        outp[0][3] = (inp[0][6] + inp[0][7]) DIV2;
        outp[0] += 4;
        inp[0]  += 8;
        nb_samples -= 4;
    }
    while (nb_samples--) {
        outp[0][0] = (inp[0][0] + inp[0][1]) DIV2;
        outp[0]++;
        inp[0] += 2;
    }
}

REMATRIX_FUNC_SIG(stereo_downmix_packed)
{
    while (nb_samples--) {
        *outp[0]++ = inp[0][0];
        *outp[0]++ = inp[0][1];
        inp[0] += aconvert->in_nb_channels;
    }
}

REMATRIX_FUNC_SIG(mono_to_stereo_packed)
{
    while (nb_samples >= 4) {
        outp[0][0] = outp[0][1] = inp[0][0];
        outp[0][2] = outp[0][3] = inp[0][1];
        outp[0][4] = outp[0][5] = inp[0][2];
        outp[0][6] = outp[0][7] = inp[0][3];
        outp[0] += 8;
        inp[0]  += 4;
        nb_samples -= 4;
    }
    while (nb_samples--) {
        outp[0][0] = outp[0][1] = inp[0][0];
        outp[0] += 2;
        inp[0]  += 1;
    }
}

/**
 * This is for when we have more than 2 input channels, need to downmix to mono
 * and do not have a conversion formula available.  We just use first two input
 * channels - left and right. This is a placeholder until more conversion
 * functions are written.
 */
REMATRIX_FUNC_SIG(mono_downmix_packed)
{
    while (nb_samples--) {
        outp[0][0] = (inp[0][0] + inp[0][1]) DIV2;
        inp[0] += aconvert->in_nb_channels;
        outp[0]++;
    }
}

REMATRIX_FUNC_SIG(mono_downmix_planar)
{
    FMT_TYPE *out = outp[0];

    while (nb_samples >= 4) {
        out[0] = (inp[0][0] + inp[1][0]) DIV2;
        out[1] = (inp[0][1] + inp[1][1]) DIV2;
        out[2] = (inp[0][2] + inp[1][2]) DIV2;
        out[3] = (inp[0][3] + inp[1][3]) DIV2;
        out    += 4;
        inp[0] += 4;
        inp[1] += 4;
        nb_samples -= 4;
    }
    while (nb_samples--) {
        out[0] = (inp[0][0] + inp[1][0]) DIV2;
        out++;
        inp[0]++;
        inp[1]++;
    }
}

/* Stereo to 5.1 output */
REMATRIX_FUNC_SIG(stereo_to_surround_5p1_packed)
{
    while (nb_samples--) {
      outp[0][0] = inp[0][0];  /* left */
      outp[0][1] = inp[0][1];  /* right */
      outp[0][2] = (inp[0][0] + inp[0][1]) DIV2; /* center */
      outp[0][3] = 0;          /* low freq */
      outp[0][4] = 0;          /* FIXME: left surround: -3dB or -6dB or -9dB of stereo left  */
      outp[0][5] = 0;          /* FIXME: right surroud: -3dB or -6dB or -9dB of stereo right */
      inp[0]  += 2;
      outp[0] += 6;
    }
}

REMATRIX_FUNC_SIG(stereo_to_surround_5p1_planar)
{
    while (nb_samples--) {
      *outp[0]++ = *inp[0];    /* left */
      *outp[1]++ = *inp[1];    /* right */
      *outp[2]++ = (*inp[0] + *inp[1]) DIV2; /* center */
      *outp[3]++ = 0;          /* low freq */
      *outp[4]++ = 0;          /* FIXME: left surround: -3dB or -6dB or -9dB of stereo left  */
      *outp[5]++ = 0;          /* FIXME: right surroud: -3dB or -6dB or -9dB of stereo right */
      inp[0]++; inp[1]++;
    }
}


/*
5.1 to stereo input: [fl, fr, c, lfe, rl, rr]
- Left = front_left + rear_gain * rear_left + center_gain * center
- Right = front_right + rear_gain * rear_right + center_gain * center
Where rear_gain is usually around 0.5-1.0 and
      center_gain is almost always 0.7 (-3 dB)
*/
REMATRIX_FUNC_SIG(surround_5p1_to_stereo_packed)
{
    while (nb_samples--) {
        *outp[0]++ = inp[0][0] + (0.5 * inp[0][4]) + (0.7 * inp[0][2]); //FIXME CLIPPING!
        *outp[0]++ = inp[0][1] + (0.5 * inp[0][5]) + (0.7 * inp[0][2]); //FIXME CLIPPING!

        inp[0] += 6;
    }
}

REMATRIX_FUNC_SIG(surround_5p1_to_stereo_planar)
{
    while (nb_samples--) {
        *outp[0]++ = *inp[0] + (0.5 * *inp[4]) + (0.7 * *inp[2]); //FIXME CLIPPING!
        *outp[1]++ = *inp[1] + (0.5 * *inp[5]) + (0.7 * *inp[2]); //FIXME CLIPPING!

        inp[0]++; inp[1]++; inp[2]++; inp[3]++; inp[4]++; inp[5]++;
    }
}

#undef DIV2
#undef REMATRIX_FUNC_NAME
#undef FMT_TYPE
