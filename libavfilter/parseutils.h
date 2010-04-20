/*
 * copyright (c) 2009 Stefano Sabatini
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
 * parsing utils
 */

#ifndef AVFILTER_PARSEUTILS_H
#define AVFILTER_PARSEUTILS_H

#include "libavcodec/opt.h"

/**
 * Unescapes the given string until a non escaped terminating char,
 * and returns the token corresponding to the unescaped string.
 *
 * The normal \ and ' escaping is supported. Leading and trailing
 * whitespaces are removed.
 *
 * @param term a 0-terminated list of terminating chars
 * @param buf the buffer to parse, buf will be updated to point to the
 * terminating char
 * @return the malloced unescaped string, which must be av_freed by
 * the user
 */
char *av_get_token(const char **buf, const char *term);

/**
 * Puts the RGBA values that correspond to color_string in rgba_color.
 *
 * @param color_string a string specifying a color. It can be the name of
 * a color (case insensitive match) or a 0xRRGGBB[AA] sequence.
 * The string "random" will result in a random color.
 * @return >= 0 in case of success, a negative value in case of
 * failure (for example if color_string cannot be parsed).
 */
int av_parse_color(uint8_t *rgba_color, const char *color_string, void *log_ctx);

/**
 * Parses the key/value pairs list in opts. For each key/value pair
 * found, stores the value in the field in ctx that is named like the
 * key. ctx must be an AVClass context, storing is done using
 * AVOptions.
 *
 * @param key_val_sep a 0-terminated list of characters used to
 * separate key from value
 * @param pairs_sep a 0-terminated list of characters used to separate
 * two pairs from each other
 * @return the number of successfully set key/value pairs, or a negative
 * value corresponding to an AVERROR code in case of error:
 * AVERROR(EINVAL) if opts cannot be parsed,
 * the error code issued by av_set_string3() if a key/value pair
 * cannot be set
 */
int av_set_options_string(void *ctx, const char *opts,
                          const char *key_val_sep, const char *pairs_sep);

#endif  /* AVFILTER_PARSEUTILS_H */
