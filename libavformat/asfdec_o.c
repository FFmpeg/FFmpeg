/*
 * Microsoft Advanced Streaming Format demuxer
 * Copyright (c) 2014 Alexandra Hájková
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

#include "libavutil/attributes.h"
#include "libavutil/avstring.h"
#include "libavutil/bswap.h"
#include "libavutil/common.h"
#include "libavutil/dict.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/time_internal.h"

#include "avformat.h"
#include "avio_internal.h"
#include "avlanguage.h"
#include "id3v2.h"
#include "internal.h"
#include "riff.h"
#include "asf.h"
#include "asfcrypt.h"

#define ASF_BOOL                              0x2
#define ASF_WORD                              0x5
#define ASF_GUID                              0x6
#define ASF_DWORD                             0x3
#define ASF_QWORD                             0x4
#define ASF_UNICODE                           0x0
#define ASF_FLAG_BROADCAST                    0x1
#define ASF_BYTE_ARRAY                        0x1
#define ASF_TYPE_AUDIO                        0x2
#define ASF_TYPE_VIDEO                        0x1
#define ASF_STREAM_NUM                        0x7F
#define ASF_MAX_STREAMS                       128
#define BMP_HEADER_SIZE                       40
#define ASF_NUM_OF_PAYLOADS                   0x3F
#define ASF_ERROR_CORRECTION_LENGTH_TYPE      0x60
#define ASF_PACKET_ERROR_CORRECTION_DATA_SIZE 0x2

typedef struct GUIDParseTable {
    const char *name;
    ff_asf_guid guid;
    int (*read_object)(AVFormatContext *, const struct GUIDParseTable *);
    int is_subobject;
} GUIDParseTable;

typedef struct ASFPacket {
    AVPacket avpkt;
    int64_t dts;
    uint32_t frame_num; // ASF payloads with the same number are parts of the same frame
    int flags;
    int data_size;
    int duration;
    int size_left;
    uint8_t stream_index;
} ASFPacket;

typedef struct ASFStream {
    uint8_t stream_index; // from packet header
    int index;  // stream index in AVFormatContext, set in asf_read_stream_properties
    int type;
    int indexed; // added index entries from the Simple Index Object or not
    int8_t span;   // for deinterleaving
    uint16_t virtual_pkt_len;
    uint16_t virtual_chunk_len;
    int16_t lang_idx;
    ASFPacket pkt;
} ASFStream;

typedef struct ASFStreamData{
    char langs[32];
    AVDictionary *asf_met; // for storing per-stream metadata
    AVRational aspect_ratio;
} ASFStreamData;

typedef struct ASFContext {
    int data_reached;
    int is_simple_index; // is simple index present or not 1/0
    int is_header;

    uint64_t preroll;
    uint64_t nb_packets; // ASF packets
    uint32_t packet_size;
    int64_t send_time;
    int duration;

    uint32_t b_flags;    // flags with broadcast flag
    uint32_t prop_flags; // file properties object flags

    uint64_t data_size; // data object size
    uint64_t unknown_size; // size of the unknown object

    int64_t offset; // offset of the current object

    int64_t data_offset;
    int64_t first_packet_offset; // packet offset
    int64_t unknown_offset;   // for top level header objects or subobjects without specified behavior

    // ASF file must not contain more than 128 streams according to the specification
    ASFStream *asf_st[ASF_MAX_STREAMS];
    ASFStreamData asf_sd[ASF_MAX_STREAMS];
    int nb_streams;

    int stream_index; // from packet header, for the subpayload case

    // packet parameters
    uint64_t sub_header_offset; // offset of subpayload header
    int64_t sub_dts;
    uint8_t dts_delta; // for subpayloads
    uint32_t packet_size_internal; // packet size stored inside ASFPacket, can be 0
    int64_t packet_offset; // offset of the current packet inside Data Object
    uint32_t pad_len; // padding after payload
    uint32_t rep_data_len;

    // packet state
    uint64_t sub_left;  // subpayloads left or not
    unsigned int nb_sub; // number of subpayloads read so far from the current ASF packet
    uint16_t mult_sub_len; // total length of subpayloads array inside multiple payload
    uint64_t nb_mult_left; // multiple payloads left
    int return_subpayload;
    enum {
        PARSE_PACKET_HEADER,
        READ_SINGLE,
        READ_MULTI,
        READ_MULTI_SUB
    } state;
} ASFContext;

static int detect_unknown_subobject(AVFormatContext *s, int64_t offset, int64_t size);
static const GUIDParseTable *find_guid(ff_asf_guid guid);

static int asf_probe(const AVProbeData *pd)
{
    /* check file header */
    if (!ff_guidcmp(pd->buf, &ff_asf_header))
        return AVPROBE_SCORE_MAX/2;
    else
        return 0;
}

static void swap_guid(ff_asf_guid guid)
{
    FFSWAP(unsigned char, guid[0], guid[3]);
    FFSWAP(unsigned char, guid[1], guid[2]);
    FFSWAP(unsigned char, guid[4], guid[5]);
    FFSWAP(unsigned char, guid[6], guid[7]);
}

static void align_position(AVIOContext *pb,  int64_t offset, uint64_t size)
{
    if (size < INT64_MAX - offset && avio_tell(pb) != offset + size)
        avio_seek(pb, offset + size, SEEK_SET);
}

static int asf_read_unknown(AVFormatContext *s, const GUIDParseTable *g)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    uint64_t size   = avio_rl64(pb);
    int ret;

    if (size > INT64_MAX)
        return AVERROR_INVALIDDATA;

    if (asf->is_header)
        asf->unknown_size = size;
    asf->is_header = 0;
    if (!g->is_subobject) {
        if (!(ret = strcmp(g->name, "Header Extension")))
            avio_skip(pb, 22); // skip reserved fields and Data Size
        if ((ret = detect_unknown_subobject(s, asf->unknown_offset,
                                            asf->unknown_size)) < 0)
            return ret;
    } else {
        if (size < 24) {
            av_log(s, AV_LOG_ERROR, "Too small size %"PRIu64" (< 24).\n", size);
            return AVERROR_INVALIDDATA;
        }
        avio_skip(pb, size - 24);
    }

    return 0;
}

static int get_asf_string(AVIOContext *pb, int maxlen, char *buf, int buflen)
{
    char *q = buf;
    int ret = 0;
    if (buflen <= 0)
        return AVERROR(EINVAL);
    while (ret + 1 < maxlen) {
        uint8_t tmp;
        uint32_t ch;
        GET_UTF16(ch, (ret += 2) <= maxlen ? avio_rl16(pb) : 0, break;);
        PUT_UTF8(ch, tmp, if (q - buf < buflen - 1) *q++ = tmp;)
    }
    *q = 0;

    return ret;
}

static int asf_read_marker(AVFormatContext *s, const GUIDParseTable *g)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    uint64_t size   = avio_rl64(pb);
    int i, nb_markers, ret;
    size_t len;
    char name[1024];

    avio_skip(pb, 8);
    avio_skip(pb, 8); // skip reserved GUID
    nb_markers = avio_rl32(pb);
    avio_skip(pb, 2); // skip reserved field
    len = avio_rl16(pb);
    for (i = 0; i < len; i++)
        avio_skip(pb, 1);

    for (i = 0; i < nb_markers; i++) {
        int64_t pts;

        avio_skip(pb, 8);
        pts = avio_rl64(pb);
        pts -= asf->preroll * 10000;
        avio_skip(pb, 2); // entry length
        avio_skip(pb, 4); // send time
        avio_skip(pb, 4); // flags
        len = avio_rl32(pb);

        if ((ret = avio_get_str16le(pb, len, name,
                                    sizeof(name))) < len)
            avio_skip(pb, len - ret);
        avpriv_new_chapter(s, i, (AVRational) { 1, 10000000 }, pts,
                           AV_NOPTS_VALUE, name);
    }
    align_position(pb, asf->offset, size);

    return 0;
}

static int asf_read_metadata(AVFormatContext *s, const char *title, uint16_t len,
                             unsigned char *ch, uint16_t buflen)
{
    AVIOContext *pb = s->pb;

    avio_get_str16le(pb, len, ch, buflen);
    if (ch[0]) {
        if (av_dict_set(&s->metadata, title, ch, 0) < 0)
            av_log(s, AV_LOG_WARNING, "av_dict_set failed.\n");
    }

    return 0;
}

