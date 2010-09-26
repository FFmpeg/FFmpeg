/*
 * AVOptions ABI compatibility wrapper
 * Copyright (c) 2010 Michael Niedermayer <michaelni@gmx.at>
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

#include "avcodec.h"
#include "opt.h"

#if LIBAVCODEC_VERSION_MAJOR < 53 && CONFIG_SHARED && HAVE_SYMVER

FF_SYMVER(const AVOption *, av_find_opt, (void *obj, const char *name, const char *unit, int mask, int flags), "LIBAVCODEC_52"){
    return av_find_opt(obj, name, unit, mask, flags);
}
FF_SYMVER(int, av_set_string3, (void *obj, const char *name, const char *val, int alloc, const AVOption **o_out), "LIBAVCODEC_52"){
    return av_set_string3(obj, name, val, alloc, o_out);
}
FF_SYMVER(const AVOption *, av_set_double, (void *obj, const char *name, double n), "LIBAVCODEC_52"){
    return av_set_double(obj, name, n);
}
FF_SYMVER(const AVOption *, av_set_q, (void *obj, const char *name, AVRational n), "LIBAVCODEC_52"){
    return av_set_q(obj, name, n);
}
FF_SYMVER(const AVOption *, av_set_int, (void *obj, const char *name, int64_t n), "LIBAVCODEC_52"){
    return av_set_int(obj, name, n);
}
FF_SYMVER(double, av_get_double, (void *obj, const char *name, const AVOption **o_out), "LIBAVCODEC_52"){
    return av_get_double(obj, name, o_out);
}
FF_SYMVER(AVRational, av_get_q, (void *obj, const char *name, const AVOption **o_out), "LIBAVCODEC_52"){
    return av_get_q(obj, name, o_out);
}
FF_SYMVER(int64_t, av_get_int, (void *obj, const char *name, const AVOption **o_out), "LIBAVCODEC_52"){
    return av_get_int(obj, name, o_out);
}
FF_SYMVER(const char *, av_get_string, (void *obj, const char *name, const AVOption **o_out, char *buf, int buf_len), "LIBAVCODEC_52"){
    return av_get_string(obj, name, o_out, buf, buf_len);
}
FF_SYMVER(const AVOption *, av_next_option, (void *obj, const AVOption *last), "LIBAVCODEC_52"){
    return av_next_option(obj, last);
}
FF_SYMVER(int, av_opt_show2, (void *obj, void *av_log_obj, int req_flags, int rej_flags), "LIBAVCODEC_52"){
    return av_opt_show2(obj, av_log_obj, req_flags, rej_flags);
}
FF_SYMVER(void, av_opt_set_defaults, (void *s), "LIBAVCODEC_52"){
    return av_opt_set_defaults(s);
}
FF_SYMVER(void, av_opt_set_defaults2, (void *s, int mask, int flags), "LIBAVCODEC_52"){
    return av_opt_set_defaults2(s, mask, flags);
}
#endif

#if LIBAVCODEC_VERSION_MAJOR < 53
const AVOption *av_set_string2(void *obj, const char *name, const char *val, int alloc){
    const AVOption *o;
    if (av_set_string3(obj, name, val, alloc, &o) < 0)
        return NULL;
    return o;
}

const AVOption *av_set_string(void *obj, const char *name, const char *val){
    const AVOption *o;
    if (av_set_string3(obj, name, val, 0, &o) < 0)
        return NULL;
    return o;
}
#endif

#if FF_API_OPT_SHOW
int av_opt_show(void *obj, void *av_log_obj){
    return av_opt_show2(obj, av_log_obj,
                        AV_OPT_FLAG_ENCODING_PARAM|AV_OPT_FLAG_DECODING_PARAM, 0);
}
#endif
