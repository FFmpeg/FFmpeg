/*
 * copyright (c) 2007 Luca Abeni
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

#include <string.h>
#include "libavutil/avstring.h"
#include "libavutil/base64.h"
#include "libavutil/dict.h"
#include "libavutil/parseutils.h"
#include "libavutil/opt.h"
#include "libavcodec/xiph.h"
#include "libavcodec/mpeg4audio.h"
#include "avformat.h"
#include "internal.h"
#include "avc.h"
#include "rtp.h"
#if CONFIG_NETWORK
#include "network.h"
#endif

#if CONFIG_RTP_MUXER
#define MAX_EXTRADATA_SIZE ((INT_MAX - 10) / 2)

struct sdp_session_level {
    int sdp_version;      /**< protocol version (currently 0) */
    int id;               /**< session ID */
    int version;          /**< session version */
    int start_time;       /**< session start time (NTP time, in seconds),
                               or 0 in case of permanent session */
    int end_time;         /**< session end time (NTP time, in seconds),
                               or 0 if the session is not bounded */
    int ttl;              /**< TTL, in case of multicast stream */
    const char *user;     /**< username of the session's creator */
    const char *src_addr; /**< IP address of the machine from which the session was created */
    const char *src_type; /**< address type of src_addr */
    const char *dst_addr; /**< destination IP address (can be multicast) */
    const char *dst_type; /**< destination IP address type */
    const char *name;     /**< session name (can be an empty string) */
};

static void sdp_write_address(char *buff, int size, const char *dest_addr,
                              const char *dest_type, int ttl)
{
    if (dest_addr) {
        if (!dest_type)
            dest_type = "IP4";
        if (ttl > 0 && !strcmp(dest_type, "IP4")) {
            /* The TTL should only be specified for IPv4 multicast addresses,
             * not for IPv6. */
            av_strlcatf(buff, size, "c=IN %s %s/%d\r\n", dest_type, dest_addr, ttl);
        } else {
            av_strlcatf(buff, size, "c=IN %s %s\r\n", dest_type, dest_addr);
        }
    }
}

static void sdp_write_header(char *buff, int size, struct sdp_session_level *s)
{
    av_strlcatf(buff, size, "v=%d\r\n"
                            "o=- %d %d IN %s %s\r\n"
                            "s=%s\r\n",
                            s->sdp_version,
                            s->id, s->version, s->src_type, s->src_addr,
                            s->name);
    sdp_write_address(buff, size, s->dst_addr, s->dst_type, s->ttl);
    av_strlcatf(buff, size, "t=%d %d\r\n"
                            "a=tool:libavformat " AV_STRINGIFY(LIBAVFORMAT_VERSION) "\r\n",
                            s->start_time, s->end_time);
}

#if CONFIG_NETWORK
static int resolve_destination(char *dest_addr, int size, char *type,
                               int type_size)
{
    struct addrinfo hints, *ai;
    int is_multicast;

    av_strlcpy(type, "IP4", type_size);
    if (!dest_addr[0])
        return 0;

    /* Resolve the destination, since it must be written
     * as a numeric IP address in the SDP. */

    memset(&hints, 0, sizeof(hints));
    if (getaddrinfo(dest_addr, NULL, &hints, &ai))
        return 0;
    getnameinfo(ai->ai_addr, ai->ai_addrlen, dest_addr, size,
                NULL, 0, NI_NUMERICHOST);
#ifdef AF_INET6
    if (ai->ai_family == AF_INET6)
        av_strlcpy(type, "IP6", type_size);
#endif
    is_multicast = ff_is_multicast_address(ai->ai_addr);
    freeaddrinfo(ai);
    return is_multicast;
}
#else
static int resolve_destination(char *dest_addr, int size, char *type,
                               int type_size)
{
    return 0;
}
#endif

static int sdp_get_address(char *dest_addr, int size, int *ttl, const char *url)
{
    int port;
    const char *p;
    char proto[32];

    av_url_split(proto, sizeof(proto), NULL, 0, dest_addr, size, &port, NULL, 0, url);

    *ttl = 0;

    if (strcmp(proto, "rtp")) {
        /* The url isn't for the actual rtp sessions,
         * don't parse out anything else than the destination.
         */
        return 0;
    }

    p = strchr(url, '?');
    if (p) {
        char buff[64];

        if (av_find_info_tag(buff, sizeof(buff), "ttl", p)) {
            *ttl = strtol(buff, NULL, 10);
        } else {
            *ttl = 5;
        }
    }

    return port;
}

