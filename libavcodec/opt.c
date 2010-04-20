/*
 * AVOptions
 * Copyright (c) 2005 Michael Niedermayer <michaelni@gmx.at>
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
 * AVOptions
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "avcodec.h"
#include "opt.h"
#include "eval.h"

//FIXME order them and do a bin search
const AVOption *av_find_opt(void *v, const char *name, const char *unit, int mask, int flags){
    AVClass *c= *(AVClass**)v; //FIXME silly way of storing AVClass
    const AVOption *o= c->option;

    for(;o && o->name; o++){
        if(!strcmp(o->name, name) && (!unit || (o->unit && !strcmp(o->unit, unit))) && (o->flags & mask) == flags )
            return o;
    }
    return NULL;
}

const AVOption *av_next_option(void *obj, const AVOption *last){
    if(last && last[1].name) return ++last;
    else if(last)            return NULL;
    else                     return (*(AVClass**)obj)->option;
}

static int av_set_number2(void *obj, const char *name, double num, int den, int64_t intnum, const AVOption **o_out){
    const AVOption *o= av_find_opt(obj, name, NULL, 0, 0);
    void *dst;
    if(o_out)
        *o_out= o;
    if(!o || o->offset<=0)
        return AVERROR(ENOENT);

    if(o->max*den < num*intnum || o->min*den > num*intnum) {
        av_log(obj, AV_LOG_ERROR, "Value %lf for parameter '%s' out of range\n", num, name);
        return AVERROR(ERANGE);
    }

    dst= ((uint8_t*)obj) + o->offset;

    switch(o->type){
    case FF_OPT_TYPE_FLAGS:
    case FF_OPT_TYPE_INT:   *(int       *)dst= llrint(num/den)*intnum; break;
    case FF_OPT_TYPE_INT64: *(int64_t   *)dst= llrint(num/den)*intnum; break;
    case FF_OPT_TYPE_FLOAT: *(float     *)dst= num*intnum/den;         break;
    case FF_OPT_TYPE_DOUBLE:*(double    *)dst= num*intnum/den;         break;
    case FF_OPT_TYPE_RATIONAL:
        if((int)num == num) *(AVRational*)dst= (AVRational){num*intnum, den};
        else                *(AVRational*)dst= av_d2q(num*intnum/den, 1<<24);
        break;
    default:
        return AVERROR(EINVAL);
    }
    return 0;
}

static const AVOption *av_set_number(void *obj, const char *name, double num, int den, int64_t intnum){
    const AVOption *o = NULL;
    if (av_set_number2(obj, name, num, den, intnum, &o) < 0)
        return NULL;
    else
        return o;
}

static const double const_values[]={
    M_PI,
    M_E,
    FF_QP2LAMBDA,
    0
};

static const char * const const_names[]={
    "PI",
    "E",
    "QP2LAMBDA",
    0
};

static int hexchar2int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int av_set_string3(void *obj, const char *name, const char *val, int alloc, const AVOption **o_out){
    int ret;
    const AVOption *o= av_find_opt(obj, name, NULL, 0, 0);
    if (o_out)
        *o_out = o;
    if(!o)
        return AVERROR(ENOENT);
    if(!val || o->offset<=0)
        return AVERROR(EINVAL);

    if(o->type == FF_OPT_TYPE_BINARY){
        uint8_t **dst = (uint8_t **)(((uint8_t*)obj) + o->offset);
        int *lendst = (int *)(dst + 1);
        uint8_t *bin, *ptr;
        int len = strlen(val);
        av_freep(dst);
        *lendst = 0;
        if (len & 1) return AVERROR(EINVAL);
        len /= 2;
        ptr = bin = av_malloc(len);
        while (*val) {
            int a = hexchar2int(*val++);
            int b = hexchar2int(*val++);
            if (a < 0 || b < 0) {
                av_free(bin);
                return AVERROR(EINVAL);
            }
            *ptr++ = (a << 4) | b;
        }
        *dst = bin;
        *lendst = len;
        return 0;
    }
    if(o->type != FF_OPT_TYPE_STRING){
        int notfirst=0;
        for(;;){
            int i;
            char buf[256];
            int cmd=0;
            double d;
            const char *error = NULL;

            if(*val == '+' || *val == '-')
                cmd= *(val++);

            for(i=0; i<sizeof(buf)-1 && val[i] && val[i]!='+' && val[i]!='-'; i++)
                buf[i]= val[i];
            buf[i]=0;

            d = ff_parse_and_eval_expr(buf, const_values, const_names, NULL, NULL, NULL, NULL, NULL, &error);
            if(isnan(d)) {
                const AVOption *o_named= av_find_opt(obj, buf, o->unit, 0, 0);
                if(o_named && o_named->type == FF_OPT_TYPE_CONST)
                    d= o_named->default_val;
                else if(!strcmp(buf, "default")) d= o->default_val;
                else if(!strcmp(buf, "max"    )) d= o->max;
                else if(!strcmp(buf, "min"    )) d= o->min;
                else if(!strcmp(buf, "none"   )) d= 0;
                else if(!strcmp(buf, "all"    )) d= ~0;
                else {
                    if (error)
                        av_log(obj, AV_LOG_ERROR, "Unable to parse option value \"%s\": %s\n", val, error);
                    return AVERROR(EINVAL);
                }
            }
            if(o->type == FF_OPT_TYPE_FLAGS){
                if     (cmd=='+') d= av_get_int(obj, name, NULL) | (int64_t)d;
                else if(cmd=='-') d= av_get_int(obj, name, NULL) &~(int64_t)d;
            }else{
                if     (cmd=='+') d= notfirst*av_get_double(obj, name, NULL) + d;
                else if(cmd=='-') d= notfirst*av_get_double(obj, name, NULL) - d;
            }

            if ((ret = av_set_number2(obj, name, d, 1, 1, o_out)) < 0)
                return ret;
            val+= i;
            if(!*val)
                return 0;
            notfirst=1;
        }
        return AVERROR(EINVAL);
    }

    if(alloc){
        av_free(*(void**)(((uint8_t*)obj) + o->offset));
        val= av_strdup(val);
    }

    memcpy(((uint8_t*)obj) + o->offset, &val, sizeof(val));
    return 0;
}

#if LIBAVCODEC_VERSION_MAJOR < 53
const AVOption *av_set_string2(void *obj, const char *name, const char *val, int alloc){
    const AVOption *o;
    if (av_set_string3(obj, name, val, alloc, &o) < 0)
        return NULL;
    return o;
}

const AVOption *av_set_string(void *obj, const char *name, const char *val){
    const AVOption *o;
    if (av_set_string3(obj, name, val, 0, &o) < 0)
        return NULL;
    return o;
}
#endif

const AVOption *av_set_double(void *obj, const char *name, double n){
    return av_set_number(obj, name, n, 1, 1);
}

const AVOption *av_set_q(void *obj, const char *name, AVRational n){
    return av_set_number(obj, name, n.num, n.den, 1);
}

const AVOption *av_set_int(void *obj, const char *name, int64_t n){
    return av_set_number(obj, name, 1, 1, n);
}

/**
 *
 * @param buf a buffer which is used for returning non string values as strings, can be NULL
 * @param buf_len allocated length in bytes of buf
 */
