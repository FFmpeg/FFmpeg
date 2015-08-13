/*
 * Copyright (c) 2002-2006 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2006 Oded Shimon <ods15@ods15.dyndns.org>
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
 * simple arithmetic expression evaluator.
 *
 * see http://joe.hotchkiss.com/programming/eval/eval.html
 */

#include <float.h>
#include "attributes.h"
#include "avutil.h"
#include "common.h"
#include "eval.h"
#include "internal.h"
#include "log.h"
#include "mathematics.h"
#include "time.h"
#include "avstring.h"
#include "timer.h"

typedef struct Parser {
    const AVClass *class;
    int stack_index;
    char *s;
    const double *const_values;
    const char * const *const_names;          // NULL terminated
    double (* const *funcs1)(void *, double a);           // NULL terminated
    const char * const *func1_names;          // NULL terminated
    double (* const *funcs2)(void *, double a, double b); // NULL terminated
    const char * const *func2_names;          // NULL terminated
    void *opaque;
    int log_offset;
    void *log_ctx;
#define VARS 10
    double *var;
} Parser;

static const AVClass eval_class = { "Eval", av_default_item_name, NULL, LIBAVUTIL_VERSION_INT, offsetof(Parser,log_offset), offsetof(Parser,log_ctx) };

static const int8_t si_prefixes['z' - 'E' + 1] = {
    ['y'-'E']= -24,
    ['z'-'E']= -21,
    ['a'-'E']= -18,
    ['f'-'E']= -15,
    ['p'-'E']= -12,
    ['n'-'E']= - 9,
    ['u'-'E']= - 6,
    ['m'-'E']= - 3,
    ['c'-'E']= - 2,
    ['d'-'E']= - 1,
    ['h'-'E']=   2,
    ['k'-'E']=   3,
    ['K'-'E']=   3,
    ['M'-'E']=   6,
    ['G'-'E']=   9,
    ['T'-'E']=  12,
    ['P'-'E']=  15,
    ['E'-'E']=  18,
    ['Z'-'E']=  21,
    ['Y'-'E']=  24,
};

static const struct {
    const char *name;
    double value;
} constants[] = {
    { "E",   M_E   },
    { "PI",  M_PI  },
    { "PHI", M_PHI },
    { "QP2LAMBDA", FF_QP2LAMBDA },
};

double av_strtod(const char *numstr, char **tail)
{
    double d;
    char *next;
    if(numstr[0]=='0' && (numstr[1]|0x20)=='x') {
        d = strtoul(numstr, &next, 16);
    } else
        d = strtod(numstr, &next);
    /* if parsing succeeded, check for and interpret postfixes */
    if (next!=numstr) {
        if (next[0] == 'd' && next[1] == 'B') {
            /* treat dB as decibels instead of decibytes */
            d = pow(10, d / 20);
            next += 2;
        } else if (*next >= 'E' && *next <= 'z') {
            int e= si_prefixes[*next - 'E'];
            if (e) {
                if (next[1] == 'i') {
                    d*= pow( 2, e/0.3);
                    next+=2;
                } else {
                    d*= pow(10, e);
                    next++;
                }
            }
        }

        if (*next=='B') {
            d*=8;
            next++;
        }
    }
    /* if requested, fill in tail with the position after the last parsed
       character */
    if (tail)
        *tail = next;
    return d;
}

#define IS_IDENTIFIER_CHAR(c) ((c) - '0' <= 9U || (c) - 'a' <= 25U || (c) - 'A' <= 25U || (c) == '_')

static int strmatch(const char *s, const char *prefix)
{
    int i;
    for (i=0; prefix[i]; i++) {
        if (prefix[i] != s[i]) return 0;
    }
    /* return 1 only if the s identifier is terminated */
    return !IS_IDENTIFIER_CHAR(s[i]);
}

