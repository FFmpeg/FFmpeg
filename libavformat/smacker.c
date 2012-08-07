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

#include "libavutil/bswap.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
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
    /* Smacker file header */
    uint32_t magic;
    uint32_t width, height;
    uint32_t frames;
    int      pts_inc;
    uint32_t flags;
    uint32_t audio[7];
    uint32_t treesize;
    uint32_t mmap_size, mclr_size, full_size, type_size;
    uint8_t  aflags[7];
    uint32_t rates[7];
    uint32_t pad;
    /* frame info */
    uint32_t *frm_size;
    uint8_t  *frm_flags;
    /* internal variables */
    int cur_frame;
    int is_ver4;
    int64_t cur_pts;
    /* current frame for demuxing */
    uint8_t pal[768];
    int indexes[7];
    int videoindex;
    uint8_t *bufs[7];
    int buf_sizes[7];
    int stream_id[7];
    int curstream;
    int64_t nextpos;
    int64_t aud_pts[7];
} SmackerContext;

typedef struct SmackerFrame {
    int64_t pts;
    int stream;
} SmackerFrame;

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


static int smacker_probe(AVProbeData *p)
{
    if(p->buf[0] == 'S' && p->buf[1] == 'M' && p->buf[2] == 'K'
        && (p->buf[3] == '2' || p->buf[3] == '4'))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int smacker_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    SmackerContext *smk = s->priv_data;
    AVStream *st, *ast[7];
    int i, ret;
    int tbase;

    /* read and check header */
    smk->magic = avio_rl32(pb);
    if (smk->magic != MKTAG('S', 'M', 'K', '2') && smk->magic != MKTAG('S', 'M', 'K', '4'))
        return -1;
    smk->width = avio_rl32(pb);
    smk->height = avio_rl32(pb);
    smk->frames = avio_rl32(pb);
    smk->pts_inc = (int32_t)avio_rl32(pb);
    smk->flags = avio_rl32(pb);
    if(smk->flags & SMACKER_FLAG_RING_FRAME)
        smk->frames++;
    for(i = 0; i < 7; i++)
        smk->audio[i] = avio_rl32(pb);
    smk->treesize = avio_rl32(pb);

    if(smk->treesize >= UINT_MAX/4){ // smk->treesize + 16 must not overflow (this check is probably redundant)
        av_log(s, AV_LOG_ERROR, "treesize too large\n");
        return -1;
    }

//FIXME remove extradata "rebuilding"
    smk->mmap_size = avio_rl32(pb);
    smk->mclr_size = avio_rl32(pb);
    smk->full_size = avio_rl32(pb);
    smk->type_size = avio_rl32(pb);
    for(i = 0; i < 7; i++) {
        smk->rates[i]  = avio_rl24(pb);
        smk->aflags[i] = avio_r8(pb);
    }
    smk->pad = avio_rl32(pb);
    /* setup data */
    if(smk->frames > 0xFFFFFF) {
        av_log(s, AV_LOG_ERROR, "Too many frames: %i\n", smk->frames);
        return -1;
    }
    smk->frm_size = av_malloc(smk->frames * 4);
    smk->frm_flags = av_malloc(smk->frames);

    smk->is_ver4 = (smk->magic != MKTAG('S', 'M', 'K', '2'));

    /* read frame info */
    for(i = 0; i < smk->frames; i++) {
        smk->frm_size[i] = avio_rl32(pb);
    }
    for(i = 0; i < smk->frames; i++) {
        smk->frm_flags[i] = avio_r8(pb);
    }

    /* init video codec */
    st = avformat_new_stream(s, NULL);
    if (!st)
        return -1;
    smk->videoindex = st->index;
    st->codec->width = smk->width;
    st->codec->height = smk->height;
    st->codec->pix_fmt = PIX_FMT_PAL8;
    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id = AV_CODEC_ID_SMACKVIDEO;
    st->codec->codec_tag = smk->magic;
    /* Smacker uses 100000 as internal timebase */
    if(smk->pts_inc < 0)
        smk->pts_inc = -smk->pts_inc;
    else
        smk->pts_inc *= 100;
    tbase = 100000;
    av_reduce(&tbase, &smk->pts_inc, tbase, smk->pts_inc, (1UL<<31)-1);
    avpriv_set_pts_info(st, 33, smk->pts_inc, tbase);
    st->duration = smk->frames;
    /* handle possible audio streams */
    for(i = 0; i < 7; i++) {
        smk->indexes[i] = -1;
        if (smk->rates[i]) {
            ast[i] = avformat_new_stream(s, NULL);
            smk->indexes[i] = ast[i]->index;
            ast[i]->codec->codec_type = AVMEDIA_TYPE_AUDIO;
            if (smk->aflags[i] & SMK_AUD_BINKAUD) {
                ast[i]->codec->codec_id = AV_CODEC_ID_BINKAUDIO_RDFT;
            } else if (smk->aflags[i] & SMK_AUD_USEDCT) {
                ast[i]->codec->codec_id = AV_CODEC_ID_BINKAUDIO_DCT;
            } else if (smk->aflags[i] & SMK_AUD_PACKED){
                ast[i]->codec->codec_id = AV_CODEC_ID_SMACKAUDIO;
                ast[i]->codec->codec_tag = MKTAG('S', 'M', 'K', 'A');
            } else {
                ast[i]->codec->codec_id = AV_CODEC_ID_PCM_U8;
            }
            ast[i]->codec->channels = (smk->aflags[i] & SMK_AUD_STEREO) ? 2 : 1;
            ast[i]->codec->sample_rate = smk->rates[i];
            ast[i]->codec->bits_per_coded_sample = (smk->aflags[i] & SMK_AUD_16BITS) ? 16 : 8;
            if(ast[i]->codec->bits_per_coded_sample == 16 && ast[i]->codec->codec_id == AV_CODEC_ID_PCM_U8)
                ast[i]->codec->codec_id = AV_CODEC_ID_PCM_S16LE;
            avpriv_set_pts_info(ast[i], 64, 1, ast[i]->codec->sample_rate
                    * ast[i]->codec->channels * ast[i]->codec->bits_per_coded_sample / 8);
        }
    }


    /* load trees to extradata, they will be unpacked by decoder */
    st->codec->extradata = av_malloc(smk->treesize + 16 + FF_INPUT_BUFFER_PADDING_SIZE);
    st->codec->extradata_size = smk->treesize + 16;
    if(!st->codec->extradata){
        av_log(s, AV_LOG_ERROR, "Cannot allocate %i bytes of extradata\n", smk->treesize + 16);
        av_free(smk->frm_size);
        av_free(smk->frm_flags);
        return -1;
    }
    ret = avio_read(pb, st->codec->extradata + 16, st->codec->extradata_size - 16);
    if(ret != st->codec->extradata_size - 16){
        av_free(smk->frm_size);
        av_free(smk->frm_flags);
        return AVERROR(EIO);
    }
    ((int32_t*)st->codec->extradata)[0] = av_le2ne32(smk->mmap_size);
    ((int32_t*)st->codec->extradata)[1] = av_le2ne32(smk->mclr_size);
    ((int32_t*)st->codec->extradata)[2] = av_le2ne32(smk->full_size);
    ((int32_t*)st->codec->extradata)[3] = av_le2ne32(smk->type_size);

    smk->curstream = -1;
    smk->nextpos = avio_tell(pb);

    return 0;
}


