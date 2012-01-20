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

#include "avutil.h"
#include "avstring.h"
#include "opt.h"
#include "eval.h"
#include "dict.h"
#include "log.h"

#if FF_API_FIND_OPT
//FIXME order them and do a bin search
const AVOption *av_find_opt(void *v, const char *name, const char *unit, int mask, int flags)
{
    const AVOption *o = NULL;

    while ((o = av_next_option(v, o))) {
        if (!strcmp(o->name, name) && (!unit || (o->unit && !strcmp(o->unit, unit))) && (o->flags & mask) == flags)
            return o;
    }
    return NULL;
}
#endif

#if FF_API_OLD_AVOPTIONS
const AVOption *av_next_option(void *obj, const AVOption *last)
{
    return av_opt_next(obj, last);
}
#endif

const AVOption *av_opt_next(void *obj, const AVOption *last)
{
    AVClass *class = *(AVClass**)obj;
    if (!last && class->option[0].name) return class->option;
    if (last && last[1].name)           return ++last;
    return NULL;
}

static int read_number(const AVOption *o, void *dst, double *num, int *den, int64_t *intnum)
{
    switch (o->type) {
    case AV_OPT_TYPE_FLAGS:     *intnum = *(unsigned int*)dst;return 0;
    case AV_OPT_TYPE_INT:       *intnum = *(int         *)dst;return 0;
    case AV_OPT_TYPE_INT64:     *intnum = *(int64_t     *)dst;return 0;
    case AV_OPT_TYPE_FLOAT:     *num    = *(float       *)dst;return 0;
    case AV_OPT_TYPE_DOUBLE:    *num    = *(double      *)dst;return 0;
    case AV_OPT_TYPE_RATIONAL:  *intnum = ((AVRational*)dst)->num;
                                *den    = ((AVRational*)dst)->den;
                                                        return 0;
    case AV_OPT_TYPE_CONST:     *num    = o->default_val.dbl; return 0;
    }
    return AVERROR(EINVAL);
}

static int write_number(void *obj, const AVOption *o, void *dst, double num, int den, int64_t intnum)
{
    if (o->max*den < num*intnum || o->min*den > num*intnum) {
        av_log(obj, AV_LOG_ERROR, "Value %f for parameter '%s' out of range\n", num*intnum/den, o->name);
        return AVERROR(ERANGE);
    }

    switch (o->type) {
    case AV_OPT_TYPE_FLAGS:
    case AV_OPT_TYPE_INT:   *(int       *)dst= llrint(num/den)*intnum; break;
    case AV_OPT_TYPE_INT64: *(int64_t   *)dst= llrint(num/den)*intnum; break;
    case AV_OPT_TYPE_FLOAT: *(float     *)dst= num*intnum/den;         break;
    case AV_OPT_TYPE_DOUBLE:*(double    *)dst= num*intnum/den;         break;
    case AV_OPT_TYPE_RATIONAL:
        if ((int)num == num) *(AVRational*)dst= (AVRational){num*intnum, den};
        else                 *(AVRational*)dst= av_d2q(num*intnum/den, 1<<24);
        break;
    default:
        return AVERROR(EINVAL);
    }
    return 0;
}

static const double const_values[] = {
    M_PI,
    M_E,
    FF_QP2LAMBDA,
    0
};

