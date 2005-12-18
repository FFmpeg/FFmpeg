/*
 * JPEG-LS encoder and decoder
 * Copyright (c) 2003 Michael Niedermayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * @file jpeg_ls.c
 * JPEG-LS encoder and decoder.
 */

#undef printf
#undef fprintf

static inline int quantize(MJpegDecodeContext *s, int v){ //FIXME optimize
    if(v==0) return 0;
    if(v < 0){
        if     (v >-s->t1) return -1;
        else if(v >-s->t2) return -2;
        else if(v >-s->t3) return -3;
        else               return -4;
    }else{
        if     (v < s->t1) return 1;
        else if(v < s->t2) return 2;
        else if(v < s->t3) return 3;
        else               return 4;
    }
}

static inline int predict8(uint8_t *src, uint8_t *last){ //FIXME perhaps its better to suppress these 2
    const int LT= last[-1];
    const int  T= last[ 0];
    const int L =  src[-1];

    return mid_pred(L, L + T - LT, T);
}

static inline int predict16(uint16_t *src, uint16_t *last){
    const int LT= last[-1];
    const int  T= last[ 0];
    const int L =  src[-1];

    return mid_pred(L, L + T - LT, T);
}

static int encode_picture_ls(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data){
    return 0;
}

static int iso_clip(int v, int vmin, int vmax){
    if(v > vmax || v < vmin) return vmin;
    else                     return v;
}

static void reset_ls_coding_parameters(MJpegDecodeContext *s, int reset_all){
    const int basic_t1= 3;
    const int basic_t2= 7;
    const int basic_t3= 21;
    int factor;

    if(s->maxval==0 || reset_all) s->maxval= (1<<s->bits) - 1;
    
    if(s->maxval >=128){
        factor= (FFMIN(s->maxval, 4096) + 128)>>8;

        if(s->t1==0     || reset_all)
            s->t1= iso_clip(factor*(basic_t1-2) + 2 + 3*s->near, s->near+1, s->maxval);
        if(s->t2==0     || reset_all)
            s->t2= iso_clip(factor*(basic_t2-3) + 3 + 5*s->near, s->t1, s->maxval);
        if(s->t3==0     || reset_all)
            s->t3= iso_clip(factor*(basic_t3-4) + 4 + 7*s->near, s->t2, s->maxval);
    }else{
        factor= 256 / (s->maxval + 1);

        if(s->t1==0     || reset_all)
            s->t1= iso_clip(FFMAX(2, basic_t1/factor + 3*s->near), s->near+1, s->maxval);
        if(s->t2==0     || reset_all)
            s->t2= iso_clip(FFMAX(3, basic_t2/factor + 5*s->near), s->t1, s->maxval);
        if(s->t3==0     || reset_all)
            s->t3= iso_clip(FFMAX(4, basic_t3/factor + 6*s->near), s->t2, s->maxval);
    }

    if(s->reset==0  || reset_all) s->reset= 64;
}

