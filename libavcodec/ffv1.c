/*
 * FFV1 codec for libavcodec
 *
 * Copyright (c) 2003-2012 Michael Niedermayer <michaelni@gmx.at>
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
 * FF Video Codec 1 (a lossless codec)
 */

#include "avcodec.h"
#include "internal.h"
#include "get_bits.h"
#include "put_bits.h"
#include "dsputil.h"
#include "rangecoder.h"
#include "golomb.h"
#include "mathops.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avassert.h"
#include "libavutil/crc.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/timer.h"

#ifdef __INTEL_COMPILER
#undef av_flatten
#define av_flatten
#endif

#define MAX_PLANES 4
#define CONTEXT_SIZE 32

#define MAX_QUANT_TABLES 8
#define MAX_CONTEXT_INPUTS 5

extern const uint8_t ff_log2_run[41];

static const int8_t quant5_10bit[256]={
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-0,-0,-0,-0,-0,-0,-0,-0,-0,-0,
};

static const int8_t quant5[256]={
 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-1,-1,-1,
};

static const int8_t quant9_10bit[256]={
 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3,
 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-3,-3,-3,-3,-3,-3,-3,
-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,
-3,-3,-3,-3,-3,-3,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-1,-1,-1,-1,-1,-1,-1,-1,-0,-0,-0,-0,
};

static const int8_t quant11[256]={
 0, 1, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-4,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
-4,-4,-4,-4,-4,-3,-3,-3,-3,-3,-3,-3,-2,-2,-2,-1,
};

static const uint8_t ver2_state[256]= {
   0,  10,  10,  10,  10,  16,  16,  16,  28,  16,  16,  29,  42,  49,  20,  49,
  59,  25,  26,  26,  27,  31,  33,  33,  33,  34,  34,  37,  67,  38,  39,  39,
  40,  40,  41,  79,  43,  44,  45,  45,  48,  48,  64,  50,  51,  52,  88,  52,
  53,  74,  55,  57,  58,  58,  74,  60, 101,  61,  62,  84,  66,  66,  68,  69,
  87,  82,  71,  97,  73,  73,  82,  75, 111,  77,  94,  78,  87,  81,  83,  97,
  85,  83,  94,  86,  99,  89,  90,  99, 111,  92,  93, 134,  95,  98, 105,  98,
 105, 110, 102, 108, 102, 118, 103, 106, 106, 113, 109, 112, 114, 112, 116, 125,
 115, 116, 117, 117, 126, 119, 125, 121, 121, 123, 145, 124, 126, 131, 127, 129,
 165, 130, 132, 138, 133, 135, 145, 136, 137, 139, 146, 141, 143, 142, 144, 148,
 147, 155, 151, 149, 151, 150, 152, 157, 153, 154, 156, 168, 158, 162, 161, 160,
 172, 163, 169, 164, 166, 184, 167, 170, 177, 174, 171, 173, 182, 176, 180, 178,
 175, 189, 179, 181, 186, 183, 192, 185, 200, 187, 191, 188, 190, 197, 193, 196,
 197, 194, 195, 196, 198, 202, 199, 201, 210, 203, 207, 204, 205, 206, 208, 214,
 209, 211, 221, 212, 213, 215, 224, 216, 217, 218, 219, 220, 222, 228, 223, 225,
 226, 224, 227, 229, 240, 230, 231, 232, 233, 234, 235, 236, 238, 239, 237, 242,
 241, 243, 242, 244, 245, 246, 247, 248, 249, 250, 251, 252, 252, 253, 254, 255,
};

typedef struct VlcState{
    int16_t drift;
    uint16_t error_sum;
    int8_t bias;
    uint8_t count;
} VlcState;

typedef struct PlaneContext{
    int16_t quant_table[MAX_CONTEXT_INPUTS][256];
    int quant_table_index;
    int context_count;
    uint8_t (*state)[CONTEXT_SIZE];
    VlcState *vlc_state;
    uint8_t interlace_bit_state[2];
} PlaneContext;

#define MAX_SLICES 256

typedef struct FFV1Context{
    AVClass *class;
    AVCodecContext *avctx;
    RangeCoder c;
    GetBitContext gb;
    PutBitContext pb;
    uint64_t rc_stat[256][2];
    uint64_t (*rc_stat2[MAX_QUANT_TABLES])[32][2];
    int version;
    int minor_version;
    int width, height;
    int chroma_h_shift, chroma_v_shift;
    int chroma_planes;
    int transparency;
    int flags;
    int picture_number;
    AVFrame picture;
    AVFrame last_picture;
    int plane_count;
    int ac;                              ///< 1=range coder <-> 0=golomb rice
    int ac_byte_count;                   ///< number of bytes used for AC coding
    PlaneContext plane[MAX_PLANES];
    int16_t quant_table[MAX_CONTEXT_INPUTS][256];
    int16_t quant_tables[MAX_QUANT_TABLES][MAX_CONTEXT_INPUTS][256];
    int context_count[MAX_QUANT_TABLES];
    uint8_t state_transition[256];
    uint8_t (*initial_states[MAX_QUANT_TABLES])[32];
    int run_index;
    int colorspace;
    int16_t *sample_buffer;
    int gob_count;
    int packed_at_lsb;
    int ec;
    int slice_damaged;
    int key_frame_ok;

    int quant_table_count;

    DSPContext dsp;

    struct FFV1Context *slice_context[MAX_SLICES];
    int slice_count;
    int num_v_slices;
    int num_h_slices;
    int slice_width;
    int slice_height;
    int slice_x;
    int slice_y;
    int bits_per_raw_sample;
}FFV1Context;

static av_always_inline int fold(int diff, int bits){
    if(bits==8)
        diff= (int8_t)diff;
    else{
        diff+= 1<<(bits-1);
        diff&=(1<<bits)-1;
        diff-= 1<<(bits-1);
    }

    return diff;
}

static inline int predict(int16_t *src, int16_t *last)
{
    const int LT= last[-1];
    const int  T= last[ 0];
    const int L =  src[-1];

    return mid_pred(L, L + T - LT, T);
}

static inline int get_context(PlaneContext *p, int16_t *src,
                              int16_t *last, int16_t *last2)
{
    const int LT= last[-1];
    const int  T= last[ 0];
    const int RT= last[ 1];
    const int L =  src[-1];

    if(p->quant_table[3][127]){
        const int TT= last2[0];
        const int LL=  src[-2];
        return p->quant_table[0][(L-LT) & 0xFF] + p->quant_table[1][(LT-T) & 0xFF] + p->quant_table[2][(T-RT) & 0xFF]
              +p->quant_table[3][(LL-L) & 0xFF] + p->quant_table[4][(TT-T) & 0xFF];
    }else
        return p->quant_table[0][(L-LT) & 0xFF] + p->quant_table[1][(LT-T) & 0xFF] + p->quant_table[2][(T-RT) & 0xFF];
}

static void find_best_state(uint8_t best_state[256][256], const uint8_t one_state[256]){
    int i,j,k,m;
    double l2tab[256];

    for(i=1; i<256; i++)
        l2tab[i]= log2(i/256.0);

    for(i=0; i<256; i++){
        double best_len[256];
        double p= i/256.0;

        for(j=0; j<256; j++)
            best_len[j]= 1<<30;

        for(j=FFMAX(i-10,1); j<FFMIN(i+11,256); j++){
            double occ[256]={0};
            double len=0;
            occ[j]=1.0;
            for(k=0; k<256; k++){
                double newocc[256]={0};
                for(m=0; m<256; m++){
                    if(occ[m]){
                        len -=occ[m]*(     p *l2tab[    m]
                                      + (1-p)*l2tab[256-m]);
                    }
                }
                if(len < best_len[k]){
                    best_len[k]= len;
                    best_state[i][k]= j;
                }
                for(m=0; m<256; m++){
                    if(occ[m]){
                        newocc[    one_state[    m]] += occ[m]*   p ;
                        newocc[256-one_state[256-m]] += occ[m]*(1-p);
                    }
                }
                memcpy(occ, newocc, sizeof(occ));
            }
        }
    }
}

static av_always_inline av_flatten void put_symbol_inline(RangeCoder *c, uint8_t *state, int v, int is_signed, uint64_t rc_stat[256][2], uint64_t rc_stat2[32][2]){
    int i;

#define put_rac(C,S,B) \
do{\
    if(rc_stat){\
    rc_stat[*(S)][B]++;\
        rc_stat2[(S)-state][B]++;\
    }\
    put_rac(C,S,B);\
}while(0)

    if(v){
        const int a= FFABS(v);
        const int e= av_log2(a);
        put_rac(c, state+0, 0);
        if(e<=9){
            for(i=0; i<e; i++){
                put_rac(c, state+1+i, 1);  //1..10
            }
            put_rac(c, state+1+i, 0);

            for(i=e-1; i>=0; i--){
                put_rac(c, state+22+i, (a>>i)&1); //22..31
            }

            if(is_signed)
                put_rac(c, state+11 + e, v < 0); //11..21
        }else{
            for(i=0; i<e; i++){
                put_rac(c, state+1+FFMIN(i,9), 1);  //1..10
            }
            put_rac(c, state+1+9, 0);

            for(i=e-1; i>=0; i--){
                put_rac(c, state+22+FFMIN(i,9), (a>>i)&1); //22..31
            }

            if(is_signed)
                put_rac(c, state+11 + 10, v < 0); //11..21
        }
    }else{
        put_rac(c, state+0, 1);
    }
#undef put_rac
}

static av_noinline void put_symbol(RangeCoder *c, uint8_t *state, int v, int is_signed){
    put_symbol_inline(c, state, v, is_signed, NULL, NULL);
}

static inline av_flatten int get_symbol_inline(RangeCoder *c, uint8_t *state, int is_signed){
    if(get_rac(c, state+0))
        return 0;
    else{
        int i, e, a;
        e= 0;
        while(get_rac(c, state+1 + FFMIN(e,9))){ //1..10
            e++;
        }

        a= 1;
        for(i=e-1; i>=0; i--){
            a += a + get_rac(c, state+22 + FFMIN(i,9)); //22..31
        }

        e= -(is_signed && get_rac(c, state+11 + FFMIN(e, 10))); //11..21
        return (a^e)-e;
    }
}

