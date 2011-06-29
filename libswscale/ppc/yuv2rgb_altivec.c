/*
 * AltiVec acceleration for colorspace conversion
 *
 * copyright (C) 2004 Marc Hoffman <marc.hoffman@analog.com>
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

/*
Convert I420 YV12 to RGB in various formats,
  it rejects images that are not in 420 formats,
  it rejects images that don't have widths of multiples of 16,
  it rejects images that don't have heights of multiples of 2.
Reject defers to C simulation code.

Lots of optimizations to be done here.

1. Need to fix saturation code. I just couldn't get it to fly with packs
   and adds, so we currently use max/min to clip.

2. The inefficient use of chroma loading needs a bit of brushing up.

3. Analysis of pipeline stalls needs to be done. Use shark to identify
   pipeline stalls.


MODIFIED to calculate coeffs from currently selected color space.
MODIFIED core to be a macro where you specify the output format.
ADDED UYVY conversion which is never called due to some thing in swscale.
CORRECTED algorithim selection to be strict on input formats.
ADDED runtime detection of AltiVec.

ADDED altivec_yuv2packedX vertical scl + RGB converter

March 27,2004
PERFORMANCE ANALYSIS

The C version uses 25% of the processor or ~250Mips for D1 video rawvideo
used as test.
The AltiVec version uses 10% of the processor or ~100Mips for D1 video
same sequence.

720 * 480 * 30  ~10MPS

so we have roughly 10 clocks per pixel. This is too high, something has
to be wrong.

OPTIMIZED clip codes to utilize vec_max and vec_packs removing the
need for vec_min.

OPTIMIZED DST OUTPUT cache/DMA controls. We are pretty much guaranteed to have
the input video frame, it was just decompressed so it probably resides in L1
caches. However, we are creating the output video stream. This needs to use the
DSTST instruction to optimize for the cache. We couple this with the fact that
we are not going to be visiting the input buffer again so we mark it Least
Recently Used. This shaves 25% of the processor cycles off.

Now memcpy is the largest mips consumer in the system, probably due
to the inefficient X11 stuff.

GL libraries seem to be very slow on this machine 1.33Ghz PB running
Jaguar, this is not the case for my 1Ghz PB.  I thought it might be
a versioning issue, however I have libGL.1.2.dylib for both
machines. (We need to figure this out now.)

GL2 libraries work now with patch for RGB32.

NOTE: quartz vo driver ARGB32_to_RGB24 consumes 30% of the processor.

Integrated luma prescaling adjustment for saturation/contrast/brightness
adjustment.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include "config.h"
#include "libswscale/rgb2rgb.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"
#include "libavutil/cpu.h"
#include "libavutil/pixdesc.h"
#include "yuv2rgb_altivec.h"

#undef PROFILE_THE_BEAST
#undef INC_SCALING

typedef unsigned char ubyte;
typedef signed char   sbyte;


/* RGB interleaver, 16 planar pels 8-bit samples per channel in
   homogeneous vector registers x0,x1,x2 are interleaved with the
   following technique:

      o0 = vec_mergeh (x0,x1);
      o1 = vec_perm (o0, x2, perm_rgb_0);
      o2 = vec_perm (o0, x2, perm_rgb_1);
      o3 = vec_mergel (x0,x1);
      o4 = vec_perm (o3,o2,perm_rgb_2);
      o5 = vec_perm (o3,o2,perm_rgb_3);

  perm_rgb_0:   o0(RG).h v1(B) --> o1*
              0   1  2   3   4
             rgbr|gbrg|brgb|rgbr
             0010 0100 1001 0010
             0102 3145 2673 894A

  perm_rgb_1:   o0(RG).h v1(B) --> o2
              0   1  2   3   4
             gbrg|brgb|bbbb|bbbb
             0100 1001 1111 1111
             B5CD 6EF7 89AB CDEF

  perm_rgb_2:   o3(RG).l o2(rgbB.l) --> o4*
              0   1  2   3   4
             gbrg|brgb|rgbr|gbrg
             1111 1111 0010 0100
             89AB CDEF 0182 3945

  perm_rgb_2:   o3(RG).l o2(rgbB.l) ---> o5*
              0   1  2   3   4
             brgb|rgbr|gbrg|brgb
             1001 0010 0100 1001
             a67b 89cA BdCD eEFf

*/
static
const vector unsigned char
  perm_rgb_0 = {0x00,0x01,0x10,0x02,0x03,0x11,0x04,0x05,
                0x12,0x06,0x07,0x13,0x08,0x09,0x14,0x0a},
  perm_rgb_1 = {0x0b,0x15,0x0c,0x0d,0x16,0x0e,0x0f,0x17,
                0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f},
  perm_rgb_2 = {0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
                0x00,0x01,0x18,0x02,0x03,0x19,0x04,0x05},
  perm_rgb_3 = {0x1a,0x06,0x07,0x1b,0x08,0x09,0x1c,0x0a,
                0x0b,0x1d,0x0c,0x0d,0x1e,0x0e,0x0f,0x1f};

