/*
 * simple arithmetic expression evaluator
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
  #define NAN 0
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define STACK_SIZE 100

typedef struct Parser{
    double stack[STACK_SIZE];
    int stack_index;
    char *s;
    double *const_value;
    const char **const_name;          // NULL terminated
    double (**func1)(void *, double a); // NULL terminated
    const char **func1_name;          // NULL terminated
    double (**func2)(void *, double a, double b); // NULL terminated
    char **func2_name;          // NULL terminated
    void *opaque;
} Parser;

static void evalExpression(Parser *p);

static void push(Parser *p, double d){
    if(p->stack_index+1>= STACK_SIZE){
        av_log(NULL, AV_LOG_ERROR, "stack overflow in the parser\n");
        return;
    }
    p->stack[ p->stack_index++ ]= d;
//printf("push %f\n", d); fflush(stdout);
}

static double pop(Parser *p){
    if(p->stack_index<=0){
        av_log(NULL, AV_LOG_ERROR, "stack underflow in the parser\n");
        return NAN;
    }
//printf("pop\n"); fflush(stdout);
    return p->stack[ --p->stack_index ];
}

static int strmatch(const char *s, const char *prefix){
    int i;
    for(i=0; prefix[i]; i++){
        if(prefix[i] != s[i]) return 0;
    }
    return 1;
}

static void evalPrimary(Parser *p){
    double d, d2=NAN;
    char *next= p->s;
    int i;

    /* number */
    d= strtod(p->s, &next);
    if(next != p->s){
        push(p, d);
        p->s= next;
        return;
    }
    
    /* named constants */
    for(i=0; p->const_name[i]; i++){
        if(strmatch(p->s, p->const_name[i])){
            push(p, p->const_value[i]);
            p->s+= strlen(p->const_name[i]);
            return;
        }
    }
    
    p->s= strchr(p->s, '(');
    if(p->s==NULL){
        av_log(NULL, AV_LOG_ERROR, "Parser: missing ( in \"%s\"\n", next);
        return;
    }
    p->s++; // "("
    evalExpression(p);
    d= pop(p);
    if(p->s[0]== ','){
        p->s++; // ","
        evalExpression(p);
        d2= pop(p);
    }
    if(p->s[0] != ')'){
        av_log(NULL, AV_LOG_ERROR, "Parser: missing ) in \"%s\"\n", next);
        return;
    }
    p->s++; // ")"
    
         if( strmatch(next, "sinh"  ) ) d= sinh(d);
    else if( strmatch(next, "cosh"  ) ) d= cosh(d);
    else if( strmatch(next, "tanh"  ) ) d= tanh(d);
    else if( strmatch(next, "sin"   ) ) d= sin(d);
    else if( strmatch(next, "cos"   ) ) d= cos(d);
    else if( strmatch(next, "tan"   ) ) d= tan(d);
    else if( strmatch(next, "exp"   ) ) d= exp(d);
    else if( strmatch(next, "log"   ) ) d= log(d);
    else if( strmatch(next, "squish") ) d= 1/(1+exp(4*d));
    else if( strmatch(next, "gauss" ) ) d= exp(-d*d/2)/sqrt(2*M_PI);
    else if( strmatch(next, "abs"   ) ) d= fabs(d);
    else if( strmatch(next, "max"   ) ) d= d > d2 ? d : d2;
    else if( strmatch(next, "min"   ) ) d= d < d2 ? d : d2;
    else if( strmatch(next, "gt"    ) ) d= d > d2 ? 1.0 : 0.0;
    else if( strmatch(next, "gte"    ) ) d= d >= d2 ? 1.0 : 0.0;
    else if( strmatch(next, "lt"    ) ) d= d > d2 ? 0.0 : 1.0;
    else if( strmatch(next, "lte"    ) ) d= d >= d2 ? 0.0 : 1.0;
    else if( strmatch(next, "eq"    ) ) d= d == d2 ? 1.0 : 0.0;
//    else if( strmatch(next, "l1"    ) ) d= 1 + d2*(d - 1);
//    else if( strmatch(next, "sq01"  ) ) d= (d >= 0.0 && d <=1.0) ? 1.0 : 0.0;
    else{
        int error=1;
        for(i=0; p->func1_name && p->func1_name[i]; i++){
            if(strmatch(next, p->func1_name[i])){
                d= p->func1[i](p->opaque, d);
                error=0;
                break;
            }
        }

        for(i=0; p->func2_name && p->func2_name[i]; i++){
            if(strmatch(next, p->func2_name[i])){
                d= p->func2[i](p->opaque, d, d2);
                error=0;
                break;
            }
        }

        if(error){
            av_log(NULL, AV_LOG_ERROR, "Parser: unknown function in \"%s\"\n", next);
            return;
        }
    }
    
    push(p, d);
}      
       
static void evalPow(Parser *p){
    int neg= 0;
    if(p->s[0]=='+') p->s++;
       
    if(p->s[0]=='-'){ 
        neg= 1;
        p->s++;
    }
    
    if(p->s[0]=='('){
        p->s++;;
        evalExpression(p);

        if(p->s[0]!=')')
            av_log(NULL, AV_LOG_ERROR, "Parser: missing )\n");
        p->s++;
    }else{
        evalPrimary(p);
    }
    
    if(neg) push(p, -pop(p));
}

static void evalFactor(Parser *p){
    evalPow(p);
    while(p->s[0]=='^'){
        double d;

        p->s++;
        evalPow(p);
        d= pop(p);
        push(p, pow(pop(p), d));
    }
}

static void evalTerm(Parser *p){
    evalFactor(p);
    while(p->s[0]=='*' || p->s[0]=='/'){
        int inv= p->s[0]=='/';
        double d;

        p->s++;
        evalFactor(p);
        d= pop(p);
        if(inv) d= 1.0/d;
        push(p, d * pop(p));
    }
}

static void evalExpression(Parser *p){
    evalTerm(p);
    while(p->s[0]=='+' || p->s[0]=='-'){
        int sign= p->s[0]=='-';
        double d;

        p->s++;
        evalTerm(p);
        d= pop(p);
        if(sign) d= -d;
        push(p, d + pop(p));
    }
}

double ff_eval(char *s, double *const_value, const char **const_name,
               double (**func1)(void *, double), const char **func1_name,
               double (**func2)(void *, double, double), char **func2_name,
               void *opaque){
    Parser p;
    
    p.stack_index=0;
    p.s= s;
    p.const_value= const_value;
    p.const_name = const_name;
    p.func1      = func1;
    p.func1_name = func1_name;
    p.func2      = func2;
    p.func2_name = func2_name;
    p.opaque     = opaque;
    
    evalExpression(&p);
    return pop(&p);
}
