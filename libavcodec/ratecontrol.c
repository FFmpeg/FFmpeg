/*
 * Rate control for video encoders
 *
 * Copyright (c) 2002 Michael Niedermayer <michaelni@gmx.at>
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
#include <math.h>
#include "common.h"
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

#undef NDEBUG // allways check asserts, the speed effect is far too small to disable them
#include <assert.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef M_E
#define M_E 2.718281828
#endif

static int init_pass2(MpegEncContext *s);
static double get_qscale(MpegEncContext *s, RateControlEntry *rce, double rate_factor, int frame_num);

void ff_write_pass1_stats(MpegEncContext *s){
    sprintf(s->avctx->stats_out, "in:%d out:%d type:%d q:%f itex:%d ptex:%d mv:%d misc:%d fcode:%d bcode:%d mc-var:%d var:%d icount:%d;\n",
            s->picture_number, s->input_picture_number - s->max_b_frames, s->pict_type, 
            s->frame_qscale, s->i_tex_bits, s->p_tex_bits, s->mv_bits, s->misc_bits, 
            s->f_code, s->b_code, s->mc_mb_var_sum, s->mb_var_sum, s->i_count);
}

int ff_rate_control_init(MpegEncContext *s)
{
    RateControlContext *rcc= &s->rc_context;
    int i;
    emms_c();

    for(i=0; i<5; i++){
        rcc->pred[i].coeff= 7.0;
        rcc->pred[i].count= 1.0;
    
        rcc->pred[i].decay= 0.4;
        rcc->i_cplx_sum [i]=
        rcc->p_cplx_sum [i]=
        rcc->mv_bits_sum[i]=
        rcc->qscale_sum [i]=
        rcc->frame_count[i]= 1; // 1 is better cuz of 1/0 and such
        rcc->last_qscale_for[i]=5;
    }
    rcc->buffer_index= s->avctx->rc_buffer_size/2;

    if(s->flags&CODEC_FLAG_PASS2){
        int i;
        char *p;

        /* find number of pics */
        p= s->avctx->stats_in;
        for(i=-1; p; i++){
            p= strchr(p+1, ';');
        }
        i+= s->max_b_frames;
        rcc->entry = (RateControlEntry*)av_mallocz(i*sizeof(RateControlEntry));
        rcc->num_entries= i;
        
        /* init all to skiped p frames (with b frames we might have a not encoded frame at the end FIXME) */
        for(i=0; i<rcc->num_entries; i++){
            RateControlEntry *rce= &rcc->entry[i];
            rce->pict_type= rce->new_pict_type=P_TYPE;
            rce->qscale= rce->new_qscale=2;
            rce->misc_bits= s->mb_num + 10;
            rce->mb_var_sum= s->mb_num*100;
        }        
        
        /* read stats */
        p= s->avctx->stats_in;
        for(i=0; i<rcc->num_entries - s->max_b_frames; i++){
            RateControlEntry *rce;
            int picture_number;
            int e;
            char *next;

            next= strchr(p, ';');
            if(next){
                (*next)=0; //sscanf in unbelieavle slow on looong strings //FIXME copy / dont write
                next++;
            }
            e= sscanf(p, " in:%d ", &picture_number);

            assert(picture_number >= 0);
            assert(picture_number < rcc->num_entries);
            rce= &rcc->entry[picture_number];

            e+=sscanf(p, " in:%*d out:%*d type:%d q:%f itex:%d ptex:%d mv:%d misc:%d fcode:%d bcode:%d mc-var:%d var:%d icount:%d",
                   &rce->pict_type, &rce->qscale, &rce->i_tex_bits, &rce->p_tex_bits, &rce->mv_bits, &rce->misc_bits, 
                   &rce->f_code, &rce->b_code, &rce->mc_mb_var_sum, &rce->mb_var_sum, &rce->i_count);
            if(e!=12){
                fprintf(stderr, "statistics are damaged at line %d, parser out=%d\n", i, e);
                return -1;
            }
            p= next;
        }
        
        if(init_pass2(s) < 0) return -1;
    }
     
    if(!(s->flags&CODEC_FLAG_PASS2)){

        rcc->short_term_qsum=0.001;
        rcc->short_term_qcount=0.001;
    
        rcc->pass1_rc_eq_output_sum= 0.001;
        rcc->pass1_wanted_bits=0.001;
        
        /* init stuff with the user specified complexity */
        if(s->avctx->rc_initial_cplx){
            for(i=0; i<60*30; i++){
                double bits= s->avctx->rc_initial_cplx * (i/10000.0 + 1.0)*s->mb_num;
                RateControlEntry rce;
                double q;
                
                if     (i%((s->gop_size+3)/4)==0) rce.pict_type= I_TYPE;
                else if(i%(s->max_b_frames+1))    rce.pict_type= B_TYPE;
                else                              rce.pict_type= P_TYPE;

                rce.new_pict_type= rce.pict_type;
                rce.mc_mb_var_sum= bits*s->mb_num/100000;
                rce.mb_var_sum   = s->mb_num;
                rce.qscale   = 2;
                rce.f_code   = 2;
                rce.b_code   = 1;
                rce.misc_bits= 1;

                if(s->pict_type== I_TYPE){
                    rce.i_count   = s->mb_num;
                    rce.i_tex_bits= bits;
                    rce.p_tex_bits= 0;
                    rce.mv_bits= 0;
                }else{
                    rce.i_count   = 0; //FIXME we do know this approx
                    rce.i_tex_bits= 0;
                    rce.p_tex_bits= bits*0.9;
                    rce.mv_bits= bits*0.1;
                }
                rcc->i_cplx_sum [rce.pict_type] += rce.i_tex_bits*rce.qscale;
                rcc->p_cplx_sum [rce.pict_type] += rce.p_tex_bits*rce.qscale;
                rcc->mv_bits_sum[rce.pict_type] += rce.mv_bits;
                rcc->frame_count[rce.pict_type] ++;

                bits= rce.i_tex_bits + rce.p_tex_bits;

                q= get_qscale(s, &rce, rcc->pass1_wanted_bits/rcc->pass1_rc_eq_output_sum, i);
                rcc->pass1_wanted_bits+= s->bit_rate/(s->frame_rate / (double)FRAME_RATE_BASE);
            }
        }

    }
    
    return 0;
}