#define vec_merge3(x2,x1,x0,y0,y1,y2)       \
do {                                        \
    __typeof__(x0) o0,o2,o3;                \
        o0 = vec_mergeh (x0,x1);            \
        y0 = vec_perm (o0, x2, perm_rgb_0); \
        o2 = vec_perm (o0, x2, perm_rgb_1); \
        o3 = vec_mergel (x0,x1);            \
        y1 = vec_perm (o3,o2,perm_rgb_2);   \
        y2 = vec_perm (o3,o2,perm_rgb_3);   \
} while(0)

#define vec_mstbgr24(x0,x1,x2,ptr)      \
do {                                    \
    __typeof__(x0) _0,_1,_2;            \
    vec_merge3 (x0,x1,x2,_0,_1,_2);     \
    vec_st (_0, 0, ptr++);              \
    vec_st (_1, 0, ptr++);              \
    vec_st (_2, 0, ptr++);              \
}  while (0)

#define vec_mstrgb24(x0,x1,x2,ptr)      \
do {                                    \
    __typeof__(x0) _0,_1,_2;            \
    vec_merge3 (x2,x1,x0,_0,_1,_2);     \
    vec_st (_0, 0, ptr++);              \
    vec_st (_1, 0, ptr++);              \
    vec_st (_2, 0, ptr++);              \
}  while (0)

/* pack the pixels in rgb0 format
   msb R
   lsb 0
*/
#define vec_mstrgb32(T,x0,x1,x2,x3,ptr)                                       \
do {                                                                          \
    T _0,_1,_2,_3;                                                            \
    _0 = vec_mergeh (x0,x1);                                                  \
    _1 = vec_mergeh (x2,x3);                                                  \
    _2 = (T)vec_mergeh ((vector unsigned short)_0,(vector unsigned short)_1); \
    _3 = (T)vec_mergel ((vector unsigned short)_0,(vector unsigned short)_1); \
    vec_st (_2, 0*16, (T *)ptr);                                              \
    vec_st (_3, 1*16, (T *)ptr);                                              \
    _0 = vec_mergel (x0,x1);                                                  \
    _1 = vec_mergel (x2,x3);                                                  \
    _2 = (T)vec_mergeh ((vector unsigned short)_0,(vector unsigned short)_1); \
    _3 = (T)vec_mergel ((vector unsigned short)_0,(vector unsigned short)_1); \
    vec_st (_2, 2*16, (T *)ptr);                                              \
    vec_st (_3, 3*16, (T *)ptr);                                              \
    ptr += 4;                                                                 \
}  while (0)

/*

  | 1     0       1.4021   | | Y |
  | 1    -0.3441 -0.7142   |x| Cb|
  | 1     1.7718  0        | | Cr|


  Y:      [-128 127]
  Cb/Cr : [-128 127]

  typical yuv conversion work on Y: 0-255 this version has been optimized for jpeg decode.

*/




#define vec_unh(x) \
    (vector signed short) \
        vec_perm(x,(__typeof__(x)){0}, \
                 ((vector unsigned char){0x10,0x00,0x10,0x01,0x10,0x02,0x10,0x03,\
                                         0x10,0x04,0x10,0x05,0x10,0x06,0x10,0x07}))
#define vec_unl(x) \
    (vector signed short) \
        vec_perm(x,(__typeof__(x)){0}, \
                 ((vector unsigned char){0x10,0x08,0x10,0x09,0x10,0x0A,0x10,0x0B,\
                                         0x10,0x0C,0x10,0x0D,0x10,0x0E,0x10,0x0F}))

#define vec_clip_s16(x) \
    vec_max (vec_min (x, ((vector signed short){235,235,235,235,235,235,235,235})), \
                         ((vector signed short){ 16, 16, 16, 16, 16, 16, 16, 16}))

