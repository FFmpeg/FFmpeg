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

#include "libavutil/avstring.h"
#include "libavutil/base64.h"
#include "avformat.h"
#include "internal.h"
#include "avc.h"
#include "rtp.h"

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
    const char *dst_addr; /**< destination IP address (can be multicast) */
    const char *name;     /**< session name (can be an empty string) */
};

static void sdp_write_address(char *buff, int size, const char *dest_addr, int ttl)
{
    if (dest_addr) {
        if (ttl > 0) {
            av_strlcatf(buff, size, "c=IN IP4 %s/%d\r\n", dest_addr, ttl);
        } else {
            av_strlcatf(buff, size, "c=IN IP4 %s\r\n", dest_addr);
        }
    }
}

static void sdp_write_header(char *buff, int size, struct sdp_session_level *s)
{
    av_strlcatf(buff, size, "v=%d\r\n"
                            "o=- %d %d IN IP4 %s\r\n"
                            "t=%d %d\r\n"
                            "s=%s\r\n"
                            "a=tool:libavformat " AV_STRINGIFY(LIBAVFORMAT_VERSION) "\r\n",
                            s->sdp_version,
                            s->id, s->version, s->src_addr,
                            s->start_time, s->end_time,
                            s->name);
    sdp_write_address(buff, size, s->dst_addr, s->ttl);
}

static int sdp_get_address(char *dest_addr, int size, int *ttl, const char *url)
{
    int port;
    const char *p;

    url_split(NULL, 0, NULL, 0, dest_addr, size, &port, NULL, 0, url);

    *ttl = 0;
    p = strchr(url, '?');
    if (p) {
        char buff[64];
        int is_multicast = find_info_tag(buff, sizeof(buff), "multicast", p);

        if (is_multicast) {
            if (find_info_tag(buff, sizeof(buff), "ttl", p)) {
                *ttl = strtol(buff, NULL, 10);
            } else {
                *ttl = 5;
            }
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
    ff_data_to_hex(config + 9, c->extradata, c->extradata_size);
    config[9 + c->extradata_size * 2] = 0;

    return config;
}

static char *sdp_write_media_attributes(char *buff, int size, AVCodecContext *c, int payload_type)
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
            av_strlcatf(buff, size, "a=rtpmap:%d H263-2000/90000\r\n", payload_type);
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
            break;
        case CODEC_ID_PCM_S16BE:
            if (payload_type >= 96)
                av_strlcatf(buff, size, "a=rtpmap:%d L16/%d/%d\r\n",
                                         payload_type,
                                         c->sample_rate, c->channels);
            break;
        case CODEC_ID_PCM_MULAW:
            if (payload_type >= 96)
                av_strlcatf(buff, size, "a=rtpmap:%d PCMU/%d/%d\r\n",
                                         payload_type,
                                         c->sample_rate, c->channels);
            break;
        case CODEC_ID_PCM_ALAW:
            if (payload_type >= 96)
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
        default:
            /* Nothing special to do here... */
            break;
    }

    av_free(config);

    return buff;
}

static void sdp_write_media(char *buff, int size, AVCodecContext *c, const char *dest_addr, int port, int ttl)
{
    const char *type;
    int payload_type;

    payload_type = ff_rtp_get_payload_type(c);
    if (payload_type < 0) {
        payload_type = 96;  /* FIXME: how to assign a private pt? rtp.c is broken too */
    }

    switch (c->codec_type) {
        case CODEC_TYPE_VIDEO   : type = "video"      ; break;
        case CODEC_TYPE_AUDIO   : type = "audio"      ; break;
        case CODEC_TYPE_SUBTITLE: type = "text"       ; break;
        default                 : type = "application"; break;
    }

    av_strlcatf(buff, size, "m=%s %d RTP/AVP %d\r\n", type, port, payload_type);
    sdp_write_address(buff, size, dest_addr, ttl);
    if (c->bit_rate) {
        av_strlcatf(buff, size, "b=AS:%d\r\n", c->bit_rate / 1000);
    }

    sdp_write_media_attributes(buff, size, c, payload_type);
}

int avf_sdp_create(AVFormatContext *ac[], int n_files, char *buff, int size)
{
    AVMetadataTag *title = av_metadata_get(ac[0]->metadata, "title", NULL, 0);
    struct sdp_session_level s;
    int i, j, port, ttl;
    char dst[32];

    memset(buff, 0, size);
    memset(&s, 0, sizeof(struct sdp_session_level));
    s.user = "-";
    s.src_addr = "127.0.0.1";    /* FIXME: Properly set this */
    s.name = title ? title->value : "No Name";

    port = 0;
    ttl = 0;
    if (n_files == 1) {
        port = sdp_get_address(dst, sizeof(dst), &ttl, ac[0]->filename);
        if (port > 0) {
            s.dst_addr = dst;
            s.ttl = ttl;
        }
    }
    sdp_write_header(buff, size, &s);

    dst[0] = 0;
    for (i = 0; i < n_files; i++) {
        if (n_files != 1) {
            port = sdp_get_address(dst, sizeof(dst), &ttl, ac[i]->filename);
        }
        for (j = 0; j < ac[i]->nb_streams; j++) {
            sdp_write_media(buff, size,
                                  ac[i]->streams[j]->codec, dst[0] ? dst : NULL,
                                  (port > 0) ? port + j * 2 : 0, ttl);
            if (port <= 0) {
                av_strlcatf(buff, size,
                                   "a=control:streamid=%d\r\n", i + j);
            }
        }
    }

    return 0;
}
#else
int avf_sdp_create(AVFormatContext *ac[], int n_files, char *buff, int size)
{
    return AVERROR(ENOSYS);
}
#endif