#define MAX_PSET_SIZE 1024
static char *extradata2psets(AVCodecContext *c)
{
    char *psets, *p;
    const uint8_t *r;
    const char *pset_string = "; sprop-parameter-sets=";

    if (c->extradata_size > MAX_EXTRADATA_SIZE) {
        av_log(c, AV_LOG_ERROR, "Too much extradata!\n");

        return NULL;
    }
    if (c->extradata[0] == 1) {
        uint8_t *dummy_p;
        int dummy_int;
        AVBitStreamFilterContext *bsfc= av_bitstream_filter_init("h264_mp4toannexb");

        if (!bsfc) {
            av_log(c, AV_LOG_ERROR, "Cannot open the h264_mp4toannexb BSF!\n");

            return NULL;
        }
        av_bitstream_filter_filter(bsfc, c, NULL, &dummy_p, &dummy_int, NULL, 0, 0);
        av_bitstream_filter_close(bsfc);
    }

    psets = av_mallocz(MAX_PSET_SIZE);
    if (psets == NULL) {
        av_log(c, AV_LOG_ERROR, "Cannot allocate memory for the parameter sets.\n");
        return NULL;
    }
    memcpy(psets, pset_string, strlen(pset_string));
    p = psets + strlen(pset_string);
    r = ff_avc_find_startcode(c->extradata, c->extradata + c->extradata_size);
    while (r < c->extradata + c->extradata_size) {
        const uint8_t *r1;
        uint8_t nal_type;

        while (!*(r++));
        nal_type = *r & 0x1f;
        r1 = ff_avc_find_startcode(r, c->extradata + c->extradata_size);
        if (nal_type != 7 && nal_type != 8) { /* Only output SPS and PPS */
            r = r1;
            continue;
        }
        if (p != (psets + strlen(pset_string))) {
            *p = ',';
            p++;
        }
        if (av_base64_encode(p, MAX_PSET_SIZE - (p - psets), r, r1 - r) == NULL) {
            av_log(c, AV_LOG_ERROR, "Cannot Base64-encode %td %td!\n", MAX_PSET_SIZE - (p - psets), r1 - r);
            av_free(psets);

            return NULL;
        }
        p += strlen(p);
        r = r1;
    }

    return psets;
}

static char *extradata2config(AVCodecContext *c)
{
    char *config;

    if (c->extradata_size > MAX_EXTRADATA_SIZE) {
        av_log(c, AV_LOG_ERROR, "Too much extradata!\n");

        return NULL;
    }
    config = av_malloc(10 + c->extradata_size * 2);
    if (config == NULL) {
        av_log(c, AV_LOG_ERROR, "Cannot allocate memory for the config info.\n");
        return NULL;
    }
    memcpy(config, "; config=", 9);
    ff_data_to_hex(config + 9, c->extradata, c->extradata_size, 0);
    config[9 + c->extradata_size * 2] = 0;

    return config;
}

static char *xiph_extradata2config(AVCodecContext *c)
{
    char *config, *encoded_config;
    uint8_t *header_start[3];
    int headers_len, header_len[3], config_len;
    int first_header_size;

    switch (c->codec_id) {
    case CODEC_ID_THEORA:
        first_header_size = 42;
        break;
    case CODEC_ID_VORBIS:
        first_header_size = 30;
        break;
    default:
        av_log(c, AV_LOG_ERROR, "Unsupported Xiph codec ID\n");
        return NULL;
    }

    if (ff_split_xiph_headers(c->extradata, c->extradata_size,
                              first_header_size, header_start,
                              header_len) < 0) {
        av_log(c, AV_LOG_ERROR, "Extradata corrupt.\n");
        return NULL;
    }

    headers_len = header_len[0] + header_len[2];
    config_len = 4 +          // count
                 3 +          // ident
                 2 +          // packet size
                 1 +          // header count
                 2 +          // header size
                 headers_len; // and the rest

    config = av_malloc(config_len);
    if (!config)
        goto xiph_fail;

    encoded_config = av_malloc(AV_BASE64_SIZE(config_len));
    if (!encoded_config) {
        av_free(config);
        goto xiph_fail;
    }

    config[0] = config[1] = config[2] = 0;
    config[3] = 1;
    config[4] = (RTP_XIPH_IDENT >> 16) & 0xff;
    config[5] = (RTP_XIPH_IDENT >>  8) & 0xff;
    config[6] = (RTP_XIPH_IDENT      ) & 0xff;
    config[7] = (headers_len >> 8) & 0xff;
    config[8] = headers_len & 0xff;
    config[9] = 2;
    config[10] = header_len[0];
    config[11] = 0; // size of comment header; nonexistent
    memcpy(config + 12, header_start[0], header_len[0]);
    memcpy(config + 12 + header_len[0], header_start[2], header_len[2]);

    av_base64_encode(encoded_config, AV_BASE64_SIZE(config_len),
                     config, config_len);
    av_free(config);

    return encoded_config;

xiph_fail:
    av_log(c, AV_LOG_ERROR,
           "Not enough memory for configuration string\n");
    return NULL;
}