#define vec_packclp(x,y) \
    (vector unsigned char)vec_packs \
        ((vector unsigned short)vec_max (x,((vector signed short) {0})), \
         (vector unsigned short)vec_max (y,((vector signed short) {0})))

//#define out_pixels(a,b,c,ptr) vec_mstrgb32(__typeof__(a),((__typeof__ (a)){255}),a,a,a,ptr)


static inline void cvtyuvtoRGB (SwsContext *c,
                                vector signed short Y, vector signed short U, vector signed short V,
                                vector signed short *R, vector signed short *G, vector signed short *B)
{
    vector signed   short vx,ux,uvx;

    Y = vec_mradds (Y, c->CY, c->OY);
    U  = vec_sub (U,(vector signed short)
                    vec_splat((vector signed short){128},0));
    V  = vec_sub (V,(vector signed short)
                    vec_splat((vector signed short){128},0));

    //   ux  = (CBU*(u<<c->CSHIFT)+0x4000)>>15;
    ux = vec_sl (U, c->CSHIFT);
    *B = vec_mradds (ux, c->CBU, Y);

    // vx  = (CRV*(v<<c->CSHIFT)+0x4000)>>15;
    vx = vec_sl (V, c->CSHIFT);
    *R = vec_mradds (vx, c->CRV, Y);

    // uvx = ((CGU*u) + (CGV*v))>>15;
    uvx = vec_mradds (U, c->CGU, Y);
    *G  = vec_mradds (V, c->CGV, uvx);
}


/*
  ------------------------------------------------------------------------------
  CS converters
  ------------------------------------------------------------------------------
*/