const char *av_get_string(void *obj, const char *name, const AVOption **o_out, char *buf, int buf_len){
    const AVOption *o= av_find_opt(obj, name, NULL, 0, 0);
    void *dst;
    uint8_t *bin;
    int len, i;
    if(!o || o->offset<=0)
        return NULL;
    if(o->type != FF_OPT_TYPE_STRING && (!buf || !buf_len))
        return NULL;

    dst= ((uint8_t*)obj) + o->offset;
    if(o_out) *o_out= o;

    switch(o->type){
    case FF_OPT_TYPE_FLAGS:     snprintf(buf, buf_len, "0x%08X",*(int    *)dst);break;
    case FF_OPT_TYPE_INT:       snprintf(buf, buf_len, "%d" , *(int    *)dst);break;
    case FF_OPT_TYPE_INT64:     snprintf(buf, buf_len, "%"PRId64, *(int64_t*)dst);break;
    case FF_OPT_TYPE_FLOAT:     snprintf(buf, buf_len, "%f" , *(float  *)dst);break;
    case FF_OPT_TYPE_DOUBLE:    snprintf(buf, buf_len, "%f" , *(double *)dst);break;
    case FF_OPT_TYPE_RATIONAL:  snprintf(buf, buf_len, "%d/%d", ((AVRational*)dst)->num, ((AVRational*)dst)->den);break;
    case FF_OPT_TYPE_STRING:    return *(void**)dst;
    case FF_OPT_TYPE_BINARY:
        len = *(int*)(((uint8_t *)dst) + sizeof(uint8_t *));
        if(len >= (buf_len + 1)/2) return NULL;
        bin = *(uint8_t**)dst;
        for(i = 0; i < len; i++) snprintf(buf + i*2, 3, "%02X", bin[i]);
        break;
    default: return NULL;
    }
    return buf;
}

