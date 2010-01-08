/*
 * H263 internal header
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
#ifndef AVCODEC_H263_H
#define AVCODEC_H263_H

// The defines below define the number of bits that are read at once for
// reading vlc values. Changing these may improve speed and data cache needs
// be aware though that decreasing them may need the number of stages that is
// passed to get_vlc* to be increased.
#define INTRA_MCBPC_VLC_BITS 6
#define INTER_MCBPC_VLC_BITS 7
#define CBPY_VLC_BITS 6
#define TEX_VLC_BITS 9

extern const AVRational ff_h263_pixel_aspect[16];
extern const uint8_t ff_h263_cbpy_tab[16][2];

extern const uint8_t mvtab[33][2];

extern const uint8_t ff_h263_intra_MCBPC_code[9];
extern const uint8_t ff_h263_intra_MCBPC_bits[9];

extern const uint8_t ff_h263_inter_MCBPC_code[28];
extern const uint8_t ff_h263_inter_MCBPC_bits[28];

extern VLC ff_h263_intra_MCBPC_vlc;
extern VLC ff_h263_inter_MCBPC_vlc;
extern VLC ff_h263_cbpy_vlc;

extern RLTable ff_h263_rl_inter;

int h263_decode_motion(MpegEncContext * s, int pred, int f_code);
av_const int ff_h263_aspect_to_info(AVRational aspect);

static inline int h263_get_motion_length(MpegEncContext * s, int val, int f_code){
    int l, bit_size, code;

    if (val == 0) {
        return mvtab[0][1];
    } else {
        bit_size = f_code - 1;
        /* modulo encoding */
        l= INT_BIT - 6 - bit_size;
        val = (val<<l)>>l;
        val--;
        code = (val >> bit_size) + 1;

        return mvtab[code][1] + 1 + bit_size;
    }
}

static inline void ff_h263_encode_motion_vector(MpegEncContext * s, int x, int y, int f_code){
    if(s->flags2 & CODEC_FLAG2_NO_OUTPUT){
        skip_put_bits(&s->pb,
            h263_get_motion_length(s, x, f_code)
           +h263_get_motion_length(s, y, f_code));
    }else{
        ff_h263_encode_motion(s, x, f_code);
        ff_h263_encode_motion(s, y, f_code);
    }
}

static inline int get_p_cbp(MpegEncContext * s,
                      DCTELEM block[6][64],
                      int motion_x, int motion_y){
    int cbp, i;

    if(s->flags & CODEC_FLAG_CBP_RD){
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
                s->dsp.clear_block(s->block[i]);
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

static inline int get_b_cbp(MpegEncContext * s, DCTELEM block[6][64],
                            int motion_x, int motion_y, int mb_type){
    int cbp=0, i;

    if(s->flags & CODEC_FLAG_CBP_RD){
        int score=0;
        const int lambda= s->lambda2 >> (FF_LAMBDA_SHIFT - 6);

        for(i=0; i<6; i++){
            if(s->coded_score[i] < 0){
                score += s->coded_score[i];
                cbp |= 1 << (5 - i);
            }
        }

        if(cbp){
            int zero_score= -6;
            if ((motion_x | motion_y | s->dquant | mb_type) == 0){
                zero_score-= 4; //2*MV + mb_type + cbp bit
            }

            zero_score*= lambda;
            if(zero_score <= score){
                cbp=0;
            }
        }

        for (i = 0; i < 6; i++) {
            if (s->block_last_index[i] >= 0 && ((cbp >> (5 - i))&1)==0 ){
                s->block_last_index[i]= -1;
                s->dsp.clear_block(s->block[i]);
            }
        }
    }else{
        for (i = 0; i < 6; i++) {
            if (s->block_last_index[i] >= 0)
                cbp |= 1 << (5 - i);
        }
    }
    return cbp;
}
#endif
