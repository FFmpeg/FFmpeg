/*
 * ASF compatible demuxer
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

//#define DEBUG

#include "libavutil/common.h"
#include "libavutil/avstring.h"
#include "libavcodec/mpegaudio.h"
#include "avformat.h"
#include "riff.h"
#include "asf.h"
#include "asfcrypt.h"
#include "avlanguage.h"

void ff_mms_set_stream_selection(URLContext *h, AVFormatContext *format);

#undef NDEBUG
#include <assert.h>

#define ASF_MAX_STREAMS 127
#define FRAME_HEADER_SIZE 17
// Fix Me! FRAME_HEADER_SIZE may be different.

static const ff_asf_guid index_guid = {
    0x90, 0x08, 0x00, 0x33, 0xb1, 0xe5, 0xcf, 0x11, 0x89, 0xf4, 0x00, 0xa0, 0xc9, 0x03, 0x49, 0xcb
};

static const ff_asf_guid stream_bitrate_guid = { /* (http://get.to/sdp) */
    0xce, 0x75, 0xf8, 0x7b, 0x8d, 0x46, 0xd1, 0x11, 0x8d, 0x82, 0x00, 0x60, 0x97, 0xc9, 0xa2, 0xb2
};
/**********************************/
/* decoding */

int ff_guidcmp(const void *g1, const void *g2)
{
    return memcmp(g1, g2, sizeof(ff_asf_guid));
}

