/*
 * This file is part of FFmpeg.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "cuda/vector_helpers.cuh"
#include "vf_scale_cuda.h"

template<typename T>
using subsample_function_t = T (*)(cudaTextureObject_t tex, int xo, int yo,
                                   int dst_width, int dst_height,
                                   int src_left, int src_top,
                                   int src_width, int src_height,
                                   int bit_depth, int src_depth, int storage_max,
                                   float param,
                                   const float *weights, const int *offsets,
                                   int filter_size);

// --- CONVERSION LOGIC ---

static const ushort mask_10bit = 0xFFC0;
static const ushort mask_16bit = 0xFFFF;

static inline __device__ ushort conv_8to16(uchar in, ushort mask, int mpeg_range)
{
    ushort shifted = (ushort)in << 8;
    return mpeg_range ? shifted : ((shifted | ((ushort)in )) & mask);
}

static inline __device__ ushort conv_8to10pl(uchar in, int mpeg_range)
{
    ushort shifted = (ushort)in << 2;
    return mpeg_range ? shifted : (shifted | ((ushort)in >> 6));
}

static inline __device__ uchar conv_16to8(ushort in)
{
    return in >> 8;
}

static inline __device__ uchar conv_10to8(ushort in)
{
    return in >> 8;
}

static inline __device__ uchar conv_10to8pl(ushort in)
{
    return in >> 2;
}

static inline __device__ ushort conv_10to16(ushort in, int mpeg_range)
{
    ushort shifted = (in >> 10);
    return mpeg_range ? in : (in | shifted);
}

static inline __device__ ushort conv_10to16pl(ushort in, int mpeg_range)
{
    ushort shifted = (in << 6);
    return mpeg_range ? shifted : (shifted | (in >> 4));
}

static inline __device__ ushort conv_16to10(ushort in)
{
    return in & mask_10bit;
}

static inline __device__ ushort conv_16to10pl(ushort in)
{
    return in >> 6;
}

#define DEF_F(N, T) \
    template<subsample_function_t<in_T> subsample_func_y,                                      \
             subsample_function_t<in_T_uv> subsample_func_uv>                                  \
    __device__ static inline void N(cudaTextureObject_t src_tex[4], T *dst[4], int xo, int yo, \
                                    int dst_width, int dst_height, int dst_pitch,              \
                                    int src_left, int src_top, int src_width, int src_height,  \
                                    int src_depth, int storage_max, float param, int mpeg_range, \
                                    const float *weights, const int *offsets, int filter_size)

#define SUB_F(m, plane) \
    subsample_func_##m(src_tex[plane], xo, yo, \
                       dst_width, dst_height,  \
                       src_left, src_top,      \
                       src_width, src_height,  \
                       in_bit_depth, src_depth, storage_max, param, \
                       weights, offsets, filter_size)

// FFmpeg passes pitch in bytes, CUDA uses potentially larger types
#define FIXED_PITCH \
    (dst_pitch/sizeof(*dst[0]))

#define DEFAULT_DST(n) \
    dst[n][yo*FIXED_PITCH+xo]

// planar8->X

struct Convert_planar8_planar8
{
    static const int in_bit_depth = 8;
    typedef uchar in_T;
    typedef uchar in_T_uv;
    typedef uchar out_T;
    typedef uchar out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = SUB_F(y, 0);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = SUB_F(uv, 1);
        DEFAULT_DST(2) = SUB_F(uv, 2);
    }
};

struct Convert_planar8_planar10
{
    static const int in_bit_depth = 8;
    typedef uchar in_T;
    typedef uchar in_T_uv;
    typedef ushort out_T;
    typedef ushort out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_8to10pl(SUB_F(y, 0), mpeg_range);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = conv_8to10pl(SUB_F(uv, 1), mpeg_range);
        DEFAULT_DST(2) = conv_8to10pl(SUB_F(uv, 2), mpeg_range);
    }
};

struct Convert_planar8_planar16
{
    static const int in_bit_depth = 8;
    typedef uchar in_T;
    typedef uchar in_T_uv;
    typedef ushort out_T;
    typedef ushort out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_8to16(SUB_F(y, 0), mask_16bit, mpeg_range);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = conv_8to16(SUB_F(uv, 1), mask_16bit, mpeg_range);
        DEFAULT_DST(2) = conv_8to16(SUB_F(uv, 2), mask_16bit, mpeg_range);
    }
};

struct Convert_planar8_semiplanar8
{
    static const int in_bit_depth = 8;
    typedef uchar in_T;
    typedef uchar in_T_uv;
    typedef uchar out_T;
    typedef uchar2 out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = SUB_F(y, 0);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = make_uchar2(
            SUB_F(uv, 1),
            SUB_F(uv, 2)
        );
    }
};

struct Convert_planar8_semiplanar10
{
    static const int in_bit_depth = 8;
    typedef uchar in_T;
    typedef uchar in_T_uv;
    typedef ushort out_T;
    typedef ushort2 out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_8to16(SUB_F(y, 0), mask_10bit, mpeg_range);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = make_ushort2(
            conv_8to16(SUB_F(uv, 1), mask_10bit, mpeg_range),
            conv_8to16(SUB_F(uv, 2), mask_10bit, mpeg_range)
        );
    }
};

struct Convert_planar8_semiplanar16
{
    static const int in_bit_depth = 8;
    typedef uchar in_T;
    typedef uchar in_T_uv;
    typedef ushort out_T;
    typedef ushort2 out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_8to16(SUB_F(y, 0), mask_16bit, mpeg_range);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = make_ushort2(
            conv_8to16(SUB_F(uv, 1), mask_16bit, mpeg_range),
            conv_8to16(SUB_F(uv, 2), mask_16bit, mpeg_range)
        );
    }
};



// planar10->X

struct Convert_planar10_planar8
{
    static const int in_bit_depth = 10;
    typedef ushort in_T;
    typedef ushort in_T_uv;
    typedef uchar out_T;
    typedef uchar out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_10to8pl(SUB_F(y, 0));
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = conv_10to8pl(SUB_F(uv, 1));
        DEFAULT_DST(2) = conv_10to8pl(SUB_F(uv, 2));
    }
};

struct Convert_planar10_planar10
{
    static const int in_bit_depth = 10;
    typedef ushort in_T;
    typedef ushort in_T_uv;
    typedef ushort out_T;
    typedef ushort out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = SUB_F(y, 0);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = SUB_F(uv, 1);
        DEFAULT_DST(2) = SUB_F(uv, 2);
    }
};

struct Convert_planar10_planar16
{
    static const int in_bit_depth = 10;
    typedef ushort in_T;
    typedef ushort in_T_uv;
    typedef ushort out_T;
    typedef ushort out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_10to16pl(SUB_F(y, 0), mpeg_range);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = conv_10to16pl(SUB_F(uv, 1), mpeg_range);
        DEFAULT_DST(2) = conv_10to16pl(SUB_F(uv, 2), mpeg_range);
    }
};

struct Convert_planar10_semiplanar8
{
    static const int in_bit_depth = 10;
    typedef ushort in_T;
    typedef ushort in_T_uv;
    typedef uchar out_T;
    typedef uchar2 out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_10to8pl(SUB_F(y, 0));
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = make_uchar2(
            conv_10to8pl(SUB_F(uv, 1)),
            conv_10to8pl(SUB_F(uv, 2))
        );
    }
};

struct Convert_planar10_semiplanar10
{
    static const int in_bit_depth = 10;
    typedef ushort in_T;
    typedef ushort in_T_uv;
    typedef ushort out_T;
    typedef ushort2 out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = (SUB_F(y, 0) << 6);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = make_ushort2(
            (SUB_F(uv, 1) << 6),
            (SUB_F(uv, 2) << 6)
        );
    }
};

struct Convert_planar10_semiplanar16
{
    static const int in_bit_depth = 10;
    typedef ushort in_T;
    typedef ushort in_T_uv;
    typedef ushort out_T;
    typedef ushort2 out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_10to16pl(SUB_F(y, 0), mpeg_range);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = make_ushort2(
            conv_10to16pl(SUB_F(uv, 1), mpeg_range),
            conv_10to16pl(SUB_F(uv, 2), mpeg_range)
        );
    }
};

// planar16->X

struct Convert_planar16_planar8
{
    static const int in_bit_depth = 16;
    typedef ushort in_T;
    typedef ushort in_T_uv;
    typedef uchar out_T;
    typedef uchar out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_16to8(SUB_F(y, 0));
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = conv_16to8(SUB_F(uv, 1));
        DEFAULT_DST(2) = conv_16to8(SUB_F(uv, 2));
    }
};

struct Convert_planar16_planar10
{
    static const int in_bit_depth = 16;
    typedef ushort in_T;
    typedef ushort in_T_uv;
    typedef ushort out_T;
    typedef ushort out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_16to10pl(SUB_F(y, 0));
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = conv_16to10pl(SUB_F(uv, 1));
        DEFAULT_DST(2) = conv_16to10pl(SUB_F(uv, 2));
    }
};

struct Convert_planar16_planar16
{
    static const int in_bit_depth = 16;
    typedef ushort in_T;
    typedef ushort in_T_uv;
    typedef ushort out_T;
    typedef ushort out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = SUB_F(y, 0);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = SUB_F(uv, 1);
        DEFAULT_DST(2) = SUB_F(uv, 2);
    }
};

struct Convert_planar16_semiplanar8
{
    static const int in_bit_depth = 16;
    typedef ushort in_T;
    typedef ushort in_T_uv;
    typedef uchar out_T;
    typedef uchar2 out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_16to8(SUB_F(y, 0));
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = make_uchar2(
            conv_16to8(SUB_F(uv, 1)),
            conv_16to8(SUB_F(uv, 2))
        );
    }
};

struct Convert_planar16_semiplanar10
{
    static const int in_bit_depth = 16;
    typedef ushort in_T;
    typedef ushort in_T_uv;
    typedef ushort out_T;
    typedef ushort2 out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_16to10(SUB_F(y, 0));
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = make_ushort2(
            conv_16to10(SUB_F(uv, 1)),
            conv_16to10(SUB_F(uv, 2))
        );
    }
};

struct Convert_planar16_semiplanar16
{
    static const int in_bit_depth = 16;
    typedef ushort in_T;
    typedef ushort in_T_uv;
    typedef ushort out_T;
    typedef ushort2 out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = SUB_F(y, 0);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = make_ushort2(
            SUB_F(uv, 1),
            SUB_F(uv, 2)
        );
    }
};

// semiplanar8->X

struct Convert_semiplanar8_planar8
{
    static const int in_bit_depth = 8;
    typedef uchar in_T;
    typedef uchar2 in_T_uv;
    typedef uchar out_T;
    typedef uchar out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = SUB_F(y, 0);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        in_T_uv res = SUB_F(uv, 1);
        DEFAULT_DST(1) = res.x;
        DEFAULT_DST(2) = res.y;
    }
};

struct Convert_semiplanar8_planar10
{
    static const int in_bit_depth = 8;
    typedef uchar in_T;
    typedef uchar2 in_T_uv;
    typedef ushort out_T;
    typedef ushort out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_8to10pl(SUB_F(y, 0), mpeg_range);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        in_T_uv res = SUB_F(uv, 1);
        DEFAULT_DST(1) = conv_8to10pl(res.x, mpeg_range);
        DEFAULT_DST(2) = conv_8to10pl(res.y, mpeg_range);
    }
};

struct Convert_semiplanar8_planar16
{
    static const int in_bit_depth = 8;
    typedef uchar in_T;
    typedef uchar2 in_T_uv;
    typedef ushort out_T;
    typedef ushort out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_8to16(SUB_F(y, 0), mask_16bit, mpeg_range);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        in_T_uv res = SUB_F(uv, 1);
        DEFAULT_DST(1) = conv_8to16(res.x, mask_16bit, mpeg_range);
        DEFAULT_DST(2) = conv_8to16(res.y, mask_16bit, mpeg_range);
    }
};

struct Convert_semiplanar8_semiplanar8
{
    static const int in_bit_depth = 8;
    typedef uchar in_T;
    typedef uchar2 in_T_uv;
    typedef uchar out_T;
    typedef uchar2 out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = SUB_F(y, 0);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = SUB_F(uv, 1);
    }
};

struct Convert_semiplanar8_semiplanar10
{
    static const int in_bit_depth = 8;
    typedef uchar in_T;
    typedef uchar2 in_T_uv;
    typedef ushort out_T;
    typedef ushort2 out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_8to16(SUB_F(y, 0), mask_10bit, mpeg_range);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        in_T_uv res = SUB_F(uv, 1);
        DEFAULT_DST(1) = make_ushort2(
            conv_8to16(res.x, mask_10bit, mpeg_range),
            conv_8to16(res.y, mask_10bit, mpeg_range)
        );
    }
};

struct Convert_semiplanar8_semiplanar16
{
    static const int in_bit_depth = 8;
    typedef uchar in_T;
    typedef uchar2 in_T_uv;
    typedef ushort out_T;
    typedef ushort2 out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_8to16(SUB_F(y, 0), mask_16bit, mpeg_range);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        in_T_uv res = SUB_F(uv, 1);
        DEFAULT_DST(1) = make_ushort2(
            conv_8to16(res.x, mask_16bit, mpeg_range),
            conv_8to16(res.y, mask_16bit, mpeg_range)
        );
    }
};

// semiplanar10->X

struct Convert_semiplanar10_planar8
{
    static const int in_bit_depth = 10;
    typedef ushort in_T;
    typedef ushort2 in_T_uv;
    typedef uchar out_T;
    typedef uchar out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_10to8(SUB_F(y, 0));
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        in_T_uv res = SUB_F(uv, 1);
        DEFAULT_DST(1) = conv_10to8(res.x);
        DEFAULT_DST(2) = conv_10to8(res.y);
    }
};

struct Convert_semiplanar10_planar10
{
    static const int in_bit_depth = 10;
    typedef ushort in_T;
    typedef ushort2 in_T_uv;
    typedef ushort out_T;
    typedef ushort out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = SUB_F(y, 0) >> 6;
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        in_T_uv res = SUB_F(uv, 1);
        DEFAULT_DST(1) = res.x >> 6;
        DEFAULT_DST(2) = res.y >> 6;
    }
};

struct Convert_semiplanar10_planar16
{
    static const int in_bit_depth = 10;
    typedef ushort in_T;
    typedef ushort2 in_T_uv;
    typedef ushort out_T;
    typedef ushort out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_10to16(SUB_F(y, 0), mpeg_range);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        in_T_uv res = SUB_F(uv, 1);
        DEFAULT_DST(1) = conv_10to16(res.x, mpeg_range);
        DEFAULT_DST(2) = conv_10to16(res.y, mpeg_range);
    }
};

struct Convert_semiplanar10_semiplanar8
{
    static const int in_bit_depth = 10;
    typedef ushort in_T;
    typedef ushort2 in_T_uv;
    typedef uchar out_T;
    typedef uchar2 out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_10to8(SUB_F(y, 0));
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        in_T_uv res = SUB_F(uv, 1);
        DEFAULT_DST(1) = make_uchar2(
            conv_10to8(res.x),
            conv_10to8(res.y)
        );
    }
};

struct Convert_semiplanar10_semiplanar10
{
    static const int in_bit_depth = 10;
    typedef ushort in_T;
    typedef ushort2 in_T_uv;
    typedef ushort out_T;
    typedef ushort2 out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = SUB_F(y, 0);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = SUB_F(uv, 1);
    }
};

struct Convert_semiplanar10_semiplanar16
{
    static const int in_bit_depth = 10;
    typedef ushort in_T;
    typedef ushort2 in_T_uv;
    typedef ushort out_T;
    typedef ushort2 out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_10to16(SUB_F(y, 0), mpeg_range);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        in_T_uv res = SUB_F(uv, 1);
        DEFAULT_DST(1) = make_ushort2(
            conv_10to16(res.x, mpeg_range),
            conv_10to16(res.y, mpeg_range)
        );
    }
};


// semiplanar16->X

struct Convert_semiplanar16_planar8
{
    static const int in_bit_depth = 16;
    typedef ushort in_T;
    typedef ushort2 in_T_uv;
    typedef uchar out_T;
    typedef uchar out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_16to8(SUB_F(y, 0));
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        in_T_uv res = SUB_F(uv, 1);
        DEFAULT_DST(1) = conv_16to8(res.x);
        DEFAULT_DST(2) = conv_16to8(res.y);
    }
};

struct Convert_semiplanar16_planar10
{
    static const int in_bit_depth = 16;
    typedef ushort in_T;
    typedef ushort2 in_T_uv;
    typedef ushort out_T;
    typedef ushort out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_16to10pl(SUB_F(y, 0));
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        in_T_uv res = SUB_F(uv, 1);
        DEFAULT_DST(1) = conv_16to10pl(res.x);
        DEFAULT_DST(2) = conv_16to10pl(res.y);
    }
};

struct Convert_semiplanar16_planar16
{
    static const int in_bit_depth = 16;
    typedef ushort in_T;
    typedef ushort2 in_T_uv;
    typedef ushort out_T;
    typedef ushort out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = SUB_F(y, 0);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        in_T_uv res = SUB_F(uv, 1);
        DEFAULT_DST(1) = res.x;
        DEFAULT_DST(2) = res.y;
    }
};

struct Convert_semiplanar16_semiplanar8
{
    static const int in_bit_depth = 16;
    typedef ushort in_T;
    typedef ushort2 in_T_uv;
    typedef uchar out_T;
    typedef uchar2 out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_16to8(SUB_F(y, 0));
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        in_T_uv res = SUB_F(uv, 1);
        DEFAULT_DST(1) = make_uchar2(
            conv_16to8(res.x),
            conv_16to8(res.y)
        );
    }
};

struct Convert_semiplanar16_semiplanar10
{
    static const int in_bit_depth = 16;
    typedef ushort in_T;
    typedef ushort2 in_T_uv;
    typedef ushort out_T;
    typedef ushort2 out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = conv_16to10(SUB_F(y, 0));
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        in_T_uv res = SUB_F(uv, 1);
        DEFAULT_DST(1) = make_ushort2(
            conv_16to10(res.x),
            conv_16to10(res.y)
        );
    }
};

struct Convert_semiplanar16_semiplanar16
{
    static const int in_bit_depth = 16;
    typedef ushort in_T;
    typedef ushort2 in_T_uv;
    typedef ushort out_T;
    typedef ushort2 out_T_uv;

    DEF_F(Convert, out_T)
    {
        DEFAULT_DST(0) = SUB_F(y, 0);
    }

    DEF_F(Convert_uv, out_T_uv)
    {
        DEFAULT_DST(1) = SUB_F(uv, 1);
    }
};

#define DEF_CONVERT_IDENTITY(fmt1, fmt2)\
                                        \
struct Convert_##fmt1##_##fmt2          \
{                                       \
    static const int in_bit_depth = 8;  \
    typedef uchar4 in_T;                \
    typedef uchar in_T_uv;              \
    typedef uchar4 out_T;               \
    typedef uchar out_T_uv;             \
                                        \
    DEF_F(Convert, out_T)               \
    {                                   \
        DEFAULT_DST(0) = SUB_F(y, 0);   \
    }                                   \
                                        \
    DEF_F(Convert_uv, out_T_uv)         \
    {                                   \
    }                                   \
};                                      \

#define DEF_CONVERT_REORDER(fmt1, fmt2) \
                                        \
struct Convert_##fmt1##_##fmt2          \
{                                       \
    static const int in_bit_depth = 8;  \
    typedef uchar4 in_T;                \
    typedef uchar in_T_uv;              \
    typedef uchar4 out_T;               \
    typedef uchar out_T_uv;             \
                                        \
    DEF_F(Convert, out_T)               \
    {                                   \
        uchar4 res = SUB_F(y, 0);       \
        DEFAULT_DST(0) = make_uchar4(   \
            res.z,                      \
            res.y,                      \
            res.x,                      \
            res.w                       \
        );                              \
    }                                   \
                                        \
    DEF_F(Convert_uv, out_T_uv)         \
    {                                   \
    }                                   \
};                                      \

#define DEF_CONVERT_RGB(fmt1, fmt2)     \
                                        \
DEF_CONVERT_IDENTITY(fmt1, fmt1)        \
DEF_CONVERT_REORDER (fmt1, fmt2)        \
DEF_CONVERT_REORDER (fmt2, fmt1)        \
DEF_CONVERT_IDENTITY(fmt2, fmt2)

DEF_CONVERT_RGB(rgb0, bgr0)
DEF_CONVERT_RGB(rgba, bgra)
DEF_CONVERT_IDENTITY(rgba, rgb0)
DEF_CONVERT_IDENTITY(bgra, bgr0)
DEF_CONVERT_REORDER(rgba, bgr0)
DEF_CONVERT_REORDER(bgra, rgb0)

struct Convert_bgr0_bgra
{
    static const int in_bit_depth = 8;
    typedef uchar4 in_T;
    typedef uchar in_T_uv;
    typedef uchar4 out_T;
    typedef uchar out_T_uv;

    DEF_F(Convert, out_T)
    {
        uchar4 res = SUB_F(y, 0);
        DEFAULT_DST(0) = make_uchar4(
            res.x,
            res.y,
            res.z,
            0xFF
        );
    }

    DEF_F(Convert_uv, out_T_uv)
    {
    }
};

struct Convert_bgr0_rgba
{
    static const int in_bit_depth = 8;
    typedef uchar4 in_T;
    typedef uchar in_T_uv;
    typedef uchar4 out_T;
    typedef uchar out_T_uv;

    DEF_F(Convert, out_T)
    {
        uchar4 res = SUB_F(y, 0);
        DEFAULT_DST(0) = make_uchar4(
            res.z,
            res.y,
            res.x,
            0xFF
        );
    }

    DEF_F(Convert_uv, out_T_uv)
    {
    }
};

struct Convert_rgb0_bgra
{
    static const int in_bit_depth = 8;
    typedef uchar4 in_T;
    typedef uchar in_T_uv;
    typedef uchar4 out_T;
    typedef uchar out_T_uv;

    DEF_F(Convert, out_T)
    {
        uchar4 res = SUB_F(y, 0);
        DEFAULT_DST(0) = make_uchar4(
            res.z,
            res.y,
            res.x,
            0xFF
        );
    }

    DEF_F(Convert_uv, out_T_uv)
    {
    }
};

struct Convert_rgb0_rgba
{
    static const int in_bit_depth = 8;
    typedef uchar4 in_T;
    typedef uchar in_T_uv;
    typedef uchar4 out_T;
    typedef uchar out_T_uv;

    DEF_F(Convert, out_T)
    {
        uchar4 res = SUB_F(y, 0);
        DEFAULT_DST(0) = make_uchar4(
            res.x,
            res.y,
            res.z,
            0xFF
        );
    }

    DEF_F(Convert_uv, out_T_uv)
    {
    }
};

// --- SCALING LOGIC ---

typedef float4 (*coeffs_function_t)(float, float);

__device__ static inline float4 lanczos_coeffs(float x, float param)
{
    const float pi = 3.141592654f;

    float4 res = make_float4(
        pi * (x + 1),
        pi * x,
        pi * (x - 1),
        pi * (x - 2));

    res.x = res.x == 0.0f ? 1.0f :
        __sinf(res.x) * __sinf(res.x / 2.0f) / (res.x * res.x / 2.0f);
    res.y = res.y == 0.0f ? 1.0f :
        __sinf(res.y) * __sinf(res.y / 2.0f) / (res.y * res.y / 2.0f);
    res.z = res.z == 0.0f ? 1.0f :
        __sinf(res.z) * __sinf(res.z / 2.0f) / (res.z * res.z / 2.0f);
    res.w = res.w == 0.0f ? 1.0f :
        __sinf(res.w) * __sinf(res.w / 2.0f) / (res.w * res.w / 2.0f);

    return res / (res.x + res.y + res.z + res.w);
}

__device__ static inline float4 bicubic_coeffs(float x, float param)
{
    const float A = param == SCALE_CUDA_PARAM_DEFAULT ? 0.0f : -param;

    float4 res;
    res.x = ((A * (x + 1) - 5 * A) * (x + 1) + 8 * A) * (x + 1) - 4 * A;
    res.y = ((A + 2) * x - (A + 3)) * x * x + 1;
    res.z = ((A + 2) * (1 - x) - (A + 3)) * (1 - x) * (1 - x) + 1;
    res.w = 1.0f - res.x - res.y - res.z;

    return res;
}

template<typename V>
__device__ static inline V apply_coeffs(float4 coeffs, V c0, V c1, V c2, V c3)
{
    V res = c0 * coeffs.x;
    res  += c1 * coeffs.y;
    res  += c2 * coeffs.z;
    res  += c3 * coeffs.w;

    return res;
}

template<typename T>
__device__ static inline T Subsample_Nearest(cudaTextureObject_t tex,
                                             int xo, int yo,
                                             int dst_width, int dst_height,
                                             int src_left, int src_top,
                                             int src_width, int src_height,
                                             int bit_depth, int src_depth, int storage_max,
                                             float param,
                                             const float *weights, const int *offsets,
                                             int filter_size)
{
    float hscale = (float)src_width / (float)dst_width;
    float vscale = (float)src_height / (float)dst_height;
    float xi = (xo + 0.5f) * hscale + src_left;
    float yi = (yo + 0.5f) * vscale + src_top;

    return tex2D<T>(tex, xi, yi);
}

__device__ static inline float clamp_f(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

/* Half away from zero, then clamp. A bare cast truncates. */
__device__ static inline float round_code(float x, float lo, float hi)
{
    return clamp_f(x + 0.5f, lo, hi);
}