static av_noinline int get_symbol(RangeCoder *c, uint8_t *state, int is_signed){
    return get_symbol_inline(c, state, is_signed);
}

static inline void update_vlc_state(VlcState * const state, const int v){
    int drift= state->drift;
    int count= state->count;
    state->error_sum += FFABS(v);
    drift += v;

    if(count == 128){ //FIXME variable
        count >>= 1;
        drift >>= 1;
        state->error_sum >>= 1;
    }
    count++;

    if(drift <= -count){
        if(state->bias > -128) state->bias--;

        drift += count;
        if(drift <= -count)
            drift= -count + 1;
    }else if(drift > 0){
        if(state->bias <  127) state->bias++;

        drift -= count;
        if(drift > 0)
            drift= 0;
    }

    state->drift= drift;
    state->count= count;
}

static inline void put_vlc_symbol(PutBitContext *pb, VlcState * const state, int v, int bits){
    int i, k, code;
//printf("final: %d ", v);
    v = fold(v - state->bias, bits);

    i= state->count;
    k=0;
    while(i < state->error_sum){ //FIXME optimize
        k++;
        i += i;
    }

    assert(k<=8);

#if 0 // JPEG LS
    if(k==0 && 2*state->drift <= - state->count) code= v ^ (-1);
    else                                         code= v;
#else
     code= v ^ ((2*state->drift + state->count)>>31);
#endif

//printf("v:%d/%d bias:%d error:%d drift:%d count:%d k:%d\n", v, code, state->bias, state->error_sum, state->drift, state->count, k);
    set_sr_golomb(pb, code, k, 12, bits);

    update_vlc_state(state, v);
}

static inline int get_vlc_symbol(GetBitContext *gb, VlcState * const state, int bits){
    int k, i, v, ret;

    i= state->count;
    k=0;
    while(i < state->error_sum){ //FIXME optimize
        k++;
        i += i;
    }

    assert(k<=8);

    v= get_sr_golomb(gb, k, 12, bits);
//printf("v:%d bias:%d error:%d drift:%d count:%d k:%d", v, state->bias, state->error_sum, state->drift, state->count, k);

#if 0 // JPEG LS
    if(k==0 && 2*state->drift <= - state->count) v ^= (-1);
#else
     v ^= ((2*state->drift + state->count)>>31);
#endif

    ret= fold(v + state->bias, bits);

    update_vlc_state(state, v);
//printf("final: %d\n", ret);
    return ret;
}

#if CONFIG_FFV1_ENCODER
static av_always_inline int encode_line(FFV1Context *s, int w,
                                        int16_t *sample[3],
                                        int plane_index, int bits)
{
    PlaneContext * const p= &s->plane[plane_index];
    RangeCoder * const c= &s->c;
    int x;
    int run_index= s->run_index;
    int run_count=0;
    int run_mode=0;

    if(s->ac){
        if(c->bytestream_end - c->bytestream < w*20){
            av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
            return -1;
        }
    }else{
        if(s->pb.buf_end - s->pb.buf - (put_bits_count(&s->pb)>>3) < w*4){
            av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
            return -1;
        }
    }

    for(x=0; x<w; x++){
        int diff, context;

        context= get_context(p, sample[0]+x, sample[1]+x, sample[2]+x);
        diff= sample[0][x] - predict(sample[0]+x, sample[1]+x);

        if(context < 0){
            context = -context;
            diff= -diff;
        }

        diff= fold(diff, bits);

        if(s->ac){
            if(s->flags & CODEC_FLAG_PASS1){
                put_symbol_inline(c, p->state[context], diff, 1, s->rc_stat, s->rc_stat2[p->quant_table_index][context]);
            }else{
                put_symbol_inline(c, p->state[context], diff, 1, NULL, NULL);
            }
        }else{
            if(context == 0) run_mode=1;

            if(run_mode){

                if(diff){
                    while(run_count >= 1<<ff_log2_run[run_index]){
                        run_count -= 1<<ff_log2_run[run_index];
                        run_index++;
                        put_bits(&s->pb, 1, 1);
                    }

                    put_bits(&s->pb, 1 + ff_log2_run[run_index], run_count);
                    if(run_index) run_index--;
                    run_count=0;
                    run_mode=0;
                    if(diff>0) diff--;
                }else{
                    run_count++;
                }
            }

//            printf("count:%d index:%d, mode:%d, x:%d y:%d pos:%d\n", run_count, run_index, run_mode, x, y, (int)put_bits_count(&s->pb));

            if(run_mode == 0)
                put_vlc_symbol(&s->pb, &p->vlc_state[context], diff, bits);
        }
    }
    if(run_mode){
        while(run_count >= 1<<ff_log2_run[run_index]){
            run_count -= 1<<ff_log2_run[run_index];
            run_index++;
            put_bits(&s->pb, 1, 1);
        }

        if(run_count)
            put_bits(&s->pb, 1, 1);
    }
    s->run_index= run_index;

    return 0;
}

static void encode_plane(FFV1Context *s, uint8_t *src, int w, int h, int stride, int plane_index){
    int x,y,i;
    const int ring_size= s->avctx->context_model ? 3 : 2;
    int16_t *sample[3];
    s->run_index=0;

    memset(s->sample_buffer, 0, ring_size*(w+6)*sizeof(*s->sample_buffer));

    for(y=0; y<h; y++){
        for(i=0; i<ring_size; i++)
            sample[i]= s->sample_buffer + (w+6)*((h+i-y)%ring_size) + 3;

        sample[0][-1]= sample[1][0  ];
        sample[1][ w]= sample[1][w-1];
//{START_TIMER
        if(s->bits_per_raw_sample<=8){
            for(x=0; x<w; x++){
                sample[0][x]= src[x + stride*y];
            }
            encode_line(s, w, sample, plane_index, 8);
        }else{
            if(s->packed_at_lsb){
                for(x=0; x<w; x++){
                    sample[0][x]= ((uint16_t*)(src + stride*y))[x];
                }
            }else{
                for(x=0; x<w; x++){
                    sample[0][x]= ((uint16_t*)(src + stride*y))[x] >> (16 - s->bits_per_raw_sample);
                }
            }
            encode_line(s, w, sample, plane_index, s->bits_per_raw_sample);
        }
//STOP_TIMER("encode line")}
    }
}

static void encode_rgb_frame(FFV1Context *s, uint8_t *src[3], int w, int h, int stride[3]){
    int x, y, p, i;
    const int ring_size= s->avctx->context_model ? 3 : 2;
    int16_t *sample[4][3];
    int lbd=  s->avctx->bits_per_raw_sample <= 8;
    int bits= s->avctx->bits_per_raw_sample > 0 ? s->avctx->bits_per_raw_sample : 8;
    int offset= 1 << bits;
    s->run_index=0;

    memset(s->sample_buffer, 0, ring_size*4*(w+6)*sizeof(*s->sample_buffer));

    for(y=0; y<h; y++){
        for(i=0; i<ring_size; i++)
            for(p=0; p<4; p++)
                sample[p][i]= s->sample_buffer + p*ring_size*(w+6) + ((h+i-y)%ring_size)*(w+6) + 3;

        for(x=0; x<w; x++){
            int b,g,r,av_uninit(a);
            if(lbd){
                unsigned v= *((uint32_t*)(src[0] + x*4 + stride[0]*y));
                b= v&0xFF;
                g= (v>>8)&0xFF;
                r= (v>>16)&0xFF;
                a=  v>>24;
            }else{
                b= *((uint16_t*)(src[0] + x*2 + stride[0]*y));
                g= *((uint16_t*)(src[1] + x*2 + stride[1]*y));
                r= *((uint16_t*)(src[2] + x*2 + stride[2]*y));
            }

            b -= g;
            r -= g;
            g += (b + r)>>2;
            b += offset;
            r += offset;

//            assert(g>=0 && b>=0 && r>=0);
//            assert(g<256 && b<512 && r<512);
            sample[0][0][x]= g;
            sample[1][0][x]= b;
            sample[2][0][x]= r;
            sample[3][0][x]= a;
        }
        for(p=0; p<3 + s->transparency; p++){
            sample[p][0][-1]= sample[p][1][0  ];
            sample[p][1][ w]= sample[p][1][w-1];
            if (lbd)
                encode_line(s, w, sample[p], (p+1)/2, 9);
            else
                encode_line(s, w, sample[p], (p+1)/2, bits+1);
        }
    }
}

static void write_quant_table(RangeCoder *c, int16_t *quant_table){
    int last=0;
    int i;
    uint8_t state[CONTEXT_SIZE];
    memset(state, 128, sizeof(state));

    for(i=1; i<128 ; i++){
        if(quant_table[i] != quant_table[i-1]){
            put_symbol(c, state, i-last-1, 0);
            last= i;
        }
    }
    put_symbol(c, state, i-last-1, 0);
}

static void write_quant_tables(RangeCoder *c, int16_t quant_table[MAX_CONTEXT_INPUTS][256]){
    int i;
    for(i=0; i<5; i++)
        write_quant_table(c, quant_table[i]);
}

static void write_header(FFV1Context *f){
    uint8_t state[CONTEXT_SIZE];
    int i, j;
    RangeCoder * const c= &f->slice_context[0]->c;

    memset(state, 128, sizeof(state));

    if(f->version < 2){
        put_symbol(c, state, f->version, 0);
        put_symbol(c, state, f->ac, 0);
        if(f->ac>1){
            for(i=1; i<256; i++){
                put_symbol(c, state, f->state_transition[i] - c->one_state[i], 1);
            }
        }
        put_symbol(c, state, f->colorspace, 0); //YUV cs type
        if(f->version>0)
            put_symbol(c, state, f->bits_per_raw_sample, 0);
        put_rac(c, state, f->chroma_planes);
        put_symbol(c, state, f->chroma_h_shift, 0);
        put_symbol(c, state, f->chroma_v_shift, 0);
        put_rac(c, state, f->transparency);

        write_quant_tables(c, f->quant_table);
    }else if(f->version < 3){
        put_symbol(c, state, f->slice_count, 0);
        for(i=0; i<f->slice_count; i++){
            FFV1Context *fs= f->slice_context[i];
            put_symbol(c, state, (fs->slice_x     +1)*f->num_h_slices / f->width   , 0);
            put_symbol(c, state, (fs->slice_y     +1)*f->num_v_slices / f->height  , 0);
            put_symbol(c, state, (fs->slice_width +1)*f->num_h_slices / f->width -1, 0);
            put_symbol(c, state, (fs->slice_height+1)*f->num_v_slices / f->height-1, 0);
            for(j=0; j<f->plane_count; j++){
                put_symbol(c, state, f->plane[j].quant_table_index, 0);
                av_assert0(f->plane[j].quant_table_index == f->avctx->context_model);
            }
        }
    }
}
#endif /* CONFIG_FFV1_ENCODER */

