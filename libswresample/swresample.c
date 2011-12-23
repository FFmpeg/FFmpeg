/*
 * Copyright (C) 2011 Michael Niedermayer (michaelni@gmx.at)
 *
 * This file is part of libswresample
 *
 * libswresample is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * libswresample is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libswresample; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/opt.h"
#include "swresample_internal.h"
#include "audioconvert.h"
#include "libavutil/avassert.h"
#include "libavutil/audioconvert.h"

#define  C30DB  M_SQRT2
#define  C15DB  1.189207115
#define C__0DB  1.0
#define C_15DB  0.840896415
#define C_30DB  M_SQRT1_2
#define C_45DB  0.594603558
#define C_60DB  0.5


//TODO split options array out?
#define OFFSET(x) offsetof(SwrContext,x)
static const AVOption options[]={
{"ich",  "input channel count", OFFSET( in.ch_count   ), AV_OPT_TYPE_INT, {.dbl=2}, 0, SWR_CH_MAX, 0},
{"och", "output channel count", OFFSET(out.ch_count   ), AV_OPT_TYPE_INT, {.dbl=2}, 0, SWR_CH_MAX, 0},
{"uch",   "used channel count", OFFSET(used_ch_count  ), AV_OPT_TYPE_INT, {.dbl=0}, 0, SWR_CH_MAX, 0},
{"isr",  "input sample rate"  , OFFSET( in_sample_rate), AV_OPT_TYPE_INT, {.dbl=48000}, 1, INT_MAX, 0},
{"osr", "output sample rate"  , OFFSET(out_sample_rate), AV_OPT_TYPE_INT, {.dbl=48000}, 1, INT_MAX, 0},
//{"ip" ,  "input planar"       , OFFSET( in.planar     ), AV_OPT_TYPE_INT, {.dbl=0},    0,       1, 0},
//{"op" , "output planar"       , OFFSET(out.planar     ), AV_OPT_TYPE_INT, {.dbl=0},    0,       1, 0},
{"isf",  "input sample format", OFFSET( in_sample_fmt ), AV_OPT_TYPE_INT, {.dbl=AV_SAMPLE_FMT_S16}, 0, AV_SAMPLE_FMT_NB-1+256, 0},
{"osf", "output sample format", OFFSET(out_sample_fmt ), AV_OPT_TYPE_INT, {.dbl=AV_SAMPLE_FMT_S16}, 0, AV_SAMPLE_FMT_NB-1+256, 0},
{"tsf", "internal sample format", OFFSET(int_sample_fmt ), AV_OPT_TYPE_INT, {.dbl=AV_SAMPLE_FMT_NONE}, -1, AV_SAMPLE_FMT_FLT, 0},
{"icl",  "input channel layout" , OFFSET( in_ch_layout), AV_OPT_TYPE_INT64, {.dbl=0}, 0, INT64_MAX, 0, "channel_layout"},
{"ocl",  "output channel layout", OFFSET(out_ch_layout), AV_OPT_TYPE_INT64, {.dbl=0}, 0, INT64_MAX, 0, "channel_layout"},
{"clev", "center mix level"     , OFFSET(clev)         , AV_OPT_TYPE_FLOAT, {.dbl=C_30DB}, 0, 4, 0},
{"slev", "sourround mix level"  , OFFSET(slev)         , AV_OPT_TYPE_FLOAT, {.dbl=C_30DB}, 0, 4, 0},
{"rmvol", "rematrix volume"     , OFFSET(rematrix_volume), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, -1000, 1000, 0},
{"flags", NULL                  , OFFSET(flags)        , AV_OPT_TYPE_FLAGS, {.dbl=0}, 0,  UINT_MAX, 0, "flags"},
{"res", "force resampling", 0, AV_OPT_TYPE_CONST, {.dbl=SWR_FLAG_RESAMPLE}, INT_MIN, INT_MAX, 0, "flags"},

{0}
};

static const char* context_to_name(void* ptr) {
    return "SWR";
}

static const AVClass av_class = {
    .class_name                = "SwrContext",
    .item_name                 = context_to_name,
    .option                    = options,
    .version                   = LIBAVUTIL_VERSION_INT,
    .log_level_offset_offset   = OFFSET(log_level_offset),
    .parent_log_context_offset = OFFSET(log_ctx),
};

unsigned swresample_version(void)
{
    av_assert0(LIBSWRESAMPLE_VERSION_MICRO >= 100);
    return LIBSWRESAMPLE_VERSION_INT;
}

const char *swresample_configuration(void)
{
    return FFMPEG_CONFIGURATION;
}

const char *swresample_license(void)
{
#define LICENSE_PREFIX "libswresample license: "
    return LICENSE_PREFIX FFMPEG_LICENSE + sizeof(LICENSE_PREFIX) - 1;
}

int swr_set_channel_mapping(struct SwrContext *s, const int *channel_map){
    if(!s || s->in_convert) // s needs to be allocated but not initialized
        return AVERROR(EINVAL);
    s->channel_map = channel_map;
    return 0;
}

struct SwrContext *swr_alloc(void){
    SwrContext *s= av_mallocz(sizeof(SwrContext));
    if(s){
        s->av_class= &av_class;
        av_opt_set_defaults(s);
    }
    return s;
}

struct SwrContext *swr_alloc_set_opts(struct SwrContext *s,
                                      int64_t out_ch_layout, enum AVSampleFormat out_sample_fmt, int out_sample_rate,
                                      int64_t  in_ch_layout, enum AVSampleFormat  in_sample_fmt, int  in_sample_rate,
                                      int log_offset, void *log_ctx){
    if(!s) s= swr_alloc();
    if(!s) return NULL;

    s->log_level_offset= log_offset;
    s->log_ctx= log_ctx;

    av_opt_set_int(s, "ocl", out_ch_layout,   0);
    av_opt_set_int(s, "osf", out_sample_fmt,  0);
    av_opt_set_int(s, "osr", out_sample_rate, 0);
    av_opt_set_int(s, "icl", in_ch_layout,    0);
    av_opt_set_int(s, "isf", in_sample_fmt,   0);
    av_opt_set_int(s, "isr", in_sample_rate,  0);
    av_opt_set_int(s, "tsf", AV_SAMPLE_FMT_S16, 0);
    av_opt_set_int(s, "ich", av_get_channel_layout_nb_channels(s-> in_ch_layout), 0);
    av_opt_set_int(s, "och", av_get_channel_layout_nb_channels(s->out_ch_layout), 0);
    av_opt_set_int(s, "uch", 0, 0);
    return s;
}


static void free_temp(AudioData *a){
    av_free(a->data);
    memset(a, 0, sizeof(*a));
}

void swr_free(SwrContext **ss){
    SwrContext *s= *ss;
    if(s){
        free_temp(&s->postin);
        free_temp(&s->midbuf);
        free_temp(&s->preout);
        free_temp(&s->in_buffer);
        swri_audio_convert_free(&s-> in_convert);
        swri_audio_convert_free(&s->out_convert);
        swri_audio_convert_free(&s->full_convert);
        swri_resample_free(&s->resample);
    }

    av_freep(ss);
}

int swr_init(struct SwrContext *s){
    s->in_buffer_index= 0;
    s->in_buffer_count= 0;
    s->resample_in_constraint= 0;
    free_temp(&s->postin);
    free_temp(&s->midbuf);
    free_temp(&s->preout);
    free_temp(&s->in_buffer);
    swri_audio_convert_free(&s-> in_convert);
    swri_audio_convert_free(&s->out_convert);
    swri_audio_convert_free(&s->full_convert);

    s-> in.planar= av_sample_fmt_is_planar(s-> in_sample_fmt);
    s->out.planar= av_sample_fmt_is_planar(s->out_sample_fmt);
    s-> in_sample_fmt= av_get_alt_sample_fmt(s-> in_sample_fmt, 0);
    s->out_sample_fmt= av_get_alt_sample_fmt(s->out_sample_fmt, 0);

    if(s-> in_sample_fmt >= AV_SAMPLE_FMT_NB){
        av_log(s, AV_LOG_ERROR, "Requested input sample format %d is invalid\n", s->in_sample_fmt);
        return AVERROR(EINVAL);
    }
    if(s->out_sample_fmt >= AV_SAMPLE_FMT_NB){
        av_log(s, AV_LOG_ERROR, "Requested output sample format %d is invalid\n", s->out_sample_fmt);
        return AVERROR(EINVAL);
    }

    if(   s->int_sample_fmt != AV_SAMPLE_FMT_S16
        &&s->int_sample_fmt != AV_SAMPLE_FMT_FLT){
        av_log(s, AV_LOG_ERROR, "Requested sample format %s is not supported internally, only float & S16 is supported\n", av_get_sample_fmt_name(s->int_sample_fmt));
        return AVERROR(EINVAL);
    }

    //FIXME should we allow/support using FLT on material that doesnt need it ?
    if(s->in_sample_fmt <= AV_SAMPLE_FMT_S16 || s->int_sample_fmt==AV_SAMPLE_FMT_S16){
        s->int_sample_fmt= AV_SAMPLE_FMT_S16;
    }else
        s->int_sample_fmt= AV_SAMPLE_FMT_FLT;


    if (s->out_sample_rate!=s->in_sample_rate || (s->flags & SWR_FLAG_RESAMPLE)){
        s->resample = swri_resample_init(s->resample, s->out_sample_rate, s->in_sample_rate, 16, 10, 0, 0.8);
    }else
        swri_resample_free(&s->resample);
    if(s->int_sample_fmt != AV_SAMPLE_FMT_S16 && s->resample){
        av_log(s, AV_LOG_ERROR, "Resampling only supported with internal s16 currently\n"); //FIXME
        return -1;
    }

    if(!s->used_ch_count)
        s->used_ch_count= s->in.ch_count;

    if(s->used_ch_count && s-> in_ch_layout && s->used_ch_count != av_get_channel_layout_nb_channels(s-> in_ch_layout)){
        av_log(s, AV_LOG_WARNING, "Input channel layout has a different number of channels than the number of used channels, ignoring layout\n");
        s-> in_ch_layout= 0;
    }

    if(!s-> in_ch_layout)
        s-> in_ch_layout= av_get_default_channel_layout(s->used_ch_count);
    if(!s->out_ch_layout)
        s->out_ch_layout= av_get_default_channel_layout(s->out.ch_count);

    s->rematrix= s->out_ch_layout  !=s->in_ch_layout || s->rematrix_volume!=1.0;

#define RSC 1 //FIXME finetune
    if(!s-> in.ch_count)
        s-> in.ch_count= av_get_channel_layout_nb_channels(s-> in_ch_layout);
    if(!s->used_ch_count)
        s->used_ch_count= s->in.ch_count;
    if(!s->out.ch_count)
        s->out.ch_count= av_get_channel_layout_nb_channels(s->out_ch_layout);

    if(!s-> in.ch_count){
        av_assert0(!s->in_ch_layout);
        av_log(s, AV_LOG_ERROR, "Input channel count and layout are unset\n");
        return -1;
    }

av_assert0(s->used_ch_count);
av_assert0(s->out.ch_count);
    s->resample_first= RSC*s->out.ch_count/s->in.ch_count - RSC < s->out_sample_rate/(float)s-> in_sample_rate - 1.0;

    s-> in.bps= av_get_bytes_per_sample(s-> in_sample_fmt);
    s->int_bps= av_get_bytes_per_sample(s->int_sample_fmt);
    s->out.bps= av_get_bytes_per_sample(s->out_sample_fmt);

    if(!s->resample && !s->rematrix && !s->channel_map){
        s->full_convert = swri_audio_convert_alloc(s->out_sample_fmt,
                                                   s-> in_sample_fmt, s-> in.ch_count, NULL, 0);
        return 0;
    }

    s->in_convert = swri_audio_convert_alloc(s->int_sample_fmt,
                                             s-> in_sample_fmt, s->used_ch_count, s->channel_map, 0);
    s->out_convert= swri_audio_convert_alloc(s->out_sample_fmt,
                                             s->int_sample_fmt, s->out.ch_count, NULL, 0);


    s->postin= s->in;
    s->preout= s->out;
    s->midbuf= s->in;
    s->in_buffer= s->in;
    if(s->channel_map){
        s->postin.ch_count=
        s->midbuf.ch_count=
        s->in_buffer.ch_count= s->used_ch_count;
    }
    if(!s->resample_first){
        s->midbuf.ch_count= s->out.ch_count;
        s->in_buffer.ch_count = s->out.ch_count;
    }

    s->in_buffer.bps = s->postin.bps = s->midbuf.bps = s->preout.bps =  s->int_bps;
    s->in_buffer.planar = s->postin.planar = s->midbuf.planar = s->preout.planar =  1;


    if(s->rematrix)
        return swri_rematrix_init(s);

    return 0;
}

static int realloc_audio(AudioData *a, int count){
    int i, countb;
    AudioData old;

    if(a->count >= count)
        return 0;

    count*=2;

    countb= FFALIGN(count*a->bps, 32);
    old= *a;

    av_assert0(a->planar);
    av_assert0(a->bps);
    av_assert0(a->ch_count);

    a->data= av_malloc(countb*a->ch_count);
    if(!a->data)
        return AVERROR(ENOMEM);
    for(i=0; i<a->ch_count; i++){
        a->ch[i]= a->data + i*(a->planar ? countb : a->bps);
        if(a->planar) memcpy(a->ch[i], old.ch[i], a->count*a->bps);
    }
    av_free(old.data);
    a->count= count;

    return 1;
}

static void copy(AudioData *out, AudioData *in,
                 int count){
    av_assert0(out->planar == in->planar);
    av_assert0(out->bps == in->bps);
    av_assert0(out->ch_count == in->ch_count);
    if(out->planar){
        int ch;
        for(ch=0; ch<out->ch_count; ch++)
            memcpy(out->ch[ch], in->ch[ch], count*out->bps);
    }else
        memcpy(out->ch[0], in->ch[0], count*out->ch_count*out->bps);
}

static void fill_audiodata(AudioData *out, uint8_t *in_arg [SWR_CH_MAX]){
    int i;
    if(out->planar){
        for(i=0; i<out->ch_count; i++)
            out->ch[i]= in_arg[i];
    }else{
        for(i=0; i<out->ch_count; i++)
            out->ch[i]= in_arg[0] + i*out->bps;
    }
}

/**
 *
 * out may be equal in.
 */
