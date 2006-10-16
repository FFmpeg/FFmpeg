/*
 * simple arithmetic expression evaluator
 *
 * Copyright (c) 2002 Michael Niedermayer <michaelni@gmx.at>
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
 *
 */

/**
 * @file eval.c
 * simple arithmetic expression evaluator.
 *
 * see http://joe.hotchkiss.com/programming/eval/eval.html
 */

#include "avcodec.h"
#include "mpegvideo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef NAN
  #define NAN 0.0/0.0
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct Parser{
    int stack_index;
    char *s;
    double *const_value;
    const char **const_name;          // NULL terminated
    double (**func1)(void *, double a); // NULL terminated
    const char **func1_name;          // NULL terminated
    double (**func2)(void *, double a, double b); // NULL terminated
    char **func2_name;          // NULL terminated
    void *opaque;
    char **error;
} Parser;

static double evalExpression(Parser *p);

static int8_t si_prefixes['z' - 'E' + 1]={
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

/** strtod() function extended with 'k', 'M', 'G', 'ki', 'Mi', 'Gi' and 'B'
 * postfixes.  This allows using f.e. kB, MiB, G and B as a postfix. This
 * function assumes that the unit of numbers is bits not bytes.
 */
static double av_strtod(const char *name, char **tail) {
    double d;
    char *next;
    d = strtod(name, &next);
    /* if parsing succeeded, check for and interpret postfixes */
    if (next!=name) {

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
            *next++;
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

static double evalPrimary(Parser *p){
    double d, d2=NAN;
    char *next= p->s;
    int i;

    /* number */
    d= av_strtod(p->s, &next);
    if(next != p->s){
        p->s= next;
        return d;
    }

    /* named constants */
    for(i=0; p->const_name && p->const_name[i]; i++){
        if(strmatch(p->s, p->const_name[i])){
            p->s+= strlen(p->const_name[i]);
            return p->const_value[i];
        }
    }

    p->s= strchr(p->s, '(');
    if(p->s==NULL){
        *p->error = "missing (";
        p->s= next;
        return NAN;
    }
    p->s++; // "("
    d= evalExpression(p);
    if(p->s[0]== ','){
        p->s++; // ","
        d2= evalExpression(p);
    }
    if(p->s[0] != ')'){
        *p->error = "missing )";
        return NAN;
    }
    p->s++; // ")"

         if( strmatch(next, "sinh"  ) ) d= sinh(d);
    else if( strmatch(next, "cosh"  ) ) d= cosh(d);
    else if( strmatch(next, "tanh"  ) ) d= tanh(d);
    else if( strmatch(next, "sin"   ) ) d= sin(d);
    else if( strmatch(next, "cos"   ) ) d= cos(d);
    else if( strmatch(next, "tan"   ) ) d= tan(d);
    else if( strmatch(next, "atan"  ) ) d= atan(d);
    else if( strmatch(next, "asin"  ) ) d= asin(d);
    else if( strmatch(next, "acos"  ) ) d= acos(d);
    else if( strmatch(next, "exp"   ) ) d= exp(d);
    else if( strmatch(next, "log"   ) ) d= log(d);
    else if( strmatch(next, "squish") ) d= 1/(1+exp(4*d));
    else if( strmatch(next, "gauss" ) ) d= exp(-d*d/2)/sqrt(2*M_PI);
    else if( strmatch(next, "abs"   ) ) d= fabs(d);
    else if( strmatch(next, "mod"   ) ) d-= floor(d/d2)*d2;
    else if( strmatch(next, "max"   ) ) d= d >  d2 ?   d : d2;
    else if( strmatch(next, "min"   ) ) d= d <  d2 ?   d : d2;
    else if( strmatch(next, "gt"    ) ) d= d >  d2 ? 1.0 : 0.0;
    else if( strmatch(next, "gte"   ) ) d= d >= d2 ? 1.0 : 0.0;
    else if( strmatch(next, "lt"    ) ) d= d >  d2 ? 0.0 : 1.0;
    else if( strmatch(next, "lte"   ) ) d= d >= d2 ? 0.0 : 1.0;
    else if( strmatch(next, "eq"    ) ) d= d == d2 ? 1.0 : 0.0;
    else if( strmatch(next, "("     ) ) d= d;
//    else if( strmatch(next, "l1"    ) ) d= 1 + d2*(d - 1);
//    else if( strmatch(next, "sq01"  ) ) d= (d >= 0.0 && d <=1.0) ? 1.0 : 0.0;
    else{
        for(i=0; p->func1_name && p->func1_name[i]; i++){
            if(strmatch(next, p->func1_name[i])){
                return p->func1[i](p->opaque, d);
            }
        }

        for(i=0; p->func2_name && p->func2_name[i]; i++){
            if(strmatch(next, p->func2_name[i])){
                return p->func2[i](p->opaque, d, d2);
            }
        }

        *p->error = "unknown function";
        return NAN;
    }

    return d;
}

static double evalPow(Parser *p, int *sign){
    *sign= (*p->s == '+') - (*p->s == '-');
    p->s += *sign&1;
    return evalPrimary(p);
}

static double evalFactor(Parser *p){
    int sign, sign2;
    double ret, e;
    ret= evalPow(p, &sign);
    while(p->s[0]=='^'){
        p->s++;
        e= evalPow(p, &sign2);
        ret= pow(ret, (sign2|1) * e);
    }
    return (sign|1) * ret;
}

static double evalTerm(Parser *p){
    double ret= evalFactor(p);
    while(p->s[0]=='*' || p->s[0]=='/'){
        if(*p->s++ == '*') ret*= evalFactor(p);
        else               ret/= evalFactor(p);
    }
    return ret;
}

static double evalExpression(Parser *p){
    double ret= 0;

    if(p->stack_index <= 0) //protect against stack overflows
        return NAN;
    p->stack_index--;

    do{
        ret += evalTerm(p);
    }while(*p->s == '+' || *p->s == '-');

    p->stack_index++;

    return ret;
}

double ff_eval2(char *s, double *const_value, const char **const_name,
               double (**func1)(void *, double), const char **func1_name,
               double (**func2)(void *, double, double), char **func2_name,
               void *opaque, char **error){
    Parser p;

    p.stack_index=100;
    p.s= s;
    p.const_value= const_value;
    p.const_name = const_name;
    p.func1      = func1;
    p.func1_name = func1_name;
    p.func2      = func2;
    p.func2_name = func2_name;
    p.opaque     = opaque;
    p.error= error;

    return evalExpression(&p);
}

#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)
attribute_deprecated double ff_eval(char *s, double *const_value, const char **const_name,
               double (**func1)(void *, double), const char **func1_name,
               double (**func2)(void *, double, double), char **func2_name,
               void *opaque){
    char *error=NULL;
    double ret;
    ret = ff_eval2(s, const_value, const_name, func1, func1_name, func2, func2_name, opaque, &error);
    if (error)
        av_log(NULL, AV_LOG_ERROR, "Error evaluating \"%s\": %s\n", s, error);
    return ret;
}
#endif

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
main(){
    int i;
    printf("%f == 12.7\n", ff_eval("1+(5-2)^(3-1)+1/2+sin(PI)-max(-2.2,-3.1)", const_values, const_names, NULL, NULL, NULL, NULL, NULL));
    printf("%f == 0.931322575\n", ff_eval("80G/80Gi", const_values, const_names, NULL, NULL, NULL, NULL, NULL));

    for(i=0; i<1050; i++){
        START_TIMER
            ff_eval("1+(5-2)^(3-1)+1/2+sin(PI)-max(-2.2,-3.1)", const_values, const_names, NULL, NULL, NULL, NULL, NULL);
        STOP_TIMER("ff_eval")
    }
}
#endif