static av_cold int common_init(AVCodecContext *avctx){
    FFV1Context *s = avctx->priv_data;

    s->avctx= avctx;
    s->flags= avctx->flags;

    avcodec_get_frame_defaults(&s->picture);

    ff_dsputil_init(&s->dsp, avctx);

    s->width = avctx->width;
    s->height= avctx->height;

    assert(s->width && s->height);
    //defaults
    s->num_h_slices=1;
    s->num_v_slices=1;


    return 0;
}

static int init_slice_state(FFV1Context *f, FFV1Context *fs){
    int j;

        fs->plane_count= f->plane_count;
        fs->transparency= f->transparency;
        for(j=0; j<f->plane_count; j++){
            PlaneContext * const p= &fs->plane[j];

            if(fs->ac){
                if(!p->    state) p->    state= av_malloc(CONTEXT_SIZE*p->context_count*sizeof(uint8_t));
                if(!p->    state)
                    return AVERROR(ENOMEM);
            }else{
                if(!p->vlc_state) p->vlc_state= av_malloc(p->context_count*sizeof(VlcState));
                if(!p->vlc_state)
                    return AVERROR(ENOMEM);
            }
        }

        if (fs->ac>1){
            //FIXME only redo if state_transition changed
            for(j=1; j<256; j++){
                fs->c.one_state [    j]= f->state_transition[j];
                fs->c.zero_state[256-j]= 256-fs->c.one_state [j];
            }
        }

    return 0;
}

static int init_slices_state(FFV1Context *f){
    int i;
    for(i=0; i<f->slice_count; i++){
        FFV1Context *fs= f->slice_context[i];
        if(init_slice_state(f, fs) < 0)
            return -1;
    }
    return 0;
}

static av_cold int init_slice_contexts(FFV1Context *f){
    int i;

    f->slice_count= f->num_h_slices * f->num_v_slices;

    for(i=0; i<f->slice_count; i++){
        FFV1Context *fs= av_mallocz(sizeof(*fs));
        int sx= i % f->num_h_slices;
        int sy= i / f->num_h_slices;
        int sxs= f->avctx->width * sx    / f->num_h_slices;
        int sxe= f->avctx->width *(sx+1) / f->num_h_slices;
        int sys= f->avctx->height* sy    / f->num_v_slices;
        int sye= f->avctx->height*(sy+1) / f->num_v_slices;
        f->slice_context[i]= fs;
        memcpy(fs, f, sizeof(*fs));
        memset(fs->rc_stat2, 0, sizeof(fs->rc_stat2));

        fs->slice_width = sxe - sxs;
        fs->slice_height= sye - sys;
        fs->slice_x     = sxs;
        fs->slice_y     = sys;

        fs->sample_buffer = av_malloc(3*4 * (fs->width+6) * sizeof(*fs->sample_buffer));
        if (!fs->sample_buffer)
            return AVERROR(ENOMEM);
    }
    return 0;
}

static int allocate_initial_states(FFV1Context *f){
    int i;

    for(i=0; i<f->quant_table_count; i++){
        f->initial_states[i]= av_malloc(f->context_count[i]*sizeof(*f->initial_states[i]));
        if(!f->initial_states[i])
            return AVERROR(ENOMEM);
        memset(f->initial_states[i], 128, f->context_count[i]*sizeof(*f->initial_states[i]));
    }
    return 0;
}

#if CONFIG_FFV1_ENCODER
static int write_extra_header(FFV1Context *f){
    RangeCoder * const c= &f->c;
    uint8_t state[CONTEXT_SIZE];
    int i, j, k;
    uint8_t state2[32][CONTEXT_SIZE];
    unsigned v;

    memset(state2, 128, sizeof(state2));
    memset(state, 128, sizeof(state));

    f->avctx->extradata= av_malloc(f->avctx->extradata_size= 10000 + (11*11*5*5*5+11*11*11)*32);
    ff_init_range_encoder(c, f->avctx->extradata, f->avctx->extradata_size);
    ff_build_rac_states(c, 0.05*(1LL<<32), 256-8);

    put_symbol(c, state, f->version, 0);
    if(f->version > 2) {
        if(f->version == 3)
            f->minor_version = 2;
        put_symbol(c, state, f->minor_version, 0);
    }
    put_symbol(c, state, f->ac, 0);
    if(f->ac>1){
        for(i=1; i<256; i++){
            put_symbol(c, state, f->state_transition[i] - c->one_state[i], 1);
        }
    }
    put_symbol(c, state, f->colorspace, 0); //YUV cs type
    put_symbol(c, state, f->bits_per_raw_sample, 0);
    put_rac(c, state, f->chroma_planes);
    put_symbol(c, state, f->chroma_h_shift, 0);
    put_symbol(c, state, f->chroma_v_shift, 0);
    put_rac(c, state, f->transparency);
    put_symbol(c, state, f->num_h_slices-1, 0);
    put_symbol(c, state, f->num_v_slices-1, 0);

    put_symbol(c, state, f->quant_table_count, 0);
    for(i=0; i<f->quant_table_count; i++)
        write_quant_tables(c, f->quant_tables[i]);

    for(i=0; i<f->quant_table_count; i++){
        for(j=0; j<f->context_count[i]*CONTEXT_SIZE; j++)
            if(f->initial_states[i] && f->initial_states[i][0][j] != 128)
                break;
        if(j<f->context_count[i]*CONTEXT_SIZE){
            put_rac(c, state, 1);
            for(j=0; j<f->context_count[i]; j++){
                for(k=0; k<CONTEXT_SIZE; k++){
                    int pred= j ? f->initial_states[i][j-1][k] : 128;
                    put_symbol(c, state2[k], (int8_t)(f->initial_states[i][j][k]-pred), 1);
                }
            }
        }else{
            put_rac(c, state, 0);
        }
    }

    if(f->version > 2){
        put_symbol(c, state, f->ec, 0);
    }

    f->avctx->extradata_size= ff_rac_terminate(c);
    v = av_crc(av_crc_get_table(AV_CRC_32_IEEE), 0, f->avctx->extradata, f->avctx->extradata_size);
    AV_WL32(f->avctx->extradata + f->avctx->extradata_size, v);
    f->avctx->extradata_size += 4;

    return 0;
}

static int sort_stt(FFV1Context *s, uint8_t stt[256]){
    int i,i2,changed,print=0;

    do{
        changed=0;
        for(i=12; i<244; i++){
            for(i2=i+1; i2<245 && i2<i+4; i2++){
#define COST(old, new) \
    s->rc_stat[old][0]*-log2((256-(new))/256.0)\
   +s->rc_stat[old][1]*-log2(     (new) /256.0)

#define COST2(old, new) \
    COST(old, new)\
   +COST(256-(old), 256-(new))

                double size0= COST2(i, i ) + COST2(i2, i2);
                double sizeX= COST2(i, i2) + COST2(i2, i );
                if(sizeX < size0 && i!=128 && i2!=128){
                    int j;
                    FFSWAP(int, stt[    i], stt[    i2]);
                    FFSWAP(int, s->rc_stat[i    ][0],s->rc_stat[    i2][0]);
                    FFSWAP(int, s->rc_stat[i    ][1],s->rc_stat[    i2][1]);
                    if(i != 256-i2){
                        FFSWAP(int, stt[256-i], stt[256-i2]);
                        FFSWAP(int, s->rc_stat[256-i][0],s->rc_stat[256-i2][0]);
                        FFSWAP(int, s->rc_stat[256-i][1],s->rc_stat[256-i2][1]);
                    }
                    for(j=1; j<256; j++){
                        if     (stt[j] == i ) stt[j] = i2;
                        else if(stt[j] == i2) stt[j] = i ;
                        if(i != 256-i2){
                            if     (stt[256-j] == 256-i ) stt[256-j] = 256-i2;
                            else if(stt[256-j] == 256-i2) stt[256-j] = 256-i ;
                        }
                    }
                    print=changed=1;
                }
            }
        }
    }while(changed);
    return print;
}