static int av_get_number(void *obj, const char *name, const AVOption **o_out, double *num, int *den, int64_t *intnum){
    const AVOption *o= av_find_opt(obj, name, NULL, 0, 0);
    void *dst;
    if(!o || o->offset<=0)
        goto error;

    dst= ((uint8_t*)obj) + o->offset;

    if(o_out) *o_out= o;

    switch(o->type){
    case FF_OPT_TYPE_FLAGS:     *intnum= *(unsigned int*)dst;return 0;
    case FF_OPT_TYPE_INT:       *intnum= *(int    *)dst;return 0;
    case FF_OPT_TYPE_INT64:     *intnum= *(int64_t*)dst;return 0;
    case FF_OPT_TYPE_FLOAT:     *num=    *(float  *)dst;return 0;
    case FF_OPT_TYPE_DOUBLE:    *num=    *(double *)dst;return 0;
    case FF_OPT_TYPE_RATIONAL:  *intnum= ((AVRational*)dst)->num;
                                *den   = ((AVRational*)dst)->den;
                                                        return 0;
    }
error:
    *den=*intnum=0;
    return -1;
}

double av_get_double(void *obj, const char *name, const AVOption **o_out){
    int64_t intnum=1;
    double num=1;
    int den=1;

    av_get_number(obj, name, o_out, &num, &den, &intnum);
    return num*intnum/den;
}

AVRational av_get_q(void *obj, const char *name, const AVOption **o_out){
    int64_t intnum=1;
    double num=1;
    int den=1;

    av_get_number(obj, name, o_out, &num, &den, &intnum);
    if(num == 1.0 && (int)intnum == intnum)
        return (AVRational){intnum, den};
    else
        return av_d2q(num*intnum/den, 1<<24);
}

int64_t av_get_int(void *obj, const char *name, const AVOption **o_out){
    int64_t intnum=1;
    double num=1;
    int den=1;

    av_get_number(obj, name, o_out, &num, &den, &intnum);
    return num*intnum/den;
}