static int latm_context2profilelevel(AVCodecContext *c)
{
    /* MP4A-LATM
     * The RTP payload format specification is described in RFC 3016
     * The encoding specifications are provided in ISO/IEC 14496-3 */

    int profile_level = 0x2B;

    /* TODO: AAC Profile only supports AAC LC Object Type.
     * Different Object Types should implement different Profile Levels */

    if (c->sample_rate <= 24000) {
        if (c->channels <= 2)
            profile_level = 0x28; // AAC Profile, Level 1
    } else if (c->sample_rate <= 48000) {
        if (c->channels <= 2) {
            profile_level = 0x29; // AAC Profile, Level 2
        } else if (c->channels <= 5) {
            profile_level = 0x2A; // AAC Profile, Level 4
        }
    } else if (c->sample_rate <= 96000) {
        if (c->channels <= 5) {
            profile_level = 0x2B; // AAC Profile, Level 5
        }
    }

    return profile_level;
}

static char *latm_context2config(AVCodecContext *c)
{
    /* MP4A-LATM
     * The RTP payload format specification is described in RFC 3016
     * The encoding specifications are provided in ISO/IEC 14496-3 */

    uint8_t config_byte[6];
    int rate_index;
    char *config;

    for (rate_index = 0; rate_index < 16; rate_index++)
        if (ff_mpeg4audio_sample_rates[rate_index] == c->sample_rate)
            break;
    if (rate_index == 16) {
        av_log(c, AV_LOG_ERROR, "Unsupported sample rate\n");
        return NULL;
    }

    config_byte[0] = 0x40;
    config_byte[1] = 0;
    config_byte[2] = 0x20 | rate_index;
    config_byte[3] = c->channels << 4;
    config_byte[4] = 0x3f;
    config_byte[5] = 0xc0;

    config = av_malloc(6*2+1);
    if (!config) {
        av_log(c, AV_LOG_ERROR, "Cannot allocate memory for the config info.\n");
        return NULL;
    }
    ff_data_to_hex(config, config_byte, 6, 1);
    config[12] = 0;

    return config;
}