static av_cold int encode_init(AVCodecContext *avctx)
{
    FFV1Context *s = avctx->priv_data;
    int i, j, k, m;

    common_init(avctx);

    s->version=0;

    if((avctx->flags & (CODEC_FLAG_PASS1|CODEC_FLAG_PASS2)) || avctx->slices>1)
        s->version = FFMAX(s->version, 2);

    if(avctx->level == 3){
        s->version = 3;
    }

    if(s->ec < 0){
        s->ec = (s->version >= 3);
    }

    if(s->version >= 2 && avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
        av_log(avctx, AV_LOG_ERROR, "Version 2 needed for requested features but version 2 is experimental and not enabled\n");
        return AVERROR_INVALIDDATA;
    }

    s->ac= avctx->coder_type > 0 ? 2 : 0;

    s->plane_count=3;
    switch(avctx->pix_fmt){
    case PIX_FMT_YUV444P9:
    case PIX_FMT_YUV422P9:
    case PIX_FMT_YUV420P9:
        if (!avctx->bits_per_raw_sample)
            s->bits_per_raw_sample = 9;
    case PIX_FMT_YUV444P10:
    case PIX_FMT_YUV420P10:
    case PIX_FMT_YUV422P10:
        s->packed_at_lsb = 1;
        if (!avctx->bits_per_raw_sample && !s->bits_per_raw_sample)
            s->bits_per_raw_sample = 10;
    case PIX_FMT_GRAY16:
    case PIX_FMT_YUV444P16:
    case PIX_FMT_YUV422P16:
    case PIX_FMT_YUV420P16:
        if (!avctx->bits_per_raw_sample && !s->bits_per_raw_sample) {
            s->bits_per_raw_sample = 16;
        } else if (!s->bits_per_raw_sample){
            s->bits_per_raw_sample = avctx->bits_per_raw_sample;
        }
        if(s->bits_per_raw_sample <=8){
            av_log(avctx, AV_LOG_ERROR, "bits_per_raw_sample invalid\n");
            return AVERROR_INVALIDDATA;
        }
        if(!s->ac && avctx->coder_type == -1) {
            av_log(avctx, AV_LOG_INFO, "bits_per_raw_sample > 8, forcing coder 1\n");
            s->ac = 2;
        }
        if(!s->ac){
            av_log(avctx, AV_LOG_ERROR, "bits_per_raw_sample of more than 8 needs -coder 1 currently\n");
            return AVERROR_INVALIDDATA;
        }
        s->version= FFMAX(s->version, 1);
    case PIX_FMT_GRAY8:
    case PIX_FMT_YUV444P:
    case PIX_FMT_YUV440P:
    case PIX_FMT_YUV422P:
    case PIX_FMT_YUV420P:
    case PIX_FMT_YUV411P:
    case PIX_FMT_YUV410P:
        s->chroma_planes= av_pix_fmt_descriptors[avctx->pix_fmt].nb_components < 3 ? 0 : 1;
        s->colorspace= 0;
        break;
    case PIX_FMT_YUVA444P:
    case PIX_FMT_YUVA422P:
    case PIX_FMT_YUVA420P:
        s->chroma_planes= 1;
        s->colorspace= 0;
        s->transparency= 1;
        break;
    case PIX_FMT_RGB32:
        s->colorspace= 1;
        s->transparency= 1;
        break;
    case PIX_FMT_0RGB32:
        s->colorspace= 1;
        break;
    case PIX_FMT_GBRP9:
        if (!avctx->bits_per_raw_sample)
            s->bits_per_raw_sample = 9;
    case PIX_FMT_GBRP10:
        if (!avctx->bits_per_raw_sample && !s->bits_per_raw_sample)
            s->bits_per_raw_sample = 10;
    case PIX_FMT_GBRP12:
        if (!avctx->bits_per_raw_sample && !s->bits_per_raw_sample)
            s->bits_per_raw_sample = 12;
    case PIX_FMT_GBRP14:
        if (!avctx->bits_per_raw_sample && !s->bits_per_raw_sample)
            s->bits_per_raw_sample = 14;
        else if (!s->bits_per_raw_sample)
            s->bits_per_raw_sample = avctx->bits_per_raw_sample;
        s->colorspace= 1;
        s->chroma_planes= 1;
        s->version= FFMAX(s->version, 1);
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "format not supported\n");
        return AVERROR_INVALIDDATA;
    }
    if (s->transparency) {
        av_log(avctx, AV_LOG_WARNING, "Storing alpha plane, this will require a recent FFV1 decoder to playback!\n");
    }
    if (avctx->context_model > 1U) {
        av_log(avctx, AV_LOG_ERROR, "Invalid context model %d, valid values are 0 and 1\n", avctx->context_model);
        return AVERROR(EINVAL);
    }

    if(s->ac>1)
        for(i=1; i<256; i++)
            s->state_transition[i]=ver2_state[i];

    for(i=0; i<256; i++){
        s->quant_table_count=2;
        if(s->bits_per_raw_sample <=8){
            s->quant_tables[0][0][i]=           quant11[i];
            s->quant_tables[0][1][i]=        11*quant11[i];
            s->quant_tables[0][2][i]=     11*11*quant11[i];
            s->quant_tables[1][0][i]=           quant11[i];
            s->quant_tables[1][1][i]=        11*quant11[i];
            s->quant_tables[1][2][i]=     11*11*quant5 [i];
            s->quant_tables[1][3][i]=   5*11*11*quant5 [i];
            s->quant_tables[1][4][i]= 5*5*11*11*quant5 [i];
        }else{
            s->quant_tables[0][0][i]=           quant9_10bit[i];
            s->quant_tables[0][1][i]=        11*quant9_10bit[i];
            s->quant_tables[0][2][i]=     11*11*quant9_10bit[i];
            s->quant_tables[1][0][i]=           quant9_10bit[i];
            s->quant_tables[1][1][i]=        11*quant9_10bit[i];
            s->quant_tables[1][2][i]=     11*11*quant5_10bit[i];
            s->quant_tables[1][3][i]=   5*11*11*quant5_10bit[i];
            s->quant_tables[1][4][i]= 5*5*11*11*quant5_10bit[i];
        }
    }
    s->context_count[0]= (11*11*11+1)/2;
    s->context_count[1]= (11*11*5*5*5+1)/2;
    memcpy(s->quant_table, s->quant_tables[avctx->context_model], sizeof(s->quant_table));

    for(i=0; i<s->plane_count; i++){
        PlaneContext * const p= &s->plane[i];

        memcpy(p->quant_table, s->quant_table, sizeof(p->quant_table));
        p->quant_table_index= avctx->context_model;
        p->context_count= s->context_count[p->quant_table_index];
    }

    if(allocate_initial_states(s) < 0)
        return AVERROR(ENOMEM);

    avctx->coded_frame= &s->picture;
    if(!s->transparency)
        s->plane_count= 2;
    avcodec_get_chroma_sub_sample(avctx->pix_fmt, &s->chroma_h_shift, &s->chroma_v_shift);

    s->picture_number=0;

    if(avctx->flags & (CODEC_FLAG_PASS1|CODEC_FLAG_PASS2)){
        for(i=0; i<s->quant_table_count; i++){
            s->rc_stat2[i]= av_mallocz(s->context_count[i]*sizeof(*s->rc_stat2[i]));
            if(!s->rc_stat2[i])
                return AVERROR(ENOMEM);
        }
    }
    if(avctx->stats_in){
        char *p= avctx->stats_in;
        uint8_t best_state[256][256];
        int gob_count=0;
        char *next;

        av_assert0(s->version>=2);

        for(;;){
            for(j=0; j<256; j++){
                for(i=0; i<2; i++){
                    s->rc_stat[j][i]= strtol(p, &next, 0);
                    if(next==p){
                        av_log(avctx, AV_LOG_ERROR, "2Pass file invalid at %d %d [%s]\n", j,i,p);
                        return -1;
                    }
                    p=next;
                }
            }
            for(i=0; i<s->quant_table_count; i++){
                for(j=0; j<s->context_count[i]; j++){
                    for(k=0; k<32; k++){
                        for(m=0; m<2; m++){
                            s->rc_stat2[i][j][k][m]= strtol(p, &next, 0);
                            if(next==p){
                                av_log(avctx, AV_LOG_ERROR, "2Pass file invalid at %d %d %d %d [%s]\n", i,j,k,m,p);
                                return AVERROR_INVALIDDATA;
                            }
                            p=next;
                        }
                    }
                }
            }
            gob_count= strtol(p, &next, 0);
            if(next==p || gob_count <0){
                av_log(avctx, AV_LOG_ERROR, "2Pass file invalid\n");
                return AVERROR_INVALIDDATA;
            }
            p=next;
            while(*p=='\n' || *p==' ') p++;
            if(p[0]==0) break;
        }
        sort_stt(s, s->state_transition);

        find_best_state(best_state, s->state_transition);

        for(i=0; i<s->quant_table_count; i++){
            for(j=0; j<s->context_count[i]; j++){
                for(k=0; k<32; k++){
                    double p= 128;
                    if(s->rc_stat2[i][j][k][0]+s->rc_stat2[i][j][k][1]){
                        p=256.0*s->rc_stat2[i][j][k][1] / (s->rc_stat2[i][j][k][0]+s->rc_stat2[i][j][k][1]);
                    }
                    s->initial_states[i][j][k]= best_state[av_clip(round(p), 1, 255)][av_clip((s->rc_stat2[i][j][k][0]+s->rc_stat2[i][j][k][1])/gob_count, 0, 255)];
                }
            }
        }
    }

    if(s->version>1){
        for(s->num_v_slices=2; s->num_v_slices<9; s->num_v_slices++){
            for(s->num_h_slices=s->num_v_slices; s->num_h_slices<2*s->num_v_slices; s->num_h_slices++){
                if(avctx->slices == s->num_h_slices * s->num_v_slices && avctx->slices <= 64 || !avctx->slices)
                    goto slices_ok;
            }
        }
        av_log(avctx, AV_LOG_ERROR, "Unsupported number %d of slices requested, please specify a supported number with -slices (ex:4,6,9,12,16, ...)\n", avctx->slices);
        return -1;
        slices_ok:
        write_extra_header(s);
    }

    if(init_slice_contexts(s) < 0)
        return -1;
    if(init_slices_state(s) < 0)
        return -1;

#define STATS_OUT_SIZE 1024*1024*6
    if(avctx->flags & CODEC_FLAG_PASS1){
        avctx->stats_out= av_mallocz(STATS_OUT_SIZE);
        for(i=0; i<s->quant_table_count; i++){
            for(j=0; j<s->slice_count; j++){
                FFV1Context *sf= s->slice_context[j];
                av_assert0(!sf->rc_stat2[i]);
                sf->rc_stat2[i]= av_mallocz(s->context_count[i]*sizeof(*sf->rc_stat2[i]));
                if(!sf->rc_stat2[i])
                    return AVERROR(ENOMEM);
            }
        }
    }

    return 0;
}
#endif /* CONFIG_FFV1_ENCODER */