/* Stored samples enter the 8-bit matrix domain.
   Limited codes are the 8-bit codes shifted by depth-8. Full-range
   codes are scaled by the code maximum, so 1023 is 255 and the
   chroma midpoint stays 128. P010 keeps that code in the high bits. */
__device__ static inline void yuv_codes_to_u8(float raw_y, float raw_u, float raw_v,
                                              int depth, int storage_max, int mpeg_range,
                                              float &fy, float &fu, float &fv)
{
    if (depth <= 8) {
        fy = raw_y;
        fu = raw_u;
        fv = raw_v;
    } else if (mpeg_range) {
        float code_div = (float)(1 << (depth - 8)) *
                         ((float)storage_max / (float)((1 << depth) - 1));
        fy = raw_y / code_div;
        fu = raw_u / code_div;
        fv = raw_v / code_div;
    } else {
        float peak = (float)((1 << depth) - 1);
        float mid = (float)(1 << (depth - 1));
        float to_code = peak / (float)storage_max;

        fy = raw_y * 255.f / (float)storage_max;
        fu = (raw_u * to_code - mid) * (255.f / peak) + 128.f;
        fv = (raw_v * to_code - mid) * (255.f / peak) + 128.f;
    }
    if (mpeg_range)
        fy -= 16.f;
    fu -= 128.f;
    fv -= 128.f;
}

