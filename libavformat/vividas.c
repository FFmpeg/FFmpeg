/*
 * Vividas VIV format Demuxer
 * Copyright (c) 2012 Krzysztof Klinikowski
 * Copyright (c) 2010 Andrzej Szombierski
 * based on vivparse Copyright (c) 2007 Måns Rullgård
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
 * @brief Vividas VIV (.viv) file demuxer
 * @author Andrzej Szombierski [qq at kuku eu org] (2010-07)
 * @sa http://wiki.multimedia.cx/index.php?title=Vividas_VIV
 */

#include "libavutil/intreadwrite.h"
#include "avio_internal.h"
#include "avformat.h"
#include "internal.h"

#define MAX_AUDIO_SUBPACKETS 100

typedef struct VIV_SB_block {
    int size, n_packets;
    int64_t byte_offset;
    int64_t packet_offset;
} VIV_SB_block;

typedef struct VIV_SB_entry {
    int size, flag;
} VIV_SB_entry;

typedef struct VIV_AudioSubpacket {
    int start, pcm_bytes;
} VIV_AudioSubpacket;

typedef struct VividasDemuxContext {
    int n_sb_blocks;
    VIV_SB_block *sb_blocks;

    uint32_t sb_key;
    int64_t sb_offset;

    int current_sb, current_sb_entry;
    uint8_t *sb_buf;
    AVIOContext *sb_pb;
    int n_sb_entries;
    VIV_SB_entry *sb_entries;

    int n_audio_subpackets;
    int current_audio_subpacket;

    int64_t audio_sample;

    VIV_AudioSubpacket audio_subpackets[MAX_AUDIO_SUBPACKETS];
} VividasDemuxContext;

static int viv_probe(const AVProbeData *p)
{
    if (memcmp(p->buf, "vividas03", 9))
        return 0;

    return AVPROBE_SCORE_MAX;
}

static const unsigned short keybits[32] = {
     163,  416,  893,   82,  223,  572, 1137,  430,
     659, 1104,   13,  626,  695,  972, 1465,  686,
     843, 1216,  317, 1122, 1383,   92,  513, 1158,
    1243,   48,  573, 1306, 1495,  396, 1009,  350,
};

static uint32_t decode_key(uint8_t *buf)
{
    uint32_t key = 0;

    for (int i = 0; i < 32; i++) {
        unsigned p = keybits[i];
        key |= !!(buf[p>>3] & (1<<(p&7))) << i;
    }

    return key;
}

static void put_v(uint8_t *p, unsigned v)
{
    if (v>>28)
        *p++ = ((v>>28)&0x7f)|0x80;
    if (v>>21)
        *p++ = ((v>>21)&0x7f)|0x80;
    if (v>>14)
        *p++ = ((v>>14)&0x7f)|0x80;
    if (v>>7)
        *p++ =  ((v>>7)&0x7f)|0x80;
}

static unsigned recover_key(unsigned char sample[4], unsigned expected_size)
{
    unsigned char plaintext[8] = { 'S', 'B' };

    put_v(plaintext+2, expected_size);

    return (sample[0]^plaintext[0])|
        ((sample[1]^plaintext[1])<<8)|
        ((sample[2]^plaintext[2])<<16)|
        ((sample[3]^plaintext[3])<<24);
}

static void xor_block(void *p1, void *p2, unsigned size, int key, unsigned *key_ptr)
{
    unsigned *d1 = p1;
    unsigned *d2 = p2;
    unsigned k = *key_ptr;

    size >>= 2;

    while (size > 0) {
        *d2 = *d1 ^ (HAVE_BIGENDIAN ? av_bswap32(k) : k);
        k += key;
        d1++;
        d2++;
        size--;
    }

    *key_ptr = k;
}

static void decode_block(uint8_t *src, uint8_t *dest, unsigned size,
                         uint32_t key, uint32_t *key_ptr,
                         int align)
{
    unsigned s = size;
    char tmp[4];
    int a2;

    if (!size)
        return;

    align &= 3;
    a2 = (4 - align) & 3;

    if (align) {
        uint32_t tmpkey = *key_ptr - key;
        memcpy(tmp + align, src, a2);
        xor_block(tmp, tmp, 4, key, &tmpkey);
        memcpy(dest, tmp + align, a2);
        s -= a2;
    }

    if (s >= 4) {
        if (!align)
            align = 4;
        xor_block(src + a2, dest + a2, s & ~3,
                  key, key_ptr);
        s &= 3;
    }

    if (s) {
        size -= s;
        memcpy(tmp, src + size, s);
        xor_block(&tmp, &tmp, 4, key, key_ptr);
        memcpy(dest + size, tmp, s);
    }
}