static void clear_slice_state(FFV1Context *f, FFV1Context *fs){
    int i, j;

        for(i=0; i<f->plane_count; i++){
            PlaneContext *p= &fs->plane[i];

            p->interlace_bit_state[0]= 128;
            p->interlace_bit_state[1]= 128;

            if(fs->ac){
                if(f->initial_states[p->quant_table_index]){
                    memcpy(p->state, f->initial_states[p->quant_table_index], CONTEXT_SIZE*p->context_count);
                }else
                memset(p->state, 128, CONTEXT_SIZE*p->context_count);
            }else{
                for(j=0; j<p->context_count; j++){
                    p->vlc_state[j].drift= 0;
                    p->vlc_state[j].error_sum= 4; //FFMAX((RANGE + 32)/64, 2);
                    p->vlc_state[j].bias= 0;
                    p->vlc_state[j].count= 1;
                }
            }
        }
}

#if CONFIG_FFV1_ENCODER

static void encode_slice_header(FFV1Context *f, FFV1Context *fs){
    RangeCoder *c = &fs->c;
    uint8_t state[CONTEXT_SIZE];
    int j;
    memset(state, 128, sizeof(state));

    put_symbol(c, state, (fs->slice_x     +1)*f->num_h_slices / f->width   , 0);
    put_symbol(c, state, (fs->slice_y     +1)*f->num_v_slices / f->height  , 0);
    put_symbol(c, state, (fs->slice_width +1)*f->num_h_slices / f->width -1, 0);
    put_symbol(c, state, (fs->slice_height+1)*f->num_v_slices / f->height-1, 0);
    for(j=0; j<f->plane_count; j++){
        put_symbol(c, state, f->plane[j].quant_table_index, 0);
        av_assert0(f->plane[j].quant_table_index == f->avctx->context_model);
    }
    if(!f->picture.interlaced_frame) put_symbol(c, state, 3, 0);
    else                             put_symbol(c, state, 1 + !f->picture.top_field_first, 0);
    put_symbol(c, state, f->picture.sample_aspect_ratio.num, 0);
    put_symbol(c, state, f->picture.sample_aspect_ratio.den, 0);
}

static int encode_slice(AVCodecContext *c, void *arg){
    FFV1Context *fs= *(void**)arg;
    FFV1Context *f= fs->avctx->priv_data;
    int width = fs->slice_width;
    int height= fs->slice_height;
    int x= fs->slice_x;
    int y= fs->slice_y;
    AVFrame * const p= &f->picture;
    const int ps= (f->bits_per_raw_sample>8)+1;

    if(p->key_frame)
        clear_slice_state(f, fs);
    if(f->version > 2){
        encode_slice_header(f, fs);
    }
    if(!fs->ac){
        if(f->version > 2)
            put_rac(&fs->c, (int[]){129}, 0);
        fs->ac_byte_count = f->version > 2 || (!x&&!y) ? ff_rac_terminate(&fs->c) : 0;
        init_put_bits(&fs->pb, fs->c.bytestream_start + fs->ac_byte_count, fs->c.bytestream_end - fs->c.bytestream_start - fs->ac_byte_count);
    }

    if(f->colorspace==0){
        const int chroma_width = -((-width )>>f->chroma_h_shift);
        const int chroma_height= -((-height)>>f->chroma_v_shift);
        const int cx= x>>f->chroma_h_shift;
        const int cy= y>>f->chroma_v_shift;

        encode_plane(fs, p->data[0] + ps*x + y*p->linesize[0], width, height, p->linesize[0], 0);

        if (f->chroma_planes){
            encode_plane(fs, p->data[1] + ps*cx+cy*p->linesize[1], chroma_width, chroma_height, p->linesize[1], 1);
            encode_plane(fs, p->data[2] + ps*cx+cy*p->linesize[2], chroma_width, chroma_height, p->linesize[2], 1);
        }
        if (fs->transparency)
            encode_plane(fs, p->data[3] + ps*x + y*p->linesize[3], width, height, p->linesize[3], 2);
    }else{
        uint8_t *planes[3] = {p->data[0] + ps*x + y*p->linesize[0],
                              p->data[1] + ps*x + y*p->linesize[1],
                              p->data[2] + ps*x + y*p->linesize[2]};
        encode_rgb_frame(fs, planes, width, height, p->linesize);
    }
    emms_c();

    return 0;
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *pict, int *got_packet)
{
    FFV1Context *f = avctx->priv_data;
    RangeCoder * const c= &f->slice_context[0]->c;
    AVFrame * const p= &f->picture;
    int used_count= 0;
    uint8_t keystate=128;
    uint8_t *buf_p;
    int i, ret;

    if ((ret = ff_alloc_packet2(avctx, pkt, avctx->width*avctx->height*((8*2+1+1)*4)/8
                                  + FF_MIN_BUFFER_SIZE)) < 0)
        return ret;

    ff_init_range_encoder(c, pkt->data, pkt->size);
    ff_build_rac_states(c, 0.05*(1LL<<32), 256-8);

    *p = *pict;
    p->pict_type= AV_PICTURE_TYPE_I;

    if(avctx->gop_size==0 || f->picture_number % avctx->gop_size == 0){
        put_rac(c, &keystate, 1);
        p->key_frame= 1;
        f->gob_count++;
        write_header(f);
    }else{
        put_rac(c, &keystate, 0);
        p->key_frame= 0;
    }

    if (f->ac>1){
        int i;
        for(i=1; i<256; i++){
            c->one_state[i]= f->state_transition[i];
            c->zero_state[256-i]= 256-c->one_state[i];
        }
    }

    for(i=1; i<f->slice_count; i++){
        FFV1Context *fs= f->slice_context[i];
        uint8_t *start = pkt->data + (pkt->size-used_count)*i/f->slice_count;
        int len = pkt->size/f->slice_count;

        ff_init_range_encoder(&fs->c, start, len);
    }
    avctx->execute(avctx, encode_slice, &f->slice_context[0], NULL, f->slice_count, sizeof(void*));

    buf_p = pkt->data;
    for(i=0; i<f->slice_count; i++){
        FFV1Context *fs= f->slice_context[i];
        int bytes;

        if(fs->ac){
            uint8_t state=129;
            put_rac(&fs->c, &state, 0);
            bytes= ff_rac_terminate(&fs->c);
        }else{
            flush_put_bits(&fs->pb); //nicer padding FIXME
            bytes= fs->ac_byte_count + (put_bits_count(&fs->pb)+7)/8;
        }
        if(i>0 || f->version>2){
            av_assert0(bytes < pkt->size/f->slice_count);
            memmove(buf_p, fs->c.bytestream_start, bytes);
            av_assert0(bytes < (1<<24));
            AV_WB24(buf_p+bytes, bytes);
            bytes+=3;
        }
        if(f->ec){
            unsigned v;
            buf_p[bytes++] = 0;
            v = av_crc(av_crc_get_table(AV_CRC_32_IEEE), 0, buf_p, bytes);
            AV_WL32(buf_p + bytes, v); bytes += 4;
        }
        buf_p += bytes;
    }

    if((avctx->flags&CODEC_FLAG_PASS1) && (f->picture_number&31)==0){
        int j, k, m;
        char *p= avctx->stats_out;
        char *end= p + STATS_OUT_SIZE;

        memset(f->rc_stat, 0, sizeof(f->rc_stat));
        for(i=0; i<f->quant_table_count; i++)
            memset(f->rc_stat2[i], 0, f->context_count[i]*sizeof(*f->rc_stat2[i]));

        for(j=0; j<f->slice_count; j++){
            FFV1Context *fs= f->slice_context[j];
            for(i=0; i<256; i++){
                f->rc_stat[i][0] += fs->rc_stat[i][0];
                f->rc_stat[i][1] += fs->rc_stat[i][1];
            }
            for(i=0; i<f->quant_table_count; i++){
                for(k=0; k<f->context_count[i]; k++){
                    for(m=0; m<32; m++){
                        f->rc_stat2[i][k][m][0] += fs->rc_stat2[i][k][m][0];
                        f->rc_stat2[i][k][m][1] += fs->rc_stat2[i][k][m][1];
                    }
                }
            }
        }

        for(j=0; j<256; j++){
            snprintf(p, end-p, "%"PRIu64" %"PRIu64" ", f->rc_stat[j][0], f->rc_stat[j][1]);
            p+= strlen(p);
        }
        snprintf(p, end-p, "\n");

        for(i=0; i<f->quant_table_count; i++){
            for(j=0; j<f->context_count[i]; j++){
                for(m=0; m<32; m++){
                    snprintf(p, end-p, "%"PRIu64" %"PRIu64" ", f->rc_stat2[i][j][m][0], f->rc_stat2[i][j][m][1]);
                    p+= strlen(p);
                }
            }
        }
        snprintf(p, end-p, "%d\n", f->gob_count);
    } else if(avctx->flags&CODEC_FLAG_PASS1)
        avctx->stats_out[0] = '\0';

    f->picture_number++;
    pkt->size   = buf_p - pkt->data;
    pkt->flags |= AV_PKT_FLAG_KEY*p->key_frame;
    *got_packet = 1;

    return 0;
}
#endif /* CONFIG_FFV1_ENCODER */

static av_cold int common_end(AVCodecContext *avctx){
    FFV1Context *s = avctx->priv_data;
    int i, j;

    if (avctx->codec->decode && s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);
    if (avctx->codec->decode && s->last_picture.data[0])
        avctx->release_buffer(avctx, &s->last_picture);

    for(j=0; j<s->slice_count; j++){
        FFV1Context *fs= s->slice_context[j];
        for(i=0; i<s->plane_count; i++){
            PlaneContext *p= &fs->plane[i];

            av_freep(&p->state);
            av_freep(&p->vlc_state);
        }
        av_freep(&fs->sample_buffer);
    }

    av_freep(&avctx->stats_out);
    for(j=0; j<s->quant_table_count; j++){
        av_freep(&s->initial_states[j]);
        for(i=0; i<s->slice_count; i++){
            FFV1Context *sf= s->slice_context[i];
            av_freep(&sf->rc_stat2[j]);
        }
        av_freep(&s->rc_stat2[j]);
    }

    for(i=0; i<s->slice_count; i++){
        av_freep(&s->slice_context[i]);
    }

    return 0;
}