/* 8-bit matrix result back to a stored code. The shift clears padding.
   Full-range luma maps 255 onto the code maximum. Full-range chroma
   keeps the midpoint (128 -> 512 at 10-bit), which a plain multiply
   by 1023/255 would move. */
__device__ static inline ushort u8_to_stored(float code8, int depth, int shift,
                                            int mpeg_range, int chroma)
{
    float peak = (float)((1 << depth) - 1);
    float code;

    if (mpeg_range)
        code = code8 * (float)(1 << (depth - 8));
    else if (chroma)
        code = (code8 - 128.f) * peak / 255.f + (float)(1 << (depth - 1));
    else
        code = code8 * peak / 255.f;
    return (ushort)round_code(code, 0.f, peak) << shift;
}

// YUV to RGB CONVERSION
//      R = m[0][0]*(Y-Yoff) + m[0][1]*(U-128) + m[0][2]*(V-128)
//      G = m[1][0]*(Y-Yoff) + m[1][1]*(U-128) + m[1][2]*(V-128)
//      B = m[2][0]*(Y-Yoff) + m[2][1]*(U-128) + m[2][2]*(V-128)
// channel_order:
//      0 = BGRA (B,G,R,A), 1 = RGBA (R,G,B,A),
//      2 = BGR0 (B,G,R,0), 3 = RGB0 (R,G,B,0)
__device__ static inline void store_rgb(uchar4 *dst, float r, float g, float b,
                                        int channel_order)
{
    uchar rc = (uchar)round_code(r, 0.0f, 255.0f);
    uchar gc = (uchar)round_code(g, 0.0f, 255.0f);
    uchar bc = (uchar)round_code(b, 0.0f, 255.0f);

    switch (channel_order) {
    case 0: *dst = make_uchar4(bc, gc, rc, 0xFF); break; // BGRA
    case 1: *dst = make_uchar4(rc, gc, bc, 0xFF); break; // RGBA
    case 2: *dst = make_uchar4(bc, gc, rc, 0x00); break; // BGR0
    case 3: *dst = make_uchar4(rc, gc, bc, 0x00); break; // RGB0
    default: *dst = make_uchar4(bc, gc, rc, 0xFF); break;
    }
}

/* Float intermediate for scaling after conversion: normalized like the
 * texture reads of the generic scaling kernels, and left unclamped so that
 * the scaler rounds and clamps only once. */
__device__ static inline void store_rgb(float4 *dst, float r, float g, float b,
                                        int channel_order)
{
    const float n = 1.0f / 255.0f;
    const float a = channel_order < 2 ? 1.0f : 0.0f;

    switch (channel_order) {
    case 0: case 2: *dst = make_float4(b * n, g * n, r * n, a); break; // BGRA, BGR0
    default:        *dst = make_float4(r * n, g * n, b * n, a); break; // RGBA, RGB0
    }
}

