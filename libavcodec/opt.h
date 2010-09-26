/*
 * AVOptions
 * copyright (c) 2005 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef AVCODEC_OPT_H
#define AVCODEC_OPT_H

/**
 * @file
 * AVOptions
 */

#include "libavutil/rational.h"
#include "avcodec.h"
#include "libavutil/opt.h"

#if LIBAVCODEC_VERSION_MAJOR < 53
/**
 * @see av_set_string2()
 */
attribute_deprecated const AVOption *av_set_string(void *obj, const char *name, const char *val);

/**
 * @return a pointer to the AVOption corresponding to the field set or
 * NULL if no matching AVOption exists, or if the value val is not
 * valid
 * @see av_set_string3()
 */
attribute_deprecated const AVOption *av_set_string2(void *obj, const char *name, const char *val, int alloc);
#endif
#if FF_API_OPT_SHOW
/**
 * @deprecated Use av_opt_show2() instead.
 */
attribute_deprecated int av_opt_show(void *obj, void *av_log_obj);
#endif

#endif /* AVCODEC_OPT_H */
