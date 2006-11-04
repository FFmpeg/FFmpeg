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
 */

/**
 * @file eval.h
 * eval header.
 */

#ifndef AVCODEC_EVAL_H
#define AVCODEC_EVAL_H

#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)
double ff_eval(char *s, double *const_value, const char **const_name,
               double (**func1)(void *, double), const char **func1_name,
               double (**func2)(void *, double, double), char **func2_name,
               void *opaque);
#endif

/**
 * Parses and evaluates an expression.
 * Note, this is significantly slower than ff_parse_eval()
 * @param s expression as a zero terminated string for example "1+2^3+5*5+sin(2/3)"
 * @param func1 NULL terminated array of function pointers for functions which take 1 argument
 * @param func2 NULL terminated array of function pointers for functions which take 2 arguments
 * @param const_name NULL terminated array of zero terminated strings of constant identifers for example {"PI", "E", 0}
 * @param func1_name NULL terminated array of zero terminated strings of func1 identifers
 * @param func2_name NULL terminated array of zero terminated strings of func2 identifers
 * @param error pointer to a char* which is set to an error message if something goes wrong
 * @param const_value a zero terminated array of values for the identifers from const_name
 * @param opaque a pointer which will be passed to all functions from func1 and func2
 * @return the value of the expression
 */
double ff_eval2(char *s, double *const_value, const char **const_name,
               double (**func1)(void *, double), const char **func1_name,
               double (**func2)(void *, double, double), char **func2_name,
               void *opaque, char **error);

typedef struct ff_expr_s AVEvalExpr;

/**
 * Parses a expression.
 * @param s expression as a zero terminated string for example "1+2^3+5*5+sin(2/3)"
 * @param func1 NULL terminated array of function pointers for functions which take 1 argument
 * @param func2 NULL terminated array of function pointers for functions which take 2 arguments
 * @param const_name NULL terminated array of zero terminated strings of constant identifers for example {"PI", "E", 0}
 * @param func1_name NULL terminated array of zero terminated strings of func1 identifers
 * @param func2_name NULL terminated array of zero terminated strings of func2 identifers
 * @param error pointer to a char* which is set to an error message if something goes wrong
 * @return AVEvalExpr which must be freed with ff_eval_free by the user when its not needed anymore
 *         NULL if anything went wrong
 */
AVEvalExpr * ff_parse(char *s, const char **const_name,
               double (**func1)(void *, double), const char **func1_name,
               double (**func2)(void *, double, double), char **func2_name,
               char **error);
/**
 * Evaluates a previously parsed expression.
 * @param const_value a zero terminated array of values for the identifers from ff_parse const_name
 * @param opaque a pointer which will be passed to all functions from func1 and func2
 * @return the value of the expression
 */
double ff_parse_eval(AVEvalExpr * e, double *const_value, void *opaque);
void ff_eval_free(AVEvalExpr * e);

#endif /* AVCODEC_EVAL_H */