static char *sdp_write_media_attributes(char *buff, int size, AVCodecContext *c, int payload_type, AVFormatContext *fmt)
{
    char *config = NULL;

    switch (c->codec_id) {
        case CODEC_ID_H264:
            if (c->extradata_size) {
                config = extradata2psets(c);
            }
            av_strlcatf(buff, size, "a=rtpmap:%d H264/90000\r\n"
                                    "a=fmtp:%d packetization-mode=1%s\r\n",
                                     payload_type,
                                     payload_type, config ? config : "");
            break;
        case CODEC_ID_H263:
        case CODEC_ID_H263P:
            /* a=framesize is required by 3GPP TS 26.234 (PSS). It
             * actually specifies the maximum video size, but we only know
             * the current size. This is required for playback on Android
             * stagefright and on Samsung bada. */
            av_strlcatf(buff, size, "a=rtpmap:%d H263-2000/90000\r\n"
                                    "a=framesize:%d %d-%d\r\n",
                                    payload_type,
                                    payload_type, c->width, c->height);
            break;
        case CODEC_ID_MPEG4:
            if (c->extradata_size) {
                config = extradata2config(c);
            }
            av_strlcatf(buff, size, "a=rtpmap:%d MP4V-ES/90000\r\n"
                                    "a=fmtp:%d profile-level-id=1%s\r\n",
                                     payload_type,
                                     payload_type, config ? config : "");
            break;
        case CODEC_ID_AAC:
            if (fmt && fmt->oformat->priv_class &&
                av_opt_flag_is_set(fmt->priv_data, "rtpflags", "latm")) {
                config = latm_context2config(c);
                if (!config)
                    return NULL;
                av_strlcatf(buff, size, "a=rtpmap:%d MP4A-LATM/%d/%d\r\n"
                                        "a=fmtp:%d profile-level-id=%d;cpresent=0;config=%s\r\n",
                                         payload_type, c->sample_rate, c->channels,
                                         payload_type, latm_context2profilelevel(c), config);
            } else {
                if (c->extradata_size) {
                    config = extradata2config(c);
                } else {
                    /* FIXME: maybe we can forge config information based on the
                     *        codec parameters...
                     */
                    av_log(c, AV_LOG_ERROR, "AAC with no global headers is currently not supported.\n");
                    return NULL;
                }
                if (config == NULL) {
                    return NULL;
                }
                av_strlcatf(buff, size, "a=rtpmap:%d MPEG4-GENERIC/%d/%d\r\n"
                                        "a=fmtp:%d profile-level-id=1;"
                                        "mode=AAC-hbr;sizelength=13;indexlength=3;"
                                        "indexdeltalength=3%s\r\n",
                                         payload_type, c->sample_rate, c->channels,
                                         payload_type, config);
            }
            break;
        case CODEC_ID_PCM_S16BE:
            if (payload_type >= RTP_PT_PRIVATE)
                av_strlcatf(buff, size, "a=rtpmap:%d L16/%d/%d\r\n",
                                         payload_type,
                                         c->sample_rate, c->channels);
            break;
        case CODEC_ID_PCM_MULAW:
            if (payload_type >= RTP_PT_PRIVATE)
                av_strlcatf(buff, size, "a=rtpmap:%d PCMU/%d/%d\r\n",
                                         payload_type,
                                         c->sample_rate, c->channels);
            break;
        case CODEC_ID_PCM_ALAW:
            if (payload_type >= RTP_PT_PRIVATE)
                av_strlcatf(buff, size, "a=rtpmap:%d PCMA/%d/%d\r\n",
                                         payload_type,
                                         c->sample_rate, c->channels);
            break;
        case CODEC_ID_AMR_NB:
            av_strlcatf(buff, size, "a=rtpmap:%d AMR/%d/%d\r\n"
                                    "a=fmtp:%d octet-align=1\r\n",
                                     payload_type, c->sample_rate, c->channels,
                                     payload_type);
            break;
        case CODEC_ID_AMR_WB:
            av_strlcatf(buff, size, "a=rtpmap:%d AMR-WB/%d/%d\r\n"
                                    "a=fmtp:%d octet-align=1\r\n",
                                     payload_type, c->sample_rate, c->channels,
                                     payload_type);
            break;
        case CODEC_ID_VORBIS:
            if (c->extradata_size)
                config = xiph_extradata2config(c);
            else
                av_log(c, AV_LOG_ERROR, "Vorbis configuration info missing\n");
            if (!config)
                return NULL;

            av_strlcatf(buff, size, "a=rtpmap:%d vorbis/%d/%d\r\n"
                                    "a=fmtp:%d configuration=%s\r\n",
                                    payload_type, c->sample_rate, c->channels,
                                    payload_type, config);
            break;
        case CODEC_ID_THEORA: {
            const char *pix_fmt;
            if (c->extradata_size)
                config = xiph_extradata2config(c);
            else
                av_log(c, AV_LOG_ERROR, "Theora configuation info missing\n");
            if (!config)
                return NULL;

            switch (c->pix_fmt) {
            case PIX_FMT_YUV420P:
                pix_fmt = "YCbCr-4:2:0";
                break;
            case PIX_FMT_YUV422P:
                pix_fmt = "YCbCr-4:2:2";
                break;
            case PIX_FMT_YUV444P:
                pix_fmt = "YCbCr-4:4:4";
                break;
            default:
                av_log(c, AV_LOG_ERROR, "Unsupported pixel format.\n");
                return NULL;
            }

            av_strlcatf(buff, size, "a=rtpmap:%d theora/90000\r\n"
                                    "a=fmtp:%d delivery-method=inline; "
                                    "width=%d; height=%d; sampling=%s; "
                                    "configuration=%s\r\n",
                                    payload_type, payload_type,
                                    c->width, c->height, pix_fmt, config);
            break;
        }
        case CODEC_ID_VP8:
            av_strlcatf(buff, size, "a=rtpmap:%d VP8/90000\r\n",
                                     payload_type);
            break;
        case CODEC_ID_ADPCM_G722:
            if (payload_type >= RTP_PT_PRIVATE)
                av_strlcatf(buff, size, "a=rtpmap:%d G722/%d/%d\r\n",
                                         payload_type,
                                         8000, c->channels);
            break;
        default:
            /* Nothing special to do here... */
            break;
    }

    av_free(config);

    return buff;
}