struct AVExpr {
    enum {
        e_value, e_const, e_func0, e_func1, e_func2,
        e_squish, e_gauss, e_ld, e_isnan, e_isinf,
        e_mod, e_max, e_min, e_eq, e_gt, e_gte, e_lte, e_lt,
        e_pow, e_mul, e_div, e_add,
        e_last, e_st, e_while, e_taylor, e_root, e_floor, e_ceil, e_trunc,
        e_sqrt, e_not, e_random, e_hypot, e_gcd,
        e_if, e_ifnot, e_print, e_bitand, e_bitor, e_between, e_clip
    } type;
    double value; // is sign in other types
    union {
        int const_index;
        double (*func0)(double);
        double (*func1)(void *, double);
        double (*func2)(void *, double, double);
    } a;
    struct AVExpr *param[3];
    double *var;
};

static double etime(double v)
{
    return av_gettime() * 0.000001;
}

static double eval_expr(Parser *p, AVExpr *e)
{
    switch (e->type) {
        case e_value:  return e->value;
        case e_const:  return e->value * p->const_values[e->a.const_index];
        case e_func0:  return e->value * e->a.func0(eval_expr(p, e->param[0]));
        case e_func1:  return e->value * e->a.func1(p->opaque, eval_expr(p, e->param[0]));
        case e_func2:  return e->value * e->a.func2(p->opaque, eval_expr(p, e->param[0]), eval_expr(p, e->param[1]));
        case e_squish: return 1/(1+exp(4*eval_expr(p, e->param[0])));
        case e_gauss: { double d = eval_expr(p, e->param[0]); return exp(-d*d/2)/sqrt(2*M_PI); }
        case e_ld:     return e->value * p->var[av_clip(eval_expr(p, e->param[0]), 0, VARS-1)];
        case e_isnan:  return e->value * !!isnan(eval_expr(p, e->param[0]));
        case e_isinf:  return e->value * !!isinf(eval_expr(p, e->param[0]));
        case e_floor:  return e->value * floor(eval_expr(p, e->param[0]));
        case e_ceil :  return e->value * ceil (eval_expr(p, e->param[0]));
        case e_trunc:  return e->value * trunc(eval_expr(p, e->param[0]));
        case e_sqrt:   return e->value * sqrt (eval_expr(p, e->param[0]));
        case e_not:    return e->value * (eval_expr(p, e->param[0]) == 0);
        case e_if:     return e->value * (eval_expr(p, e->param[0]) ? eval_expr(p, e->param[1]) :
                                          e->param[2] ? eval_expr(p, e->param[2]) : 0);
        case e_ifnot:  return e->value * (!eval_expr(p, e->param[0]) ? eval_expr(p, e->param[1]) :
                                          e->param[2] ? eval_expr(p, e->param[2]) : 0);
        case e_clip: {
            double x = eval_expr(p, e->param[0]);
            double min = eval_expr(p, e->param[1]), max = eval_expr(p, e->param[2]);
            if (isnan(min) || isnan(max) || isnan(x) || min > max)
                return NAN;
            return e->value * av_clipd(eval_expr(p, e->param[0]), min, max);
        }
        case e_between: {
            double d = eval_expr(p, e->param[0]);
            return e->value * (d >= eval_expr(p, e->param[1]) &&
                               d <= eval_expr(p, e->param[2]));
        }
        case e_print: {
            double x = eval_expr(p, e->param[0]);
            int level = e->param[1] ? av_clip(eval_expr(p, e->param[1]), INT_MIN, INT_MAX) : AV_LOG_INFO;
            av_log(p, level, "%f\n", x);
            return x;
        }
        case e_random:{
            int idx= av_clip(eval_expr(p, e->param[0]), 0, VARS-1);
            uint64_t r= isnan(p->var[idx]) ? 0 : p->var[idx];
            r= r*1664525+1013904223;
            p->var[idx]= r;
            return e->value * (r * (1.0/UINT64_MAX));
        }
        case e_while: {
            double d = NAN;
            while (eval_expr(p, e->param[0]))
                d=eval_expr(p, e->param[1]);
            return d;
        }
        case e_taylor: {
            double t = 1, d = 0, v;
            double x = eval_expr(p, e->param[1]);
            int id = e->param[2] ? av_clip(eval_expr(p, e->param[2]), 0, VARS-1) : 0;
            int i;
            double var0 = p->var[id];
            for(i=0; i<1000; i++) {
                double ld = d;
                p->var[id] = i;
                v = eval_expr(p, e->param[0]);
                d += t*v;
                if(ld==d && v)
                    break;
                t *= x / (i+1);
            }
            p->var[id] = var0;
            return d;
        }
        case e_root: {
            int i, j;
            double low = -1, high = -1, v, low_v = -DBL_MAX, high_v = DBL_MAX;
            double var0 = p->var[0];
            double x_max = eval_expr(p, e->param[1]);
            for(i=-1; i<1024; i++) {
                if(i<255) {
                    p->var[0] = ff_reverse[i&255]*x_max/255;
                } else {
                    p->var[0] = x_max*pow(0.9, i-255);
                    if (i&1) p->var[0] *= -1;
                    if (i&2) p->var[0] += low;
                    else     p->var[0] += high;
                }
                v = eval_expr(p, e->param[0]);
                if (v<=0 && v>low_v) {
                    low    = p->var[0];
                    low_v  = v;
                }
                if (v>=0 && v<high_v) {
                    high   = p->var[0];
                    high_v = v;
                }
                if (low>=0 && high>=0){
                    for (j=0; j<1000; j++) {
                        p->var[0] = (low+high)*0.5;
                        if (low == p->var[0] || high == p->var[0])
                            break;
                        v = eval_expr(p, e->param[0]);
                        if (v<=0) low = p->var[0];
                        if (v>=0) high= p->var[0];
                        if (isnan(v)) {
                            low = high = v;
                            break;
                        }
                    }
                    break;
                }
            }
            p->var[0] = var0;
            return -low_v<high_v ? low : high;
        }
        default: {
            double d = eval_expr(p, e->param[0]);
            double d2 = eval_expr(p, e->param[1]);
            switch (e->type) {
                case e_mod: return e->value * (d - floor((!CONFIG_FTRAPV || d2) ? d / d2 : d * INFINITY) * d2);
                case e_gcd: return e->value * av_gcd(d,d2);
                case e_max: return e->value * (d >  d2 ?   d : d2);
                case e_min: return e->value * (d <  d2 ?   d : d2);
                case e_eq:  return e->value * (d == d2 ? 1.0 : 0.0);
                case e_gt:  return e->value * (d >  d2 ? 1.0 : 0.0);
                case e_gte: return e->value * (d >= d2 ? 1.0 : 0.0);
                case e_lt:  return e->value * (d <  d2 ? 1.0 : 0.0);
                case e_lte: return e->value * (d <= d2 ? 1.0 : 0.0);
                case e_pow: return e->value * pow(d, d2);
                case e_mul: return e->value * (d * d2);
                case e_div: return e->value * ((!CONFIG_FTRAPV || d2 ) ? (d / d2) : d * INFINITY);
                case e_add: return e->value * (d + d2);
                case e_last:return e->value * d2;
                case e_st : return e->value * (p->var[av_clip(d, 0, VARS-1)]= d2);
                case e_hypot:return e->value * (sqrt(d*d + d2*d2));
                case e_bitand: return isnan(d) || isnan(d2) ? NAN : e->value * ((long int)d & (long int)d2);
                case e_bitor:  return isnan(d) || isnan(d2) ? NAN : e->value * ((long int)d | (long int)d2);
            }
        }
    }
    return NAN;
}