#ifdef DEBUG
#define PRINT_IF_GUID(g,cmp) \
if (!ff_guidcmp(g, &cmp)) \
    dprintf(NULL, "(GUID: %s) ", #cmp)

static void print_guid(const ff_asf_guid *g)
{
    int i;
    PRINT_IF_GUID(g, ff_asf_header);
    else PRINT_IF_GUID(g, ff_asf_file_header);
    else PRINT_IF_GUID(g, ff_asf_stream_header);
    else PRINT_IF_GUID(g, ff_asf_audio_stream);
    else PRINT_IF_GUID(g, ff_asf_audio_conceal_none);
    else PRINT_IF_GUID(g, ff_asf_video_stream);
    else PRINT_IF_GUID(g, ff_asf_video_conceal_none);
    else PRINT_IF_GUID(g, ff_asf_command_stream);
    else PRINT_IF_GUID(g, ff_asf_comment_header);
    else PRINT_IF_GUID(g, ff_asf_codec_comment_header);
    else PRINT_IF_GUID(g, ff_asf_codec_comment1_header);
    else PRINT_IF_GUID(g, ff_asf_data_header);
    else PRINT_IF_GUID(g, index_guid);
    else PRINT_IF_GUID(g, ff_asf_head1_guid);
    else PRINT_IF_GUID(g, ff_asf_head2_guid);
    else PRINT_IF_GUID(g, ff_asf_my_guid);
    else PRINT_IF_GUID(g, ff_asf_ext_stream_header);
    else PRINT_IF_GUID(g, ff_asf_extended_content_header);
    else PRINT_IF_GUID(g, ff_asf_ext_stream_embed_stream_header);
    else PRINT_IF_GUID(g, ff_asf_ext_stream_audio_stream);
    else PRINT_IF_GUID(g, ff_asf_metadata_header);
    else PRINT_IF_GUID(g, ff_asf_marker_header);
    else PRINT_IF_GUID(g, stream_bitrate_guid);
    else PRINT_IF_GUID(g, ff_asf_language_guid);
    else
        dprintf(NULL, "(GUID: unknown) ");
    for(i=0;i<16;i++)
        dprintf(NULL, " 0x%02x,", (*g)[i]);
    dprintf(NULL, "}\n");
}
#undef PRINT_IF_GUID
#else
#define print_guid(g)
#endif

void ff_get_guid(ByteIOContext *s, ff_asf_guid *g)
{
    assert(sizeof(*g) == 16);
    get_buffer(s, *g, sizeof(*g));
}

#if 0
static void get_str16(ByteIOContext *pb, char *buf, int buf_size)
{
    int len, c;
    char *q;

    len = get_le16(pb);
    q = buf;
    while (len > 0) {
        c = get_le16(pb);
        if ((q - buf) < buf_size - 1)
            *q++ = c;
        len--;
    }
    *q = '\0';
}
#endif

static void get_str16_nolen(ByteIOContext *pb, int len, char *buf, int buf_size)
{
    char* q = buf;
    while (len > 1) {
        uint8_t tmp;
        uint32_t ch;

        GET_UTF16(ch, (len -= 2) >= 0 ? get_le16(pb) : 0, break;)
        PUT_UTF8(ch, tmp, if (q - buf < buf_size - 1) *q++ = tmp;)
    }
    if (len > 0)
        url_fskip(pb, len);
    *q = '\0';
}

static int asf_probe(AVProbeData *pd)
{
    /* check file header */
    if (!ff_guidcmp(pd->buf, &ff_asf_header))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int get_value(ByteIOContext *pb, int type){
    switch(type){
        case 2: return get_le32(pb);
        case 3: return get_le32(pb);
        case 4: return get_le64(pb);
        case 5: return get_le16(pb);
        default:return INT_MIN;
    }
}

static void get_tag(AVFormatContext *s, const char *key, int type, int len)
{
    char *value;

    if ((unsigned)len >= (UINT_MAX - 1)/2)
        return;

    value = av_malloc(2*len+1);
    if (!value)
        return;

    if (type == 0) {         // UTF16-LE
        get_str16_nolen(s->pb, len, value, 2*len + 1);
    } else if (type > 1 && type <= 5) {  // boolean or DWORD or QWORD or WORD
        uint64_t num = get_value(s->pb, type);
        snprintf(value, len, "%"PRIu64, num);
    } else {
        url_fskip(s->pb, len);
        av_freep(&value);
        av_log(s, AV_LOG_DEBUG, "Unsupported value type %d in tag %s.\n", type, key);
        return;
    }
    av_metadata_set2(&s->metadata, key, value, 0);
    av_freep(&value);
}

static int asf_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    ASFContext *asf = s->priv_data;
    ff_asf_guid g;
    ByteIOContext *pb = s->pb;
    AVStream *st;
    ASFStream *asf_st;
    int size, i;
    int64_t gsize;
    AVRational dar[128];
    uint32_t bitrate[128];

    memset(dar, 0, sizeof(dar));
    memset(bitrate, 0, sizeof(bitrate));

    ff_get_guid(pb, &g);
    if (ff_guidcmp(&g, &ff_asf_header))
        return -1;
    get_le64(pb);
    get_le32(pb);
    get_byte(pb);
    get_byte(pb);
    memset(&asf->asfid2avid, -1, sizeof(asf->asfid2avid));
    for(;;) {
        uint64_t gpos= url_ftell(pb);
        ff_get_guid(pb, &g);
        gsize = get_le64(pb);
        dprintf(s, "%08"PRIx64": ", gpos);
        print_guid(&g);
        dprintf(s, "  size=0x%"PRIx64"\n", gsize);
        if (!ff_guidcmp(&g, &ff_asf_data_header)) {
            asf->data_object_offset = url_ftell(pb);
            // if not streaming, gsize is not unlimited (how?), and there is enough space in the file..
            if (!(asf->hdr.flags & 0x01) && gsize >= 100) {
                asf->data_object_size = gsize - 24;
            } else {
                asf->data_object_size = (uint64_t)-1;
            }
            break;
        }
        if (gsize < 24)
            return -1;
        if (!ff_guidcmp(&g, &ff_asf_file_header)) {
            ff_get_guid(pb, &asf->hdr.guid);
            asf->hdr.file_size          = get_le64(pb);
            asf->hdr.create_time        = get_le64(pb);
            asf->nb_packets             = get_le64(pb);
            asf->hdr.play_time          = get_le64(pb);
            asf->hdr.send_time          = get_le64(pb);
            asf->hdr.preroll            = get_le32(pb);
            asf->hdr.ignore             = get_le32(pb);
            asf->hdr.flags              = get_le32(pb);
            asf->hdr.min_pktsize        = get_le32(pb);
            asf->hdr.max_pktsize        = get_le32(pb);
            asf->hdr.max_bitrate        = get_le32(pb);
            s->packet_size = asf->hdr.max_pktsize;
        } else if (!ff_guidcmp(&g, &ff_asf_stream_header)) {
            enum AVMediaType type;
            int type_specific_size, sizeX;
            uint64_t total_size;
            unsigned int tag1;
            int64_t pos1, pos2, start_time;
            int test_for_ext_stream_audio, is_dvr_ms_audio=0;

            if (s->nb_streams == ASF_MAX_STREAMS) {
                av_log(s, AV_LOG_ERROR, "too many streams\n");
                return AVERROR(EINVAL);
            }

            pos1 = url_ftell(pb);

            st = av_new_stream(s, 0);
            if (!st)
                return AVERROR(ENOMEM);
            av_set_pts_info(st, 32, 1, 1000); /* 32 bit pts in ms */
            asf_st = av_mallocz(sizeof(ASFStream));
            if (!asf_st)
                return AVERROR(ENOMEM);
            st->priv_data = asf_st;
            start_time = asf->hdr.preroll;

            asf_st->stream_language_index = 128; // invalid stream index means no language info

            if(!(asf->hdr.flags & 0x01)) { // if we aren't streaming...
                st->duration = asf->hdr.play_time /
                    (10000000 / 1000) - start_time;
            }
            ff_get_guid(pb, &g);

            test_for_ext_stream_audio = 0;
            if (!ff_guidcmp(&g, &ff_asf_audio_stream)) {
                type = AVMEDIA_TYPE_AUDIO;
            } else if (!ff_guidcmp(&g, &ff_asf_video_stream)) {
                type = AVMEDIA_TYPE_VIDEO;
            } else if (!ff_guidcmp(&g, &ff_asf_jfif_media)) {
                type = AVMEDIA_TYPE_VIDEO;
                st->codec->codec_id = CODEC_ID_MJPEG;
            } else if (!ff_guidcmp(&g, &ff_asf_command_stream)) {
                type = AVMEDIA_TYPE_DATA;
            } else if (!ff_guidcmp(&g, &ff_asf_ext_stream_embed_stream_header)) {
                test_for_ext_stream_audio = 1;
                type = AVMEDIA_TYPE_UNKNOWN;
            } else {
                return -1;
            }
            ff_get_guid(pb, &g);
            total_size = get_le64(pb);
            type_specific_size = get_le32(pb);
            get_le32(pb);
            st->id = get_le16(pb) & 0x7f; /* stream id */
            // mapping of asf ID to AV stream ID;
            asf->asfid2avid[st->id] = s->nb_streams - 1;

            get_le32(pb);

            if (test_for_ext_stream_audio) {
                ff_get_guid(pb, &g);
                if (!ff_guidcmp(&g, &ff_asf_ext_stream_audio_stream)) {
                    type = AVMEDIA_TYPE_AUDIO;
                    is_dvr_ms_audio=1;
                    ff_get_guid(pb, &g);
                    get_le32(pb);
                    get_le32(pb);
                    get_le32(pb);
                    ff_get_guid(pb, &g);
                    get_le32(pb);
                }
            }

            st->codec->codec_type = type;
            if (type == AVMEDIA_TYPE_AUDIO) {
                ff_get_wav_header(pb, st->codec, type_specific_size);
                if (is_dvr_ms_audio) {
                    // codec_id and codec_tag are unreliable in dvr_ms
                    // files. Set them later by probing stream.
                    st->codec->codec_id = CODEC_ID_PROBE;
                    st->codec->codec_tag = 0;
                }
                if (st->codec->codec_id == CODEC_ID_AAC) {
                    st->need_parsing = AVSTREAM_PARSE_NONE;
                } else {
                    st->need_parsing = AVSTREAM_PARSE_FULL;
                }
                /* We have to init the frame size at some point .... */
                pos2 = url_ftell(pb);
                if (gsize >= (pos2 + 8 - pos1 + 24)) {
                    asf_st->ds_span = get_byte(pb);
                    asf_st->ds_packet_size = get_le16(pb);
                    asf_st->ds_chunk_size = get_le16(pb);
                    get_le16(pb); //ds_data_size
                    get_byte(pb); //ds_silence_data
                }
                //printf("Descrambling: ps:%d cs:%d ds:%d s:%d  sd:%d\n",
                //       asf_st->ds_packet_size, asf_st->ds_chunk_size,
                //       asf_st->ds_data_size, asf_st->ds_span, asf_st->ds_silence_data);
                if (asf_st->ds_span > 1) {
                    if (!asf_st->ds_chunk_size
                        || (asf_st->ds_packet_size/asf_st->ds_chunk_size <= 1)
                        || asf_st->ds_packet_size % asf_st->ds_chunk_size)
                        asf_st->ds_span = 0; // disable descrambling
                }
                switch (st->codec->codec_id) {
                case CODEC_ID_MP3:
                    st->codec->frame_size = MPA_FRAME_SIZE;
                    break;
                case CODEC_ID_PCM_S16LE:
                case CODEC_ID_PCM_S16BE:
                case CODEC_ID_PCM_U16LE:
                case CODEC_ID_PCM_U16BE:
                case CODEC_ID_PCM_S8:
                case CODEC_ID_PCM_U8:
                case CODEC_ID_PCM_ALAW:
                case CODEC_ID_PCM_MULAW:
                    st->codec->frame_size = 1;
                    break;
                default:
                    /* This is probably wrong, but it prevents a crash later */
                    st->codec->frame_size = 1;
                    break;
                }
            } else if (type == AVMEDIA_TYPE_VIDEO &&
                       gsize - (url_ftell(pb) - pos1 + 24) >= 51) {
                get_le32(pb);
                get_le32(pb);
                get_byte(pb);
                size = get_le16(pb); /* size */
                sizeX= get_le32(pb); /* size */
                st->codec->width = get_le32(pb);
                st->codec->height = get_le32(pb);
                /* not available for asf */
                get_le16(pb); /* panes */
                st->codec->bits_per_coded_sample = get_le16(pb); /* depth */
                tag1 = get_le32(pb);
                url_fskip(pb, 20);
//                av_log(s, AV_LOG_DEBUG, "size:%d tsize:%d sizeX:%d\n", size, total_size, sizeX);
                size= sizeX;
                if (size > 40) {
                    st->codec->extradata_size = size - 40;
                    st->codec->extradata = av_mallocz(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
                    get_buffer(pb, st->codec->extradata, st->codec->extradata_size);
                }

                /* Extract palette from extradata if bpp <= 8 */
                /* This code assumes that extradata contains only palette */
                /* This is true for all paletted codecs implemented in ffmpeg */
                if (st->codec->extradata_size && (st->codec->bits_per_coded_sample <= 8)) {
                    st->codec->palctrl = av_mallocz(sizeof(AVPaletteControl));
#if HAVE_BIGENDIAN
                    for (i = 0; i < FFMIN(st->codec->extradata_size, AVPALETTE_SIZE)/4; i++)
                        st->codec->palctrl->palette[i] = av_bswap32(((uint32_t*)st->codec->extradata)[i]);
#else
                    memcpy(st->codec->palctrl->palette, st->codec->extradata,
                           FFMIN(st->codec->extradata_size, AVPALETTE_SIZE));
#endif
                    st->codec->palctrl->palette_changed = 1;
                }

                st->codec->codec_tag = tag1;
                st->codec->codec_id = ff_codec_get_id(ff_codec_bmp_tags, tag1);
                if(tag1 == MKTAG('D', 'V', 'R', ' ')){
                    st->need_parsing = AVSTREAM_PARSE_FULL;
                    // issue658 containse wrong w/h and MS even puts a fake seq header with wrong w/h in extradata while a correct one is in te stream. maximum lameness
                    st->codec->width  =
                    st->codec->height = 0;
                    av_freep(&st->codec->extradata);
                    st->codec->extradata_size=0;
                }
                if(st->codec->codec_id == CODEC_ID_H264)
                    st->need_parsing = AVSTREAM_PARSE_FULL_ONCE;
            }
            pos2 = url_ftell(pb);
            url_fskip(pb, gsize - (pos2 - pos1 + 24));
        } else if (!ff_guidcmp(&g, &ff_asf_comment_header)) {
            int len1, len2, len3, len4, len5;

            len1 = get_le16(pb);
            len2 = get_le16(pb);
            len3 = get_le16(pb);
            len4 = get_le16(pb);
            len5 = get_le16(pb);
            get_tag(s, "title"    , 0, len1);
            get_tag(s, "author"   , 0, len2);
            get_tag(s, "copyright", 0, len3);
            get_tag(s, "comment"  , 0, len4);
            url_fskip(pb, len5);
        } else if (!ff_guidcmp(&g, &stream_bitrate_guid)) {
            int stream_count = get_le16(pb);
            int j;

//            av_log(s, AV_LOG_ERROR, "stream bitrate properties\n");
//            av_log(s, AV_LOG_ERROR, "streams %d\n", streams);
            for(j = 0; j < stream_count; j++) {
                int flags, bitrate, stream_id;

                flags= get_le16(pb);
                bitrate= get_le32(pb);
                stream_id= (flags & 0x7f);
//                av_log(s, AV_LOG_ERROR, "flags: 0x%x stream id %d, bitrate %d\n", flags, stream_id, bitrate);
                asf->stream_bitrates[stream_id]= bitrate;
            }
        } else if (!ff_guidcmp(&g, &ff_asf_language_guid)) {
            int j;
            int stream_count = get_le16(pb);
            for(j = 0; j < stream_count; j++) {
                char lang[6];
                unsigned int lang_len = get_byte(pb);
                get_str16_nolen(pb, lang_len, lang, sizeof(lang));
                if (j < 128)
                    av_strlcpy(asf->stream_languages[j], lang, sizeof(*asf->stream_languages));
            }
        } else if (!ff_guidcmp(&g, &ff_asf_extended_content_header)) {
            int desc_count, i;

            desc_count = get_le16(pb);
            for(i=0;i<desc_count;i++) {
                    int name_len,value_type,value_len;
                    char name[1024];

                    name_len = get_le16(pb);
                    if (name_len%2)     // must be even, broken lavf versions wrote len-1
                        name_len += 1;
                    get_str16_nolen(pb, name_len, name, sizeof(name));
                    value_type = get_le16(pb);
                    value_len  = get_le16(pb);
                    if (!value_type && value_len%2)
                        value_len += 1;
                    /**
                     * My sample has that stream set to 0 maybe that mean the container.
                     * Asf stream count start at 1. I am using 0 to the container value since it's unused
                     */
                    if (!strcmp(name, "AspectRatioX")){
                        dar[0].num= get_value(s->pb, value_type);
                    } else if(!strcmp(name, "AspectRatioY")){
                        dar[0].den= get_value(s->pb, value_type);
                    } else
                        get_tag(s, name, value_type, value_len);
            }
        } else if (!ff_guidcmp(&g, &ff_asf_metadata_header)) {
            int n, stream_num, name_len, value_len, value_type, value_num;
            n = get_le16(pb);

            for(i=0;i<n;i++) {
                char name[1024];

                get_le16(pb); //lang_list_index
                stream_num= get_le16(pb);
                name_len=   get_le16(pb);
                value_type= get_le16(pb);
                value_len=  get_le32(pb);

                get_str16_nolen(pb, name_len, name, sizeof(name));
//av_log(s, AV_LOG_ERROR, "%d %d %d %d %d <%s>\n", i, stream_num, name_len, value_type, value_len, name);
                value_num= get_le16(pb);//we should use get_value() here but it does not work 2 is le16 here but le32 elsewhere
                url_fskip(pb, value_len - 2);

                if(stream_num<128){
                    if     (!strcmp(name, "AspectRatioX")) dar[stream_num].num= value_num;
                    else if(!strcmp(name, "AspectRatioY")) dar[stream_num].den= value_num;
                }
            }
        } else if (!ff_guidcmp(&g, &ff_asf_ext_stream_header)) {
            int ext_len, payload_ext_ct, stream_ct;
            uint32_t ext_d, leak_rate, stream_num;
            unsigned int stream_languageid_index;

            get_le64(pb); // starttime
            get_le64(pb); // endtime
            leak_rate = get_le32(pb); // leak-datarate
            get_le32(pb); // bucket-datasize
            get_le32(pb); // init-bucket-fullness
            get_le32(pb); // alt-leak-datarate
            get_le32(pb); // alt-bucket-datasize
            get_le32(pb); // alt-init-bucket-fullness
            get_le32(pb); // max-object-size
            get_le32(pb); // flags (reliable,seekable,no_cleanpoints?,resend-live-cleanpoints, rest of bits reserved)
            stream_num = get_le16(pb); // stream-num

            stream_languageid_index = get_le16(pb); // stream-language-id-index
            if (stream_num < 128)
                asf->streams[stream_num].stream_language_index = stream_languageid_index;

            get_le64(pb); // avg frametime in 100ns units
            stream_ct = get_le16(pb); //stream-name-count
            payload_ext_ct = get_le16(pb); //payload-extension-system-count

            if (stream_num < 128)
                bitrate[stream_num] = leak_rate;

            for (i=0; i<stream_ct; i++){
                get_le16(pb);
                ext_len = get_le16(pb);
                url_fseek(pb, ext_len, SEEK_CUR);
            }

            for (i=0; i<payload_ext_ct; i++){
                ff_get_guid(pb, &g);
                ext_d=get_le16(pb);
                ext_len=get_le32(pb);
                url_fseek(pb, ext_len, SEEK_CUR);
            }

            // there could be a optional stream properties object to follow
            // if so the next iteration will pick it up
            continue;
        } else if (!ff_guidcmp(&g, &ff_asf_head1_guid)) {
            int v1, v2;
            ff_get_guid(pb, &g);
            v1 = get_le32(pb);
            v2 = get_le16(pb);
            continue;
        } else if (!ff_guidcmp(&g, &ff_asf_marker_header)) {
            int i, count, name_len;
            char name[1024];

            get_le64(pb);            // reserved 16 bytes
            get_le64(pb);            // ...
            count = get_le32(pb);    // markers count
            get_le16(pb);            // reserved 2 bytes
            name_len = get_le16(pb); // name length
            for(i=0;i<name_len;i++){
                get_byte(pb); // skip the name
            }

            for(i=0;i<count;i++){
                int64_t pres_time;
                int name_len;

                get_le64(pb);             // offset, 8 bytes
                pres_time = get_le64(pb); // presentation time
                get_le16(pb);             // entry length
                get_le32(pb);             // send time
                get_le32(pb);             // flags
                name_len = get_le32(pb);  // name length
                get_str16_nolen(pb, name_len * 2, name, sizeof(name));
                ff_new_chapter(s, i, (AVRational){1, 10000000}, pres_time, AV_NOPTS_VALUE, name );
            }
#if 0
        } else if (!ff_guidcmp(&g, &ff_asf_codec_comment_header)) {
            int len, v1, n, num;
            char str[256], *q;
            char tag[16];

            ff_get_guid(pb, &g);
            print_guid(&g);

            n = get_le32(pb);
            for(i=0;i<n;i++) {
                num = get_le16(pb); /* stream number */
                get_str16(pb, str, sizeof(str));
                get_str16(pb, str, sizeof(str));
                len = get_le16(pb);
                q = tag;
                while (len > 0) {
                    v1 = get_byte(pb);
                    if ((q - tag) < sizeof(tag) - 1)
                        *q++ = v1;
                    len--;
                }
                *q = '\0';
            }
#endif
        } else if (url_feof(pb)) {
            return -1;
        } else {
            if (!s->keylen) {
                if (!ff_guidcmp(&g, &ff_asf_content_encryption)) {
                    av_log(s, AV_LOG_WARNING, "DRM protected stream detected, decoding will likely fail!\n");
                } else if (!ff_guidcmp(&g, &ff_asf_ext_content_encryption)) {
                    av_log(s, AV_LOG_WARNING, "Ext DRM protected stream detected, decoding will likely fail!\n");
                } else if (!ff_guidcmp(&g, &ff_asf_digital_signature)) {
                    av_log(s, AV_LOG_WARNING, "Digital signature detected, decoding will likely fail!\n");
                }
            }
        }
        if(url_ftell(pb) != gpos + gsize)
            av_log(s, AV_LOG_DEBUG, "gpos mismatch our pos=%"PRIu64", end=%"PRIu64"\n", url_ftell(pb)-gpos, gsize);
        url_fseek(pb, gpos + gsize, SEEK_SET);
    }
    ff_get_guid(pb, &g);
    get_le64(pb);
    get_byte(pb);
    get_byte(pb);
    if (url_feof(pb))
        return -1;
    asf->data_offset = url_ftell(pb);
    asf->packet_size_left = 0;


    for(i=0; i<128; i++){
        int stream_num= asf->asfid2avid[i];
        if(stream_num>=0){
            AVStream *st = s->streams[stream_num];
            if (!st->codec->bit_rate)
                st->codec->bit_rate = bitrate[i];
            if (dar[i].num > 0 && dar[i].den > 0){
                av_reduce(&st->sample_aspect_ratio.num,
                          &st->sample_aspect_ratio.den,
                          dar[i].num, dar[i].den, INT_MAX);
            } else if ((dar[0].num > 0) && (dar[0].den > 0) && (st->codec->codec_type==AVMEDIA_TYPE_VIDEO)) // Use ASF container value if the stream doesn't AR set.
                av_reduce(&st->sample_aspect_ratio.num,
                          &st->sample_aspect_ratio.den,
                          dar[0].num, dar[0].den, INT_MAX);

//av_log(s, AV_LOG_INFO, "i=%d, st->codec->codec_type:%d, dar %d:%d sar=%d:%d\n", i, st->codec->codec_type, dar[i].num, dar[i].den, st->sample_aspect_ratio.num, st->sample_aspect_ratio.den);

            // copy and convert language codes to the frontend
            if (asf->streams[i].stream_language_index < 128) {
                const char *rfc1766 = asf->stream_languages[asf->streams[i].stream_language_index];
                if (rfc1766 && strlen(rfc1766) > 1) {
                    const char primary_tag[3] = { rfc1766[0], rfc1766[1], '\0' }; // ignore country code if any
                    const char *iso6392 = av_convert_lang_to(primary_tag, AV_LANG_ISO639_2_BIBL);
                    if (iso6392)
                        av_metadata_set2(&st->metadata, "language", iso6392, 0);
                }
            }
        }
    }

    ff_metadata_conv(&s->metadata, NULL, ff_asf_metadata_conv);

    return 0;
}

#define DO_2BITS(bits, var, defval) \
    switch (bits & 3) \
    { \
    case 3: var = get_le32(pb); rsize += 4; break; \
    case 2: var = get_le16(pb); rsize += 2; break; \
    case 1: var = get_byte(pb); rsize++; break; \
    default: var = defval; break; \
    }

/**
 * Load a single ASF packet into the demuxer.
 * @param s demux context
 * @param pb context to read data from
 * @return 0 on success, <0 on error
 */
static int ff_asf_get_packet(AVFormatContext *s, ByteIOContext *pb)
{
    ASFContext *asf = s->priv_data;
    uint32_t packet_length, padsize;
    int rsize = 8;
    int c, d, e, off;

    // if we do not know packet size, allow skipping up to 32 kB
    off= 32768;
    if (s->packet_size > 0)
        off= (url_ftell(pb) - s->data_offset) % s->packet_size + 3;

    c=d=e=-1;
    while(off-- > 0){
        c=d; d=e;
        e= get_byte(pb);
        if(c == 0x82 && !d && !e)
            break;
    }

    if (c != 0x82) {
        /**
         * This code allows handling of -EAGAIN at packet boundaries (i.e.
         * if the packet sync code above triggers -EAGAIN). This does not
         * imply complete -EAGAIN handling support at random positions in
         * the stream.
         */
        if (url_ferror(pb) == AVERROR(EAGAIN))
            return AVERROR(EAGAIN);
        if (!url_feof(pb))
            av_log(s, AV_LOG_ERROR, "ff asf bad header %x  at:%"PRId64"\n", c, url_ftell(pb));
    }
    if ((c & 0x8f) == 0x82) {
        if (d || e) {
            if (!url_feof(pb))
                av_log(s, AV_LOG_ERROR, "ff asf bad non zero\n");
            return -1;
        }
        c= get_byte(pb);
        d= get_byte(pb);
        rsize+=3;
    }else{
        url_fseek(pb, -1, SEEK_CUR); //FIXME
    }

    asf->packet_flags    = c;
    asf->packet_property = d;

    DO_2BITS(asf->packet_flags >> 5, packet_length, s->packet_size);
    DO_2BITS(asf->packet_flags >> 1, padsize, 0); // sequence ignored
    DO_2BITS(asf->packet_flags >> 3, padsize, 0); // padding length

    //the following checks prevent overflows and infinite loops
    if(!packet_length || packet_length >= (1U<<29)){
        av_log(s, AV_LOG_ERROR, "invalid packet_length %d at:%"PRId64"\n", packet_length, url_ftell(pb));
        return -1;
    }
    if(padsize >= packet_length){
        av_log(s, AV_LOG_ERROR, "invalid padsize %d at:%"PRId64"\n", padsize, url_ftell(pb));
        return -1;
    }

    asf->packet_timestamp = get_le32(pb);
    get_le16(pb); /* duration */
    // rsize has at least 11 bytes which have to be present

    if (asf->packet_flags & 0x01) {
        asf->packet_segsizetype = get_byte(pb); rsize++;
        asf->packet_segments = asf->packet_segsizetype & 0x3f;
    } else {
        asf->packet_segments = 1;
        asf->packet_segsizetype = 0x80;
    }
    asf->packet_size_left = packet_length - padsize - rsize;
    if (packet_length < asf->hdr.min_pktsize)
        padsize += asf->hdr.min_pktsize - packet_length;
    asf->packet_padsize = padsize;
    dprintf(s, "packet: size=%d padsize=%d  left=%d\n", s->packet_size, asf->packet_padsize, asf->packet_size_left);
    return 0;
}

/**
 *
 * @return <0 if error
 */
static int asf_read_frame_header(AVFormatContext *s, ByteIOContext *pb){
    ASFContext *asf = s->priv_data;
    int rsize = 1;
    int num = get_byte(pb);
    int64_t ts0, ts1;

    asf->packet_segments--;
    asf->packet_key_frame = num >> 7;
    asf->stream_index = asf->asfid2avid[num & 0x7f];
    // sequence should be ignored!
    DO_2BITS(asf->packet_property >> 4, asf->packet_seq, 0);
    DO_2BITS(asf->packet_property >> 2, asf->packet_frag_offset, 0);
    DO_2BITS(asf->packet_property, asf->packet_replic_size, 0);
//printf("key:%d stream:%d seq:%d offset:%d replic_size:%d\n", asf->packet_key_frame, asf->stream_index, asf->packet_seq, //asf->packet_frag_offset, asf->packet_replic_size);
    if (asf->packet_replic_size >= 8) {
        asf->packet_obj_size = get_le32(pb);
        if(asf->packet_obj_size >= (1<<24) || asf->packet_obj_size <= 0){
            av_log(s, AV_LOG_ERROR, "packet_obj_size invalid\n");
            return -1;
        }
        asf->packet_frag_timestamp = get_le32(pb); // timestamp
        if(asf->packet_replic_size >= 8+38+4){
//            for(i=0; i<asf->packet_replic_size-8; i++)
//                av_log(s, AV_LOG_DEBUG, "%02X ",get_byte(pb));
//            av_log(s, AV_LOG_DEBUG, "\n");
            url_fskip(pb, 10);
            ts0= get_le64(pb);
            ts1= get_le64(pb);
            url_fskip(pb, 12);
            get_le32(pb);
            url_fskip(pb, asf->packet_replic_size - 8 - 38 - 4);
            if(ts0!= -1) asf->packet_frag_timestamp= ts0/10000;
            else         asf->packet_frag_timestamp= AV_NOPTS_VALUE;
        }else
            url_fskip(pb, asf->packet_replic_size - 8);
        rsize += asf->packet_replic_size; // FIXME - check validity
    } else if (asf->packet_replic_size==1){
        // multipacket - frag_offset is beginning timestamp
        asf->packet_time_start = asf->packet_frag_offset;
        asf->packet_frag_offset = 0;
        asf->packet_frag_timestamp = asf->packet_timestamp;

        asf->packet_time_delta = get_byte(pb);
        rsize++;
    }else if(asf->packet_replic_size!=0){
        av_log(s, AV_LOG_ERROR, "unexpected packet_replic_size of %d\n", asf->packet_replic_size);
        return -1;
    }
    if (asf->packet_flags & 0x01) {
        DO_2BITS(asf->packet_segsizetype >> 6, asf->packet_frag_size, 0); // 0 is illegal
        if(asf->packet_frag_size > asf->packet_size_left - rsize){
            av_log(s, AV_LOG_ERROR, "packet_frag_size is invalid\n");
            return -1;
        }
        //printf("Fragsize %d\n", asf->packet_frag_size);
    } else {
        asf->packet_frag_size = asf->packet_size_left - rsize;
        //printf("Using rest  %d %d %d\n", asf->packet_frag_size, asf->packet_size_left, rsize);
    }
    if (asf->packet_replic_size == 1) {
        asf->packet_multi_size = asf->packet_frag_size;
        if (asf->packet_multi_size > asf->packet_size_left)
            return -1;
    }
    asf->packet_size_left -= rsize;
    //printf("___objsize____  %d   %d    rs:%d\n", asf->packet_obj_size, asf->packet_frag_offset, rsize);

    return 0;
}

/**
 * Parse data from individual ASF packets (which were previously loaded
 * with asf_get_packet()).
 * @param s demux context
 * @param pb context to read data from
 * @param pkt pointer to store packet data into
 * @return 0 if data was stored in pkt, <0 on error or 1 if more ASF
 *          packets need to be loaded (through asf_get_packet())
 */
static int ff_asf_parse_packet(AVFormatContext *s, ByteIOContext *pb, AVPacket *pkt)
{
    ASFContext *asf = s->priv_data;
    ASFStream *asf_st = 0;
    for (;;) {
        int ret;
        if(url_feof(pb))
            return AVERROR_EOF;
        if (asf->packet_size_left < FRAME_HEADER_SIZE
            || asf->packet_segments < 1) {
            //asf->packet_size_left <= asf->packet_padsize) {
            int ret = asf->packet_size_left + asf->packet_padsize;
            //printf("PacketLeftSize:%d  Pad:%d Pos:%"PRId64"\n", asf->packet_size_left, asf->packet_padsize, url_ftell(pb));
            assert(ret>=0);
            /* fail safe */
            url_fskip(pb, ret);

            asf->packet_pos= url_ftell(pb);
            if (asf->data_object_size != (uint64_t)-1 &&
                (asf->packet_pos - asf->data_object_offset >= asf->data_object_size))
                return AVERROR_EOF; /* Do not exceed the size of the data object */
            return 1;
        }
        if (asf->packet_time_start == 0) {
            if(asf_read_frame_header(s, pb) < 0){
                asf->packet_segments= 0;
                continue;
            }
            if (asf->stream_index < 0
                || s->streams[asf->stream_index]->discard >= AVDISCARD_ALL
                || (!asf->packet_key_frame && s->streams[asf->stream_index]->discard >= AVDISCARD_NONKEY)
                ) {
                asf->packet_time_start = 0;
                /* unhandled packet (should not happen) */
                url_fskip(pb, asf->packet_frag_size);
                asf->packet_size_left -= asf->packet_frag_size;
                if(asf->stream_index < 0)
                    av_log(s, AV_LOG_ERROR, "ff asf skip %d (unknown stream)\n", asf->packet_frag_size);
                continue;
            }
            asf->asf_st = s->streams[asf->stream_index]->priv_data;
        }
        asf_st = asf->asf_st;

        if (asf->packet_replic_size == 1) {
            // frag_offset is here used as the beginning timestamp
            asf->packet_frag_timestamp = asf->packet_time_start;
            asf->packet_time_start += asf->packet_time_delta;
            asf->packet_obj_size = asf->packet_frag_size = get_byte(pb);
            asf->packet_size_left--;
            asf->packet_multi_size--;
            if (asf->packet_multi_size < asf->packet_obj_size)
            {
                asf->packet_time_start = 0;
                url_fskip(pb, asf->packet_multi_size);
                asf->packet_size_left -= asf->packet_multi_size;
                continue;
            }
            asf->packet_multi_size -= asf->packet_obj_size;
            //printf("COMPRESS size  %d  %d  %d   ms:%d\n", asf->packet_obj_size, asf->packet_frag_timestamp, asf->packet_size_left, asf->packet_multi_size);
        }
        if(   /*asf->packet_frag_size == asf->packet_obj_size*/
              asf_st->frag_offset + asf->packet_frag_size <= asf_st->pkt.size
           && asf_st->frag_offset + asf->packet_frag_size > asf->packet_obj_size){
            av_log(s, AV_LOG_INFO, "ignoring invalid packet_obj_size (%d %d %d %d)\n",
                asf_st->frag_offset, asf->packet_frag_size,
                asf->packet_obj_size, asf_st->pkt.size);
            asf->packet_obj_size= asf_st->pkt.size;
        }

        if (   asf_st->pkt.size != asf->packet_obj_size
            || asf_st->frag_offset + asf->packet_frag_size > asf_st->pkt.size) { //FIXME is this condition sufficient?
            if(asf_st->pkt.data){
                av_log(s, AV_LOG_INFO, "freeing incomplete packet size %d, new %d\n", asf_st->pkt.size, asf->packet_obj_size);
                asf_st->frag_offset = 0;
                av_free_packet(&asf_st->pkt);
            }
            /* new packet */
            av_new_packet(&asf_st->pkt, asf->packet_obj_size);
            asf_st->seq = asf->packet_seq;
            asf_st->pkt.dts = asf->packet_frag_timestamp;
            asf_st->pkt.stream_index = asf->stream_index;
            asf_st->pkt.pos =
            asf_st->packet_pos= asf->packet_pos;
//printf("new packet: stream:%d key:%d packet_key:%d audio:%d size:%d\n",
//asf->stream_index, asf->packet_key_frame, asf_st->pkt.flags & AV_PKT_FLAG_KEY,
//s->streams[asf->stream_index]->codec->codec_type == AVMEDIA_TYPE_AUDIO, asf->packet_obj_size);
            if (s->streams[asf->stream_index]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
                asf->packet_key_frame = 1;
            if (asf->packet_key_frame)
                asf_st->pkt.flags |= AV_PKT_FLAG_KEY;
        }

        /* read data */
        //printf("READ PACKET s:%d  os:%d  o:%d,%d  l:%d   DATA:%p\n",
        //       s->packet_size, asf_st->pkt.size, asf->packet_frag_offset,
        //       asf_st->frag_offset, asf->packet_frag_size, asf_st->pkt.data);
        asf->packet_size_left -= asf->packet_frag_size;
        if (asf->packet_size_left < 0)
            continue;

        if(   asf->packet_frag_offset >= asf_st->pkt.size
           || asf->packet_frag_size > asf_st->pkt.size - asf->packet_frag_offset){
            av_log(s, AV_LOG_ERROR, "packet fragment position invalid %u,%u not in %u\n",
                asf->packet_frag_offset, asf->packet_frag_size, asf_st->pkt.size);
            continue;
        }

        ret = get_buffer(pb, asf_st->pkt.data + asf->packet_frag_offset,
                         asf->packet_frag_size);
        if (ret != asf->packet_frag_size) {
            if (ret < 0 || asf->packet_frag_offset + ret == 0)
                return ret < 0 ? ret : AVERROR_EOF;
            if (asf_st->ds_span > 1) {
                // scrambling, we can either drop it completely or fill the remainder
                // TODO: should we fill the whole packet instead of just the current
                // fragment?
                memset(asf_st->pkt.data + asf->packet_frag_offset + ret, 0,
                       asf->packet_frag_size - ret);
                ret = asf->packet_frag_size;
            } else
                // no scrambling, so we can return partial packets
                av_shrink_packet(&asf_st->pkt, asf->packet_frag_offset + ret);
        }
        if (s->key && s->keylen == 20)
            ff_asfcrypt_dec(s->key, asf_st->pkt.data + asf->packet_frag_offset,
                            ret);
        asf_st->frag_offset += ret;
        /* test if whole packet is read */
        if (asf_st->frag_offset == asf_st->pkt.size) {
            //workaround for macroshit radio DVR-MS files
            if(   s->streams[asf->stream_index]->codec->codec_id == CODEC_ID_MPEG2VIDEO
               && asf_st->pkt.size > 100){
                int i;
                for(i=0; i<asf_st->pkt.size && !asf_st->pkt.data[i]; i++);
                if(i == asf_st->pkt.size){
                    av_log(s, AV_LOG_DEBUG, "discarding ms fart\n");
                    asf_st->frag_offset = 0;
                    av_free_packet(&asf_st->pkt);
                    continue;
                }
            }

            /* return packet */
            if (asf_st->ds_span > 1) {
              if(asf_st->pkt.size != asf_st->ds_packet_size * asf_st->ds_span){
                    av_log(s, AV_LOG_ERROR, "pkt.size != ds_packet_size * ds_span (%d %d %d)\n", asf_st->pkt.size, asf_st->ds_packet_size, asf_st->ds_span);
              }else{
                /* packet descrambling */
                uint8_t *newdata = av_malloc(asf_st->pkt.size + FF_INPUT_BUFFER_PADDING_SIZE);
                if (newdata) {
                    int offset = 0;
                    memset(newdata + asf_st->pkt.size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
                    while (offset < asf_st->pkt.size) {
                        int off = offset / asf_st->ds_chunk_size;
                        int row = off / asf_st->ds_span;
                        int col = off % asf_st->ds_span;
                        int idx = row + col * asf_st->ds_packet_size / asf_st->ds_chunk_size;
                        //printf("off:%d  row:%d  col:%d  idx:%d\n", off, row, col, idx);

                        assert(offset + asf_st->ds_chunk_size <= asf_st->pkt.size);
                        assert(idx+1 <= asf_st->pkt.size / asf_st->ds_chunk_size);
                        memcpy(newdata + offset,
                               asf_st->pkt.data + idx * asf_st->ds_chunk_size,
                               asf_st->ds_chunk_size);
                        offset += asf_st->ds_chunk_size;
                    }
                    av_free(asf_st->pkt.data);
                    asf_st->pkt.data = newdata;
                }
              }
            }
            asf_st->frag_offset = 0;
            *pkt= asf_st->pkt;
            //printf("packet %d %d\n", asf_st->pkt.size, asf->packet_frag_size);
            asf_st->pkt.size = 0;
            asf_st->pkt.data = 0;
            break; // packet completed
        }
    }
    return 0;
}

static int asf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ASFContext *asf = s->priv_data;

    for (;;) {
        int ret;

        /* parse cached packets, if any */
        if ((ret = ff_asf_parse_packet(s, s->pb, pkt)) <= 0)
            return ret;
        if ((ret = ff_asf_get_packet(s, s->pb)) < 0)
            assert(asf->packet_size_left < FRAME_HEADER_SIZE || asf->packet_segments < 1);
        asf->packet_time_start = 0;
    }

    return 0;
}

// Added to support seeking after packets have been read
// If information is not reset, read_packet fails due to
// leftover information from previous reads
static void asf_reset_header(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;
    ASFStream *asf_st;
    int i;

    asf->packet_nb_frames = 0;
    asf->packet_size_left = 0;
    asf->packet_segments = 0;
    asf->packet_flags = 0;
    asf->packet_property = 0;
    asf->packet_timestamp = 0;
    asf->packet_segsizetype = 0;
    asf->packet_segments = 0;
    asf->packet_seq = 0;
    asf->packet_replic_size = 0;
    asf->packet_key_frame = 0;
    asf->packet_padsize = 0;
    asf->packet_frag_offset = 0;
    asf->packet_frag_size = 0;
    asf->packet_frag_timestamp = 0;
    asf->packet_multi_size = 0;
    asf->packet_obj_size = 0;
    asf->packet_time_delta = 0;
    asf->packet_time_start = 0;

    for(i=0; i<s->nb_streams; i++){
        asf_st= s->streams[i]->priv_data;
        av_free_packet(&asf_st->pkt);
        asf_st->frag_offset=0;
        asf_st->seq=0;
    }
    asf->asf_st= NULL;
}

static int asf_read_close(AVFormatContext *s)
{
    int i;

    asf_reset_header(s);
    for(i=0;i<s->nb_streams;i++) {
        AVStream *st = s->streams[i];
        av_free(st->codec->palctrl);
    }
    return 0;
}

static int64_t asf_read_pts(AVFormatContext *s, int stream_index, int64_t *ppos, int64_t pos_limit)
{
    AVPacket pkt1, *pkt = &pkt1;
    ASFStream *asf_st;
    int64_t pts;
    int64_t pos= *ppos;
    int i;
    int64_t start_pos[ASF_MAX_STREAMS];

    for(i=0; i<s->nb_streams; i++){
        start_pos[i]= pos;
    }

    if (s->packet_size > 0)
        pos= (pos+s->packet_size-1-s->data_offset)/s->packet_size*s->packet_size+ s->data_offset;
    *ppos= pos;
    url_fseek(s->pb, pos, SEEK_SET);

//printf("asf_read_pts\n");
    asf_reset_header(s);
    for(;;){
        if (av_read_frame(s, pkt) < 0){
            av_log(s, AV_LOG_INFO, "asf_read_pts failed\n");
            return AV_NOPTS_VALUE;
        }

        pts= pkt->pts;

        av_free_packet(pkt);
        if(pkt->flags&AV_PKT_FLAG_KEY){
            i= pkt->stream_index;

            asf_st= s->streams[i]->priv_data;

//            assert((asf_st->packet_pos - s->data_offset) % s->packet_size == 0);
            pos= asf_st->packet_pos;

            av_add_index_entry(s->streams[i], pos, pts, pkt->size, pos - start_pos[i] + 1, AVINDEX_KEYFRAME);
            start_pos[i]= asf_st->packet_pos + 1;

            if(pkt->stream_index == stream_index)
               break;
        }
    }

    *ppos= pos;
//printf("found keyframe at %"PRId64" stream %d stamp:%"PRId64"\n", *ppos, stream_index, pts);

    return pts;
}

static void asf_build_simple_index(AVFormatContext *s, int stream_index)
{
    ff_asf_guid g;
    ASFContext *asf = s->priv_data;
    int64_t current_pos= url_ftell(s->pb);
    int i;

    url_fseek(s->pb, asf->data_object_offset + asf->data_object_size, SEEK_SET);
    ff_get_guid(s->pb, &g);

    /* the data object can be followed by other top-level objects,
       skip them until the simple index object is reached */
    while (ff_guidcmp(&g, &index_guid)) {
        int64_t gsize= get_le64(s->pb);
        if (gsize < 24 || url_feof(s->pb)) {
            url_fseek(s->pb, current_pos, SEEK_SET);
            return;
        }
        url_fseek(s->pb, gsize-24, SEEK_CUR);
        ff_get_guid(s->pb, &g);
    }

    {
        int64_t itime, last_pos=-1;
        int pct, ict;
        int64_t av_unused gsize= get_le64(s->pb);
        ff_get_guid(s->pb, &g);
        itime=get_le64(s->pb);
        pct=get_le32(s->pb);
        ict=get_le32(s->pb);
        av_log(s, AV_LOG_DEBUG, "itime:0x%"PRIx64", pct:%d, ict:%d\n",itime,pct,ict);

        for (i=0;i<ict;i++){
            int pktnum=get_le32(s->pb);
            int pktct =get_le16(s->pb);
            int64_t pos      = s->data_offset + s->packet_size*(int64_t)pktnum;
            int64_t index_pts= av_rescale(itime, i, 10000);

            if(pos != last_pos){
            av_log(s, AV_LOG_DEBUG, "pktnum:%d, pktct:%d\n", pktnum, pktct);
            av_add_index_entry(s->streams[stream_index], pos, index_pts, s->packet_size, 0, AVINDEX_KEYFRAME);
            last_pos=pos;
            }
        }
        asf->index_read= 1;
    }
    url_fseek(s->pb, current_pos, SEEK_SET);
}

static int asf_read_seek(AVFormatContext *s, int stream_index, int64_t pts, int flags)
{
    ASFContext *asf = s->priv_data;
    AVStream *st = s->streams[stream_index];
    int64_t pos;
    int index;

    if (s->packet_size <= 0)
        return -1;

    /* Try using the protocol's read_seek if available */
    if(s->pb) {
        int ret = av_url_read_fseek(s->pb, stream_index, pts, flags);
        if(ret >= 0)
            asf_reset_header(s);
        if (ret != AVERROR(ENOSYS))
            return ret;
    }

    if (!asf->index_read)
        asf_build_simple_index(s, stream_index);

    if(!(asf->index_read && st->index_entries)){
        if(av_seek_frame_binary(s, stream_index, pts, flags)<0)
            return -1;
    }else{
        index= av_index_search_timestamp(st, pts, flags);
        if(index<0)
            return -1;

        /* find the position */
        pos = st->index_entries[index].pos;

    // various attempts to find key frame have failed so far
    //    asf_reset_header(s);
    //    url_fseek(s->pb, pos, SEEK_SET);
    //    key_pos = pos;
    //     for(i=0;i<16;i++){
    //         pos = url_ftell(s->pb);
    //         if (av_read_frame(s, &pkt) < 0){
    //             av_log(s, AV_LOG_INFO, "seek failed\n");
    //             return -1;
    //         }
    //         asf_st = s->streams[stream_index]->priv_data;
    //         pos += st->parser->frame_offset;
    //
    //         if (pkt.size > b) {
    //             b = pkt.size;
    //             key_pos = pos;
    //         }
    //
    //         av_free_packet(&pkt);
    //     }

        /* do the seek */
        av_log(s, AV_LOG_DEBUG, "SEEKTO: %"PRId64"\n", pos);
        url_fseek(s->pb, pos, SEEK_SET);
    }
    asf_reset_header(s);
    return 0;
}

AVInputFormat asf_demuxer = {
    "asf",
    NULL_IF_CONFIG_SMALL("ASF format"),
    sizeof(ASFContext),
    asf_probe,
    asf_read_header,
    asf_read_packet,
    asf_read_close,
    asf_read_seek,
    asf_read_pts,
};