void ff_rate_control_uninit(MpegEncContext *s)
{
    RateControlContext *rcc= &s->rc_context;
    emms_c();

    av_freep(&rcc->entry);
}

static inline double qp2bits(RateControlEntry *rce, double qp){
    if(qp<=0.0){
        fprintf(stderr, "qp<=0.0\n");
    }
    return rce->qscale * (double)(rce->i_tex_bits + rce->p_tex_bits+1)/ qp;
}

static inline double bits2qp(RateControlEntry *rce, double bits){
    if(bits<0.9){
        fprintf(stderr, "bits<0.9\n");
    }
    return rce->qscale * (double)(rce->i_tex_bits + rce->p_tex_bits+1)/ bits;
}
    
static void update_rc_buffer(MpegEncContext *s, int frame_size){
    RateControlContext *rcc= &s->rc_context;
    const double fps= (double)s->frame_rate / FRAME_RATE_BASE;
    const double buffer_size= s->avctx->rc_buffer_size;
    const double min_rate= s->avctx->rc_min_rate/fps;
    const double max_rate= s->avctx->rc_max_rate/fps;

    if(buffer_size){
        rcc->buffer_index-= frame_size;
        if(rcc->buffer_index < buffer_size/2 /*FIXME /2 */ || min_rate==0){
            rcc->buffer_index+= max_rate;
            if(rcc->buffer_index >= buffer_size)
                rcc->buffer_index= buffer_size-1;
        }else{
            rcc->buffer_index+= min_rate;
        }
        
        if(rcc->buffer_index < 0)
            fprintf(stderr, "rc buffer underflow\n");
        if(rcc->buffer_index >= s->avctx->rc_buffer_size)
            fprintf(stderr, "rc buffer overflow\n");
    }
}

/**
 * modifies the bitrate curve from pass1 for one frame
 */