static int parse_expr(AVExpr **e, Parser *p);

void av_expr_free(AVExpr *e)
{
    if (!e) return;
    av_expr_free(e->param[0]);
    av_expr_free(e->param[1]);
    av_expr_free(e->param[2]);
    av_freep(&e->var);
    av_freep(&e);
}

static int parse_primary(AVExpr **e, Parser *p)
{
    AVExpr *d = av_mallocz(sizeof(AVExpr));
    char *next = p->s, *s0 = p->s;
    int ret, i;

    if (!d)
        return AVERROR(ENOMEM);

    /* number */
    d->value = av_strtod(p->s, &next);
    if (next != p->s) {
        d->type = e_value;
        p->s= next;
        *e = d;
        return 0;
    }
    d->value = 1;

    /* named constants */
    for (i=0; p->const_names && p->const_names[i]; i++) {
        if (strmatch(p->s, p->const_names[i])) {
            p->s+= strlen(p->const_names[i]);
            d->type = e_const;
            d->a.const_index = i;
            *e = d;
            return 0;
        }
    }
    for (i = 0; i < FF_ARRAY_ELEMS(constants); i++) {
        if (strmatch(p->s, constants[i].name)) {
            p->s += strlen(constants[i].name);
            d->type = e_value;
            d->value = constants[i].value;
            *e = d;
            return 0;
        }
    }

    p->s= strchr(p->s, '(');
    if (!p->s) {
        av_log(p, AV_LOG_ERROR, "Undefined constant or missing '(' in '%s'\n", s0);
        p->s= next;
        av_expr_free(d);
        return AVERROR(EINVAL);
    }
    p->s++; // "("
    if (*next == '(') { // special case do-nothing
        av_freep(&d);
        if ((ret = parse_expr(&d, p)) < 0)
            return ret;
        if (p->s[0] != ')') {
            av_log(p, AV_LOG_ERROR, "Missing ')' in '%s'\n", s0);
            av_expr_free(d);
            return AVERROR(EINVAL);
        }
        p->s++; // ")"
        *e = d;
        return 0;
    }
    if ((ret = parse_expr(&(d->param[0]), p)) < 0) {
        av_expr_free(d);
        return ret;
    }
    if (p->s[0]== ',') {
        p->s++; // ","
        parse_expr(&d->param[1], p);
    }
    if (p->s[0]== ',') {
        p->s++; // ","
        parse_expr(&d->param[2], p);
    }
    if (p->s[0] != ')') {
        av_log(p, AV_LOG_ERROR, "Missing ')' or too many args in '%s'\n", s0);
        av_expr_free(d);
        return AVERROR(EINVAL);
    }
    p->s++; // ")"

    d->type = e_func0;
         if (strmatch(next, "sinh"  )) d->a.func0 = sinh;
    else if (strmatch(next, "cosh"  )) d->a.func0 = cosh;
    else if (strmatch(next, "tanh"  )) d->a.func0 = tanh;
    else if (strmatch(next, "sin"   )) d->a.func0 = sin;
    else if (strmatch(next, "cos"   )) d->a.func0 = cos;
    else if (strmatch(next, "tan"   )) d->a.func0 = tan;
    else if (strmatch(next, "atan"  )) d->a.func0 = atan;
    else if (strmatch(next, "asin"  )) d->a.func0 = asin;
    else if (strmatch(next, "acos"  )) d->a.func0 = acos;
    else if (strmatch(next, "exp"   )) d->a.func0 = exp;
    else if (strmatch(next, "log"   )) d->a.func0 = log;
    else if (strmatch(next, "abs"   )) d->a.func0 = fabs;
    else if (strmatch(next, "time"  )) d->a.func0 = etime;
    else if (strmatch(next, "squish")) d->type = e_squish;
    else if (strmatch(next, "gauss" )) d->type = e_gauss;
    else if (strmatch(next, "mod"   )) d->type = e_mod;
    else if (strmatch(next, "max"   )) d->type = e_max;
    else if (strmatch(next, "min"   )) d->type = e_min;
    else if (strmatch(next, "eq"    )) d->type = e_eq;
    else if (strmatch(next, "gte"   )) d->type = e_gte;
    else if (strmatch(next, "gt"    )) d->type = e_gt;
    else if (strmatch(next, "lte"   )) d->type = e_lte;
    else if (strmatch(next, "lt"    )) d->type = e_lt;
    else if (strmatch(next, "ld"    )) d->type = e_ld;
    else if (strmatch(next, "isnan" )) d->type = e_isnan;
    else if (strmatch(next, "isinf" )) d->type = e_isinf;
    else if (strmatch(next, "st"    )) d->type = e_st;
    else if (strmatch(next, "while" )) d->type = e_while;
    else if (strmatch(next, "taylor")) d->type = e_taylor;
    else if (strmatch(next, "root"  )) d->type = e_root;
    else if (strmatch(next, "floor" )) d->type = e_floor;
    else if (strmatch(next, "ceil"  )) d->type = e_ceil;
    else if (strmatch(next, "trunc" )) d->type = e_trunc;
    else if (strmatch(next, "sqrt"  )) d->type = e_sqrt;
    else if (strmatch(next, "not"   )) d->type = e_not;
    else if (strmatch(next, "pow"   )) d->type = e_pow;
    else if (strmatch(next, "print" )) d->type = e_print;
    else if (strmatch(next, "random")) d->type = e_random;
    else if (strmatch(next, "hypot" )) d->type = e_hypot;
    else if (strmatch(next, "gcd"   )) d->type = e_gcd;
    else if (strmatch(next, "if"    )) d->type = e_if;
    else if (strmatch(next, "ifnot" )) d->type = e_ifnot;
    else if (strmatch(next, "bitand")) d->type = e_bitand;
    else if (strmatch(next, "bitor" )) d->type = e_bitor;
    else if (strmatch(next, "between"))d->type = e_between;
    else if (strmatch(next, "clip"  )) d->type = e_clip;
    else {
        for (i=0; p->func1_names && p->func1_names[i]; i++) {
            if (strmatch(next, p->func1_names[i])) {
                d->a.func1 = p->funcs1[i];
                d->type = e_func1;
                *e = d;
                return 0;
            }
        }

        for (i=0; p->func2_names && p->func2_names[i]; i++) {
            if (strmatch(next, p->func2_names[i])) {
                d->a.func2 = p->funcs2[i];
                d->type = e_func2;
                *e = d;
                return 0;
            }
        }

        av_log(p, AV_LOG_ERROR, "Unknown function in '%s'\n", s0);
        av_expr_free(d);
        return AVERROR(EINVAL);
    }

    *e = d;
    return 0;
}