static uint32_t get_v(uint8_t *p)
{
    uint32_t v = 0;

    do {
        if (v >= UINT_MAX / 128 - *p)
            return v;
        v <<= 7;
        v += *p & 0x7f;
    } while (*p++ & 0x80);

    return v;
}

static uint8_t *read_vblock(AVIOContext *src, uint32_t *size,
                            uint32_t key, uint32_t *k2, int align)
{
    uint8_t tmp[4];
    uint8_t *buf;
    unsigned n;

    if (avio_read(src, tmp, 4) != 4)
        return NULL;

    decode_block(tmp, tmp, 4, key, k2, align);

    n = get_v(tmp);
    if (!n)
        return NULL;

    buf = av_malloc(n);
    if (!buf)
        return NULL;

    *size = n;
    n -= 4;

    memcpy(buf, tmp, 4);

    if (avio_read(src, buf + 4, n) == n) {
        decode_block(buf + 4, buf + 4, n, key, k2, align + 4);
    } else {
        av_free(buf);
        buf = NULL;
    }

    return buf;
}

static uint8_t *read_sb_block(AVIOContext *src, unsigned *size,
                              uint32_t *key, unsigned expected_size)
{
    uint8_t *buf;
    uint8_t ibuf[8], sbuf[8];
    uint32_t k2;
    unsigned n;

    if (avio_read(src, ibuf, 8) < 8)
        return NULL;

    k2 = *key;
    decode_block(ibuf, sbuf, 8, *key, &k2, 0);

    n = get_v(sbuf+2);

    if (sbuf[0] != 'S' || sbuf[1] != 'B' || (expected_size>0 && n != expected_size)) {
        uint32_t tmpkey = recover_key(ibuf, expected_size);
        k2 = tmpkey;
        decode_block(ibuf, sbuf, 8, tmpkey, &k2, 0);
        n = get_v(sbuf+2);
        if (sbuf[0] != 'S' || sbuf[1] != 'B' || expected_size != n)
            return NULL;
        *key = tmpkey;
    }

    buf = av_malloc(n);
    if (!buf)
        return NULL;

    memcpy(buf, sbuf, 8);

    *size = n;
    n -= 8;

    if (avio_read(src, buf+8, n) < n) {
        av_free(buf);
        return NULL;
    }

    decode_block(buf + 8, buf + 8, n, *key, &k2, 0);

    return buf;
}

