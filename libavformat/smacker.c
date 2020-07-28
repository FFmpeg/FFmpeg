/*
 * Smacker demuxer
 * Copyright (c) 2006 Konstantin Shishkov
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

/*
 * Based on http://wiki.multimedia.cx/index.php?title=Smacker
 */

#include <inttypes.h>

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"

#define SMACKER_PAL 0x01
#define SMACKER_FLAG_RING_FRAME 0x01

enum SAudFlags {
    SMK_AUD_PACKED  = 0x80,
    SMK_AUD_16BITS  = 0x20,
    SMK_AUD_STEREO  = 0x10,
    SMK_AUD_BINKAUD = 0x08,
    SMK_AUD_USEDCT  = 0x04
};

typedef struct SmackerContext {
    uint32_t frames;
    /* frame info */
    uint32_t *frm_size;
    uint8_t  *frm_flags;
    /* internal variables */
    int64_t next_frame_pos;
    int cur_frame;
    int videoindex;
    int indexes[7];
    int duration_size[7];
    /* current frame for demuxing */
    uint32_t frame_size;
    int flags;
    int next_audio_index;
    int new_palette;
    uint8_t pal[768];
    int64_t aud_pts[7];
} SmackerContext;

/* palette used in Smacker */
static const uint8_t smk_pal[64] = {
    0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C,
    0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C,
    0x41, 0x45, 0x49, 0x4D, 0x51, 0x55, 0x59, 0x5D,
    0x61, 0x65, 0x69, 0x6D, 0x71, 0x75, 0x79, 0x7D,
    0x82, 0x86, 0x8A, 0x8E, 0x92, 0x96, 0x9A, 0x9E,
    0xA2, 0xA6, 0xAA, 0xAE, 0xB2, 0xB6, 0xBA, 0xBE,
    0xC3, 0xC7, 0xCB, 0xCF, 0xD3, 0xD7, 0xDB, 0xDF,
    0xE3, 0xE7, 0xEB, 0xEF, 0xF3, 0xF7, 0xFB, 0xFF
};


static int smacker_probe(const AVProbeData *p)
{
    if (   AV_RL32(p->buf) != MKTAG('S', 'M', 'K', '2')
        && AV_RL32(p->buf) != MKTAG('S', 'M', 'K', '4'))
        return 0;

    if (AV_RL32(p->buf+4) > 32768U || AV_RL32(p->buf+8) > 32768U)
        return AVPROBE_SCORE_MAX/4;

    return AVPROBE_SCORE_MAX;
}

