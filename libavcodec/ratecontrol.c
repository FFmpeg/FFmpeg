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
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

#define STATS_FILE "lavc_stats.txt"

static int init_pass2(MpegEncContext *s);

void ff_write_pass1_stats(MpegEncContext *s){
    RateControlContext *rcc= &s->rc_context;
//    fprintf(c->stats_file, "type:%d q:%d icount:%d pcount:%d scount:%d itex:%d ptex%d mv:%d misc:%d fcode:%d bcode:%d\")
    fprintf(rcc->stats_file, "in:%d out:%d type:%d q:%d itex:%d ptex:%d mv:%d misc:%d fcode:%d bcode:%d\n",
            s->picture_number, s->input_picture_number - s->max_b_frames, s->pict_type, 
            s->qscale, s->i_tex_bits, s->p_tex_bits, s->mv_bits, s->misc_bits, s->f_code, s->b_code);
}

int ff_rate_control_init(MpegEncContext *s)
{
    RateControlContext *rcc= &s->rc_context;
    emms_c();

    if(s->flags&CODEC_FLAG_PASS1){
        rcc->stats_file= fopen(STATS_FILE, "w");
        if(!rcc->stats_file){
            fprintf(stderr, "failed to open " STATS_FILE "\n");
            return -1;
        }
    } else if(s->flags&CODEC_FLAG_PASS2){
        int size;
        int i;

        rcc->stats_file= fopen(STATS_FILE, "r");
        if(!rcc->stats_file){
            fprintf(stderr, "failed to open " STATS_FILE "\n");
            return -1;
        }

        /* find number of pics without reading the file twice :) */
        fseek(rcc->stats_file, 0, SEEK_END);
        size= ftell(rcc->stats_file);
        fseek(rcc->stats_file, 0, SEEK_SET);

        size/= 64; // we need at least 64 byte to store a line ...
        rcc->entry = (RateControlEntry*)av_mallocz(size*sizeof(RateControlEntry));

        for(i=0; !feof(rcc->stats_file); i++){
            RateControlEntry *rce;
            int picture_number;
            int e;
            
            e= fscanf(rcc->stats_file, "in:%d ", &picture_number);
            rce= &rcc->entry[picture_number];
            e+=fscanf(rcc->stats_file, "out:%*d type:%d q:%d itex:%d ptex:%d mv:%d misc:%d fcode:%*d bcode:%*d\n",
                   &rce->pict_type, &rce->qscale, &rce->i_tex_bits, &rce->p_tex_bits, &rce->mv_bits, &rce->misc_bits);
            if(e!=7){
                fprintf(stderr, STATS_FILE " is damaged\n");
                return -1;
            }
        }
        rcc->num_entries= i;
        
        if(init_pass2(s) < 0) return -1;
    }
     
    /* no 2pass stuff, just normal 1-pass */
    //initial values, they dont really matter as they will be totally different within a few frames
    s->i_pred.coeff= s->p_pred.coeff= 7.0;
    s->i_pred.count= s->p_pred.count= 1.0;
    
    s->i_pred.decay= s->p_pred.decay= 0.4;
    
    // use more bits at the beginning, otherwise high motion at the begin will look like shit
    s->qsum=100 * s->qmin;
    s->qcount=100;

    s->short_term_qsum=0.001;
    s->short_term_qcount=0.001;

    return 0;
}

void ff_rate_control_uninit(MpegEncContext *s)
{
    RateControlContext *rcc= &s->rc_context;
    emms_c();

    if(rcc->stats_file) 
        fclose(rcc->stats_file);
    rcc->stats_file = NULL;
    av_freep(&rcc->entry);
}

//----------------------------------
// 1 Pass Code

static double predict(Predictor *p, double q, double var)
{
     return p->coeff*var / (q*p->count);
}

static void update_predictor(Predictor *p, double q, double var, double size)
{
    double new_coeff= size*q / (var + 1);
    if(var<1000) return;

    p->count*= p->decay;
    p->coeff*= p->decay;
    p->count++;
    p->coeff+= new_coeff;
}