static void track_header(VividasDemuxContext *viv, AVFormatContext *s,  uint8_t *buf, int size)
{
    int i,j;
    int64_t off;
    int val_1;
    int num_video, num_audio;
    AVIOContext *pb;

    pb = avio_alloc_context(buf, size, 0, NULL, NULL, NULL, NULL);
    if (!pb)
        return;

    ffio_read_varlen(pb); // track_header_len
    avio_r8(pb); // '1'

    val_1 = ffio_read_varlen(pb);

    for (i=0;i<val_1;i++) {
        int c = avio_r8(pb);
        for (j=0;j<c;j++) {
            avio_r8(pb); // val_3
            avio_r8(pb); // val_4
        }
    }

    avio_r8(pb); // num_streams

    off = avio_tell(pb);
    off += ffio_read_varlen(pb); // val_5

    avio_r8(pb); // '2'
    num_video = avio_r8(pb);

    avio_seek(pb, off, SEEK_SET);
    if (num_video != 1)
        av_log(s, AV_LOG_WARNING, "number of video tracks %d is not 1\n", num_video);

    for (i = 0; i < num_video; i++) {
        AVStream *st = avformat_new_stream(s, NULL);

        st->id = i;

        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codecpar->codec_id = AV_CODEC_ID_VP6;

        off = avio_tell(pb);
        off += ffio_read_varlen(pb);
        avio_r8(pb); // '3'
        avio_r8(pb); // val_7
        st->time_base.num = avio_rl32(pb); // frame_time
        st->time_base.den = avio_rl32(pb); // time_base
        st->nb_frames = avio_rl32(pb); // n frames
        st->codecpar->width = avio_rl16(pb); // width
        st->codecpar->height = avio_rl16(pb); // height
        avio_r8(pb); // val_8
        avio_rl32(pb); // val_9

        avio_seek(pb, off, SEEK_SET);
    }

    off = avio_tell(pb);
    off += ffio_read_varlen(pb); // val_10
    avio_r8(pb); // '4'
    num_audio = avio_r8(pb);
    avio_seek(pb, off, SEEK_SET);

    if (num_audio != 1)
        av_log(s, AV_LOG_WARNING, "number of audio tracks %d is not 1\n", num_audio);

    for(i=0;i<num_audio;i++) {
        int q;
        AVStream *st = avformat_new_stream(s, NULL);

        st->id = num_video + i;

        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id = AV_CODEC_ID_VORBIS;

        off = avio_tell(pb);
        off += ffio_read_varlen(pb); // length
        avio_r8(pb); // '5'
        avio_r8(pb); //codec_id
        avio_rl16(pb); //codec_subid
        st->codecpar->channels = avio_rl16(pb); // channels
        st->codecpar->sample_rate = avio_rl32(pb); // sample_rate
        avio_seek(pb, 10, SEEK_CUR); // data_1
        q = avio_r8(pb);
        avio_seek(pb, q, SEEK_CUR); // data_2
        avio_r8(pb); // zeropad

        if (avio_tell(pb) < off) {
            int num_data;
            int xd_size = 0;
            int data_len[256];
            int offset = 1;
            uint8_t *p;
            ffio_read_varlen(pb); // val_13
            avio_r8(pb); // '19'
            ffio_read_varlen(pb); // len_3
            num_data = avio_r8(pb);
            for (j = 0; j < num_data; j++) {
                data_len[j] = ffio_read_varlen(pb);
                xd_size += data_len[j];
            }

            st->codecpar->extradata_size = 64 + xd_size + xd_size / 255;
            if (ff_alloc_extradata(st->codecpar, st->codecpar->extradata_size))
                return;

            p = st->codecpar->extradata;
            p[0] = 2;

            for (j = 0; j < num_data - 1; j++)
                offset += av_xiphlacing(&p[offset], data_len[j]);

            for (j = 0; j < num_data; j++) {
                avio_read(pb, &p[offset], data_len[j]);
                offset += data_len[j];
            }

            if (offset < st->codecpar->extradata_size)
                st->codecpar->extradata_size = offset;
        }
    }

    av_free(pb);
}

static void track_index(VividasDemuxContext *viv, AVFormatContext *s, uint8_t *buf, unsigned size)
{
    int64_t off;
    int64_t poff;
    int maxnp=0;
    AVIOContext *pb;
    int i;

    pb = avio_alloc_context(buf, size, 0, NULL, NULL, NULL, NULL);
    if (!pb)
        return;

    ffio_read_varlen(pb); // track_index_len
    avio_r8(pb); // 'c'
    viv->n_sb_blocks = ffio_read_varlen(pb);
    viv->sb_blocks = av_calloc(viv->n_sb_blocks, sizeof(VIV_SB_block));
    if (!viv->sb_blocks) {
        viv->n_sb_blocks = 0;
        av_free(pb);
        return;
    }

    off = 0;
    poff = 0;

    for (i = 0; i < viv->n_sb_blocks; i++) {
        viv->sb_blocks[i].byte_offset = off;
        viv->sb_blocks[i].packet_offset = poff;

        viv->sb_blocks[i].size = ffio_read_varlen(pb);
        viv->sb_blocks[i].n_packets = ffio_read_varlen(pb);

        off += viv->sb_blocks[i].size;
        poff += viv->sb_blocks[i].n_packets;


        if (maxnp < viv->sb_blocks[i].n_packets)
            maxnp = viv->sb_blocks[i].n_packets;
    }

    viv->sb_entries = av_calloc(maxnp, sizeof(VIV_SB_entry));
    av_free(pb);
}