static av_always_inline void decode_line(FFV1Context *s, int w,
                                         int16_t *sample[2],
                                         int plane_index, int bits)
{
    PlaneContext * const p= &s->plane[plane_index];
    RangeCoder * const c= &s->c;
    int x;
    int run_count=0;
    int run_mode=0;
    int run_index= s->run_index;

    for(x=0; x<w; x++){
        int diff, context, sign;

        context= get_context(p, sample[1] + x, sample[0] + x, sample[1] + x);
        if(context < 0){
            context= -context;
            sign=1;
        }else
            sign=0;

        av_assert2(context < p->context_count);

        if(s->ac){
            diff= get_symbol_inline(c, p->state[context], 1);
        }else{
            if(context == 0 && run_mode==0) run_mode=1;

            if(run_mode){
                if(run_count==0 && run_mode==1){
                    if(get_bits1(&s->gb)){
                        run_count = 1<<ff_log2_run[run_index];
                        if(x + run_count <= w) run_index++;
                    }else{
                        if(ff_log2_run[run_index]) run_count = get_bits(&s->gb, ff_log2_run[run_index]);
                        else run_count=0;
                        if(run_index) run_index--;
                        run_mode=2;
                    }
                }
                run_count--;
                if(run_count < 0){
                    run_mode=0;
                    run_count=0;
                    diff= get_vlc_symbol(&s->gb, &p->vlc_state[context], bits);
                    if(diff>=0) diff++;
                }else
                    diff=0;
            }else
                diff= get_vlc_symbol(&s->gb, &p->vlc_state[context], bits);

//            printf("count:%d index:%d, mode:%d, x:%d y:%d pos:%d\n", run_count, run_index, run_mode, x, y, get_bits_count(&s->gb));
        }

        if(sign) diff= -diff;

        sample[1][x]= (predict(sample[1] + x, sample[0] + x) + diff) & ((1<<bits)-1);
    }
    s->run_index= run_index;
}

static void decode_plane(FFV1Context *s, uint8_t *src, int w, int h, int stride, int plane_index){
    int x, y;
    int16_t *sample[2];
    sample[0]=s->sample_buffer    +3;
    sample[1]=s->sample_buffer+w+6+3;

    s->run_index=0;

    memset(s->sample_buffer, 0, 2*(w+6)*sizeof(*s->sample_buffer));

    for(y=0; y<h; y++){
        int16_t *temp = sample[0]; //FIXME try a normal buffer

        sample[0]= sample[1];
        sample[1]= temp;

        sample[1][-1]= sample[0][0  ];
        sample[0][ w]= sample[0][w-1];

//{START_TIMER
        if(s->avctx->bits_per_raw_sample <= 8){
            decode_line(s, w, sample, plane_index, 8);
            for(x=0; x<w; x++){
                src[x + stride*y]= sample[1][x];
            }
        }else{
            decode_line(s, w, sample, plane_index, s->avctx->bits_per_raw_sample);
            if(s->packed_at_lsb){
                for(x=0; x<w; x++){
                    ((uint16_t*)(src + stride*y))[x]= sample[1][x];
                }
            }else{
                for(x=0; x<w; x++){
                    ((uint16_t*)(src + stride*y))[x]= sample[1][x] << (16 - s->avctx->bits_per_raw_sample);
                }
            }
        }
//STOP_TIMER("decode-line")}
    }
}

static void decode_rgb_frame(FFV1Context *s, uint8_t *src[3], int w, int h, int stride[3]){
    int x, y, p;
    int16_t *sample[4][2];
    int lbd=  s->avctx->bits_per_raw_sample <= 8;
    int bits= s->avctx->bits_per_raw_sample > 0 ? s->avctx->bits_per_raw_sample : 8;
    int offset= 1 << bits;
    for(x=0; x<4; x++){
        sample[x][0] = s->sample_buffer +  x*2   *(w+6) + 3;
        sample[x][1] = s->sample_buffer + (x*2+1)*(w+6) + 3;
    }

    s->run_index=0;

    memset(s->sample_buffer, 0, 8*(w+6)*sizeof(*s->sample_buffer));

    for(y=0; y<h; y++){
        for(p=0; p<3 + s->transparency; p++){
            int16_t *temp = sample[p][0]; //FIXME try a normal buffer

            sample[p][0]= sample[p][1];
            sample[p][1]= temp;

            sample[p][1][-1]= sample[p][0][0  ];
            sample[p][0][ w]= sample[p][0][w-1];
            if (lbd)
                decode_line(s, w, sample[p], (p+1)/2, 9);
            else
                decode_line(s, w, sample[p], (p+1)/2, bits+1);
        }
        for(x=0; x<w; x++){
            int g= sample[0][1][x];
            int b= sample[1][1][x];
            int r= sample[2][1][x];
            int a= sample[3][1][x];

//            assert(g>=0 && b>=0 && r>=0);
//            assert(g<256 && b<512 && r<512);

            b -= offset;
            r -= offset;
            g -= (b + r)>>2;
            b += g;
            r += g;

            if(lbd)
                *((uint32_t*)(src[0] + x*4 + stride[0]*y))= b + (g<<8) + (r<<16) + (a<<24);
            else{
                *((uint16_t*)(src[0] + x*2 + stride[0]*y)) = b;
                *((uint16_t*)(src[1] + x*2 + stride[1]*y)) = g;
                *((uint16_t*)(src[2] + x*2 + stride[2]*y)) = r;
            }
        }
    }
}

static int decode_slice_header(FFV1Context *f, FFV1Context *fs){
    RangeCoder *c = &fs->c;
    uint8_t state[CONTEXT_SIZE];
    unsigned ps, i, context_count;
    memset(state, 128, sizeof(state));

    av_assert0(f->version > 2);

    fs->slice_x     = get_symbol(c, state, 0)   *f->width ;
    fs->slice_y     = get_symbol(c, state, 0)   *f->height;
    fs->slice_width =(get_symbol(c, state, 0)+1)*f->width  + fs->slice_x;
    fs->slice_height=(get_symbol(c, state, 0)+1)*f->height + fs->slice_y;

    fs->slice_x /= f->num_h_slices;
    fs->slice_y /= f->num_v_slices;
    fs->slice_width  = fs->slice_width /f->num_h_slices - fs->slice_x;
    fs->slice_height = fs->slice_height/f->num_v_slices - fs->slice_y;
    if((unsigned)fs->slice_width > f->width || (unsigned)fs->slice_height > f->height)
        return -1;
    if(    (unsigned)fs->slice_x + (uint64_t)fs->slice_width  > f->width
        || (unsigned)fs->slice_y + (uint64_t)fs->slice_height > f->height)
        return -1;

    for(i=0; i<f->plane_count; i++){
        PlaneContext * const p= &fs->plane[i];
        int idx=get_symbol(c, state, 0);
        if(idx > (unsigned)f->quant_table_count){
            av_log(f->avctx, AV_LOG_ERROR, "quant_table_index out of range\n");
            return -1;
        }
        p->quant_table_index= idx;
        memcpy(p->quant_table, f->quant_tables[idx], sizeof(p->quant_table));
        context_count= f->context_count[idx];

        if(p->context_count < context_count){
            av_freep(&p->state);
            av_freep(&p->vlc_state);
        }
        p->context_count= context_count;
    }

    ps = get_symbol(c, state, 0);
    if(ps==1){
        f->picture.interlaced_frame = 1;
        f->picture.top_field_first  = 1;
    } else if(ps==2){
        f->picture.interlaced_frame = 1;
        f->picture.top_field_first  = 0;
    } else if(ps==3){
        f->picture.interlaced_frame = 0;
    }
    f->picture.sample_aspect_ratio.num = get_symbol(c, state, 0);
    f->picture.sample_aspect_ratio.den = get_symbol(c, state, 0);

    return 0;
}

static int decode_slice(AVCodecContext *c, void *arg){
    FFV1Context *fs= *(void**)arg;
    FFV1Context *f= fs->avctx->priv_data;
    int width, height, x, y;
    const int ps= (c->bits_per_raw_sample>8)+1;
    AVFrame * const p= &f->picture;

    if(f->version > 2){
        if(init_slice_state(f, fs) < 0)
            return AVERROR(ENOMEM);
        if(decode_slice_header(f, fs) < 0) {
            fs->slice_damaged = 1;
            return AVERROR_INVALIDDATA;
        }
    }
    if(init_slice_state(f, fs) < 0)
        return AVERROR(ENOMEM);
    if(f->picture.key_frame)
        clear_slice_state(f, fs);
    width = fs->slice_width;
    height= fs->slice_height;
    x= fs->slice_x;
    y= fs->slice_y;

    if(!fs->ac){
        if (f->version == 3 && f->minor_version > 1 || f->version > 3)
            get_rac(&fs->c, (int[]){129});
        fs->ac_byte_count = f->version > 2 || (!x&&!y) ? fs->c.bytestream - fs->c.bytestream_start - 1 : 0;
        init_get_bits(&fs->gb,
                      fs->c.bytestream_start + fs->ac_byte_count,
                      (fs->c.bytestream_end - fs->c.bytestream_start - fs->ac_byte_count) * 8);
    }

    av_assert1(width && height);
    if(f->colorspace==0){
        const int chroma_width = -((-width )>>f->chroma_h_shift);
        const int chroma_height= -((-height)>>f->chroma_v_shift);
        const int cx= x>>f->chroma_h_shift;
        const int cy= y>>f->chroma_v_shift;
        decode_plane(fs, p->data[0] + ps*x + y*p->linesize[0], width, height, p->linesize[0], 0);

        if (f->chroma_planes){
            decode_plane(fs, p->data[1] + ps*cx+cy*p->linesize[1], chroma_width, chroma_height, p->linesize[1], 1);
            decode_plane(fs, p->data[2] + ps*cx+cy*p->linesize[2], chroma_width, chroma_height, p->linesize[2], 1);
        }
        if (fs->transparency)
            decode_plane(fs, p->data[3] + ps*x + y*p->linesize[3], width, height, p->linesize[3], 2);
    }else{
        uint8_t *planes[3] = {p->data[0] + ps*x + y*p->linesize[0],
                              p->data[1] + ps*x + y*p->linesize[1],
                              p->data[2] + ps*x + y*p->linesize[2]};
        decode_rgb_frame(fs, planes, width, height, p->linesize);
    }
    if(fs->ac && f->version > 2) {
        int v;
        get_rac(&fs->c, (int[]){129});
        v = fs->c.bytestream_end - fs->c.bytestream - 2 - 5*f->ec;
        if(v) {
            av_log(f->avctx, AV_LOG_ERROR, "bytestream end mismatching by %d\n", v);
            fs->slice_damaged = 1;
        }
    }

    emms_c();

    return 0;
}

