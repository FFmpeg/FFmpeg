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
                                   int bit_depth, float param);

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
                                    float param, int mpeg_range)

#define SUB_F(m, plane) \
    subsample_func_##m(src_tex[plane], xo, yo, \
                       dst_width, dst_height,  \
                       src_left, src_top,      \
                       src_width, src_height,  \
                       in_bit_depth, param)

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
                                             int bit_depth, float param)
{
    float hscale = (float)src_width / (float)dst_width;
    float vscale = (float)src_height / (float)dst_height;
    float xi = (xo + 0.5f) * hscale + src_left;
    float yi = (yo + 0.5f) * vscale + src_top;

    return tex2D<T>(tex, xi, yi);
}

template<typename T>
__device__ static inline T Subsample_Bilinear(cudaTextureObject_t tex,
                                              int xo, int yo,
                                              int dst_width, int dst_height,
                                              int src_left, int src_top,
                                              int src_width, int src_height,
                                              int bit_depth, float param)
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
                                             int bit_depth, float param)
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
        apply_coeffs<floatT>(coeffsY,
            apply_coeffs<floatT>(coeffsX, PIX(px - 1, py - 1), PIX(px, py - 1), PIX(px + 1, py - 1), PIX(px + 2, py - 1)),
            apply_coeffs<floatT>(coeffsX, PIX(px - 1, py    ), PIX(px, py    ), PIX(px + 1, py    ), PIX(px + 2, py    )),
            apply_coeffs<floatT>(coeffsX, PIX(px - 1, py + 1), PIX(px, py + 1), PIX(px + 1, py + 1), PIX(px + 2, py + 1)),
            apply_coeffs<floatT>(coeffsX, PIX(px - 1, py + 2), PIX(px, py + 2), PIX(px + 1, py + 2), PIX(px + 2, py + 2))
        ) * factor
    );

#undef PIX
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
        params.param, params.mpeg_range);

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
}
