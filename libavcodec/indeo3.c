/*
 * Intel Indeo 3 (IV31, IV32, etc.) video decoder for ffmpeg
 * written, produced, and directed by Alan Smithee
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "avcodec.h"
#include "dsputil.h"
#include "bytestream.h"

#include "indeo3data.h"

typedef struct
{
    uint8_t *Ybuf;
    uint8_t *Ubuf;
    uint8_t *Vbuf;
    unsigned short y_w, y_h;
    unsigned short uv_w, uv_h;
} YUVBufs;

typedef struct Indeo3DecodeContext {
    AVCodecContext *avctx;
    int width, height;
    AVFrame frame;

    uint8_t *buf;
    YUVBufs iv_frame[2];
    YUVBufs *cur_frame;
    YUVBufs *ref_frame;

    uint8_t *ModPred;
    uint8_t *corrector_type;
} Indeo3DecodeContext;

static const uint8_t corrector_type_0[24] = {
    195, 159, 133, 115, 101,  93,  87,  77,
    195, 159, 133, 115, 101,  93,  87,  77,
    128,  79,  79,  79,  79,  79,  79,  79
};

static const uint8_t corrector_type_2[8] = { 9, 7, 6, 8, 5, 4, 3, 2 };

static av_cold int build_modpred(Indeo3DecodeContext *s)
{
    int i, j;

    if (!(s->ModPred = av_malloc(8 * 128)))
        return AVERROR(ENOMEM);

    for (i=0; i < 128; ++i) {
        s->ModPred[i+0*128] = i >  126 ? 254 : 2*(i + 1 - ((i + 1) % 2));
        s->ModPred[i+1*128] = i ==   7 ?  20 :
                              i == 119 ||
                              i == 120 ? 236 : 2*(i + 2 - ((i + 1) % 3));
        s->ModPred[i+2*128] = i >  125 ? 248 : 2*(i + 2 - ((i + 2) % 4));
        s->ModPred[i+3*128] =                  2*(i + 1 - ((i - 3) % 5));
        s->ModPred[i+4*128] = i ==   8 ?  20 : 2*(i + 1 - ((i - 3) % 6));
        s->ModPred[i+5*128] =                  2*(i + 4 - ((i + 3) % 7));
        s->ModPred[i+6*128] = i >  123 ? 240 : 2*(i + 4 - ((i + 4) % 8));
        s->ModPred[i+7*128] =                  2*(i + 5 - ((i + 4) % 9));
    }

    if (!(s->corrector_type = av_malloc(24 * 256)))
        return AVERROR(ENOMEM);

    for (i=0; i < 24; ++i) {
        for (j=0; j < 256; ++j) {
            s->corrector_type[i*256+j] = j < corrector_type_0[i]          ? 1 :
                                         j < 248 || (i == 16 && j == 248) ? 0 :
                                         corrector_type_2[j - 248];
        }
    }

  return 0;
}

static av_cold int iv_alloc_frames(Indeo3DecodeContext *s)
{
    int luma_width    = (s->width           + 3) & ~3,
        luma_height   = (s->height          + 3) & ~3,
        chroma_width  = ((luma_width  >> 2) + 3) & ~3,
        chroma_height = ((luma_height >> 2) + 3) & ~3,
        luma_pixels   = luma_width   * luma_height,
        chroma_pixels = chroma_width * chroma_height,
        i;
    unsigned int bufsize = luma_pixels * 2 + luma_width * 3 +
                          (chroma_pixels   + chroma_width) * 4;

    if(!(s->buf = av_malloc(bufsize)))
        return AVERROR(ENOMEM);
    s->iv_frame[0].y_w = s->iv_frame[1].y_w = luma_width;
    s->iv_frame[0].y_h = s->iv_frame[1].y_h = luma_height;
    s->iv_frame[0].uv_w = s->iv_frame[1].uv_w = chroma_width;
    s->iv_frame[0].uv_h = s->iv_frame[1].uv_h = chroma_height;

    s->iv_frame[0].Ybuf = s->buf + luma_width;
    i = luma_pixels + luma_width * 2;
    s->iv_frame[1].Ybuf = s->buf + i;
    i += (luma_pixels + luma_width);
    s->iv_frame[0].Ubuf = s->buf + i;
    i += (chroma_pixels + chroma_width);
    s->iv_frame[1].Ubuf = s->buf + i;
    i += (chroma_pixels + chroma_width);
    s->iv_frame[0].Vbuf = s->buf + i;
    i += (chroma_pixels + chroma_width);
    s->iv_frame[1].Vbuf = s->buf + i;

    for(i = 1; i <= luma_width; i++)
        s->iv_frame[0].Ybuf[-i] = s->iv_frame[1].Ybuf[-i] =
            s->iv_frame[0].Ubuf[-i] = 0x80;

    for(i = 1; i <= chroma_width; i++) {
        s->iv_frame[1].Ubuf[-i] = 0x80;
        s->iv_frame[0].Vbuf[-i] = 0x80;
        s->iv_frame[1].Vbuf[-i] = 0x80;
        s->iv_frame[1].Vbuf[chroma_pixels+i-1] = 0x80;
    }

    return 0;
}

static av_cold void iv_free_func(Indeo3DecodeContext *s)
{
    av_free(s->buf);
    av_free(s->ModPred);
    av_free(s->corrector_type);
}

struct ustr {
    long xpos;
    long ypos;
    long width;
    long height;
    long split_flag;
    long split_direction;
    long usl7;
};


#define LV1_CHECK(buf1,rle_v3,lv1,lp2)  \
    if((lv1 & 0x80) != 0) {             \
        if(rle_v3 != 0)                 \
            rle_v3 = 0;                 \
        else {                          \
            rle_v3 = 1;                 \
            buf1 -= 2;                  \
        }                               \
    }                                   \
    lp2 = 4;


#define RLE_V3_CHECK(buf1,rle_v1,rle_v2,rle_v3)  \
    if(rle_v3 == 0) {                            \
        rle_v2 = *buf1;                          \
        rle_v1 = 1;                              \
        if(rle_v2 > 32) {                        \
            rle_v2 -= 32;                        \
            rle_v1 = 0;                          \
        }                                        \
        rle_v3 = 1;                              \
    }                                            \
    buf1--;


#define LP2_CHECK(buf1,rle_v3,lp2)  \
    if(lp2 == 0 && rle_v3 != 0)     \
        rle_v3 = 0;                 \
    else {                          \
        buf1--;                     \
        rle_v3 = 1;                 \
    }


#define RLE_V2_CHECK(buf1,rle_v2, rle_v3,lp2) \
    rle_v2--;                                 \
    if(rle_v2 == 0) {                         \
        rle_v3 = 0;                           \
        buf1 += 2;                            \
    }                                         \
    lp2 = 4;

static void iv_Decode_Chunk(Indeo3DecodeContext *s,
        uint8_t *cur, uint8_t *ref, int width, int height,
        const uint8_t *buf1, long cb_offset, const uint8_t *hdr,
        const uint8_t *buf2, int min_width_160)
{
    uint8_t bit_buf;
    unsigned long bit_pos, lv, lv1, lv2;
    long *width_tbl, width_tbl_arr[10];
    const signed char *ref_vectors;
    uint8_t *cur_frm_pos, *ref_frm_pos, *cp, *cp2;
    uint32_t *cur_lp, *ref_lp;
    const uint32_t *correction_lp[2], *correctionloworder_lp[2], *correctionhighorder_lp[2];
    uint8_t *correction_type_sp[2];
    struct ustr strip_tbl[20], *strip;
    int i, j, k, lp1, lp2, flag1, cmd, blks_width, blks_height, region_160_width,
        rle_v1, rle_v2, rle_v3;
    unsigned short res;

    bit_buf = 0;
    ref_vectors = NULL;

    width_tbl = width_tbl_arr + 1;
    i = (width < 0 ? width + 3 : width)/4;
    for(j = -1; j < 8; j++)
        width_tbl[j] = i * j;

    strip = strip_tbl;

    for(region_160_width = 0; region_160_width < (width - min_width_160); region_160_width += min_width_160);

    strip->ypos = strip->xpos = 0;
    for(strip->width = min_width_160; width > strip->width; strip->width *= 2);
    strip->height = height;
    strip->split_direction = 0;
    strip->split_flag = 0;
    strip->usl7 = 0;

    bit_pos = 0;

    rle_v1 = rle_v2 = rle_v3 = 0;

    while(strip >= strip_tbl) {
        if(bit_pos <= 0) {
            bit_pos = 8;
            bit_buf = *buf1++;
        }

        bit_pos -= 2;
        cmd = (bit_buf >> bit_pos) & 0x03;

        if(cmd == 0) {
            strip++;
            if(strip >= strip_tbl + FF_ARRAY_ELEMS(strip_tbl)) {
                av_log(s->avctx, AV_LOG_WARNING, "out of range strip\n");
                break;
            }
            memcpy(strip, strip-1, sizeof(*strip));
            strip->split_flag = 1;
            strip->split_direction = 0;
            strip->height = (strip->height > 8 ? ((strip->height+8)>>4)<<3 : 4);
            continue;
        } else if(cmd == 1) {
            strip++;
            if(strip >= strip_tbl + FF_ARRAY_ELEMS(strip_tbl)) {
                av_log(s->avctx, AV_LOG_WARNING, "out of range strip\n");
                break;
            }
            memcpy(strip, strip-1, sizeof(*strip));
            strip->split_flag = 1;
            strip->split_direction = 1;
            strip->width = (strip->width > 8 ? ((strip->width+8)>>4)<<3 : 4);
            continue;
        } else if(cmd == 2) {
            if(strip->usl7 == 0) {
                strip->usl7 = 1;
                ref_vectors = NULL;
                continue;
            }
        } else if(cmd == 3) {
            if(strip->usl7 == 0) {
                strip->usl7 = 1;
                ref_vectors = (const signed char*)buf2 + (*buf1 * 2);
                buf1++;
                continue;
            }
        }

        cur_frm_pos = cur + width * strip->ypos + strip->xpos;

        if((blks_width = strip->width) < 0)
            blks_width += 3;
        blks_width >>= 2;
        blks_height = strip->height;

        if(ref_vectors != NULL) {
            ref_frm_pos = ref + (ref_vectors[0] + strip->ypos) * width +
                ref_vectors[1] + strip->xpos;
        } else
            ref_frm_pos = cur_frm_pos - width_tbl[4];

        if(cmd == 2) {
            if(bit_pos <= 0) {
                bit_pos = 8;
                bit_buf = *buf1++;
            }

            bit_pos -= 2;
            cmd = (bit_buf >> bit_pos) & 0x03;

            if(cmd == 0 || ref_vectors != NULL) {
                for(lp1 = 0; lp1 < blks_width; lp1++) {
                    for(i = 0, j = 0; i < blks_height; i++, j += width_tbl[1])
                        ((uint32_t *)cur_frm_pos)[j] = ((uint32_t *)ref_frm_pos)[j];
                    cur_frm_pos += 4;
                    ref_frm_pos += 4;
                }
            } else if(cmd != 1)
                return;
        } else {
            k = *buf1 >> 4;
            j = *buf1 & 0x0f;
            buf1++;
            lv = j + cb_offset;

            if((lv - 8) <= 7 && (k == 0 || k == 3 || k == 10)) {
                cp2 = s->ModPred + ((lv - 8) << 7);
                cp = ref_frm_pos;
                for(i = 0; i < blks_width << 2; i++) {
                    int v = *cp >> 1;
                    *(cp++) = cp2[v];
                }
            }

            if(k == 1 || k == 4) {
                lv = (hdr[j] & 0xf) + cb_offset;
                correction_type_sp[0] = s->corrector_type + (lv << 8);
                correction_lp[0] = correction + (lv << 8);
                lv = (hdr[j] >> 4) + cb_offset;
                correction_lp[1] = correction + (lv << 8);
                correction_type_sp[1] = s->corrector_type + (lv << 8);
            } else {
                correctionloworder_lp[0] = correctionloworder_lp[1] = correctionloworder + (lv << 8);
                correctionhighorder_lp[0] = correctionhighorder_lp[1] = correctionhighorder + (lv << 8);
                correction_type_sp[0] = correction_type_sp[1] = s->corrector_type + (lv << 8);
                correction_lp[0] = correction_lp[1] = correction + (lv << 8);
            }

            switch(k) {
            case 1:
            case 0:                    /********** CASE 0 **********/
                for( ; blks_height > 0; blks_height -= 4) {
                    for(lp1 = 0; lp1 < blks_width; lp1++) {
                        for(lp2 = 0; lp2 < 4; ) {
                            k = *buf1++;
                            cur_lp = ((uint32_t *)cur_frm_pos) + width_tbl[lp2];
                            ref_lp = ((uint32_t *)ref_frm_pos) + width_tbl[lp2];

                            switch(correction_type_sp[0][k]) {
                            case 0:
                                *cur_lp = le2me_32(((le2me_32(*ref_lp) >> 1) + correction_lp[lp2 & 0x01][k]) << 1);
                                lp2++;
                                break;
                            case 1:
                                res = ((le2me_16(((unsigned short *)(ref_lp))[0]) >> 1) + correction_lp[lp2 & 0x01][*buf1]) << 1;
                                ((unsigned short *)cur_lp)[0] = le2me_16(res);
                                res = ((le2me_16(((unsigned short *)(ref_lp))[1]) >> 1) + correction_lp[lp2 & 0x01][k]) << 1;
                                ((unsigned short *)cur_lp)[1] = le2me_16(res);
                                buf1++;
                                lp2++;
                                break;
                            case 2:
                                if(lp2 == 0) {
                                    for(i = 0, j = 0; i < 2; i++, j += width_tbl[1])
                                        cur_lp[j] = ref_lp[j];
                                    lp2 += 2;
                                }
                                break;
                            case 3:
                                if(lp2 < 2) {
                                    for(i = 0, j = 0; i < (3 - lp2); i++, j += width_tbl[1])
                                        cur_lp[j] = ref_lp[j];
                                    lp2 = 3;
                                }
                                break;
                            case 8:
                                if(lp2 == 0) {
                                    RLE_V3_CHECK(buf1,rle_v1,rle_v2,rle_v3)

                                    if(rle_v1 == 1 || ref_vectors != NULL) {
                                        for(i = 0, j = 0; i < 4; i++, j += width_tbl[1])
                                            cur_lp[j] = ref_lp[j];
                                    }

                                    RLE_V2_CHECK(buf1,rle_v2, rle_v3,lp2)
                                    break;
                                } else {
                                    rle_v1 = 1;
                                    rle_v2 = *buf1 - 1;
                                }
                            case 5:
                                LP2_CHECK(buf1,rle_v3,lp2)
                            case 4:
                                for(i = 0, j = 0; i < (4 - lp2); i++, j += width_tbl[1])
                                    cur_lp[j] = ref_lp[j];
                                lp2 = 4;
                                break;

                            case 7:
                                if(rle_v3 != 0)
                                    rle_v3 = 0;
                                else {
                                    buf1--;
                                    rle_v3 = 1;
                                }
                            case 6:
                                if(ref_vectors != NULL) {
                                    for(i = 0, j = 0; i < 4; i++, j += width_tbl[1])
                                        cur_lp[j] = ref_lp[j];
                                }
                                lp2 = 4;
                                break;

                            case 9:
                                lv1 = *buf1++;
                                lv = (lv1 & 0x7F) << 1;
                                lv += (lv << 8);
                                lv += (lv << 16);
                                for(i = 0, j = 0; i < 4; i++, j += width_tbl[1])
                                    cur_lp[j] = lv;

                                LV1_CHECK(buf1,rle_v3,lv1,lp2)
                                break;
                            default:
                                return;
                            }
                        }

                        cur_frm_pos += 4;
                        ref_frm_pos += 4;
                    }

                    cur_frm_pos += ((width - blks_width) * 4);
                    ref_frm_pos += ((width - blks_width) * 4);
                }
                break;

            case 4:
            case 3:                    /********** CASE 3 **********/
                if(ref_vectors != NULL)
                    return;
                flag1 = 1;

                for( ; blks_height > 0; blks_height -= 8) {
                    for(lp1 = 0; lp1 < blks_width; lp1++) {
                        for(lp2 = 0; lp2 < 4; ) {
                            k = *buf1++;

                            cur_lp = ((uint32_t *)cur_frm_pos) + width_tbl[lp2 * 2];
                            ref_lp = ((uint32_t *)cur_frm_pos) + width_tbl[(lp2 * 2) - 1];

                            switch(correction_type_sp[lp2 & 0x01][k]) {
                            case 0:
                                cur_lp[width_tbl[1]] = le2me_32(((le2me_32(*ref_lp) >> 1) + correction_lp[lp2 & 0x01][k]) << 1);
                                if(lp2 > 0 || flag1 == 0 || strip->ypos != 0)
                                    cur_lp[0] = ((cur_lp[-width_tbl[1]] >> 1) + (cur_lp[width_tbl[1]] >> 1)) & 0xFEFEFEFE;
                                else
                                    cur_lp[0] = le2me_32(((le2me_32(*ref_lp) >> 1) + correction_lp[lp2 & 0x01][k]) << 1);
                                lp2++;
                                break;

                            case 1:
                                res = ((le2me_16(((unsigned short *)ref_lp)[0]) >> 1) + correction_lp[lp2 & 0x01][*buf1]) << 1;
                                ((unsigned short *)cur_lp)[width_tbl[2]] = le2me_16(res);
                                res = ((le2me_16(((unsigned short *)ref_lp)[1]) >> 1) + correction_lp[lp2 & 0x01][k]) << 1;
                                ((unsigned short *)cur_lp)[width_tbl[2]+1] = le2me_16(res);

                                if(lp2 > 0 || flag1 == 0 || strip->ypos != 0)
                                    cur_lp[0] = ((cur_lp[-width_tbl[1]] >> 1) + (cur_lp[width_tbl[1]] >> 1)) & 0xFEFEFEFE;
                                else
                                    cur_lp[0] = cur_lp[width_tbl[1]];
                                buf1++;
                                lp2++;
                                break;

                            case 2:
                                if(lp2 == 0) {
                                    for(i = 0, j = 0; i < 4; i++, j += width_tbl[1])
                                        cur_lp[j] = *ref_lp;
                                    lp2 += 2;
                                }
                                break;

                            case 3:
                                if(lp2 < 2) {
                                    for(i = 0, j = 0; i < 6 - (lp2 * 2); i++, j += width_tbl[1])
                                        cur_lp[j] = *ref_lp;
                                    lp2 = 3;
                                }
                                break;

                            case 6:
                                lp2 = 4;
                                break;

                            case 7:
                                if(rle_v3 != 0)
                                    rle_v3 = 0;
                                else {
                                    buf1--;
                                    rle_v3 = 1;
                                }
                                lp2 = 4;
                                break;

                            case 8:
                                if(lp2 == 0) {
                                    RLE_V3_CHECK(buf1,rle_v1,rle_v2,rle_v3)

                                    if(rle_v1 == 1) {
                                        for(i = 0, j = 0; i < 8; i++, j += width_tbl[1])
                                            cur_lp[j] = ref_lp[j];
                                    }

                                    RLE_V2_CHECK(buf1,rle_v2, rle_v3,lp2)
                                    break;
                                } else {
                                    rle_v2 = (*buf1) - 1;
                                    rle_v1 = 1;
                                }
                            case 5:
                                LP2_CHECK(buf1,rle_v3,lp2)
                            case 4:
                                for(i = 0, j = 0; i < 8 - (lp2 * 2); i++, j += width_tbl[1])
                                    cur_lp[j] = *ref_lp;
                                lp2 = 4;
                                break;

                            case 9:
                                av_log(s->avctx, AV_LOG_ERROR, "UNTESTED.\n");
                                lv1 = *buf1++;
                                lv = (lv1 & 0x7F) << 1;
                                lv += (lv << 8);
                                lv += (lv << 16);

                                for(i = 0, j = 0; i < 4; i++, j += width_tbl[1])
                                    cur_lp[j] = lv;

                                LV1_CHECK(buf1,rle_v3,lv1,lp2)
                                break;

                            default:
                                return;
                            }
                        }

                        cur_frm_pos += 4;
                    }

                    cur_frm_pos += (((width * 2) - blks_width) * 4);
                    flag1 = 0;
                }
                break;

            case 10:                    /********** CASE 10 **********/
                if(ref_vectors == NULL) {
                    flag1 = 1;

                    for( ; blks_height > 0; blks_height -= 8) {
                        for(lp1 = 0; lp1 < blks_width; lp1 += 2) {
                            for(lp2 = 0; lp2 < 4; ) {
                                k = *buf1++;
                                cur_lp = ((uint32_t *)cur_frm_pos) + width_tbl[lp2 * 2];
                                ref_lp = ((uint32_t *)cur_frm_pos) + width_tbl[(lp2 * 2) - 1];
                                lv1 = ref_lp[0];
                                lv2 = ref_lp[1];
                                if(lp2 == 0 && flag1 != 0) {
#ifdef WORDS_BIGENDIAN
                                    lv1 = lv1 & 0xFF00FF00;
                                    lv1 = (lv1 >> 8) | lv1;
                                    lv2 = lv2 & 0xFF00FF00;
                                    lv2 = (lv2 >> 8) | lv2;
#else
                                    lv1 = lv1 & 0x00FF00FF;
                                    lv1 = (lv1 << 8) | lv1;
                                    lv2 = lv2 & 0x00FF00FF;
                                    lv2 = (lv2 << 8) | lv2;
#endif
                                }

                                switch(correction_type_sp[lp2 & 0x01][k]) {
                                case 0:
                                    cur_lp[width_tbl[1]] = le2me_32(((le2me_32(lv1) >> 1) + correctionloworder_lp[lp2 & 0x01][k]) << 1);
                                    cur_lp[width_tbl[1]+1] = le2me_32(((le2me_32(lv2) >> 1) + correctionhighorder_lp[lp2 & 0x01][k]) << 1);
                                    if(lp2 > 0 || strip->ypos != 0 || flag1 == 0) {
                                        cur_lp[0] = ((cur_lp[-width_tbl[1]] >> 1) + (cur_lp[width_tbl[1]] >> 1)) & 0xFEFEFEFE;
                                        cur_lp[1] = ((cur_lp[-width_tbl[1]+1] >> 1) + (cur_lp[width_tbl[1]+1] >> 1)) & 0xFEFEFEFE;
                                    } else {
                                        cur_lp[0] = cur_lp[width_tbl[1]];
                                        cur_lp[1] = cur_lp[width_tbl[1]+1];
                                    }
                                    lp2++;
                                    break;

                                case 1:
                                    cur_lp[width_tbl[1]] = le2me_32(((le2me_32(lv1) >> 1) + correctionloworder_lp[lp2 & 0x01][*buf1]) << 1);
                                    cur_lp[width_tbl[1]+1] = le2me_32(((le2me_32(lv2) >> 1) + correctionloworder_lp[lp2 & 0x01][k]) << 1);
                                    if(lp2 > 0 || strip->ypos != 0 || flag1 == 0) {
                                        cur_lp[0] = ((cur_lp[-width_tbl[1]] >> 1) + (cur_lp[width_tbl[1]] >> 1)) & 0xFEFEFEFE;
                                        cur_lp[1] = ((cur_lp[-width_tbl[1]+1] >> 1) + (cur_lp[width_tbl[1]+1] >> 1)) & 0xFEFEFEFE;
                                    } else {
                                        cur_lp[0] = cur_lp[width_tbl[1]];
                                        cur_lp[1] = cur_lp[width_tbl[1]+1];
                                    }
                                    buf1++;
                                    lp2++;
                                    break;

                                case 2:
                                    if(lp2 == 0) {
                                        if(flag1 != 0) {
                                            for(i = 0, j = width_tbl[1]; i < 3; i++, j += width_tbl[1]) {
                                                cur_lp[j] = lv1;
                                                cur_lp[j+1] = lv2;
                                            }
                                            cur_lp[0] = ((cur_lp[-width_tbl[1]] >> 1) + (cur_lp[width_tbl[1]] >> 1)) & 0xFEFEFEFE;
                                            cur_lp[1] = ((cur_lp[-width_tbl[1]+1] >> 1) + (cur_lp[width_tbl[1]+1] >> 1)) & 0xFEFEFEFE;
                                        } else {
                                            for(i = 0, j = 0; i < 4; i++, j += width_tbl[1]) {
                                                cur_lp[j] = lv1;
                                                cur_lp[j+1] = lv2;
                                            }
                                        }
                                        lp2 += 2;
                                    }
                                    break;

                                case 3:
                                    if(lp2 < 2) {
                                        if(lp2 == 0 && flag1 != 0) {
                                            for(i = 0, j = width_tbl[1]; i < 5; i++, j += width_tbl[1]) {
                                                cur_lp[j] = lv1;
                                                cur_lp[j+1] = lv2;
                                            }
                                            cur_lp[0] = ((cur_lp[-width_tbl[1]] >> 1) + (cur_lp[width_tbl[1]] >> 1)) & 0xFEFEFEFE;
                                            cur_lp[1] = ((cur_lp[-width_tbl[1]+1] >> 1) + (cur_lp[width_tbl[1]+1] >> 1)) & 0xFEFEFEFE;
                                        } else {
                                            for(i = 0, j = 0; i < 6 - (lp2 * 2); i++, j += width_tbl[1]) {
                                                cur_lp[j] = lv1;
                                                cur_lp[j+1] = lv2;
                                            }
                                        }
                                        lp2 = 3;
                                    }
                                    break;

                                case 8:
                                    if(lp2 == 0) {
                                        RLE_V3_CHECK(buf1,rle_v1,rle_v2,rle_v3)
                                        if(rle_v1 == 1) {
                                            if(flag1 != 0) {
                                                for(i = 0, j = width_tbl[1]; i < 7; i++, j += width_tbl[1]) {
                                                    cur_lp[j] = lv1;
                                                    cur_lp[j+1] = lv2;
                                                }
                                                cur_lp[0] = ((cur_lp[-width_tbl[1]] >> 1) + (cur_lp[width_tbl[1]] >> 1)) & 0xFEFEFEFE;
                                                cur_lp[1] = ((cur_lp[-width_tbl[1]+1] >> 1) + (cur_lp[width_tbl[1]+1] >> 1)) & 0xFEFEFEFE;
                                            } else {
                                                for(i = 0, j = 0; i < 8; i++, j += width_tbl[1]) {
                                                    cur_lp[j] = lv1;
                                                    cur_lp[j+1] = lv2;
                                                }
                                            }
                                        }
                                        RLE_V2_CHECK(buf1,rle_v2, rle_v3,lp2)
                                        break;
                                    } else {
                                        rle_v1 = 1;
                                        rle_v2 = (*buf1) - 1;
                                    }
                                case 5:
                                    LP2_CHECK(buf1,rle_v3,lp2)
                                case 4:
                                    if(lp2 == 0 && flag1 != 0) {
                                        for(i = 0, j = width_tbl[1]; i < 7; i++, j += width_tbl[1]) {
                                            cur_lp[j] = lv1;
                                            cur_lp[j+1] = lv2;
                                        }
                                        cur_lp[0] = ((cur_lp[-width_tbl[1]] >> 1) + (cur_lp[width_tbl[1]] >> 1)) & 0xFEFEFEFE;
                                        cur_lp[1] = ((cur_lp[-width_tbl[1]+1] >> 1) + (cur_lp[width_tbl[1]+1] >> 1)) & 0xFEFEFEFE;
                                    } else {
                                        for(i = 0, j = 0; i < 8 - (lp2 * 2); i++, j += width_tbl[1]) {
                                            cur_lp[j] = lv1;
                                            cur_lp[j+1] = lv2;
                                        }
                                    }
                                    lp2 = 4;
                                    break;

                                case 6:
                                    lp2 = 4;
                                    break;

                                case 7:
                                    if(lp2 == 0) {
                                        if(rle_v3 != 0)
                                            rle_v3 = 0;
                                        else {
                                            buf1--;
                                            rle_v3 = 1;
                                        }
                                        lp2 = 4;
                                    }
                                    break;

                                case 9:
                                    av_log(s->avctx, AV_LOG_ERROR, "UNTESTED.\n");
                                    lv1 = *buf1;
                                    lv = (lv1 & 0x7F) << 1;
                                    lv += (lv << 8);
                                    lv += (lv << 16);
                                    for(i = 0, j = 0; i < 8; i++, j += width_tbl[1])
                                        cur_lp[j] = lv;
                                    LV1_CHECK(buf1,rle_v3,lv1,lp2)
                                    break;

                                default:
                                    return;
                                }
                            }

                            cur_frm_pos += 8;
                        }

                        cur_frm_pos += (((width * 2) - blks_width) * 4);
                        flag1 = 0;
                    }
                } else {
                    for( ; blks_height > 0; blks_height -= 8) {
                        for(lp1 = 0; lp1 < blks_width; lp1 += 2) {
                            for(lp2 = 0; lp2 < 4; ) {
                                k = *buf1++;
                                cur_lp = ((uint32_t *)cur_frm_pos) + width_tbl[lp2 * 2];
                                ref_lp = ((uint32_t *)ref_frm_pos) + width_tbl[lp2 * 2];

                                switch(correction_type_sp[lp2 & 0x01][k]) {
                                case 0:
                                    lv1 = correctionloworder_lp[lp2 & 0x01][k];
                                    lv2 = correctionhighorder_lp[lp2 & 0x01][k];
                                    cur_lp[0] = le2me_32(((le2me_32(ref_lp[0]) >> 1) + lv1) << 1);
                                    cur_lp[1] = le2me_32(((le2me_32(ref_lp[1]) >> 1) + lv2) << 1);
                                    cur_lp[width_tbl[1]] = le2me_32(((le2me_32(ref_lp[width_tbl[1]]) >> 1) + lv1) << 1);
                                    cur_lp[width_tbl[1]+1] = le2me_32(((le2me_32(ref_lp[width_tbl[1]+1]) >> 1) + lv2) << 1);
                                    lp2++;
                                    break;

                                case 1:
                                    lv1 = correctionloworder_lp[lp2 & 0x01][*buf1++];
                                    lv2 = correctionloworder_lp[lp2 & 0x01][k];
                                    cur_lp[0] = le2me_32(((le2me_32(ref_lp[0]) >> 1) + lv1) << 1);
                                    cur_lp[1] = le2me_32(((le2me_32(ref_lp[1]) >> 1) + lv2) << 1);
                                    cur_lp[width_tbl[1]] = le2me_32(((le2me_32(ref_lp[width_tbl[1]]) >> 1) + lv1) << 1);
                                    cur_lp[width_tbl[1]+1] = le2me_32(((le2me_32(ref_lp[width_tbl[1]+1]) >> 1) + lv2) << 1);
                                    lp2++;
                                    break;

                                case 2:
                                    if(lp2 == 0) {
                                        for(i = 0, j = 0; i < 4; i++, j += width_tbl[1]) {
                                            cur_lp[j] = ref_lp[j];
                                            cur_lp[j+1] = ref_lp[j+1];
                                        }
                                        lp2 += 2;
                                    }
                                    break;

                                case 3:
                                    if(lp2 < 2) {
                                        for(i = 0, j = 0; i < 6 - (lp2 * 2); i++, j += width_tbl[1]) {
                                            cur_lp[j] = ref_lp[j];
                                            cur_lp[j+1] = ref_lp[j+1];
                                        }
                                        lp2 = 3;
                                    }
                                    break;

                                case 8:
                                    if(lp2 == 0) {
                                        RLE_V3_CHECK(buf1,rle_v1,rle_v2,rle_v3)
                                        for(i = 0, j = 0; i < 8; i++, j += width_tbl[1]) {
                                            ((uint32_t *)cur_frm_pos)[j] = ((uint32_t *)ref_frm_pos)[j];
                                            ((uint32_t *)cur_frm_pos)[j+1] = ((uint32_t *)ref_frm_pos)[j+1];
                                        }
                                        RLE_V2_CHECK(buf1,rle_v2, rle_v3,lp2)
                                        break;
                                    } else {
                                        rle_v1 = 1;
                                        rle_v2 = (*buf1) - 1;
                                    }
                                case 5:
                                case 7:
                                    LP2_CHECK(buf1,rle_v3,lp2)
                                case 6:
                                case 4:
                                    for(i = 0, j = 0; i < 8 - (lp2 * 2); i++, j += width_tbl[1]) {
                                        cur_lp[j] = ref_lp[j];
                                        cur_lp[j+1] = ref_lp[j+1];
                                    }
                                    lp2 = 4;
                                    break;

                                case 9:
                                    av_log(s->avctx, AV_LOG_ERROR, "UNTESTED.\n");
                                    lv1 = *buf1;
                                    lv = (lv1 & 0x7F) << 1;
                                    lv += (lv << 8);
                                    lv += (lv << 16);
                                    for(i = 0, j = 0; i < 8; i++, j += width_tbl[1])
                                        ((uint32_t *)cur_frm_pos)[j] = ((uint32_t *)cur_frm_pos)[j+1] = lv;
                                    LV1_CHECK(buf1,rle_v3,lv1,lp2)
                                    break;

                                default:
                                    return;
                                }
                            }

                            cur_frm_pos += 8;
                            ref_frm_pos += 8;
                        }

                        cur_frm_pos += (((width * 2) - blks_width) * 4);
                        ref_frm_pos += (((width * 2) - blks_width) * 4);
                    }
                }
                break;

            case 11:                    /********** CASE 11 **********/
                if(ref_vectors == NULL)
                    return;

                for( ; blks_height > 0; blks_height -= 8) {
                    for(lp1 = 0; lp1 < blks_width; lp1++) {
                        for(lp2 = 0; lp2 < 4; ) {
                            k = *buf1++;
                            cur_lp = ((uint32_t *)cur_frm_pos) + width_tbl[lp2 * 2];
                            ref_lp = ((uint32_t *)ref_frm_pos) + width_tbl[lp2 * 2];

                            switch(correction_type_sp[lp2 & 0x01][k]) {
                            case 0:
                                cur_lp[0] = le2me_32(((le2me_32(*ref_lp) >> 1) + correction_lp[lp2 & 0x01][k]) << 1);
                                cur_lp[width_tbl[1]] = le2me_32(((le2me_32(ref_lp[width_tbl[1]]) >> 1) + correction_lp[lp2 & 0x01][k]) << 1);
                                lp2++;
                                break;

                            case 1:
                                lv1 = (unsigned short)(correction_lp[lp2 & 0x01][*buf1++]);
                                lv2 = (unsigned short)(correction_lp[lp2 & 0x01][k]);
                                res = (unsigned short)(((le2me_16(((unsigned short *)ref_lp)[0]) >> 1) + lv1) << 1);
                                ((unsigned short *)cur_lp)[0] = le2me_16(res);
                                res = (unsigned short)(((le2me_16(((unsigned short *)ref_lp)[1]) >> 1) + lv2) << 1);
                                ((unsigned short *)cur_lp)[1] = le2me_16(res);
                                res = (unsigned short)(((le2me_16(((unsigned short *)ref_lp)[width_tbl[2]]) >> 1) + lv1) << 1);
                                ((unsigned short *)cur_lp)[width_tbl[2]] = le2me_16(res);
                                res = (unsigned short)(((le2me_16(((unsigned short *)ref_lp)[width_tbl[2]+1]) >> 1) + lv2) << 1);
                                ((unsigned short *)cur_lp)[width_tbl[2]+1] = le2me_16(res);
                                lp2++;
                                break;

                            case 2:
                                if(lp2 == 0) {
                                    for(i = 0, j = 0; i < 4; i++, j += width_tbl[1])
                                        cur_lp[j] = ref_lp[j];
                                    lp2 += 2;
                                }
                                break;

                            case 3:
                                if(lp2 < 2) {
                                    for(i = 0, j = 0; i < 6 - (lp2 * 2); i++, j += width_tbl[1])
                                        cur_lp[j] = ref_lp[j];
                                    lp2 = 3;
                                }
                                break;

                            case 8:
                                if(lp2 == 0) {
                                    RLE_V3_CHECK(buf1,rle_v1,rle_v2,rle_v3)

                                    for(i = 0, j = 0; i < 8; i++, j += width_tbl[1])
                                        cur_lp[j] = ref_lp[j];

                                    RLE_V2_CHECK(buf1,rle_v2, rle_v3,lp2)
                                    break;
                                } else {
                                    rle_v1 = 1;
                                    rle_v2 = (*buf1) - 1;
                                }
                            case 5:
                            case 7:
                                LP2_CHECK(buf1,rle_v3,lp2)
                            case 4:
                            case 6:
                                for(i = 0, j = 0; i < 8 - (lp2 * 2); i++, j += width_tbl[1])
                                    cur_lp[j] = ref_lp[j];
                                lp2 = 4;
                                break;

                            case 9:
                                av_log(s->avctx, AV_LOG_ERROR, "UNTESTED.\n");
                                lv1 = *buf1++;
                                lv = (lv1 & 0x7F) << 1;
                                lv += (lv << 8);
                                lv += (lv << 16);
                                for(i = 0, j = 0; i < 4; i++, j += width_tbl[1])
                                    cur_lp[j] = lv;
                                LV1_CHECK(buf1,rle_v3,lv1,lp2)
                                break;

                            default:
                                return;
                            }
                        }

                        cur_frm_pos += 4;
                        ref_frm_pos += 4;
                    }

                    cur_frm_pos += (((width * 2) - blks_width) * 4);
                    ref_frm_pos += (((width * 2) - blks_width) * 4);
                }
                break;

            default:
                return;
            }
        }

        for( ; strip >= strip_tbl; strip--) {
            if(strip->split_flag != 0) {
                strip->split_flag = 0;
                strip->usl7 = (strip-1)->usl7;

                if(strip->split_direction) {
                    strip->xpos += strip->width;
                    strip->width = (strip-1)->width - strip->width;
                    if(region_160_width <= strip->xpos && width < strip->width + strip->xpos)
                        strip->width = width - strip->xpos;
                } else {
                    strip->ypos += strip->height;
                    strip->height = (strip-1)->height - strip->height;
                }
                break;
            }
        }
    }
}