static double get_qscale(MpegEncContext *s, RateControlEntry *rce, double rate_factor, int frame_num){
    RateControlContext *rcc= &s->rc_context;
    double q, bits;
    const int pict_type= rce->new_pict_type;
    const double mb_num= s->mb_num;  
    int i;

    double const_values[]={
        M_PI,
        M_E,
        rce->i_tex_bits*rce->qscale,
        rce->p_tex_bits*rce->qscale,
        (rce->i_tex_bits + rce->p_tex_bits)*(double)rce->qscale,
        rce->mv_bits/mb_num,
        rce->pict_type == B_TYPE ? (rce->f_code + rce->b_code)*0.5 : rce->f_code,
        rce->i_count/mb_num,
        rce->mc_mb_var_sum/mb_num,
        rce->mb_var_sum/mb_num,
        rce->pict_type == I_TYPE,
        rce->pict_type == P_TYPE,
        rce->pict_type == B_TYPE,
        rcc->qscale_sum[pict_type] / (double)rcc->frame_count[pict_type],
        s->qcompress,
/*        rcc->last_qscale_for[I_TYPE],
        rcc->last_qscale_for[P_TYPE],
        rcc->last_qscale_for[B_TYPE],
        rcc->next_non_b_qscale,*/
        rcc->i_cplx_sum[I_TYPE] / (double)rcc->frame_count[I_TYPE],
        rcc->i_cplx_sum[P_TYPE] / (double)rcc->frame_count[P_TYPE],
        rcc->p_cplx_sum[P_TYPE] / (double)rcc->frame_count[P_TYPE],
        rcc->p_cplx_sum[B_TYPE] / (double)rcc->frame_count[B_TYPE],
        (rcc->i_cplx_sum[pict_type] + rcc->p_cplx_sum[pict_type]) / (double)rcc->frame_count[pict_type],
        0
    };
    char *const_names[]={
        "PI",
        "E",
        "iTex",
        "pTex",
        "tex",
        "mv",
        "fCode",
        "iCount",
        "mcVar",
        "var",
        "isI",
        "isP",
        "isB",
        "avgQP",
        "qComp",
/*        "lastIQP",
        "lastPQP",
        "lastBQP",
        "nextNonBQP",*/
        "avgIITex",
        "avgPITex",
        "avgPPTex",
        "avgBPTex",
        "avgTex",
        NULL
    };
    static double (*func1[])(void *, double)={
        (void *)bits2qp,
        (void *)qp2bits,
        NULL
    };
    char *func1_names[]={
        "bits2qp",
        "qp2bits",
        NULL
    };

    bits= ff_eval(s->avctx->rc_eq, const_values, const_names, func1, func1_names, NULL, NULL, rce);
    
    rcc->pass1_rc_eq_output_sum+= bits;
    bits*=rate_factor;
    if(bits<0.0) bits=0.0;
    bits+= 1.0; //avoid 1/0 issues
    
    /* user override */
    for(i=0; i<s->avctx->rc_override_count; i++){
        RcOverride *rco= s->avctx->rc_override;
        if(rco[i].start_frame > frame_num) continue;
        if(rco[i].end_frame   < frame_num) continue;
    
        if(rco[i].qscale) 
            bits= qp2bits(rce, rco[i].qscale); //FIXME move at end to really force it?
        else
            bits*= rco[i].quality_factor;
    }

    q= bits2qp(rce, bits);
    
    /* I/B difference */
    if     (pict_type==I_TYPE && s->avctx->i_quant_factor<0.0)
        q= -q*s->avctx->i_quant_factor + s->avctx->i_quant_offset;
    else if(pict_type==B_TYPE && s->avctx->b_quant_factor<0.0)
        q= -q*s->avctx->b_quant_factor + s->avctx->b_quant_offset;
        
    return q;
}