static AVExpr *make_eval_expr(int type, int value, AVExpr *p0, AVExpr *p1)
{
    AVExpr *e = av_mallocz(sizeof(AVExpr));
    if (!e)
        return NULL;
    e->type     =type   ;
    e->value    =value  ;
    e->param[0] =p0     ;
    e->param[1] =p1     ;
    return e;
}

static int parse_pow(AVExpr **e, Parser *p, int *sign)
{
    *sign= (*p->s == '+') - (*p->s == '-');
    p->s += *sign&1;
    return parse_primary(e, p);
}

static int parse_dB(AVExpr **e, Parser *p, int *sign)
{
    /* do not filter out the negative sign when parsing a dB value.
       for example, -3dB is not the same as -(3dB) */
    if (*p->s == '-') {
        char *next;
        double av_unused ignored = strtod(p->s, &next);
        if (next != p->s && next[0] == 'd' && next[1] == 'B') {
            *sign = 0;
            return parse_primary(e, p);
        }
    }
    return parse_pow(e, p, sign);
}

static int parse_factor(AVExpr **e, Parser *p)
{
    int sign, sign2, ret;
    AVExpr *e0, *e1, *e2;
    if ((ret = parse_dB(&e0, p, &sign)) < 0)
        return ret;
    while(p->s[0]=='^'){
        e1 = e0;
        p->s++;
        if ((ret = parse_dB(&e2, p, &sign2)) < 0) {
            av_expr_free(e1);
            return ret;
        }
        e0 = make_eval_expr(e_pow, 1, e1, e2);
        if (!e0) {
            av_expr_free(e1);
            av_expr_free(e2);
            return AVERROR(ENOMEM);
        }
        if (e0->param[1]) e0->param[1]->value *= (sign2|1);
    }
    if (e0) e0->value *= (sign|1);

    *e = e0;
    return 0;
}