static av_cold int indeo3_decode_init(AVCodecContext *avctx)
{
    Indeo3DecodeContext *s = avctx->priv_data;
    int ret = 0;

    s->avctx = avctx;
    s->width = avctx->width;
    s->height = avctx->height;
    avctx->pix_fmt = PIX_FMT_YUV410P;

    if (!(ret = build_modpred(s)))
        ret = iv_alloc_frames(s);
    if (ret)
        iv_free_func(s);

    return ret;
}

static unsigned long iv_decode_frame(Indeo3DecodeContext *s,
                                     const uint8_t *buf, int buf_size)
{
    unsigned int image_width, image_height,
                 chroma_width, chroma_height;
    unsigned long flags, cb_offset, data_size,
                  y_offset, v_offset, u_offset, mc_vector_count;
    const uint8_t *hdr_pos, *buf_pos;

    buf_pos = buf;
    buf_pos += 18; /* skip OS header (16 bytes) and version number */

    flags = bytestream_get_le16(&buf_pos);
    data_size = bytestream_get_le32(&buf_pos);
    cb_offset = *buf_pos++;
    buf_pos += 3; /* skip reserved byte and checksum */
    image_height = bytestream_get_le16(&buf_pos);
    image_width  = bytestream_get_le16(&buf_pos);

    if(avcodec_check_dimensions(NULL, image_width, image_height))
        return -1;

    chroma_height = ((image_height >> 2) + 3) & 0x7ffc;
    chroma_width = ((image_width >> 2) + 3) & 0x7ffc;
    y_offset = bytestream_get_le32(&buf_pos);
    v_offset = bytestream_get_le32(&buf_pos);
    u_offset = bytestream_get_le32(&buf_pos);
    buf_pos += 4; /* reserved */
    hdr_pos = buf_pos;
    if(data_size == 0x80) return 4;

    if(flags & 0x200) {
        s->cur_frame = s->iv_frame + 1;
        s->ref_frame = s->iv_frame;
    } else {
        s->cur_frame = s->iv_frame;
        s->ref_frame = s->iv_frame + 1;
    }

    buf_pos = buf + 16 + y_offset;
    mc_vector_count = bytestream_get_le32(&buf_pos);

    iv_Decode_Chunk(s, s->cur_frame->Ybuf, s->ref_frame->Ybuf, image_width,
                    image_height, buf_pos + mc_vector_count * 2, cb_offset, hdr_pos, buf_pos,
                    FFMIN(image_width, 160));

    if (!(s->avctx->flags & CODEC_FLAG_GRAY))
    {

        buf_pos = buf + 16 + v_offset;
        mc_vector_count = bytestream_get_le32(&buf_pos);

        iv_Decode_Chunk(s, s->cur_frame->Vbuf, s->ref_frame->Vbuf, chroma_width,
                chroma_height, buf_pos + mc_vector_count * 2, cb_offset, hdr_pos, buf_pos,
                FFMIN(chroma_width, 40));

        buf_pos = buf + 16 + u_offset;
        mc_vector_count = bytestream_get_le32(&buf_pos);

        iv_Decode_Chunk(s, s->cur_frame->Ubuf, s->ref_frame->Ubuf, chroma_width,
                chroma_height, buf_pos + mc_vector_count * 2, cb_offset, hdr_pos, buf_pos,
                FFMIN(chroma_width, 40));

    }

    return 8;
}

