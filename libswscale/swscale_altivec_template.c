/*
 * AltiVec-enhanced yuv2yuvX
 *
 * Copyright (C) 2004 Romain Dolbeau <romain@dolbeau.org>
 * based on the equivalent C code in swscale.c
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#define vzero vec_splat_s32(0)

static inline void
altivec_packIntArrayToCharArray(int *val, uint8_t* dest, int dstW) {
    register int i;
    vector unsigned int altivec_vectorShiftInt19 =
        vec_add(vec_splat_u32(10), vec_splat_u32(9));
    if ((unsigned long)dest % 16) {
        /* badly aligned store, we force store alignment */
        /* and will handle load misalignment on val w/ vec_perm */
        vector unsigned char perm1;
        vector signed int v1;
        for (i = 0 ; (i < dstW) &&
            (((unsigned long)dest + i) % 16) ; i++) {
                int t = val[i] >> 19;
                dest[i] = (t < 0) ? 0 : ((t > 255) ? 255 : t);
        }
        perm1 = vec_lvsl(i << 2, val);
        v1 = vec_ld(i << 2, val);
        for ( ; i < (dstW - 15); i+=16) {
            int offset = i << 2;
            vector signed int v2 = vec_ld(offset + 16, val);
            vector signed int v3 = vec_ld(offset + 32, val);
            vector signed int v4 = vec_ld(offset + 48, val);
            vector signed int v5 = vec_ld(offset + 64, val);
            vector signed int v12 = vec_perm(v1, v2, perm1);
            vector signed int v23 = vec_perm(v2, v3, perm1);
            vector signed int v34 = vec_perm(v3, v4, perm1);
            vector signed int v45 = vec_perm(v4, v5, perm1);

            vector signed int vA = vec_sra(v12, altivec_vectorShiftInt19);
            vector signed int vB = vec_sra(v23, altivec_vectorShiftInt19);
            vector signed int vC = vec_sra(v34, altivec_vectorShiftInt19);
            vector signed int vD = vec_sra(v45, altivec_vectorShiftInt19);
            vector unsigned short vs1 = vec_packsu(vA, vB);
            vector unsigned short vs2 = vec_packsu(vC, vD);
            vector unsigned char vf = vec_packsu(vs1, vs2);
            vec_st(vf, i, dest);
            v1 = v5;
        }
    } else { // dest is properly aligned, great
        for (i = 0; i < (dstW - 15); i+=16) {
            int offset = i << 2;
            vector signed int v1 = vec_ld(offset, val);
            vector signed int v2 = vec_ld(offset + 16, val);
            vector signed int v3 = vec_ld(offset + 32, val);
            vector signed int v4 = vec_ld(offset + 48, val);
            vector signed int v5 = vec_sra(v1, altivec_vectorShiftInt19);
            vector signed int v6 = vec_sra(v2, altivec_vectorShiftInt19);
            vector signed int v7 = vec_sra(v3, altivec_vectorShiftInt19);
            vector signed int v8 = vec_sra(v4, altivec_vectorShiftInt19);
            vector unsigned short vs1 = vec_packsu(v5, v6);
            vector unsigned short vs2 = vec_packsu(v7, v8);
            vector unsigned char vf = vec_packsu(vs1, vs2);
            vec_st(vf, i, dest);
        }
    }
    for ( ; i < dstW ; i++) {
        int t = val[i] >> 19;
        dest[i] = (t < 0) ? 0 : ((t > 255) ? 255 : t);
    }
}