static double get_diff_limited_q(MpegEncContext *s, RateControlEntry *rce, double q){
    RateControlContext *rcc= &s->rc_context;
    AVCodecContext *a= s->avctx;
    const int pict_type= rce->new_pict_type;
    const double last_p_q    = rcc->last_qscale_for[P_TYPE];
    const double last_non_b_q= rcc->last_qscale_for[rcc->last_non_b_pict_type];

    if     (pict_type==I_TYPE && (a->i_quant_factor>0.0 || rcc->last_non_b_pict_type==P_TYPE))
        q= last_p_q    *ABS(a->i_quant_factor) + a->i_quant_offset;
    else if(pict_type==B_TYPE && a->b_quant_factor>0.0)
        q= last_non_b_q*    a->b_quant_factor  + a->b_quant_offset;

    /* last qscale / qdiff stuff */
    if(rcc->last_non_b_pict_type==pict_type || pict_type!=I_TYPE){
        double last_q= rcc->last_qscale_for[pict_type];
        if     (q > last_q + a->max_qdiff) q= last_q + a->max_qdiff;
        else if(q < last_q - a->max_qdiff) q= last_q - a->max_qdiff;
    }

    rcc->last_qscale_for[pict_type]= q; //Note we cant do that after blurring
    
    if(pict_type!=B_TYPE)
        rcc->last_non_b_pict_type= pict_type;

    return q;
}

/**
 * gets the qmin & qmax for pict_type
 */
static void get_qminmax(int *qmin_ret, int *qmax_ret, MpegEncContext *s, int pict_type){
    int qmin= s->qmin;                                                       
    int qmax= s->qmax;

    if(pict_type==B_TYPE){
        qmin= (int)(qmin*ABS(s->avctx->b_quant_factor)+s->avctx->b_quant_offset + 0.5);
        qmax= (int)(qmax*ABS(s->avctx->b_quant_factor)+s->avctx->b_quant_offset + 0.5);
    }else if(pict_type==I_TYPE){
        qmin= (int)(qmin*ABS(s->avctx->i_quant_factor)+s->avctx->i_quant_offset + 0.5);
        qmax= (int)(qmax*ABS(s->avctx->i_quant_factor)+s->avctx->i_quant_offset + 0.5);
    }

    if(qmin<1) qmin=1;
    if(qmin==1 && s->qmin>1) qmin=2; //avoid qmin=1 unless the user wants qmin=1

    if(qmin<3 && s->max_qcoeff<=128 && pict_type==I_TYPE) qmin=3; //reduce cliping problems

    if(qmax>31) qmax=31;
    if(qmax<=qmin) qmax= qmin= (qmax+qmin+1)>>1;
    
    *qmin_ret= qmin;
    *qmax_ret= qmax;
}

static double modify_qscale(MpegEncContext *s, RateControlEntry *rce, double q, int frame_num){
    RateControlContext *rcc= &s->rc_context;
    int qmin, qmax;
    double bits;
    const int pict_type= rce->new_pict_type;
    const double buffer_size= s->avctx->rc_buffer_size;
    const double min_rate= s->avctx->rc_min_rate;
    const double max_rate= s->avctx->rc_max_rate;
    
    get_qminmax(&qmin, &qmax, s, pict_type);

    /* modulation */
    if(s->avctx->rc_qmod_freq && frame_num%s->avctx->rc_qmod_freq==0 && pict_type==P_TYPE)
        q*= s->avctx->rc_qmod_amp;

    bits= qp2bits(rce, q);
//printf("q:%f\n", q);
    /* buffer overflow/underflow protection */
    if(buffer_size){
        double expected_size= rcc->buffer_index;

        if(min_rate){
            double d= 2*(buffer_size - expected_size)/buffer_size;
            if(d>1.0) d=1.0;
            else if(d<0.0001) d=0.0001;
            q*= pow(d, 1.0/s->avctx->rc_buffer_aggressivity);

            q= FFMIN(q, bits2qp(rce, FFMAX((min_rate - buffer_size + rcc->buffer_index)*2, 1)));
        }

        if(max_rate){
            double d= 2*expected_size/buffer_size;
            if(d>1.0) d=1.0;
            else if(d<0.0001) d=0.0001;
            q/= pow(d, 1.0/s->avctx->rc_buffer_aggressivity);

            q= FFMAX(q, bits2qp(rce, FFMAX(rcc->buffer_index/2, 1)));
        }
    }
//printf("q:%f max:%f min:%f size:%f index:%d bits:%f agr:%f\n", q,max_rate, min_rate, buffer_size, rcc->buffer_index, bits, s->avctx->rc_buffer_aggressivity);
    if(s->avctx->rc_qsquish==0.0 || qmin==qmax){
        if     (q<qmin) q=qmin;
        else if(q>qmax) q=qmax;
    }else{
        double min2= log(qmin);
        double max2= log(qmax);
        
        q= log(q);
        q= (q - min2)/(max2-min2) - 0.5;
        q*= -4.0;
        q= 1.0/(1.0 + exp(q));
        q= q*(max2-min2) + min2;
        
        q= exp(q);
    }
    
    return q;
}