static void buf_set(AudioData *out, AudioData *in, int count){
    if(in->planar){
        int ch;
        for(ch=0; ch<out->ch_count; ch++)
            out->ch[ch]= in->ch[ch] + count*out->bps;
    }else
        out->ch[0]= in->ch[0] + count*out->ch_count*out->bps;
}

/**
 *
 * @return number of samples output per channel
 */
static int resample(SwrContext *s, AudioData *out_param, int out_count,
                             const AudioData * in_param, int in_count){
    AudioData in, out, tmp;
    int ret_sum=0;
    int border=0;

    tmp=out=*out_param;
    in =  *in_param;

    do{
        int ret, size, consumed;
        if(!s->resample_in_constraint && s->in_buffer_count){
            buf_set(&tmp, &s->in_buffer, s->in_buffer_index);
            ret= swri_multiple_resample(s->resample, &out, out_count, &tmp, s->in_buffer_count, &consumed);
            out_count -= ret;
            ret_sum += ret;
            buf_set(&out, &out, ret);
            s->in_buffer_count -= consumed;
            s->in_buffer_index += consumed;

            if(!in_count)
                break;
            if(s->in_buffer_count <= border){
                buf_set(&in, &in, -s->in_buffer_count);
                in_count += s->in_buffer_count;
                s->in_buffer_count=0;
                s->in_buffer_index=0;
                border = 0;
            }
        }

        if(in_count && !s->in_buffer_count){
            s->in_buffer_index=0;
            ret= swri_multiple_resample(s->resample, &out, out_count, &in, in_count, &consumed);
            out_count -= ret;
            ret_sum += ret;
            buf_set(&out, &out, ret);
            in_count -= consumed;
            buf_set(&in, &in, consumed);
        }

        //TODO is this check sane considering the advanced copy avoidance below
        size= s->in_buffer_index + s->in_buffer_count + in_count;
        if(   size > s->in_buffer.count
           && s->in_buffer_count + in_count <= s->in_buffer_index){
            buf_set(&tmp, &s->in_buffer, s->in_buffer_index);
            copy(&s->in_buffer, &tmp, s->in_buffer_count);
            s->in_buffer_index=0;
        }else
            if((ret=realloc_audio(&s->in_buffer, size)) < 0)
                return ret;

        if(in_count){
            int count= in_count;
            if(s->in_buffer_count && s->in_buffer_count+2 < count && out_count) count= s->in_buffer_count+2;

            buf_set(&tmp, &s->in_buffer, s->in_buffer_index + s->in_buffer_count);
            copy(&tmp, &in, /*in_*/count);
            s->in_buffer_count += count;
            in_count -= count;
            border += count;
            buf_set(&in, &in, count);
            s->resample_in_constraint= 0;
            if(s->in_buffer_count != count || in_count)
                continue;
        }
        break;
    }while(1);

    s->resample_in_constraint= !!out_count;

    return ret_sum;
}