static inline void
yuv2yuvX_altivec_real(int16_t *lumFilter, int16_t **lumSrc, int lumFilterSize,
                      int16_t *chrFilter, int16_t **chrSrc, int chrFilterSize,
                      uint8_t *dest, uint8_t *uDest, uint8_t *vDest, int dstW, int chrDstW)
{
    const vector signed int vini = {(1 << 18), (1 << 18), (1 << 18), (1 << 18)};
    register int i, j;
    {
        int __attribute__ ((aligned (16))) val[dstW];

        for (i = 0; i < (dstW -7); i+=4) {
            vec_st(vini, i << 2, val);
        }
        for (; i < dstW; i++) {
            val[i] = (1 << 18);
        }

        for (j = 0; j < lumFilterSize; j++) {
            vector signed short l1, vLumFilter = vec_ld(j << 1, lumFilter);
            vector unsigned char perm, perm0 = vec_lvsl(j << 1, lumFilter);
            vLumFilter = vec_perm(vLumFilter, vLumFilter, perm0);
            vLumFilter = vec_splat(vLumFilter, 0); // lumFilter[j] is loaded 8 times in vLumFilter

            perm = vec_lvsl(0, lumSrc[j]);
            l1 = vec_ld(0, lumSrc[j]);

            for (i = 0; i < (dstW - 7); i+=8) {
                int offset = i << 2;
                vector signed short l2 = vec_ld((i << 1) + 16, lumSrc[j]);

                vector signed int v1 = vec_ld(offset, val);
                vector signed int v2 = vec_ld(offset + 16, val);

                vector signed short ls = vec_perm(l1, l2, perm); // lumSrc[j][i] ... lumSrc[j][i+7]

                vector signed int i1 = vec_mule(vLumFilter, ls);
                vector signed int i2 = vec_mulo(vLumFilter, ls);

                vector signed int vf1 = vec_mergeh(i1, i2);
                vector signed int vf2 = vec_mergel(i1, i2); // lumSrc[j][i] * lumFilter[j] ... lumSrc[j][i+7] * lumFilter[j]

                vector signed int vo1 = vec_add(v1, vf1);
                vector signed int vo2 = vec_add(v2, vf2);

                vec_st(vo1, offset, val);
                vec_st(vo2, offset + 16, val);

                l1 = l2;
            }
            for ( ; i < dstW; i++) {
                val[i] += lumSrc[j][i] * lumFilter[j];
            }
        }
        altivec_packIntArrayToCharArray(val, dest, dstW);
    }
    if (uDest != 0) {
        int  __attribute__ ((aligned (16))) u[chrDstW];
        int  __attribute__ ((aligned (16))) v[chrDstW];

        for (i = 0; i < (chrDstW -7); i+=4) {
            vec_st(vini, i << 2, u);
            vec_st(vini, i << 2, v);
        }
        for (; i < chrDstW; i++) {
            u[i] = (1 << 18);
            v[i] = (1 << 18);
        }

        for (j = 0; j < chrFilterSize; j++) {
            vector signed short l1, l1_V, vChrFilter = vec_ld(j << 1, chrFilter);
            vector unsigned char perm, perm0 = vec_lvsl(j << 1, chrFilter);
            vChrFilter = vec_perm(vChrFilter, vChrFilter, perm0);
            vChrFilter = vec_splat(vChrFilter, 0); // chrFilter[j] is loaded 8 times in vChrFilter

            perm = vec_lvsl(0, chrSrc[j]);
            l1 = vec_ld(0, chrSrc[j]);
            l1_V = vec_ld(2048 << 1, chrSrc[j]);

            for (i = 0; i < (chrDstW - 7); i+=8) {
                int offset = i << 2;
                vector signed short l2 = vec_ld((i << 1) + 16, chrSrc[j]);
                vector signed short l2_V = vec_ld(((i + 2048) << 1) + 16, chrSrc[j]);

                vector signed int v1 = vec_ld(offset, u);
                vector signed int v2 = vec_ld(offset + 16, u);
                vector signed int v1_V = vec_ld(offset, v);
                vector signed int v2_V = vec_ld(offset + 16, v);

                vector signed short ls = vec_perm(l1, l2, perm); // chrSrc[j][i] ... chrSrc[j][i+7]
                vector signed short ls_V = vec_perm(l1_V, l2_V, perm); // chrSrc[j][i+2048] ... chrSrc[j][i+2055]

                vector signed int i1 = vec_mule(vChrFilter, ls);
                vector signed int i2 = vec_mulo(vChrFilter, ls);
                vector signed int i1_V = vec_mule(vChrFilter, ls_V);
                vector signed int i2_V = vec_mulo(vChrFilter, ls_V);

                vector signed int vf1 = vec_mergeh(i1, i2);
                vector signed int vf2 = vec_mergel(i1, i2); // chrSrc[j][i] * chrFilter[j] ... chrSrc[j][i+7] * chrFilter[j]
                vector signed int vf1_V = vec_mergeh(i1_V, i2_V);
                vector signed int vf2_V = vec_mergel(i1_V, i2_V); // chrSrc[j][i] * chrFilter[j] ... chrSrc[j][i+7] * chrFilter[j]

                vector signed int vo1 = vec_add(v1, vf1);
                vector signed int vo2 = vec_add(v2, vf2);
                vector signed int vo1_V = vec_add(v1_V, vf1_V);
                vector signed int vo2_V = vec_add(v2_V, vf2_V);

                vec_st(vo1, offset, u);
                vec_st(vo2, offset + 16, u);
                vec_st(vo1_V, offset, v);
                vec_st(vo2_V, offset + 16, v);

                l1 = l2;
                l1_V = l2_V;
            }
            for ( ; i < chrDstW; i++) {
                u[i] += chrSrc[j][i] * chrFilter[j];
                v[i] += chrSrc[j][i + 2048] * chrFilter[j];
            }
        }
        altivec_packIntArrayToCharArray(u, uDest, chrDstW);
        altivec_packIntArrayToCharArray(v, vDest, chrDstW);
    }
}