int ff_rate_estimate_qscale(MpegEncContext *s)
{
    int qmin= s->qmin;
    int qmax= s->qmax;
    int rate_q=5;
    float q;
    int qscale;
    float br_compensation;
    double diff;
    double short_term_q;
    double long_term_q;
    double fps;
    int picture_number= s->input_picture_number - s->max_b_frames;
    int64_t wanted_bits;
    emms_c();

    fps= (double)s->frame_rate / FRAME_RATE_BASE;
    wanted_bits= (uint64_t)(s->bit_rate*(double)picture_number/fps);
//    printf("%d %d %d\n", picture_number, (int)wanted_bits, (int)s->total_bits);
    
    if(s->pict_type==B_TYPE){
        qmin= (int)(qmin*s->b_quant_factor+s->b_quant_offset + 0.5);
        qmax= (int)(qmax*s->b_quant_factor+s->b_quant_offset + 0.5);
    }
    if(qmin<1) qmin=1;
    if(qmax>31) qmax=31;
    if(qmax<=qmin) qmax= qmin;

        /* update predictors */
    if(picture_number>2){
        if(s->pict_type!=B_TYPE && s->last_non_b_pict_type == P_TYPE){
//printf("%d %d %d %f\n", s->qscale, s->last_mc_mb_var, s->frame_bits, s->p_pred.coeff);
            update_predictor(&s->p_pred, s->last_non_b_qscale, s->last_non_b_mc_mb_var, s->pb_frame_bits);
        }
    }

    if(s->pict_type == I_TYPE){
        short_term_q= s->short_term_qsum/s->short_term_qcount;
    
        long_term_q= s->qsum/s->qcount*(s->total_bits+1)/(wanted_bits+1); //+1 to avoid nan & 0

        q= 1/((1/long_term_q - 1/short_term_q)*s->qcompress + 1/short_term_q);
    }else if(s->pict_type==B_TYPE){
        q= (int)(s->last_non_b_qscale*s->b_quant_factor+s->b_quant_offset + 0.5);
    }else{ //P Frame
        int i;
        int diff, best_diff=1000000000;
        for(i=1; i<=31; i++){
            diff= predict(&s->p_pred, i, s->mc_mb_var_sum) - (double)s->bit_rate/fps;
            if(diff<0) diff= -diff;
            if(diff<best_diff){
                best_diff= diff;
                rate_q= i;
            }
        }
        s->short_term_qsum*=s->qblur;
        s->short_term_qcount*=s->qblur;

        s->short_term_qsum+= rate_q;
        s->short_term_qcount++;
        short_term_q= s->short_term_qsum/s->short_term_qcount;
    
        long_term_q= s->qsum/s->qcount*(s->total_bits+1)/(wanted_bits+1); //+1 to avoid nan & 0

//    q= (long_term_q - short_term_q)*s->qcompress + short_term_q;
        q= 1/((1/long_term_q - 1/short_term_q)*s->qcompress + 1/short_term_q);
    }

    diff= s->total_bits - wanted_bits;
    br_compensation= (s->bit_rate_tolerance - diff)/s->bit_rate_tolerance;
    if(br_compensation<=0.0) br_compensation=0.001;
    q/=br_compensation;
//printf("%f %f %f\n", q, br_compensation, short_term_q);
    qscale= (int)(q + 0.5);
    if     (qscale<qmin) qscale=qmin;
    else if(qscale>qmax) qscale=qmax;
    
    if(s->pict_type!=B_TYPE){
        s->qsum+= qscale;
        s->qcount++;
        if     (qscale<s->last_non_b_qscale-s->max_qdiff) qscale=s->last_non_b_qscale-s->max_qdiff;
        else if(qscale>s->last_non_b_qscale+s->max_qdiff) qscale=s->last_non_b_qscale+s->max_qdiff;
    }
//printf("q:%d diff:%d comp:%f rate_q:%d st_q:%f fvar:%d last_size:%d\n", qscale, (int)diff, br_compensation, 
//       rate_q, short_term_q, s->mc_mb_var, s->frame_bits);
//printf("%d %d\n", s->bit_rate, (int)fps);
    return qscale;
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
    int num_frames[5]={0,0,0,0,0};
    double rate_factor=0;
    double step;
    int last_i_frame=-10000000;

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

        complexity[rce->new_pict_type]+= (rce->i_tex_bits+ rce->p_tex_bits)*(double)rce->qscale;
        const_bits[rce->new_pict_type]+= rce->mv_bits + rce->misc_bits;
        num_frames[rce->new_pict_type]++;
    }
    all_const_bits= const_bits[I_TYPE] + const_bits[P_TYPE] + const_bits[B_TYPE];
    
    if(all_available_bits < all_const_bits){
        fprintf(stderr, "requested bitrate is to low\n");
        return -1;
    }

//    avg_complexity= complexity/rcc->num_entries;
    avg_quantizer[P_TYPE]= 
    avg_quantizer[I_TYPE]=   (complexity[I_TYPE]+complexity[P_TYPE] + complexity[B_TYPE]/s->b_quant_factor) 
                           / (all_available_bits - all_const_bits);
    avg_quantizer[B_TYPE]= avg_quantizer[P_TYPE]*s->b_quant_factor + s->b_quant_offset;
//printf("avg quantizer: %f %f\n", avg_quantizer[P_TYPE], avg_quantizer[B_TYPE]);

    for(i=0; i<5; i++){
        available_bits[i]= const_bits[i] + complexity[i]/avg_quantizer[i];
    }
