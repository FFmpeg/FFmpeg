/*
 * AVOptions
 * Copyright (c) 2005 Michael Niedermayer <michaelni@gmx.at>
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
 *
 */
 
/**
 * @file opt.c
 * AVOptions
 * @author Michael Niedermayer <michaelni@gmx.at>
 */
 
#include "avcodec.h"
#include "opt.h"
 
static double av_parse_num(const char *name, char **tail){
    double d;
    d= strtod(name, tail);
    if(*tail>name && (**tail=='/' || **tail==':'))
        d/=strtod((*tail)+1, tail);
    return d;
}

//FIXME order them and do a bin search
static AVOption *find_opt(void *v, const char *name, const char *unit){
    AVClass *c= *(AVClass**)v; //FIXME silly way of storing AVClass
    AVOption *o= c->option;
    
    for(;o && o->name; o++){
        if(!strcmp(o->name, name) && (!unit || !strcmp(o->unit, unit)) )
            return o;
    }
    return NULL;
}

AVOption *av_next_option(void *obj, AVOption *last){
    if(last && last[1].name) return ++last;
    else if(last)            return NULL;
    else                     return (*(AVClass**)obj)->option;
}

static AVOption *av_set_number(void *obj, const char *name, double num, int den, int64_t intnum){
    AVOption *o= find_opt(obj, name, NULL);
    void *dst;
    if(!o || o->offset<=0) 
        return NULL;
    
    if(o->max*den < num*intnum || o->min*den > num*intnum)
        return NULL;
        
    dst= ((uint8_t*)obj) + o->offset;

    switch(o->type){
    case FF_OPT_TYPE_FLAGS:   
    case FF_OPT_TYPE_INT:   *(int       *)dst= lrintf(num/den)*intnum; break;
    case FF_OPT_TYPE_INT64: *(int64_t   *)dst= lrintf(num/den)*intnum; break;
    case FF_OPT_TYPE_FLOAT: *(float     *)dst= num*intnum/den;         break;
    case FF_OPT_TYPE_DOUBLE:*(double    *)dst= num*intnum/den;         break;
    case FF_OPT_TYPE_RATIONAL:
        if((int)num == num) *(AVRational*)dst= (AVRational){num*intnum, den};
        else                *(AVRational*)dst= av_d2q(num*intnum/den, 1<<24);
    default:
        return NULL;
    }
    return o;
}

static AVOption *set_all_opt(void *v, const char *unit, double d){
    AVClass *c= *(AVClass**)v; //FIXME silly way of storing AVClass
    AVOption *o= c->option;
    AVOption *ret=NULL;
    
    for(;o && o->name; o++){
        if(o->type != FF_OPT_TYPE_CONST && o->unit && !strcmp(o->unit, unit)){
            double tmp= d;
            if(o->type == FF_OPT_TYPE_FLAGS)
                tmp= av_get_int(v, o->name, NULL) | (int64_t)d;

            av_set_number(v, o->name, tmp, 1, 1);
            ret= o;
        }
    }
    return ret;
}

//FIXME use eval.c maybe?
AVOption *av_set_string(void *obj, const char *name, const char *val){
    AVOption *o= find_opt(obj, name, NULL);
    if(o && o->offset==0 && o->type == FF_OPT_TYPE_CONST && o->unit){
        return set_all_opt(obj, o->unit, o->default_val);
    }
    if(!o || !val || o->offset<=0) 
        return NULL;
    if(o->type != FF_OPT_TYPE_STRING){
        for(;;){
            int i;
            char buf[256], *tail;
            int cmd=0;
            double d;

            if(*val == '+' || *val == '-')
                cmd= *(val++);
            
            for(i=0; i<sizeof(buf)-1 && val[i] && val[i]!='+' && val[i]!='-'; i++)
                buf[i]= val[i];
            buf[i]=0;
            val+= i;
            
            d= av_parse_num(buf, &tail);
            if(tail <= buf){
                AVOption *o_named= find_opt(obj, buf, o->unit);
                if(o_named && o_named->type == FF_OPT_TYPE_CONST) 
                    d= o_named->default_val;
                else if(!strcmp(buf, "default")) d= o->default_val;
                else if(!strcmp(buf, "max"    )) d= o->max;
                else if(!strcmp(buf, "min"    )) d= o->min;
                else return NULL;
            }
            if(o->type == FF_OPT_TYPE_FLAGS){
                if     (cmd=='+') d= av_get_int(obj, name, NULL) | (int64_t)d;
                else if(cmd=='-') d= av_get_int(obj, name, NULL) &~(int64_t)d;
            }else if(cmd=='-')
                d= -d;

            av_set_number(obj, name, d, 1, 1);
            if(!*val)
                return o;
        }
        return NULL;
    }
    
    memcpy(((uint8_t*)obj) + o->offset, val, sizeof(val));
    return o;
}