static const char * const const_names[] = {
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

static int set_string_binary(void *obj, const AVOption *o, const char *val, uint8_t **dst)
{
    int *lendst = (int *)(dst + 1);
    uint8_t *bin, *ptr;
    int len = strlen(val);

    av_freep(dst);
    *lendst = 0;

    if (len & 1)
        return AVERROR(EINVAL);
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

static int set_string(void *obj, const AVOption *o, const char *val, uint8_t **dst)
{
    av_freep(dst);
    *dst = av_strdup(val);
    return 0;
}

static int set_string_number(void *obj, const AVOption *o, const char *val, void *dst)
{
    int ret = 0, notfirst = 0;
    for (;;) {
        int i, den = 1;
        char buf[256];
        int cmd = 0;
        double d, num = 1;
        int64_t intnum = 1;

        if (*val == '+' || *val == '-')
            cmd = *(val++);

        for (i = 0; i < sizeof(buf) - 1 && val[i] && val[i] != '+' && val[i] != '-'; i++)
            buf[i] = val[i];
        buf[i] = 0;

        {
            const AVOption *o_named = av_opt_find(obj, buf, o->unit, 0, 0);
            if (o_named && o_named->type == AV_OPT_TYPE_CONST)
                d = o_named->default_val.dbl;
            else if (!strcmp(buf, "default")) d = o->default_val.dbl;
            else if (!strcmp(buf, "max"    )) d = o->max;
            else if (!strcmp(buf, "min"    )) d = o->min;
            else if (!strcmp(buf, "none"   )) d = 0;
            else if (!strcmp(buf, "all"    )) d = ~0;
            else {
                int res = av_expr_parse_and_eval(&d, buf, const_names, const_values, NULL, NULL, NULL, NULL, NULL, 0, obj);
                if (res < 0) {
                    av_log(obj, AV_LOG_ERROR, "Unable to parse option value \"%s\"\n", val);
                    return res;
                }
            }
        }
        if (o->type == AV_OPT_TYPE_FLAGS) {
            read_number(o, dst, NULL, NULL, &intnum);
            if      (cmd == '+') d = intnum | (int64_t)d;
            else if (cmd == '-') d = intnum &~(int64_t)d;
        } else {
            read_number(o, dst, &num, &den, &intnum);
            if      (cmd == '+') d = notfirst*num*intnum/den + d;
            else if (cmd == '-') d = notfirst*num*intnum/den - d;
        }

        if ((ret = write_number(obj, o, dst, d, 1, 1)) < 0)
            return ret;
        val += i;
        if (!*val)
            return 0;
        notfirst = 1;
    }

    return 0;
}

#if FF_API_OLD_AVOPTIONS
int av_set_string3(void *obj, const char *name, const char *val, int alloc, const AVOption **o_out)
{
    const AVOption *o = av_opt_find(obj, name, NULL, 0, 0);
    if (o_out)
        *o_out = o;
    return av_opt_set(obj, name, val, 0);
}
#endif

int av_opt_set(void *obj, const char *name, const char *val, int search_flags)
{
    void *dst, *target_obj;
    const AVOption *o = av_opt_find2(obj, name, NULL, 0, search_flags, &target_obj);
    if (!o || !target_obj)
        return AVERROR_OPTION_NOT_FOUND;
    if (!val && o->type != AV_OPT_TYPE_STRING)
        return AVERROR(EINVAL);

    dst = ((uint8_t*)target_obj) + o->offset;
    switch (o->type) {
    case AV_OPT_TYPE_STRING:   return set_string(obj, o, val, dst);
    case AV_OPT_TYPE_BINARY:   return set_string_binary(obj, o, val, dst);
    case AV_OPT_TYPE_FLAGS:
    case AV_OPT_TYPE_INT:
    case AV_OPT_TYPE_INT64:
    case AV_OPT_TYPE_FLOAT:
    case AV_OPT_TYPE_DOUBLE:
    case AV_OPT_TYPE_RATIONAL: return set_string_number(obj, o, val, dst);
    }

    av_log(obj, AV_LOG_ERROR, "Invalid option type.\n");
    return AVERROR(EINVAL);
}

#define OPT_EVAL_NUMBER(name, opttype, vartype)\
    int av_opt_eval_ ## name(void *obj, const AVOption *o, const char *val, vartype *name ## _out)\
    {\
        if (!o || o->type != opttype)\
            return AVERROR(EINVAL);\
        return set_string_number(obj, o, val, name ## _out);\
    }

OPT_EVAL_NUMBER(flags,  AV_OPT_TYPE_FLAGS,    int)
OPT_EVAL_NUMBER(int,    AV_OPT_TYPE_INT,      int)
OPT_EVAL_NUMBER(int64,  AV_OPT_TYPE_INT64,    int64_t)
OPT_EVAL_NUMBER(float,  AV_OPT_TYPE_FLOAT,    float)
OPT_EVAL_NUMBER(double, AV_OPT_TYPE_DOUBLE,   double)
OPT_EVAL_NUMBER(q,      AV_OPT_TYPE_RATIONAL, AVRational)

static int set_number(void *obj, const char *name, double num, int den, int64_t intnum,
                                  int search_flags)
{
    void *dst, *target_obj;
    const AVOption *o = av_opt_find2(obj, name, NULL, 0, search_flags, &target_obj);

    if (!o || !target_obj)
        return AVERROR_OPTION_NOT_FOUND;

    dst = ((uint8_t*)target_obj) + o->offset;
    return write_number(obj, o, dst, num, den, intnum);
}

#if FF_API_OLD_AVOPTIONS
const AVOption *av_set_double(void *obj, const char *name, double n)
{
    const AVOption *o = av_opt_find(obj, name, NULL, 0, 0);
    if (set_number(obj, name, n, 1, 1, 0) < 0)
        return NULL;
    return o;
}

const AVOption *av_set_q(void *obj, const char *name, AVRational n)
{
    const AVOption *o = av_opt_find(obj, name, NULL, 0, 0);
    if (set_number(obj, name, n.num, n.den, 1, 0) < 0)
        return NULL;
    return o;
}

const AVOption *av_set_int(void *obj, const char *name, int64_t n)
{
    const AVOption *o = av_opt_find(obj, name, NULL, 0, 0);
    if (set_number(obj, name, 1, 1, n, 0) < 0)
        return NULL;
    return o;
}
#endif

int av_opt_set_int(void *obj, const char *name, int64_t val, int search_flags)
{
    return set_number(obj, name, 1, 1, val, search_flags);
}

int av_opt_set_double(void *obj, const char *name, double val, int search_flags)
{
    return set_number(obj, name, val, 1, 1, search_flags);
}

int av_opt_set_q(void *obj, const char *name, AVRational val, int search_flags)
{
    return set_number(obj, name, val.num, val.den, 1, search_flags);
}

#if FF_API_OLD_AVOPTIONS
/**
 *
 * @param buf a buffer which is used for returning non string values as strings, can be NULL
 * @param buf_len allocated length in bytes of buf
 */
const char *av_get_string(void *obj, const char *name, const AVOption **o_out, char *buf, int buf_len)
{
    const AVOption *o = av_opt_find(obj, name, NULL, 0, AV_OPT_SEARCH_CHILDREN);
    void *dst;
    uint8_t *bin;
    int len, i;
    if (!o)
        return NULL;
    if (o->type != AV_OPT_TYPE_STRING && (!buf || !buf_len))
        return NULL;

    dst= ((uint8_t*)obj) + o->offset;
    if (o_out) *o_out= o;

    switch (o->type) {
    case AV_OPT_TYPE_FLAGS:     snprintf(buf, buf_len, "0x%08X",*(int    *)dst);break;
    case AV_OPT_TYPE_INT:       snprintf(buf, buf_len, "%d" , *(int    *)dst);break;
    case AV_OPT_TYPE_INT64:     snprintf(buf, buf_len, "%"PRId64, *(int64_t*)dst);break;
    case AV_OPT_TYPE_FLOAT:     snprintf(buf, buf_len, "%f" , *(float  *)dst);break;
    case AV_OPT_TYPE_DOUBLE:    snprintf(buf, buf_len, "%f" , *(double *)dst);break;
    case AV_OPT_TYPE_RATIONAL:  snprintf(buf, buf_len, "%d/%d", ((AVRational*)dst)->num, ((AVRational*)dst)->den);break;
    case AV_OPT_TYPE_CONST:     snprintf(buf, buf_len, "%f" , o->default_val.dbl);break;
    case AV_OPT_TYPE_STRING:    return *(void**)dst;
    case AV_OPT_TYPE_BINARY:
        len = *(int*)(((uint8_t *)dst) + sizeof(uint8_t *));
        if (len >= (buf_len + 1)/2) return NULL;
        bin = *(uint8_t**)dst;
        for (i = 0; i < len; i++) snprintf(buf + i*2, 3, "%02X", bin[i]);
        break;
    default: return NULL;
    }
    return buf;
}
#endif

int av_opt_get(void *obj, const char *name, int search_flags, uint8_t **out_val)
{
    void *dst, *target_obj;
    const AVOption *o = av_opt_find2(obj, name, NULL, 0, search_flags, &target_obj);
    uint8_t *bin, buf[128];
    int len, i, ret;

    if (!o || !target_obj || (o->offset<=0 && o->type != AV_OPT_TYPE_CONST))
        return AVERROR_OPTION_NOT_FOUND;

    dst = (uint8_t*)target_obj + o->offset;

    buf[0] = 0;
    switch (o->type) {
    case AV_OPT_TYPE_FLAGS:     ret = snprintf(buf, sizeof(buf), "0x%08X",  *(int    *)dst);break;
    case AV_OPT_TYPE_INT:       ret = snprintf(buf, sizeof(buf), "%d" ,     *(int    *)dst);break;
    case AV_OPT_TYPE_INT64:     ret = snprintf(buf, sizeof(buf), "%"PRId64, *(int64_t*)dst);break;
    case AV_OPT_TYPE_FLOAT:     ret = snprintf(buf, sizeof(buf), "%f" ,     *(float  *)dst);break;
    case AV_OPT_TYPE_DOUBLE:    ret = snprintf(buf, sizeof(buf), "%f" ,     *(double *)dst);break;
    case AV_OPT_TYPE_RATIONAL:  ret = snprintf(buf, sizeof(buf), "%d/%d",   ((AVRational*)dst)->num, ((AVRational*)dst)->den);break;
    case AV_OPT_TYPE_CONST:     ret = snprintf(buf, sizeof(buf), "%f" ,     o->default_val.dbl);break;
    case AV_OPT_TYPE_STRING:
        if (*(uint8_t**)dst)
            *out_val = av_strdup(*(uint8_t**)dst);
        else
            *out_val = av_strdup("");
        return 0;
    case AV_OPT_TYPE_BINARY:
        len = *(int*)(((uint8_t *)dst) + sizeof(uint8_t *));
        if ((uint64_t)len*2 + 1 > INT_MAX)
            return AVERROR(EINVAL);
        if (!(*out_val = av_malloc(len*2 + 1)))
            return AVERROR(ENOMEM);
        bin = *(uint8_t**)dst;
        for (i = 0; i < len; i++)
            snprintf(*out_val + i*2, 3, "%02X", bin[i]);
        return 0;
    default:
        return AVERROR(EINVAL);
    }

    if (ret >= sizeof(buf))
        return AVERROR(EINVAL);
    *out_val = av_strdup(buf);
    return 0;
}

static int get_number(void *obj, const char *name, const AVOption **o_out, double *num, int *den, int64_t *intnum,
                      int search_flags)
{
    void *dst, *target_obj;
    const AVOption *o = av_opt_find2(obj, name, NULL, 0, search_flags, &target_obj);
    if (!o || !target_obj)
        goto error;

    dst = ((uint8_t*)target_obj) + o->offset;

    if (o_out) *o_out= o;

    return read_number(o, dst, num, den, intnum);

error:
    *den=*intnum=0;
    return -1;
}

#if FF_API_OLD_AVOPTIONS
double av_get_double(void *obj, const char *name, const AVOption **o_out)
{
    int64_t intnum=1;
    double num=1;
    int den=1;

    if (get_number(obj, name, o_out, &num, &den, &intnum, 0) < 0)
        return NAN;
    return num*intnum/den;
}

AVRational av_get_q(void *obj, const char *name, const AVOption **o_out)
{
    int64_t intnum=1;
    double num=1;
    int den=1;

    if (get_number(obj, name, o_out, &num, &den, &intnum, 0) < 0)
        return (AVRational){0, 0};
    if (num == 1.0 && (int)intnum == intnum)
        return (AVRational){intnum, den};
    else
        return av_d2q(num*intnum/den, 1<<24);
}

int64_t av_get_int(void *obj, const char *name, const AVOption **o_out)
{
    int64_t intnum=1;
    double num=1;
    int den=1;

    if (get_number(obj, name, o_out, &num, &den, &intnum, 0) < 0)
        return -1;
    return num*intnum/den;
}
#endif

int av_opt_get_int(void *obj, const char *name, int search_flags, int64_t *out_val)
{
    int64_t intnum = 1;
    double     num = 1;
    int   ret, den = 1;

    if ((ret = get_number(obj, name, NULL, &num, &den, &intnum, search_flags)) < 0)
        return ret;
    *out_val = num*intnum/den;
    return 0;
}

int av_opt_get_double(void *obj, const char *name, int search_flags, double *out_val)
{
    int64_t intnum = 1;
    double     num = 1;
    int   ret, den = 1;

    if ((ret = get_number(obj, name, NULL, &num, &den, &intnum, search_flags)) < 0)
        return ret;
    *out_val = num*intnum/den;
    return 0;
}

int av_opt_get_q(void *obj, const char *name, int search_flags, AVRational *out_val)
{
    int64_t intnum = 1;
    double     num = 1;
    int   ret, den = 1;

    if ((ret = get_number(obj, name, NULL, &num, &den, &intnum, search_flags)) < 0)
        return ret;

    if (num == 1.0 && (int)intnum == intnum)
        *out_val = (AVRational){intnum, den};
    else
        *out_val = av_d2q(num*intnum/den, 1<<24);
    return 0;
}

int av_opt_flag_is_set(void *obj, const char *field_name, const char *flag_name)
{
    const AVOption *field = av_opt_find(obj, field_name, NULL, 0, 0);
    const AVOption *flag  = av_opt_find(obj, flag_name,
                                        field ? field->unit : NULL, 0, 0);
    int64_t res;

    if (!field || !flag || flag->type != AV_OPT_TYPE_CONST ||
        av_opt_get_int(obj, field_name, 0, &res) < 0)
        return 0;
    return res & (int) flag->default_val.dbl;
}

static void opt_list(void *obj, void *av_log_obj, const char *unit,
                     int req_flags, int rej_flags)
{
    const AVOption *opt=NULL;

    while ((opt = av_opt_next(obj, opt))) {
        if (!(opt->flags & req_flags) || (opt->flags & rej_flags))
            continue;

        /* Don't print CONST's on level one.
         * Don't print anything but CONST's on level two.
         * Only print items from the requested unit.
         */
        if (!unit && opt->type==AV_OPT_TYPE_CONST)
            continue;
        else if (unit && opt->type!=AV_OPT_TYPE_CONST)
            continue;
        else if (unit && opt->type==AV_OPT_TYPE_CONST && strcmp(unit, opt->unit))
            continue;
        else if (unit && opt->type == AV_OPT_TYPE_CONST)
            av_log(av_log_obj, AV_LOG_INFO, "   %-15s ", opt->name);
        else
            av_log(av_log_obj, AV_LOG_INFO, "-%-17s ", opt->name);

        switch (opt->type) {
            case AV_OPT_TYPE_FLAGS:
                av_log(av_log_obj, AV_LOG_INFO, "%-7s ", "<flags>");
                break;
            case AV_OPT_TYPE_INT:
                av_log(av_log_obj, AV_LOG_INFO, "%-7s ", "<int>");
                break;
            case AV_OPT_TYPE_INT64:
                av_log(av_log_obj, AV_LOG_INFO, "%-7s ", "<int64>");
                break;
            case AV_OPT_TYPE_DOUBLE:
                av_log(av_log_obj, AV_LOG_INFO, "%-7s ", "<double>");
                break;
            case AV_OPT_TYPE_FLOAT:
                av_log(av_log_obj, AV_LOG_INFO, "%-7s ", "<float>");
                break;
            case AV_OPT_TYPE_STRING:
                av_log(av_log_obj, AV_LOG_INFO, "%-7s ", "<string>");
                break;
            case AV_OPT_TYPE_RATIONAL:
                av_log(av_log_obj, AV_LOG_INFO, "%-7s ", "<rational>");
                break;
            case AV_OPT_TYPE_BINARY:
                av_log(av_log_obj, AV_LOG_INFO, "%-7s ", "<binary>");
                break;
            case AV_OPT_TYPE_CONST:
            default:
                av_log(av_log_obj, AV_LOG_INFO, "%-7s ", "");
                break;
        }
        av_log(av_log_obj, AV_LOG_INFO, "%c", (opt->flags & AV_OPT_FLAG_ENCODING_PARAM) ? 'E' : '.');
        av_log(av_log_obj, AV_LOG_INFO, "%c", (opt->flags & AV_OPT_FLAG_DECODING_PARAM) ? 'D' : '.');
        av_log(av_log_obj, AV_LOG_INFO, "%c", (opt->flags & AV_OPT_FLAG_VIDEO_PARAM   ) ? 'V' : '.');
        av_log(av_log_obj, AV_LOG_INFO, "%c", (opt->flags & AV_OPT_FLAG_AUDIO_PARAM   ) ? 'A' : '.');
        av_log(av_log_obj, AV_LOG_INFO, "%c", (opt->flags & AV_OPT_FLAG_SUBTITLE_PARAM) ? 'S' : '.');

        if (opt->help)
            av_log(av_log_obj, AV_LOG_INFO, " %s", opt->help);
        av_log(av_log_obj, AV_LOG_INFO, "\n");
        if (opt->unit && opt->type != AV_OPT_TYPE_CONST) {
            opt_list(obj, av_log_obj, opt->unit, req_flags, rej_flags);
        }
    }
}

int av_opt_show2(void *obj, void *av_log_obj, int req_flags, int rej_flags)
{
    if (!obj)
        return -1;

    av_log(av_log_obj, AV_LOG_INFO, "%s AVOptions:\n", (*(AVClass**)obj)->class_name);

    opt_list(obj, av_log_obj, NULL, req_flags, rej_flags);

    return 0;
}

void av_opt_set_defaults(void *s)
{
#if FF_API_OLD_AVOPTIONS
    av_opt_set_defaults2(s, 0, 0);
}

void av_opt_set_defaults2(void *s, int mask, int flags)
{
#endif
    const AVOption *opt = NULL;
    while ((opt = av_opt_next(s, opt)) != NULL) {
#if FF_API_OLD_AVOPTIONS
        if ((opt->flags & mask) != flags)
            continue;
#endif
        switch (opt->type) {
            case AV_OPT_TYPE_CONST:
                /* Nothing to be done here */
            break;
            case AV_OPT_TYPE_FLAGS:
            case AV_OPT_TYPE_INT: {
                int val;
                val = opt->default_val.dbl;
                av_opt_set_int(s, opt->name, val, 0);
            }
            break;
            case AV_OPT_TYPE_INT64:
                if ((double)(opt->default_val.dbl+0.6) == opt->default_val.dbl)
                    av_log(s, AV_LOG_DEBUG, "loss of precision in default of %s\n", opt->name);
                av_opt_set_int(s, opt->name, opt->default_val.dbl, 0);
            break;
            case AV_OPT_TYPE_DOUBLE:
            case AV_OPT_TYPE_FLOAT: {
                double val;
                val = opt->default_val.dbl;
                av_opt_set_double(s, opt->name, val, 0);
            }
            break;
            case AV_OPT_TYPE_RATIONAL: {
                AVRational val;
                val = av_d2q(opt->default_val.dbl, INT_MAX);
                av_opt_set_q(s, opt->name, val, 0);
            }
            break;
            case AV_OPT_TYPE_STRING:
                av_opt_set(s, opt->name, opt->default_val.str, 0);
                break;
            case AV_OPT_TYPE_BINARY:
                /* Cannot set default for binary */
            break;
            default:
                av_log(s, AV_LOG_DEBUG, "AVOption type %d of option %s not implemented yet\n", opt->type, opt->name);
        }
    }
}

/**
 * Store the value in the field in ctx that is named like key.
 * ctx must be an AVClass context, storing is done using AVOptions.
 *
 * @param buf the string to parse, buf will be updated to point at the
 * separator just after the parsed key/value pair
 * @param key_val_sep a 0-terminated list of characters used to
 * separate key from value
 * @param pairs_sep a 0-terminated list of characters used to separate
 * two pairs from each other
 * @return 0 if the key/value pair has been successfully parsed and
 * set, or a negative value corresponding to an AVERROR code in case
 * of error:
 * AVERROR(EINVAL) if the key/value pair cannot be parsed,
 * the error code issued by av_opt_set() if the key/value pair
 * cannot be set
 */
static int parse_key_value_pair(void *ctx, const char **buf,
                                const char *key_val_sep, const char *pairs_sep)
{
    char *key = av_get_token(buf, key_val_sep);
    char *val;
    int ret;

    if (*key && strspn(*buf, key_val_sep)) {
        (*buf)++;
        val = av_get_token(buf, pairs_sep);
    } else {
        av_log(ctx, AV_LOG_ERROR, "Missing key or no key/value separator found after key '%s'\n", key);
        av_free(key);
        return AVERROR(EINVAL);
    }

    av_log(ctx, AV_LOG_DEBUG, "Setting value '%s' for key '%s'\n", val, key);

    ret = av_opt_set(ctx, key, val, 0);
    if (ret == AVERROR_OPTION_NOT_FOUND)
        av_log(ctx, AV_LOG_ERROR, "Key '%s' not found.\n", key);

    av_free(key);
    av_free(val);
    return ret;
}

int av_set_options_string(void *ctx, const char *opts,
                          const char *key_val_sep, const char *pairs_sep)
{
    int ret, count = 0;

    if (!opts)
        return 0;

    while (*opts) {
        if ((ret = parse_key_value_pair(ctx, &opts, key_val_sep, pairs_sep)) < 0)
            return ret;
        count++;

        if (*opts)
            opts++;
    }

    return count;
}

void av_opt_free(void *obj)
{
    const AVOption *o = NULL;
    while ((o = av_opt_next(obj, o)))
        if (o->type == AV_OPT_TYPE_STRING || o->type == AV_OPT_TYPE_BINARY)
            av_freep((uint8_t *)obj + o->offset);
}

int av_opt_set_dict(void *obj, AVDictionary **options)
{
    AVDictionaryEntry *t = NULL;
    AVDictionary    *tmp = NULL;
    int ret = 0;

    while ((t = av_dict_get(*options, "", t, AV_DICT_IGNORE_SUFFIX))) {
        ret = av_opt_set(obj, t->key, t->value, 0);
        if (ret == AVERROR_OPTION_NOT_FOUND)
            av_dict_set(&tmp, t->key, t->value, 0);
        else if (ret < 0) {
            av_log(obj, AV_LOG_ERROR, "Error setting option %s to value %s.\n", t->key, t->value);
            break;
        }
        ret = 0;
    }
    av_dict_free(options);
    *options = tmp;
    return ret;
}

const AVOption *av_opt_find(void *obj, const char *name, const char *unit,
                            int opt_flags, int search_flags)
{
    return av_opt_find2(obj, name, unit, opt_flags, search_flags, NULL);
}

const AVOption *av_opt_find2(void *obj, const char *name, const char *unit,
                             int opt_flags, int search_flags, void **target_obj)
{
    const AVClass  *c;
    const AVOption *o = NULL;

    if(!obj)
        return NULL;

    c= *(AVClass**)obj;

    if (search_flags & AV_OPT_SEARCH_CHILDREN) {
        if (search_flags & AV_OPT_SEARCH_FAKE_OBJ) {
            const AVClass *child = NULL;
            while (child = av_opt_child_class_next(c, child))
                if (o = av_opt_find2(&child, name, unit, opt_flags, search_flags, NULL))
                    return o;
        } else {
            void *child = NULL;
            while (child = av_opt_child_next(obj, child))
                if (o = av_opt_find2(child, name, unit, opt_flags, search_flags, target_obj))
                    return o;
        }
    }

    while (o = av_opt_next(obj, o)) {
        if (!strcmp(o->name, name) && (o->flags & opt_flags) == opt_flags &&
            ((!unit && o->type != AV_OPT_TYPE_CONST) ||
             (unit  && o->type == AV_OPT_TYPE_CONST && o->unit && !strcmp(o->unit, unit)))) {
            if (target_obj) {
                if (!(search_flags & AV_OPT_SEARCH_FAKE_OBJ))
                    *target_obj = obj;
                else
                    *target_obj = NULL;
            }
            return o;
        }
    }
    return NULL;
}

void *av_opt_child_next(void *obj, void *prev)
{
    const AVClass *c = *(AVClass**)obj;
    if (c->child_next)
        return c->child_next(obj, prev);
    return NULL;
}

const AVClass *av_opt_child_class_next(const AVClass *parent, const AVClass *prev)
{
    if (parent->child_class_next)
        return parent->child_class_next(prev);
    return NULL;
}

void *av_opt_ptr(const AVClass *class, void *obj, const char *name)
{
    const AVOption *opt= av_opt_find2(&class, name, NULL, 0, AV_OPT_SEARCH_FAKE_OBJ, NULL);
    if(!opt)
        return NULL;
    return (uint8_t*)obj + opt->offset;
}

#ifdef TEST

#undef printf

typedef struct TestContext
{
    const AVClass *class;
    int num;
    int toggle;
    char *string;
    int flags;
    AVRational rational;
} TestContext;

#define OFFSET(x) offsetof(TestContext, x)

#define TEST_FLAG_COOL 01
#define TEST_FLAG_LAME 02
#define TEST_FLAG_MU   04

static const AVOption test_options[]= {
{"num",      "set num",        OFFSET(num),      AV_OPT_TYPE_INT,      {0},              0,        100                 },
{"toggle",   "set toggle",     OFFSET(toggle),   AV_OPT_TYPE_INT,      {0},              0,        1                   },
{"rational", "set rational",   OFFSET(rational), AV_OPT_TYPE_RATIONAL, {0},              0,        10                  },
{"string",   "set string",     OFFSET(string),   AV_OPT_TYPE_STRING,   {0},              CHAR_MIN, CHAR_MAX            },
{"flags",    "set flags",      OFFSET(flags),    AV_OPT_TYPE_FLAGS,    {0},              0,        INT_MAX, 0, "flags" },
{"cool",     "set cool flag ", 0,                AV_OPT_TYPE_CONST,    {TEST_FLAG_COOL}, INT_MIN,  INT_MAX, 0, "flags" },
{"lame",     "set lame flag ", 0,                AV_OPT_TYPE_CONST,    {TEST_FLAG_LAME}, INT_MIN,  INT_MAX, 0, "flags" },
{"mu",       "set mu flag ",   0,                AV_OPT_TYPE_CONST,    {TEST_FLAG_MU},   INT_MIN,  INT_MAX, 0, "flags" },
{NULL},
};

static const char *test_get_name(void *ctx)
{
    return "test";
}

static const AVClass test_class = {
    "TestContext",
    test_get_name,
    test_options
};

int main(void)
{
    int i;

    printf("\nTesting av_set_options_string()\n");
    {
        TestContext test_ctx;
        const char *options[] = {
            "",
            ":",
            "=",
            "foo=:",
            ":=foo",
            "=foo",
            "foo=",
            "foo",
            "foo=val",
            "foo==val",
            "toggle=:",
            "string=:",
            "toggle=1 : foo",
            "toggle=100",
            "toggle==1",
            "flags=+mu-lame : num=42: toggle=0",
            "num=42 : string=blahblah",
            "rational=0 : rational=1/2 : rational=1/-1",
            "rational=-1/0",
        };

        test_ctx.class = &test_class;
        av_opt_set_defaults(&test_ctx);
        test_ctx.string = av_strdup("default");

        av_log_set_level(AV_LOG_DEBUG);

        for (i=0; i < FF_ARRAY_ELEMS(options); i++) {
            av_log(&test_ctx, AV_LOG_DEBUG, "Setting options string '%s'\n", options[i]);
            if (av_set_options_string(&test_ctx, options[i], "=", ":") < 0)
                av_log(&test_ctx, AV_LOG_ERROR, "Error setting options string: '%s'\n", options[i]);
            printf("\n");
        }
    }

    return 0;
}

#endif
