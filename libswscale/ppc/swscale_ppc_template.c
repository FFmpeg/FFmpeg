/*
 * AltiVec-enhanced yuv2yuvX
 *
 * Copyright (C) 2004 Romain Dolbeau <romain@dolbeau.org>
 * based on the equivalent C code in swscale.c
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

static void FUNC(yuv2planeX_16)(const int16_t *filter, int filterSize,
                                  const int16_t **src, uint8_t *dest,
                                  const uint8_t *dither, int offset, int x)
{
    register int i, j;
    LOCAL_ALIGNED(16, int, val, [16]);
    vector signed int vo1, vo2, vo3, vo4;
    vector unsigned short vs1, vs2;
    vector unsigned char vf;
    vector unsigned int altivec_vectorShiftInt19 =
        vec_add(vec_splat_u32(10), vec_splat_u32(9));

    for (i = 0; i < 16; i++)
        val[i] = dither[(x + i + offset) & 7] << 12;

    vo1 = vec_ld(0,  val);
    vo2 = vec_ld(16, val);
    vo3 = vec_ld(32, val);
    vo4 = vec_ld(48, val);

    for (j = 0; j < filterSize; j++) {
        unsigned int joffset=j<<1;
        unsigned int xoffset=x<<1;
        vector unsigned char perm;
        vector signed short l1,vLumFilter;
        LOAD_FILTER(vLumFilter,filter);
        vLumFilter = vec_splat(vLumFilter, 0);
        LOAD_L1(l1,src[j],perm);
        yuv2planeX_8(vo1, vo2, l1, src[j], x,     perm, vLumFilter);
        yuv2planeX_8(vo3, vo4, l1, src[j], x + 8, perm, vLumFilter);
    }

    vo1 = vec_sra(vo1, altivec_vectorShiftInt19);
    vo2 = vec_sra(vo2, altivec_vectorShiftInt19);
    vo3 = vec_sra(vo3, altivec_vectorShiftInt19);
    vo4 = vec_sra(vo4, altivec_vectorShiftInt19);
    vs1 = vec_packsu(vo1, vo2);
    vs2 = vec_packsu(vo3, vo4);
    vf  = vec_packsu(vs1, vs2);
    VEC_ST(vf, 0, dest);
}


static inline void yuv2planeX_u(const int16_t *filter, int filterSize,
                                const int16_t **src, uint8_t *dest, int dstW,
                                const uint8_t *dither, int offset, int x)
{
    int i, j;

    for (i = x; i < dstW; i++) {
        int t = dither[(i + offset) & 7] << 12;
        for (j = 0; j < filterSize; j++)
            t += src[j][i] * filter[j];
        dest[i] = av_clip_uint8(t >> 19);
    }
}

static void FUNC(yuv2planeX)(const int16_t *filter, int filterSize,
                               const int16_t **src, uint8_t *dest, int dstW,
                               const uint8_t *dither, int offset)
{
    int dst_u = -(uintptr_t)dest & 15;
    int i;

    yuv2planeX_u(filter, filterSize, src, dest, dst_u, dither, offset, 0);

    for (i = dst_u; i < dstW - 15; i += 16)
        FUNC(yuv2planeX_16)(filter, filterSize, src, dest + i, dither,
                              offset, i);

    yuv2planeX_u(filter, filterSize, src, dest, dstW, dither, offset, i);
}

static void FUNC(hScale_real)(SwsContext *c, int16_t *dst, int dstW,
                                const uint8_t *src, const int16_t *filter,
                                const int32_t *filterPos, int filterSize)
{
    register int i;
    LOCAL_ALIGNED(16, int, tempo, [4]);

    if (filterSize % 4) {
        for (i = 0; i < dstW; i++) {
            register int j;
            register int srcPos = filterPos[i];
            register int val    = 0;
            for (j = 0; j < filterSize; j++)
                val += ((int)src[srcPos + j]) * filter[filterSize * i + j];
            dst[i] = FFMIN(val >> 7, (1 << 15) - 1);
        }
    } else
        switch (filterSize) {
        case 4:
            for (i = 0; i < dstW; i++) {
                register int srcPos = filterPos[i];

                vector unsigned char src_vF = unaligned_load(srcPos, src);
                vector signed short src_v, filter_v;
                vector signed int val_vEven, val_s;
                src_v = // vec_unpackh sign-extends...
                        (vector signed short)(VEC_MERGEH((vector unsigned char)vzero, src_vF));
                // now put our elements in the even slots
                src_v = vec_mergeh(src_v, (vector signed short)vzero);
                GET_VF4(i, filter_v, filter);
                val_vEven = vec_mule(src_v, filter_v);
                val_s     = vec_sums(val_vEven, vzero);
                vec_st(val_s, 0, tempo);
                dst[i] = FFMIN(tempo[3] >> 7, (1 << 15) - 1);
            }
        break;
        case 8:
            for (i = 0; i < dstW; i++) {
                register int srcPos = filterPos[i];
                vector unsigned char src_vF, src_v0, src_v1;
                vector unsigned char permS;
                vector signed short src_v, filter_v;
                vector signed int val_v, val_s;
                FIRST_LOAD(src_v0, srcPos, src, permS);
                LOAD_SRCV8(srcPos, 0, src, permS, src_v0, src_v1, src_vF);
                src_v = // vec_unpackh sign-extends...
                        (vector signed short)(VEC_MERGEH((vector unsigned char)vzero, src_vF));
                filter_v = vec_ld(i << 4, filter);
                val_v = vec_msums(src_v, filter_v, (vector signed int)vzero);
                val_s = vec_sums(val_v, vzero);
                vec_st(val_s, 0, tempo);
                dst[i] = FFMIN(tempo[3] >> 7, (1 << 15) - 1);
            }
        break;

        case 16:
            for (i = 0; i < dstW; i++) {
                register int srcPos = filterPos[i];

                vector unsigned char src_vF = unaligned_load(srcPos, src);
                vector signed short src_vA = // vec_unpackh sign-extends...
                                             (vector signed short)(VEC_MERGEH((vector unsigned char)vzero, src_vF));
                vector signed short src_vB = // vec_unpackh sign-extends...
                                             (vector signed short)(VEC_MERGEL((vector unsigned char)vzero, src_vF));
                vector signed short filter_v0 = vec_ld(i << 5, filter);
                vector signed short filter_v1 = vec_ld((i << 5) + 16, filter);

                vector signed int val_acc = vec_msums(src_vA, filter_v0, (vector signed int)vzero);
                vector signed int val_v   = vec_msums(src_vB, filter_v1, val_acc);

                vector signed int val_s = vec_sums(val_v, vzero);

                VEC_ST(val_s, 0, tempo);
                dst[i] = FFMIN(tempo[3] >> 7, (1 << 15) - 1);
            }
        break;

        default:
            for (i = 0; i < dstW; i++) {
                register int j, offset = i * 2 * filterSize;
                register int srcPos = filterPos[i];

                vector signed int val_s, val_v = (vector signed int)vzero;
                vector signed short filter_v0R;
                vector unsigned char permF, src_v0, permS;
                FIRST_LOAD(filter_v0R, offset, filter, permF);
                FIRST_LOAD(src_v0, srcPos, src, permS);

                for (j = 0; j < filterSize - 15; j += 16) {
                    vector unsigned char src_v1, src_vF;
                    vector signed short filter_v1R, filter_v2R, filter_v0, filter_v1;
                    LOAD_SRCV(srcPos, j, src, permS, src_v0, src_v1, src_vF);
                    vector signed short src_vA = // vec_unpackh sign-extends...
                                                 (vector signed short)(VEC_MERGEH((vector unsigned char)vzero, src_vF));
                    vector signed short src_vB = // vec_unpackh sign-extends...
                                                 (vector signed short)(VEC_MERGEL((vector unsigned char)vzero, src_vF));
                    GET_VFD(i, j, filter, filter_v0R, filter_v1R, permF, filter_v0, 0);
                    GET_VFD(i, j, filter, filter_v1R, filter_v2R, permF, filter_v1, 16);

                    vector signed int val_acc = vec_msums(src_vA, filter_v0, val_v);
                    val_v = vec_msums(src_vB, filter_v1, val_acc);
                    UPDATE_PTR(filter_v2R, filter_v0R, src_v1, src_v0);
                }

                if (j < filterSize - 7) {
                    // loading src_v0 is useless, it's already done above
                    vector unsigned char src_v1, src_vF;
                    vector signed short src_v, filter_v1R, filter_v;
                    LOAD_SRCV8(srcPos, j, src, permS, src_v0, src_v1, src_vF);
                    src_v = // vec_unpackh sign-extends...
                            (vector signed short)(VEC_MERGEH((vector unsigned char)vzero, src_vF));
                    GET_VFD(i, j, filter, filter_v0R, filter_v1R, permF, filter_v, 0);
                    val_v = vec_msums(src_v, filter_v, val_v);
                }
                val_s = vec_sums(val_v, vzero);

                VEC_ST(val_s, 0, tempo);
                dst[i] = FFMIN(tempo[3] >> 7, (1 << 15) - 1);
            }
        }
}
