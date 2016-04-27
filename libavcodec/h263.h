/*
 * H.263 internal header
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#ifndef AVCODEC_H263_H
#define AVCODEC_H263_H

#include <stdint.h>
#include "libavutil/rational.h"
#include "get_bits.h"
#include "mpegvideo.h"
#include "h263data.h"
#include "rl.h"

#if !FF_API_ASPECT_EXTENDED
#define FF_ASPECT_EXTENDED 15
#endif
#define INT_BIT (CHAR_BIT * sizeof(int))

// The defines below define the number of bits that are read at once for
// reading vlc values. Changing these may improve speed and data cache needs
// be aware though that decreasing them may need the number of stages that is
// passed to get_vlc* to be increased.
#define INTRA_MCBPC_VLC_BITS 6
#define INTER_MCBPC_VLC_BITS 7
#define CBPY_VLC_BITS 6
#define TEX_VLC_BITS 9

#define H263_GOB_HEIGHT(h) ((h) <= 400 ? 1 : (h) <= 800 ? 2 : 4)

extern VLC ff_h263_intra_MCBPC_vlc;
extern VLC ff_h263_inter_MCBPC_vlc;
extern VLC ff_h263_cbpy_vlc;

extern const enum AVPixelFormat ff_h263_hwaccel_pixfmt_list_420[];


int ff_h263_decode_motion(MpegEncContext * s, int pred, int f_code);
av_const int ff_h263_aspect_to_info(AVRational aspect);
int ff_h263_decode_init(AVCodecContext *avctx);
int ff_h263_decode_frame(AVCodecContext *avctx,
                             void *data, int *got_frame,
                             AVPacket *avpkt);
int ff_h263_decode_end(AVCodecContext *avctx);
void ff_h263_encode_mb(MpegEncContext *s,
                       int16_t block[6][64],
                       int motion_x, int motion_y);
void ff_h263_encode_picture_header(MpegEncContext *s, int picture_number);
void ff_h263_encode_gob_header(MpegEncContext * s, int mb_line);
int16_t *ff_h263_pred_motion(MpegEncContext * s, int block, int dir,
                             int *px, int *py);
void ff_h263_encode_init(MpegEncContext *s);
void ff_h263_decode_init_vlc(void);
int ff_h263_decode_picture_header(MpegEncContext *s);
int ff_h263_decode_gob_header(MpegEncContext *s);
void ff_h263_update_motion_val(MpegEncContext * s);
void ff_h263_loop_filter(MpegEncContext * s);
int ff_h263_decode_mba(MpegEncContext *s);
void ff_h263_encode_mba(MpegEncContext *s);
void ff_init_qscale_tab(MpegEncContext *s);
int ff_h263_pred_dc(MpegEncContext * s, int n, int16_t **dc_val_ptr);
void ff_h263_pred_acdc(MpegEncContext * s, int16_t *block, int n);


/**
 * Print picture info if FF_DEBUG_PICT_INFO is set.
 */
void ff_h263_show_pict_info(MpegEncContext *s);

int ff_intel_h263_decode_picture_header(MpegEncContext *s);
int ff_h263_decode_mb(MpegEncContext *s,
                      int16_t block[6][64]);

/**
 * Return the value of the 3-bit "source format" syntax element.
 * This represents some standard picture dimensions or indicates that
 * width&height are explicitly stored later.
 */
int av_const h263_get_picture_format(int width, int height);

void ff_clean_h263_qscales(MpegEncContext *s);
int ff_h263_resync(MpegEncContext *s);
const uint8_t *ff_h263_find_resync_marker(const uint8_t *p, const uint8_t *end);
void ff_h263_encode_motion(MpegEncContext * s, int val, int f_code);


static inline int h263_get_motion_length(MpegEncContext * s, int val, int f_code){
    int l, bit_size, code;

    if (val == 0) {
        return ff_mvtab[0][1];
    } else {
        bit_size = f_code - 1;
        /* modulo encoding */
        l= INT_BIT - 6 - bit_size;
        val = (val<<l)>>l;
        val--;
        code = (val >> bit_size) + 1;

        return ff_mvtab[code][1] + 1 + bit_size;
    }
}

static inline void ff_h263_encode_motion_vector(MpegEncContext * s, int x, int y, int f_code){
    if (s->avctx->flags2 & AV_CODEC_FLAG2_NO_OUTPUT) {
        skip_put_bits(&s->pb,
            h263_get_motion_length(s, x, f_code)
           +h263_get_motion_length(s, y, f_code));
    }else{
        ff_h263_encode_motion(s, x, f_code);
        ff_h263_encode_motion(s, y, f_code);
    }
}

static inline int get_p_cbp(MpegEncContext * s,
                      int16_t block[6][64],
                      int motion_x, int motion_y){
    int cbp, i;

    if (s->mpv_flags & FF_MPV_FLAG_CBP_RD) {
        int best_cbpy_score= INT_MAX;
        int best_cbpc_score= INT_MAX;
        int cbpc = (-1), cbpy= (-1);
        const int offset= (s->mv_type==MV_TYPE_16X16 ? 0 : 16) + (s->dquant ? 8 : 0);
        const int lambda= s->lambda2 >> (FF_LAMBDA_SHIFT - 6);

        for(i=0; i<4; i++){
            int score= ff_h263_inter_MCBPC_bits[i + offset] * lambda;
            if(i&1) score += s->coded_score[5];
            if(i&2) score += s->coded_score[4];

            if(score < best_cbpc_score){
                best_cbpc_score= score;
                cbpc= i;
            }
        }

        for(i=0; i<16; i++){
            int score= ff_h263_cbpy_tab[i ^ 0xF][1] * lambda;
            if(i&1) score += s->coded_score[3];
            if(i&2) score += s->coded_score[2];
            if(i&4) score += s->coded_score[1];
            if(i&8) score += s->coded_score[0];

            if(score < best_cbpy_score){
                best_cbpy_score= score;
                cbpy= i;
            }
        }
        cbp= cbpc + 4*cbpy;
        if ((motion_x | motion_y | s->dquant) == 0 && s->mv_type==MV_TYPE_16X16){
            if(best_cbpy_score + best_cbpc_score + 2*lambda >= 0)
                cbp= 0;
        }

        for (i = 0; i < 6; i++) {
            if (s->block_last_index[i] >= 0 && ((cbp >> (5 - i))&1)==0 ){
                s->block_last_index[i]= -1;
                s->bdsp.clear_block(s->block[i]);
            }
        }
    }else{
        cbp= 0;
        for (i = 0; i < 6; i++) {
            if (s->block_last_index[i] >= 0)
                cbp |= 1 << (5 - i);
        }
    }
    return cbp;
}

#endif /* AVCODEC_H263_H */