static int asf_read_value(AVFormatContext *s, const uint8_t *name,
                          uint16_t val_len, int type, AVDictionary **met)
{
    int ret;
    uint8_t *value;
    uint16_t buflen = 2 * val_len + 1;
    AVIOContext *pb = s->pb;

    value = av_malloc(buflen);
    if (!value)
        return AVERROR(ENOMEM);
    if (type == ASF_UNICODE) {
        // get_asf_string reads UTF-16 and converts it to UTF-8 which needs longer buffer
        if ((ret = get_asf_string(pb, val_len, value, buflen)) < 0)
            goto failed;
        if (av_dict_set(met, name, value, 0) < 0)
            av_log(s, AV_LOG_WARNING, "av_dict_set failed.\n");
    } else {
        char buf[256];
        if (val_len > sizeof(buf)) {
            ret = AVERROR_INVALIDDATA;
            goto failed;
        }
        if ((ret = avio_read(pb, value, val_len)) < 0)
            goto failed;
        if (ret < 2 * val_len)
            value[ret] = '\0';
        else
            value[2 * val_len - 1] = '\0';
        snprintf(buf, sizeof(buf), "%s", value);
        if (av_dict_set(met, name, buf, 0) < 0)
            av_log(s, AV_LOG_WARNING, "av_dict_set failed.\n");
    }
    av_freep(&value);

    return 0;

failed:
    av_freep(&value);
    return ret;
}
static int asf_read_generic_value(AVIOContext *pb, int type, uint64_t *value)
{

    switch (type) {
    case ASF_BOOL:
        *value = avio_rl16(pb);
        break;
    case ASF_DWORD:
        *value = avio_rl32(pb);
        break;
    case ASF_QWORD:
        *value = avio_rl64(pb);
        break;
    case ASF_WORD:
        *value = avio_rl16(pb);
        break;
    default:
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int asf_set_metadata(AVFormatContext *s, const uint8_t *name,
                            int type, AVDictionary **met)
{
    AVIOContext *pb = s->pb;
    uint64_t value;
    char buf[32];
    int ret;

    ret = asf_read_generic_value(pb, type, &value);
    if (ret < 0)
        return ret;

    snprintf(buf, sizeof(buf), "%"PRIu64, value);
    if (av_dict_set(met, name, buf, 0) < 0)
        av_log(s, AV_LOG_WARNING, "av_dict_set failed.\n");

    return 0;
}

/* MSDN claims that this should be "compatible with the ID3 frame, APIC",
 * but in reality this is only loosely similar */
static int asf_read_picture(AVFormatContext *s, int len)
{
    ASFContext *asf       = s->priv_data;
    AVPacket pkt          = { 0 };
    const CodecMime *mime = ff_id3v2_mime_tags;
    enum  AVCodecID id    = AV_CODEC_ID_NONE;
    char mimetype[64];
    uint8_t  *desc = NULL;
    AVStream   *st = NULL;
    int ret, type, picsize, desc_len;
    ASFStream *asf_st;

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

    ret = av_get_packet(s->pb, &pkt, picsize);
    if (ret < 0)
        goto fail;

    st  = avformat_new_stream(s, NULL);
    if (!st) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    asf->asf_st[asf->nb_streams] = av_mallocz(sizeof(*asf_st));
    asf_st = asf->asf_st[asf->nb_streams];
    if (!asf_st) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    st->disposition              |= AV_DISPOSITION_ATTACHED_PIC;
    st->codecpar->codec_type      = asf_st->type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id        = id;
    st->attached_pic              = pkt;
    st->attached_pic.stream_index = asf_st->index = st->index;
    st->attached_pic.flags       |= AV_PKT_FLAG_KEY;

    asf->nb_streams++;

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
    av_packet_unref(&pkt);
    return ret;
}

static void get_id3_tag(AVFormatContext *s, int len)
{
    ID3v2ExtraMeta *id3v2_extra_meta = NULL;

    ff_id3v2_read(s, ID3v2_DEFAULT_MAGIC, &id3v2_extra_meta, len);
    if (id3v2_extra_meta) {
        ff_id3v2_parse_apic(s, id3v2_extra_meta);
        ff_id3v2_parse_chapters(s, id3v2_extra_meta);
    }
    ff_id3v2_free_extra_meta(&id3v2_extra_meta);
}

static int process_metadata(AVFormatContext *s, const uint8_t *name, uint16_t name_len,
                            uint16_t val_len, uint16_t type, AVDictionary **met)
{
    int ret;
    ff_asf_guid guid;

    if (val_len) {
        switch (type) {
        case ASF_UNICODE:
            asf_read_value(s, name, val_len, type, met);
            break;
        case ASF_BYTE_ARRAY:
            if (!strcmp(name, "WM/Picture")) // handle cover art
                asf_read_picture(s, val_len);
            else if (!strcmp(name, "ID3")) // handle ID3 tag
                get_id3_tag(s, val_len);
            else
                asf_read_value(s, name, val_len, type, met);
            break;
        case ASF_GUID:
            ff_get_guid(s->pb, &guid);
            break;
        default:
            if ((ret = asf_set_metadata(s, name, type, met)) < 0)
                return ret;
            break;
        }
    }

    return 0;
}

static int asf_read_ext_content(AVFormatContext *s, const GUIDParseTable *g)
{
    ASFContext *asf  = s->priv_data;
    AVIOContext *pb  = s->pb;
    uint64_t size    = avio_rl64(pb);
    uint16_t nb_desc = avio_rl16(pb);
    int i, ret;

    for (i = 0; i < nb_desc; i++) {
        uint16_t name_len, type, val_len;
        uint8_t *name = NULL;

        name_len = avio_rl16(pb);
        if (!name_len)
            return AVERROR_INVALIDDATA;
        name = av_malloc(name_len);
        if (!name)
            return AVERROR(ENOMEM);
        avio_get_str16le(pb, name_len, name,
                         name_len);
        type    = avio_rl16(pb);
        // BOOL values are 16 bits long in the Metadata Object
        // but 32 bits long in the Extended Content Description Object
        if (type == ASF_BOOL)
            type = ASF_DWORD;
        val_len = avio_rl16(pb);

        ret = process_metadata(s, name, name_len, val_len, type, &s->metadata);
        av_freep(&name);
        if (ret < 0)
            return ret;
    }

    align_position(pb, asf->offset, size);
    return 0;
}

static AVStream *find_stream(AVFormatContext *s, uint16_t st_num)
{
    AVStream *st = NULL;
    ASFContext *asf = s->priv_data;
    int i;

    for (i = 0; i < asf->nb_streams; i++) {
        if (asf->asf_st[i]->stream_index == st_num) {
            st = s->streams[asf->asf_st[i]->index];
            break;
        }
    }

    return st;
}

static int asf_store_aspect_ratio(AVFormatContext *s, uint8_t st_num, uint8_t *name, int type)
{
    ASFContext *asf   = s->priv_data;
    AVIOContext *pb   = s->pb;
    uint64_t value = 0;
    int ret;

    ret = asf_read_generic_value(pb, type, &value);
    if (ret < 0)
        return ret;

    if (st_num < ASF_MAX_STREAMS) {
        if (!strcmp(name, "AspectRatioX"))
            asf->asf_sd[st_num].aspect_ratio.num = value;
        else
            asf->asf_sd[st_num].aspect_ratio.den = value;
    }
    return 0;
}

static int asf_read_metadata_obj(AVFormatContext *s, const GUIDParseTable *g)
{
    ASFContext *asf   = s->priv_data;
    AVIOContext *pb   = s->pb;
    uint64_t size     = avio_rl64(pb);
    uint16_t nb_recs  = avio_rl16(pb); // number of records in the Description Records list
    int i, ret;

    for (i = 0; i < nb_recs; i++) {
        uint16_t name_len, buflen, type, val_len, st_num;
        uint8_t *name = NULL;

        avio_skip(pb, 2); // skip reserved field
        st_num   = avio_rl16(pb);
        name_len = avio_rl16(pb);
        buflen   = 2 * name_len + 1;
        if (!name_len)
            break;
        type     = avio_rl16(pb);
        val_len  = avio_rl32(pb);
        name     = av_malloc(buflen);
        if (!name)
            return AVERROR(ENOMEM);
        avio_get_str16le(pb, name_len, name,
                         buflen);
        if (!strcmp(name, "AspectRatioX") || !strcmp(name, "AspectRatioY")) {
            ret = asf_store_aspect_ratio(s, st_num, name, type);
            if (ret < 0) {
                av_freep(&name);
                break;
            }
        } else {
            if (st_num < ASF_MAX_STREAMS) {
                if ((ret = process_metadata(s, name, name_len, val_len, type,
                                            &asf->asf_sd[st_num].asf_met)) < 0) {
                    av_freep(&name);
                    break;
                }
            }
        }
        av_freep(&name);
    }

    align_position(pb, asf->offset, size);
    return 0;
}

static int asf_read_content_desc(AVFormatContext *s, const GUIDParseTable *g)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    int i;
    static const char *const titles[] =
    { "Title", "Author", "Copyright", "Description", "Rate" };
    uint16_t len[5], buflen[5] = { 0 };
    uint8_t *ch;
    uint64_t size = avio_rl64(pb);

    for (i = 0; i < 5; i++) {
        len[i]  = avio_rl16(pb);
        // utf8 string should be <= 2 * utf16 string, extra byte for the terminator
        buflen[i]  = 2 * len[i] + 1;
    }

    for (i = 0; i < 5; i++) {
        ch = av_malloc(buflen[i]);
        if (!ch)
            return(AVERROR(ENOMEM));
        asf_read_metadata(s, titles[i], len[i], ch, buflen[i]);
        av_freep(&ch);
    }
    align_position(pb, asf->offset, size);

    return 0;
}

static int asf_read_properties(AVFormatContext *s, const GUIDParseTable *g)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    time_t creation_time;

    avio_rl64(pb); // read object size
    avio_skip(pb, 16); // skip File ID
    avio_skip(pb, 8);  // skip File size
    creation_time = avio_rl64(pb);
    if (!(asf->b_flags & ASF_FLAG_BROADCAST)) {
        struct tm tmbuf;
        struct tm *tm;
        char buf[64];

        // creation date is in 100 ns units from 1 Jan 1601, conversion to s
        creation_time /= 10000000;
        // there are 11644473600 seconds between 1 Jan 1601 and 1 Jan 1970
        creation_time -= 11644473600;
        tm = gmtime_r(&creation_time, &tmbuf);
        if (tm) {
            if (!strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm))
                buf[0] = '\0';
        } else
            buf[0] = '\0';
        if (buf[0]) {
            if (av_dict_set(&s->metadata, "creation_time", buf, 0) < 0)
                av_log(s, AV_LOG_WARNING, "av_dict_set failed.\n");
        }
    }
    asf->nb_packets  = avio_rl64(pb);
    asf->duration    = avio_rl64(pb) / 10000; // stream duration
    avio_skip(pb, 8); // skip send duration
    asf->preroll     = avio_rl64(pb);
    asf->duration   -= asf->preroll;
    asf->b_flags     = avio_rl32(pb);
    avio_skip(pb, 4); // skip minimal packet size
    asf->packet_size  = avio_rl32(pb);
    avio_skip(pb, 4); // skip max_bitrate

    return 0;
}