template<typename out_T>
__device__ static inline void yuv8_to_rgba_generic(
    out_T *dst, float fy, float fu, float fv,
    const CUDAScaleColorMatrix &mat,
    int channel_order)
{
    float r = mat.m[0][0] * fy + mat.m[0][1] * fu + mat.m[0][2] * fv;
    float g = mat.m[1][0] * fy + mat.m[1][1] * fu + mat.m[1][2] * fv;
    float b = mat.m[2][0] * fy + mat.m[2][1] * fu + mat.m[2][2] * fv;

    store_rgb(dst, r, g, b, channel_order);
}

// YUV to RGB kernel for semiplanar input (NV12, P010, NV16, P210)
// No resize: src and dst have identical dimensions.
template<typename in_y_T, typename in_uv_T, int channel_order, typename out_T = uchar4>
__device__ void YuvToRgb_semiplanar(CUDAScaleKernelParams params)
{
    int xo = blockIdx.x * blockDim.x + threadIdx.x;
    int yo = blockIdx.y * blockDim.y + threadIdx.y;
    if (yo >= params.dst_height || xo >= params.dst_width) return;

    // Direct 1:1 Y plane read
    in_y_T y_raw = tex2D<in_y_T>(
        params.src_tex[0],
        xo + params.src_left + 0.5f,
        yo + params.src_top  + 0.5f);

    // Chroma: map luma coords to chroma coords for subsampled formats
    float cx = ((float)xo + 0.5f) / (float)(1 << params.log2_chroma_w)
             + (float)(params.src_left >> params.log2_chroma_w);
    float cy = ((float)yo + 0.5f) / (float)(1 << params.log2_chroma_h)
             + (float)(params.src_top  >> params.log2_chroma_h);
    in_uv_T uv_raw = tex2D<in_uv_T>(params.src_tex[1], cx, cy);

    float fy, fu, fv;
    yuv_codes_to_u8((float)(int)y_raw, (float)(int)uv_raw.x, (float)(int)uv_raw.y,
                    params.src_depth, params.src_storage_max, params.mpeg_range,
                    fy, fu, fv);

    out_T *dst = (out_T*)params.dst[0];
    int dst_pitch = params.dst_pitch / sizeof(out_T);
    yuv8_to_rgba_generic(&dst[yo * dst_pitch + xo], fy, fu, fv,
                         params.color_matrix, channel_order);
}

// YUV to RGB kernel for planar input (YUV420P, YUV422P, YUV444P, etc)
// No resize: src and dst have identical dimensions.
template<typename in_y_T, typename in_uv_T, int channel_order, typename out_T = uchar4>
__device__ void YuvToRgb_planar(CUDAScaleKernelParams params)
{
    int xo = blockIdx.x * blockDim.x + threadIdx.x;
    int yo = blockIdx.y * blockDim.y + threadIdx.y;
    if (yo >= params.dst_height || xo >= params.dst_width) return;

    // Direct 1:1 Y plane read
    in_y_T y_raw = tex2D<in_y_T>(
        params.src_tex[0],
        xo + params.src_left + 0.5f,
        yo + params.src_top  + 0.5f);

    // Chroma: map luma coords to chroma coords for subsampled formats
    float cx = ((float)xo + 0.5f) / (float)(1 << params.log2_chroma_w)
             + (float)(params.src_left >> params.log2_chroma_w);
    float cy = ((float)yo + 0.5f) / (float)(1 << params.log2_chroma_h)
             + (float)(params.src_top  >> params.log2_chroma_h);

    in_uv_T u_raw = tex2D<in_uv_T>(params.src_tex[1], cx, cy);
    in_uv_T v_raw = tex2D<in_uv_T>(params.src_tex[2], cx, cy);

    float fy, fu, fv;
    yuv_codes_to_u8((float)(int)y_raw, (float)(int)u_raw, (float)(int)v_raw,
                    params.src_depth, params.src_storage_max, params.mpeg_range,
                    fy, fu, fv);

    out_T *dst = (out_T*)params.dst[0];
    int dst_pitch = params.dst_pitch / sizeof(out_T);
    yuv8_to_rgba_generic(&dst[yo * dst_pitch + xo], fy, fu, fv,
                         params.color_matrix, channel_order);
}

// RGB to YUV
//
//   Y = m[0][0]*R + m[0][1]*G + m[0][2]*B  (+ 16 for limited range)
//   U = m[1][0]*R + m[1][1]*G + m[1][2]*B  (+ 128)
//   V = m[2][0]*R + m[2][1]*G + m[2][2]*B  (+ 128)

// Extract R,G,B from a packed pixel based on channel ordering.
// channel_order: 0 = BGRA (x=B,y=G,z=R), 1 = RGBA (x=R,y=G,z=B),
//                2 = BGR0 (x=B,y=G,z=R), 3 = RGB0 (x=R,y=G,z=B)
__device__ static inline void extract_rgb(float x, float y, float z,
                                          int channel_order,
                                          float &r, float &g, float &b)
{
    switch (channel_order) {
    case 0: // BGRA
    case 2: // BGR0
        b = x; g = y; r = z;
        break;
    case 1: // RGBA
    case 3: // RGB0
    default:
        r = x; g = y; b = z;
        break;
    }
}

__device__ static inline void extract_rgb(uchar4 pixel, int channel_order,
                                          float &r, float &g, float &b)
{
    extract_rgb((float)pixel.x, (float)pixel.y, (float)pixel.z,
                channel_order, r, g, b);
}

/* Normalized float intermediate produced by scaling before conversion. */
__device__ static inline void extract_rgb(float4 pixel, int channel_order,
                                          float &r, float &g, float &b)
{
    extract_rgb(pixel.x * 255.0f, pixel.y * 255.0f, pixel.z * 255.0f,
                channel_order, r, g, b);
}

__device__ static inline float rgb_to_y(float r, float g, float b,
                                        const CUDAScaleColorMatrix &mat, int mpeg_range)
{
    float y = mat.m[0][0] * r + mat.m[0][1] * g + mat.m[0][2] * b;
    if (mpeg_range) y += 16.0f;
    return y;
}

/* Like luma, chroma is left unclamped, so that subsampling averages the
 * exact values; the stores clamp at destination precision. */
__device__ static inline float rgb_to_u(float r, float g, float b,
                                        const CUDAScaleColorMatrix &mat)
{
    return mat.m[1][0] * r + mat.m[1][1] * g + mat.m[1][2] * b + 128.0f;
}

__device__ static inline float rgb_to_v(float r, float g, float b,
                                        const CUDAScaleColorMatrix &mat)
{
    return mat.m[2][0] * r + mat.m[2][1] * g + mat.m[2][2] * b + 128.0f;
}

// RGB->YUV Y kernel (8-bit output)
template<typename out_y_T, int channel_order, typename in_T = uchar4>
__device__ void RgbToYuv_Y(CUDAScaleKernelParams params)
{
    int xo = blockIdx.x * blockDim.x + threadIdx.x;
    int yo = blockIdx.y * blockDim.y + threadIdx.y;
    if (yo >= params.dst_height || xo >= params.dst_width) return;

    in_T pixel = tex2D<in_T>(
        params.src_tex[0],
        xo + params.src_left + 0.5f,
        yo + params.src_top  + 0.5f);

    float r, g, b;
    extract_rgb(pixel, channel_order, r, g, b);
    float y = rgb_to_y(r, g, b, params.color_matrix, params.mpeg_range);

    out_y_T *dst_y = (out_y_T*)params.dst[0];
    int pitch = params.dst_pitch / sizeof(out_y_T);
    dst_y[yo * pitch + xo] = (out_y_T)round_code(y, 0.0f, 255.0f);
}

// RGB->YUV Y kernel for semiplanar10/16 output
template<int channel_order, typename in_T = uchar4>
__device__ void RgbToYuv_Y_semiplanar16(CUDAScaleKernelParams params)
{
    int xo = blockIdx.x * blockDim.x + threadIdx.x;
    int yo = blockIdx.y * blockDim.y + threadIdx.y;
    if (yo >= params.dst_height || xo >= params.dst_width) return;

    in_T pixel = tex2D<in_T>(
        params.src_tex[0],
        xo + params.src_left + 0.5f,
        yo + params.src_top  + 0.5f);

    float r, g, b;
    extract_rgb(pixel, channel_order, r, g, b);
    float y = rgb_to_y(r, g, b, params.color_matrix, params.mpeg_range);

    ushort *dst_y = (ushort*)params.dst[0];
    int pitch = params.dst_pitch / sizeof(ushort);
    dst_y[yo * pitch + xo] = u8_to_stored(y, params.dst_depth, params.dst_shift,
                                         params.mpeg_range, 0);
}

// RGB->YUV Y kernel for planar10 output: the luma plane is stored like the
// semiplanar one, with dst_depth and dst_shift describing the code.
template<int channel_order, typename in_T = uchar4>
__device__ void RgbToYuv_Y_planar10(CUDAScaleKernelParams params)
{
    RgbToYuv_Y_semiplanar16<channel_order, in_T>(params);
}

// RGB->YUV UV kernel for semiplanar output (NV12, NV16)
template<typename out_uv_T, int channel_order, typename in_T = uchar4>
__device__ void RgbToYuv_semiplanar_UV(CUDAScaleKernelParams params)
{
    int xo = blockIdx.x * blockDim.x + threadIdx.x;
    int yo = blockIdx.y * blockDim.y + threadIdx.y;
    if (yo >= params.dst_height || xo >= params.dst_width) return;

    int chroma_w = 1 << params.log2_chroma_w;
    int chroma_h = 1 << params.log2_chroma_h;

    float u_acc = 0.0f, v_acc = 0.0f;
    int count = 0;
    for (int dy = 0; dy < chroma_h; dy++) {
        for (int dx = 0; dx < chroma_w; dx++) {
            int lx = xo * chroma_w + dx;
            int ly = yo * chroma_h + dy;
            if (lx >= params.src_width || ly >= params.src_height) continue;

            in_T pixel = tex2D<in_T>(
                params.src_tex[0],
                lx + params.src_left + 0.5f,
                ly + params.src_top  + 0.5f);

            float r, g, b;
            extract_rgb(pixel, channel_order, r, g, b);
            u_acc += rgb_to_u(r, g, b, params.color_matrix);
            v_acc += rgb_to_v(r, g, b, params.color_matrix);
            count++;
        }
    }

    float u_avg = u_acc / (float)count;
    float v_avg = v_acc / (float)count;

    out_uv_T *dst_uv = (out_uv_T*)params.dst[1];
    int pitch = params.dst_pitch / sizeof(out_uv_T);
    dst_uv[yo * pitch + xo] = make_uchar2((uchar)round_code(u_avg, 0.0f, 255.0f),
                                         (uchar)round_code(v_avg, 0.0f, 255.0f));
}