static inline void hScale_altivec_real(int16_t *dst, int dstW, uint8_t *src, int srcW, int xInc, int16_t *filter, int16_t *filterPos, int filterSize) {
    register int i;
    int __attribute__ ((aligned (16))) tempo[4];

    if (filterSize % 4) {
        for (i=0; i<dstW; i++) {
            register int j;
            register int srcPos = filterPos[i];
            register int val = 0;
            for (j=0; j<filterSize; j++) {
                val += ((int)src[srcPos + j])*filter[filterSize*i + j];
            }
            dst[i] = FFMIN(val>>7, (1<<15)-1);
        }
    }
    else
    switch (filterSize) {
    case 4:
    {
    for (i=0; i<dstW; i++) {
        register int srcPos = filterPos[i];

        vector unsigned char src_v0 = vec_ld(srcPos, src);
        vector unsigned char src_v1, src_vF;
        vector signed short src_v, filter_v;
        vector signed int val_vEven, val_s;
        if ((((int)src + srcPos)% 16) > 12) {
            src_v1 = vec_ld(srcPos + 16, src);
        }
        src_vF = vec_perm(src_v0, src_v1, vec_lvsl(srcPos, src));

        src_v = // vec_unpackh sign-extends...
            (vector signed short)(vec_mergeh((vector unsigned char)vzero, src_vF));
        // now put our elements in the even slots
        src_v = vec_mergeh(src_v, (vector signed short)vzero);

        filter_v = vec_ld(i << 3, filter);
        // The 3 above is 2 (filterSize == 4) + 1 (sizeof(short) == 2).

        // The neat trick: We only care for half the elements,
        // high or low depending on (i<<3)%16 (it's 0 or 8 here),
        // and we're going to use vec_mule, so we choose
        // carefully how to "unpack" the elements into the even slots.
        if ((i << 3) % 16)
            filter_v = vec_mergel(filter_v, (vector signed short)vzero);
        else
            filter_v = vec_mergeh(filter_v, (vector signed short)vzero);

        val_vEven = vec_mule(src_v, filter_v);
        val_s = vec_sums(val_vEven, vzero);
        vec_st(val_s, 0, tempo);
        dst[i] = FFMIN(tempo[3]>>7, (1<<15)-1);
    }
    }
    break;

    case 8:
    {
    for (i=0; i<dstW; i++) {
        register int srcPos = filterPos[i];

        vector unsigned char src_v0 = vec_ld(srcPos, src);
        vector unsigned char src_v1, src_vF;
        vector signed short src_v, filter_v;
        vector signed int val_v, val_s;
        if ((((int)src + srcPos)% 16) > 8) {
            src_v1 = vec_ld(srcPos + 16, src);
        }
        src_vF = vec_perm(src_v0, src_v1, vec_lvsl(srcPos, src));

        src_v = // vec_unpackh sign-extends...
            (vector signed short)(vec_mergeh((vector unsigned char)vzero, src_vF));
        filter_v = vec_ld(i << 4, filter);
        // the 4 above is 3 (filterSize == 8) + 1 (sizeof(short) == 2)

        val_v = vec_msums(src_v, filter_v, (vector signed int)vzero);
        val_s = vec_sums(val_v, vzero);
        vec_st(val_s, 0, tempo);
        dst[i] = FFMIN(tempo[3]>>7, (1<<15)-1);
    }
    }
    break;

    case 16:
    {
        for (i=0; i<dstW; i++) {
            register int srcPos = filterPos[i];

            vector unsigned char src_v0 = vec_ld(srcPos, src);
            vector unsigned char src_v1 = vec_ld(srcPos + 16, src);
            vector unsigned char src_vF = vec_perm(src_v0, src_v1, vec_lvsl(srcPos, src));

            vector signed short src_vA = // vec_unpackh sign-extends...
                (vector signed short)(vec_mergeh((vector unsigned char)vzero, src_vF));
            vector signed short src_vB = // vec_unpackh sign-extends...
                (vector signed short)(vec_mergel((vector unsigned char)vzero, src_vF));

            vector signed short filter_v0 = vec_ld(i << 5, filter);
            vector signed short filter_v1 = vec_ld((i << 5) + 16, filter);
            // the 5 above are 4 (filterSize == 16) + 1 (sizeof(short) == 2)

            vector signed int val_acc = vec_msums(src_vA, filter_v0, (vector signed int)vzero);
            vector signed int val_v = vec_msums(src_vB, filter_v1, val_acc);

            vector signed int val_s = vec_sums(val_v, vzero);

            vec_st(val_s, 0, tempo);
            dst[i] = FFMIN(tempo[3]>>7, (1<<15)-1);
        }
    }
    break;

    default:
    {
    for (i=0; i<dstW; i++) {
        register int j;
        register int srcPos = filterPos[i];

        vector signed int val_s, val_v = (vector signed int)vzero;
        vector signed short filter_v0R = vec_ld(i * 2 * filterSize, filter);
        vector unsigned char permF = vec_lvsl((i * 2 * filterSize), filter);

        vector unsigned char src_v0 = vec_ld(srcPos, src);
        vector unsigned char permS = vec_lvsl(srcPos, src);

        for (j = 0 ; j < filterSize - 15; j += 16) {
            vector unsigned char src_v1 = vec_ld(srcPos + j + 16, src);
            vector unsigned char src_vF = vec_perm(src_v0, src_v1, permS);

            vector signed short src_vA = // vec_unpackh sign-extends...
                (vector signed short)(vec_mergeh((vector unsigned char)vzero, src_vF));
            vector signed short src_vB = // vec_unpackh sign-extends...
                (vector signed short)(vec_mergel((vector unsigned char)vzero, src_vF));

            vector signed short filter_v1R = vec_ld((i * 2 * filterSize) + (j * 2) + 16, filter);
            vector signed short filter_v2R = vec_ld((i * 2 * filterSize) + (j * 2) + 32, filter);
            vector signed short filter_v0  = vec_perm(filter_v0R, filter_v1R, permF);
            vector signed short filter_v1  = vec_perm(filter_v1R, filter_v2R, permF);

            vector signed int val_acc = vec_msums(src_vA, filter_v0, val_v);
            val_v = vec_msums(src_vB, filter_v1, val_acc);

            filter_v0R = filter_v2R;
            src_v0 = src_v1;
        }

        if (j < filterSize-7) {
            // loading src_v0 is useless, it's already done above
            //vector unsigned char src_v0 = vec_ld(srcPos + j, src);
            vector unsigned char src_v1, src_vF;
            vector signed short src_v, filter_v1R, filter_v;
            if ((((int)src + srcPos)% 16) > 8) {
                src_v1 = vec_ld(srcPos + j + 16, src);
            }
            src_vF = vec_perm(src_v0, src_v1, permS);

            src_v = // vec_unpackh sign-extends...
                (vector signed short)(vec_mergeh((vector unsigned char)vzero, src_vF));
            // loading filter_v0R is useless, it's already done above
            //vector signed short filter_v0R = vec_ld((i * 2 * filterSize) + j, filter);
            filter_v1R = vec_ld((i * 2 * filterSize) + (j * 2) + 16, filter);
            filter_v = vec_perm(filter_v0R, filter_v1R, permF);

            val_v = vec_msums(src_v, filter_v, val_v);
        }

        val_s = vec_sums(val_v, vzero);

        vec_st(val_s, 0, tempo);
        dst[i] = FFMIN(tempo[3]>>7, (1<<15)-1);
    }

    }
    }
}