static int parse_video_info(AVIOContext *pb, AVStream *st)
{
    uint16_t size_asf; // ASF-specific Format Data size
    uint32_t size_bmp; // BMP_HEADER-specific Format Data size
    unsigned int tag;

    st->codecpar->width  = avio_rl32(pb);
    st->codecpar->height = avio_rl32(pb);
    avio_skip(pb, 1); // skip reserved flags
    size_asf = avio_rl16(pb);
    tag = ff_get_bmp_header(pb, st, &size_bmp);
    st->codecpar->codec_tag = tag;
    st->codecpar->codec_id  = ff_codec_get_id(ff_codec_bmp_tags, tag);
    size_bmp = FFMAX(size_asf, size_bmp);

    if (size_bmp > BMP_HEADER_SIZE &&
        size_bmp < INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE) {
        int ret;
        st->codecpar->extradata_size  = size_bmp - BMP_HEADER_SIZE;
        if (!(st->codecpar->extradata = av_malloc(st->codecpar->extradata_size +
                                               AV_INPUT_BUFFER_PADDING_SIZE))) {
            st->codecpar->extradata_size = 0;
            return AVERROR(ENOMEM);
        }
        memset(st->codecpar->extradata + st->codecpar->extradata_size , 0,
               AV_INPUT_BUFFER_PADDING_SIZE);
        if ((ret = avio_read(pb, st->codecpar->extradata,
                             st->codecpar->extradata_size)) < 0)
            return ret;
    }
    return 0;
}

static int asf_read_stream_properties(AVFormatContext *s, const GUIDParseTable *g)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    uint64_t size;
    uint32_t err_data_len, ts_data_len; // type specific data length
    uint16_t flags;
    ff_asf_guid stream_type;
    enum AVMediaType type;
    int i, ret;
    uint8_t stream_index;
    AVStream *st;
    ASFStream *asf_st;

    // ASF file must not contain more than 128 streams according to the specification
    if (asf->nb_streams >= ASF_MAX_STREAMS)
        return AVERROR_INVALIDDATA;

    size = avio_rl64(pb);
    ff_get_guid(pb, &stream_type);
    if (!ff_guidcmp(&stream_type, &ff_asf_audio_stream))
        type = AVMEDIA_TYPE_AUDIO;
    else if (!ff_guidcmp(&stream_type, &ff_asf_video_stream))
        type = AVMEDIA_TYPE_VIDEO;
    else if (!ff_guidcmp(&stream_type, &ff_asf_jfif_media))
        type = AVMEDIA_TYPE_VIDEO;
    else if (!ff_guidcmp(&stream_type, &ff_asf_command_stream))
        type = AVMEDIA_TYPE_DATA;
    else if (!ff_guidcmp(&stream_type,
                         &ff_asf_ext_stream_embed_stream_header))
        type = AVMEDIA_TYPE_UNKNOWN;
    else
        return AVERROR_INVALIDDATA;

    ff_get_guid(pb, &stream_type); // error correction type
    avio_skip(pb, 8); // skip the time offset
    ts_data_len      = avio_rl32(pb);
    err_data_len     = avio_rl32(pb);
    flags            = avio_rl16(pb); // bit 15 - Encrypted Content

    stream_index = flags & ASF_STREAM_NUM;
    for (i = 0; i < asf->nb_streams; i++)
        if (stream_index == asf->asf_st[i]->stream_index) {
            av_log(s, AV_LOG_WARNING,
                   "Duplicate stream found, this stream will be ignored.\n");
            align_position(pb, asf->offset, size);
            return 0;
        }

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 32, 1, 1000); // pts should be dword, in milliseconds
    st->codecpar->codec_type = type;
    asf->asf_st[asf->nb_streams] = av_mallocz(sizeof(*asf_st));
    if (!asf->asf_st[asf->nb_streams])
        return AVERROR(ENOMEM);
    asf_st                       = asf->asf_st[asf->nb_streams];
    asf->nb_streams++;
    asf_st->stream_index         = stream_index;
    asf_st->index                = st->index;
    asf_st->indexed              = 0;
    st->id                       = flags & ASF_STREAM_NUM;
    av_init_packet(&asf_st->pkt.avpkt);
    asf_st->pkt.data_size        = 0;
    avio_skip(pb, 4); // skip reserved field

    switch (type) {
    case AVMEDIA_TYPE_AUDIO:
        asf_st->type = AVMEDIA_TYPE_AUDIO;
        if ((ret = ff_get_wav_header(s, pb, st->codecpar, ts_data_len, 0)) < 0)
            return ret;
        break;
    case AVMEDIA_TYPE_VIDEO:
        asf_st->type = AVMEDIA_TYPE_VIDEO;
        if ((ret = parse_video_info(pb, st)) < 0)
            return ret;
        break;
    default:
        avio_skip(pb, ts_data_len);
        break;
    }

    if (err_data_len) {
        if (type == AVMEDIA_TYPE_AUDIO) {
            uint8_t span = avio_r8(pb);
            if (span > 1) {
                asf_st->span              = span;
                asf_st->virtual_pkt_len   = avio_rl16(pb);
                asf_st->virtual_chunk_len = avio_rl16(pb);
                if (!asf_st->virtual_chunk_len || !asf_st->virtual_pkt_len)
                    return AVERROR_INVALIDDATA;
                avio_skip(pb, err_data_len - 5);
            } else
                avio_skip(pb, err_data_len - 1);
        } else
            avio_skip(pb, err_data_len);
    }

    align_position(pb, asf->offset, size);

    return 0;
}

static void set_language(AVFormatContext *s, const char *rfc1766, AVDictionary **met)
{
    // language abbr should contain at least 2 chars
    if (rfc1766 && strlen(rfc1766) > 1) {
        const char primary_tag[3] = { rfc1766[0], rfc1766[1], '\0' }; // ignore country code if any
        const char *iso6392       = ff_convert_lang_to(primary_tag,
                                                       AV_LANG_ISO639_2_BIBL);
        if (iso6392)
            if (av_dict_set(met, "language", iso6392, 0) < 0)
                av_log(s, AV_LOG_WARNING, "av_dict_set failed.\n");
    }
}

static int asf_read_ext_stream_properties(AVFormatContext *s, const GUIDParseTable *g)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st    = NULL;
    ff_asf_guid guid;
    uint16_t nb_st_name, nb_pay_exts, st_num, lang_idx;
    int i, ret;
    uint32_t bitrate;
    uint64_t start_time, end_time, time_per_frame;
    uint64_t size = avio_rl64(pb);

    start_time = avio_rl64(pb);
    end_time   = avio_rl64(pb);
    bitrate    = avio_rl32(pb);
    avio_skip(pb, 28); // skip some unused values
    st_num     = avio_rl16(pb);
    st_num    &= ASF_STREAM_NUM;
    lang_idx   = avio_rl16(pb); // Stream Language ID Index
    for (i = 0; i < asf->nb_streams; i++) {
        if (st_num == asf->asf_st[i]->stream_index) {
            st                       = s->streams[asf->asf_st[i]->index];
            asf->asf_st[i]->lang_idx = lang_idx;
            break;
        }
    }
    time_per_frame = avio_rl64(pb); // average time per frame
    if (st) {
        st->start_time           = start_time;
        st->duration             = end_time - start_time;
        st->codecpar->bit_rate   = bitrate;
        st->avg_frame_rate.num   = 10000000;
        st->avg_frame_rate.den   = time_per_frame;
    }
    nb_st_name = avio_rl16(pb);
    nb_pay_exts   = avio_rl16(pb);
    for (i = 0; i < nb_st_name; i++) {
        uint16_t len;

        avio_rl16(pb); // Language ID Index
        len = avio_rl16(pb);
        avio_skip(pb, len);
    }

    for (i = 0; i < nb_pay_exts; i++) {
        uint32_t len;
        avio_skip(pb, 16); // Extension System ID
        avio_skip(pb, 2);  // Extension Data Size
        len = avio_rl32(pb);
        avio_skip(pb, len);
    }

    if ((ret = ff_get_guid(pb, &guid)) < 0) {
        align_position(pb, asf->offset, size);

        return 0;
    }

    g = find_guid(guid);
    if (g && !(strcmp(g->name, "Stream Properties"))) {
        if ((ret = g->read_object(s, g)) < 0)
            return ret;
    }

    align_position(pb, asf->offset, size);
    return 0;
}