//printf("%lld %lld %lld %lld\n", available_bits[I_TYPE], available_bits[P_TYPE], available_bits[B_TYPE], all_available_bits);
    
    for(step=256*256; step>0.0000001; step*=0.5){
        uint64_t expected_bits=0;
        rate_factor+= step;
        /* find qscale */
        for(i=0; i<rcc->num_entries; i++){
            RateControlEntry *rce= &rcc->entry[i];
            double short_term_q, q, bits_left;
            const int pict_type= rce->new_pict_type;
            int qmin= s->qmin;
            int qmax= s->qmax;

            if(pict_type==B_TYPE){
                qmin= (int)(qmin*s->b_quant_factor+s->b_quant_offset + 0.5);
                qmax= (int)(qmax*s->b_quant_factor+s->b_quant_offset + 0.5);
            }
            if(qmin<1) qmin=1;
            if(qmax>31) qmax=31;
            if(qmax<=qmin) qmax= qmin;
            
            switch(s->rc_strategy){
            case 0:
                bits_left= available_bits[pict_type]/num_frames[pict_type]*rate_factor - rce->misc_bits - rce->mv_bits;
                if(bits_left<1.0) bits_left=1.0;
                short_term_q= rce->qscale*(rce->i_tex_bits + rce->p_tex_bits)/bits_left;
                break;
            case 1:
                bits_left= (available_bits[pict_type] - const_bits[pict_type])/num_frames[pict_type]*rate_factor;
                if(bits_left<1.0) bits_left=1.0;
                short_term_q= rce->qscale*(rce->i_tex_bits + rce->p_tex_bits)/bits_left;
                break;
            case 2:
                bits_left= available_bits[pict_type]/num_frames[pict_type]*rate_factor;
                if(bits_left<1.0) bits_left=1.0;
                short_term_q= rce->qscale*(rce->i_tex_bits + rce->p_tex_bits + rce->misc_bits + rce->mv_bits)/bits_left;
                break;
            default:
                fprintf(stderr, "unknown strategy\n");
                short_term_q=3; //gcc warning fix
            }

            if(short_term_q>31.0) short_term_q=31.0;
            else if (short_term_q<1.0) short_term_q=1.0;

            q= 1/((1/avg_quantizer[pict_type] - 1/short_term_q)*s->qcompress + 1/short_term_q);
            if     (q<qmin) q=qmin;
            else if(q>qmax) q=qmax;
//printf("lq:%f, sq:%f t:%f q:%f\n", avg_quantizer[rce->pict_type], short_term_q, bits_left, q);
            rce->new_qscale= q;
        }

        /* smooth curve */
    
        /* find expected bits */
        for(i=0; i<rcc->num_entries; i++){
            RateControlEntry *rce= &rcc->entry[i];
            double factor= rce->qscale / rce->new_qscale;
            
            rce->expected_bits= expected_bits;
            expected_bits += (int)(rce->misc_bits + rce->mv_bits + (rce->i_tex_bits + rce->p_tex_bits)*factor + 0.5);
        }

//        printf("%d %d %f\n", (int)expected_bits, (int)all_available_bits, rate_factor);
        if(expected_bits > all_available_bits) rate_factor-= step;
    }

    return 0;
}

int ff_rate_estimate_qscale_pass2(MpegEncContext *s)
{
    int qmin= s->qmin;
    int qmax= s->qmax;
    float q;
    int qscale;
    float br_compensation;
    double diff;
    int picture_number= s->picture_number;
    RateControlEntry *rce= &s->rc_context.entry[picture_number];
    int64_t wanted_bits= rce->expected_bits;
    emms_c();

//    printf("%d %d %d\n", picture_number, (int)wanted_bits, (int)s->total_bits);
    
    if(s->pict_type==B_TYPE){
        qmin= (int)(qmin*s->b_quant_factor+s->b_quant_offset + 0.5);
        qmax= (int)(qmax*s->b_quant_factor+s->b_quant_offset + 0.5);
    }
    if(qmin<1) qmin=1;
    if(qmax>31) qmax=31;
    if(qmax<=qmin) qmax= qmin;

    q= rce->new_qscale;

    diff= s->total_bits - wanted_bits;
    br_compensation= (s->bit_rate_tolerance - diff)/s->bit_rate_tolerance;
    if(br_compensation<=0.0) br_compensation=0.001;
    q/=br_compensation;

    qscale= (int)(q + 0.5);
    if     (qscale<qmin) qscale=qmin;
    else if(qscale>qmax) qscale=qmax;
//    printf("%d %d %d %d type:%d\n", qmin, qscale, qmax, picture_number, s->pict_type); fflush(stdout);
    return qscale;
}