// RGB->YUV UV kernel for semiplanar10/16 output
template<int channel_order, typename in_T = uchar4>
__device__ void RgbToYuv_semiplanar16_UV(CUDAScaleKernelParams params)
{
    int xo = blockIdx.x * blockDim.x + threadIdx.x;
    int yo = blockIdx.y * blockDim.y + threadIdx.y;
    if (yo >= params.dst_height || xo >= params.dst_width) return;

    int chroma_w = 1 << params.log2_chroma_w;
    int chroma_h = 1 << params.log2_chroma_h;

    float u_acc = 0.0f, v_acc = 0.0f;
    int count = 0;
    for (int dy = 0; dy < chroma_h; dy++) {
        for (int dx = 0; dx < chroma_w; dx++) {
            int lx = xo * chroma_w + dx;
            int ly = yo * chroma_h + dy;
            if (lx >= params.src_width || ly >= params.src_height) continue;

            in_T pixel = tex2D<in_T>(
                params.src_tex[0],
                lx + params.src_left + 0.5f,
                ly + params.src_top  + 0.5f);

            float r, g, b;
            extract_rgb(pixel, channel_order, r, g, b);
            u_acc += rgb_to_u(r, g, b, params.color_matrix);
            v_acc += rgb_to_v(r, g, b, params.color_matrix);
            count++;
        }
    }

    float u_avg = u_acc / (float)count;
    float v_avg = v_acc / (float)count;

    ushort2 *dst_uv = (ushort2*)params.dst[1];
    int pitch = params.dst_pitch / sizeof(ushort2);
    ushort u_val = u8_to_stored(u_avg, params.dst_depth, params.dst_shift,
                               params.mpeg_range, 1);
    ushort v_val = u8_to_stored(v_avg, params.dst_depth, params.dst_shift,
                               params.mpeg_range, 1);
    dst_uv[yo * pitch + xo] = make_ushort2(u_val, v_val);
}

// RGB->YUV UV kernel for planar output (YUV420P, YUV422P, YUV444P, YUV420P10)
template<typename out_uv_T, int channel_order, typename in_T = uchar4>
__device__ void RgbToYuv_planar_UV(CUDAScaleKernelParams params)
{
    int xo = blockIdx.x * blockDim.x + threadIdx.x;
    int yo = blockIdx.y * blockDim.y + threadIdx.y;
    if (yo >= params.dst_height || xo >= params.dst_width) return;

    int chroma_w = 1 << params.log2_chroma_w;
    int chroma_h = 1 << params.log2_chroma_h;

    float u_acc = 0.0f, v_acc = 0.0f;
    int count = 0;
    for (int dy = 0; dy < chroma_h; dy++) {
        for (int dx = 0; dx < chroma_w; dx++) {
            int lx = xo * chroma_w + dx;
            int ly = yo * chroma_h + dy;
            if (lx >= params.src_width || ly >= params.src_height) continue;

            in_T pixel = tex2D<in_T>(
                params.src_tex[0],
                lx + params.src_left + 0.5f,
                ly + params.src_top  + 0.5f);

            float r, g, b;
            extract_rgb(pixel, channel_order, r, g, b);
            u_acc += rgb_to_u(r, g, b, params.color_matrix);
            v_acc += rgb_to_v(r, g, b, params.color_matrix);
            count++;
        }
    }

    float u_avg = u_acc / (float)count;
    float v_avg = v_acc / (float)count;

    out_uv_T *dst_u = (out_uv_T*)params.dst[1];
    out_uv_T *dst_v = (out_uv_T*)params.dst[2];
    int pitch = params.dst_pitch / sizeof(out_uv_T);

    if (sizeof(out_uv_T) == 1) {
        dst_u[yo * pitch + xo] = (out_uv_T)round_code(u_avg, 0.0f, 255.0f);
        dst_v[yo * pitch + xo] = (out_uv_T)round_code(v_avg, 0.0f, 255.0f);
    } else {
        dst_u[yo * pitch + xo] = (out_uv_T)u8_to_stored(u_avg, params.dst_depth,
                                                       params.dst_shift, params.mpeg_range, 1);
        dst_v[yo * pitch + xo] = (out_uv_T)u8_to_stored(v_avg, params.dst_depth,
                                                       params.dst_shift, params.mpeg_range, 1);
    }
}

template<typename T>
__device__ static inline T Subsample_Bilinear(cudaTextureObject_t tex,
                                              int xo, int yo,
                                              int dst_width, int dst_height,
                                              int src_left, int src_top,
                                              int src_width, int src_height,
                                              int bit_depth, int src_depth, int storage_max,
                                              float param,
                                              const float *weights, const int *offsets,
                                              int filter_size)
{
    float hscale = (float)src_width / (float)dst_width;
    float vscale = (float)src_height / (float)dst_height;
    float xi = (xo + 0.5f) * hscale + src_left;
    float yi = (yo + 0.5f) * vscale + src_top;
    // 3-tap filter weights are {wh,1.0,wh} and {wv,1.0,wv}
    float wh = min(max(0.5f * (hscale - 1.0f), 0.0f), 1.0f);
    float wv = min(max(0.5f * (vscale - 1.0f), 0.0f), 1.0f);
    // Convert weights to two bilinear weights -> {wh,1.0,wh} -> {wh,0.5,0} + {0,0.5,wh}
    float dx = wh / (0.5f + wh);
    float dy = wv / (0.5f + wv);

    intT r;
    vec_set_scalar(r, 2);
    r += tex2D<T>(tex, xi - dx, yi - dy);
    r += tex2D<T>(tex, xi + dx, yi - dy);
    r += tex2D<T>(tex, xi - dx, yi + dy);
    r += tex2D<T>(tex, xi + dx, yi + dy);

    T res;
    vec_set(res, r >> 2);

    return res;
}

template<typename T, coeffs_function_t coeffs_function>
__device__ static inline T Subsample_Bicubic(cudaTextureObject_t tex,
                                             int xo, int yo,
                                             int dst_width, int dst_height,
                                             int src_left, int src_top,
                                             int src_width, int src_height,
                                             int bit_depth, int src_depth, int storage_max,
                                             float param,
                                             const float *weights, const int *offsets,
                                             int filter_size)
{
    float hscale = (float)src_width / (float)dst_width;
    float vscale = (float)src_height / (float)dst_height;
    float xi = (xo + 0.5f) * hscale - 0.5f + src_left;
    float yi = (yo + 0.5f) * vscale - 0.5f + src_top;
    float px = floor(xi);
    float py = floor(yi);
    float fx = xi - px;
    float fy = yi - py;

    float factor = bit_depth > 8 ? 0xFFFF : 0xFF;

    float4 coeffsX = coeffs_function(fx, param);
    float4 coeffsY = coeffs_function(fy, param);

#define PIX(x, y) tex2D<floatT>(tex, (x), (y))

    return from_floatN<T, floatT>(
        saturate_rintf(
            apply_coeffs<floatT>(coeffsY,
                apply_coeffs<floatT>(coeffsX, PIX(px - 1, py - 1), PIX(px, py - 1), PIX(px + 1, py - 1), PIX(px + 2, py - 1)),
                apply_coeffs<floatT>(coeffsX, PIX(px - 1, py    ), PIX(px, py    ), PIX(px + 1, py    ), PIX(px + 2, py    )),
                apply_coeffs<floatT>(coeffsX, PIX(px - 1, py + 1), PIX(px, py + 1), PIX(px + 1, py + 1), PIX(px + 2, py + 1)),
                apply_coeffs<floatT>(coeffsX, PIX(px - 1, py + 2), PIX(px, py + 2), PIX(px + 1, py + 2), PIX(px + 2, py + 2))
            ),
            factor
        )
    );

#undef PIX
}

enum ScaleDir {
    SCALE_DIR_X,
    SCALE_DIR_Y,
};

template<typename T, int dir>
__device__ static inline T Subsample_Generic(cudaTextureObject_t tex,
                                             int xo, int yo,
                                             int dst_width, int dst_height,
                                             int src_left, int src_top,
                                             int src_width, int src_height,
                                             int bit_depth, int src_depth, int storage_max,
                                             float param,
                                             const float *weights, const int *offsets,
                                             int filter_size)
{
    const float factor = bit_depth > 8 ? 0xFFFF : 0xFF;
    const float code_max = (1U << src_depth) - 1;
    const float sample_step = storage_max / code_max;

    floatT sum;
    vec_set_scalar(sum, 0.0f);

    if (dir == SCALE_DIR_X) {
        const float *row = &weights[xo * filter_size];
        const float x = 0.5f + src_left + offsets[xo];
        const float y = 0.5f + src_top  + yo;
        for (int i = 0; i < filter_size; i++)
            sum += tex2D<floatT>(tex, x + i, y) * row[i];
    } else {
        const float *col = &weights[yo * filter_size];
        const float x = 0.5f + src_left + xo;
        const float y = 0.5f + src_top  + offsets[yo];
        for (int i = 0; i < filter_size; i++)
            sum += tex2D<floatT>(tex, x, y + i) * col[i];
    }

    return from_floatN<T, floatT>(
        saturate_rintf(sum * (factor / storage_max), code_max) * sample_step
    );
}