static int asf_read_language_list(AVFormatContext *s, const GUIDParseTable *g)
{
    ASFContext *asf   = s->priv_data;
    AVIOContext *pb   = s->pb;
    int i, ret;
    uint64_t size     = avio_rl64(pb);
    uint16_t nb_langs = avio_rl16(pb);

    if (nb_langs < ASF_MAX_STREAMS) {
        for (i = 0; i < nb_langs; i++) {
            size_t len;
            len = avio_r8(pb);
            if (!len)
                len = 6;
            if ((ret = get_asf_string(pb, len, asf->asf_sd[i].langs,
                                      sizeof(asf->asf_sd[i].langs))) < 0) {
                return ret;
            }
        }
    }

    align_position(pb, asf->offset, size);
    return 0;
}

// returns data object offset when reading this object for the first time
static int asf_read_data(AVFormatContext *s, const GUIDParseTable *g)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    uint64_t size   = asf->data_size = avio_rl64(pb);
    int i;

    if (!asf->data_reached) {
        asf->data_reached       = 1;
        asf->data_offset        = asf->offset;
    }

    for (i = 0; i < asf->nb_streams; i++) {
        if (!(asf->b_flags & ASF_FLAG_BROADCAST))
            s->streams[i]->duration = asf->duration;
    }
    asf->nb_mult_left           = 0;
    asf->sub_left               = 0;
    asf->state                  = PARSE_PACKET_HEADER;
    asf->return_subpayload      = 0;
    asf->packet_size_internal   = 0;
    avio_skip(pb, 16); // skip File ID
    size = avio_rl64(pb); // Total Data Packets
    if (size != asf->nb_packets)
        av_log(s, AV_LOG_WARNING,
               "Number of Packets from File Properties Object is not equal to Total"
               "Datapackets value! num of packets %"PRIu64" total num %"PRIu64".\n",
               size, asf->nb_packets);
    avio_skip(pb, 2); // skip reserved field
    asf->first_packet_offset = avio_tell(pb);
    if ((pb->seekable & AVIO_SEEKABLE_NORMAL) && !(asf->b_flags & ASF_FLAG_BROADCAST))
        align_position(pb, asf->offset, asf->data_size);

    return 0;
}

static int asf_read_simple_index(AVFormatContext *s, const GUIDParseTable *g)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st    = NULL;
    uint64_t interval; // index entry time interval in 100 ns units, usually it's 1s
    uint32_t pkt_num, nb_entries;
    int32_t prev_pkt_num = -1;
    int i;
    int64_t offset;
    uint64_t size = avio_rl64(pb);

    // simple index objects should be ordered by stream number, this loop tries to find
    // the first not indexed video stream
    for (i = 0; i < asf->nb_streams; i++) {
        if ((asf->asf_st[i]->type == AVMEDIA_TYPE_VIDEO) && !asf->asf_st[i]->indexed) {
            asf->asf_st[i]->indexed = 1;
            st = s->streams[asf->asf_st[i]->index];
            break;
        }
    }
    if (!st) {
        avio_skip(pb, size - 24); // if there's no video stream, skip index object
        return 0;
    }
    avio_skip(pb, 16); // skip File ID
    interval = avio_rl64(pb);
    avio_skip(pb, 4);
    nb_entries = avio_rl32(pb);
    for (i = 0; i < nb_entries; i++) {
        pkt_num = avio_rl32(pb);
        offset = avio_skip(pb, 2);
        if (offset < 0) {
            av_log(s, AV_LOG_ERROR, "Skipping failed in asf_read_simple_index.\n");
            return offset;
        }
        if (prev_pkt_num != pkt_num) {
            av_add_index_entry(st, asf->first_packet_offset + asf->packet_size *
                               pkt_num, av_rescale(interval, i, 10000),
                               asf->packet_size, 0, AVINDEX_KEYFRAME);
            prev_pkt_num = pkt_num;
        }
    }
    asf->is_simple_index = 1;
    align_position(pb, asf->offset, size);

    return 0;
}

