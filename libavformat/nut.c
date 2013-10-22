/*
 * nut
 * Copyright (c) 2004-2007 Michael Niedermayer
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/mathematics.h"
#include "libavutil/tree.h"
#include "nut.h"
#include "riff.h"
#include "internal.h"

const AVCodecTag ff_nut_subtitle_tags[] = {
    { AV_CODEC_ID_TEXT,             MKTAG('U', 'T', 'F', '8') },
    { AV_CODEC_ID_SSA,              MKTAG('S', 'S', 'A',   0) },
    { AV_CODEC_ID_DVD_SUBTITLE,     MKTAG('D', 'V', 'D', 'S') },
    { AV_CODEC_ID_DVB_SUBTITLE,     MKTAG('D', 'V', 'B', 'S') },
    { AV_CODEC_ID_NONE,             0                         }
};

const AVCodecTag ff_nut_data_tags[] = {
    { AV_CODEC_ID_TEXT,             MKTAG('U', 'T', 'F', '8') },
    { AV_CODEC_ID_NONE,             0 }
};

const AVCodecTag ff_nut_video_tags[] = {
    { AV_CODEC_ID_VP9,              MKTAG('V', 'P', '9', '0') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('R', 'G', 'B',  15) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('B', 'G', 'R',  15) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('R', 'G', 'B',  16) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('B', 'G', 'R',  16) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(15,  'B', 'G', 'R') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(15,  'R', 'G', 'B') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(16,  'B', 'G', 'R') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(16,  'R', 'G', 'B') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('R', 'G', 'B',  12) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('B', 'G', 'R',  12) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(12,  'B', 'G', 'R') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(12,  'R', 'G', 'B') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('R', 'G', 'B', 'A') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('B', 'G', 'R', 'A') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('A', 'B', 'G', 'R') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('A', 'R', 'G', 'B') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('R', 'G', 'B',  24) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('B', 'G', 'R',  24) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('4', '1', '1', 'P') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('4', '2', '2', 'P') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('4', '2', '2', 'P') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('4', '4', '0', 'P') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('4', '4', '0', 'P') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('4', '4', '4', 'P') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('4', '4', '4', 'P') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('B', '1', 'W', '0') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('B', '0', 'W', '1') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('B', 'G', 'R',   8) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('R', 'G', 'B',   8) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('B', 'G', 'R',   4) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('R', 'G', 'B',   4) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('B', '4', 'B', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('R', '4', 'B', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('B', 'G', 'R',  48) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('R', 'G', 'B',  48) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(48,  'B', 'G', 'R') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(48,  'R', 'G', 'B') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3',  11,  10) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(10,   11, '3', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3',  10,  10) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(10,   10, '3', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3',   0,  10) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(10,    0, '3', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '1',   0,  16) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(16,    0, '1', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3',  11,  16) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(16,   11, '3', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3',  10,  16) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(16,   10, '3', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3',   0,  16) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(16,    0, '3', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',  11,   8) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '2',   0,   8) },

    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '1',   0,   9) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(9,     0, '1', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',  11,   9) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(9,    11, '4', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',  10,   9) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(9,    10, '4', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',   0,   9) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(9,     0, '4', 'Y') },

    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '1',   0,  10) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(10,    0, '1', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',  11,  10) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(10,   11, '4', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',  10,  10) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(10,   10, '4', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',   0,  10) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(10,    0, '4', 'Y') },

    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '1',   0,  16) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(16,    0, '1', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',  11,  16) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(16,   11, '4', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',  10,  16) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(16,   10, '4', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',   0,  16) },
    { AV_CODEC_ID_RAWVIDEO,         MKTAG(16,    0, '4', 'Y') },

    { AV_CODEC_ID_NONE,             0 }
};

const AVCodecTag ff_nut_audio_tags[] = {
    { AV_CODEC_ID_PCM_ALAW,         MKTAG('A', 'L', 'A', 'W') },
    { AV_CODEC_ID_PCM_MULAW,        MKTAG('U', 'L', 'A', 'W') },
    { AV_CODEC_ID_PCM_F32BE,        MKTAG(32,  'D', 'F', 'P') },
    { AV_CODEC_ID_PCM_F32LE,        MKTAG('P', 'F', 'D',  32) },
    { AV_CODEC_ID_PCM_F64BE,        MKTAG(64,  'D', 'F', 'P') },
    { AV_CODEC_ID_PCM_F64LE,        MKTAG('P', 'F', 'D',  64) },
    { AV_CODEC_ID_PCM_S16BE,        MKTAG(16,  'D', 'S', 'P') },
    { AV_CODEC_ID_PCM_S16LE,        MKTAG('P', 'S', 'D',  16) },
    { AV_CODEC_ID_PCM_S24BE,        MKTAG(24,  'D', 'S', 'P') },
    { AV_CODEC_ID_PCM_S24LE,        MKTAG('P', 'S', 'D',  24) },
    { AV_CODEC_ID_PCM_S32BE,        MKTAG(32,  'D', 'S', 'P') },
    { AV_CODEC_ID_PCM_S32LE,        MKTAG('P', 'S', 'D',  32) },
    { AV_CODEC_ID_PCM_S8,           MKTAG('P', 'S', 'D',   8) },
    { AV_CODEC_ID_PCM_U16BE,        MKTAG(16,  'D', 'U', 'P') },
    { AV_CODEC_ID_PCM_U16LE,        MKTAG('P', 'U', 'D',  16) },
    { AV_CODEC_ID_PCM_U24BE,        MKTAG(24,  'D', 'U', 'P') },
    { AV_CODEC_ID_PCM_U24LE,        MKTAG('P', 'U', 'D',  24) },
    { AV_CODEC_ID_PCM_U32BE,        MKTAG(32,  'D', 'U', 'P') },
    { AV_CODEC_ID_PCM_U32LE,        MKTAG('P', 'U', 'D',  32) },
    { AV_CODEC_ID_PCM_U8,           MKTAG('P', 'U', 'D',   8) },
    { AV_CODEC_ID_PCM_S16LE_PLANAR, MKTAG('P', 'S', 'P',  16) },
    { AV_CODEC_ID_PCM_S24LE_PLANAR, MKTAG('P', 'S', 'P',  24) },
    { AV_CODEC_ID_PCM_S32LE_PLANAR, MKTAG('P', 'S', 'P',  32) },
    { AV_CODEC_ID_MP3,              MKTAG('M', 'P', '3', ' ') },
    { AV_CODEC_ID_NONE,             0 }
};

const AVCodecTag * const ff_nut_codec_tags[] = {
    ff_nut_video_tags, ff_nut_audio_tags, ff_nut_subtitle_tags,
    ff_codec_bmp_tags, ff_codec_wav_tags, ff_nut_data_tags, 0
};

void ff_nut_reset_ts(NUTContext *nut, AVRational time_base, int64_t val)
{
    int i;
    for (i = 0; i < nut->avf->nb_streams; i++)
        nut->stream[i].last_pts =
            av_rescale_rnd(val,
                           time_base.num * (int64_t)nut->stream[i].time_base->den,
                           time_base.den * (int64_t)nut->stream[i].time_base->num,
                           AV_ROUND_DOWN);
}

int64_t ff_lsb2full(StreamContext *stream, int64_t lsb)
{
    int64_t mask  = (1 << stream->msb_pts_shift) - 1;
    int64_t delta = stream->last_pts - mask / 2;
    return ((lsb - delta) & mask) + delta;
}

int ff_nut_sp_pos_cmp(const Syncpoint *a, const Syncpoint *b)
{
    return ((a->pos - b->pos) >> 32) - ((b->pos - a->pos) >> 32);
}

int ff_nut_sp_pts_cmp(const Syncpoint *a, const Syncpoint *b)
{
    return ((a->ts - b->ts) >> 32) - ((b->ts - a->ts) >> 32);
}

int ff_nut_add_sp(NUTContext *nut, int64_t pos, int64_t back_ptr, int64_t ts)
{
    Syncpoint *sp           = av_mallocz(sizeof(Syncpoint));
    struct AVTreeNode *node = av_tree_node_alloc();

    if (!sp || !node) {
        av_freep(&sp);
        av_freep(&node);
        return AVERROR(ENOMEM);
    }

    sp->pos      = pos;
    sp->back_ptr = back_ptr;
    sp->ts       = ts;
    av_tree_insert(&nut->syncpoints, sp, (void *) ff_nut_sp_pos_cmp, &node);
    if (node) {
        av_free(sp);
        av_free(node);
    }

    return 0;
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
    { "default",        AV_DISPOSITION_DEFAULT  },
    { "dub",            AV_DISPOSITION_DUB      },
    { "original",       AV_DISPOSITION_ORIGINAL },
    { "comment",        AV_DISPOSITION_COMMENT  },
    { "lyrics",         AV_DISPOSITION_LYRICS   },
    { "karaoke",        AV_DISPOSITION_KARAOKE  },
    { "",               0                       }
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
