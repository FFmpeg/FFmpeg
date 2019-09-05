/*
 * FFT  transform, optimized with VSX built-in functions
 * Copyright (c) 2014 Rong Yan
 *
 * This algorithm (though not any of the implementation details) is
 * based on libdjbfft by D. J. Bernstein.
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


#include "config.h"
#include "libavutil/cpu.h"
#include "libavutil/ppc/util_altivec.h"
#include "libavcodec/fft.h"
#include "libavcodec/fft-internal.h"
#include "fft_vsx.h"

#if HAVE_VSX

static void fft32_vsx_interleave(FFTComplex *z)
{
    fft16_vsx_interleave(z);
    fft8_vsx_interleave(z+16);
    fft8_vsx_interleave(z+24);
    pass_vsx_interleave(z,ff_cos_32,4);
}

static void fft64_vsx_interleave(FFTComplex *z)
{
    fft32_vsx_interleave(z);
    fft16_vsx_interleave(z+32);
    fft16_vsx_interleave(z+48);
    pass_vsx_interleave(z,ff_cos_64, 8);
}
static void fft128_vsx_interleave(FFTComplex *z)
{
    fft64_vsx_interleave(z);
    fft32_vsx_interleave(z+64);
    fft32_vsx_interleave(z+96);
    pass_vsx_interleave(z,ff_cos_128,16);
}
static void fft256_vsx_interleave(FFTComplex *z)
{
    fft128_vsx_interleave(z);
    fft64_vsx_interleave(z+128);
    fft64_vsx_interleave(z+192);
    pass_vsx_interleave(z,ff_cos_256,32);
}
static void fft512_vsx_interleave(FFTComplex *z)
{
    fft256_vsx_interleave(z);
    fft128_vsx_interleave(z+256);
    fft128_vsx_interleave(z+384);
    pass_vsx_interleave(z,ff_cos_512,64);
}
static void fft1024_vsx_interleave(FFTComplex *z)
{
    fft512_vsx_interleave(z);
    fft256_vsx_interleave(z+512);
    fft256_vsx_interleave(z+768);
    pass_vsx_interleave(z,ff_cos_1024,128);

}
static void fft2048_vsx_interleave(FFTComplex *z)
{
    fft1024_vsx_interleave(z);
    fft512_vsx_interleave(z+1024);
    fft512_vsx_interleave(z+1536);
    pass_vsx_interleave(z,ff_cos_2048,256);
}
static void fft4096_vsx_interleave(FFTComplex *z)
{
    fft2048_vsx_interleave(z);
    fft1024_vsx_interleave(z+2048);
    fft1024_vsx_interleave(z+3072);
    pass_vsx_interleave(z,ff_cos_4096, 512);
}
static void fft8192_vsx_interleave(FFTComplex *z)
{
    fft4096_vsx_interleave(z);
    fft2048_vsx_interleave(z+4096);
    fft2048_vsx_interleave(z+6144);
    pass_vsx_interleave(z,ff_cos_8192,1024);
}
static void fft16384_vsx_interleave(FFTComplex *z)
{
    fft8192_vsx_interleave(z);
    fft4096_vsx_interleave(z+8192);
    fft4096_vsx_interleave(z+12288);
    pass_vsx_interleave(z,ff_cos_16384,2048);
}
static void fft32768_vsx_interleave(FFTComplex *z)
{
    fft16384_vsx_interleave(z);
    fft8192_vsx_interleave(z+16384);
    fft8192_vsx_interleave(z+24576);
    pass_vsx_interleave(z,ff_cos_32768,4096);
}
static void fft65536_vsx_interleave(FFTComplex *z)
{
    fft32768_vsx_interleave(z);
    fft16384_vsx_interleave(z+32768);
    fft16384_vsx_interleave(z+49152);
    pass_vsx_interleave(z,ff_cos_65536,8192);
}

static void fft32_vsx(FFTComplex *z)
{
    fft16_vsx(z);
    fft8_vsx(z+16);
    fft8_vsx(z+24);
    pass_vsx(z,ff_cos_32,4);
}

static void fft64_vsx(FFTComplex *z)
{
    fft32_vsx(z);
    fft16_vsx(z+32);
    fft16_vsx(z+48);
    pass_vsx(z,ff_cos_64, 8);
}
static void fft128_vsx(FFTComplex *z)
{
    fft64_vsx(z);
    fft32_vsx(z+64);
    fft32_vsx(z+96);
    pass_vsx(z,ff_cos_128,16);
}
static void fft256_vsx(FFTComplex *z)
{
    fft128_vsx(z);
    fft64_vsx(z+128);
    fft64_vsx(z+192);
    pass_vsx(z,ff_cos_256,32);
}
static void fft512_vsx(FFTComplex *z)
{
    fft256_vsx(z);
    fft128_vsx(z+256);
    fft128_vsx(z+384);
    pass_vsx(z,ff_cos_512,64);
}
static void fft1024_vsx(FFTComplex *z)
{
    fft512_vsx(z);
    fft256_vsx(z+512);
    fft256_vsx(z+768);
    pass_vsx(z,ff_cos_1024,128);

}
static void fft2048_vsx(FFTComplex *z)
{
    fft1024_vsx(z);
    fft512_vsx(z+1024);
    fft512_vsx(z+1536);
    pass_vsx(z,ff_cos_2048,256);
}
static void fft4096_vsx(FFTComplex *z)
{
    fft2048_vsx(z);
    fft1024_vsx(z+2048);
    fft1024_vsx(z+3072);
    pass_vsx(z,ff_cos_4096, 512);
}
static void fft8192_vsx(FFTComplex *z)
{
    fft4096_vsx(z);
    fft2048_vsx(z+4096);
    fft2048_vsx(z+6144);
    pass_vsx(z,ff_cos_8192,1024);
}
static void fft16384_vsx(FFTComplex *z)
{
    fft8192_vsx(z);
    fft4096_vsx(z+8192);
    fft4096_vsx(z+12288);
    pass_vsx(z,ff_cos_16384,2048);
}
static void fft32768_vsx(FFTComplex *z)
{
    fft16384_vsx(z);
    fft8192_vsx(z+16384);
    fft8192_vsx(z+24576);
    pass_vsx(z,ff_cos_32768,4096);
}
static void fft65536_vsx(FFTComplex *z)
{
    fft32768_vsx(z);
    fft16384_vsx(z+32768);
    fft16384_vsx(z+49152);
    pass_vsx(z,ff_cos_65536,8192);
}

static void (* const fft_dispatch_vsx[])(FFTComplex*) = {
    fft4_vsx, fft8_vsx, fft16_vsx, fft32_vsx, fft64_vsx, fft128_vsx, fft256_vsx, fft512_vsx, fft1024_vsx,
    fft2048_vsx, fft4096_vsx, fft8192_vsx, fft16384_vsx, fft32768_vsx, fft65536_vsx,
};
static void (* const fft_dispatch_vsx_interleave[])(FFTComplex*) = {
    fft4_vsx_interleave, fft8_vsx_interleave, fft16_vsx_interleave, fft32_vsx_interleave, fft64_vsx_interleave,
    fft128_vsx_interleave, fft256_vsx_interleave, fft512_vsx_interleave, fft1024_vsx_interleave,
    fft2048_vsx_interleave, fft4096_vsx_interleave, fft8192_vsx_interleave, fft16384_vsx_interleave, fft32768_vsx_interleave, fft65536_vsx_interleave,
};
void ff_fft_calc_interleave_vsx(FFTContext *s, FFTComplex *z)
{
     fft_dispatch_vsx_interleave[s->nbits-2](z);
}
void ff_fft_calc_vsx(FFTContext *s, FFTComplex *z)
{
     fft_dispatch_vsx[s->nbits-2](z);
}
#endif /* HAVE_VSX */