static void load_sb_block(AVFormatContext *s, VividasDemuxContext *viv, unsigned expected_size)
{
    uint32_t size = 0;
    int i;
    AVIOContext *pb = 0;

    if (viv->sb_pb) {
        av_free(viv->sb_pb);
        viv->sb_pb = NULL;
    }

    if (viv->sb_buf)
        av_free(viv->sb_buf);

    viv->sb_buf = read_sb_block(s->pb, &size, &viv->sb_key, expected_size);
    if (!viv->sb_buf) {
        return;
    }

    pb = avio_alloc_context(viv->sb_buf, size, 0, NULL, NULL, NULL, NULL);
    if (!pb)
        return;

    viv->sb_pb = pb;

    avio_r8(pb); //  'S'
    avio_r8(pb); //  'B'
    ffio_read_varlen(pb); //  size
    avio_r8(pb); //  junk
    ffio_read_varlen(pb); // first packet

    viv->n_sb_entries = viv->sb_blocks[viv->current_sb].n_packets;

    for (i = 0; i < viv->n_sb_entries; i++) {
        viv->sb_entries[i].size = ffio_read_varlen(pb);
        viv->sb_entries[i].flag = avio_r8(pb);
    }

    ffio_read_varlen(pb);
    avio_r8(pb);

    viv->current_sb_entry = 0;
}

static int viv_read_header(AVFormatContext *s)
{
    VividasDemuxContext *viv = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t header_end;
    int num_tracks;
    uint32_t key, k2;
    uint32_t v;
    uint8_t keybuffer[187];
    uint32_t b22_size = 0;
    uint32_t b22_key = 0;
    uint8_t *buf = 0;

    avio_skip(pb, 9);

    header_end = avio_tell(pb);

    header_end += ffio_read_varlen(pb);

    num_tracks = avio_r8(pb);

    if (num_tracks != 1) {
        av_log(s, AV_LOG_ERROR, "number of tracks %d is not 1\n", num_tracks);
        return AVERROR(EINVAL);
    }

    v = avio_r8(pb);
    avio_seek(pb, v, SEEK_CUR);

    avio_read(pb, keybuffer, 187);
    key = decode_key(keybuffer);
    viv->sb_key = key;

    avio_rl32(pb);

    for (;;) {
        int64_t here = avio_tell(pb);
        int block_len, block_type;

        if (here >= header_end)
            break;

        block_len = ffio_read_varlen(pb);
        block_type = avio_r8(pb);

        if (block_type == 22) {
            avio_read(pb, keybuffer, 187);
            b22_key = decode_key(keybuffer);
            b22_size = avio_rl32(pb);
        }

        avio_seek(pb, here + block_len, SEEK_SET);
    }

    if (b22_size) {
        k2 = b22_key;
        buf = read_vblock(pb, &v, b22_key, &k2, 0);
        if (!buf)
            return AVERROR(EIO);

        av_free(buf);
    }

    k2 = key;
    buf = read_vblock(pb, &v, key, &k2, 0);
    if (!buf)
        return AVERROR(EIO);
    track_header(viv, s, buf, v);
    av_free(buf);

    buf = read_vblock(pb, &v, key, &k2, v);
    if (!buf)
        return AVERROR(EIO);
    track_index(viv, s, buf, v);
    av_free(buf);

    viv->sb_offset = avio_tell(pb);
    if (viv->n_sb_blocks > 0) {
        viv->current_sb = 0;
        load_sb_block(s, viv, viv->sb_blocks[0].size);
    } else {
        viv->current_sb = -1;
    }

    return 0;
}