static const GUIDParseTable gdef[] = {
    { "Data",                         { 0x75, 0xB2, 0x26, 0x36, 0x66, 0x8E, 0x11, 0xCF, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C }, asf_read_data, 1 },
    { "Simple Index",                 { 0x33, 0x00, 0x08, 0x90, 0xE5, 0xB1, 0x11, 0xCF, 0x89, 0xF4, 0x00, 0xA0, 0xC9, 0x03, 0x49, 0xCB }, asf_read_simple_index, 1 },
    { "Content Description",          { 0x75, 0xB2, 0x26, 0x33, 0x66 ,0x8E, 0x11, 0xCF, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C }, asf_read_content_desc, 1 },
    { "Extended Content Description", { 0xD2, 0xD0, 0xA4, 0x40, 0xE3, 0x07, 0x11, 0xD2, 0x97, 0xF0, 0x00, 0xA0, 0xC9, 0x5e, 0xA8, 0x50 }, asf_read_ext_content, 1 },
    { "Stream Bitrate Properties",    { 0x7B, 0xF8, 0x75, 0xCE, 0x46, 0x8D, 0x11, 0xD1, 0x8D, 0x82, 0x00, 0x60, 0x97, 0xC9, 0xA2, 0xB2 }, asf_read_unknown, 1 },
    { "File Properties",              { 0x8C, 0xAB, 0xDC, 0xA1, 0xA9, 0x47, 0x11, 0xCF, 0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 }, asf_read_properties, 1 },
    { "Header Extension",             { 0x5F, 0xBF, 0x03, 0xB5, 0xA9, 0x2E, 0x11, 0xCF, 0x8E, 0xE3, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 }, asf_read_unknown, 0 },
    { "Stream Properties",            { 0xB7, 0xDC, 0x07, 0x91, 0xA9, 0xB7, 0x11, 0xCF, 0x8E, 0xE6, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 }, asf_read_stream_properties, 1 },
    { "Codec List",                   { 0x86, 0xD1, 0x52, 0x40, 0x31, 0x1D, 0x11, 0xD0, 0xA3, 0xA4, 0x00, 0xA0, 0xC9, 0x03, 0x48, 0xF6 }, asf_read_unknown, 1 },
    { "Marker",                       { 0xF4, 0x87, 0xCD, 0x01, 0xA9, 0x51, 0x11, 0xCF, 0x8E, 0xE6, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 }, asf_read_marker, 1 },
    { "Script Command",               { 0x1E, 0xFB, 0x1A, 0x30, 0x0B, 0x62, 0x11, 0xD0, 0xA3, 0x9B, 0x00, 0xA0, 0xC9, 0x03, 0x48, 0xF6 }, asf_read_unknown, 1 },
    { "Language List",                { 0x7C, 0x43, 0x46, 0xa9, 0xef, 0xe0, 0x4B, 0xFC, 0xB2, 0x29, 0x39, 0x3e, 0xde, 0x41, 0x5c, 0x85 }, asf_read_language_list, 1},
    { "Padding",                      { 0x18, 0x06, 0xD4, 0x74, 0xCA, 0xDF, 0x45, 0x09, 0xA4, 0xBA, 0x9A, 0xAB, 0xCB, 0x96, 0xAA, 0xE8 }, asf_read_unknown, 1 },
    { "DRMv1 Header",                 { 0x22, 0x11, 0xB3, 0xFB, 0xBD, 0x23, 0x11, 0xD2, 0xB4, 0xB7, 0x00, 0xA0, 0xC9, 0x55, 0xFC, 0x6E }, asf_read_unknown, 1 },
    { "DRMv2 Header",                 { 0x29, 0x8A, 0xE6, 0x14, 0x26, 0x22, 0x4C, 0x17, 0xB9, 0x35, 0xDA, 0xE0, 0x7E, 0xE9, 0x28, 0x9c }, asf_read_unknown, 1 },
    { "Index",                        { 0xD6, 0xE2, 0x29, 0xD3, 0x35, 0xDA, 0x11, 0xD1, 0x90, 0x34, 0x00, 0xA0, 0xC9, 0x03, 0x49, 0xBE }, asf_read_unknown, 1 },
    { "Media Object Index",           { 0xFE, 0xB1, 0x03, 0xF8, 0x12, 0xAD, 0x4C, 0x64, 0x84, 0x0F, 0x2A, 0x1D, 0x2F, 0x7A, 0xD4, 0x8C }, asf_read_unknown, 1 },
    { "Timecode Index",               { 0x3C, 0xB7, 0x3F, 0xD0, 0x0C, 0x4A, 0x48, 0x03, 0x95, 0x3D, 0xED, 0xF7, 0xB6, 0x22, 0x8F, 0x0C }, asf_read_unknown, 0 },
    { "Bitrate_Mutual_Exclusion",     { 0xD6, 0xE2, 0x29, 0xDC, 0x35, 0xDA, 0x11, 0xD1, 0x90, 0x34, 0x00, 0xA0, 0xC9, 0x03, 0x49, 0xBE }, asf_read_unknown, 1 },
    { "Error Correction",             { 0x75, 0xB2, 0x26, 0x35, 0x66, 0x8E, 0x11, 0xCF, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C }, asf_read_unknown, 1 },
    { "Content Branding",             { 0x22, 0x11, 0xB3, 0xFA, 0xBD, 0x23, 0x11, 0xD2, 0xB4, 0xB7, 0x00, 0xA0, 0xC9, 0x55, 0xFC, 0x6E }, asf_read_unknown, 1 },
    { "Content Encryption",           { 0x22, 0x11, 0xB3, 0xFB, 0xBD, 0x23, 0x11, 0xD2, 0xB4, 0xB7, 0x00, 0xA0, 0xC9, 0x55, 0xFC, 0x6E }, asf_read_unknown, 1 },
    { "Extended Content Encryption",  { 0x29, 0x8A, 0xE6, 0x14, 0x26, 0x22, 0x4C, 0x17, 0xB9, 0x35, 0xDA, 0xE0, 0x7E, 0xE9, 0x28, 0x9C }, asf_read_unknown, 1 },
    { "Digital Signature",            { 0x22, 0x11, 0xB3, 0xFC, 0xBD, 0x23, 0x11, 0xD2, 0xB4, 0xB7, 0x00, 0xA0, 0xC9, 0x55, 0xFC, 0x6E }, asf_read_unknown, 1 },
    { "Extended Stream Properties",   { 0x14, 0xE6, 0xA5, 0xCB, 0xC6, 0x72, 0x43, 0x32, 0x83, 0x99, 0xA9, 0x69, 0x52, 0x06, 0x5B, 0x5A }, asf_read_ext_stream_properties, 1 },
    { "Advanced Mutual Exclusion",    { 0xA0, 0x86, 0x49, 0xCF, 0x47, 0x75, 0x46, 0x70, 0x8A, 0x16, 0x6E, 0x35, 0x35, 0x75, 0x66, 0xCD }, asf_read_unknown, 1 },
    { "Group Mutual Exclusion",       { 0xD1, 0x46, 0x5A, 0x40, 0x5A, 0x79, 0x43, 0x38, 0xB7, 0x1B, 0xE3, 0x6B, 0x8F, 0xD6, 0xC2, 0x49 }, asf_read_unknown, 1},
    { "Stream Prioritization",        { 0xD4, 0xFE, 0xD1, 0x5B, 0x88, 0xD3, 0x45, 0x4F, 0x81, 0xF0, 0xED, 0x5C, 0x45, 0x99, 0x9E, 0x24 }, asf_read_unknown, 1 },
    { "Bandwidth Sharing Object",     { 0xA6, 0x96, 0x09, 0xE6, 0x51, 0x7B, 0x11, 0xD2, 0xB6, 0xAF, 0x00, 0xC0, 0x4F, 0xD9, 0x08, 0xE9 }, asf_read_unknown, 1 },
    { "Metadata",                     { 0xC5, 0xF8, 0xCB, 0xEA, 0x5B, 0xAF, 0x48, 0x77, 0x84, 0x67, 0xAA, 0x8C, 0x44, 0xFA, 0x4C, 0xCA }, asf_read_metadata_obj, 1 },
    { "Metadata Library",             { 0x44, 0x23, 0x1C, 0x94, 0x94, 0x98, 0x49, 0xD1, 0xA1, 0x41, 0x1D, 0x13, 0x4E, 0x45, 0x70, 0x54 }, asf_read_metadata_obj, 1 },
    { "Audio Spread",                 { 0xBF, 0xC3, 0xCD, 0x50, 0x61, 0x8F, 0x11, 0xCF, 0x8B, 0xB2, 0x00, 0xAA, 0x00, 0xB4, 0xE2, 0x20 }, asf_read_unknown, 1 },
    { "Index Parameters",             { 0xD6, 0xE2, 0x29, 0xDF, 0x35, 0xDA, 0x11, 0xD1, 0x90, 0x34, 0x00, 0xA0, 0xC9, 0x03, 0x49, 0xBE }, asf_read_unknown, 1 },
    { "Content Encryption System Windows Media DRM Network Devices",
                                      { 0x7A, 0x07, 0x9B, 0xB6, 0xDA, 0XA4, 0x4e, 0x12, 0xA5, 0xCA, 0x91, 0xD3, 0x8D, 0xC1, 0x1A, 0x8D }, asf_read_unknown, 1 },
    { "Mutex Language",               { 0xD6, 0xE2, 0x2A, 0x00, 0x25, 0xDA, 0x11, 0xD1, 0x90, 0x34, 0x00, 0xA0, 0xC9, 0x03, 0x49, 0xBE }, asf_read_unknown, 1 },
    { "Mutex Bitrate",                { 0xD6, 0xE2, 0x2A, 0x01, 0x25, 0xDA, 0x11, 0xD1, 0x90, 0x34, 0x00, 0xA0, 0xC9, 0x03, 0x49, 0xBE }, asf_read_unknown, 1 },
    { "Mutex Unknown",                { 0xD6, 0xE2, 0x2A, 0x02, 0x25, 0xDA, 0x11, 0xD1, 0x90, 0x34, 0x00, 0xA0, 0xC9, 0x03, 0x49, 0xBE }, asf_read_unknown, 1 },
    { "Bandwidth Sharing Exclusive",  { 0xAF, 0x60, 0x60, 0xAA, 0x51, 0x97, 0x11, 0xD2, 0xB6, 0xAF, 0x00, 0xC0, 0x4F, 0xD9, 0x08, 0xE9 }, asf_read_unknown, 1 },
    { "Bandwidth Sharing Partial",    { 0xAF, 0x60, 0x60, 0xAB, 0x51, 0x97, 0x11, 0xD2, 0xB6, 0xAF, 0x00, 0xC0, 0x4F, 0xD9, 0x08, 0xE9 }, asf_read_unknown, 1 },
    { "Payload Extension System Timecode", { 0x39, 0x95, 0x95, 0xEC, 0x86, 0x67, 0x4E, 0x2D, 0x8F, 0xDB, 0x98, 0x81, 0x4C, 0xE7, 0x6C, 0x1E }, asf_read_unknown, 1 },
    { "Payload Extension System File Name", { 0xE1, 0x65, 0xEC, 0x0E, 0x19, 0xED, 0x45, 0xD7, 0xB4, 0xA7, 0x25, 0xCB, 0xD1, 0xE2, 0x8E, 0x9B }, asf_read_unknown, 1 },
    { "Payload Extension System Content Type", { 0xD5, 0x90, 0xDC, 0x20, 0x07, 0xBC, 0x43, 0x6C, 0x9C, 0xF7, 0xF3, 0xBB, 0xFB, 0xF1, 0xA4, 0xDC }, asf_read_unknown, 1 },
    { "Payload Extension System Pixel Aspect Ratio", { 0x1, 0x1E, 0xE5, 0x54, 0xF9, 0xEA, 0x4B, 0xC8, 0x82, 0x1A, 0x37, 0x6B, 0x74, 0xE4, 0xC4, 0xB8 }, asf_read_unknown, 1 },
    { "Payload Extension System Sample Duration", { 0xC6, 0xBD, 0x94, 0x50, 0x86, 0x7F, 0x49, 0x07, 0x83, 0xA3, 0xC7, 0x79, 0x21, 0xB7, 0x33, 0xAD }, asf_read_unknown, 1 },
    { "Payload Extension System Encryption Sample ID", { 0x66, 0x98, 0xB8, 0x4E, 0x0A, 0xFA, 0x43, 0x30, 0xAE, 0xB2, 0x1C, 0x0A, 0x98, 0xD7, 0xA4, 0x4D }, asf_read_unknown, 1 },
    { "Payload Extension System Degradable JPEG", { 0x00, 0xE1, 0xAF, 0x06, 0x7B, 0xEC, 0x11, 0xD1, 0xA5, 0x82, 0x00, 0xC0, 0x4F, 0xC2, 0x9C, 0xFB }, asf_read_unknown, 1 },
};