template<typename T, int dir = SCALE_DIR_X>
__device__ static inline floatT Subsample_GenericFloat(cudaTextureObject_t tex,
                                                       int xo, int yo,
                                                       int src_left, int src_top,
                                                       const float *weights,
                                                       const int *offsets,
                                                       int filter_size)
{
    floatT sum;

    vec_set_scalar(sum, 0.0f);
    if (dir == SCALE_DIR_X) {
        const float *row = &weights[xo * filter_size];
        const float x = 0.5f + src_left + offsets[xo];
        const float y = 0.5f + src_top  + yo;
        for (int i = 0; i < filter_size; i++)
            sum += tex2D<floatT>(tex, x + i, y) * row[i];
    } else {
        const float *col = &weights[yo * filter_size];
        const float x = 0.5f + src_left + xo;
        const float y = 0.5f + src_top  + offsets[yo];
        for (int i = 0; i < filter_size; i++)
            sum += tex2D<floatT>(tex, x, y + i) * col[i];
    }

    return sum;
}

/// --- FUNCTION EXPORTS ---

#define KERNEL_ARGS(T) CUDAScaleKernelParams params

#define SUBSAMPLE(Convert, T) \
    cudaTextureObject_t src_tex[4] = {                  \
        params.src_tex[0], params.src_tex[1],           \
        params.src_tex[2], params.src_tex[3]            \
    };                                                  \
    T *dst[4] = {                                       \
        (T*)params.dst[0], (T*)params.dst[1],           \
        (T*)params.dst[2], (T*)params.dst[3]            \
    };                                                  \
    int xo = blockIdx.x * blockDim.x + threadIdx.x;     \
    int yo = blockIdx.y * blockDim.y + threadIdx.y;     \
    if (yo >= params.dst_height || xo >= params.dst_width) return; \
    Convert(                                            \
        src_tex, dst, xo, yo,                           \
        params.dst_width, params.dst_height, params.dst_pitch, \
        params.src_left, params.src_top,                \
        params.src_width, params.src_height,            \
        params.src_depth, params.src_storage_max,       \
        params.param, params.mpeg_range,                \
        (const float*) params.weights,                  \
        (const int*) params.offsets,                    \
        params.filter_size);