#define DEFCSP420_CVT(name,out_pixels)                                  \
static int altivec_##name (SwsContext *c,                               \
                           const unsigned char **in, int *instrides,    \
                           int srcSliceY,        int srcSliceH,         \
                           unsigned char **oplanes, int *outstrides)    \
{                                                                       \
    int w = c->srcW;                                                    \
    int h = srcSliceH;                                                  \
    int i,j;                                                            \
    int instrides_scl[3];                                               \
    vector unsigned char y0,y1;                                         \
                                                                        \
    vector signed char  u,v;                                            \
                                                                        \
    vector signed short Y0,Y1,Y2,Y3;                                    \
    vector signed short U,V;                                            \
    vector signed short vx,ux,uvx;                                      \
    vector signed short vx0,ux0,uvx0;                                   \
    vector signed short vx1,ux1,uvx1;                                   \
    vector signed short R0,G0,B0;                                       \
    vector signed short R1,G1,B1;                                       \
    vector unsigned char R,G,B;                                         \
                                                                        \
    const vector unsigned char *y1ivP, *y2ivP, *uivP, *vivP;            \
    vector unsigned char align_perm;                                    \
                                                                        \
    vector signed short                                                 \
        lCY  = c->CY,                                                   \
        lOY  = c->OY,                                                   \
        lCRV = c->CRV,                                                  \
        lCBU = c->CBU,                                                  \
        lCGU = c->CGU,                                                  \
        lCGV = c->CGV;                                                  \
                                                                        \
    vector unsigned short lCSHIFT = c->CSHIFT;                          \
                                                                        \
    const ubyte *y1i   = in[0];                                         \
    const ubyte *y2i   = in[0]+instrides[0];                            \
    const ubyte *ui    = in[1];                                         \
    const ubyte *vi    = in[2];                                         \
                                                                        \
    vector unsigned char *oute                                          \
        = (vector unsigned char *)                                      \
            (oplanes[0]+srcSliceY*outstrides[0]);                       \
    vector unsigned char *outo                                          \
        = (vector unsigned char *)                                      \
            (oplanes[0]+srcSliceY*outstrides[0]+outstrides[0]);         \
                                                                        \
                                                                        \
    instrides_scl[0] = instrides[0]*2-w;  /* the loop moves y{1,2}i by w */ \
    instrides_scl[1] = instrides[1]-w/2;  /* the loop moves ui by w/2 */    \
    instrides_scl[2] = instrides[2]-w/2;  /* the loop moves vi by w/2 */    \
                                                                        \
                                                                        \
    for (i=0;i<h/2;i++) {                                               \
        vec_dstst (outo, (0x02000002|(((w*3+32)/32)<<16)), 0);          \
        vec_dstst (oute, (0x02000002|(((w*3+32)/32)<<16)), 1);          \
                                                                        \
        for (j=0;j<w/16;j++) {                                          \
                                                                        \
            y1ivP = (const vector unsigned char *)y1i;                  \
            y2ivP = (const vector unsigned char *)y2i;                  \
            uivP  = (const vector unsigned char *)ui;                   \
            vivP  = (const vector unsigned char *)vi;                   \
                                                                        \
            align_perm = vec_lvsl (0, y1i);                             \
            y0 = (vector unsigned char)                                 \
                 vec_perm (y1ivP[0], y1ivP[1], align_perm);             \
                                                                        \
            align_perm = vec_lvsl (0, y2i);                             \
            y1 = (vector unsigned char)                                 \
                 vec_perm (y2ivP[0], y2ivP[1], align_perm);             \
                                                                        \
            align_perm = vec_lvsl (0, ui);                              \
            u = (vector signed char)                                    \
                vec_perm (uivP[0], uivP[1], align_perm);                \
                                                                        \
            align_perm = vec_lvsl (0, vi);                              \
            v = (vector signed char)                                    \
                vec_perm (vivP[0], vivP[1], align_perm);                \
                                                                        \
            u  = (vector signed char)                                   \
                 vec_sub (u,(vector signed char)                        \
                          vec_splat((vector signed char){128},0));      \
            v  = (vector signed char)                                   \
                 vec_sub (v,(vector signed char)                        \
                          vec_splat((vector signed char){128},0));      \
                                                                        \
            U  = vec_unpackh (u);                                       \
            V  = vec_unpackh (v);                                       \
                                                                        \
                                                                        \
            Y0 = vec_unh (y0);                                          \
            Y1 = vec_unl (y0);                                          \
            Y2 = vec_unh (y1);                                          \
            Y3 = vec_unl (y1);                                          \
                                                                        \
            Y0 = vec_mradds (Y0, lCY, lOY);                             \
            Y1 = vec_mradds (Y1, lCY, lOY);                             \
            Y2 = vec_mradds (Y2, lCY, lOY);                             \
            Y3 = vec_mradds (Y3, lCY, lOY);                             \
                                                                        \
            /*   ux  = (CBU*(u<<CSHIFT)+0x4000)>>15 */                  \
            ux = vec_sl (U, lCSHIFT);                                   \
            ux = vec_mradds (ux, lCBU, (vector signed short){0});       \
            ux0  = vec_mergeh (ux,ux);                                  \
            ux1  = vec_mergel (ux,ux);                                  \
                                                                        \
            /* vx  = (CRV*(v<<CSHIFT)+0x4000)>>15;        */            \
            vx = vec_sl (V, lCSHIFT);                                   \
            vx = vec_mradds (vx, lCRV, (vector signed short){0});       \
            vx0  = vec_mergeh (vx,vx);                                  \
            vx1  = vec_mergel (vx,vx);                                  \
                                                                        \
            /* uvx = ((CGU*u) + (CGV*v))>>15 */                         \
            uvx = vec_mradds (U, lCGU, (vector signed short){0});       \
            uvx = vec_mradds (V, lCGV, uvx);                            \
            uvx0 = vec_mergeh (uvx,uvx);                                \
            uvx1 = vec_mergel (uvx,uvx);                                \
                                                                        \
            R0 = vec_add (Y0,vx0);                                      \
            G0 = vec_add (Y0,uvx0);                                     \
            B0 = vec_add (Y0,ux0);                                      \
            R1 = vec_add (Y1,vx1);                                      \
            G1 = vec_add (Y1,uvx1);                                     \
            B1 = vec_add (Y1,ux1);                                      \
                                                                        \
            R  = vec_packclp (R0,R1);                                   \
            G  = vec_packclp (G0,G1);                                   \
            B  = vec_packclp (B0,B1);                                   \
                                                                        \
            out_pixels(R,G,B,oute);                                     \
                                                                        \
            R0 = vec_add (Y2,vx0);                                      \
            G0 = vec_add (Y2,uvx0);                                     \
            B0 = vec_add (Y2,ux0);                                      \
            R1 = vec_add (Y3,vx1);                                      \
            G1 = vec_add (Y3,uvx1);                                     \
            B1 = vec_add (Y3,ux1);                                      \
            R  = vec_packclp (R0,R1);                                   \
            G  = vec_packclp (G0,G1);                                   \
            B  = vec_packclp (B0,B1);                                   \
                                                                        \
                                                                        \
            out_pixels(R,G,B,outo);                                     \
                                                                        \
            y1i  += 16;                                                 \
            y2i  += 16;                                                 \
            ui   += 8;                                                  \
            vi   += 8;                                                  \
                                                                        \
        }                                                               \
                                                                        \
        outo  += (outstrides[0])>>4;                                    \
        oute  += (outstrides[0])>>4;                                    \
                                                                        \
        ui    += instrides_scl[1];                                      \
        vi    += instrides_scl[2];                                      \
        y1i   += instrides_scl[0];                                      \
        y2i   += instrides_scl[0];                                      \
    }                                                                   \
    return srcSliceH;                                                   \
}


