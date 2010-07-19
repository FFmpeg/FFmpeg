/*
 * SAUCE header parser
 * Copyright (c) 2010 Peter Ross <pross@xvid.org>
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
 * SAUCE header parser
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "sauce.h"

int ff_sauce_read(AVFormatContext *avctx, uint64_t *fsize, int *got_width, int get_height)
{
    ByteIOContext *pb = avctx->pb;
    char buf[36];
    int datatype, filetype, t1, t2, nb_comments, flags;
    uint64_t start_pos = url_fsize(pb) - 128;

    url_fseek(pb, start_pos, SEEK_SET);
    if (get_buffer(pb, buf, 7) != 7)
        return -1;
    if (memcmp(buf, "SAUCE00", 7))
        return -1;

#define GET_SAUCE_META(name,size) \
    if (get_buffer(pb, buf, size) == size && buf[0]) { \
        buf[size] = 0; \
        av_metadata_set2(&avctx->metadata, name, buf, 0); \
    }

    GET_SAUCE_META("title",     35)
    GET_SAUCE_META("artist",    20)
    GET_SAUCE_META("publisher", 20)
    GET_SAUCE_META("date",      8)
    url_fskip(pb, 4);
    datatype    = get_byte(pb);
    filetype    = get_byte(pb);
    t1          = get_le16(pb);
    t2          = get_le16(pb);
    nb_comments = get_byte(pb);
    flags       = get_byte(pb);
    url_fskip(pb, 4);
    GET_SAUCE_META("encoder",   22);

    if (got_width && datatype && filetype) {
        if ((datatype == 1 && filetype <=2) || (datatype == 5 && filetype == 255) || datatype == 6) {
            if (t1) {
                avctx->streams[0]->codec->width = t1<<3;
                *got_width = 1;
            }
            if (get_height && t2)
                avctx->streams[0]->codec->height = t2<<4;
        } else if (datatype == 5) {
            if (filetype > 1) {
                avctx->streams[0]->codec->width = (filetype == 1 ? t1 : filetype) << 4;
                *got_width = 1;
            }
            if (get_height && t2)
                avctx->streams[0]->codec->height = t2<<4;
        }
    }

    *fsize -= 128;

    if (nb_comments > 0) {
        url_fseek(pb, start_pos - 64*nb_comments - 5, SEEK_SET);
        if (get_buffer(pb, buf, 5) == 5 && !memcmp(buf, "COMNT", 5)) {
            int i;
            char *str = av_malloc(65*nb_comments + 1);
            *fsize -= 64*nb_comments + 5;
            if (!str)
                return 0;
            for (i = 0; i < nb_comments; i++) {
                if (get_buffer(pb, str + 65*i, 64) != 64)
                    break;
                str[65*i + 64] = '\n';
            }
            str[65*i] = 0;
            av_metadata_set2(&avctx->metadata, "comment", str, AV_METADATA_DONT_STRDUP_VAL);
        }
    }

    return 0;
}
