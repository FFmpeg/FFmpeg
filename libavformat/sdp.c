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
#include "hevc.h"
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
    struct addrinfo hints = { 0 }, *ai;
    int is_multicast;

    av_strlcpy(type, "IP4", type_size);
    if (!dest_addr[0])
        return 0;

    /* Resolve the destination, since it must be written
     * as a numeric IP address in the SDP. */

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

    if (strcmp(proto, "rtp") && strcmp(proto, "srtp")) {
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
static char *extradata2psets(AVFormatContext *s, AVCodecParameters *par)
{
    char *psets, *p;
    const uint8_t *r;
    static const char pset_string[] = "; sprop-parameter-sets=";
    static const char profile_string[] = "; profile-level-id=";
    uint8_t *extradata = par->extradata;
    int extradata_size = par->extradata_size;
    uint8_t *tmpbuf = NULL;
    const uint8_t *sps = NULL, *sps_end;

    if (par->extradata_size > MAX_EXTRADATA_SIZE) {
        av_log(s, AV_LOG_ERROR, "Too much extradata!\n");

        return NULL;
    }
    if (par->extradata[0] == 1) {
        if (ff_avc_write_annexb_extradata(par->extradata, &extradata,
                                          &extradata_size))
            return NULL;
        tmpbuf = extradata;
    }

    psets = av_mallocz(MAX_PSET_SIZE);
    if (!psets) {
        av_log(s, AV_LOG_ERROR, "Cannot allocate memory for the parameter sets.\n");
        av_free(tmpbuf);
        return NULL;
    }
    memcpy(psets, pset_string, strlen(pset_string));
    p = psets + strlen(pset_string);
    r = ff_avc_find_startcode(extradata, extradata + extradata_size);
    while (r < extradata + extradata_size) {
        const uint8_t *r1;
        uint8_t nal_type;

        while (!*(r++));
        nal_type = *r & 0x1f;
        r1 = ff_avc_find_startcode(r, extradata + extradata_size);
        if (nal_type != 7 && nal_type != 8) { /* Only output SPS and PPS */
            r = r1;
            continue;
        }
        if (p != (psets + strlen(pset_string))) {
            *p = ',';
            p++;
        }
        if (!sps) {
            sps = r;
            sps_end = r1;
        }
        if (!av_base64_encode(p, MAX_PSET_SIZE - (p - psets), r, r1 - r)) {
            av_log(s, AV_LOG_ERROR, "Cannot Base64-encode %"PTRDIFF_SPECIFIER" %"PTRDIFF_SPECIFIER"!\n", MAX_PSET_SIZE - (p - psets), r1 - r);
            av_free(psets);
            av_free(tmpbuf);

            return NULL;
        }
        p += strlen(p);
        r = r1;
    }
    if (sps && sps_end - sps >= 4) {
        memcpy(p, profile_string, strlen(profile_string));
        p += strlen(p);
        ff_data_to_hex(p, sps + 1, 3, 0);
        p[6] = '\0';
    }
    av_free(tmpbuf);

    return psets;
}

static char *extradata2psets_hevc(AVCodecParameters *par)
{
    char *psets;
    uint8_t *extradata = par->extradata;
    int extradata_size = par->extradata_size;
    uint8_t *tmpbuf = NULL;
    int ps_pos[3] = { 0 };
    static const char * const ps_names[3] = { "vps", "sps", "pps" };
    int num_arrays, num_nalus;
    int pos, i, j;

    // Convert to hvcc format. Since we need to group multiple NALUs of
    // the same type, and we might need to convert from one format to the
    // other anyway, we get away with a little less work by using the hvcc
    // format.
    if (par->extradata[0] != 1) {
        AVIOContext *pb;
        if (avio_open_dyn_buf(&pb) < 0)
            return NULL;
        if (ff_isom_write_hvcc(pb, par->extradata, par->extradata_size, 0) < 0) {
            avio_close_dyn_buf(pb, &tmpbuf);
            goto err;
        }
        extradata_size = avio_close_dyn_buf(pb, &extradata);
        tmpbuf = extradata;
    }

    if (extradata_size < 23)
        goto err;

    num_arrays = extradata[22];
    pos = 23;
    for (i = 0; i < num_arrays; i++) {
        int num_nalus, nalu_type;
        if (pos + 3 > extradata_size)
            goto err;
        nalu_type = extradata[pos] & 0x3f;
        // Not including libavcodec/hevc.h to avoid confusion between
        // NAL_* with the same name for both H.264 and HEVC.
        if (nalu_type == 32) // VPS
            ps_pos[0] = pos;
        else if (nalu_type == 33) // SPS
            ps_pos[1] = pos;
        else if (nalu_type == 34) // PPS
            ps_pos[2] = pos;
        num_nalus = AV_RB16(&extradata[pos + 1]);
        pos += 3;
        for (j = 0; j < num_nalus; j++) {
            int len;
            if (pos + 2 > extradata_size)
                goto err;
            len = AV_RB16(&extradata[pos]);
            pos += 2;
            if (pos + len > extradata_size)
                goto err;
            pos += len;
        }
    }
    if (!ps_pos[0] || !ps_pos[1] || !ps_pos[2])
        goto err;

    psets = av_mallocz(MAX_PSET_SIZE);
    if (!psets)
        goto err;
    psets[0] = '\0';

    for (i = 0; i < 3; i++) {
        pos = ps_pos[i];

        if (i > 0)
            av_strlcat(psets, "; ", MAX_PSET_SIZE);
        av_strlcatf(psets, MAX_PSET_SIZE, "sprop-%s=", ps_names[i]);

        // Skipping boundary checks in the input here; we've already traversed
        // the whole hvcc structure above without issues
        num_nalus = AV_RB16(&extradata[pos + 1]);
        pos += 3;
        for (j = 0; j < num_nalus; j++) {
            int len = AV_RB16(&extradata[pos]);
            int strpos;
            pos += 2;
            if (j > 0)
                av_strlcat(psets, ",", MAX_PSET_SIZE);
            strpos = strlen(psets);
            if (!av_base64_encode(psets + strpos, MAX_PSET_SIZE - strpos,
                                  &extradata[pos], len)) {
                av_free(psets);
                goto err;
            }
            pos += len;
        }
    }
    av_free(tmpbuf);

    return psets;

err:
    av_free(tmpbuf);
    return NULL;
}

static char *extradata2config(AVFormatContext *s, AVCodecParameters *par)
{
    char *config;

    if (par->extradata_size > MAX_EXTRADATA_SIZE) {
        av_log(s, AV_LOG_ERROR, "Too much extradata!\n");

        return NULL;
    }
    config = av_malloc(10 + par->extradata_size * 2);
    if (!config) {
        av_log(s, AV_LOG_ERROR, "Cannot allocate memory for the config info.\n");
        return NULL;
    }
    memcpy(config, "; config=", 9);
    ff_data_to_hex(config + 9, par->extradata, par->extradata_size, 0);
    config[9 + par->extradata_size * 2] = 0;

    return config;
}

static char *xiph_extradata2config(AVFormatContext *s, AVCodecParameters *par)
{
    char *config, *encoded_config;
    const uint8_t *header_start[3];
    int headers_len, header_len[3], config_len;
    int first_header_size;

    switch (par->codec_id) {
    case AV_CODEC_ID_THEORA:
        first_header_size = 42;
        break;
    case AV_CODEC_ID_VORBIS:
        first_header_size = 30;
        break;
    default:
        av_log(s, AV_LOG_ERROR, "Unsupported Xiph codec ID\n");
        return NULL;
    }

    if (avpriv_split_xiph_headers(par->extradata, par->extradata_size,
                              first_header_size, header_start,
                              header_len) < 0) {
        av_log(s, AV_LOG_ERROR, "Extradata corrupt.\n");
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
    av_log(s, AV_LOG_ERROR,
           "Not enough memory for configuration string\n");
    return NULL;
}

static int latm_context2profilelevel(AVCodecParameters *par)
{
    /* MP4A-LATM
     * The RTP payload format specification is described in RFC 3016
     * The encoding specifications are provided in ISO/IEC 14496-3 */

    int profile_level = 0x2B;

    /* TODO: AAC Profile only supports AAC LC Object Type.
     * Different Object Types should implement different Profile Levels */

    if (par->sample_rate <= 24000) {
        if (par->channels <= 2)
            profile_level = 0x28; // AAC Profile, Level 1
    } else if (par->sample_rate <= 48000) {
        if (par->channels <= 2) {
            profile_level = 0x29; // AAC Profile, Level 2
        } else if (par->channels <= 5) {
            profile_level = 0x2A; // AAC Profile, Level 4
        }
    } else if (par->sample_rate <= 96000) {
        if (par->channels <= 5) {
            profile_level = 0x2B; // AAC Profile, Level 5
        }
    }

    return profile_level;
}

static char *latm_context2config(AVFormatContext *s, AVCodecParameters *par)
{
    /* MP4A-LATM
     * The RTP payload format specification is described in RFC 3016
     * The encoding specifications are provided in ISO/IEC 14496-3 */

    uint8_t config_byte[6];
    int rate_index;
    char *config;

    for (rate_index = 0; rate_index < 16; rate_index++)
        if (avpriv_mpeg4audio_sample_rates[rate_index] == par->sample_rate)
            break;
    if (rate_index == 16) {
        av_log(s, AV_LOG_ERROR, "Unsupported sample rate\n");
        return NULL;
    }

    config_byte[0] = 0x40;
    config_byte[1] = 0;
    config_byte[2] = 0x20 | rate_index;
    config_byte[3] = par->channels << 4;
    config_byte[4] = 0x3f;
    config_byte[5] = 0xc0;

    config = av_malloc(6*2+1);
    if (!config) {
        av_log(s, AV_LOG_ERROR, "Cannot allocate memory for the config info.\n");
        return NULL;
    }
    ff_data_to_hex(config, config_byte, 6, 1);
    config[12] = 0;

    return config;
}

static char *sdp_write_media_attributes(char *buff, int size, AVStream *st, int payload_type, AVFormatContext *fmt)
{
    char *config = NULL;
    AVCodecParameters *p = st->codecpar;

    switch (p->codec_id) {
        case AV_CODEC_ID_DIRAC:
            av_strlcatf(buff, size, "a=rtpmap:%d VC2/90000\r\n", payload_type);
            break;
        case AV_CODEC_ID_H264: {
            int mode = 1;
            if (fmt && fmt->oformat && fmt->oformat->priv_class &&
                av_opt_flag_is_set(fmt->priv_data, "rtpflags", "h264_mode0"))
                mode = 0;
            if (p->extradata_size) {
                config = extradata2psets(fmt, p);
            }
            av_strlcatf(buff, size, "a=rtpmap:%d H264/90000\r\n"
                                    "a=fmtp:%d packetization-mode=%d%s\r\n",
                                     payload_type,
                                     payload_type, mode, config ? config : "");
            break;
        }
        case AV_CODEC_ID_H261:
        {
            const char *pic_fmt = NULL;
            /* only QCIF and CIF are specified as supported in RFC 4587 */
            if (p->width == 176 && p->height == 144)
                pic_fmt = "QCIF=1";
            else if (p->width == 352 && p->height == 288)
                pic_fmt = "CIF=1";
            if (payload_type >= RTP_PT_PRIVATE)
                av_strlcatf(buff, size, "a=rtpmap:%d H261/90000\r\n", payload_type);
            if (pic_fmt)
                av_strlcatf(buff, size, "a=fmtp:%d %s\r\n", payload_type, pic_fmt);
            break;
        }
        case AV_CODEC_ID_H263:
        case AV_CODEC_ID_H263P:
            /* a=framesize is required by 3GPP TS 26.234 (PSS). It
             * actually specifies the maximum video size, but we only know
             * the current size. This is required for playback on Android
             * stagefright and on Samsung bada. */
            if (!fmt || !fmt->oformat->priv_class ||
                !av_opt_flag_is_set(fmt->priv_data, "rtpflags", "rfc2190") ||
                p->codec_id == AV_CODEC_ID_H263P)
            av_strlcatf(buff, size, "a=rtpmap:%d H263-2000/90000\r\n"
                                    "a=framesize:%d %d-%d\r\n",
                                    payload_type,
                                    payload_type, p->width, p->height);
            break;
        case AV_CODEC_ID_HEVC:
            if (p->extradata_size)
                config = extradata2psets_hevc(p);
            av_strlcatf(buff, size, "a=rtpmap:%d H265/90000\r\n", payload_type);
            if (config)
                av_strlcatf(buff, size, "a=fmtp:%d %s\r\n",
                                         payload_type, config);
            break;
        case AV_CODEC_ID_MPEG4:
            if (p->extradata_size) {
                config = extradata2config(fmt, p);
            }
            av_strlcatf(buff, size, "a=rtpmap:%d MP4V-ES/90000\r\n"
                                    "a=fmtp:%d profile-level-id=1%s\r\n",
                                     payload_type,
                                     payload_type, config ? config : "");
            break;
        case AV_CODEC_ID_AAC:
            if (fmt && fmt->oformat && fmt->oformat->priv_class &&
                av_opt_flag_is_set(fmt->priv_data, "rtpflags", "latm")) {
                config = latm_context2config(fmt, p);
                if (!config)
                    return NULL;
                av_strlcatf(buff, size, "a=rtpmap:%d MP4A-LATM/%d/%d\r\n"
                                        "a=fmtp:%d profile-level-id=%d;cpresent=0;config=%s\r\n",
                                         payload_type, p->sample_rate, p->channels,
                                         payload_type, latm_context2profilelevel(p), config);
            } else {
                if (p->extradata_size) {
                    config = extradata2config(fmt, p);
                } else {
                    /* FIXME: maybe we can forge config information based on the
                     *        codec parameters...
                     */
                    av_log(fmt, AV_LOG_ERROR, "AAC with no global headers is currently not supported.\n");
                    return NULL;
                }
                if (!config) {
                    return NULL;
                }
                av_strlcatf(buff, size, "a=rtpmap:%d MPEG4-GENERIC/%d/%d\r\n"
                                        "a=fmtp:%d profile-level-id=1;"
                                        "mode=AAC-hbr;sizelength=13;indexlength=3;"
                                        "indexdeltalength=3%s\r\n",
                                         payload_type, p->sample_rate, p->channels,
                                         payload_type, config);
            }
            break;
        case AV_CODEC_ID_PCM_S16BE:
            if (payload_type >= RTP_PT_PRIVATE)
                av_strlcatf(buff, size, "a=rtpmap:%d L16/%d/%d\r\n",
                                         payload_type,
                                         p->sample_rate, p->channels);
            break;
        case AV_CODEC_ID_PCM_MULAW:
            if (payload_type >= RTP_PT_PRIVATE)
                av_strlcatf(buff, size, "a=rtpmap:%d PCMU/%d/%d\r\n",
                                         payload_type,
                                         p->sample_rate, p->channels);
            break;
        case AV_CODEC_ID_PCM_ALAW:
            if (payload_type >= RTP_PT_PRIVATE)
                av_strlcatf(buff, size, "a=rtpmap:%d PCMA/%d/%d\r\n",
                                         payload_type,
                                         p->sample_rate, p->channels);
            break;
        case AV_CODEC_ID_AMR_NB:
            av_strlcatf(buff, size, "a=rtpmap:%d AMR/%d/%d\r\n"
                                    "a=fmtp:%d octet-align=1\r\n",
                                     payload_type, p->sample_rate, p->channels,
                                     payload_type);
            break;
        case AV_CODEC_ID_AMR_WB:
            av_strlcatf(buff, size, "a=rtpmap:%d AMR-WB/%d/%d\r\n"
                                    "a=fmtp:%d octet-align=1\r\n",
                                     payload_type, p->sample_rate, p->channels,
                                     payload_type);
            break;
        case AV_CODEC_ID_VORBIS:
            if (p->extradata_size)
                config = xiph_extradata2config(fmt, p);
            else
                av_log(fmt, AV_LOG_ERROR, "Vorbis configuration info missing\n");
            if (!config)
                return NULL;

            av_strlcatf(buff, size, "a=rtpmap:%d vorbis/%d/%d\r\n"
                                    "a=fmtp:%d configuration=%s\r\n",
                                    payload_type, p->sample_rate, p->channels,
                                    payload_type, config);
            break;
        case AV_CODEC_ID_THEORA: {
            const char *pix_fmt;
            switch (p->format) {
            case AV_PIX_FMT_YUV420P:
                pix_fmt = "YCbCr-4:2:0";
                break;
            case AV_PIX_FMT_YUV422P:
                pix_fmt = "YCbCr-4:2:2";
                break;
            case AV_PIX_FMT_YUV444P:
                pix_fmt = "YCbCr-4:4:4";
                break;
            default:
                av_log(fmt, AV_LOG_ERROR, "Unsupported pixel format.\n");
                return NULL;
            }

            if (p->extradata_size)
                config = xiph_extradata2config(fmt, p);
            else
                av_log(fmt, AV_LOG_ERROR, "Theora configuration info missing\n");
            if (!config)
                return NULL;

            av_strlcatf(buff, size, "a=rtpmap:%d theora/90000\r\n"
                                    "a=fmtp:%d delivery-method=inline; "
                                    "width=%d; height=%d; sampling=%s; "
                                    "configuration=%s\r\n",
                                    payload_type, payload_type,
                                    p->width, p->height, pix_fmt, config);
            break;
        }
        case AV_CODEC_ID_VP8:
            av_strlcatf(buff, size, "a=rtpmap:%d VP8/90000\r\n",
                                     payload_type);
            break;
        case AV_CODEC_ID_VP9:
            av_strlcatf(buff, size, "a=rtpmap:%d VP9/90000\r\n",
                                     payload_type);
            break;
        case AV_CODEC_ID_MJPEG:
            if (payload_type >= RTP_PT_PRIVATE)
                av_strlcatf(buff, size, "a=rtpmap:%d JPEG/90000\r\n",
                                         payload_type);
            break;
        case AV_CODEC_ID_ADPCM_G722:
            if (payload_type >= RTP_PT_PRIVATE)
                av_strlcatf(buff, size, "a=rtpmap:%d G722/%d/%d\r\n",
                                         payload_type,
                                         8000, p->channels);
            break;
        case AV_CODEC_ID_ADPCM_G726: {
            if (payload_type >= RTP_PT_PRIVATE)
                av_strlcatf(buff, size, "a=rtpmap:%d G726-%d/%d\r\n",
                                         payload_type,
                                         p->bits_per_coded_sample*8,
                                         p->sample_rate);
            break;
        }
        case AV_CODEC_ID_ILBC:
            av_strlcatf(buff, size, "a=rtpmap:%d iLBC/%d\r\n"
                                    "a=fmtp:%d mode=%d\r\n",
                                     payload_type, p->sample_rate,
                                     payload_type, p->block_align == 38 ? 20 : 30);
            break;
        case AV_CODEC_ID_SPEEX:
            av_strlcatf(buff, size, "a=rtpmap:%d speex/%d\r\n",
                                     payload_type, p->sample_rate);
            if (st->codec) {
                const char *mode;
                uint64_t vad_option;

                if (st->codec->flags & AV_CODEC_FLAG_QSCALE)
                      mode = "on";
                else if (!av_opt_get_int(st->codec, "vad", AV_OPT_FLAG_ENCODING_PARAM, &vad_option) && vad_option)
                      mode = "vad";
                else
                      mode = "off";

                av_strlcatf(buff, size, "a=fmtp:%d vbr=%s\r\n",
                                        payload_type, mode);
            }
            break;
        case AV_CODEC_ID_OPUS:
            /* The opus RTP draft says that all opus streams MUST be declared
               as stereo, to avoid negotiation failures. The actual number of
               channels can change on a packet-by-packet basis. The number of
               channels a receiver prefers to receive or a sender plans to send
               can be declared via fmtp parameters (both default to mono), but
               receivers MUST be able to receive and process stereo packets. */
            av_strlcatf(buff, size, "a=rtpmap:%d opus/48000/2\r\n",
                                     payload_type);
            if (p->channels == 2) {
                av_strlcatf(buff, size, "a=fmtp:%d sprop-stereo=1\r\n",
                                         payload_type);
            }
            break;
        default:
            /* Nothing special to do here... */
            break;
    }

    av_free(config);

    return buff;
}

void ff_sdp_write_media(char *buff, int size, AVStream *st, int idx,
                        const char *dest_addr, const char *dest_type,
                        int port, int ttl, AVFormatContext *fmt)
{
    AVCodecParameters *p = st->codecpar;
    const char *type;
    int payload_type;

    payload_type = ff_rtp_get_payload_type(fmt, st->codecpar, idx);

    switch (p->codec_type) {
        case AVMEDIA_TYPE_VIDEO   : type = "video"      ; break;
        case AVMEDIA_TYPE_AUDIO   : type = "audio"      ; break;
        case AVMEDIA_TYPE_SUBTITLE: type = "text"       ; break;
        default                 : type = "application"; break;
    }

    av_strlcatf(buff, size, "m=%s %d RTP/AVP %d\r\n", type, port, payload_type);
    sdp_write_address(buff, size, dest_addr, dest_type, ttl);
    if (p->bit_rate) {
        av_strlcatf(buff, size, "b=AS:%"PRId64"\r\n", (int64_t)p->bit_rate / 1000);
    }

    sdp_write_media_attributes(buff, size, st, payload_type, fmt);
}

int av_sdp_create(AVFormatContext *ac[], int n_files, char *buf, int size)
{
    AVDictionaryEntry *title = av_dict_get(ac[0]->metadata, "title", NULL, 0);
    struct sdp_session_level s = { 0 };
    int i, j, port, ttl, is_multicast, index = 0;
    char dst[32], dst_type[5];

    memset(buf, 0, size);
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
            ff_sdp_write_media(buf, size, ac[i]->streams[j], index++,
                               dst[0] ? dst : NULL, dst_type,
                               (port > 0) ? port + j * 2 : 0,
                               ttl, ac[i]);
            if (port <= 0) {
                av_strlcatf(buf, size,
                                   "a=control:streamid=%d\r\n", i + j);
            }
            if (ac[i]->pb && ac[i]->pb->av_class) {
                uint8_t *crypto_suite = NULL, *crypto_params = NULL;
                av_opt_get(ac[i]->pb, "srtp_out_suite",  AV_OPT_SEARCH_CHILDREN,
                           &crypto_suite);
                av_opt_get(ac[i]->pb, "srtp_out_params", AV_OPT_SEARCH_CHILDREN,
                           &crypto_params);
                if (crypto_suite && crypto_suite[0])
                    av_strlcatf(buf, size,
                                "a=crypto:1 %s inline:%s\r\n",
                                crypto_suite, crypto_params);
                av_free(crypto_suite);
                av_free(crypto_params);
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

void ff_sdp_write_media(char *buff, int size, AVStream *st, int idx,
                        const char *dest_addr, const char *dest_type,
                        int port, int ttl, AVFormatContext *fmt)
{
}
#endif
