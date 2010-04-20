/*
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
 * @file
 * simple arithmetic expression evaluator
 */

#ifndef AVCODEC_EVAL_H
#define AVCODEC_EVAL_H

typedef struct AVExpr AVExpr;

/**
 * Parses and evaluates an expression.
 * Note, this is significantly slower than ff_eval_expr().
 *
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
double ff_parse_and_eval_expr(const char *s, const double *const_value, const char * const *const_name,
               double (* const *func1)(void *, double), const char * const *func1_name,
               double (* const *func2)(void *, double, double), const char * const *func2_name,
               void *opaque, const char **error);

/**
 * Parses an expression.
 *
 * @param s expression as a zero terminated string for example "1+2^3+5*5+sin(2/3)"
 * @param func1 NULL terminated array of function pointers for functions which take 1 argument
 * @param func2 NULL terminated array of function pointers for functions which take 2 arguments
 * @param const_name NULL terminated array of zero terminated strings of constant identifers for example {"PI", "E", 0}
 * @param func1_name NULL terminated array of zero terminated strings of func1 identifers
 * @param func2_name NULL terminated array of zero terminated strings of func2 identifers
 * @param error pointer to a char* which is set to an error message if something goes wrong
 * @return AVExpr which must be freed with ff_free_expr() by the user when it is not needed anymore
 *         NULL if anything went wrong
 */
AVExpr *ff_parse_expr(const char *s, const char * const *const_name,
               double (* const *func1)(void *, double), const char * const *func1_name,
               double (* const *func2)(void *, double, double), const char * const *func2_name,
               const char **error);

/**
 * Evaluates a previously parsed expression.
 *
 * @param const_value a zero terminated array of values for the identifers from ff_parse const_name
 * @param opaque a pointer which will be passed to all functions from func1 and func2
 * @return the value of the expression
 */
double ff_eval_expr(AVExpr * e, const double *const_value, void *opaque);

/**
 * Frees a parsed expression previously created with ff_parse().
 */
void ff_free_expr(AVExpr *e);

/**
 * Parses the string in numstr and returns its value as a double. If
 * the string is empty, contains only whitespaces, or does not contain
 * an initial substring that has the expected syntax for a
 * floating-point number, no conversion is performed. In this case,
 * returns a value of zero and the value returned in tail is the value
 * of numstr.
 *
 * @param numstr a string representing a number, may contain one of
 * the International System number postfixes, for example 'K', 'M',
 * 'G'. If 'i' is appended after the postfix, powers of 2 are used
 * instead of powers of 10. The 'B' postfix multiplies the value for
 * 8, and can be appended after another postfix or used alone. This
 * allows using for example 'KB', 'MiB', 'G' and 'B' as postfix.
 * @param tail if non-NULL puts here the pointer to the char next
 * after the last parsed character
 */
double av_strtod(const char *numstr, char **tail);

#endif /* AVCODEC_EVAL_H */