static int parse_term(AVExpr **e, Parser *p)
{
    int ret;
    AVExpr *e0, *e1, *e2;
    if ((ret = parse_factor(&e0, p)) < 0)
        return ret;
    while (p->s[0]=='*' || p->s[0]=='/') {
        int c= *p->s++;
        e1 = e0;
        if ((ret = parse_factor(&e2, p)) < 0) {
            av_expr_free(e1);
            return ret;
        }
        e0 = make_eval_expr(c == '*' ? e_mul : e_div, 1, e1, e2);
        if (!e0) {
            av_expr_free(e1);
            av_expr_free(e2);
            return AVERROR(ENOMEM);
        }
    }
    *e = e0;
    return 0;
}

static int parse_subexpr(AVExpr **e, Parser *p)
{
    int ret;
    AVExpr *e0, *e1, *e2;
    if ((ret = parse_term(&e0, p)) < 0)
        return ret;
    while (*p->s == '+' || *p->s == '-') {
        e1 = e0;
        if ((ret = parse_term(&e2, p)) < 0) {
            av_expr_free(e1);
            return ret;
        }
        e0 = make_eval_expr(e_add, 1, e1, e2);
        if (!e0) {
            av_expr_free(e1);
            av_expr_free(e2);
            return AVERROR(ENOMEM);
        }
    };

    *e = e0;
    return 0;
}