static int smacker_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    SmackerContext *smk = s->priv_data;
    AVStream *st;
    AVCodecParameters *par;
    uint32_t magic, width, height, flags, treesize;
    int i, ret, pts_inc;
    int tbase;

    /* read and check header */
    magic  = avio_rl32(pb);
    if (magic != MKTAG('S', 'M', 'K', '2') && magic != MKTAG('S', 'M', 'K', '4'))
        return AVERROR_INVALIDDATA;
    width  = avio_rl32(pb);
    height = avio_rl32(pb);
    smk->frames = avio_rl32(pb);
    pts_inc = avio_rl32(pb);
    if (pts_inc > INT_MAX / 100) {
        av_log(s, AV_LOG_ERROR, "pts_inc %d is too large\n", pts_inc);
        return AVERROR_INVALIDDATA;
    }

    flags = avio_rl32(pb);
    if (flags & SMACKER_FLAG_RING_FRAME)
        smk->frames++;
    if (smk->frames > 0xFFFFFF) {
        av_log(s, AV_LOG_ERROR, "Too many frames: %"PRIu32"\n", smk->frames);
        return AVERROR_INVALIDDATA;
    }

    avio_skip(pb, 28); /* Unused audio related data */

    treesize = avio_rl32(pb);
    if (treesize >= UINT_MAX/4) {
        // treesize + 16 must not overflow (this check is probably redundant)
        av_log(s, AV_LOG_ERROR, "treesize too large\n");
        return AVERROR_INVALIDDATA;
    }

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    smk->videoindex = st->index;
    /* Smacker uses 100000 as internal timebase */
    if (pts_inc < 0)
        pts_inc = -pts_inc;
    else
        pts_inc *= 100;
    tbase = 100000;
    av_reduce(&tbase, &pts_inc, tbase, pts_inc, (1UL << 31) - 1);
    avpriv_set_pts_info(st, 33, pts_inc, tbase);
    st->duration = smk->frames;

    /* init video codec */
    par = st->codecpar;
    par->width      = width;
    par->height     = height;
    par->format     = AV_PIX_FMT_PAL8;
    par->codec_type = AVMEDIA_TYPE_VIDEO;
    par->codec_id   = AV_CODEC_ID_SMACKVIDEO;
    par->codec_tag  = magic;

    if ((ret = ff_alloc_extradata(par, treesize + 16)) < 0) {
        av_log(s, AV_LOG_ERROR,
               "Cannot allocate %"PRIu32" bytes of extradata\n",
               treesize + 16);
        return ret;
    }
    if ((ret = ffio_read_size(pb, par->extradata, 16)) < 0)
        return ret;

    /* handle possible audio streams */
    for (i = 0; i < 7; i++) {
        uint32_t rate = avio_rl24(pb);
        uint8_t aflag = avio_r8(pb);

        smk->indexes[i] = -1;

        if (rate) {
            AVStream *ast = avformat_new_stream(s, NULL);
            AVCodecParameters *par;
            if (!ast)
                return AVERROR(ENOMEM);

            smk->indexes[i] = ast->index;
            par = ast->codecpar;
            par->codec_type = AVMEDIA_TYPE_AUDIO;
            if (aflag & SMK_AUD_BINKAUD) {
                par->codec_id  = AV_CODEC_ID_BINKAUDIO_RDFT;
            } else if (aflag & SMK_AUD_USEDCT) {
                par->codec_id  = AV_CODEC_ID_BINKAUDIO_DCT;
            } else if (aflag & SMK_AUD_PACKED) {
                par->codec_id  = AV_CODEC_ID_SMACKAUDIO;
                par->codec_tag = MKTAG('S', 'M', 'K', 'A');
            } else {
                par->codec_id  = AV_CODEC_ID_PCM_U8;
            }
            if (aflag & SMK_AUD_STEREO) {
                par->channels       = 2;
                par->channel_layout = AV_CH_LAYOUT_STEREO;
            } else {
                par->channels       = 1;
                par->channel_layout = AV_CH_LAYOUT_MONO;
            }
            par->sample_rate = rate;
            par->bits_per_coded_sample = (aflag & SMK_AUD_16BITS) ? 16 : 8;
            if (par->bits_per_coded_sample == 16 &&
                par->codec_id == AV_CODEC_ID_PCM_U8)
                par->codec_id = AV_CODEC_ID_PCM_S16LE;
            else
                smk->duration_size[i] = 4;
            avpriv_set_pts_info(ast, 64, 1, par->sample_rate * par->channels
                                            * par->bits_per_coded_sample / 8);
        }
    }

    avio_rl32(pb); /* padding */

    /* setup data */
    st->priv_data  = av_malloc_array(smk->frames, sizeof(*smk->frm_size) +
                                                  sizeof(*smk->frm_flags));
    if (!st->priv_data)
        return AVERROR(ENOMEM);
    smk->frm_size  = st->priv_data;
    smk->frm_flags = (void*)(smk->frm_size + smk->frames);

    /* read frame info */
    for (i = 0; i < smk->frames; i++) {
        smk->frm_size[i] = avio_rl32(pb);
    }
    if ((ret = ffio_read_size(pb, smk->frm_flags, smk->frames)) < 0 ||
        /* load trees to extradata, they will be unpacked by decoder */
        (ret = ffio_read_size(pb, par->extradata + 16,
                              par->extradata_size - 16)) < 0) {
        return ret;
    }

    return 0;
}