static int indeo3_decode_frame(AVCodecContext *avctx,
                               void *data, int *data_size,
                               const uint8_t *buf, int buf_size)
{
    Indeo3DecodeContext *s=avctx->priv_data;
    uint8_t *src, *dest;
    int y;

    iv_decode_frame(s, buf, buf_size);

    if(s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    s->frame.reference = 0;
    if(avctx->get_buffer(avctx, &s->frame) < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    src = s->cur_frame->Ybuf;
    dest = s->frame.data[0];
    for (y = 0; y < s->height; y++) {
        memcpy(dest, src, s->cur_frame->y_w);
        src += s->cur_frame->y_w;
        dest += s->frame.linesize[0];
    }

    if (!(s->avctx->flags & CODEC_FLAG_GRAY))
    {
        src = s->cur_frame->Ubuf;
        dest = s->frame.data[1];
        for (y = 0; y < s->height / 4; y++) {
            memcpy(dest, src, s->cur_frame->uv_w);
            src += s->cur_frame->uv_w;
            dest += s->frame.linesize[1];
        }

        src = s->cur_frame->Vbuf;
        dest = s->frame.data[2];
        for (y = 0; y < s->height / 4; y++) {
            memcpy(dest, src, s->cur_frame->uv_w);
            src += s->cur_frame->uv_w;
            dest += s->frame.linesize[2];
        }
    }

    *data_size=sizeof(AVFrame);
    *(AVFrame*)data= s->frame;

    return buf_size;
}

static av_cold int indeo3_decode_end(AVCodecContext *avctx)
{
    Indeo3DecodeContext *s = avctx->priv_data;

    iv_free_func(s);

    return 0;
}

AVCodec indeo3_decoder = {
    "indeo3",
    CODEC_TYPE_VIDEO,
    CODEC_ID_INDEO3,
    sizeof(Indeo3DecodeContext),
    indeo3_decode_init,
    NULL,
    indeo3_decode_end,
    indeo3_decode_frame,
    0,
    NULL,
    .long_name = NULL_IF_CONFIG_SMALL("Intel Indeo 3"),
};