static inline int yv12toyuy2_unscaled_altivec(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY,
                                              int srcSliceH, uint8_t* dstParam[], int dstStride_a[]) {
    uint8_t *dst=dstParam[0] + dstStride_a[0]*srcSliceY;
    // yv12toyuy2(src[0], src[1], src[2], dst, c->srcW, srcSliceH, srcStride[0], srcStride[1], dstStride[0]);
    uint8_t *ysrc = src[0];
    uint8_t *usrc = src[1];
    uint8_t *vsrc = src[2];
    const int width = c->srcW;
    const int height = srcSliceH;
    const int lumStride = srcStride[0];
    const int chromStride = srcStride[1];
    const int dstStride = dstStride_a[0];
    const vector unsigned char yperm = vec_lvsl(0, ysrc);
    const int vertLumPerChroma = 2;
    register unsigned int y;

    if (width&15) {
        yv12toyuy2(ysrc, usrc, vsrc, dst, c->srcW, srcSliceH, lumStride, chromStride, dstStride);
        return srcSliceH;
    }

    /* This code assumes:

    1) dst is 16 bytes-aligned
    2) dstStride is a multiple of 16
    3) width is a multiple of 16
    4) lum & chrom stride are multiples of 8
    */

    for (y=0; y<height; y++) {
        int i;
        for (i = 0; i < width - 31; i+= 32) {
            const unsigned int j = i >> 1;
            vector unsigned char v_yA = vec_ld(i, ysrc);
            vector unsigned char v_yB = vec_ld(i + 16, ysrc);
            vector unsigned char v_yC = vec_ld(i + 32, ysrc);
            vector unsigned char v_y1 = vec_perm(v_yA, v_yB, yperm);
            vector unsigned char v_y2 = vec_perm(v_yB, v_yC, yperm);
            vector unsigned char v_uA = vec_ld(j, usrc);
            vector unsigned char v_uB = vec_ld(j + 16, usrc);
            vector unsigned char v_u = vec_perm(v_uA, v_uB, vec_lvsl(j, usrc));
            vector unsigned char v_vA = vec_ld(j, vsrc);
            vector unsigned char v_vB = vec_ld(j + 16, vsrc);
            vector unsigned char v_v = vec_perm(v_vA, v_vB, vec_lvsl(j, vsrc));
            vector unsigned char v_uv_a = vec_mergeh(v_u, v_v);
            vector unsigned char v_uv_b = vec_mergel(v_u, v_v);
            vector unsigned char v_yuy2_0 = vec_mergeh(v_y1, v_uv_a);
            vector unsigned char v_yuy2_1 = vec_mergel(v_y1, v_uv_a);
            vector unsigned char v_yuy2_2 = vec_mergeh(v_y2, v_uv_b);
            vector unsigned char v_yuy2_3 = vec_mergel(v_y2, v_uv_b);
            vec_st(v_yuy2_0, (i << 1), dst);
            vec_st(v_yuy2_1, (i << 1) + 16, dst);
            vec_st(v_yuy2_2, (i << 1) + 32, dst);
            vec_st(v_yuy2_3, (i << 1) + 48, dst);
        }
        if (i < width) {
            const unsigned int j = i >> 1;
            vector unsigned char v_y1 = vec_ld(i, ysrc);
            vector unsigned char v_u = vec_ld(j, usrc);
            vector unsigned char v_v = vec_ld(j, vsrc);
            vector unsigned char v_uv_a = vec_mergeh(v_u, v_v);
            vector unsigned char v_yuy2_0 = vec_mergeh(v_y1, v_uv_a);
            vector unsigned char v_yuy2_1 = vec_mergel(v_y1, v_uv_a);
            vec_st(v_yuy2_0, (i << 1), dst);
            vec_st(v_yuy2_1, (i << 1) + 16, dst);
        }
        if ((y&(vertLumPerChroma-1)) == vertLumPerChroma-1) {
            usrc += chromStride;
            vsrc += chromStride;
        }
        ysrc += lumStride;
        dst += dstStride;
    }

    return srcSliceH;
}