static int smacker_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    SmackerContext *smk = s->priv_data;
    int flags;
    int ret;

    if (avio_feof(s->pb) || smk->cur_frame >= smk->frames)
        return AVERROR_EOF;

    /* if we demuxed all streams, pass another frame */
    if (!smk->next_audio_index) {
        smk->frame_size = smk->frm_size[smk->cur_frame] & (~3);
        smk->next_frame_pos = avio_tell(s->pb) + smk->frame_size;
        flags = smk->frm_flags[smk->cur_frame];
        smk->flags = flags >> 1;
        /* handle palette change event */
        if (flags & SMACKER_PAL) {
            int size, sz, t, off, j, pos;
            uint8_t *pal = smk->pal;
            uint8_t oldpal[768];

            memcpy(oldpal, pal, 768);
            size = avio_r8(s->pb);
            size = size * 4;
            if (size > smk->frame_size) {
                ret = AVERROR_INVALIDDATA;
                goto next_frame;
            }
            smk->frame_size -= size--;
            sz = 0;
            pos = avio_tell(s->pb) + size;
            while (sz < 256) {
                t = avio_r8(s->pb);
                if (t & 0x80) { /* skip palette entries */
                    sz  +=  (t & 0x7F) + 1;
                    pal += ((t & 0x7F) + 1) * 3;
                } else if (t & 0x40) { /* copy with offset */
                    off = avio_r8(s->pb);
                    j = (t & 0x3F) + 1;
                    if (off + j > 0x100) {
                        av_log(s, AV_LOG_ERROR,
                               "Invalid palette update, offset=%d length=%d extends beyond palette size\n",
                               off, j);
                        ret = AVERROR_INVALIDDATA;
                        goto next_frame;
                    }
                    off *= 3;
                    while (j-- && sz < 256) {
                        *pal++ = oldpal[off + 0];
                        *pal++ = oldpal[off + 1];
                        *pal++ = oldpal[off + 2];
                        sz++;
                        off += 3;
                    }
                } else { /* new entries */
                    *pal++ = smk_pal[t];
                    *pal++ = smk_pal[avio_r8(s->pb) & 0x3F];
                    *pal++ = smk_pal[avio_r8(s->pb) & 0x3F];
                    sz++;
                }
            }
            avio_seek(s->pb, pos, 0);
            smk->new_palette = 1;
        }
    }

    for (int i = smk->next_audio_index; i < 7; i++) {
        if (smk->flags & (1 << i)) {
            uint32_t size;

            size = avio_rl32(s->pb);
            if ((int)size < 4 + smk->duration_size[i] || size > smk->frame_size) {
                av_log(s, AV_LOG_ERROR, "Invalid audio part size\n");
                ret = AVERROR_INVALIDDATA;
                goto next_frame;
            }
            smk->frame_size -= size;
            size            -= 4;

            if (smk->indexes[i] < 0 ||
                s->streams[smk->indexes[i]]->discard >= AVDISCARD_ALL) {
                smk->aud_pts[i] += smk->duration_size[i] ? avio_rl32(s->pb)
                                                         : size;
                avio_skip(s->pb, size - smk->duration_size[i]);
                continue;
            }
            if ((ret = av_get_packet(s->pb, pkt, size)) != size) {
                ret = ret < 0 ? ret : AVERROR_INVALIDDATA;
                goto next_frame;
            }
            pkt->stream_index = smk->indexes[i];
            pkt->pts          = smk->aud_pts[i];
            pkt->duration     = smk->duration_size[i] ? AV_RL32(pkt->data)
                                                      : size;
            smk->aud_pts[i]  += pkt->duration;
            smk->next_audio_index = i + 1;
            return 0;
        }
    }

    if (s->streams[smk->videoindex]->discard >= AVDISCARD_ALL) {
        ret = FFERROR_REDO;
        goto next_frame;
    }
    if (smk->frame_size >= INT_MAX/2) {
        ret = AVERROR_INVALIDDATA;
        goto next_frame;
    }
    if ((ret = av_new_packet(pkt, smk->frame_size + 769)) < 0)
        goto next_frame;
    flags = smk->new_palette;
    if (smk->frm_size[smk->cur_frame] & 1)
        flags |= 2;
    pkt->data[0] = flags;
    memcpy(pkt->data + 1, smk->pal, 768);
    ret = ffio_read_size(s->pb, pkt->data + 769, smk->frame_size);
    if (ret < 0)
        goto next_frame;
    pkt->stream_index = smk->videoindex;
    pkt->pts          = smk->cur_frame;
    smk->next_audio_index = 0;
    smk->new_palette = 0;
    smk->cur_frame++;

    return 0;
next_frame:
    avio_seek(s->pb, smk->next_frame_pos, SEEK_SET);
    smk->next_audio_index = 0;
    smk->cur_frame++;
    return ret;
}

static int smacker_read_seek(AVFormatContext *s, int stream_index,
                             int64_t timestamp, int flags)
{
    SmackerContext *smk = s->priv_data;
    int64_t ret;

    /* only rewinding to start is supported */
    if (timestamp != 0) {
        av_log(s, AV_LOG_ERROR,
               "Random seeks are not supported (can only seek to start).\n");
        return AVERROR(EINVAL);
    }

    if ((ret = avio_seek(s->pb, s->internal->data_offset, SEEK_SET)) < 0)
        return ret;

    smk->cur_frame = 0;
    smk->next_audio_index = 0;
    smk->new_palette = 0;
    memset(smk->pal, 0, sizeof(smk->pal));
    memset(smk->aud_pts, 0, sizeof(smk->aud_pts));

    return 0;
}

AVInputFormat ff_smacker_demuxer = {
    .name           = "smk",
    .long_name      = NULL_IF_CONFIG_SMALL("Smacker"),
    .priv_data_size = sizeof(SmackerContext),
    .read_probe     = smacker_probe,
    .read_header    = smacker_read_header,
    .read_packet    = smacker_read_packet,
    .read_seek      = smacker_read_seek,
};