static int read_quant_table(RangeCoder *c, int16_t *quant_table, int scale){
    int v;
    int i=0;
    uint8_t state[CONTEXT_SIZE];

    memset(state, 128, sizeof(state));

    for(v=0; i<128 ; v++){
        unsigned len= get_symbol(c, state, 0) + 1;

        if(len > 128 - i) return -1;

        while(len--){
            quant_table[i] = scale*v;
            i++;
//printf("%2d ",v);
//if(i%16==0) printf("\n");
        }
    }

    for(i=1; i<128; i++){
        quant_table[256-i]= -quant_table[i];
    }
    quant_table[128]= -quant_table[127];

    return 2*v - 1;
}

static int read_quant_tables(RangeCoder *c, int16_t quant_table[MAX_CONTEXT_INPUTS][256]){
    int i;
    int context_count=1;

    for(i=0; i<5; i++){
        context_count*= read_quant_table(c, quant_table[i], context_count);
        if(context_count > 32768U){
            return -1;
        }
    }
    return (context_count+1)/2;
}

static int read_extra_header(FFV1Context *f){
    RangeCoder * const c= &f->c;
    uint8_t state[CONTEXT_SIZE];
    int i, j, k;
    uint8_t state2[32][CONTEXT_SIZE];

    memset(state2, 128, sizeof(state2));
    memset(state, 128, sizeof(state));

    ff_init_range_decoder(c, f->avctx->extradata, f->avctx->extradata_size);
    ff_build_rac_states(c, 0.05*(1LL<<32), 256-8);

    f->version= get_symbol(c, state, 0);
    if(f->version > 2) {
        c->bytestream_end -= 4;
        f->minor_version= get_symbol(c, state, 0);
    }
    f->ac= f->avctx->coder_type= get_symbol(c, state, 0);
    if(f->ac>1){
        for(i=1; i<256; i++){
            f->state_transition[i]= get_symbol(c, state, 1) + c->one_state[i];
        }
    }
    f->colorspace= get_symbol(c, state, 0); //YUV cs type
    f->avctx->bits_per_raw_sample= get_symbol(c, state, 0);
    f->chroma_planes= get_rac(c, state);
    f->chroma_h_shift= get_symbol(c, state, 0);
    f->chroma_v_shift= get_symbol(c, state, 0);
    f->transparency= get_rac(c, state);
    f->plane_count= 2 + f->transparency;
    f->num_h_slices= 1 + get_symbol(c, state, 0);
    f->num_v_slices= 1 + get_symbol(c, state, 0);
    if(f->num_h_slices > (unsigned)f->width || f->num_v_slices > (unsigned)f->height){
        av_log(f->avctx, AV_LOG_ERROR, "too many slices\n");
        return -1;
    }

    f->quant_table_count= get_symbol(c, state, 0);
    if(f->quant_table_count > (unsigned)MAX_QUANT_TABLES)
        return -1;
    for(i=0; i<f->quant_table_count; i++){
        if((f->context_count[i]= read_quant_tables(c, f->quant_tables[i])) < 0){
            av_log(f->avctx, AV_LOG_ERROR, "read_quant_table error\n");
            return -1;
        }
    }

    if(allocate_initial_states(f) < 0)
        return AVERROR(ENOMEM);

    for(i=0; i<f->quant_table_count; i++){
        if(get_rac(c, state)){
            for(j=0; j<f->context_count[i]; j++){
                for(k=0; k<CONTEXT_SIZE; k++){
                    int pred= j ? f->initial_states[i][j-1][k] : 128;
                    f->initial_states[i][j][k]= (pred+get_symbol(c, state2[k], 1))&0xFF;
                }
            }
        }
    }

    if(f->version > 2){
        f->ec = get_symbol(c, state, 0);
    }

    if(f->version > 2){
        unsigned v;
        v = av_crc(av_crc_get_table(AV_CRC_32_IEEE), 0, f->avctx->extradata, f->avctx->extradata_size);
        if(v){
            av_log(f->avctx, AV_LOG_ERROR, "CRC mismatch %X!\n", v);
            return AVERROR_INVALIDDATA;
        }
    }

    return 0;
}