int swr_convert(struct SwrContext *s, uint8_t *out_arg[SWR_CH_MAX], int out_count,
                         const uint8_t *in_arg [SWR_CH_MAX], int  in_count){
    AudioData *postin, *midbuf, *preout;
    int ret/*, in_max*/;
    AudioData * in= &s->in;
    AudioData *out= &s->out;
    AudioData preout_tmp, midbuf_tmp;

    if(!s->resample){
        if(in_count > out_count)
            return -1;
        out_count = in_count;
    }

    if(!in_arg){
        if(s->in_buffer_count){
            AudioData *a= &s->in_buffer;
            int i, j, ret;
            if((ret=realloc_audio(a, s->in_buffer_index + 2*s->in_buffer_count)) < 0)
                return ret;
            av_assert0(a->planar);
            for(i=0; i<a->ch_count; i++){
                for(j=0; j<s->in_buffer_count; j++){
                    memcpy(a->ch[i] + (s->in_buffer_index+s->in_buffer_count+j  )*a->bps,
                           a->ch[i] + (s->in_buffer_index+s->in_buffer_count-j-1)*a->bps, a->bps);
                }
            }
            s->in_buffer_count += (s->in_buffer_count+1)/2;
            s->resample_in_constraint = 0;
        }else{
            return 0;
        }
    }else
        fill_audiodata(in ,  (void*)in_arg);
    fill_audiodata(out, out_arg);

    if(s->full_convert){
        av_assert0(!s->resample);
        swri_audio_convert(s->full_convert, out, in, in_count);
        return out_count;
    }

//     in_max= out_count*(int64_t)s->in_sample_rate / s->out_sample_rate + resample_filter_taps;
//     in_count= FFMIN(in_count, in_in + 2 - s->hist_buffer_count);

    if((ret=realloc_audio(&s->postin, in_count))<0)
        return ret;
    if(s->resample_first){
        av_assert0(s->midbuf.ch_count == s->used_ch_count);
        if((ret=realloc_audio(&s->midbuf, out_count))<0)
            return ret;
    }else{
        av_assert0(s->midbuf.ch_count ==  s->out.ch_count);
        if((ret=realloc_audio(&s->midbuf,  in_count))<0)
            return ret;
    }
    if((ret=realloc_audio(&s->preout, out_count))<0)
        return ret;

    postin= &s->postin;

    midbuf_tmp= s->midbuf;
    midbuf= &midbuf_tmp;
    preout_tmp= s->preout;
    preout= &preout_tmp;

    if(s->int_sample_fmt == s-> in_sample_fmt && s->in.planar)
        postin= in;

    if(s->resample_first ? !s->resample : !s->rematrix)
        midbuf= postin;

    if(s->resample_first ? !s->rematrix : !s->resample)
        preout= midbuf;

    if(s->int_sample_fmt == s->out_sample_fmt && s->out.planar){
        if(preout==in){
            out_count= FFMIN(out_count, in_count); //TODO check at teh end if this is needed or redundant
            av_assert0(s->in.planar); //we only support planar internally so it has to be, we support copying non planar though
            copy(out, in, out_count);
            return out_count;
        }
        else if(preout==postin) preout= midbuf= postin= out;
        else if(preout==midbuf) preout= midbuf= out;
        else                    preout= out;
    }

    if(in != postin){
        swri_audio_convert(s->in_convert, postin, in, in_count);
    }

    if(s->resample_first){
        if(postin != midbuf)
            out_count= resample(s, midbuf, out_count, postin, in_count);
        if(midbuf != preout)
            swri_rematrix(s, preout, midbuf, out_count, preout==out);
    }else{
        if(postin != midbuf)
            swri_rematrix(s, midbuf, postin, in_count, midbuf==out);
        if(midbuf != preout)
            out_count= resample(s, preout, out_count, midbuf, in_count);
    }

    if(preout != out){
//FIXME packed doesnt need more than 1 chan here!
        swri_audio_convert(s->out_convert, out, preout, out_count);
    }
    if(!in_arg)
        s->in_buffer_count = 0;
    return out_count;
}