static int viv_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    VividasDemuxContext *viv = s->priv_data;
    AVIOContext *pb;
    int64_t off;
    int ret;

    if (!viv->sb_pb)
        return AVERROR(EIO);
    if (avio_feof(viv->sb_pb))
        return AVERROR_EOF;

    if (viv->current_audio_subpacket < viv->n_audio_subpackets) {
        AVStream *astream;
        int size = viv->audio_subpackets[viv->current_audio_subpacket+1].start - viv->audio_subpackets[viv->current_audio_subpacket].start;

        pb = viv->sb_pb;
        ret = av_get_packet(pb, pkt, size);
        if (ret < 0)
            return ret;
        pkt->pos += viv->sb_offset + viv->sb_blocks[viv->current_sb].byte_offset;

        pkt->stream_index = 1;
        astream = s->streams[pkt->stream_index];

        pkt->pts = av_rescale(viv->audio_sample, astream->time_base.den, astream->time_base.num) / astream->codecpar->sample_rate;
        viv->audio_sample += viv->audio_subpackets[viv->current_audio_subpacket].pcm_bytes / 2 / astream->codecpar->channels;
        pkt->flags |= AV_PKT_FLAG_KEY;
        viv->current_audio_subpacket++;
        return 0;
    }

    if (viv->current_sb_entry >= viv->n_sb_entries) {
        if (viv->current_sb+1 >= viv->n_sb_blocks)
            return AVERROR(EIO);
        viv->current_sb++;

        load_sb_block(s, viv, 0);
        viv->current_sb_entry = 0;
    }

    pb = viv->sb_pb;
    if (!pb)
        return AVERROR(EIO);
    off = avio_tell(pb);
    off += viv->sb_entries[viv->current_sb_entry].size;

    if (viv->sb_entries[viv->current_sb_entry].flag == 0) {
        uint64_t v_size = ffio_read_varlen(pb);

        ffio_read_varlen(pb);
        if (v_size > INT_MAX)
            return AVERROR_INVALIDDATA;
        ret = av_get_packet(pb, pkt, v_size);
        if (ret < 0)
            return ret;
        pkt->pos += viv->sb_offset + viv->sb_blocks[viv->current_sb].byte_offset;

        pkt->pts = viv->sb_blocks[viv->current_sb].packet_offset + viv->current_sb_entry;
        pkt->flags |= (pkt->data[0]&0x80)?0:AV_PKT_FLAG_KEY;
        pkt->stream_index = 0;

        for (int i = 0; i < MAX_AUDIO_SUBPACKETS - 1; i++) {
            int start, pcm_bytes;
            start = ffio_read_varlen(pb);
            pcm_bytes = ffio_read_varlen(pb);

            if (i > 0 && start == 0)
                break;

            viv->n_audio_subpackets = i + 1;
            viv->audio_subpackets[i].start = start;
            viv->audio_subpackets[i].pcm_bytes = pcm_bytes;
        }
        viv->audio_subpackets[viv->n_audio_subpackets].start = (int)(off - avio_tell(pb));
        viv->current_audio_subpacket = 0;

    } else {
        uint64_t v_size = ffio_read_varlen(pb);

        if (v_size > INT_MAX)
            return AVERROR_INVALIDDATA;
        ret = av_get_packet(pb, pkt, v_size);
        if (ret < 0)
            return ret;
        pkt->pos += viv->sb_offset + viv->sb_blocks[viv->current_sb].byte_offset;
        pkt->pts = viv->sb_blocks[viv->current_sb].packet_offset + viv->current_sb_entry;
        pkt->flags |= (pkt->data[0] & 0x80) ? 0 : AV_PKT_FLAG_KEY;
        pkt->stream_index = 0;
    }

    viv->current_sb_entry++;

    return 0;
}

static int viv_read_close(AVFormatContext *s)
{
    VividasDemuxContext *viv = s->priv_data;

    av_freep(&viv->sb_pb);
    av_freep(&viv->sb_buf);
    av_freep(&viv->sb_blocks);
    av_freep(&viv->sb_entries);

    return 0;
}

static int viv_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    VividasDemuxContext *viv = s->priv_data;
    int64_t frame;

    if (stream_index == 0)
        frame = timestamp;
    else
        frame = av_rescale_q(timestamp, s->streams[0]->time_base, s->streams[stream_index]->time_base);

    for (int i = 0; i < viv->n_sb_blocks; i++) {
        if (frame >= viv->sb_blocks[i].packet_offset && frame < viv->sb_blocks[i].packet_offset + viv->sb_blocks[i].n_packets) {
            // flush audio packet queue
            viv->current_audio_subpacket = 0;
            viv->n_audio_subpackets = 0;
            viv->current_sb = i;
            // seek to ith sb block
            avio_seek(s->pb, viv->sb_offset + viv->sb_blocks[i].byte_offset, SEEK_SET);
            // load the block
            load_sb_block(s, viv, 0);
            // most problematic part: guess audio offset
            viv->audio_sample = av_rescale_q(viv->sb_blocks[i].packet_offset, av_make_q(s->streams[1]->codecpar->sample_rate, 1), av_inv_q(s->streams[0]->time_base));
            // hand-tuned 1.s a/v offset
            viv->audio_sample += s->streams[1]->codecpar->sample_rate;
            viv->current_sb_entry = 0;
            return 1;
        }
    }
    return 0;
}

AVInputFormat ff_vividas_demuxer = {
    .name           = "vividas",
    .long_name      = NULL_IF_CONFIG_SMALL("Vividas VIV"),
    .priv_data_size = sizeof(VividasDemuxContext),
    .read_probe     = viv_probe,
    .read_header    = viv_read_header,
    .read_packet    = viv_read_packet,
    .read_close     = viv_read_close,
    .read_seek      = viv_read_seek,
};