static int parse_expr(AVExpr **e, Parser *p)
{
    int ret;
    AVExpr *e0, *e1, *e2;
    if (p->stack_index <= 0) //protect against stack overflows
        return AVERROR(EINVAL);
    p->stack_index--;

    if ((ret = parse_subexpr(&e0, p)) < 0)
        return ret;
    while (*p->s == ';') {
        p->s++;
        e1 = e0;
        if ((ret = parse_subexpr(&e2, p)) < 0) {
            av_expr_free(e1);
            return ret;
        }
        e0 = make_eval_expr(e_last, 1, e1, e2);
        if (!e0) {
            av_expr_free(e1);
            av_expr_free(e2);
            return AVERROR(ENOMEM);
        }
    };

    p->stack_index++;
    *e = e0;
    return 0;
}

static int verify_expr(AVExpr *e)
{
    if (!e) return 0;
    switch (e->type) {
        case e_value:
        case e_const: return 1;
        case e_func0:
        case e_func1:
        case e_squish:
        case e_ld:
        case e_gauss:
        case e_isnan:
        case e_isinf:
        case e_floor:
        case e_ceil:
        case e_trunc:
        case e_sqrt:
        case e_not:
        case e_random:
            return verify_expr(e->param[0]) && !e->param[1];
        case e_print:
            return verify_expr(e->param[0])
                   && (!e->param[1] || verify_expr(e->param[1]));
        case e_if:
        case e_ifnot:
        case e_taylor:
            return verify_expr(e->param[0]) && verify_expr(e->param[1])
                   && (!e->param[2] || verify_expr(e->param[2]));
        case e_between:
        case e_clip:
            return verify_expr(e->param[0]) &&
                   verify_expr(e->param[1]) &&
                   verify_expr(e->param[2]);
        default: return verify_expr(e->param[0]) && verify_expr(e->param[1]) && !e->param[2];
    }
}