#define out_abgr(a,b,c,ptr)  vec_mstrgb32(__typeof__(a),((__typeof__ (a)){255}),c,b,a,ptr)
#define out_bgra(a,b,c,ptr)  vec_mstrgb32(__typeof__(a),c,b,a,((__typeof__ (a)){255}),ptr)
#define out_rgba(a,b,c,ptr)  vec_mstrgb32(__typeof__(a),a,b,c,((__typeof__ (a)){255}),ptr)
#define out_argb(a,b,c,ptr)  vec_mstrgb32(__typeof__(a),((__typeof__ (a)){255}),a,b,c,ptr)
#define out_rgb24(a,b,c,ptr) vec_mstrgb24(a,b,c,ptr)
#define out_bgr24(a,b,c,ptr) vec_mstbgr24(a,b,c,ptr)

DEFCSP420_CVT (yuv2_abgr, out_abgr)
DEFCSP420_CVT (yuv2_bgra, out_bgra)
DEFCSP420_CVT (yuv2_rgba, out_rgba)
DEFCSP420_CVT (yuv2_argb, out_argb)
DEFCSP420_CVT (yuv2_rgb24,  out_rgb24)
DEFCSP420_CVT (yuv2_bgr24,  out_bgr24)


// uyvy|uyvy|uyvy|uyvy
// 0123 4567 89ab cdef
static
const vector unsigned char
    demux_u = {0x10,0x00,0x10,0x00,
               0x10,0x04,0x10,0x04,
               0x10,0x08,0x10,0x08,
               0x10,0x0c,0x10,0x0c},
    demux_v = {0x10,0x02,0x10,0x02,
               0x10,0x06,0x10,0x06,
               0x10,0x0A,0x10,0x0A,
               0x10,0x0E,0x10,0x0E},
    demux_y = {0x10,0x01,0x10,0x03,
               0x10,0x05,0x10,0x07,
               0x10,0x09,0x10,0x0B,
               0x10,0x0D,0x10,0x0F};

/*
  this is so I can play live CCIR raw video
*/
static int altivec_uyvy_rgb32 (SwsContext *c,
                               const unsigned char **in, int *instrides,
                               int srcSliceY,        int srcSliceH,
                               unsigned char **oplanes, int *outstrides)
{
    int w = c->srcW;
    int h = srcSliceH;
    int i,j;
    vector unsigned char uyvy;
    vector signed   short Y,U,V;
    vector signed   short R0,G0,B0,R1,G1,B1;
    vector unsigned char  R,G,B;
    vector unsigned char *out;
    const ubyte *img;

    img = in[0];
    out = (vector unsigned char *)(oplanes[0]+srcSliceY*outstrides[0]);

    for (i=0;i<h;i++) {
        for (j=0;j<w/16;j++) {
            uyvy = vec_ld (0, img);
            U = (vector signed short)
                vec_perm (uyvy, (vector unsigned char){0}, demux_u);

            V = (vector signed short)
                vec_perm (uyvy, (vector unsigned char){0}, demux_v);

            Y = (vector signed short)
                vec_perm (uyvy, (vector unsigned char){0}, demux_y);

            cvtyuvtoRGB (c, Y,U,V,&R0,&G0,&B0);

            uyvy = vec_ld (16, img);
            U = (vector signed short)
                vec_perm (uyvy, (vector unsigned char){0}, demux_u);

            V = (vector signed short)
                vec_perm (uyvy, (vector unsigned char){0}, demux_v);

            Y = (vector signed short)
                vec_perm (uyvy, (vector unsigned char){0}, demux_y);

            cvtyuvtoRGB (c, Y,U,V,&R1,&G1,&B1);

            R  = vec_packclp (R0,R1);
            G  = vec_packclp (G0,G1);
            B  = vec_packclp (B0,B1);

            //      vec_mstbgr24 (R,G,B, out);
            out_rgba (R,G,B,out);

            img += 32;
        }
    }
    return srcSliceH;
}



