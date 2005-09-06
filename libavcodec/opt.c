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
 
static double av_parse_num(const char *name, char **tail){
    double d;
    d= strtod(name, tail);
    if(*tail>name && (**tail=='/' || **tail==':'))
        d/=strtod((*tail)+1, tail);
    return d;
}

//FIXME order them and do a bin search
static AVOption *find_opt(void *v, const char *name){
    AVClass *c= *(AVClass**)v; //FIXME silly way of storing AVClass
    AVOption *o= c->option;
    
    for(;o && o->name; o++){
        if(!strcmp(o->name, name))
            return o;
    }
    return NULL;
}

AVOption *av_next_option(void *obj, AVOption *last){
    if(last && last[1].name) return ++last;
    else if(last)            return NULL;
    else                     return (*(AVClass**)obj)->option;
}

static int av_set_number(void *obj, const char *name, double num, int den, int64_t intnum){
    AVOption *o= find_opt(obj, name);
    void *dst;
    if(!o || o->offset<=0) 
        return -1;
    
    if(o->max*den < num*intnum || o->min*den > num*intnum)
        return -1;
        
    dst= ((uint8_t*)obj) + o->offset;

    switch(o->type){
    case FF_OPT_TYPE_INT:
        *(int*)dst= lrintf(num/den)*intnum;
        break;
    case FF_OPT_TYPE_INT64:
        *(int64_t*)dst= lrintf(num/den)*intnum;
        break;
    case FF_OPT_TYPE_FLOAT:
        *(float*)dst= num*intnum/den;
        break;
    case FF_OPT_TYPE_DOUBLE:
        *(double*)dst= num*intnum/den;
        break;
    case FF_OPT_TYPE_RATIONAL:
        if((int)num == num)
            *(AVRational*)dst= (AVRational){num*intnum, den};
        else
            *(AVRational*)dst= av_d2q(num*intnum/den, 1<<24);
    default:
        return -1;
    }
    return 0;
}

//FIXME use eval.c maybe?
int av_set_string(void *obj, const char *name, const char *val){
    AVOption *o= find_opt(obj, name);
    if(!o || !val || o->offset<=0) 
        return -1;
    if(o->type != FF_OPT_TYPE_STRING){
        double d=0, tmp_d;
        for(;;){
            int i;
            char buf[256], *tail;

            for(i=0; i<sizeof(buf)-1 && val[i] && val[i]!='+'; i++)
                buf[i]= val[i];
            buf[i]=0;
            val+= i;
            
            tmp_d= av_parse_num(buf, &tail);
            if(tail > buf)
                d+= tmp_d;
            else{
                AVOption *o_named= find_opt(obj, buf);
                if(o_named && o_named->type == FF_OPT_TYPE_CONST) 
                    d+= o_named->default_val;
                else if(!strcmp(buf, "default")) d+= o->default_val;
                else if(!strcmp(buf, "max"    )) d+= o->max;
                else if(!strcmp(buf, "min"    )) d+= o->min;
                else return -1;
            }

            if(*val == '+') val++;
            if(!*val)
                return av_set_number(obj, name, d, 1, 1);
        }
        return -1;
    }
    
    memcpy(((uint8_t*)obj) + o->offset, val, sizeof(val));
    return 0;
}

int av_set_double(void *obj, const char *name, double n){
    return av_set_number(obj, name, n, 1, 1);
}

int av_set_q(void *obj, const char *name, AVRational n){
    return av_set_number(obj, name, n.num, n.den, 1);
}

int av_set_int(void *obj, const char *name, int64_t n){
    return av_set_number(obj, name, 1, 1, n);
}

const char *av_get_string(void *obj, const char *name){
    AVOption *o= find_opt(obj, name);
    if(!o || o->offset<=0)
        return NULL;
    if(o->type != FF_OPT_TYPE_STRING) //FIXME convert to string? but what about free()?
        return NULL;

    return (const char*)(((uint8_t*)obj) + o->offset);
}

double av_get_double(void *obj, const char *name){
    AVOption *o= find_opt(obj, name);
    void *dst;
    if(!o || o->offset<=0)
        return NAN;

    dst= ((uint8_t*)obj) + o->offset;

    switch(o->type){
    case FF_OPT_TYPE_INT:       return *(int*)dst;
    case FF_OPT_TYPE_INT64:     return *(int64_t*)dst; //FIXME maybe write a av_get_int64() ?
    case FF_OPT_TYPE_FLOAT:     return *(float*)dst;
    case FF_OPT_TYPE_DOUBLE:    return *(double*)dst;
    case FF_OPT_TYPE_RATIONAL:  return av_q2d(*(AVRational*)dst); //FIXME maybe write a av_get_q() ?
    default:                    return NAN;
    }
}