//----------------------------------
// 1 Pass Code

static double predict_size(Predictor *p, double q, double var)
{
     return p->coeff*var / (q*p->count);
}

static double predict_qp(Predictor *p, double size, double var)
{
//printf("coeff:%f, count:%f, var:%f, size:%f//\n", p->coeff, p->count, var, size);
     return p->coeff*var / (size*p->count);
}

static void update_predictor(Predictor *p, double q, double var, double size)
{
    double new_coeff= size*q / (var + 1);
    if(var<10) return;

    p->count*= p->decay;
    p->coeff*= p->decay;
    p->count++;
    p->coeff+= new_coeff;
}

static void adaptive_quantization(MpegEncContext *s, double q){
    int i;
    const float lumi_masking= s->avctx->lumi_masking / (128.0*128.0);
    const float dark_masking= s->avctx->dark_masking / (128.0*128.0);
    const float temp_cplx_masking= s->avctx->temporal_cplx_masking;
    const float spatial_cplx_masking = s->avctx->spatial_cplx_masking;
    const float p_masking = s->avctx->p_masking;
    float bits_sum= 0.0;
    float cplx_sum= 0.0;
    float cplx_tab[s->mb_num];
    float bits_tab[s->mb_num];
    const int qmin= 2; //s->avctx->mb_qmin;
    const int qmax= 31; //s->avctx->mb_qmax;
    
    for(i=0; i<s->mb_num; i++){
        float temp_cplx= sqrt(s->mc_mb_var[i]);
        float spat_cplx= sqrt(s->mb_var[i]);
        const int lumi= s->mb_mean[i];
        float bits, cplx, factor;
        
        if(spat_cplx < q/3) spat_cplx= q/3; //FIXME finetune
        if(temp_cplx < q/3) temp_cplx= q/3; //FIXME finetune
        
        if((s->mb_type[i]&MB_TYPE_INTRA)){//FIXME hq mode 
            cplx= spat_cplx;
            factor= 1.0 + p_masking;
        }else{
            cplx= temp_cplx;
            factor= pow(temp_cplx, - temp_cplx_masking);
        }
        factor*=pow(spat_cplx, - spatial_cplx_masking);

        if(lumi>127)
            factor*= (1.0 - (lumi-128)*(lumi-128)*lumi_masking);
        else
            factor*= (1.0 - (lumi-128)*(lumi-128)*dark_masking);
        
        if(factor<0.00001) factor= 0.00001;
        
        bits= cplx*factor;
        cplx_sum+= cplx;
        bits_sum+= bits;
        cplx_tab[i]= cplx;
        bits_tab[i]= bits;
    }

    /* handle qmin/qmax cliping */
    if(s->flags&CODEC_FLAG_NORMALIZE_AQP){
        for(i=0; i<s->mb_num; i++){
            float newq= q*cplx_tab[i]/bits_tab[i];
            newq*= bits_sum/cplx_sum;

            if     (newq > qmax){
                bits_sum -= bits_tab[i];
                cplx_sum -= cplx_tab[i]*q/qmax;
            }
            else if(newq < qmin){
                bits_sum -= bits_tab[i];
                cplx_sum -= cplx_tab[i]*q/qmin;
            }
        }
    }
   
    for(i=0; i<s->mb_num; i++){
        float newq= q*cplx_tab[i]/bits_tab[i];
        int intq;

        if(s->flags&CODEC_FLAG_NORMALIZE_AQP){
            newq*= bits_sum/cplx_sum;
        }

        if(i && ABS(s->qscale_table[i-1] - newq)<0.75)
            intq= s->qscale_table[i-1];
        else
            intq= (int)(newq + 0.5);

        if     (intq > qmax) intq= qmax;
        else if(intq < qmin) intq= qmin;
//if(i%s->mb_width==0) printf("\n");
//printf("%2d%3d ", intq, ff_sqrt(s->mc_mb_var[i]));
        s->qscale_table[i]= intq;
    }
}