AVOption *av_set_double(void *obj, const char *name, double n){
    return av_set_number(obj, name, n, 1, 1);
}

AVOption *av_set_q(void *obj, const char *name, AVRational n){
    return av_set_number(obj, name, n.num, n.den, 1);
}

AVOption *av_set_int(void *obj, const char *name, int64_t n){
    return av_set_number(obj, name, 1, 1, n);
}

/**
 * 
 * @param buf a buffer which is used for returning non string values as strings, can be NULL
 * @param buf_len allocated length in bytes of buf
 */
const char *av_get_string(void *obj, const char *name, AVOption **o_out, char *buf, int buf_len){
    AVOption *o= find_opt(obj, name, NULL);
    void *dst;
    if(!o || o->offset<=0)
        return NULL;
    if(o->type != FF_OPT_TYPE_STRING && (!buf || !buf_len))
        return NULL;

    dst= ((uint8_t*)obj) + o->offset;
    if(o_out) *o_out= o;
    
    if(o->type == FF_OPT_TYPE_STRING)
        return dst;
    
    switch(o->type){
    case FF_OPT_TYPE_FLAGS:     snprintf(buf, buf_len, "0x%08X",*(int    *)dst);break;
    case FF_OPT_TYPE_INT:       snprintf(buf, buf_len, "%d" , *(int    *)dst);break;
    case FF_OPT_TYPE_INT64:     snprintf(buf, buf_len, "%Ld", *(int64_t*)dst);break;
    case FF_OPT_TYPE_FLOAT:     snprintf(buf, buf_len, "%f" , *(float  *)dst);break;
    case FF_OPT_TYPE_DOUBLE:    snprintf(buf, buf_len, "%f" , *(double *)dst);break;
    case FF_OPT_TYPE_RATIONAL:  snprintf(buf, buf_len, "%d/%d", ((AVRational*)dst)->num, ((AVRational*)dst)->den);break;
    default: return NULL;
    }
    return buf;
}

static int av_get_number(void *obj, const char *name, AVOption **o_out, double *num, int *den, int64_t *intnum){
    AVOption *o= find_opt(obj, name, NULL);
    void *dst;
    if(!o || o->offset<=0)
        goto error;

    dst= ((uint8_t*)obj) + o->offset;

    if(o_out) *o_out= o;

    switch(o->type){
    case FF_OPT_TYPE_FLAGS:   
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

double av_get_double(void *obj, const char *name, AVOption **o_out){
    int64_t intnum=1;
    double num=1;
    int den=1;

    av_get_number(obj, name, o_out, &num, &den, &intnum);
    return num*intnum/den;
}

AVRational av_get_q(void *obj, const char *name, AVOption **o_out){
    int64_t intnum=1;
    double num=1;
    int den=1;

    av_get_number(obj, name, o_out, &num, &den, &intnum);
    if(num == 1.0 && (int)intnum == intnum)
        return (AVRational){intnum, den};
    else
        return av_d2q(num*intnum/den, 1<<24);
}

int64_t av_get_int(void *obj, const char *name, AVOption **o_out){
    int64_t intnum=1;
    double num=1;
    int den=1;

    av_get_number(obj, name, o_out, &num, &den, &intnum);
    return num*intnum/den;
}

int av_opt_show(void *obj, void *av_log_obj){
    AVOption *opt=NULL;
    
    if(!obj)
        return -1;

    av_log(av_log_obj, AV_LOG_INFO, "%s AVOptions:\n", (*(AVClass**)obj)->class_name);

    while((opt= av_next_option(obj, opt))){
        if(!(opt->flags & (AV_OPT_FLAG_ENCODING_PARAM|AV_OPT_FLAG_DECODING_PARAM)))
            continue;
            
        av_log(av_log_obj, AV_LOG_INFO, "-%-17s ", opt->name);
        av_log(av_log_obj, AV_LOG_INFO, "%c", (opt->flags & AV_OPT_FLAG_ENCODING_PARAM) ? 'E' : '.');
        av_log(av_log_obj, AV_LOG_INFO, "%c", (opt->flags & AV_OPT_FLAG_DECODING_PARAM) ? 'D' : '.');
        av_log(av_log_obj, AV_LOG_INFO, "%c", (opt->flags & AV_OPT_FLAG_VIDEO_PARAM   ) ? 'V' : '.');
        av_log(av_log_obj, AV_LOG_INFO, "%c", (opt->flags & AV_OPT_FLAG_AUDIO_PARAM   ) ? 'A' : '.');
        av_log(av_log_obj, AV_LOG_INFO, "%c", (opt->flags & AV_OPT_FLAG_SUBTITLE_PARAM) ? 'S' : '.');
        
        if(opt->help)
            av_log(av_log_obj, AV_LOG_INFO, " %s", opt->help);
        av_log(av_log_obj, AV_LOG_INFO, "\n");
    }
    return 0;
}