static int smacker_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    SmackerContext *smk = s->priv_data;
    int flags;
    int ret;
    int i;
    int frame_size = 0;
    int palchange = 0;

    if (url_feof(s->pb) || smk->cur_frame >= smk->frames)
        return AVERROR_EOF;

    /* if we demuxed all streams, pass another frame */
    if(smk->curstream < 0) {
        avio_seek(s->pb, smk->nextpos, 0);
        frame_size = smk->frm_size[smk->cur_frame] & (~3);
        flags = smk->frm_flags[smk->cur_frame];
        /* handle palette change event */
        if(flags & SMACKER_PAL){
            int size, sz, t, off, j, pos;
            uint8_t *pal = smk->pal;
            uint8_t oldpal[768];

            memcpy(oldpal, pal, 768);
            size = avio_r8(s->pb);
            size = size * 4 - 1;
            if(size + 1 > frame_size)
                return AVERROR_INVALIDDATA;
            frame_size -= size;
            frame_size--;
            sz = 0;
            pos = avio_tell(s->pb) + size;
            while(sz < 256){
                t = avio_r8(s->pb);
                if(t & 0x80){ /* skip palette entries */
                    sz += (t & 0x7F) + 1;
                    pal += ((t & 0x7F) + 1) * 3;
                } else if(t & 0x40){ /* copy with offset */
                    off = avio_r8(s->pb);
                    j = (t & 0x3F) + 1;
                    if (off + j > 0xff) {
                        av_log(s, AV_LOG_ERROR,
                               "Invalid palette update, offset=%d length=%d extends beyond palette size\n",
                               off, j);
                        return AVERROR_INVALIDDATA;
                    }
                    off *= 3;
                    while(j-- && sz < 256) {
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
            palchange |= 1;
        }
        flags >>= 1;
        smk->curstream = -1;
        /* if audio chunks are present, put them to stack and retrieve later */
        for(i = 0; i < 7; i++) {
            if(flags & 1) {
                unsigned int size;
                uint8_t *tmpbuf;

                size = avio_rl32(s->pb) - 4;
                if(size + 4L > frame_size)
                    return AVERROR_INVALIDDATA;
                frame_size -= size;
                frame_size -= 4;
                smk->curstream++;
                tmpbuf = av_realloc(smk->bufs[smk->curstream], size);
                if (!tmpbuf)
                    return AVERROR(ENOMEM);
                smk->bufs[smk->curstream] = tmpbuf;
                smk->buf_sizes[smk->curstream] = size;
                ret = avio_read(s->pb, smk->bufs[smk->curstream], size);
                if(ret != size)
                    return AVERROR(EIO);
                smk->stream_id[smk->curstream] = smk->indexes[i];
            }
            flags >>= 1;
        }
        if (frame_size < 0)
            return AVERROR_INVALIDDATA;
        if (av_new_packet(pkt, frame_size + 769))
            return AVERROR(ENOMEM);
        if(smk->frm_size[smk->cur_frame] & 1)
            palchange |= 2;
        pkt->data[0] = palchange;
        memcpy(pkt->data + 1, smk->pal, 768);
        ret = avio_read(s->pb, pkt->data + 769, frame_size);
        if(ret != frame_size)
            return AVERROR(EIO);
        pkt->stream_index = smk->videoindex;
        pkt->size = ret + 769;
        smk->cur_frame++;
        smk->nextpos = avio_tell(s->pb);
    } else {
        if (av_new_packet(pkt, smk->buf_sizes[smk->curstream]))
            return AVERROR(ENOMEM);
        memcpy(pkt->data, smk->bufs[smk->curstream], smk->buf_sizes[smk->curstream]);
        pkt->size = smk->buf_sizes[smk->curstream];
        pkt->stream_index = smk->stream_id[smk->curstream];
        pkt->pts = smk->aud_pts[smk->curstream];
        smk->aud_pts[smk->curstream] += AV_RL32(pkt->data);
        smk->curstream--;
    }

    return 0;
}

static int smacker_read_close(AVFormatContext *s)
{
    SmackerContext *smk = s->priv_data;
    int i;

    for(i = 0; i < 7; i++)
        av_free(smk->bufs[i]);
    av_free(smk->frm_size);
    av_free(smk->frm_flags);

    return 0;
}

AVInputFormat ff_smacker_demuxer = {
    .name           = "smk",
    .long_name      = NULL_IF_CONFIG_SMALL("Smacker"),
    .priv_data_size = sizeof(SmackerContext),
    .read_probe     = smacker_probe,
    .read_header    = smacker_read_header,
    .read_packet    = smacker_read_packet,
    .read_close     = smacker_read_close,
};