float ff_rate_estimate_qscale(MpegEncContext *s)
{
    float q;
    int qmin, qmax;
    float br_compensation;
    double diff;
    double short_term_q;
    double fps;
    int picture_number= s->picture_number;
    int64_t wanted_bits;
    RateControlContext *rcc= &s->rc_context;
    RateControlEntry local_rce, *rce;
    double bits;
    double rate_factor;
    int var;
    const int pict_type= s->pict_type;
    emms_c();

    get_qminmax(&qmin, &qmax, s, pict_type);

    fps= (double)s->frame_rate / FRAME_RATE_BASE;
//printf("input_pic_num:%d pic_num:%d frame_rate:%d\n", s->input_picture_number, s->picture_number, s->frame_rate);
        /* update predictors */
    if(picture_number>2){
        const int last_var= s->last_pict_type == I_TYPE ? rcc->last_mb_var_sum : rcc->last_mc_mb_var_sum;
        update_predictor(&rcc->pred[s->last_pict_type], rcc->last_qscale, sqrt(last_var), s->frame_bits);
    }

    if(s->flags&CODEC_FLAG_PASS2){
        assert(picture_number>=0);
        assert(picture_number<rcc->num_entries);
        rce= &rcc->entry[picture_number];
        wanted_bits= rce->expected_bits;
    }else{
        rce= &local_rce;
        wanted_bits= (uint64_t)(s->bit_rate*(double)picture_number/fps);
    }

    diff= s->total_bits - wanted_bits;
    br_compensation= (s->bit_rate_tolerance - diff)/s->bit_rate_tolerance;
    if(br_compensation<=0.0) br_compensation=0.001;

    var= pict_type == I_TYPE ? s->mb_var_sum : s->mc_mb_var_sum;
    
    if(s->flags&CODEC_FLAG_PASS2){
        if(pict_type!=I_TYPE)
            assert(pict_type == rce->new_pict_type);

        q= rce->new_qscale / br_compensation;
//printf("%f %f %f last:%d var:%d type:%d//\n", q, rce->new_qscale, br_compensation, s->frame_bits, var, pict_type);
    }else{
        rce->pict_type= 
        rce->new_pict_type= pict_type;
        rce->mc_mb_var_sum= s->mc_mb_var_sum;
        rce->mb_var_sum   = s->   mb_var_sum;
        rce->qscale   = 2;
        rce->f_code   = s->f_code;
        rce->b_code   = s->b_code;
        rce->misc_bits= 1;

        if(picture_number>0)
            update_rc_buffer(s, s->frame_bits);

        bits= predict_size(&rcc->pred[pict_type], rce->qscale, sqrt(var));
        if(pict_type== I_TYPE){
            rce->i_count   = s->mb_num;
            rce->i_tex_bits= bits;
            rce->p_tex_bits= 0;
            rce->mv_bits= 0;
        }else{
            rce->i_count   = 0; //FIXME we do know this approx
            rce->i_tex_bits= 0;
            rce->p_tex_bits= bits*0.9;
            
            rce->mv_bits= bits*0.1;
        }
        rcc->i_cplx_sum [pict_type] += rce->i_tex_bits*rce->qscale;
        rcc->p_cplx_sum [pict_type] += rce->p_tex_bits*rce->qscale;
        rcc->mv_bits_sum[pict_type] += rce->mv_bits;
        rcc->frame_count[pict_type] ++;

        bits= rce->i_tex_bits + rce->p_tex_bits;
        rate_factor= rcc->pass1_wanted_bits/rcc->pass1_rc_eq_output_sum * br_compensation;
    
        q= get_qscale(s, rce, rate_factor, picture_number);

        assert(q>0.0);
//printf("%f ", q);
        q= get_diff_limited_q(s, rce, q);
//printf("%f ", q);
        assert(q>0.0);

        if(pict_type==P_TYPE || s->intra_only){ //FIXME type dependant blur like in 2-pass
            rcc->short_term_qsum*=s->qblur;
            rcc->short_term_qcount*=s->qblur;

            rcc->short_term_qsum+= q;
            rcc->short_term_qcount++;
//printf("%f ", q);
            q= short_term_q= rcc->short_term_qsum/rcc->short_term_qcount;
//printf("%f ", q);
        }
        assert(q>0.0);
        
        q= modify_qscale(s, rce, q, picture_number);

        rcc->pass1_wanted_bits+= s->bit_rate/fps;

        assert(q>0.0);
    }
//printf("qmin:%d, qmax:%d, q:%f\n", qmin, qmax, q);
    

    if     (q<qmin) q=qmin; 
    else if(q>qmax) q=qmax;
        
//    printf("%f %d %d %d\n", q, picture_number, (int)wanted_bits, (int)s->total_bits);
    
//printf("%f %f %f\n", q, br_compensation, short_term_q);
   
//printf("q:%d diff:%d comp:%f st_q:%f last_size:%d type:%d\n", qscale, (int)diff, br_compensation, 
//       short_term_q, s->frame_bits, pict_type);
//printf("%d %d\n", s->bit_rate, (int)fps);

    if(s->adaptive_quant)
        adaptive_quantization(s, q);
    else
        q= (int)(q + 0.5);
    
    rcc->last_qscale= q;
    rcc->last_mc_mb_var_sum= s->mc_mb_var_sum;
    rcc->last_mb_var_sum= s->mb_var_sum;
    return q;
}