static int read_header(FFV1Context *f){
    uint8_t state[CONTEXT_SIZE];
    int i, j, context_count = -1; //-1 to avoid warning
    RangeCoder * const c= &f->slice_context[0]->c;

    memset(state, 128, sizeof(state));

    if(f->version < 2){
        unsigned v= get_symbol(c, state, 0);
        if(v >= 2){
            av_log(f->avctx, AV_LOG_ERROR, "invalid version %d in ver01 header\n", v);
            return AVERROR_INVALIDDATA;
        }
        f->version = v;
        f->ac= f->avctx->coder_type= get_symbol(c, state, 0);
        if(f->ac>1){
            for(i=1; i<256; i++){
                f->state_transition[i]= get_symbol(c, state, 1) + c->one_state[i];
            }
        }
        f->colorspace= get_symbol(c, state, 0); //YUV cs type
        if(f->version>0)
            f->avctx->bits_per_raw_sample= get_symbol(c, state, 0);
        f->chroma_planes= get_rac(c, state);
        f->chroma_h_shift= get_symbol(c, state, 0);
        f->chroma_v_shift= get_symbol(c, state, 0);
        f->transparency= get_rac(c, state);
        f->plane_count= 2 + f->transparency;
    }

    if(f->colorspace==0){
        if(!f->transparency && !f->chroma_planes){
            if (f->avctx->bits_per_raw_sample<=8)
                f->avctx->pix_fmt= PIX_FMT_GRAY8;
            else
                f->avctx->pix_fmt= PIX_FMT_GRAY16;
        }else if(f->avctx->bits_per_raw_sample<=8 && !f->transparency){
            switch(16*f->chroma_h_shift + f->chroma_v_shift){
            case 0x00: f->avctx->pix_fmt= PIX_FMT_YUV444P; break;
            case 0x01: f->avctx->pix_fmt= PIX_FMT_YUV440P; break;
            case 0x10: f->avctx->pix_fmt= PIX_FMT_YUV422P; break;
            case 0x11: f->avctx->pix_fmt= PIX_FMT_YUV420P; break;
            case 0x20: f->avctx->pix_fmt= PIX_FMT_YUV411P; break;
            case 0x22: f->avctx->pix_fmt= PIX_FMT_YUV410P; break;
            default:
                av_log(f->avctx, AV_LOG_ERROR, "format not supported\n");
                return -1;
            }
        }else if(f->avctx->bits_per_raw_sample<=8 && f->transparency){
            switch(16*f->chroma_h_shift + f->chroma_v_shift){
            case 0x00: f->avctx->pix_fmt= PIX_FMT_YUVA444P; break;
            case 0x10: f->avctx->pix_fmt= PIX_FMT_YUVA422P; break;
            case 0x11: f->avctx->pix_fmt= PIX_FMT_YUVA420P; break;
            default:
                av_log(f->avctx, AV_LOG_ERROR, "format not supported\n");
                return -1;
            }
        }else if(f->avctx->bits_per_raw_sample==9) {
            f->packed_at_lsb=1;
            switch(16*f->chroma_h_shift + f->chroma_v_shift){
            case 0x00: f->avctx->pix_fmt= PIX_FMT_YUV444P9; break;
            case 0x10: f->avctx->pix_fmt= PIX_FMT_YUV422P9; break;
            case 0x11: f->avctx->pix_fmt= PIX_FMT_YUV420P9; break;
            default:
                av_log(f->avctx, AV_LOG_ERROR, "format not supported\n");
                return -1;
            }
        }else if(f->avctx->bits_per_raw_sample==10) {
            f->packed_at_lsb=1;
            switch(16*f->chroma_h_shift + f->chroma_v_shift){
            case 0x00: f->avctx->pix_fmt= PIX_FMT_YUV444P10; break;
            case 0x10: f->avctx->pix_fmt= PIX_FMT_YUV422P10; break;
            case 0x11: f->avctx->pix_fmt= PIX_FMT_YUV420P10; break;
            default:
                av_log(f->avctx, AV_LOG_ERROR, "format not supported\n");
                return -1;
            }
        }else {
            switch(16*f->chroma_h_shift + f->chroma_v_shift){
            case 0x00: f->avctx->pix_fmt= PIX_FMT_YUV444P16; break;
            case 0x10: f->avctx->pix_fmt= PIX_FMT_YUV422P16; break;
            case 0x11: f->avctx->pix_fmt= PIX_FMT_YUV420P16; break;
            default:
                av_log(f->avctx, AV_LOG_ERROR, "format not supported\n");
                return -1;
            }
        }
    }else if(f->colorspace==1){
        if(f->chroma_h_shift || f->chroma_v_shift){
            av_log(f->avctx, AV_LOG_ERROR, "chroma subsampling not supported in this colorspace\n");
            return -1;
        }
        if(f->avctx->bits_per_raw_sample==9)
            f->avctx->pix_fmt= PIX_FMT_GBRP9;
        else if(f->avctx->bits_per_raw_sample==10)
            f->avctx->pix_fmt= PIX_FMT_GBRP10;
        else if(f->avctx->bits_per_raw_sample==12)
            f->avctx->pix_fmt= PIX_FMT_GBRP12;
        else if(f->avctx->bits_per_raw_sample==14)
            f->avctx->pix_fmt= PIX_FMT_GBRP14;
        else
        if(f->transparency) f->avctx->pix_fmt= PIX_FMT_RGB32;
        else                f->avctx->pix_fmt= PIX_FMT_0RGB32;
    }else{
        av_log(f->avctx, AV_LOG_ERROR, "colorspace not supported\n");
        return -1;
    }

//printf("%d %d %d\n", f->chroma_h_shift, f->chroma_v_shift,f->avctx->pix_fmt);
    if(f->version < 2){
        context_count= read_quant_tables(c, f->quant_table);
        if(context_count < 0){
                av_log(f->avctx, AV_LOG_ERROR, "read_quant_table error\n");
                return -1;
        }
    }else if(f->version < 3){
        f->slice_count= get_symbol(c, state, 0);
    }else{
        const uint8_t *p= c->bytestream_end;
        for(f->slice_count = 0; f->slice_count < MAX_SLICES && 3 < p - c->bytestream_start; f->slice_count++){
            int trailer = 3 + 5*!!f->ec;
            int size = AV_RB24(p-trailer);
            if(size + trailer > p - c->bytestream_start)
                break;
            p -= size + trailer;
        }
    }
    if(f->slice_count > (unsigned)MAX_SLICES || f->slice_count <= 0){
        av_log(f->avctx, AV_LOG_ERROR, "slice count %d is invalid\n", f->slice_count);
        return -1;
    }

    for(j=0; j<f->slice_count; j++){
        FFV1Context *fs= f->slice_context[j];
        fs->ac= f->ac;
        fs->packed_at_lsb= f->packed_at_lsb;

        fs->slice_damaged = 0;

        if(f->version == 2){
            fs->slice_x     = get_symbol(c, state, 0)   *f->width ;
            fs->slice_y     = get_symbol(c, state, 0)   *f->height;
            fs->slice_width =(get_symbol(c, state, 0)+1)*f->width  + fs->slice_x;
            fs->slice_height=(get_symbol(c, state, 0)+1)*f->height + fs->slice_y;

            fs->slice_x /= f->num_h_slices;
            fs->slice_y /= f->num_v_slices;
            fs->slice_width  = fs->slice_width /f->num_h_slices - fs->slice_x;
            fs->slice_height = fs->slice_height/f->num_v_slices - fs->slice_y;
            if((unsigned)fs->slice_width > f->width || (unsigned)fs->slice_height > f->height)
                return -1;
            if(    (unsigned)fs->slice_x + (uint64_t)fs->slice_width  > f->width
                || (unsigned)fs->slice_y + (uint64_t)fs->slice_height > f->height)
                return -1;
        }

        for(i=0; i<f->plane_count; i++){
            PlaneContext * const p= &fs->plane[i];

            if(f->version == 2){
                int idx=get_symbol(c, state, 0);
                if(idx > (unsigned)f->quant_table_count){
                    av_log(f->avctx, AV_LOG_ERROR, "quant_table_index out of range\n");
                    return -1;
                }
                p->quant_table_index= idx;
                memcpy(p->quant_table, f->quant_tables[idx], sizeof(p->quant_table));
                context_count= f->context_count[idx];
            }else{
                memcpy(p->quant_table, f->quant_table, sizeof(p->quant_table));
            }

            if(f->version <= 2){
                av_assert0(context_count>=0);
                if(p->context_count < context_count){
                    av_freep(&p->state);
                    av_freep(&p->vlc_state);
                }
                p->context_count= context_count;
            }
        }
    }
    return 0;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    FFV1Context *f = avctx->priv_data;

    common_init(avctx);

    if(avctx->extradata && read_extra_header(f) < 0)
        return -1;

    if(init_slice_contexts(f) < 0)
        return -1;

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, AVPacket *avpkt){
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    FFV1Context *f = avctx->priv_data;
    RangeCoder * const c= &f->slice_context[0]->c;
    AVFrame * const p= &f->picture;
    int i;
    uint8_t keystate= 128;
    const uint8_t *buf_p;

    AVFrame *picture = data;

    /* release previously stored data */
    if (p->data[0])
        avctx->release_buffer(avctx, p);

    ff_init_range_decoder(c, buf, buf_size);
    ff_build_rac_states(c, 0.05*(1LL<<32), 256-8);


    p->pict_type= AV_PICTURE_TYPE_I; //FIXME I vs. P
    if(get_rac(c, &keystate)){
        p->key_frame= 1;
        f->key_frame_ok = 0;
        if(read_header(f) < 0)
            return -1;
        f->key_frame_ok = 1;
    }else{
        if (!f->key_frame_ok) {
            av_log(avctx, AV_LOG_ERROR, "Cant decode non keyframe without valid keyframe\n");
            return AVERROR_INVALIDDATA;
        }
        p->key_frame= 0;
    }

    p->reference= 3; //for error concealment
    if(avctx->get_buffer(avctx, p) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    if(avctx->debug&FF_DEBUG_PICT_INFO)
        av_log(avctx, AV_LOG_DEBUG, "ver:%d keyframe:%d coder:%d ec:%d slices:%d\n",
               f->version, p->key_frame, f->ac, f->ec, f->slice_count);

    buf_p= buf + buf_size;
    for(i=f->slice_count-1; i>=0; i--){
        FFV1Context *fs= f->slice_context[i];
        int trailer = 3 + 5*!!f->ec;
        int v;

        if(i || f->version>2) v = AV_RB24(buf_p-trailer)+trailer;
        else                  v = buf_p - c->bytestream_start;
        if(buf_p - c->bytestream_start < v){
            av_log(avctx, AV_LOG_ERROR, "Slice pointer chain broken\n");
            return -1;
        }
        buf_p -= v;

        if(f->ec){
            unsigned crc = av_crc(av_crc_get_table(AV_CRC_32_IEEE), 0, buf_p, v);
            if(crc){
                int64_t ts = avpkt->pts != AV_NOPTS_VALUE ? avpkt->pts : avpkt->dts;
                av_log(f->avctx, AV_LOG_ERROR, "CRC mismatch %X!", crc);
                if(ts != AV_NOPTS_VALUE && avctx->pkt_timebase.num) {
                    av_log(f->avctx, AV_LOG_ERROR, "at %f seconds\n",ts*av_q2d(avctx->pkt_timebase));
                } else if(ts != AV_NOPTS_VALUE) {
                    av_log(f->avctx, AV_LOG_ERROR, "at %"PRId64"\n", ts);
                } else {
                    av_log(f->avctx, AV_LOG_ERROR, "\n");
                }
                fs->slice_damaged = 1;
            }
        }

        if(i){
            ff_init_range_decoder(&fs->c, buf_p, v);
        }else
            fs->c.bytestream_end = (uint8_t *)(buf_p + v);
    }

    avctx->execute(avctx, decode_slice, &f->slice_context[0], NULL, f->slice_count, sizeof(void*));

    for(i=f->slice_count-1; i>=0; i--){
        FFV1Context *fs= f->slice_context[i];
        int j;
        if(fs->slice_damaged && f->last_picture.data[0]){
            uint8_t *dst[4], *src[4];
            for(j=0; j<4; j++){
                int sh = (j==1 || j==2) ? f->chroma_h_shift : 0;
                int sv = (j==1 || j==2) ? f->chroma_v_shift : 0;
                dst[j] = f->picture     .data[j] + f->picture     .linesize[j]*
                         (fs->slice_y>>sv) + (fs->slice_x>>sh);
                src[j] = f->last_picture.data[j] + f->last_picture.linesize[j]*
                         (fs->slice_y>>sv) + (fs->slice_x>>sh);
            }
            av_image_copy(dst, f->picture.linesize, (const uint8_t **)src, f->last_picture.linesize,
                          avctx->pix_fmt, fs->slice_width, fs->slice_height);
        }
    }

    f->picture_number++;

    *picture= *p;
    *data_size = sizeof(AVFrame);

    FFSWAP(AVFrame, f->picture, f->last_picture);

    return buf_size;
}

AVCodec ff_ffv1_decoder = {
    .name           = "ffv1",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_FFV1,
    .priv_data_size = sizeof(FFV1Context),
    .init           = decode_init,
    .close          = common_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1 /*| CODEC_CAP_DRAW_HORIZ_BAND*/ |
                      CODEC_CAP_SLICE_THREADS,
    .long_name      = NULL_IF_CONFIG_SMALL("FFmpeg video codec #1"),
};

#if CONFIG_FFV1_ENCODER

#define OFFSET(x) offsetof(FFV1Context, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "slicecrc",        "Protect slices with CRCs",               OFFSET(ec),              AV_OPT_TYPE_INT, {.i64 = -1}, -1, 1, VE},
{NULL}
};

static const AVClass class = {
    .class_name = "ffv1 encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecDefault ffv1_defaults[] = {
    { "coder",                "-1" },
    { NULL },
};

AVCodec ff_ffv1_encoder = {
    .name           = "ffv1",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_FFV1,
    .priv_data_size = sizeof(FFV1Context),
    .init           = encode_init,
    .encode2        = encode_frame,
    .close          = common_end,
    .capabilities   = CODEC_CAP_SLICE_THREADS,
    .defaults       = ffv1_defaults,
    .pix_fmts       = (const enum PixelFormat[]){
        PIX_FMT_YUV420P, PIX_FMT_YUVA420P, PIX_FMT_YUVA422P, PIX_FMT_YUV444P,
        PIX_FMT_YUVA444P, PIX_FMT_YUV440P, PIX_FMT_YUV422P, PIX_FMT_YUV411P,
        PIX_FMT_YUV410P, PIX_FMT_0RGB32, PIX_FMT_RGB32, PIX_FMT_YUV420P16,
        PIX_FMT_YUV422P16, PIX_FMT_YUV444P16, PIX_FMT_YUV444P9, PIX_FMT_YUV422P9,
        PIX_FMT_YUV420P9, PIX_FMT_YUV420P10, PIX_FMT_YUV422P10, PIX_FMT_YUV444P10,
        PIX_FMT_GRAY16, PIX_FMT_GRAY8, PIX_FMT_GBRP9, PIX_FMT_GBRP10,
        PIX_FMT_GBRP12, PIX_FMT_GBRP14,
        PIX_FMT_NONE
    },
    .long_name      = NULL_IF_CONFIG_SMALL("FFmpeg video codec #1"),
    .priv_class     = &class,
};
#endif