int av_expr_parse(AVExpr **expr, const char *s,
                  const char * const *const_names,
                  const char * const *func1_names, double (* const *funcs1)(void *, double),
                  const char * const *func2_names, double (* const *funcs2)(void *, double, double),
                  int log_offset, void *log_ctx)
{
    Parser p = { 0 };
    AVExpr *e = NULL;
    char *w = av_malloc(strlen(s) + 1);
    char *wp = w;
    const char *s0 = s;
    int ret = 0;

    if (!w)
        return AVERROR(ENOMEM);

    while (*s)
        if (!av_isspace(*s++)) *wp++ = s[-1];
    *wp++ = 0;

    p.class      = &eval_class;
    p.stack_index=100;
    p.s= w;
    p.const_names = const_names;
    p.funcs1      = funcs1;
    p.func1_names = func1_names;
    p.funcs2      = funcs2;
    p.func2_names = func2_names;
    p.log_offset = log_offset;
    p.log_ctx    = log_ctx;

    if ((ret = parse_expr(&e, &p)) < 0)
        goto end;
    if (*p.s) {
        av_log(&p, AV_LOG_ERROR, "Invalid chars '%s' at the end of expression '%s'\n", p.s, s0);
        ret = AVERROR(EINVAL);
        goto end;
    }
    if (!verify_expr(e)) {
        ret = AVERROR(EINVAL);
        goto end;
    }
    e->var= av_mallocz(sizeof(double) *VARS);
    if (!e->var) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    *expr = e;
    e = NULL;
end:
    av_expr_free(e);
    av_free(w);
    return ret;
}

double av_expr_eval(AVExpr *e, const double *const_values, void *opaque)
{
    Parser p = { 0 };
    p.var= e->var;

    p.const_values = const_values;
    p.opaque     = opaque;
    return eval_expr(&p, e);
}

int av_expr_parse_and_eval(double *d, const char *s,
                           const char * const *const_names, const double *const_values,
                           const char * const *func1_names, double (* const *funcs1)(void *, double),
                           const char * const *func2_names, double (* const *funcs2)(void *, double, double),
                           void *opaque, int log_offset, void *log_ctx)
{
    AVExpr *e = NULL;
    int ret = av_expr_parse(&e, s, const_names, func1_names, funcs1, func2_names, funcs2, log_offset, log_ctx);

    if (ret < 0) {
        *d = NAN;
        return ret;
    }
    *d = av_expr_eval(e, const_values, opaque);
    av_expr_free(e);
    return isnan(*d) ? AVERROR(EINVAL) : 0;
}

#ifdef TEST
#include <string.h>

static const double const_values[] = {
    M_PI,
    M_E,
    0
};

static const char *const const_names[] = {
    "PI",
    "E",
    0
};

