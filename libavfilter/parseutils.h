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
 * @file libavfilter/parseutils.h
 * parsing utils
 */

#ifndef AVFILTER_PARSEUTILS_H
#define AVFILTER_PARSEUTILS_H

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

#endif  /* AVFILTER_PARSEUTILS_H */