//----------------------------------------------
// 2-Pass code

static int init_pass2(MpegEncContext *s)
{
    RateControlContext *rcc= &s->rc_context;
    int i;
    double fps= (double)s->frame_rate / FRAME_RATE_BASE;
    double complexity[5]={0,0,0,0,0};   // aproximate bits at quant=1
    double avg_quantizer[5];
    uint64_t const_bits[5]={0,0,0,0,0}; // quantizer idependant bits
    uint64_t available_bits[5];
    uint64_t all_const_bits;
    uint64_t all_available_bits= (uint64_t)(s->bit_rate*(double)rcc->num_entries/fps);
    double rate_factor=0;
    double step;
    int last_i_frame=-10000000;
    const int filter_size= (int)(s->qblur*4) | 1;  
    double expected_bits;
    double *qscale, *blured_qscale;

    /* find complexity & const_bits & decide the pict_types */
    for(i=0; i<rcc->num_entries; i++){
        RateControlEntry *rce= &rcc->entry[i];
        
        if(s->b_frame_strategy==0 || s->max_b_frames==0){
            rce->new_pict_type= rce->pict_type;
        }else{
            int j;
            int next_non_b_type=P_TYPE;

            switch(rce->pict_type){
            case I_TYPE:
                if(i-last_i_frame>s->gop_size/2){ //FIXME this is not optimal
                    rce->new_pict_type= I_TYPE;
                    last_i_frame= i;
                }else{
                    rce->new_pict_type= P_TYPE; // will be caught by the scene detection anyway
                }
                break;
            case P_TYPE:
                rce->new_pict_type= P_TYPE;
                break;
            case B_TYPE:
                for(j=i+1; j<i+s->max_b_frames+2 && j<rcc->num_entries; j++){
                    if(rcc->entry[j].pict_type != B_TYPE){
                        next_non_b_type= rcc->entry[j].pict_type;
                        break;
                    }
                }
                if(next_non_b_type==I_TYPE)
                    rce->new_pict_type= P_TYPE;
                else
                    rce->new_pict_type= B_TYPE;
                break;
            }
        }
        rcc->i_cplx_sum [rce->pict_type] += rce->i_tex_bits*rce->qscale;
        rcc->p_cplx_sum [rce->pict_type] += rce->p_tex_bits*rce->qscale;
        rcc->mv_bits_sum[rce->pict_type] += rce->mv_bits;
        rcc->frame_count[rce->pict_type] ++;

        complexity[rce->new_pict_type]+= (rce->i_tex_bits+ rce->p_tex_bits)*(double)rce->qscale;
        const_bits[rce->new_pict_type]+= rce->mv_bits + rce->misc_bits;
    }
    all_const_bits= const_bits[I_TYPE] + const_bits[P_TYPE] + const_bits[B_TYPE];
    
    if(all_available_bits < all_const_bits){
        fprintf(stderr, "requested bitrate is to low\n");
        return -1;
    }
    
    /* find average quantizers */
    avg_quantizer[P_TYPE]=0;
    for(step=256*256; step>0.0000001; step*=0.5){
        double expected_bits=0;
        avg_quantizer[P_TYPE]+= step;
        
        avg_quantizer[I_TYPE]= avg_quantizer[P_TYPE]*ABS(s->avctx->i_quant_factor) + s->avctx->i_quant_offset;
        avg_quantizer[B_TYPE]= avg_quantizer[P_TYPE]*ABS(s->avctx->b_quant_factor) + s->avctx->b_quant_offset;
        
        expected_bits= 
            + all_const_bits 
            + complexity[I_TYPE]/avg_quantizer[I_TYPE]
            + complexity[P_TYPE]/avg_quantizer[P_TYPE]
            + complexity[B_TYPE]/avg_quantizer[B_TYPE];
            
        if(expected_bits < all_available_bits) avg_quantizer[P_TYPE]-= step;
//printf("%f %lld %f\n", expected_bits, all_available_bits, avg_quantizer[P_TYPE]);
    }
//printf("qp_i:%f, qp_p:%f, qp_b:%f\n", avg_quantizer[I_TYPE],avg_quantizer[P_TYPE],avg_quantizer[B_TYPE]);

    for(i=0; i<5; i++){
        available_bits[i]= const_bits[i] + complexity[i]/avg_quantizer[i];
    }
//printf("%lld %lld %lld %lld\n", available_bits[I_TYPE], available_bits[P_TYPE], available_bits[B_TYPE], all_available_bits);
        
    qscale= malloc(sizeof(double)*rcc->num_entries);
    blured_qscale= malloc(sizeof(double)*rcc->num_entries);

    for(step=256*256; step>0.0000001; step*=0.5){
        expected_bits=0;
        rate_factor+= step;
        
        rcc->buffer_index= s->avctx->rc_buffer_size/2;

        /* find qscale */
        for(i=0; i<rcc->num_entries; i++){
            qscale[i]= get_qscale(s, &rcc->entry[i], rate_factor, i);
        }
        assert(filter_size%2==1);

        /* fixed I/B QP relative to P mode */
        for(i=rcc->num_entries-1; i>=0; i--){
            RateControlEntry *rce= &rcc->entry[i];
            
            qscale[i]= get_diff_limited_q(s, rce, qscale[i]);
        }

        /* smooth curve */
        for(i=0; i<rcc->num_entries; i++){
            RateControlEntry *rce= &rcc->entry[i];
            const int pict_type= rce->new_pict_type;
            int j;
            double q=0.0, sum=0.0;
        
            for(j=0; j<filter_size; j++){
                int index= i+j-filter_size/2;
                double d= index-i;
                double coeff= s->qblur==0 ? 1.0 : exp(-d*d/(s->qblur * s->qblur));
            
                if(index < 0 || index >= rcc->num_entries) continue;
                if(pict_type != rcc->entry[index].new_pict_type) continue;
                q+= qscale[index] * coeff;
                sum+= coeff;
            }
            blured_qscale[i]= q/sum;
        }
    
        /* find expected bits */
        for(i=0; i<rcc->num_entries; i++){
            RateControlEntry *rce= &rcc->entry[i];
            double bits;
            rce->new_qscale= modify_qscale(s, rce, blured_qscale[i], i);
            bits= qp2bits(rce, rce->new_qscale) + rce->mv_bits + rce->misc_bits;
//printf("%d %f\n", rce->new_bits, blured_qscale[i]);
            update_rc_buffer(s, bits);

            rce->expected_bits= expected_bits;
            expected_bits += bits;
        }

//        printf("%f %d %f\n", expected_bits, (int)all_available_bits, rate_factor);
        if(expected_bits > all_available_bits) rate_factor-= step;
    }
    free(qscale);
    free(blured_qscale);

    if(abs(expected_bits/all_available_bits - 1.0) > 0.01 ){
        fprintf(stderr, "Error: 2pass curve failed to converge\n");
        return -1;
    }

    return 0;
}