#define READ_LEN(flag, name, len)            \
    do {                                     \
        if ((flag) == name ## IS_BYTE)       \
            len = avio_r8(pb);               \
        else if ((flag) == name ## IS_WORD)  \
            len = avio_rl16(pb);             \
        else if ((flag) == name ## IS_DWORD) \
            len = avio_rl32(pb);             \
        else                                 \
            len = 0;                         \
    } while(0)

static int asf_read_subpayload(AVFormatContext *s, AVPacket *pkt, int is_header)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    uint8_t sub_len;
    int ret, i;

    if (is_header) {
        asf->dts_delta = avio_r8(pb);
        if (asf->nb_mult_left) {
            asf->mult_sub_len = avio_rl16(pb); // total
        }
        asf->sub_header_offset = avio_tell(pb);
        asf->nb_sub = 0;
        asf->sub_left = 1;
    }
    sub_len = avio_r8(pb);
    if ((ret = av_get_packet(pb, pkt, sub_len)) < 0) // each subpayload is entire frame
        return ret;
    for (i = 0; i < asf->nb_streams; i++) {
        if (asf->stream_index == asf->asf_st[i]->stream_index) {
            pkt->stream_index  = asf->asf_st[i]->index;
            break;
        }
    }
    asf->return_subpayload = 1;
    if (!sub_len)
        asf->return_subpayload = 0;

    if (sub_len)
        asf->nb_sub++;
    pkt->dts = asf->sub_dts + (asf->nb_sub - 1) * asf->dts_delta - asf->preroll;
    if (asf->nb_mult_left && (avio_tell(pb) >=
                              (asf->sub_header_offset + asf->mult_sub_len))) {
        asf->sub_left = 0;
        asf->nb_mult_left--;
    }
    if (avio_tell(pb) >= asf->packet_offset + asf->packet_size - asf->pad_len) {
        asf->sub_left = 0;
        if (!asf->nb_mult_left) {
            avio_skip(pb, asf->pad_len);
            if (avio_tell(pb) != asf->packet_offset + asf->packet_size) {
                if (!asf->packet_size)
                    return AVERROR_INVALIDDATA;
                av_log(s, AV_LOG_WARNING,
                       "Position %"PRId64" wrong, should be %"PRId64"\n",
                       avio_tell(pb), asf->packet_offset + asf->packet_size);
                avio_seek(pb, asf->packet_offset + asf->packet_size, SEEK_SET);
            }
        }
    }

    return 0;
}

static void reset_packet(ASFPacket *asf_pkt)
{
    asf_pkt->size_left = 0;
    asf_pkt->data_size = 0;
    asf_pkt->duration  = 0;
    asf_pkt->flags     = 0;
    asf_pkt->dts       = 0;
    asf_pkt->duration  = 0;
    av_packet_unref(&asf_pkt->avpkt);
    av_init_packet(&asf_pkt->avpkt);
}

static int asf_read_replicated_data(AVFormatContext *s, ASFPacket *asf_pkt)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret, data_size;

    if (!asf_pkt->data_size) {
        data_size = avio_rl32(pb); // read media object size
        if (data_size <= 0)
            return AVERROR_INVALIDDATA;
        if ((ret = av_new_packet(&asf_pkt->avpkt, data_size)) < 0)
            return ret;
        asf_pkt->data_size = asf_pkt->size_left = data_size;
    } else
        avio_skip(pb, 4); // reading of media object size is already done
    asf_pkt->dts = avio_rl32(pb); // read presentation time
    if (asf->rep_data_len >= 8)
        avio_skip(pb, asf->rep_data_len - 8); // skip replicated data

    return 0;
}

static int asf_read_multiple_payload(AVFormatContext *s, AVPacket *pkt,
                                 ASFPacket *asf_pkt)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    uint16_t pay_len;
    unsigned char *p;
    int ret;
    int skip = 0;

    // if replicated length is 1, subpayloads are present
    if (asf->rep_data_len == 1) {
        asf->sub_left = 1;
        asf->state = READ_MULTI_SUB;
        pkt->flags = asf_pkt->flags;
        if ((ret = asf_read_subpayload(s, pkt, 1)) < 0)
            return ret;
    } else {
        if (asf->rep_data_len)
            if ((ret = asf_read_replicated_data(s, asf_pkt)) < 0)
                return ret;
        pay_len = avio_rl16(pb); // payload length should be WORD
        if (pay_len > asf->packet_size) {
            av_log(s, AV_LOG_ERROR,
                   "Error: invalid data packet size, pay_len %"PRIu16", "
                   "asf->packet_size %"PRIu32", offset %"PRId64".\n",
                   pay_len, asf->packet_size, avio_tell(pb));
            return AVERROR_INVALIDDATA;
        }
        p = asf_pkt->avpkt.data + asf_pkt->data_size - asf_pkt->size_left;
        if (pay_len > asf_pkt->size_left) {
            av_log(s, AV_LOG_ERROR,
                   "Error: invalid buffer size, pay_len %d, data size left %d.\n",
            pay_len, asf_pkt->size_left);
            skip = pay_len - asf_pkt->size_left;
            pay_len = asf_pkt->size_left;
        }
        if (asf_pkt->size_left <= 0)
            return AVERROR_INVALIDDATA;
        if ((ret = avio_read(pb, p, pay_len)) < 0)
            return ret;
        if (s->key && s->keylen == 20)
            ff_asfcrypt_dec(s->key, p, ret);
        avio_skip(pb, skip);
        asf_pkt->size_left -= pay_len;
        asf->nb_mult_left--;
    }

    return 0;
}

static int asf_read_single_payload(AVFormatContext *s, ASFPacket *asf_pkt)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t  offset;
    uint64_t size;
    unsigned char *p;
    int ret, data_size;

    if (!asf_pkt->data_size) {
        data_size = avio_rl32(pb); // read media object size
        if (data_size <= 0)
            return AVERROR_EOF;
        if ((ret = av_new_packet(&asf_pkt->avpkt, data_size)) < 0)
            return ret;
        asf_pkt->data_size = asf_pkt->size_left = data_size;
    } else
        avio_skip(pb, 4); // skip media object size
    asf_pkt->dts = avio_rl32(pb); // read presentation time
    if (asf->rep_data_len >= 8)
        avio_skip(pb, asf->rep_data_len - 8); // skip replicated data
    offset = avio_tell(pb);

    // size of the payload - size of the packet without header and padding
    if (asf->packet_size_internal)
        size = asf->packet_size_internal - offset + asf->packet_offset - asf->pad_len;
    else
        size = asf->packet_size - offset + asf->packet_offset - asf->pad_len;
    if (size > asf->packet_size) {
        av_log(s, AV_LOG_ERROR,
               "Error: invalid data packet size, offset %"PRId64".\n",
               avio_tell(pb));
        return AVERROR_INVALIDDATA;
    }
    p = asf_pkt->avpkt.data + asf_pkt->data_size - asf_pkt->size_left;
    if (size > asf_pkt->size_left || asf_pkt->size_left <= 0)
        return AVERROR_INVALIDDATA;
    if (asf_pkt->size_left > size)
        asf_pkt->size_left -= size;
    else
        asf_pkt->size_left = 0;
    if ((ret = avio_read(pb, p, size)) < 0)
        return ret;
    if (s->key && s->keylen == 20)
            ff_asfcrypt_dec(s->key, p, ret);
    if (asf->packet_size_internal)
        avio_skip(pb, asf->packet_size - asf->packet_size_internal);
    avio_skip(pb, asf->pad_len); // skip padding

    return 0;
}