int main(int argc, char **argv)
{
    int i;
    double d;
    const char *const *expr;
    static const char *const exprs[] = {
        "",
        "1;2",
        "-20",
        "-PI",
        "+PI",
        "1+(5-2)^(3-1)+1/2+sin(PI)-max(-2.2,-3.1)",
        "80G/80Gi",
        "1k",
        "1Gi",
        "1gi",
        "1GiFoo",
        "1k+1k",
        "1Gi*3foo",
        "foo",
        "foo(",
        "foo()",
        "foo)",
        "sin",
        "sin(",
        "sin()",
        "sin)",
        "sin 10",
        "sin(1,2,3)",
        "sin(1 )",
        "1",
        "1foo",
        "bar + PI + E + 100f*2 + foo",
        "13k + 12f - foo(1, 2)",
        "1gi",
        "1Gi",
        "st(0, 123)",
        "st(1, 123); ld(1)",
        "lte(0, 1)",
        "lte(1, 1)",
        "lte(1, 0)",
        "lt(0, 1)",
        "lt(1, 1)",
        "gt(1, 0)",
        "gt(2, 7)",
        "gte(122, 122)",
        /* compute 1+2+...+N */
        "st(0, 1); while(lte(ld(0), 100), st(1, ld(1)+ld(0));st(0, ld(0)+1)); ld(1)",
        /* compute Fib(N) */
        "st(1, 1); st(2, 2); st(0, 1); while(lte(ld(0),10), st(3, ld(1)+ld(2)); st(1, ld(2)); st(2, ld(3)); st(0, ld(0)+1)); ld(3)",
        "while(0, 10)",
        "st(0, 1); while(lte(ld(0),100), st(1, ld(1)+ld(0)); st(0, ld(0)+1))",
        "isnan(1)",
        "isnan(NAN)",
        "isnan(INF)",
        "isinf(1)",
        "isinf(NAN)",
        "isinf(INF)",
        "floor(NAN)",
        "floor(123.123)",
        "floor(-123.123)",
        "trunc(123.123)",
        "trunc(-123.123)",
        "ceil(123.123)",
        "ceil(-123.123)",
        "sqrt(1764)",
        "isnan(sqrt(-1))",
        "not(1)",
        "not(NAN)",
        "not(0)",
        "6.0206dB",
        "-3.0103dB",
        "pow(0,1.23)",
        "pow(PI,1.23)",
        "PI^1.23",
        "pow(-1,1.23)",
        "if(1, 2)",
        "if(1, 1, 2)",
        "if(0, 1, 2)",
        "ifnot(0, 23)",
        "ifnot(1, NaN) + if(0, 1)",
        "ifnot(1, 1, 2)",
        "ifnot(0, 1, 2)",
        "taylor(1, 1)",
        "taylor(eq(mod(ld(1),4),1)-eq(mod(ld(1),4),3), PI/2, 1)",
        "root(sin(ld(0))-1, 2)",
        "root(sin(ld(0))+6+sin(ld(0)/12)-log(ld(0)), 100)",
        "7000000B*random(0)",
        "squish(2)",
        "gauss(0.1)",
        "hypot(4,3)",
        "gcd(30,55)*print(min(9,1))",
        "bitor(42, 12)",
        "bitand(42, 12)",
        "bitand(NAN, 1)",
        "between(10, -3, 10)",
        "between(-4, -2, -1)",
        "between(1,2)",
        "clip(0, 2, 1)",
        "clip(0/0, 1, 2)",
        "clip(0, 0/0, 1)",
        NULL
    };

    for (expr = exprs; *expr; expr++) {
        printf("Evaluating '%s'\n", *expr);
        av_expr_parse_and_eval(&d, *expr,
                               const_names, const_values,
                               NULL, NULL, NULL, NULL, NULL, 0, NULL);
        if (isnan(d))
            printf("'%s' -> nan\n\n", *expr);
        else
            printf("'%s' -> %f\n\n", *expr, d);
    }

    av_expr_parse_and_eval(&d, "1+(5-2)^(3-1)+1/2+sin(PI)-max(-2.2,-3.1)",
                           const_names, const_values,
                           NULL, NULL, NULL, NULL, NULL, 0, NULL);
    printf("%f == 12.7\n", d);
    av_expr_parse_and_eval(&d, "80G/80Gi",
                           const_names, const_values,
                           NULL, NULL, NULL, NULL, NULL, 0, NULL);
    printf("%f == 0.931322575\n", d);

    if (argc > 1 && !strcmp(argv[1], "-t")) {
        for (i = 0; i < 1050; i++) {
            START_TIMER;
            av_expr_parse_and_eval(&d, "1+(5-2)^(3-1)+1/2+sin(PI)-max(-2.2,-3.1)",
                                   const_names, const_values,
                                   NULL, NULL, NULL, NULL, NULL, 0, NULL);
            STOP_TIMER("av_expr_parse_and_eval");
        }
    }

    return 0;
}
#endif
