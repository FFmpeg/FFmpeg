/*
 * nut
 * Copyright (c) 2004-2007 Michael Niedermayer
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

#include "libavutil/tree.h"
#include "nut.h"

const AVCodecTag ff_nut_subtitle_tags[] = {
    { CODEC_ID_TEXT        , MKTAG('U', 'T', 'F', '8') },
    { CODEC_ID_SSA         , MKTAG('S', 'S', 'A',  0 ) },
    { CODEC_ID_DVD_SUBTITLE, MKTAG('D', 'V', 'D', 'S') },
    { CODEC_ID_DVB_SUBTITLE, MKTAG('D', 'V', 'B', 'S') },
    { CODEC_ID_NONE        , 0                         }
};

void ff_nut_reset_ts(NUTContext *nut, AVRational time_base, int64_t val){
    int i;
    for(i=0; i<nut->avf->nb_streams; i++){
        nut->stream[i].last_pts= av_rescale_rnd(
            val,
            time_base.num * (int64_t)nut->stream[i].time_base->den,
            time_base.den * (int64_t)nut->stream[i].time_base->num,
            AV_ROUND_DOWN);
    }
}

int64_t ff_lsb2full(StreamContext *stream, int64_t lsb){
    int64_t mask = (1<<stream->msb_pts_shift)-1;
    int64_t delta= stream->last_pts - mask/2;
    return  ((lsb - delta)&mask) + delta;
}

int ff_nut_sp_pos_cmp(const Syncpoint *a, const Syncpoint *b){
    return ((a->pos - b->pos) >> 32) - ((b->pos - a->pos) >> 32);
}

int ff_nut_sp_pts_cmp(const Syncpoint *a, const Syncpoint *b){
    return ((a->ts - b->ts) >> 32) - ((b->ts - a->ts) >> 32);
}

void ff_nut_add_sp(NUTContext *nut, int64_t pos, int64_t back_ptr, int64_t ts){
    Syncpoint *sp= av_mallocz(sizeof(Syncpoint));
    struct AVTreeNode *node= av_mallocz(av_tree_node_size);

    sp->pos= pos;
    sp->back_ptr= back_ptr;
    sp->ts= ts;
    av_tree_insert(&nut->syncpoints, sp, (void *) ff_nut_sp_pos_cmp, &node);
    if(node){
        av_free(sp);
        av_free(node);
    }
}

static int enu_free(void *opaque, void *elem)
{
    av_free(elem);
    return 0;
}

void ff_nut_free_sp(NUTContext *nut)
{
    av_tree_enumerate(nut->syncpoints, NULL, NULL, enu_free);
    av_tree_destroy(nut->syncpoints);
}

const Dispositions ff_nut_dispositions[] = {
    {"default"     , AV_DISPOSITION_DEFAULT},
    {"dub"         , AV_DISPOSITION_DUB},
    {"original"    , AV_DISPOSITION_ORIGINAL},
    {"comment"     , AV_DISPOSITION_COMMENT},
    {"lyrics"      , AV_DISPOSITION_LYRICS},
    {"karaoke"     , AV_DISPOSITION_KARAOKE},
    {""            , 0}
};

const AVMetadataConv ff_nut_metadata_conv[] = {
    { "Author",         "artist"      },
    { "X-CreationTime", "date"        },
    { "CreationTime",   "date"        },
    { "SourceFilename", "filename"    },
    { "X-Language",     "language"    },
    { "X-Disposition",  "disposition" },
    { "X-Replaces",     "replaces"    },
    { "X-Depends",      "depends"     },
    { "X-Uses",         "uses"        },
    { "X-UsesFont",     "usesfont"    },
    { 0 },
};