static int asf_read_payload(AVFormatContext *s, AVPacket *pkt)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret, i;
    ASFPacket *asf_pkt = NULL;

    if (!asf->sub_left) {
        uint32_t off_len, media_len;
        uint8_t stream_num;

        stream_num = avio_r8(pb);
        asf->stream_index = stream_num & ASF_STREAM_NUM;
        for (i = 0; i < asf->nb_streams; i++) {
            if (asf->stream_index == asf->asf_st[i]->stream_index) {
                asf_pkt               = &asf->asf_st[i]->pkt;
                asf_pkt->stream_index = asf->asf_st[i]->index;
                break;
            }
        }
        if (!asf_pkt) {
            if (asf->packet_offset + asf->packet_size <= asf->data_offset + asf->data_size) {
                if (!asf->packet_size) {
                    av_log(s, AV_LOG_ERROR, "Invalid packet size 0.\n");
                    return AVERROR_INVALIDDATA;
                }
                avio_seek(pb, asf->packet_offset + asf->packet_size, SEEK_SET);
                av_log(s, AV_LOG_WARNING, "Skipping the stream with the invalid stream index %d.\n",
                       asf->stream_index);
                return AVERROR(EAGAIN);
            } else
                return AVERROR_INVALIDDATA;
        }

        if (stream_num >> 7)
            asf_pkt->flags |= AV_PKT_FLAG_KEY;
        READ_LEN(asf->prop_flags & ASF_PL_MASK_MEDIA_OBJECT_NUMBER_LENGTH_FIELD_SIZE,
                 ASF_PL_FLAG_MEDIA_OBJECT_NUMBER_LENGTH_FIELD_, media_len);
        READ_LEN(asf->prop_flags & ASF_PL_MASK_OFFSET_INTO_MEDIA_OBJECT_LENGTH_FIELD_SIZE,
                 ASF_PL_FLAG_OFFSET_INTO_MEDIA_OBJECT_LENGTH_FIELD_, off_len);
        READ_LEN(asf->prop_flags & ASF_PL_MASK_REPLICATED_DATA_LENGTH_FIELD_SIZE,
                 ASF_PL_FLAG_REPLICATED_DATA_LENGTH_FIELD_, asf->rep_data_len);
        if (asf_pkt->size_left && (asf_pkt->frame_num != media_len)) {
            av_log(s, AV_LOG_WARNING, "Unfinished frame will be ignored\n");
            reset_packet(asf_pkt);
        }
        asf_pkt->frame_num = media_len;
        asf->sub_dts = off_len;
        if (asf->nb_mult_left) {
            if ((ret = asf_read_multiple_payload(s, pkt, asf_pkt)) < 0)
                return ret;
        } else if (asf->rep_data_len == 1) {
            asf->sub_left = 1;
            asf->state    = READ_SINGLE;
            pkt->flags    = asf_pkt->flags;
            if ((ret = asf_read_subpayload(s, pkt, 1)) < 0)
                return ret;
        } else {
            if ((ret = asf_read_single_payload(s, asf_pkt)) < 0)
                return ret;
        }
    } else {
        for (i = 0; i <= asf->nb_streams; i++) {
            if (asf->stream_index == asf->asf_st[i]->stream_index) {
                asf_pkt = &asf->asf_st[i]->pkt;
                break;
            }
        }
        if (!asf_pkt)
            return AVERROR_INVALIDDATA;
        pkt->flags         = asf_pkt->flags;
        pkt->dts           = asf_pkt->dts;
        pkt->stream_index  = asf->asf_st[i]->index;
        if ((ret = asf_read_subpayload(s, pkt, 0)) < 0) // read subpayload without its header
            return ret;
    }

    return 0;
}

static int asf_read_packet_header(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    uint64_t size;
    uint32_t av_unused seq;
    unsigned char error_flags, len_flags, pay_flags;

    asf->packet_offset = avio_tell(pb);
    error_flags = avio_r8(pb); // read Error Correction Flags
    if (error_flags & ASF_PACKET_FLAG_ERROR_CORRECTION_PRESENT) {
        if (!(error_flags & ASF_ERROR_CORRECTION_LENGTH_TYPE)) {
            size = error_flags & ASF_PACKET_ERROR_CORRECTION_DATA_SIZE;
            avio_skip(pb, size);
        }
        len_flags       = avio_r8(pb);
    } else
        len_flags = error_flags;
    asf->prop_flags = avio_r8(pb);
    READ_LEN(len_flags & ASF_PPI_MASK_PACKET_LENGTH_FIELD_SIZE,
             ASF_PPI_FLAG_PACKET_LENGTH_FIELD_, asf->packet_size_internal);
    READ_LEN(len_flags & ASF_PPI_MASK_SEQUENCE_FIELD_SIZE,
             ASF_PPI_FLAG_SEQUENCE_FIELD_, seq);
    READ_LEN(len_flags & ASF_PPI_MASK_PADDING_LENGTH_FIELD_SIZE,
             ASF_PPI_FLAG_PADDING_LENGTH_FIELD_, asf->pad_len );
    asf->send_time = avio_rl32(pb); // send time
    avio_skip(pb, 2); // skip duration
    if (len_flags & ASF_PPI_FLAG_MULTIPLE_PAYLOADS_PRESENT) { // Multiple Payloads present
        pay_flags = avio_r8(pb);
        asf->nb_mult_left = (pay_flags & ASF_NUM_OF_PAYLOADS);
    }

    return 0;
}

static int asf_deinterleave(AVFormatContext *s, ASFPacket *asf_pkt, int st_num)
{
    ASFContext *asf    = s->priv_data;
    ASFStream *asf_st  = asf->asf_st[st_num];
    unsigned char *p   = asf_pkt->avpkt.data;
    uint16_t pkt_len   = asf->asf_st[st_num]->virtual_pkt_len;
    uint16_t chunk_len = asf->asf_st[st_num]->virtual_chunk_len;
    int nchunks        = pkt_len / chunk_len;
    AVPacket pkt;
    int pos = 0, j, l, ret;


    if ((ret = av_new_packet(&pkt, asf_pkt->data_size)) < 0)
        return ret;

    while (asf_pkt->data_size >= asf_st->span * pkt_len + pos) {
        if (pos >= asf_pkt->data_size) {
            break;
        }
        for (l = 0; l < pkt_len; l++) {
            if (pos >= asf_pkt->data_size) {
                break;
            }
            for (j = 0; j < asf_st->span; j++) {
                if ((pos + chunk_len) >= asf_pkt->data_size)
                    break;
                memcpy(pkt.data + pos,
                       p + (j * nchunks + l) * chunk_len,
                       chunk_len);
                pos += chunk_len;
            }
        }
        p += asf_st->span * pkt_len;
        if (p > asf_pkt->avpkt.data + asf_pkt->data_size)
            break;
    }
    av_packet_unref(&asf_pkt->avpkt);
    asf_pkt->avpkt = pkt;

    return 0;
}

static int asf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret, i;

    if ((avio_tell(pb) >= asf->data_offset + asf->data_size) &&
        !(asf->b_flags & ASF_FLAG_BROADCAST))
        return AVERROR_EOF;
    while (!pb->eof_reached) {
        if (asf->state == PARSE_PACKET_HEADER) {
            asf_read_packet_header(s);
            if (pb->eof_reached)
                break;
            if (!asf->nb_mult_left)
                asf->state = READ_SINGLE;
            else
                asf->state = READ_MULTI;
        }
        ret = asf_read_payload(s, pkt);
        if (ret == AVERROR(EAGAIN)) {
            asf->state = PARSE_PACKET_HEADER;
            continue;
        }
        else if (ret < 0)
            return ret;

        switch (asf->state) {
        case READ_SINGLE:
            if (!asf->sub_left)
                asf->state = PARSE_PACKET_HEADER;
            break;
        case READ_MULTI_SUB:
            if (!asf->sub_left && !asf->nb_mult_left) {
                asf->state = PARSE_PACKET_HEADER;
                if (!asf->return_subpayload &&
                    (avio_tell(pb) <= asf->packet_offset +
                     asf->packet_size - asf->pad_len))
                    avio_skip(pb, asf->pad_len); // skip padding
                if (asf->packet_offset + asf->packet_size > avio_tell(pb))
                    avio_seek(pb, asf->packet_offset + asf->packet_size, SEEK_SET);
            } else if (!asf->sub_left)
                asf->state = READ_MULTI;
            break;
        case READ_MULTI:
            if (!asf->nb_mult_left) {
                asf->state = PARSE_PACKET_HEADER;
                if (!asf->return_subpayload &&
                    (avio_tell(pb) <= asf->packet_offset +
                     asf->packet_size - asf->pad_len))
                    avio_skip(pb, asf->pad_len); // skip padding
                if (asf->packet_offset + asf->packet_size > avio_tell(pb))
                    avio_seek(pb, asf->packet_offset + asf->packet_size, SEEK_SET);
            }
            break;
        }
        if (asf->return_subpayload) {
            asf->return_subpayload = 0;
            return 0;
        }
        for (i = 0; i < asf->nb_streams; i++) {
            ASFPacket *asf_pkt = &asf->asf_st[i]->pkt;
            if (asf_pkt && !asf_pkt->size_left && asf_pkt->data_size) {
                if (asf->asf_st[i]->span > 1 &&
                    asf->asf_st[i]->type == AVMEDIA_TYPE_AUDIO)
                    if ((ret = asf_deinterleave(s, asf_pkt, i)) < 0)
                        return ret;
                av_packet_move_ref(pkt, &asf_pkt->avpkt);
                pkt->stream_index  = asf->asf_st[i]->index;
                pkt->flags         = asf_pkt->flags;
                pkt->dts           = asf_pkt->dts - asf->preroll;
                asf_pkt->data_size = 0;
                asf_pkt->frame_num = 0;
                return 0;
            }
        }
    }

    if (pb->eof_reached)
        return AVERROR_EOF;

    return 0;
}

static int asf_read_close(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;
    int i;

    for (i = 0; i < ASF_MAX_STREAMS; i++) {
        av_dict_free(&asf->asf_sd[i].asf_met);
        if (i < asf->nb_streams) {
            av_packet_unref(&asf->asf_st[i]->pkt.avpkt);
            av_freep(&asf->asf_st[i]);
        }
    }

    asf->nb_streams = 0;
    return 0;
}

