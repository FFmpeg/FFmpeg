/*
 * Copyright (c) 2000, 2001 Fabrice Bellard
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

#include "asf.h"
#include "demux.h"
#include "id3v2.h"
#include "internal.h"

/* List of official tags at http://msdn.microsoft.com/en-us/library/dd743066(VS.85).aspx */
const AVMetadataConv ff_asf_metadata_conv[] = {
    { "WM/AlbumArtist",          "album_artist"     },
    { "WM/AlbumTitle",           "album"            },
    { "Author",                  "artist"           },
    { "Description",             "comment"          },
    { "WM/Composer",             "composer"         },
    { "WM/EncodedBy",            "encoded_by"       },
    { "WM/EncodingSettings",     "encoder"          },
    { "WM/Genre",                "genre"            },
    { "WM/Language",             "language"         },
    { "WM/OriginalFilename",     "filename"         },
    { "WM/PartOfSet",            "disc"             },
    { "WM/Publisher",            "publisher"        },
    { "WM/Tool",                 "encoder"          },
    { "WM/TrackNumber",          "track"            },
    { "WM/MediaStationCallSign", "service_provider" },
    { "WM/MediaStationName",     "service_name"     },
//  { "Year"               , "date"        }, TODO: conversion year<->date
    { 0 }
};

/* MSDN claims that this should be "compatible with the ID3 frame, APIC",
 * but in reality this is only loosely similar */
static int asf_read_picture(AVFormatContext *s, int len)
{
    const CodecMime *mime = ff_id3v2_mime_tags;
    enum  AVCodecID id    = AV_CODEC_ID_NONE;
    char mimetype[64];
    uint8_t  *desc = NULL;
    AVStream   *st = NULL;
    int ret, type, picsize, desc_len;

    /* type + picsize + mime + desc */
    if (len < 1 + 4 + 2 + 2) {
        av_log(s, AV_LOG_ERROR, "Invalid attached picture size: %d.\n", len);
        return AVERROR_INVALIDDATA;
    }

    /* picture type */
    type = avio_r8(s->pb);
    len--;
    if (type >= FF_ARRAY_ELEMS(ff_id3v2_picture_types) || type < 0) {
        av_log(s, AV_LOG_WARNING, "Unknown attached picture type: %d.\n", type);
        type = 0;
    }

    /* picture data size */
    picsize = avio_rl32(s->pb);
    len    -= 4;

    /* picture MIME type */
    len -= avio_get_str16le(s->pb, len, mimetype, sizeof(mimetype));
    while (mime->id != AV_CODEC_ID_NONE) {
        if (!strncmp(mime->str, mimetype, sizeof(mimetype))) {
            id = mime->id;
            break;
        }
        mime++;
    }
    if (id == AV_CODEC_ID_NONE) {
        av_log(s, AV_LOG_ERROR, "Unknown attached picture mimetype: %s.\n",
               mimetype);
        return 0;
    }

    if (picsize >= len) {
        av_log(s, AV_LOG_ERROR, "Invalid attached picture data size: %d >= %d.\n",
               picsize, len);
        return AVERROR_INVALIDDATA;
    }

    /* picture description */
    desc_len = (len - picsize) * 2 + 1;
    desc     = av_malloc(desc_len);
    if (!desc)
        return AVERROR(ENOMEM);
    len -= avio_get_str16le(s->pb, len - picsize, desc, desc_len);

    ret = ff_add_attached_pic(s, NULL, s->pb, NULL, picsize);
    if (ret < 0)
        goto fail;
    st = s->streams[s->nb_streams - 1];

    st->codecpar->codec_id        = id;

    if (*desc) {
        if (av_dict_set(&st->metadata, "title", desc, AV_DICT_DONT_STRDUP_VAL) < 0)
            av_log(s, AV_LOG_WARNING, "av_dict_set failed.\n");
    } else
        av_freep(&desc);

    if (av_dict_set(&st->metadata, "comment", ff_id3v2_picture_types[type], 0) < 0)
        av_log(s, AV_LOG_WARNING, "av_dict_set failed.\n");

    return 0;

fail:
    av_freep(&desc);
    return ret;
}

static int get_id3_tag(AVFormatContext *s, int len)
{
    ID3v2ExtraMeta *id3v2_extra_meta;

    ff_id3v2_read(s, ID3v2_DEFAULT_MAGIC, &id3v2_extra_meta, len);
    if (id3v2_extra_meta) {
        ff_id3v2_parse_apic(s, id3v2_extra_meta);
        ff_id3v2_parse_chapters(s, id3v2_extra_meta);
        ff_id3v2_free_extra_meta(&id3v2_extra_meta);
    }
    return 0;
}

int ff_asf_handle_byte_array(AVFormatContext *s, const char *name,
                             int val_len)
{
    if (!strcmp(name, "WM/Picture")) // handle cover art
        return asf_read_picture(s, val_len);
    else if (!strcmp(name, "ID3")) // handle ID3 tag
        return get_id3_tag(s, val_len);

    return 1;
}