static inline int yv12touyvy_unscaled_altivec(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY,
                                              int srcSliceH, uint8_t* dstParam[], int dstStride_a[]) {
    uint8_t *dst=dstParam[0] + dstStride_a[0]*srcSliceY;
    // yv12toyuy2(src[0], src[1], src[2], dst, c->srcW, srcSliceH, srcStride[0], srcStride[1], dstStride[0]);
    uint8_t *ysrc = src[0];
    uint8_t *usrc = src[1];
    uint8_t *vsrc = src[2];
    const int width = c->srcW;
    const int height = srcSliceH;
    const int lumStride = srcStride[0];
    const int chromStride = srcStride[1];
    const int dstStride = dstStride_a[0];
    const int vertLumPerChroma = 2;
    const vector unsigned char yperm = vec_lvsl(0, ysrc);
    register unsigned int y;

    if (width&15) {
        yv12touyvy(ysrc, usrc, vsrc, dst, c->srcW, srcSliceH, lumStride, chromStride, dstStride);
        return srcSliceH;
    }

    /* This code assumes:

    1) dst is 16 bytes-aligned
    2) dstStride is a multiple of 16
    3) width is a multiple of 16
    4) lum & chrom stride are multiples of 8
    */

    for (y=0; y<height; y++) {
        int i;
        for (i = 0; i < width - 31; i+= 32) {
            const unsigned int j = i >> 1;
            vector unsigned char v_yA = vec_ld(i, ysrc);
            vector unsigned char v_yB = vec_ld(i + 16, ysrc);
            vector unsigned char v_yC = vec_ld(i + 32, ysrc);
            vector unsigned char v_y1 = vec_perm(v_yA, v_yB, yperm);
            vector unsigned char v_y2 = vec_perm(v_yB, v_yC, yperm);
            vector unsigned char v_uA = vec_ld(j, usrc);
            vector unsigned char v_uB = vec_ld(j + 16, usrc);
            vector unsigned char v_u = vec_perm(v_uA, v_uB, vec_lvsl(j, usrc));
            vector unsigned char v_vA = vec_ld(j, vsrc);
            vector unsigned char v_vB = vec_ld(j + 16, vsrc);
            vector unsigned char v_v = vec_perm(v_vA, v_vB, vec_lvsl(j, vsrc));
            vector unsigned char v_uv_a = vec_mergeh(v_u, v_v);
            vector unsigned char v_uv_b = vec_mergel(v_u, v_v);
            vector unsigned char v_uyvy_0 = vec_mergeh(v_uv_a, v_y1);
            vector unsigned char v_uyvy_1 = vec_mergel(v_uv_a, v_y1);
            vector unsigned char v_uyvy_2 = vec_mergeh(v_uv_b, v_y2);
            vector unsigned char v_uyvy_3 = vec_mergel(v_uv_b, v_y2);
            vec_st(v_uyvy_0, (i << 1), dst);
            vec_st(v_uyvy_1, (i << 1) + 16, dst);
            vec_st(v_uyvy_2, (i << 1) + 32, dst);
            vec_st(v_uyvy_3, (i << 1) + 48, dst);
        }
        if (i < width) {
            const unsigned int j = i >> 1;
            vector unsigned char v_y1 = vec_ld(i, ysrc);
            vector unsigned char v_u = vec_ld(j, usrc);
            vector unsigned char v_v = vec_ld(j, vsrc);
            vector unsigned char v_uv_a = vec_mergeh(v_u, v_v);
            vector unsigned char v_uyvy_0 = vec_mergeh(v_uv_a, v_y1);
            vector unsigned char v_uyvy_1 = vec_mergel(v_uv_a, v_y1);
            vec_st(v_uyvy_0, (i << 1), dst);
            vec_st(v_uyvy_1, (i << 1) + 16, dst);
        }
        if ((y&(vertLumPerChroma-1)) == vertLumPerChroma-1) {
            usrc += chromStride;
            vsrc += chromStride;
        }
        ysrc += lumStride;
        dst += dstStride;
    }
    return srcSliceH;
}