/* Ok currently the acceleration routine only supports
   inputs of widths a multiple of 16
   and heights a multiple 2

   So we just fall back to the C codes for this.
*/
SwsFunc ff_yuv2rgb_init_altivec(SwsContext *c)
{
    if (!(av_get_cpu_flags() & AV_CPU_FLAG_ALTIVEC))
        return NULL;

    /*
      and this seems not to matter too much I tried a bunch of
      videos with abnormal widths and MPlayer crashes elsewhere.
      mplayer -vo x11 -rawvideo on:w=350:h=240 raw-350x240.eyuv
      boom with X11 bad match.

    */
    if ((c->srcW & 0xf) != 0)    return NULL;

    switch (c->srcFormat) {
    case PIX_FMT_YUV410P:
    case PIX_FMT_YUV420P:
    /*case IMGFMT_CLPL:        ??? */
    case PIX_FMT_GRAY8:
    case PIX_FMT_NV12:
    case PIX_FMT_NV21:
        if ((c->srcH & 0x1) != 0)
            return NULL;

        switch(c->dstFormat) {
        case PIX_FMT_RGB24:
            av_log(c, AV_LOG_WARNING, "ALTIVEC: Color Space RGB24\n");
            return altivec_yuv2_rgb24;
        case PIX_FMT_BGR24:
            av_log(c, AV_LOG_WARNING, "ALTIVEC: Color Space BGR24\n");
            return altivec_yuv2_bgr24;
        case PIX_FMT_ARGB:
            av_log(c, AV_LOG_WARNING, "ALTIVEC: Color Space ARGB\n");
            return altivec_yuv2_argb;
        case PIX_FMT_ABGR:
            av_log(c, AV_LOG_WARNING, "ALTIVEC: Color Space ABGR\n");
            return altivec_yuv2_abgr;
        case PIX_FMT_RGBA:
            av_log(c, AV_LOG_WARNING, "ALTIVEC: Color Space RGBA\n");
            return altivec_yuv2_rgba;
        case PIX_FMT_BGRA:
            av_log(c, AV_LOG_WARNING, "ALTIVEC: Color Space BGRA\n");
            return altivec_yuv2_bgra;
        default: return NULL;
        }
        break;

    case PIX_FMT_UYVY422:
        switch(c->dstFormat) {
        case PIX_FMT_BGR32:
            av_log(c, AV_LOG_WARNING, "ALTIVEC: Color Space UYVY -> RGB32\n");
            return altivec_uyvy_rgb32;
        default: return NULL;
        }
        break;

    }
    return NULL;
}

void ff_yuv2rgb_init_tables_altivec(SwsContext *c, const int inv_table[4], int brightness, int contrast, int saturation)
{
    union {
        DECLARE_ALIGNED(16, signed short, tmp)[8];
        vector signed short vec;
    } buf;

    buf.tmp[0] =  ((0xffffLL) * contrast>>8)>>9;                        //cy
    buf.tmp[1] =  -256*brightness;                                      //oy
    buf.tmp[2] =  (inv_table[0]>>3) *(contrast>>16)*(saturation>>16);   //crv
    buf.tmp[3] =  (inv_table[1]>>3) *(contrast>>16)*(saturation>>16);   //cbu
    buf.tmp[4] = -((inv_table[2]>>1)*(contrast>>16)*(saturation>>16));  //cgu
    buf.tmp[5] = -((inv_table[3]>>1)*(contrast>>16)*(saturation>>16));  //cgv


    c->CSHIFT = (vector unsigned short)vec_splat_u16(2);
    c->CY   = vec_splat ((vector signed short)buf.vec, 0);
    c->OY   = vec_splat ((vector signed short)buf.vec, 1);
    c->CRV  = vec_splat ((vector signed short)buf.vec, 2);
    c->CBU  = vec_splat ((vector signed short)buf.vec, 3);
    c->CGU  = vec_splat ((vector signed short)buf.vec, 4);
    c->CGV  = vec_splat ((vector signed short)buf.vec, 5);
    return;
}