static void opt_list(void *obj, void *av_log_obj, const char *unit)
{
    const AVOption *opt=NULL;

    while((opt= av_next_option(obj, opt))){
        if(!(opt->flags & (AV_OPT_FLAG_ENCODING_PARAM|AV_OPT_FLAG_DECODING_PARAM)))
            continue;

        /* Don't print CONST's on level one.
         * Don't print anything but CONST's on level two.
         * Only print items from the requested unit.
         */
        if (!unit && opt->type==FF_OPT_TYPE_CONST)
            continue;
        else if (unit && opt->type!=FF_OPT_TYPE_CONST)
            continue;
        else if (unit && opt->type==FF_OPT_TYPE_CONST && strcmp(unit, opt->unit))
            continue;
        else if (unit && opt->type == FF_OPT_TYPE_CONST)
            av_log(av_log_obj, AV_LOG_INFO, "   %-15s ", opt->name);
        else
            av_log(av_log_obj, AV_LOG_INFO, "-%-17s ", opt->name);

        switch( opt->type )
        {
            case FF_OPT_TYPE_FLAGS:
                av_log( av_log_obj, AV_LOG_INFO, "%-7s ", "<flags>" );
                break;
            case FF_OPT_TYPE_INT:
                av_log( av_log_obj, AV_LOG_INFO, "%-7s ", "<int>" );
                break;
            case FF_OPT_TYPE_INT64:
                av_log( av_log_obj, AV_LOG_INFO, "%-7s ", "<int64>" );
                break;
            case FF_OPT_TYPE_DOUBLE:
                av_log( av_log_obj, AV_LOG_INFO, "%-7s ", "<double>" );
                break;
            case FF_OPT_TYPE_FLOAT:
                av_log( av_log_obj, AV_LOG_INFO, "%-7s ", "<float>" );
                break;
            case FF_OPT_TYPE_STRING:
                av_log( av_log_obj, AV_LOG_INFO, "%-7s ", "<string>" );
                break;
            case FF_OPT_TYPE_RATIONAL:
                av_log( av_log_obj, AV_LOG_INFO, "%-7s ", "<rational>" );
                break;
            case FF_OPT_TYPE_BINARY:
                av_log( av_log_obj, AV_LOG_INFO, "%-7s ", "<binary>" );
                break;
            case FF_OPT_TYPE_CONST:
            default:
                av_log( av_log_obj, AV_LOG_INFO, "%-7s ", "" );
                break;
        }
        av_log(av_log_obj, AV_LOG_INFO, "%c", (opt->flags & AV_OPT_FLAG_ENCODING_PARAM) ? 'E' : '.');
        av_log(av_log_obj, AV_LOG_INFO, "%c", (opt->flags & AV_OPT_FLAG_DECODING_PARAM) ? 'D' : '.');
        av_log(av_log_obj, AV_LOG_INFO, "%c", (opt->flags & AV_OPT_FLAG_VIDEO_PARAM   ) ? 'V' : '.');
        av_log(av_log_obj, AV_LOG_INFO, "%c", (opt->flags & AV_OPT_FLAG_AUDIO_PARAM   ) ? 'A' : '.');
        av_log(av_log_obj, AV_LOG_INFO, "%c", (opt->flags & AV_OPT_FLAG_SUBTITLE_PARAM) ? 'S' : '.');

        if(opt->help)
            av_log(av_log_obj, AV_LOG_INFO, " %s", opt->help);
        av_log(av_log_obj, AV_LOG_INFO, "\n");
        if (opt->unit && opt->type != FF_OPT_TYPE_CONST) {
            opt_list(obj, av_log_obj, opt->unit);
        }
    }
}

int av_opt_show(void *obj, void *av_log_obj){
    if(!obj)
        return -1;

    av_log(av_log_obj, AV_LOG_INFO, "%s AVOptions:\n", (*(AVClass**)obj)->class_name);

    opt_list(obj, av_log_obj, NULL);

    return 0;
}

/** Set the values of the AVCodecContext or AVFormatContext structure.
 * They are set to the defaults specified in the according AVOption options
 * array default_val field.
 *
 * @param s AVCodecContext or AVFormatContext for which the defaults will be set
 */
void av_opt_set_defaults2(void *s, int mask, int flags)
{
    const AVOption *opt = NULL;
    while ((opt = av_next_option(s, opt)) != NULL) {
        if((opt->flags & mask) != flags)
            continue;
        switch(opt->type) {
            case FF_OPT_TYPE_CONST:
                /* Nothing to be done here */
            break;
            case FF_OPT_TYPE_FLAGS:
            case FF_OPT_TYPE_INT: {
                int val;
                val = opt->default_val;
                av_set_int(s, opt->name, val);
            }
            break;
            case FF_OPT_TYPE_INT64:
                if((double)(opt->default_val+0.6) == opt->default_val)
                    av_log(s, AV_LOG_DEBUG, "loss of precision in default of %s\n", opt->name);
                av_set_int(s, opt->name, opt->default_val);
            break;
            case FF_OPT_TYPE_FLOAT: {
                double val;
                val = opt->default_val;
                av_set_double(s, opt->name, val);
            }
            break;
            case FF_OPT_TYPE_RATIONAL: {
                AVRational val;
                val = av_d2q(opt->default_val, INT_MAX);
                av_set_q(s, opt->name, val);
            }
            break;
            case FF_OPT_TYPE_STRING:
            case FF_OPT_TYPE_BINARY:
                /* Cannot set default for string as default_val is of type * double */
            break;
            default:
                av_log(s, AV_LOG_DEBUG, "AVOption type %d of option %s not implemented yet\n", opt->type, opt->name);
        }
    }
}

void av_opt_set_defaults(void *s){
    av_opt_set_defaults2(s, 0, 0);
}

