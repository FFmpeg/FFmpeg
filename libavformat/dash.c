/*
 * MPEG-DASH ISO BMFF segmenter
 * Copyright (c) 2014 Martin Storsjo
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

#include "config.h"
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/rational.h"
#include "libavutil/time_internal.h"

#include "avc.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "isom.h"
#include "os_support.h"
#include "url.h"
#include "dash.h"

static DASHTmplId dash_read_tmpl_id(const char *identifier, char *format_tag,
                                    size_t format_tag_size, const char **ptr) {
    const char *next_ptr;
    DASHTmplId id_type = DASH_TMPL_ID_UNDEFINED;

    if (av_strstart(identifier, "$$", &next_ptr)) {
        id_type = DASH_TMPL_ID_ESCAPE;
        *ptr = next_ptr;
    } else if (av_strstart(identifier, "$RepresentationID$", &next_ptr)) {
        id_type = DASH_TMPL_ID_REP_ID;
        // default to basic format, as $RepresentationID$ identifiers
        // are not allowed to have custom format-tags.
        av_strlcpy(format_tag, "%d", format_tag_size);
        *ptr = next_ptr;
    } else { // the following identifiers may have an explicit format_tag
        if (av_strstart(identifier, "$Number", &next_ptr))
            id_type = DASH_TMPL_ID_NUMBER;
        else if (av_strstart(identifier, "$Bandwidth", &next_ptr))
            id_type = DASH_TMPL_ID_BANDWIDTH;
        else if (av_strstart(identifier, "$Time", &next_ptr))
            id_type = DASH_TMPL_ID_TIME;
        else
            id_type = DASH_TMPL_ID_UNDEFINED;

        // next parse the dash format-tag and generate a c-string format tag
        // (next_ptr now points at the first '%' at the beginning of the format-tag)
        if (id_type != DASH_TMPL_ID_UNDEFINED) {
            const char *number_format = (id_type == DASH_TMPL_ID_TIME) ? PRId64 : "d";
            if (next_ptr[0] == '$') { // no dash format-tag
                snprintf(format_tag, format_tag_size, "%%%s", number_format);
                *ptr = &next_ptr[1];
            } else {
                const char *width_ptr;
                // only tolerate single-digit width-field (i.e. up to 9-digit width)
                if (av_strstart(next_ptr, "%0", &width_ptr) &&
                    av_isdigit(width_ptr[0]) &&
                    av_strstart(&width_ptr[1], "d$", &next_ptr)) {
                    // yes, we're using a format tag to build format_tag.
                    snprintf(format_tag, format_tag_size, "%s%c%s", "%0", width_ptr[0], number_format);
                    *ptr = next_ptr;
                } else {
                    av_log(NULL, AV_LOG_WARNING, "Failed to parse format-tag beginning with %s. Expected either a "
                                                 "closing '$' character or a format-string like '%%0[width]d', "
                                                 "where width must be a single digit\n", next_ptr);
                    id_type = DASH_TMPL_ID_UNDEFINED;
                }
            }
        }
    }
    return id_type;
}

void ff_dash_fill_tmpl_params(char *dst, size_t buffer_size,
                                  const char *template, int rep_id,
                                  int number, int bit_rate,
                                  int64_t time) {
    int dst_pos = 0;
    const char *t_cur = template;
    while (dst_pos < buffer_size - 1 && *t_cur) {
        char format_tag[7]; // May be "%d", "%0Xd", or "%0Xlld" (for $Time$), where X is in [0-9]
        int n = 0;
        DASHTmplId id_type;
        const char *t_next = strchr(t_cur, '$'); // copy over everything up to the first '$' character
        if (t_next) {
            int num_copy_bytes = FFMIN(t_next - t_cur, buffer_size - dst_pos - 1);
            av_strlcpy(&dst[dst_pos], t_cur, num_copy_bytes + 1);
            // advance
            dst_pos += num_copy_bytes;
            t_cur = t_next;
        } else { // no more DASH identifiers to substitute - just copy the rest over and break
            av_strlcpy(&dst[dst_pos], t_cur, buffer_size - dst_pos);
            break;
        }

        if (dst_pos >= buffer_size - 1 || !*t_cur)
            break;

        // t_cur is now pointing to a '$' character
        id_type = dash_read_tmpl_id(t_cur, format_tag, sizeof(format_tag), &t_next);
        switch (id_type) {
        case DASH_TMPL_ID_ESCAPE:
            av_strlcpy(&dst[dst_pos], "$", 2);
            n = 1;
            break;
        case DASH_TMPL_ID_REP_ID:
            n = snprintf(&dst[dst_pos], buffer_size - dst_pos, format_tag, rep_id);
            break;
        case DASH_TMPL_ID_NUMBER:
            n = snprintf(&dst[dst_pos], buffer_size - dst_pos, format_tag, number);
            break;
        case DASH_TMPL_ID_BANDWIDTH:
            n = snprintf(&dst[dst_pos], buffer_size - dst_pos, format_tag, bit_rate);
            break;
        case DASH_TMPL_ID_TIME:
            n = snprintf(&dst[dst_pos], buffer_size - dst_pos, format_tag, time);
            break;
        case DASH_TMPL_ID_UNDEFINED:
            // copy over one byte and advance
            av_strlcpy(&dst[dst_pos], t_cur, 2);
            n = 1;
            t_next = &t_cur[1];
            break;
        }
        // t_next points just past the processed identifier
        // n is the number of bytes that were attempted to be written to dst
        // (may have failed to write all because buffer_size).

        // advance
        dst_pos += FFMIN(n, buffer_size - dst_pos - 1);
        t_cur = t_next;
    }
}