static av_always_inline void
ff_yuv2packedX_altivec(SwsContext *c, const int16_t *lumFilter,
                       const int16_t **lumSrc, int lumFilterSize,
                       const int16_t *chrFilter, const int16_t **chrUSrc,
                       const int16_t **chrVSrc, int chrFilterSize,
                       const int16_t **alpSrc, uint8_t *dest,
                       int dstW, int dstY, enum PixelFormat target)
{
    int i,j;
    vector signed short X,X0,X1,Y0,U0,V0,Y1,U1,V1,U,V;
    vector signed short R0,G0,B0,R1,G1,B1;

    vector unsigned char R,G,B;
    vector unsigned char *out,*nout;

    vector signed short   RND = vec_splat_s16(1<<3);
    vector unsigned short SCL = vec_splat_u16(4);
    DECLARE_ALIGNED(16, unsigned int, scratch)[16];

    vector signed short *YCoeffs, *CCoeffs;

    YCoeffs = c->vYCoeffsBank+dstY*lumFilterSize;
    CCoeffs = c->vCCoeffsBank+dstY*chrFilterSize;

    out = (vector unsigned char *)dest;

    for (i=0; i<dstW; i+=16) {
        Y0 = RND;
        Y1 = RND;
        /* extract 16 coeffs from lumSrc */
        for (j=0; j<lumFilterSize; j++) {
            X0 = vec_ld (0,  &lumSrc[j][i]);
            X1 = vec_ld (16, &lumSrc[j][i]);
            Y0 = vec_mradds (X0, YCoeffs[j], Y0);
            Y1 = vec_mradds (X1, YCoeffs[j], Y1);
        }

        U = RND;
        V = RND;
        /* extract 8 coeffs from U,V */
        for (j=0; j<chrFilterSize; j++) {
            X  = vec_ld (0, &chrUSrc[j][i/2]);
            U  = vec_mradds (X, CCoeffs[j], U);
            X  = vec_ld (0, &chrVSrc[j][i/2]);
            V  = vec_mradds (X, CCoeffs[j], V);
        }

        /* scale and clip signals */
        Y0 = vec_sra (Y0, SCL);
        Y1 = vec_sra (Y1, SCL);
        U  = vec_sra (U,  SCL);
        V  = vec_sra (V,  SCL);

        Y0 = vec_clip_s16 (Y0);
        Y1 = vec_clip_s16 (Y1);
        U  = vec_clip_s16 (U);
        V  = vec_clip_s16 (V);

        /* now we have
          Y0= y0 y1 y2 y3 y4 y5 y6 y7     Y1= y8 y9 y10 y11 y12 y13 y14 y15
          U= u0 u1 u2 u3 u4 u5 u6 u7      V= v0 v1 v2 v3 v4 v5 v6 v7

          Y0= y0 y1 y2 y3 y4 y5 y6 y7    Y1= y8 y9 y10 y11 y12 y13 y14 y15
          U0= u0 u0 u1 u1 u2 u2 u3 u3    U1= u4 u4 u5 u5 u6 u6 u7 u7
          V0= v0 v0 v1 v1 v2 v2 v3 v3    V1= v4 v4 v5 v5 v6 v6 v7 v7
        */

        U0 = vec_mergeh (U,U);
        V0 = vec_mergeh (V,V);

        U1 = vec_mergel (U,U);
        V1 = vec_mergel (V,V);

        cvtyuvtoRGB (c, Y0,U0,V0,&R0,&G0,&B0);
        cvtyuvtoRGB (c, Y1,U1,V1,&R1,&G1,&B1);

        R  = vec_packclp (R0,R1);
        G  = vec_packclp (G0,G1);
        B  = vec_packclp (B0,B1);

        switch(target) {
        case PIX_FMT_ABGR:  out_abgr  (R,G,B,out); break;
        case PIX_FMT_BGRA:  out_bgra  (R,G,B,out); break;
        case PIX_FMT_RGBA:  out_rgba  (R,G,B,out); break;
        case PIX_FMT_ARGB:  out_argb  (R,G,B,out); break;
        case PIX_FMT_RGB24: out_rgb24 (R,G,B,out); break;
        case PIX_FMT_BGR24: out_bgr24 (R,G,B,out); break;
        default:
            {
                /* If this is reached, the caller should have called yuv2packedXinC
                   instead. */
                static int printed_error_message;
                if (!printed_error_message) {
                    av_log(c, AV_LOG_ERROR, "altivec_yuv2packedX doesn't support %s output\n",
                           av_get_pix_fmt_name(c->dstFormat));
                    printed_error_message=1;
                }
                return;
            }
        }
    }

    if (i < dstW) {
        i -= 16;

        Y0 = RND;
        Y1 = RND;
        /* extract 16 coeffs from lumSrc */
        for (j=0; j<lumFilterSize; j++) {
            X0 = vec_ld (0,  &lumSrc[j][i]);
            X1 = vec_ld (16, &lumSrc[j][i]);
            Y0 = vec_mradds (X0, YCoeffs[j], Y0);
            Y1 = vec_mradds (X1, YCoeffs[j], Y1);
        }

        U = RND;
        V = RND;
        /* extract 8 coeffs from U,V */
        for (j=0; j<chrFilterSize; j++) {
            X  = vec_ld (0, &chrUSrc[j][i/2]);
            U  = vec_mradds (X, CCoeffs[j], U);
            X  = vec_ld (0, &chrVSrc[j][i/2]);
            V  = vec_mradds (X, CCoeffs[j], V);
        }

        /* scale and clip signals */
        Y0 = vec_sra (Y0, SCL);
        Y1 = vec_sra (Y1, SCL);
        U  = vec_sra (U,  SCL);
        V  = vec_sra (V,  SCL);

        Y0 = vec_clip_s16 (Y0);
        Y1 = vec_clip_s16 (Y1);
        U  = vec_clip_s16 (U);
        V  = vec_clip_s16 (V);

        /* now we have
           Y0= y0 y1 y2 y3 y4 y5 y6 y7     Y1= y8 y9 y10 y11 y12 y13 y14 y15
           U = u0 u1 u2 u3 u4 u5 u6 u7     V = v0 v1 v2 v3 v4 v5 v6 v7

           Y0= y0 y1 y2 y3 y4 y5 y6 y7    Y1= y8 y9 y10 y11 y12 y13 y14 y15
           U0= u0 u0 u1 u1 u2 u2 u3 u3    U1= u4 u4 u5 u5 u6 u6 u7 u7
           V0= v0 v0 v1 v1 v2 v2 v3 v3    V1= v4 v4 v5 v5 v6 v6 v7 v7
        */

        U0 = vec_mergeh (U,U);
        V0 = vec_mergeh (V,V);

        U1 = vec_mergel (U,U);
        V1 = vec_mergel (V,V);

        cvtyuvtoRGB (c, Y0,U0,V0,&R0,&G0,&B0);
        cvtyuvtoRGB (c, Y1,U1,V1,&R1,&G1,&B1);

        R  = vec_packclp (R0,R1);
        G  = vec_packclp (G0,G1);
        B  = vec_packclp (B0,B1);

        nout = (vector unsigned char *)scratch;
        switch(target) {
        case PIX_FMT_ABGR:  out_abgr  (R,G,B,nout); break;
        case PIX_FMT_BGRA:  out_bgra  (R,G,B,nout); break;
        case PIX_FMT_RGBA:  out_rgba  (R,G,B,nout); break;
        case PIX_FMT_ARGB:  out_argb  (R,G,B,nout); break;
        case PIX_FMT_RGB24: out_rgb24 (R,G,B,nout); break;
        case PIX_FMT_BGR24: out_bgr24 (R,G,B,nout); break;
        default:
            /* Unreachable, I think. */
            av_log(c, AV_LOG_ERROR, "altivec_yuv2packedX doesn't support %s output\n",
                   av_get_pix_fmt_name(c->dstFormat));
            return;
        }

        memcpy (&((uint32_t*)dest)[i], scratch, (dstW-i)/4);
    }

}