extern "C" {

#define NEAREST_KERNEL(C, S) \
    __global__ void Subsample_Nearest_##C##S(                      \
        KERNEL_ARGS(Convert_##C::out_T##S))                        \
    {                                                              \
        SUBSAMPLE((Convert_##C::Convert##S<                        \
                       Subsample_Nearest<Convert_##C::in_T>,       \
                       Subsample_Nearest<Convert_##C::in_T_uv> >), \
                  Convert_##C::out_T##S) \
    }

#define NEAREST_KERNEL_RAW(C) \
    NEAREST_KERNEL(C,)   \
    NEAREST_KERNEL(C,_uv)

#define NEAREST_KERNELS(C) \
    NEAREST_KERNEL_RAW(planar8_ ## C)      \
    NEAREST_KERNEL_RAW(planar10_ ## C)     \
    NEAREST_KERNEL_RAW(planar16_ ## C)     \
    NEAREST_KERNEL_RAW(semiplanar8_ ## C)  \
    NEAREST_KERNEL_RAW(semiplanar10_ ## C) \
    NEAREST_KERNEL_RAW(semiplanar16_ ## C)

#define NEAREST_KERNELS_RGB(C) \
    NEAREST_KERNEL_RAW(rgb0_ ## C)  \
    NEAREST_KERNEL_RAW(bgr0_ ## C)  \
    NEAREST_KERNEL_RAW(rgba_ ## C)  \
    NEAREST_KERNEL_RAW(bgra_ ## C)  \

NEAREST_KERNELS(planar8)
NEAREST_KERNELS(planar10)
NEAREST_KERNELS(planar16)
NEAREST_KERNELS(semiplanar8)
NEAREST_KERNELS(semiplanar10)
NEAREST_KERNELS(semiplanar16)

NEAREST_KERNELS_RGB(rgb0)
NEAREST_KERNELS_RGB(bgr0)
NEAREST_KERNELS_RGB(rgba)
NEAREST_KERNELS_RGB(bgra)

#define BILINEAR_KERNEL(C, S) \
    __global__ void Subsample_Bilinear_##C##S(                      \
        KERNEL_ARGS(Convert_##C::out_T##S))                         \
    {                                                               \
        SUBSAMPLE((Convert_##C::Convert##S<                         \
                       Subsample_Bilinear<Convert_##C::in_T>,       \
                       Subsample_Bilinear<Convert_##C::in_T_uv> >), \
                  Convert_##C::out_T##S) \
    }

#define BILINEAR_KERNEL_RAW(C) \
    BILINEAR_KERNEL(C,)   \
    BILINEAR_KERNEL(C,_uv)

#define BILINEAR_KERNELS(C) \
    BILINEAR_KERNEL_RAW(planar8_ ## C)      \
    BILINEAR_KERNEL_RAW(planar10_ ## C)     \
    BILINEAR_KERNEL_RAW(planar16_ ## C)     \
    BILINEAR_KERNEL_RAW(semiplanar8_ ## C)  \
    BILINEAR_KERNEL_RAW(semiplanar10_ ## C) \
    BILINEAR_KERNEL_RAW(semiplanar16_ ## C)

#define BILINEAR_KERNELS_RGB(C)     \
    BILINEAR_KERNEL_RAW(rgb0_ ## C) \
    BILINEAR_KERNEL_RAW(bgr0_ ## C) \
    BILINEAR_KERNEL_RAW(rgba_ ## C) \
    BILINEAR_KERNEL_RAW(bgra_ ## C)

BILINEAR_KERNELS(planar8)
BILINEAR_KERNELS(planar10)
BILINEAR_KERNELS(planar16)
BILINEAR_KERNELS(semiplanar8)
BILINEAR_KERNELS(semiplanar10)
BILINEAR_KERNELS(semiplanar16)

BILINEAR_KERNELS_RGB(rgb0)
BILINEAR_KERNELS_RGB(bgr0)
BILINEAR_KERNELS_RGB(rgba)
BILINEAR_KERNELS_RGB(bgra)

#define BICUBIC_KERNEL(C, S) \
    __global__ void Subsample_Bicubic_##C##S(                                        \
        KERNEL_ARGS(Convert_##C::out_T##S))                                          \
    {                                                                                \
        SUBSAMPLE((Convert_##C::Convert##S<                                          \
                       Subsample_Bicubic<Convert_## C ::in_T, bicubic_coeffs>,       \
                       Subsample_Bicubic<Convert_## C ::in_T_uv, bicubic_coeffs> >), \
                  Convert_##C::out_T##S)                                             \
    }

#define BICUBIC_KERNEL_RAW(C) \
    BICUBIC_KERNEL(C,)   \
    BICUBIC_KERNEL(C,_uv)

#define BICUBIC_KERNELS(C) \
    BICUBIC_KERNEL_RAW(planar8_ ## C)      \
    BICUBIC_KERNEL_RAW(planar10_ ## C)     \
    BICUBIC_KERNEL_RAW(planar16_ ## C)     \
    BICUBIC_KERNEL_RAW(semiplanar8_ ## C)  \
    BICUBIC_KERNEL_RAW(semiplanar10_ ## C) \
    BICUBIC_KERNEL_RAW(semiplanar16_ ## C)

#define BICUBIC_KERNELS_RGB(C)      \
    BICUBIC_KERNEL_RAW(rgb0_ ## C)  \
    BICUBIC_KERNEL_RAW(bgr0_ ## C)  \
    BICUBIC_KERNEL_RAW(rgba_ ## C)  \
    BICUBIC_KERNEL_RAW(bgra_ ## C)

BICUBIC_KERNELS(planar8)
BICUBIC_KERNELS(planar10)
BICUBIC_KERNELS(planar16)
BICUBIC_KERNELS(semiplanar8)
BICUBIC_KERNELS(semiplanar10)
BICUBIC_KERNELS(semiplanar16)

BICUBIC_KERNELS_RGB(rgb0)
BICUBIC_KERNELS_RGB(bgr0)
BICUBIC_KERNELS_RGB(rgba)
BICUBIC_KERNELS_RGB(bgra)

#define LANCZOS_KERNEL(C, S) \
    __global__ void Subsample_Lanczos_##C##S(                                        \
        KERNEL_ARGS(Convert_##C::out_T##S))                                          \
    {                                                                                \
        SUBSAMPLE((Convert_##C::Convert##S<                                          \
                       Subsample_Bicubic<Convert_## C ::in_T, lanczos_coeffs>,       \
                       Subsample_Bicubic<Convert_## C ::in_T_uv, lanczos_coeffs> >), \
                  Convert_##C::out_T##S) \
    }

#define LANCZOS_KERNEL_RAW(C) \
    LANCZOS_KERNEL(C,)   \
    LANCZOS_KERNEL(C,_uv)

#define LANCZOS_KERNELS(C) \
    LANCZOS_KERNEL_RAW(planar8_ ## C)      \
    LANCZOS_KERNEL_RAW(planar10_ ## C)     \
    LANCZOS_KERNEL_RAW(planar16_ ## C)     \
    LANCZOS_KERNEL_RAW(semiplanar8_ ## C)  \
    LANCZOS_KERNEL_RAW(semiplanar10_ ## C) \
    LANCZOS_KERNEL_RAW(semiplanar16_ ## C)

#define LANCZOS_KERNELS_RGB(C)      \
    LANCZOS_KERNEL_RAW(rgb0_ ## C)  \
    LANCZOS_KERNEL_RAW(bgr0_ ## C)  \
    LANCZOS_KERNEL_RAW(rgba_ ## C)  \
    LANCZOS_KERNEL_RAW(bgra_ ## C)

LANCZOS_KERNELS(planar8)
LANCZOS_KERNELS(planar10)
LANCZOS_KERNELS(planar16)
LANCZOS_KERNELS(semiplanar8)
LANCZOS_KERNELS(semiplanar10)
LANCZOS_KERNELS(semiplanar16)

LANCZOS_KERNELS_RGB(rgb0)
LANCZOS_KERNELS_RGB(bgr0)
LANCZOS_KERNELS_RGB(rgba)
LANCZOS_KERNELS_RGB(bgra)

#define GENERIC_KERNEL(D, DIR, C, S) \
    __global__ void Subsample_Generic_##D##_##C##S(                     \
        KERNEL_ARGS(Convert_##C::out_T##S))                             \
    {                                                                   \
        SUBSAMPLE((Convert_##C::Convert##S<                             \
                       Subsample_Generic<Convert_##C::in_T, DIR>,       \
                       Subsample_Generic<Convert_##C::in_T_uv, DIR> >), \
                  Convert_##C::out_T##S) \
    }

#define GENERIC_KERNEL_RAW(C) \
    GENERIC_KERNEL(h, SCALE_DIR_X, C,)      \
    GENERIC_KERNEL(h, SCALE_DIR_X, C,_uv)   \
    GENERIC_KERNEL(v, SCALE_DIR_Y, C,)      \
    GENERIC_KERNEL(v, SCALE_DIR_Y, C,_uv)

#define GENERIC_KERNELS(C) \
    GENERIC_KERNEL_RAW(planar8_ ## C)       \
    GENERIC_KERNEL_RAW(planar10_ ## C)      \
    GENERIC_KERNEL_RAW(planar16_ ## C)      \
    GENERIC_KERNEL_RAW(semiplanar8_ ## C)   \
    GENERIC_KERNEL_RAW(semiplanar10_ ## C)  \
    GENERIC_KERNEL_RAW(semiplanar16_ ## C)

#define GENERIC_KERNELS_RGB(C) \
    GENERIC_KERNEL_RAW(rgb0_ ## C)  \
    GENERIC_KERNEL_RAW(bgr0_ ## C)  \
    GENERIC_KERNEL_RAW(rgba_ ## C)  \
    GENERIC_KERNEL_RAW(bgra_ ## C)

GENERIC_KERNELS(planar8)
GENERIC_KERNELS(planar10)
GENERIC_KERNELS(planar16)
GENERIC_KERNELS(semiplanar8)
GENERIC_KERNELS(semiplanar10)
GENERIC_KERNELS(semiplanar16)

GENERIC_KERNELS_RGB(rgb0)
GENERIC_KERNELS_RGB(bgr0)
GENERIC_KERNELS_RGB(rgba)
GENERIC_KERNELS_RGB(bgra)

#define GENERIC_FLOAT_KERNEL_Y(C, T, F)                                \
    __global__ void Subsample_Generic_float_h_##C##_##C(               \
        CUDAScaleKernelParams params)                                  \
    {                                                                  \
        int xo = blockIdx.x * blockDim.x + threadIdx.x;                \
        int yo = blockIdx.y * blockDim.y + threadIdx.y;                \
        if (yo >= params.dst_height || xo >= params.dst_width) return; \
        F *dst = (F *)((char *)params.dst[0] +                         \
                       yo * params.dst_pitch);                         \
        dst[xo] = Subsample_GenericFloat<T>(                           \
            params.src_tex[0], xo, yo, params.src_left, params.src_top, \
            (const float *)params.weights, (const int *)params.offsets, \
            params.filter_size);                                      \
    }

#define GENERIC_FLOAT_KERNEL_UV_PLANAR(C, T)                           \
    __global__ void Subsample_Generic_float_h_##C##_##C##_uv(          \
        CUDAScaleKernelParams params)                                  \
    {                                                                  \
        int xo = blockIdx.x * blockDim.x + threadIdx.x;                \
        int yo = blockIdx.y * blockDim.y + threadIdx.y;                \
        if (yo >= params.dst_height || xo >= params.dst_width) return; \
        float *dst_u = (float *)((char *)params.dst[1] +               \
                                 yo * params.dst_pitch);               \
        float *dst_v = (float *)((char *)params.dst[2] +               \
                                 yo * params.dst_pitch);               \
        const float *weights = (const float *)params.weights;          \
        const int *offsets = (const int *)params.offsets;              \
        dst_u[xo] = Subsample_GenericFloat<T>(params.src_tex[1], xo, yo, \
            params.src_left, params.src_top, weights, offsets,         \
            params.filter_size);                                      \
        dst_v[xo] = Subsample_GenericFloat<T>(params.src_tex[2], xo, yo, \
            params.src_left, params.src_top, weights, offsets,         \
            params.filter_size);                                      \
    }

#define GENERIC_FLOAT_KERNEL_UV_SEMI(C, T, F)                          \
    __global__ void Subsample_Generic_float_h_##C##_##C##_uv(          \
        CUDAScaleKernelParams params)                                  \
    {                                                                  \
        int xo = blockIdx.x * blockDim.x + threadIdx.x;                \
        int yo = blockIdx.y * blockDim.y + threadIdx.y;                \
        if (yo >= params.dst_height || xo >= params.dst_width) return; \
        F *dst = (F *)((char *)params.dst[1] +                         \
                       yo * params.dst_pitch);                         \
        dst[xo] = Subsample_GenericFloat<T>(                           \
            params.src_tex[1], xo, yo, params.src_left, params.src_top, \
            (const float *)params.weights, (const int *)params.offsets, \
            params.filter_size);                                      \
    }

#define GENERIC_FLOAT_KERNEL_PLANAR(C, T) \
    GENERIC_FLOAT_KERNEL_Y(C, T, float)    \
    GENERIC_FLOAT_KERNEL_UV_PLANAR(C, T)

#define GENERIC_FLOAT_KERNEL_SEMI(C, T, TUV) \
    GENERIC_FLOAT_KERNEL_Y(C, T, float)       \
    GENERIC_FLOAT_KERNEL_UV_SEMI(C, TUV, float2)

GENERIC_FLOAT_KERNEL_PLANAR(planar8,  uchar)
GENERIC_FLOAT_KERNEL_PLANAR(planar10, ushort)
GENERIC_FLOAT_KERNEL_PLANAR(planar16, ushort)
GENERIC_FLOAT_KERNEL_SEMI(semiplanar8,  uchar,  uchar2)
GENERIC_FLOAT_KERNEL_SEMI(semiplanar10, ushort, ushort2)
GENERIC_FLOAT_KERNEL_SEMI(semiplanar16, ushort, ushort2)
GENERIC_FLOAT_KERNEL_Y(rgb0, uchar4, float4)
GENERIC_FLOAT_KERNEL_Y(bgr0, uchar4, float4)
GENERIC_FLOAT_KERNEL_Y(rgba, uchar4, float4)
GENERIC_FLOAT_KERNEL_Y(bgra, uchar4, float4)

/* Vertical float pass, used to scale RGB before converting it to YUV. */
#define GENERIC_FLOAT_KERNEL_V(C, T, F)                                \
    __global__ void Subsample_Generic_float_v_##C##_##C(               \
        CUDAScaleKernelParams params)                                  \
    {                                                                  \
        int xo = blockIdx.x * blockDim.x + threadIdx.x;                \
        int yo = blockIdx.y * blockDim.y + threadIdx.y;                \
        if (yo >= params.dst_height || xo >= params.dst_width) return; \
        F *dst = (F *)((char *)params.dst[0] +                         \
                       yo * params.dst_pitch);                         \
        dst[xo] = Subsample_GenericFloat<T, SCALE_DIR_Y>(              \
            params.src_tex[0], xo, yo, params.src_left, params.src_top, \
            (const float *)params.weights, (const int *)params.offsets, \
            params.filter_size);                                      \
    }

GENERIC_FLOAT_KERNEL_V(rgb0, uchar4, float4)
GENERIC_FLOAT_KERNEL_V(bgr0, uchar4, float4)
GENERIC_FLOAT_KERNEL_V(rgba, uchar4, float4)
GENERIC_FLOAT_KERNEL_V(bgra, uchar4, float4)


// --- YUV to RGB kernel exports ---
// Single-pass kernels (no resize, color conversion only)

// semiplanar8 (NV12, NV16) -> RGB outputs
__global__ void Subsample_Nearest_semiplanar8_bgra(CUDAScaleKernelParams p)
{ YuvToRgb_semiplanar<uchar, uchar2, 0>(p); }
__global__ void Subsample_Nearest_semiplanar8_rgba(CUDAScaleKernelParams p)
{ YuvToRgb_semiplanar<uchar, uchar2, 1>(p); }
__global__ void Subsample_Nearest_semiplanar8_bgr0(CUDAScaleKernelParams p)
{ YuvToRgb_semiplanar<uchar, uchar2, 2>(p); }
__global__ void Subsample_Nearest_semiplanar8_rgb0(CUDAScaleKernelParams p)
{ YuvToRgb_semiplanar<uchar, uchar2, 3>(p); }

// semiplanar10 (P010, P210) -> RGB outputs
__global__ void Subsample_Nearest_semiplanar10_bgra(CUDAScaleKernelParams p)
{ YuvToRgb_semiplanar<ushort, ushort2, 0>(p); }
__global__ void Subsample_Nearest_semiplanar10_rgba(CUDAScaleKernelParams p)
{ YuvToRgb_semiplanar<ushort, ushort2, 1>(p); }
__global__ void Subsample_Nearest_semiplanar10_bgr0(CUDAScaleKernelParams p)
{ YuvToRgb_semiplanar<ushort, ushort2, 2>(p); }
__global__ void Subsample_Nearest_semiplanar10_rgb0(CUDAScaleKernelParams p)
{ YuvToRgb_semiplanar<ushort, ushort2, 3>(p); }

// planar8 (YUV420P, YUV422P, YUV444P) -> RGB outputs
__global__ void Subsample_Nearest_planar8_bgra(CUDAScaleKernelParams p)
{ YuvToRgb_planar<uchar, uchar, 0>(p); }
__global__ void Subsample_Nearest_planar8_rgba(CUDAScaleKernelParams p)
{ YuvToRgb_planar<uchar, uchar, 1>(p); }
__global__ void Subsample_Nearest_planar8_bgr0(CUDAScaleKernelParams p)
{ YuvToRgb_planar<uchar, uchar, 2>(p); }
__global__ void Subsample_Nearest_planar8_rgb0(CUDAScaleKernelParams p)
{ YuvToRgb_planar<uchar, uchar, 3>(p); }

// planar10 (YUV420P10, YUV422P10, YUV444P10) -> RGB outputs
__global__ void Subsample_Nearest_planar10_bgra(CUDAScaleKernelParams p)
{ YuvToRgb_planar<ushort, ushort, 0>(p); }
__global__ void Subsample_Nearest_planar10_rgba(CUDAScaleKernelParams p)
{ YuvToRgb_planar<ushort, ushort, 1>(p); }
__global__ void Subsample_Nearest_planar10_bgr0(CUDAScaleKernelParams p)
{ YuvToRgb_planar<ushort, ushort, 2>(p); }
__global__ void Subsample_Nearest_planar10_rgb0(CUDAScaleKernelParams p)
{ YuvToRgb_planar<ushort, ushort, 3>(p); }

// --- RGB to YUV kernel exports ---
// Two-pass: Y kernel writes Y plane, UV kernel writes chroma planes.
// No resize, color conversion only.

// bgra -> semiplanar8 (NV12, NV16)
__global__ void Subsample_Nearest_bgra_semiplanar8(CUDAScaleKernelParams p)
{ RgbToYuv_Y<uchar, 0>(p); }
__global__ void Subsample_Nearest_bgra_semiplanar8_uv(CUDAScaleKernelParams p)
{ RgbToYuv_semiplanar_UV<uchar2, 0>(p); }

// rgba -> semiplanar8
__global__ void Subsample_Nearest_rgba_semiplanar8(CUDAScaleKernelParams p)
{ RgbToYuv_Y<uchar, 1>(p); }
__global__ void Subsample_Nearest_rgba_semiplanar8_uv(CUDAScaleKernelParams p)
{ RgbToYuv_semiplanar_UV<uchar2, 1>(p); }

// bgr0 -> semiplanar8
__global__ void Subsample_Nearest_bgr0_semiplanar8(CUDAScaleKernelParams p)
{ RgbToYuv_Y<uchar, 2>(p); }
__global__ void Subsample_Nearest_bgr0_semiplanar8_uv(CUDAScaleKernelParams p)
{ RgbToYuv_semiplanar_UV<uchar2, 2>(p); }

// rgb0 -> semiplanar8
__global__ void Subsample_Nearest_rgb0_semiplanar8(CUDAScaleKernelParams p)
{ RgbToYuv_Y<uchar, 3>(p); }
__global__ void Subsample_Nearest_rgb0_semiplanar8_uv(CUDAScaleKernelParams p)
{ RgbToYuv_semiplanar_UV<uchar2, 3>(p); }

// bgra -> semiplanar10 (P010, P210)
__global__ void Subsample_Nearest_bgra_semiplanar10(CUDAScaleKernelParams p)
{ RgbToYuv_Y_semiplanar16<0>(p); }
__global__ void Subsample_Nearest_bgra_semiplanar10_uv(CUDAScaleKernelParams p)
{ RgbToYuv_semiplanar16_UV<0>(p); }

// rgba -> semiplanar10
__global__ void Subsample_Nearest_rgba_semiplanar10(CUDAScaleKernelParams p)
{ RgbToYuv_Y_semiplanar16<1>(p); }
__global__ void Subsample_Nearest_rgba_semiplanar10_uv(CUDAScaleKernelParams p)
{ RgbToYuv_semiplanar16_UV<1>(p); }

// bgr0 -> semiplanar10
__global__ void Subsample_Nearest_bgr0_semiplanar10(CUDAScaleKernelParams p)
{ RgbToYuv_Y_semiplanar16<2>(p); }
__global__ void Subsample_Nearest_bgr0_semiplanar10_uv(CUDAScaleKernelParams p)
{ RgbToYuv_semiplanar16_UV<2>(p); }

// rgb0 -> semiplanar10
__global__ void Subsample_Nearest_rgb0_semiplanar10(CUDAScaleKernelParams p)
{ RgbToYuv_Y_semiplanar16<3>(p); }
__global__ void Subsample_Nearest_rgb0_semiplanar10_uv(CUDAScaleKernelParams p)
{ RgbToYuv_semiplanar16_UV<3>(p); }

// bgra -> planar8 (YUV420P, YUV422P, YUV444P)
__global__ void Subsample_Nearest_bgra_planar8(CUDAScaleKernelParams p)
{ RgbToYuv_Y<uchar, 0>(p); }
__global__ void Subsample_Nearest_bgra_planar8_uv(CUDAScaleKernelParams p)
{ RgbToYuv_planar_UV<uchar, 0>(p); }

// rgba -> planar8
__global__ void Subsample_Nearest_rgba_planar8(CUDAScaleKernelParams p)
{ RgbToYuv_Y<uchar, 1>(p); }
__global__ void Subsample_Nearest_rgba_planar8_uv(CUDAScaleKernelParams p)
{ RgbToYuv_planar_UV<uchar, 1>(p); }

// bgr0 -> planar8
__global__ void Subsample_Nearest_bgr0_planar8(CUDAScaleKernelParams p)
{ RgbToYuv_Y<uchar, 2>(p); }
__global__ void Subsample_Nearest_bgr0_planar8_uv(CUDAScaleKernelParams p)
{ RgbToYuv_planar_UV<uchar, 2>(p); }

// rgb0 -> planar8
__global__ void Subsample_Nearest_rgb0_planar8(CUDAScaleKernelParams p)
{ RgbToYuv_Y<uchar, 3>(p); }
__global__ void Subsample_Nearest_rgb0_planar8_uv(CUDAScaleKernelParams p)
{ RgbToYuv_planar_UV<uchar, 3>(p); }

// bgra -> planar10 (YUV420P10, YUV422P10, YUV444P10)
__global__ void Subsample_Nearest_bgra_planar10(CUDAScaleKernelParams p)
{ RgbToYuv_Y_planar10<0>(p); }
__global__ void Subsample_Nearest_bgra_planar10_uv(CUDAScaleKernelParams p)
{ RgbToYuv_planar_UV<ushort, 0>(p); }

// rgba -> planar10
__global__ void Subsample_Nearest_rgba_planar10(CUDAScaleKernelParams p)
{ RgbToYuv_Y_planar10<1>(p); }
__global__ void Subsample_Nearest_rgba_planar10_uv(CUDAScaleKernelParams p)
{ RgbToYuv_planar_UV<ushort, 1>(p); }

// bgr0 -> planar10
__global__ void Subsample_Nearest_bgr0_planar10(CUDAScaleKernelParams p)
{ RgbToYuv_Y_planar10<2>(p); }
__global__ void Subsample_Nearest_bgr0_planar10_uv(CUDAScaleKernelParams p)
{ RgbToYuv_planar_UV<ushort, 2>(p); }

// rgb0 -> planar10
__global__ void Subsample_Nearest_rgb0_planar10(CUDAScaleKernelParams p)
{ RgbToYuv_Y_planar10<3>(p); }
__global__ void Subsample_Nearest_rgb0_planar10_uv(CUDAScaleKernelParams p)
{ RgbToYuv_planar_UV<ushort, 3>(p); }

// --- Conversion kernels for a float RGB intermediate ---
// Converting together with scaling scales on the RGB side: YUV->RGB writes
// the intermediate that is then scaled, RGB->YUV reads the scaled one.

#define CONV_FLOAT_TO_RGB(RGB, O)                                       \
__global__ void Conv_float_semiplanar8_##RGB(CUDAScaleKernelParams p)   \
{ YuvToRgb_semiplanar<uchar, uchar2, O, float4>(p); }                   \
__global__ void Conv_float_semiplanar10_##RGB(CUDAScaleKernelParams p)  \
{ YuvToRgb_semiplanar<ushort, ushort2, O, float4>(p); }                 \
__global__ void Conv_float_planar8_##RGB(CUDAScaleKernelParams p)       \
{ YuvToRgb_planar<uchar, uchar, O, float4>(p); }                        \
__global__ void Conv_float_planar10_##RGB(CUDAScaleKernelParams p)      \
{ YuvToRgb_planar<ushort, ushort, O, float4>(p); }

#define CONV_FLOAT_FROM_RGB(RGB, O)                                     \
__global__ void Conv_float_##RGB##_semiplanar8(CUDAScaleKernelParams p) \
{ RgbToYuv_Y<uchar, O, float4>(p); }                                    \
__global__ void Conv_float_##RGB##_semiplanar8_uv(CUDAScaleKernelParams p) \
{ RgbToYuv_semiplanar_UV<uchar2, O, float4>(p); }                       \
__global__ void Conv_float_##RGB##_semiplanar10(CUDAScaleKernelParams p) \
{ RgbToYuv_Y_semiplanar16<O, float4>(p); }                              \
__global__ void Conv_float_##RGB##_semiplanar10_uv(CUDAScaleKernelParams p) \
{ RgbToYuv_semiplanar16_UV<O, float4>(p); }                             \
__global__ void Conv_float_##RGB##_planar8(CUDAScaleKernelParams p)     \
{ RgbToYuv_Y<uchar, O, float4>(p); }                                    \
__global__ void Conv_float_##RGB##_planar8_uv(CUDAScaleKernelParams p)  \
{ RgbToYuv_planar_UV<uchar, O, float4>(p); }                            \
__global__ void Conv_float_##RGB##_planar10(CUDAScaleKernelParams p)    \
{ RgbToYuv_Y_planar10<O, float4>(p); }                                  \
__global__ void Conv_float_##RGB##_planar10_uv(CUDAScaleKernelParams p) \
{ RgbToYuv_planar_UV<ushort, O, float4>(p); }

CONV_FLOAT_TO_RGB(bgra, 0)
CONV_FLOAT_TO_RGB(rgba, 1)
CONV_FLOAT_TO_RGB(bgr0, 2)
CONV_FLOAT_TO_RGB(rgb0, 3)

CONV_FLOAT_FROM_RGB(bgra, 0)
CONV_FLOAT_FROM_RGB(rgba, 1)
CONV_FLOAT_FROM_RGB(bgr0, 2)
CONV_FLOAT_FROM_RGB(rgb0, 3)
}