static int decode_lse(MJpegDecodeContext *s)
{
    int len, id;

    /* XXX: verify len field validity */
    len = get_bits(&s->gb, 16);
    id = get_bits(&s->gb, 8);
    
    switch(id){
    case 1:
        s->maxval= get_bits(&s->gb, 16);
        s->t1= get_bits(&s->gb, 16);
        s->t2= get_bits(&s->gb, 16);
        s->t3= get_bits(&s->gb, 16);
        s->reset= get_bits(&s->gb, 16);
        
        reset_ls_coding_parameters(s, 0);
        //FIXME quant table?
    break;
    case 2:
    case 3:
        printf("palette not supported\n");
        return -1;
    case 4:
        printf("oversize image not supported\n");
        return -1;
    default:
        printf("invalid id %d\n", id);
        return -1;
    }

    return 0;
}
#if 0
static inline void update_vlc_state(VlcState * const state, const int v, int half_count){
    int drift= state->drift;
    int count= state->count;
    state->error_sum += ABS(v);
    drift += v;

    if(count == half_count){
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

#define R(p, i) (is_uint8 ? (((uint8_t*)p)[i] : ((uint16_t*)p)[i])

static inline int ls_decode_line(MJpegDecodeContext *s, void *lastv, void *dstv, int last2,
                                 int w, int point_transform, int is_uint8){
    int i, x, y;

    for(x=0; x < w; x++){
        int l, t, lt, rt;
    
        t= R(last, 0);
        if(x){
            l = t;
            lt= last2;
        }else{
            l = R(dst, x-1);
            lt= R(last, x-1);
        }

        if(x<w-1) rt= R(last, x+1);
        else      rt= t;
        
        hr_gradient= rt - t;
        hl_gradient= t - lt;
         v_gradient= lt - l;
            
        context= quantize(s, v_gradient) + 9*(quantize(s, hl_gradient) + 9*quantize(s, hr_gradient));

        if(context){
            int pred= mid_pred(l, l + t - lt, t);

            if(context < 0){
                context= -context;
                sign= 1;
                pred= clip(0, pred - state->bias, maxval);
            }else{
                sign= 0;
                pred= clip(0, pred + state->bias, maxval);
            }

            i= state->count;
            k=0;
            while(i < state->error_sum){ //FIXME optimize
                k++;
                i += i;
            }
            
            v= get_ur_golomb_jpegls(gb, k, LIMIT-qbpp, qbpp);
#if 1
    v++;
    if(v&1) v=  (v>>1);
    else    v= -(v>>1);

    if(k==0 && 2*state->drift <= - state->count) v ^= (-1);
#else
    v ^= (k==0 && 2*state->drift <= - state->count);
    v++;
    if(v&1) v=  (v>>1);
    else    v= -(v>>1);

#endif
            update_vlc_state(state, v, half_count);
            
            if(sign) v= -v;
            
            if(is_uint8) ((uint8_t *)dst)[x]= (pred + v) & maxval;
            else         ((uint16_t*)dst)[x]= (pred + v) & maxval;
        }else{
            int run_count;

            while(get_bits1(&s->gb)){
                run_count = 1<<log2_run[run_index];
                if(x + run_count > w) run_count= w - x;
                else                  run_index++;
                
                for(; run_count; run_count--){
                    if(is_uint8) ((uint8_t *)dst)[x++]= l;
                    else         ((uint16_t*)dst)[x++]= l;
                }
                
                if(x >= w) return 0; 
            }
            
            run_count= get_bits(&s->gb, log2_run[run_index]);

            for(; run_count; run_count--){
                if(is_uint8) ((uint8_t *)dst)[x++]= l;
                else         ((uint16_t*)dst)[x++]= l;
            }
            
            if(run_index) run_index--;
            
            if(x >= w) return 0;

            t= R(last, 0);
            
            RItype= (l==t);
            if(l==t){
                state= 366;
                temp= state->error_sum + (state->count>>1);
            }else{
                state= 365;
                temp= state->error_sum;
            }
            
            pred= t;
            sign= l > t;
            
            i= state->count;
            k=0;
            while(i < temp){ //FIXME optimize
                k++;
                i += i;
            }
            
            assert(Errval != 0);
            map = (k==0 && 2*Nn < state->count) == (Errval>0); 
            
            
                    if(run_count==0 && run_mode==1){
                        if(get_bits1(&s->gb)){
                            run_count = 1<<log2_run[run_index];
                            if(x + run_count <= w) run_index++;
                        }else{
                            if(log2_run[run_index]) run_count = get_bits(&s->gb, log2_run[run_index]);
                            else run_count=0;
                            if(run_index) run_index--;
                            run_mode=2;
                        }
                    }
                    run_count--;
                    if(run_count < 0){
                        run_mode=0;
                        run_count=0;
                        diff= get_vlc_symbol(&s->gb, &p->vlc_state[context]);
                        if(diff>=0) diff++;
                    }else
                        diff=0;
        
        }
    }

/*                if (s->restart_interval && !s->restart_count)
                    s->restart_count = s->restart_interval;*/

            if(mb_x==0 || mb_y==0 || s->interlaced){
                for(i=0;i<nb_components;i++) {
                    uint8_t *ptr;
                    int n, h, v, x, y, c, j, linesize;
                    n = s->nb_blocks[i];
                    c = s->comp_index[i];
                    h = s->h_scount[i];
                    v = s->v_scount[i];
                    x = 0;
                    y = 0;
                    linesize= s->linesize[c];
                    
                    for(j=0; j<n; j++) {
                        int pred;

                        ptr = s->current_picture[c] + (linesize * (v * mb_y + y)) + (h * mb_x + x); //FIXME optimize this crap
                        if(y==0 && mb_y==0){
                            if(x==0 && mb_x==0){
                                pred= 128 << point_transform;
                            }else{
                                pred= ptr[-1];
                            }
                        }else{
                            if(x==0 && mb_x==0){
                                pred= ptr[-linesize];
                            }else{
                                PREDICT(pred, ptr[-linesize-1], ptr[-linesize], ptr[-1], predictor);
                            }
                        }
                        
                        if (s->interlaced && s->bottom_field)
                            ptr += linesize >> 1;
                        *ptr= pred + (mjpeg_decode_dc(s, s->dc_index[i]) << point_transform);

                        if (++x == h) {
                            x = 0;
                            y++;
                        }
                    }
                }
            }else{
                for(i=0;i<nb_components;i++) {
                    uint8_t *ptr;
                    int n, h, v, x, y, c, j, linesize;
                    n = s->nb_blocks[i];
                    c = s->comp_index[i];
                    h = s->h_scount[i];
                    v = s->v_scount[i];
                    x = 0;
                    y = 0;
                    linesize= s->linesize[c];
                    
                    for(j=0; j<n; j++) {
                        int pred;

                        ptr = s->current_picture[c] + (linesize * (v * mb_y + y)) + (h * mb_x + x); //FIXME optimize this crap
                        PREDICT(pred, ptr[-linesize-1], ptr[-linesize], ptr[-1], predictor);
                        *ptr= pred + (mjpeg_decode_dc(s, s->dc_index[i]) << point_transform);
                        if (++x == h) {
                            x = 0;
                            y++;
                        }
                    }
                }
            }
            if (s->restart_interval && !--s->restart_count) {
                align_get_bits(&s->gb);
                skip_bits(&s->gb, 16); /* skip RSTn */
            }
    return 0;
}
#endif

#ifdef CONFIG_ENCODERS
AVCodec jpegls_encoder = { //FIXME avoid MPV_* lossless jpeg shouldnt need them
    "jpegls",
    CODEC_TYPE_VIDEO,
    CODEC_ID_JPEGLS,
    sizeof(MpegEncContext),
    MPV_encode_init,
    encode_picture_ls,
    MPV_encode_end,
};
#endif