#define YUV2PACKEDX_WRAPPER(suffix, pixfmt) \
void ff_yuv2 ## suffix ## _X_altivec(SwsContext *c, const int16_t *lumFilter, \
                            const int16_t **lumSrc, int lumFilterSize, \
                            const int16_t *chrFilter, const int16_t **chrUSrc, \
                            const int16_t **chrVSrc, int chrFilterSize, \
                            const int16_t **alpSrc, uint8_t *dest, \
                            int dstW, int dstY) \
{ \
    ff_yuv2packedX_altivec(c, lumFilter, lumSrc, lumFilterSize, \
                           chrFilter, chrUSrc, chrVSrc, chrFilterSize, \
                           alpSrc, dest, dstW, dstY, pixfmt); \
}

YUV2PACKEDX_WRAPPER(abgr,  PIX_FMT_ABGR);
YUV2PACKEDX_WRAPPER(bgra,  PIX_FMT_BGRA);
YUV2PACKEDX_WRAPPER(argb,  PIX_FMT_ARGB);
YUV2PACKEDX_WRAPPER(rgba,  PIX_FMT_RGBA);
YUV2PACKEDX_WRAPPER(rgb24, PIX_FMT_RGB24);
YUV2PACKEDX_WRAPPER(bgr24, PIX_FMT_BGR24);