static void reset_packet_state(AVFormatContext *s)
{
    ASFContext *asf        = s->priv_data;
    int i;

    asf->state             = PARSE_PACKET_HEADER;
    asf->offset            = 0;
    asf->return_subpayload = 0;
    asf->sub_left          = 0;
    asf->sub_header_offset = 0;
    asf->packet_offset     = asf->first_packet_offset;
    asf->pad_len           = 0;
    asf->rep_data_len      = 0;
    asf->dts_delta         = 0;
    asf->mult_sub_len      = 0;
    asf->nb_mult_left      = 0;
    asf->nb_sub            = 0;
    asf->prop_flags        = 0;
    asf->sub_dts           = 0;
    for (i = 0; i < asf->nb_streams; i++) {
        ASFPacket *pkt = &asf->asf_st[i]->pkt;
        pkt->size_left = 0;
        pkt->data_size = 0;
        pkt->duration  = 0;
        pkt->flags     = 0;
        pkt->dts       = 0;
        pkt->duration  = 0;
        av_packet_unref(&pkt->avpkt);
        av_init_packet(&pkt->avpkt);
    }
}

/*
 * Find a timestamp for the requested position within the payload
 * where the pos (position) is the offset inside the Data Object.
 * When position is not on the packet boundary, asf_read_timestamp tries
 * to find the closest packet offset after this position. If this packet
 * is a key frame, this packet timestamp is read and an index entry is created
 * for the packet. If this packet belongs to the requested stream,
 * asf_read_timestamp upgrades pos to the packet beginning offset and
 * returns this packet's dts. So returned dts is the dts of the first key frame with
 * matching stream number after given position.
 */
static int64_t asf_read_timestamp(AVFormatContext *s, int stream_index,
                                  int64_t *pos, int64_t pos_limit)
{
    ASFContext *asf = s->priv_data;
    int64_t pkt_pos = *pos, pkt_offset, dts = AV_NOPTS_VALUE, data_end;
    AVPacket pkt;
    int n;

    data_end = asf->data_offset + asf->data_size;

    n = (pkt_pos - asf->first_packet_offset + asf->packet_size - 1) /
        asf->packet_size;
    n = av_clip(n, 0, ((data_end - asf->first_packet_offset) / asf->packet_size - 1));
    pkt_pos = asf->first_packet_offset +  n * asf->packet_size;

    avio_seek(s->pb, pkt_pos, SEEK_SET);
    pkt_offset = pkt_pos;

    reset_packet_state(s);
    while (avio_tell(s->pb) < data_end) {

        int i, ret, st_found;

        av_init_packet(&pkt);
        pkt_offset = avio_tell(s->pb);
        if ((ret = asf_read_packet(s, &pkt)) < 0) {
            dts = AV_NOPTS_VALUE;
            return ret;
        }
        // ASFPacket may contain fragments of packets belonging to different streams,
        // pkt_offset is the offset of the first fragment within it.
        if ((pkt_offset >= (pkt_pos + asf->packet_size)))
            pkt_pos += asf->packet_size;
        for (i = 0; i < asf->nb_streams; i++) {
            ASFStream *st = asf->asf_st[i];

            st_found = 0;
            if (pkt.flags & AV_PKT_FLAG_KEY) {
                dts = pkt.dts;
                if (dts) {
                    av_add_index_entry(s->streams[pkt.stream_index], pkt_pos,
                                       dts, pkt.size, 0, AVINDEX_KEYFRAME);
                    if (stream_index == st->index) {
                        st_found = 1;
                        break;
                    }
                }
            }
        }
        if (st_found)
            break;
        av_packet_unref(&pkt);
    }
    *pos = pkt_pos;

    av_packet_unref(&pkt);
    return dts;
}

static int asf_read_seek(AVFormatContext *s, int stream_index,
                         int64_t timestamp, int flags)
{
    ASFContext *asf = s->priv_data;
    int idx, ret;

    if (s->streams[stream_index]->nb_index_entries && asf->is_simple_index) {
        idx = av_index_search_timestamp(s->streams[stream_index], timestamp, flags);
        if (idx < 0 || idx >= s->streams[stream_index]->nb_index_entries)
            return AVERROR_INVALIDDATA;
        avio_seek(s->pb, s->streams[stream_index]->index_entries[idx].pos, SEEK_SET);
    } else {
        if ((ret = ff_seek_frame_binary(s, stream_index, timestamp, flags)) < 0)
            return ret;
    }

    reset_packet_state(s);

    return 0;
}

static const GUIDParseTable *find_guid(ff_asf_guid guid)
{
    int j, ret;
    const GUIDParseTable *g;

    swap_guid(guid);
    g = gdef;
    for (j = 0; j < FF_ARRAY_ELEMS(gdef); j++) {
        if (!(ret = memcmp(guid, g->guid, sizeof(g->guid))))
            return g;
        g++;
    }

    return NULL;
}

static int detect_unknown_subobject(AVFormatContext *s, int64_t offset, int64_t size)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    const GUIDParseTable *g = NULL;
    ff_asf_guid guid;
    int ret;

    while (avio_tell(pb) <= offset + size) {
        if (avio_tell(pb) == asf->offset)
            break;
        asf->offset = avio_tell(pb);
        if ((ret = ff_get_guid(pb, &guid)) < 0)
            return ret;
        g = find_guid(guid);
        if (g) {
            if ((ret = g->read_object(s, g)) < 0)
                return ret;
        } else {
            GUIDParseTable g2;

            g2.name         = "Unknown";
            g2.is_subobject = 1;
            asf_read_unknown(s, &g2);
        }
    }

    return 0;
}

static int asf_read_header(AVFormatContext *s)
{
    ASFContext *asf         = s->priv_data;
    AVIOContext *pb         = s->pb;
    const GUIDParseTable *g = NULL;
    ff_asf_guid guid;
    int i, ret;
    uint64_t size;

    asf->preroll         = 0;
    asf->is_simple_index = 0;
    ff_get_guid(pb, &guid);
    if (ff_guidcmp(&guid, &ff_asf_header))
        return AVERROR_INVALIDDATA;
    avio_skip(pb, 8); // skip header object size
    avio_skip(pb, 6); // skip number of header objects and 2 reserved bytes
    asf->data_reached = 0;

    /* 1  is here instead of pb->eof_reached because (when not streaming), Data are skipped
     * for the first time,
     * Index object is processed and got eof and then seeking back to the Data is performed.
     */
    while (1) {
        // for the cases when object size is invalid
        if (avio_tell(pb) == asf->offset)
            break;
        asf->offset = avio_tell(pb);
        if ((ret = ff_get_guid(pb, &guid)) < 0) {
            if (ret == AVERROR_EOF && asf->data_reached)
                break;
            else
                goto failed;
        }
        g = find_guid(guid);
        if (g) {
            asf->unknown_offset = asf->offset;
            asf->is_header = 1;
            if ((ret = g->read_object(s, g)) < 0)
                goto failed;
        } else {
            size = avio_rl64(pb);
            align_position(pb, asf->offset, size);
        }
        if (asf->data_reached &&
            (!(pb->seekable & AVIO_SEEKABLE_NORMAL) ||
             (asf->b_flags & ASF_FLAG_BROADCAST)))
            break;
    }

    if (!asf->data_reached) {
        av_log(s, AV_LOG_ERROR, "Data Object was not found.\n");
        ret = AVERROR_INVALIDDATA;
        goto failed;
    }
    if (pb->seekable & AVIO_SEEKABLE_NORMAL)
        avio_seek(pb, asf->first_packet_offset, SEEK_SET);

    for (i = 0; i < asf->nb_streams; i++) {
        const char *rfc1766 = asf->asf_sd[asf->asf_st[i]->lang_idx].langs;
        AVStream *st        = s->streams[asf->asf_st[i]->index];
        set_language(s, rfc1766, &st->metadata);
    }

    for (i = 0; i < ASF_MAX_STREAMS; i++) {
        AVStream *st = NULL;

        st = find_stream(s, i);
        if (st) {
            av_dict_copy(&st->metadata, asf->asf_sd[i].asf_met, AV_DICT_IGNORE_SUFFIX);
            if (asf->asf_sd[i].aspect_ratio.num > 0 && asf->asf_sd[i].aspect_ratio.den > 0) {
                st->sample_aspect_ratio.num = asf->asf_sd[i].aspect_ratio.num;
                st->sample_aspect_ratio.den = asf->asf_sd[i].aspect_ratio.den;
            }
        }
    }

    return 0;

failed:
    asf_read_close(s);
    return ret;
}

AVInputFormat ff_asf_o_demuxer = {
    .name           = "asf_o",
    .long_name      = NULL_IF_CONFIG_SMALL("ASF (Advanced / Active Streaming Format)"),
    .priv_data_size = sizeof(ASFContext),
    .read_probe     = asf_probe,
    .read_header    = asf_read_header,
    .read_packet    = asf_read_packet,
    .read_close     = asf_read_close,
    .read_timestamp = asf_read_timestamp,
    .read_seek      = asf_read_seek,
    .flags          = AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH,
};
