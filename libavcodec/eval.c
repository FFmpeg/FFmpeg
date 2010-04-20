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

#include "libavutil/avutil.h"
#include "eval.h"

typedef struct Parser{
    int stack_index;
    char *s;
    const double *const_value;
    const char * const *const_name;          // NULL terminated
    double (* const *func1)(void *, double a);           // NULL terminated
    const char * const *func1_name;          // NULL terminated
    double (* const *func2)(void *, double a, double b); // NULL terminated
    const char * const *func2_name;          // NULL terminated
    void *opaque;
    const char **error;
#define VARS 10
    double var[VARS];
} Parser;

static const int8_t si_prefixes['z' - 'E' + 1]={
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

double av_strtod(const char *numstr, char **tail) {
    double d;
    char *next;
    d = strtod(numstr, &next);
    /* if parsing succeeded, check for and interpret postfixes */
    if (next!=numstr) {

        if(*next >= 'E' && *next <= 'z'){
            int e= si_prefixes[*next - 'E'];
            if(e){
                if(next[1] == 'i'){
                    d*= pow( 2, e/0.3);
                    next+=2;
                }else{
                    d*= pow(10, e);
                    next++;
                }
            }
        }

        if(*next=='B') {
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

static int strmatch(const char *s, const char *prefix){
    int i;
    for(i=0; prefix[i]; i++){
        if(prefix[i] != s[i]) return 0;
    }
    return 1;
}

struct AVExpr {
    enum {
        e_value, e_const, e_func0, e_func1, e_func2,
        e_squish, e_gauss, e_ld,
        e_mod, e_max, e_min, e_eq, e_gt, e_gte,
        e_pow, e_mul, e_div, e_add,
        e_last, e_st, e_while,
    } type;
    double value; // is sign in other types
    union {
        int const_index;
        double (*func0)(double);
        double (*func1)(void *, double);
        double (*func2)(void *, double, double);
    } a;
    struct AVExpr *param[2];
};

static double eval_expr(Parser * p, AVExpr * e) {
    switch (e->type) {
        case e_value:  return e->value;
        case e_const:  return e->value * p->const_value[e->a.const_index];
        case e_func0:  return e->value * e->a.func0(eval_expr(p, e->param[0]));
        case e_func1:  return e->value * e->a.func1(p->opaque, eval_expr(p, e->param[0]));
        case e_func2:  return e->value * e->a.func2(p->opaque, eval_expr(p, e->param[0]), eval_expr(p, e->param[1]));
        case e_squish: return 1/(1+exp(4*eval_expr(p, e->param[0])));
        case e_gauss: { double d = eval_expr(p, e->param[0]); return exp(-d*d/2)/sqrt(2*M_PI); }
        case e_ld:     return e->value * p->var[av_clip(eval_expr(p, e->param[0]), 0, VARS-1)];
        case e_while: {
            double d = NAN;
            while(eval_expr(p, e->param[0]))
                d=eval_expr(p, e->param[1]);
            return d;
        }
        default: {
            double d = eval_expr(p, e->param[0]);
            double d2 = eval_expr(p, e->param[1]);
            switch (e->type) {
                case e_mod: return e->value * (d - floor(d/d2)*d2);
                case e_max: return e->value * (d >  d2 ?   d : d2);
                case e_min: return e->value * (d <  d2 ?   d : d2);
                case e_eq:  return e->value * (d == d2 ? 1.0 : 0.0);
                case e_gt:  return e->value * (d >  d2 ? 1.0 : 0.0);
                case e_gte: return e->value * (d >= d2 ? 1.0 : 0.0);
                case e_pow: return e->value * pow(d, d2);
                case e_mul: return e->value * (d * d2);
                case e_div: return e->value * (d / d2);
                case e_add: return e->value * (d + d2);
                case e_last:return e->value * d2;
                case e_st : return e->value * (p->var[av_clip(d, 0, VARS-1)]= d2);
            }
        }
    }
    return NAN;
}

static AVExpr * parse_expr(Parser *p);

void ff_free_expr(AVExpr * e) {
    if (!e) return;
    ff_free_expr(e->param[0]);
    ff_free_expr(e->param[1]);
    av_freep(&e);
}

static AVExpr * parse_primary(Parser *p) {
    AVExpr * d = av_mallocz(sizeof(AVExpr));
    char *next= p->s;
    int i;

    if (!d)
        return NULL;

    /* number */
    d->value = av_strtod(p->s, &next);
    if(next != p->s){
        d->type = e_value;
        p->s= next;
        return d;
    }
    d->value = 1;

    /* named constants */
    for(i=0; p->const_name && p->const_name[i]; i++){
        if(strmatch(p->s, p->const_name[i])){
            p->s+= strlen(p->const_name[i]);
            d->type = e_const;
            d->a.const_index = i;
            return d;
        }
    }

    p->s= strchr(p->s, '(');
    if(p->s==NULL){
        *p->error = "undefined constant or missing (";
        p->s= next;
        ff_free_expr(d);
        return NULL;
    }
    p->s++; // "("
    if (*next == '(') { // special case do-nothing
        av_freep(&d);
        d = parse_expr(p);
        if(p->s[0] != ')'){
            *p->error = "missing )";
            ff_free_expr(d);
            return NULL;
        }
        p->s++; // ")"
        return d;
    }
    d->param[0] = parse_expr(p);
    if(p->s[0]== ','){
        p->s++; // ","
        d->param[1] = parse_expr(p);
    }
    if(p->s[0] != ')'){
        *p->error = "missing )";
        ff_free_expr(d);
        return NULL;
    }
    p->s++; // ")"

    d->type = e_func0;
         if( strmatch(next, "sinh"  ) ) d->a.func0 = sinh;
    else if( strmatch(next, "cosh"  ) ) d->a.func0 = cosh;
    else if( strmatch(next, "tanh"  ) ) d->a.func0 = tanh;
    else if( strmatch(next, "sin"   ) ) d->a.func0 = sin;
    else if( strmatch(next, "cos"   ) ) d->a.func0 = cos;
    else if( strmatch(next, "tan"   ) ) d->a.func0 = tan;
    else if( strmatch(next, "atan"  ) ) d->a.func0 = atan;
    else if( strmatch(next, "asin"  ) ) d->a.func0 = asin;
    else if( strmatch(next, "acos"  ) ) d->a.func0 = acos;
    else if( strmatch(next, "exp"   ) ) d->a.func0 = exp;
    else if( strmatch(next, "log"   ) ) d->a.func0 = log;
    else if( strmatch(next, "abs"   ) ) d->a.func0 = fabs;
    else if( strmatch(next, "squish") ) d->type = e_squish;
    else if( strmatch(next, "gauss" ) ) d->type = e_gauss;
    else if( strmatch(next, "mod"   ) ) d->type = e_mod;
    else if( strmatch(next, "max"   ) ) d->type = e_max;
    else if( strmatch(next, "min"   ) ) d->type = e_min;
    else if( strmatch(next, "eq"    ) ) d->type = e_eq;
    else if( strmatch(next, "gte"   ) ) d->type = e_gte;
    else if( strmatch(next, "gt"    ) ) d->type = e_gt;
    else if( strmatch(next, "lte"   ) ) { AVExpr * tmp = d->param[1]; d->param[1] = d->param[0]; d->param[0] = tmp; d->type = e_gt; }
    else if( strmatch(next, "lt"    ) ) { AVExpr * tmp = d->param[1]; d->param[1] = d->param[0]; d->param[0] = tmp; d->type = e_gte; }
    else if( strmatch(next, "ld"    ) ) d->type = e_ld;
    else if( strmatch(next, "st"    ) ) d->type = e_st;
    else if( strmatch(next, "while" ) ) d->type = e_while;
    else {
        for(i=0; p->func1_name && p->func1_name[i]; i++){
            if(strmatch(next, p->func1_name[i])){
                d->a.func1 = p->func1[i];
                d->type = e_func1;
                return d;
            }
        }

        for(i=0; p->func2_name && p->func2_name[i]; i++){
            if(strmatch(next, p->func2_name[i])){
                d->a.func2 = p->func2[i];
                d->type = e_func2;
                return d;
            }
        }

        *p->error = "unknown function";
        ff_free_expr(d);
        return NULL;
    }

    return d;
}

static AVExpr * new_eval_expr(int type, int value, AVExpr *p0, AVExpr *p1){
    AVExpr * e = av_mallocz(sizeof(AVExpr));
    if (!e)
        return NULL;
    e->type     =type   ;
    e->value    =value  ;
    e->param[0] =p0     ;
    e->param[1] =p1     ;
    return e;
}

static AVExpr * parse_pow(Parser *p, int *sign){
    *sign= (*p->s == '+') - (*p->s == '-');
    p->s += *sign&1;
    return parse_primary(p);
}

static AVExpr * parse_factor(Parser *p){
    int sign, sign2;
    AVExpr * e = parse_pow(p, &sign);
    while(p->s[0]=='^'){
        p->s++;
        e= new_eval_expr(e_pow, 1, e, parse_pow(p, &sign2));
        if (!e)
            return NULL;
        if (e->param[1]) e->param[1]->value *= (sign2|1);
    }
    if (e) e->value *= (sign|1);
    return e;
}

static AVExpr * parse_term(Parser *p){
    AVExpr * e = parse_factor(p);
    while(p->s[0]=='*' || p->s[0]=='/'){
        int c= *p->s++;
        e= new_eval_expr(c == '*' ? e_mul : e_div, 1, e, parse_factor(p));
        if (!e)
            return NULL;
    }
    return e;
}

static AVExpr * parse_subexpr(Parser *p) {
    AVExpr * e = parse_term(p);
    while(*p->s == '+' || *p->s == '-') {
        e= new_eval_expr(e_add, 1, e, parse_term(p));
        if (!e)
            return NULL;
    };

    return e;
}

static AVExpr * parse_expr(Parser *p) {
    AVExpr * e;

    if(p->stack_index <= 0) //protect against stack overflows
        return NULL;
    p->stack_index--;

    e = parse_subexpr(p);

    while(*p->s == ';') {
        p->s++;
        e= new_eval_expr(e_last, 1, e, parse_subexpr(p));
        if (!e)
            return NULL;
    };

    p->stack_index++;

    return e;
}

static int verify_expr(AVExpr * e) {
    if (!e) return 0;
    switch (e->type) {
        case e_value:
        case e_const: return 1;
        case e_func0:
        case e_func1:
        case e_squish:
        case e_ld:
        case e_gauss: return verify_expr(e->param[0]);
        default: return verify_expr(e->param[0]) && verify_expr(e->param[1]);
    }
}

AVExpr *ff_parse_expr(const char *s, const char * const *const_name,
               double (* const *func1)(void *, double), const char * const *func1_name,
               double (* const *func2)(void *, double, double), const char * const *func2_name,
               const char **error){
    Parser p;
    AVExpr *e = NULL;
    char *w = av_malloc(strlen(s) + 1);
    char *wp = w;

    if (!w)
        goto end;

    while (*s)
        if (!isspace(*s++)) *wp++ = s[-1];
    *wp++ = 0;

    p.stack_index=100;
    p.s= w;
    p.const_name = const_name;
    p.func1      = func1;
    p.func1_name = func1_name;
    p.func2      = func2;
    p.func2_name = func2_name;
    p.error= error;

    e = parse_expr(&p);
    if (!verify_expr(e)) {
        ff_free_expr(e);
        e = NULL;
    }
end:
    av_free(w);
    return e;
}

double ff_eval_expr(AVExpr * e, const double *const_value, void *opaque) {
    Parser p;

    p.const_value= const_value;
    p.opaque     = opaque;
    return eval_expr(&p, e);
}

double ff_parse_and_eval_expr(const char *s, const double *const_value, const char * const *const_name,
               double (* const *func1)(void *, double), const char * const *func1_name,
               double (* const *func2)(void *, double, double), const char * const *func2_name,
               void *opaque, const char **error){
    AVExpr * e = ff_parse_expr(s, const_name, func1, func1_name, func2, func2_name, error);
    double d;
    if (!e) return NAN;
    d = ff_eval_expr(e, const_value, opaque);
    ff_free_expr(e);
    return d;
}

#ifdef TEST
#undef printf
static double const_values[]={
    M_PI,
    M_E,
    0
};
static const char *const_names[]={
    "PI",
    "E",
    0
};
int main(void){
    int i;
    printf("%f == 12.7\n", ff_parse_and_eval_expr("1+(5-2)^(3-1)+1/2+sin(PI)-max(-2.2,-3.1)", const_values, const_names, NULL, NULL, NULL, NULL, NULL, NULL));
    printf("%f == 0.931322575\n", ff_parse_and_eval_expr("80G/80Gi", const_values, const_names, NULL, NULL, NULL, NULL, NULL, NULL));

    for(i=0; i<1050; i++){
        START_TIMER
            ff_parse_and_eval_expr("1+(5-2)^(3-1)+1/2+sin(PI)-max(-2.2,-3.1)", const_values, const_names, NULL, NULL, NULL, NULL, NULL, NULL);
        STOP_TIMER("ff_parse_and_eval_expr")
    }
    return 0;
}
#endif