void ff_sdp_write_media(char *buff, int size, AVCodecContext *c, const char *dest_addr, const char *dest_type, int port, int ttl, AVFormatContext *fmt)
{
    const char *type;
    int payload_type;

    payload_type = ff_rtp_get_payload_type(c);
    if (payload_type < 0) {
        payload_type = RTP_PT_PRIVATE + (c->codec_type == AVMEDIA_TYPE_AUDIO);
    }

    switch (c->codec_type) {
        case AVMEDIA_TYPE_VIDEO   : type = "video"      ; break;
        case AVMEDIA_TYPE_AUDIO   : type = "audio"      ; break;
        case AVMEDIA_TYPE_SUBTITLE: type = "text"       ; break;
        default                 : type = "application"; break;
    }

    av_strlcatf(buff, size, "m=%s %d RTP/AVP %d\r\n", type, port, payload_type);
    sdp_write_address(buff, size, dest_addr, dest_type, ttl);
    if (c->bit_rate) {
        av_strlcatf(buff, size, "b=AS:%d\r\n", c->bit_rate / 1000);
    }

    sdp_write_media_attributes(buff, size, c, payload_type, fmt);
}

int av_sdp_create(AVFormatContext *ac[], int n_files, char *buf, int size)
{
    AVDictionaryEntry *title = av_dict_get(ac[0]->metadata, "title", NULL, 0);
    struct sdp_session_level s;
    int i, j, port, ttl, is_multicast;
    char dst[32], dst_type[5];

    memset(buf, 0, size);
    memset(&s, 0, sizeof(struct sdp_session_level));
    s.user = "-";
    s.src_addr = "127.0.0.1";    /* FIXME: Properly set this */
    s.src_type = "IP4";
    s.name = title ? title->value : "No Name";

    port = 0;
    ttl = 0;
    if (n_files == 1) {
        port = sdp_get_address(dst, sizeof(dst), &ttl, ac[0]->filename);
        is_multicast = resolve_destination(dst, sizeof(dst), dst_type,
                                           sizeof(dst_type));
        if (!is_multicast)
            ttl = 0;
        if (dst[0]) {
            s.dst_addr = dst;
            s.dst_type = dst_type;
            s.ttl = ttl;
            if (!strcmp(dst_type, "IP6")) {
                s.src_addr = "::1";
                s.src_type = "IP6";
            }
        }
    }
    sdp_write_header(buf, size, &s);

    dst[0] = 0;
    for (i = 0; i < n_files; i++) {
        if (n_files != 1) {
            port = sdp_get_address(dst, sizeof(dst), &ttl, ac[i]->filename);
            is_multicast = resolve_destination(dst, sizeof(dst), dst_type,
                                               sizeof(dst_type));
            if (!is_multicast)
                ttl = 0;
        }
        for (j = 0; j < ac[i]->nb_streams; j++) {
            ff_sdp_write_media(buf, size,
                                  ac[i]->streams[j]->codec, dst[0] ? dst : NULL,
                                  dst_type, (port > 0) ? port + j * 2 : 0, ttl,
                                  ac[i]);
            if (port <= 0) {
                av_strlcatf(buf, size,
                                   "a=control:streamid=%d\r\n", i + j);
            }
        }
    }

    return 0;
}
#else
int av_sdp_create(AVFormatContext *ac[], int n_files, char *buf, int size)
{
    return AVERROR(ENOSYS);
}

void ff_sdp_write_media(char *buff, int size, AVCodecContext *c, const char *dest_addr, const char *dest_type, int port, int ttl, AVFormatContext *fmt)
{
}
#endif

#if FF_API_SDP_CREATE
int avf_sdp_create(AVFormatContext *ac[], int n_files, char *buff, int size)
{
    return av_sdp_create(ac, n_files, buff, size);
}
#endif
