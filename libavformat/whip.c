/*
 * WebRTC-HTTP ingestion protocol (WHIP) muxer
 * Copyright (c) 2023 The FFmpeg Project
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

#include "libavcodec/avcodec.h"
#include "libavcodec/codec_desc.h"
#include "libavcodec/h264.h"
#include "libavcodec/startcode.h"
#include "libavutil/base64.h"
#include "libavutil/bprint.h"
#include "libavutil/crc.h"
#include "libavutil/hmac.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/lfg.h"
#include "libavutil/opt.h"
#include "libavutil/mem.h"
#include "libavutil/random_seed.h"
#include "libavutil/time.h"
#include "avc.h"
#include "nal.h"
#include "avio_internal.h"
#include "http.h"
#include "internal.h"
#include "mux.h"
#include "network.h"
#include "srtp.h"
#include "tls.h"

/**
 * Maximum size limit of a Session Description Protocol (SDP),
 * be it an offer or answer.
 */
#define MAX_SDP_SIZE 8192

/**
 * The size of the Secure Real-time Transport Protocol (SRTP) master key material
 * that is exported by Secure Sockets Layer (SSL) after a successful Datagram
 * Transport Layer Security (DTLS) handshake. This material consists of a key
 * of 16 bytes and a salt of 14 bytes.
 */
#define DTLS_SRTP_KEY_LEN 16
#define DTLS_SRTP_SALT_LEN 14

/**
 * The maximum size of the Secure Real-time Transport Protocol (SRTP) HMAC checksum
 * and padding that is appended to the end of the packet. To calculate the maximum
 * size of the User Datagram Protocol (UDP) packet that can be sent out, subtract
 * this size from the `pkt_size`.
 */
#define DTLS_SRTP_CHECKSUM_LEN 16

/**
 * When sending ICE or DTLS messages, responses are received via UDP. However, the peer
 * may not be ready and return EAGAIN, in which case we should wait for a short duration
 * and retry reading.
 * For instance, if we try to read from UDP and get EAGAIN, we sleep for 5ms and retry.
 * This macro is used to limit the total duration in milliseconds (e.g., 50ms), so we
 * will try at most 5 times.
 * Keep in mind that this macro should have a minimum duration of 5 ms.
 */
#define ICE_DTLS_READ_INTERVAL 50

/* The magic cookie for Session Traversal Utilities for NAT (STUN) messages. */
#define STUN_MAGIC_COOKIE 0x2112A442

/**
 * The DTLS content type.
 * See https://tools.ietf.org/html/rfc2246#section-6.2.1
 * change_cipher_spec(20), alert(21), handshake(22), application_data(23)
 */
#define DTLS_CONTENT_TYPE_CHANGE_CIPHER_SPEC 20

/**
 * The DTLS record layer header has a total size of 13 bytes, consisting of
 * ContentType (1 byte), ProtocolVersion (2 bytes), Epoch (2 bytes),
 * SequenceNumber (6 bytes), and Length (2 bytes).
 * See https://datatracker.ietf.org/doc/html/rfc9147#section-4
 */
#define DTLS_RECORD_LAYER_HEADER_LEN 13

/**
 * The DTLS version number, which is 0xfeff for DTLS 1.0, or 0xfefd for DTLS 1.2.
 * See https://datatracker.ietf.org/doc/html/rfc9147#name-the-dtls-record-layer
 */
#define DTLS_VERSION_10 0xfeff
#define DTLS_VERSION_12 0xfefd

/**
 * Maximum size of the buffer for sending and receiving UDP packets.
 * Please note that this size does not limit the size of the UDP packet that can be sent.
 * To set the limit for packet size, modify the `pkt_size` parameter.
 * For instance, it is possible to set the UDP buffer to 4096 to send or receive packets,
 * but please keep in mind that the `pkt_size` option limits the packet size to 1400.
 */
#define MAX_UDP_BUFFER_SIZE 4096

/* Referring to Chrome's definition of RTP payload types. */
#define WHIP_RTP_PAYLOAD_TYPE_H264 106
#define WHIP_RTP_PAYLOAD_TYPE_OPUS 111

/**
 * The STUN message header, which is 20 bytes long, comprises the
 * STUNMessageType (1B), MessageLength (2B), MagicCookie (4B),
 * and TransactionID (12B).
 * See https://datatracker.ietf.org/doc/html/rfc5389#section-6
 */
#define ICE_STUN_HEADER_SIZE 20

/**
 * The RTP header is 12 bytes long, comprising the Version(1B), PT(1B),
 * SequenceNumber(2B), Timestamp(4B), and SSRC(4B).
 * See https://www.rfc-editor.org/rfc/rfc3550#section-5.1
 */
#define WHIP_RTP_HEADER_SIZE 12

/**
 * For RTCP, PT is [128, 223] (or without marker [0, 95]). Literally, RTCP starts
 * from 64 not 0, so PT is [192, 223] (or without marker [64, 95]), see "RTCP Control
 * Packet Types (PT)" at
 * https://www.iana.org/assignments/rtp-parameters/rtp-parameters.xhtml#rtp-parameters-4
 *
 * For RTP, the PT is [96, 127], or [224, 255] with marker. See "RTP Payload Types (PT)
 * for standard audio and video encodings" at
 * https://www.iana.org/assignments/rtp-parameters/rtp-parameters.xhtml#rtp-parameters-1
 */
#define WHIP_RTCP_PT_START 192
#define WHIP_RTCP_PT_END   223

/**
 * In the case of ICE-LITE, these fields are not used; instead, they are defined
 * as constant values.
 */
#define WHIP_SDP_SESSION_ID "4489045141692799359"
#define WHIP_SDP_CREATOR_IP "127.0.0.1"

/* Calculate the elapsed time from starttime to endtime in milliseconds. */
#define ELAPSED(starttime, endtime) ((int)(endtime - starttime) / 1000)

/* STUN Attribute, comprehension-required range (0x0000-0x7FFF) */
enum STUNAttr {
    STUN_ATTR_USERNAME                  = 0x0006, /// shared secret response/bind request
    STUN_ATTR_USE_CANDIDATE             = 0x0025, /// bind request
    STUN_ATTR_MESSAGE_INTEGRITY         = 0x0008, /// bind request/response
    STUN_ATTR_FINGERPRINT               = 0x8028, /// rfc5389
};

enum WHIPState {
    WHIP_STATE_NONE,

    /* The initial state. */
    WHIP_STATE_INIT,
    /* The muxer has sent the offer to the peer. */
    WHIP_STATE_OFFER,
    /* The muxer has received the answer from the peer. */
    WHIP_STATE_ANSWER,
    /**
     * After parsing the answer received from the peer, the muxer negotiates the abilities
     * in the offer that it generated.
     */
    WHIP_STATE_NEGOTIATED,
    /* The muxer has connected to the peer via UDP. */
    WHIP_STATE_UDP_CONNECTED,
    /* The muxer has sent the ICE request to the peer. */
    WHIP_STATE_ICE_CONNECTING,
    /* The muxer has received the ICE response from the peer. */
    WHIP_STATE_ICE_CONNECTED,
    /* The muxer starts attempting the DTLS handshake. */
    WHIP_STATE_DTLS_CONNECTING,
    /* The muxer has finished the DTLS handshake with the peer. */
    WHIP_STATE_DTLS_FINISHED,
    /* The muxer has finished the SRTP setup. */
    WHIP_STATE_SRTP_FINISHED,
    /* The muxer is ready to send/receive media frames. */
    WHIP_STATE_READY,
    /* The muxer is failed. */
    WHIP_STATE_FAILED,
};

typedef struct WHIPContext {
    AVClass *av_class;

    /* The state of the RTC connection. */
    enum WHIPState state;
    /* The callback return value for DTLS. */
    int dtls_ret;
    int dtls_closed;

    /* Parameters for the input audio and video codecs. */
    AVCodecParameters *audio_par;
    AVCodecParameters *video_par;

    /**
     * The h264_mp4toannexb Bitstream Filter (BSF) bypasses the AnnexB packet;
     * therefore, it is essential to insert the SPS and PPS before each IDR frame
     * in such cases.
     */
    int h264_annexb_insert_sps_pps;

    /* The random number generator. */
    AVLFG rnd;

    /* The ICE username and pwd fragment generated by the muxer. */
    char ice_ufrag_local[9];
    char ice_pwd_local[33];
    /* The SSRC of the audio and video stream, generated by the muxer. */
    uint32_t audio_ssrc;
    uint32_t video_ssrc;
    /* The PT(Payload Type) of stream, generated by the muxer. */
    uint8_t audio_payload_type;
    uint8_t video_payload_type;
    /**
     * This is the SDP offer generated by the muxer based on the codec parameters,
     * DTLS, and ICE information.
     */
    char *sdp_offer;

    /* The ICE username and pwd from remote server. */
    char *ice_ufrag_remote;
    char *ice_pwd_remote;
    /**
     * This represents the ICE candidate protocol, priority, host and port.
     * Currently, we only support one candidate and choose the first UDP candidate.
     * However, we plan to support multiple candidates in the future.
     */
    char *ice_protocol;
    char *ice_host;
    int ice_port;

    /* The SDP answer received from the WebRTC server. */
    char *sdp_answer;
    /* The resource URL returned in the Location header of WHIP HTTP response. */
    char *whip_resource_url;

    /* These variables represent timestamps used for calculating and tracking the cost. */
    int64_t whip_starttime;
    int64_t whip_init_time;
    int64_t whip_offer_time;
    int64_t whip_answer_time;
    int64_t whip_udp_time;
    int64_t whip_ice_time;
    int64_t whip_dtls_time;
    int64_t whip_srtp_time;

    /* The certificate and private key content used for DTLS hanshake */
    char cert_buf[MAX_CERTIFICATE_SIZE];
    char key_buf[MAX_CERTIFICATE_SIZE];
    /* The fingerprint of certificate, used in SDP offer. */
    char *dtls_fingerprint;
    /**
     * This represents the material used to build the SRTP master key. It is
     * generated by DTLS and has the following layout:
     *          16B         16B         14B             14B
     *      client_key | server_key | client_salt | server_salt
     */
    uint8_t dtls_srtp_materials[(DTLS_SRTP_KEY_LEN + DTLS_SRTP_SALT_LEN) * 2];

    char ssl_error_message[256];

    /* TODO: Use AVIOContext instead of URLContext */
    URLContext *dtls_uc;

    /* The SRTP send context, to encrypt outgoing packets. */
    SRTPContext srtp_audio_send;
    SRTPContext srtp_video_send;
    SRTPContext srtp_rtcp_send;
    /* The SRTP receive context, to decrypt incoming packets. */
    SRTPContext srtp_recv;

    /* The UDP transport is used for delivering ICE, DTLS and SRTP packets. */
    URLContext *udp;
    /* The buffer for UDP transmission. */
    char buf[MAX_UDP_BUFFER_SIZE];

    /* The timeout in milliseconds for ICE and DTLS handshake. */
    int handshake_timeout;
    /**
     * The size of RTP packet, should generally be set to MTU.
     * Note that pion requires a smaller value, for example, 1200.
     */
    int pkt_size;
    /**
     * The optional Bearer token for WHIP Authorization.
     * See https://www.ietf.org/archive/id/draft-ietf-wish-whip-08.html#name-authentication-and-authoriz
     */
    char* authorization;
    /* The certificate and private key used for DTLS handshake. */
    char* cert_file;
    char* key_file;
} WHIPContext;

/**
 * Whether the packet is a DTLS packet.
 */
static int is_dtls_packet(uint8_t *b, int size) {
    uint16_t version = AV_RB16(&b[1]);
    return size > DTLS_RECORD_LAYER_HEADER_LEN &&
        b[0] >= DTLS_CONTENT_TYPE_CHANGE_CIPHER_SPEC &&
        (version == DTLS_VERSION_10 || version == DTLS_VERSION_12);
}


/**
 * Get or Generate a self-signed certificate and private key for DTLS,
 * fingerprint for SDP
 */
static av_cold int certificate_key_init(AVFormatContext *s)
{
    int ret = 0;
    WHIPContext *whip = s->priv_data;

    if (whip->cert_file && whip->key_file) {
        /* Read the private key and certificate from the file. */
        if ((ret = ff_ssl_read_key_cert(whip->key_file, whip->cert_file,
                                        whip->key_buf, sizeof(whip->key_buf),
                                        whip->cert_buf, sizeof(whip->cert_buf),
                                        &whip->dtls_fingerprint)) < 0) {
            av_log(s, AV_LOG_ERROR, "DTLS: Failed to read DTLS certificate from cert=%s, key=%s\n",
                whip->cert_file, whip->key_file);
            return ret;
        }
    } else {
        /* Generate a private key to ctx->dtls_pkey and self-signed certificate. */
        if ((ret = ff_ssl_gen_key_cert(whip->key_buf, sizeof(whip->key_buf),
                                       whip->cert_buf, sizeof(whip->cert_buf),
                                       &whip->dtls_fingerprint)) < 0) {
            av_log(s, AV_LOG_ERROR, "DTLS: Failed to generate DTLS private key and certificate\n");
            return ret;
        }
    }

    return ret;
}

/**
 * When DTLS state change.
 */
static int dtls_context_on_state(AVFormatContext *s, const char* type, const char* desc)
{
    int ret = 0;
    WHIPContext *whip = s->priv_data;
    int state = ff_dtls_state(whip->dtls_uc);

    if (state == DTLS_STATE_CLOSED) {
        whip->dtls_closed = 1;
        av_log(whip, AV_LOG_VERBOSE, "WHIP: DTLS session closed, type=%s, desc=%s, elapsed=%dms\n",
            type ? type : "", desc ? desc : "", ELAPSED(whip->whip_starttime, av_gettime()));
        goto error;
    }

    if (state == DTLS_STATE_FAILED) {
        whip->state = WHIP_STATE_FAILED;
        av_log(whip, AV_LOG_ERROR, "WHIP: DTLS session failed, type=%s, desc=%s\n",
            type ? type : "", desc ? desc : "");
        whip->dtls_ret = AVERROR(EIO);
        goto error;
    }

    if (state == DTLS_STATE_FINISHED && whip->state < WHIP_STATE_DTLS_FINISHED) {
        whip->state = WHIP_STATE_DTLS_FINISHED;
        whip->whip_dtls_time = av_gettime();
        av_log(whip, AV_LOG_VERBOSE, "WHIP: DTLS handshake is done, elapsed=%dms\n",
            ELAPSED(whip->whip_starttime, av_gettime()));
        return ret;
    }
error:
    return -1;
}

static av_cold int dtls_initialize(AVFormatContext *s)
{
    WHIPContext *whip = s->priv_data;
    /* reuse the udp created by whip */
    ff_dtls_set_udp(whip->dtls_uc, whip->udp);
    return 0;
}

/**
 * Initialize and check the options for the WebRTC muxer.
 */
static av_cold int initialize(AVFormatContext *s)
{
    int ret, ideal_pkt_size = 532;
    WHIPContext *whip = s->priv_data;
    uint32_t seed;

    whip->whip_starttime = av_gettime();

    ret = certificate_key_init(s);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Failed to init certificate and key\n");
        return ret;
    }

    /* Initialize the random number generator. */
    seed = av_get_random_seed();
    av_lfg_init(&whip->rnd, seed);

    if (whip->pkt_size < ideal_pkt_size)
        av_log(whip, AV_LOG_WARNING, "WHIP: pkt_size=%d(<%d) is too small, may cause packet loss\n",
               whip->pkt_size, ideal_pkt_size);

    if (whip->state < WHIP_STATE_INIT)
        whip->state = WHIP_STATE_INIT;
    whip->whip_init_time = av_gettime();
    av_log(whip, AV_LOG_VERBOSE, "WHIP: Init state=%d, handshake_timeout=%dms, pkt_size=%d, seed=%d, elapsed=%dms\n",
        whip->state, whip->handshake_timeout, whip->pkt_size, seed, ELAPSED(whip->whip_starttime, av_gettime()));

    return 0;
}

/**
 * When duplicating a stream, the demuxer has already set the extradata, profile, and
 * level of the par. Keep in mind that this function will not be invoked since the
 * profile and level are set.
 *
 * When utilizing an encoder, such as libx264, to encode a stream, the extradata in
 * par->extradata contains the SPS, which includes profile and level information.
 * However, the profile and level of par remain unspecified. Therefore, it is necessary
 * to extract the profile and level data from the extradata and assign it to the par's
 * profile and level. Keep in mind that AVFMT_GLOBALHEADER must be enabled; otherwise,
 * the extradata will remain empty.
 */
static int parse_profile_level(AVFormatContext *s, AVCodecParameters *par)
{
    int ret = 0;
    const uint8_t *r = par->extradata, *r1, *end = par->extradata + par->extradata_size;
    H264SPS seq, *const sps = &seq;
    uint32_t state;
    WHIPContext *whip = s->priv_data;

    if (par->codec_id != AV_CODEC_ID_H264)
        return ret;

    if (par->profile != AV_PROFILE_UNKNOWN && par->level != AV_LEVEL_UNKNOWN)
        return ret;

    if (!par->extradata || par->extradata_size <= 0) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Unable to parse profile from empty extradata=%p, size=%d\n",
            par->extradata, par->extradata_size);
        return AVERROR(EINVAL);
    }

    while (1) {
        r = avpriv_find_start_code(r, end, &state);
        if (r >= end)
            break;

        r1 = ff_nal_find_startcode(r, end);
        if ((state & 0x1f) == H264_NAL_SPS) {
            ret = ff_avc_decode_sps(sps, r, r1 - r);
            if (ret < 0) {
                av_log(whip, AV_LOG_ERROR, "WHIP: Failed to decode SPS, state=%x, size=%d\n",
                    state, (int)(r1 - r));
                return ret;
            }

            av_log(whip, AV_LOG_VERBOSE, "WHIP: Parse profile=%d, level=%d from SPS\n",
                sps->profile_idc, sps->level_idc);
            par->profile = sps->profile_idc;
            par->level = sps->level_idc;
        }

        r = r1;
    }

    return ret;
}

/**
 * Parses video SPS/PPS from the extradata of codecpar and checks the codec.
 * Currently only supports video(h264) and audio(opus). Note that only baseline
 * and constrained baseline profiles of h264 are supported.
 *
 * If the profile is less than 0, the function considers the profile as baseline.
 * It may need to parse the profile from SPS/PPS. This situation occurs when ingesting
 * desktop and transcoding.
 *
 * @param s Pointer to the AVFormatContext
 * @returns Returns 0 if successful or AVERROR_xxx in case of an error.
 *
 * TODO: FIXME: There is an issue with the timestamp of OPUS audio, especially when
 *  the input is an MP4 file. The timestamp deviates from the expected value of 960,
 *  causing Chrome to play the audio stream with noise. This problem can be replicated
 *  by transcoding a specific file into MP4 format and publishing it using the WHIP
 *  muxer. However, when directly transcoding and publishing through the WHIP muxer,
 *  the issue is not present, and the audio timestamp remains consistent. The root
 *  cause is still unknown, and this comment has been added to address this issue
 *  in the future. Further research is needed to resolve the problem.
 */
static int parse_codec(AVFormatContext *s)
{
    int i, ret = 0;
    WHIPContext *whip = s->priv_data;

    for (i = 0; i < s->nb_streams; i++) {
        AVCodecParameters *par = s->streams[i]->codecpar;
        const AVCodecDescriptor *desc = avcodec_descriptor_get(par->codec_id);
        switch (par->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            if (whip->video_par) {
                av_log(whip, AV_LOG_ERROR, "WHIP: Only one video stream is supported by RTC\n");
                return AVERROR(EINVAL);
            }
            whip->video_par = par;

            if (par->codec_id != AV_CODEC_ID_H264) {
                av_log(whip, AV_LOG_ERROR, "WHIP: Unsupported video codec %s by RTC, choose h264\n",
                       desc ? desc->name : "unknown");
                return AVERROR_PATCHWELCOME;
            }

            if (par->video_delay > 0) {
                av_log(whip, AV_LOG_ERROR, "WHIP: Unsupported B frames by RTC\n");
                return AVERROR_PATCHWELCOME;
            }

            if ((ret = parse_profile_level(s, par)) < 0) {
                av_log(whip, AV_LOG_ERROR, "WHIP: Failed to parse SPS/PPS from extradata\n");
                return AVERROR(EINVAL);
            }

            if (par->profile == AV_PROFILE_UNKNOWN) {
                av_log(whip, AV_LOG_WARNING, "WHIP: No profile found in extradata, consider baseline\n");
                return AVERROR(EINVAL);
            }
            if (par->level == AV_LEVEL_UNKNOWN) {
                av_log(whip, AV_LOG_WARNING, "WHIP: No level found in extradata, consider 3.1\n");
                return AVERROR(EINVAL);
            }
            break;
        case AVMEDIA_TYPE_AUDIO:
            if (whip->audio_par) {
                av_log(whip, AV_LOG_ERROR, "WHIP: Only one audio stream is supported by RTC\n");
                return AVERROR(EINVAL);
            }
            whip->audio_par = par;

            if (par->codec_id != AV_CODEC_ID_OPUS) {
                av_log(whip, AV_LOG_ERROR, "WHIP: Unsupported audio codec %s by RTC, choose opus\n",
                    desc ? desc->name : "unknown");
                return AVERROR_PATCHWELCOME;
            }

            if (par->ch_layout.nb_channels != 2) {
                av_log(whip, AV_LOG_ERROR, "WHIP: Unsupported audio channels %d by RTC, choose stereo\n",
                    par->ch_layout.nb_channels);
                return AVERROR_PATCHWELCOME;
            }

            if (par->sample_rate != 48000) {
                av_log(whip, AV_LOG_ERROR, "WHIP: Unsupported audio sample rate %d by RTC, choose 48000\n", par->sample_rate);
                return AVERROR_PATCHWELCOME;
            }
            break;
        default:
            av_log(whip, AV_LOG_ERROR, "WHIP: Codec type '%s' for stream %d is not supported by RTC\n",
                   av_get_media_type_string(par->codec_type), i);
            return AVERROR_PATCHWELCOME;
        }
    }

    return ret;
}

/**
 * Generate SDP offer according to the codec parameters, DTLS and ICE information.
 *
 * Note that we don't use av_sdp_create to generate SDP offer because it doesn't
 * support DTLS and ICE information.
 *
 * @return 0 if OK, AVERROR_xxx on error
 */
static int generate_sdp_offer(AVFormatContext *s)
{
    int ret = 0, profile, level, profile_iop;
    const char *acodec_name = NULL, *vcodec_name = NULL;
    AVBPrint bp;
    WHIPContext *whip = s->priv_data;

    /* To prevent a crash during cleanup, always initialize it. */
    av_bprint_init(&bp, 1, MAX_SDP_SIZE);

    if (whip->sdp_offer) {
        av_log(whip, AV_LOG_ERROR, "WHIP: SDP offer is already set\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    snprintf(whip->ice_ufrag_local, sizeof(whip->ice_ufrag_local), "%08x",
        av_lfg_get(&whip->rnd));
    snprintf(whip->ice_pwd_local, sizeof(whip->ice_pwd_local), "%08x%08x%08x%08x",
        av_lfg_get(&whip->rnd), av_lfg_get(&whip->rnd), av_lfg_get(&whip->rnd),
        av_lfg_get(&whip->rnd));

    whip->audio_ssrc = av_lfg_get(&whip->rnd);
    whip->video_ssrc = av_lfg_get(&whip->rnd);

    whip->audio_payload_type = WHIP_RTP_PAYLOAD_TYPE_OPUS;
    whip->video_payload_type = WHIP_RTP_PAYLOAD_TYPE_H264;

    av_bprintf(&bp, ""
        "v=0\r\n"
        "o=FFmpeg %s 2 IN IP4 %s\r\n"
        "s=FFmpegPublishSession\r\n"
        "t=0 0\r\n"
        "a=group:BUNDLE 0 1\r\n"
        "a=extmap-allow-mixed\r\n"
        "a=msid-semantic: WMS\r\n",
        WHIP_SDP_SESSION_ID,
        WHIP_SDP_CREATOR_IP);

    if (whip->audio_par) {
        if (whip->audio_par->codec_id == AV_CODEC_ID_OPUS)
            acodec_name = "opus";

        av_bprintf(&bp, ""
            "m=audio 9 UDP/TLS/RTP/SAVPF %u\r\n"
            "c=IN IP4 0.0.0.0\r\n"
            "a=ice-ufrag:%s\r\n"
            "a=ice-pwd:%s\r\n"
            "a=fingerprint:sha-256 %s\r\n"
            "a=setup:passive\r\n"
            "a=mid:0\r\n"
            "a=sendonly\r\n"
            "a=msid:FFmpeg audio\r\n"
            "a=rtcp-mux\r\n"
            "a=rtpmap:%u %s/%d/%d\r\n"
            "a=ssrc:%u cname:FFmpeg\r\n"
            "a=ssrc:%u msid:FFmpeg audio\r\n",
            whip->audio_payload_type,
            whip->ice_ufrag_local,
            whip->ice_pwd_local,
            whip->dtls_fingerprint,
            whip->audio_payload_type,
            acodec_name,
            whip->audio_par->sample_rate,
            whip->audio_par->ch_layout.nb_channels,
            whip->audio_ssrc,
            whip->audio_ssrc);
    }

    if (whip->video_par) {
        profile_iop = profile = whip->video_par->profile;
        level = whip->video_par->level;
        if (whip->video_par->codec_id == AV_CODEC_ID_H264) {
            vcodec_name = "H264";
            profile_iop &= AV_PROFILE_H264_CONSTRAINED;
            profile &= (~AV_PROFILE_H264_CONSTRAINED);
        }

        av_bprintf(&bp, ""
            "m=video 9 UDP/TLS/RTP/SAVPF %u\r\n"
            "c=IN IP4 0.0.0.0\r\n"
            "a=ice-ufrag:%s\r\n"
            "a=ice-pwd:%s\r\n"
            "a=fingerprint:sha-256 %s\r\n"
            "a=setup:passive\r\n"
            "a=mid:1\r\n"
            "a=sendonly\r\n"
            "a=msid:FFmpeg video\r\n"
            "a=rtcp-mux\r\n"
            "a=rtcp-rsize\r\n"
            "a=rtpmap:%u %s/90000\r\n"
            "a=fmtp:%u level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=%02x%02x%02x\r\n"
            "a=ssrc:%u cname:FFmpeg\r\n"
            "a=ssrc:%u msid:FFmpeg video\r\n",
            whip->video_payload_type,
            whip->ice_ufrag_local,
            whip->ice_pwd_local,
            whip->dtls_fingerprint,
            whip->video_payload_type,
            vcodec_name,
            whip->video_payload_type,
            profile,
            profile_iop,
            level,
            whip->video_ssrc,
            whip->video_ssrc);
    }

    if (!av_bprint_is_complete(&bp)) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Offer exceed max %d, %s\n", MAX_SDP_SIZE, bp.str);
        ret = AVERROR(EIO);
        goto end;
    }

    whip->sdp_offer = av_strdup(bp.str);
    if (!whip->sdp_offer) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if (whip->state < WHIP_STATE_OFFER)
        whip->state = WHIP_STATE_OFFER;
    whip->whip_offer_time = av_gettime();
    av_log(whip, AV_LOG_VERBOSE, "WHIP: Generated state=%d, offer: %s\n", whip->state, whip->sdp_offer);

end:
    av_bprint_finalize(&bp, NULL);
    return ret;
}

/**
 * Exchange SDP offer with WebRTC peer to get the answer.
 *
 * @return 0 if OK, AVERROR_xxx on error
 */
static int exchange_sdp(AVFormatContext *s)
{
    int ret;
    char buf[MAX_URL_SIZE];
    AVBPrint bp;
    WHIPContext *whip = s->priv_data;
    /* The URL context is an HTTP transport layer for the WHIP protocol. */
    URLContext *whip_uc = NULL;
    AVDictionary *opts = NULL;
    char *hex_data = NULL;

    /* To prevent a crash during cleanup, always initialize it. */
    av_bprint_init(&bp, 1, MAX_SDP_SIZE);

    if (!whip->sdp_offer || !strlen(whip->sdp_offer)) {
        av_log(whip, AV_LOG_ERROR, "WHIP: No offer to exchange\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    ret = snprintf(buf, sizeof(buf), "Cache-Control: no-cache\r\nContent-Type: application/sdp\r\n");
    if (whip->authorization)
        ret += snprintf(buf + ret, sizeof(buf) - ret, "Authorization: Bearer %s\r\n", whip->authorization);
    if (ret <= 0 || ret >= sizeof(buf)) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Failed to generate headers, size=%d, %s\n", ret, buf);
        ret = AVERROR(EINVAL);
        goto end;
    }

    av_dict_set(&opts, "headers", buf, 0);
    av_dict_set_int(&opts, "chunked_post", 0, 0);

    hex_data = av_mallocz(2 * strlen(whip->sdp_offer) + 1);
    if (!hex_data) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    ff_data_to_hex(hex_data, whip->sdp_offer, strlen(whip->sdp_offer), 0);
    av_dict_set(&opts, "post_data", hex_data, 0);

    ret = ffurl_open_whitelist(&whip_uc, s->url, AVIO_FLAG_READ_WRITE, &s->interrupt_callback,
        &opts, s->protocol_whitelist, s->protocol_blacklist, NULL);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Failed to request url=%s, offer: %s\n", s->url, whip->sdp_offer);
        goto end;
    }

    if (ff_http_get_new_location(whip_uc)) {
        whip->whip_resource_url = av_strdup(ff_http_get_new_location(whip_uc));
        if (!whip->whip_resource_url) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
    }

    while (1) {
        ret = ffurl_read(whip_uc, buf, sizeof(buf));
        if (ret == AVERROR_EOF) {
            /* Reset the error because we read all response as answer util EOF. */
            ret = 0;
            break;
        }
        if (ret <= 0) {
            av_log(whip, AV_LOG_ERROR, "WHIP: Failed to read response from url=%s, offer is %s, answer is %s\n",
                s->url, whip->sdp_offer, whip->sdp_answer);
            goto end;
        }

        av_bprintf(&bp, "%.*s", ret, buf);
        if (!av_bprint_is_complete(&bp)) {
            av_log(whip, AV_LOG_ERROR, "WHIP: Answer exceed max size %d, %.*s, %s\n", MAX_SDP_SIZE, ret, buf, bp.str);
            ret = AVERROR(EIO);
            goto end;
        }
    }

    if (!av_strstart(bp.str, "v=", NULL)) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Invalid answer: %s\n", bp.str);
        ret = AVERROR(EINVAL);
        goto end;
    }

    whip->sdp_answer = av_strdup(bp.str);
    if (!whip->sdp_answer) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if (whip->state < WHIP_STATE_ANSWER)
        whip->state = WHIP_STATE_ANSWER;
    av_log(whip, AV_LOG_VERBOSE, "WHIP: Got state=%d, answer: %s\n", whip->state, whip->sdp_answer);

end:
    ffurl_closep(&whip_uc);
    av_bprint_finalize(&bp, NULL);
    av_dict_free(&opts);
    av_freep(&hex_data);
    return ret;
}

/**
 * Parses the ICE ufrag, pwd, and candidates from the SDP answer.
 *
 * This function is used to extract the ICE ufrag, pwd, and candidates from the SDP answer.
 * It returns an error if any of these fields is NULL. The function only uses the first
 * candidate if there are multiple candidates. However, support for multiple candidates
 * will be added in the future.
 *
 * @param s Pointer to the AVFormatContext
 * @returns Returns 0 if successful or AVERROR_xxx if an error occurs.
 */
static int parse_answer(AVFormatContext *s)
{
    int ret = 0;
    AVIOContext *pb;
    char line[MAX_URL_SIZE];
    const char *ptr;
    int i;
    WHIPContext *whip = s->priv_data;

    if (!whip->sdp_answer || !strlen(whip->sdp_answer)) {
        av_log(whip, AV_LOG_ERROR, "WHIP: No answer to parse\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    pb = avio_alloc_context(whip->sdp_answer, strlen(whip->sdp_answer), 0, NULL, NULL, NULL, NULL);
    if (!pb)
        return AVERROR(ENOMEM);

    for (i = 0; !avio_feof(pb); i++) {
        ff_get_chomp_line(pb, line, sizeof(line));
        if (av_strstart(line, "a=ice-ufrag:", &ptr) && !whip->ice_ufrag_remote) {
            whip->ice_ufrag_remote = av_strdup(ptr);
            if (!whip->ice_ufrag_remote) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
        } else if (av_strstart(line, "a=ice-pwd:", &ptr) && !whip->ice_pwd_remote) {
            whip->ice_pwd_remote = av_strdup(ptr);
            if (!whip->ice_pwd_remote) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
        } else if (av_strstart(line, "a=candidate:", &ptr) && !whip->ice_protocol) {
            ptr = av_stristr(ptr, "udp");
            if (ptr && av_stristr(ptr, "host")) {
                char protocol[17], host[129];
                int priority, port;
                ret = sscanf(ptr, "%16s %d %128s %d typ host", protocol, &priority, host, &port);
                if (ret != 4) {
                    av_log(whip, AV_LOG_ERROR, "WHIP: Failed %d to parse line %d %s from %s\n",
                        ret, i, line, whip->sdp_answer);
                    ret = AVERROR(EIO);
                    goto end;
                }

                if (av_strcasecmp(protocol, "udp")) {
                    av_log(whip, AV_LOG_ERROR, "WHIP: Protocol %s is not supported by RTC, choose udp, line %d %s of %s\n",
                        protocol, i, line, whip->sdp_answer);
                    ret = AVERROR(EIO);
                    goto end;
                }

                whip->ice_protocol = av_strdup(protocol);
                whip->ice_host = av_strdup(host);
                whip->ice_port = port;
                if (!whip->ice_protocol || !whip->ice_host) {
                    ret = AVERROR(ENOMEM);
                    goto end;
                }
            }
        }
    }

    if (!whip->ice_pwd_remote || !strlen(whip->ice_pwd_remote)) {
        av_log(whip, AV_LOG_ERROR, "WHIP: No remote ice pwd parsed from %s\n", whip->sdp_answer);
        ret = AVERROR(EINVAL);
        goto end;
    }

    if (!whip->ice_ufrag_remote || !strlen(whip->ice_ufrag_remote)) {
        av_log(whip, AV_LOG_ERROR, "WHIP: No remote ice ufrag parsed from %s\n", whip->sdp_answer);
        ret = AVERROR(EINVAL);
        goto end;
    }

    if (!whip->ice_protocol || !whip->ice_host || !whip->ice_port) {
        av_log(whip, AV_LOG_ERROR, "WHIP: No ice candidate parsed from %s\n", whip->sdp_answer);
        ret = AVERROR(EINVAL);
        goto end;
    }

    if (whip->state < WHIP_STATE_NEGOTIATED)
        whip->state = WHIP_STATE_NEGOTIATED;
    whip->whip_answer_time = av_gettime();
    av_log(whip, AV_LOG_VERBOSE, "WHIP: SDP state=%d, offer=%luB, answer=%luB, ufrag=%s, pwd=%luB, transport=%s://%s:%d, elapsed=%dms\n",
        whip->state, strlen(whip->sdp_offer), strlen(whip->sdp_answer), whip->ice_ufrag_remote, strlen(whip->ice_pwd_remote),
        whip->ice_protocol, whip->ice_host, whip->ice_port, ELAPSED(whip->whip_starttime, av_gettime()));

end:
    avio_context_free(&pb);
    return ret;
}

/**
 * Creates and marshals an ICE binding request packet.
 *
 * This function creates and marshals an ICE binding request packet. The function only
 * generates the username attribute and does not include goog-network-info, ice-controlling,
 * use-candidate, and priority. However, some of these attributes may be added in the future.
 *
 * @param s Pointer to the AVFormatContext
 * @param buf Pointer to memory buffer to store the request packet
 * @param buf_size Size of the memory buffer
 * @param request_size Pointer to an integer that receives the size of the request packet
 * @return Returns 0 if successful or AVERROR_xxx if an error occurs.
 */
static int ice_create_request(AVFormatContext *s, uint8_t *buf, int buf_size, int *request_size)
{
    int ret, size, crc32;
    char username[128];
    AVIOContext *pb = NULL;
    AVHMAC *hmac = NULL;
    WHIPContext *whip = s->priv_data;

    pb = avio_alloc_context(buf, buf_size, 1, NULL, NULL, NULL, NULL);
    if (!pb)
        return AVERROR(ENOMEM);

    hmac = av_hmac_alloc(AV_HMAC_SHA1);
    if (!hmac) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* Write 20 bytes header */
    avio_wb16(pb, 0x0001); /* STUN binding request */
    avio_wb16(pb, 0);      /* length */
    avio_wb32(pb, STUN_MAGIC_COOKIE); /* magic cookie */
    avio_wb32(pb, av_lfg_get(&whip->rnd)); /* transaction ID */
    avio_wb32(pb, av_lfg_get(&whip->rnd)); /* transaction ID */
    avio_wb32(pb, av_lfg_get(&whip->rnd)); /* transaction ID */

    /* The username is the concatenation of the two ICE ufrag */
    ret = snprintf(username, sizeof(username), "%s:%s", whip->ice_ufrag_remote, whip->ice_ufrag_local);
    if (ret <= 0 || ret >= sizeof(username)) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Failed to build username %s:%s, max=%lu, ret=%d\n",
            whip->ice_ufrag_remote, whip->ice_ufrag_local, sizeof(username), ret);
        ret = AVERROR(EIO);
        goto end;
    }

    /* Write the username attribute */
    avio_wb16(pb, STUN_ATTR_USERNAME); /* attribute type username */
    avio_wb16(pb, ret); /* size of username */
    avio_write(pb, username, ret); /* bytes of username */
    ffio_fill(pb, 0, (4 - (ret % 4)) % 4); /* padding */

    /* Write the use-candidate attribute */
    avio_wb16(pb, STUN_ATTR_USE_CANDIDATE); /* attribute type use-candidate */
    avio_wb16(pb, 0); /* size of use-candidate */

    /* Build and update message integrity */
    avio_wb16(pb, STUN_ATTR_MESSAGE_INTEGRITY); /* attribute type message integrity */
    avio_wb16(pb, 20); /* size of message integrity */
    ffio_fill(pb, 0, 20); /* fill with zero to directly write and skip it */
    size = avio_tell(pb);
    buf[2] = (size - 20) >> 8;
    buf[3] = (size - 20) & 0xFF;
    av_hmac_init(hmac, whip->ice_pwd_remote, strlen(whip->ice_pwd_remote));
    av_hmac_update(hmac, buf, size - 24);
    av_hmac_final(hmac, buf + size - 20, 20);

    /* Write the fingerprint attribute */
    avio_wb16(pb, STUN_ATTR_FINGERPRINT); /* attribute type fingerprint */
    avio_wb16(pb, 4); /* size of fingerprint */
    ffio_fill(pb, 0, 4); /* fill with zero to directly write and skip it */
    size = avio_tell(pb);
    buf[2] = (size - 20) >> 8;
    buf[3] = (size - 20) & 0xFF;
    /* Refer to the av_hash_alloc("CRC32"), av_hash_init and av_hash_final */
    crc32 = av_crc(av_crc_get_table(AV_CRC_32_IEEE_LE), 0xFFFFFFFF, buf, size - 8) ^ 0xFFFFFFFF;
    avio_skip(pb, -4);
    avio_wb32(pb, crc32 ^ 0x5354554E); /* xor with "STUN" */

    *request_size = size;

end:
    avio_context_free(&pb);
    av_hmac_free(hmac);
    return ret;
}

/**
 * Create an ICE binding response.
 *
 * This function generates an ICE binding response and writes it to the provided
 * buffer. The response is signed using the local password for message integrity.
 *
 * @param s Pointer to the AVFormatContext structure.
 * @param tid Pointer to the transaction ID of the binding request. The tid_size should be 12.
 * @param tid_size The size of the transaction ID, should be 12.
 * @param buf Pointer to the buffer where the response will be written.
 * @param buf_size The size of the buffer provided for the response.
 * @param response_size Pointer to an integer that will store the size of the generated response.
 * @return Returns 0 if successful or AVERROR_xxx if an error occurs.
 */
static int ice_create_response(AVFormatContext *s, char *tid, int tid_size, uint8_t *buf, int buf_size, int *response_size)
{
    int ret = 0, size, crc32;
    AVIOContext *pb = NULL;
    AVHMAC *hmac = NULL;
    WHIPContext *whip = s->priv_data;

    if (tid_size != 12) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Invalid transaction ID size. Expected 12, got %d\n", tid_size);
        return AVERROR(EINVAL);
    }

    pb = avio_alloc_context(buf, buf_size, 1, NULL, NULL, NULL, NULL);
    if (!pb)
        return AVERROR(ENOMEM);

    hmac = av_hmac_alloc(AV_HMAC_SHA1);
    if (!hmac) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* Write 20 bytes header */
    avio_wb16(pb, 0x0101); /* STUN binding response */
    avio_wb16(pb, 0);      /* length */
    avio_wb32(pb, STUN_MAGIC_COOKIE); /* magic cookie */
    avio_write(pb, tid, tid_size); /* transaction ID */

    /* Build and update message integrity */
    avio_wb16(pb, STUN_ATTR_MESSAGE_INTEGRITY); /* attribute type message integrity */
    avio_wb16(pb, 20); /* size of message integrity */
    ffio_fill(pb, 0, 20); /* fill with zero to directly write and skip it */
    size = avio_tell(pb);
    buf[2] = (size - 20) >> 8;
    buf[3] = (size - 20) & 0xFF;
    av_hmac_init(hmac, whip->ice_pwd_local, strlen(whip->ice_pwd_local));
    av_hmac_update(hmac, buf, size - 24);
    av_hmac_final(hmac, buf + size - 20, 20);

    /* Write the fingerprint attribute */
    avio_wb16(pb, STUN_ATTR_FINGERPRINT); /* attribute type fingerprint */
    avio_wb16(pb, 4); /* size of fingerprint */
    ffio_fill(pb, 0, 4); /* fill with zero to directly write and skip it */
    size = avio_tell(pb);
    buf[2] = (size - 20) >> 8;
    buf[3] = (size - 20) & 0xFF;
    /* Refer to the av_hash_alloc("CRC32"), av_hash_init and av_hash_final */
    crc32 = av_crc(av_crc_get_table(AV_CRC_32_IEEE_LE), 0xFFFFFFFF, buf, size - 8) ^ 0xFFFFFFFF;
    avio_skip(pb, -4);
    avio_wb32(pb, crc32 ^ 0x5354554E); /* xor with "STUN" */

    *response_size = size;

end:
    avio_context_free(&pb);
    av_hmac_free(hmac);
    return ret;
}

/**
 * A Binding request has class=0b00 (request) and method=0b000000000001 (Binding)
 * and is encoded into the first 16 bits as 0x0001.
 * See https://datatracker.ietf.org/doc/html/rfc5389#section-6
 */
static int ice_is_binding_request(uint8_t *b, int size)
{
    return size >= ICE_STUN_HEADER_SIZE && AV_RB16(&b[0]) == 0x0001;
}

/**
 * A Binding response has class=0b10 (success response) and method=0b000000000001,
 * and is encoded into the first 16 bits as 0x0101.
 */
static int ice_is_binding_response(uint8_t *b, int size)
{
    return size >= ICE_STUN_HEADER_SIZE && AV_RB16(&b[0]) == 0x0101;
}

/**
 * In RTP packets, the first byte is represented as 0b10xxxxxx, where the initial
 * two bits (0b10) indicate the RTP version,
 * see https://www.rfc-editor.org/rfc/rfc3550#section-5.1
 * The RTCP packet header is similar to RTP,
 * see https://www.rfc-editor.org/rfc/rfc3550#section-6.4.1
 */
static int media_is_rtp_rtcp(const uint8_t *b, int size)
{
    return size >= WHIP_RTP_HEADER_SIZE && (b[0] & 0xC0) == 0x80;
}

/* Whether the packet is RTCP. */
static int media_is_rtcp(const uint8_t *b, int size)
{
    return size >= WHIP_RTP_HEADER_SIZE && b[1] >= WHIP_RTCP_PT_START && b[1] <= WHIP_RTCP_PT_END;
}

/**
 * This function handles incoming binding request messages by responding to them.
 * If the message is not a binding request, it will be ignored.
 */
static int ice_handle_binding_request(AVFormatContext *s, char *buf, int buf_size)
{
    int ret = 0, size;
    char tid[12];
    WHIPContext *whip = s->priv_data;

    /* Ignore if not a binding request. */
    if (!ice_is_binding_request(buf, buf_size))
        return ret;

    if (buf_size < ICE_STUN_HEADER_SIZE) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Invalid STUN message, expected at least %d, got %d\n",
            ICE_STUN_HEADER_SIZE, buf_size);
        return AVERROR(EINVAL);
    }

    /* Parse transaction id from binding request in buf. */
    memcpy(tid, buf + 8, 12);

    /* Build the STUN binding response. */
    ret = ice_create_response(s, tid, sizeof(tid), whip->buf, sizeof(whip->buf), &size);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Failed to create STUN binding response, size=%d\n", size);
        return ret;
    }

    ret = ffurl_write(whip->udp, whip->buf, size);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Failed to send STUN binding response, size=%d\n", size);
        return ret;
    }

    return 0;
}

/**
 * To establish a connection with the UDP server, we utilize ICE-LITE in a Client-Server
 * mode. In this setup, FFmpeg acts as the UDP client, while the peer functions as the
 * UDP server.
 */
static int udp_connect(AVFormatContext *s)
{
    int ret = 0;
    char url[256];
    AVDictionary *opts = NULL;
    WHIPContext *whip = s->priv_data;

    /* Build UDP URL and create the UDP context as transport. */
    ff_url_join(url, sizeof(url), "udp", NULL, whip->ice_host, whip->ice_port, NULL);

    av_dict_set_int(&opts, "connect", 1, 0);
    av_dict_set_int(&opts, "fifo_size", 0, 0);
    /* Set the max packet size to the buffer size. */
    av_dict_set_int(&opts, "pkt_size", whip->pkt_size, 0);

    ret = ffurl_open_whitelist(&whip->udp, url, AVIO_FLAG_WRITE, &s->interrupt_callback,
        &opts, s->protocol_whitelist, s->protocol_blacklist, NULL);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Failed to connect udp://%s:%d\n", whip->ice_host, whip->ice_port);
        goto end;
    }

    /* Make the socket non-blocking, set to READ and WRITE mode after connected */
    ff_socket_nonblock(ffurl_get_file_handle(whip->udp), 1);
    whip->udp->flags |= AVIO_FLAG_READ | AVIO_FLAG_NONBLOCK;

    if (whip->state < WHIP_STATE_UDP_CONNECTED)
        whip->state = WHIP_STATE_UDP_CONNECTED;
    whip->whip_udp_time = av_gettime();
    av_log(whip, AV_LOG_VERBOSE, "WHIP: UDP state=%d, elapsed=%dms, connected to udp://%s:%d\n",
        whip->state, ELAPSED(whip->whip_starttime, av_gettime()), whip->ice_host, whip->ice_port);

end:
    av_dict_free(&opts);
    return ret;
}

static int ice_dtls_handshake(AVFormatContext *s)
{
    int ret = 0, size, i;
    int64_t starttime = av_gettime(), now;
    WHIPContext *whip = s->priv_data;
    AVDictionary *opts = NULL;
    char str[8];
    char buf[256], *cert_buf = NULL, *key_buf = NULL;

    if (whip->state < WHIP_STATE_UDP_CONNECTED || !whip->udp) {
        av_log(whip, AV_LOG_ERROR, "WHIP: UDP not connected, state=%d, udp=%p\n", whip->state, whip->udp);
        return AVERROR(EINVAL);
    }

    while (1) {
        if (whip->state <= WHIP_STATE_ICE_CONNECTING) {
            /* Build the STUN binding request. */
            ret = ice_create_request(s, whip->buf, sizeof(whip->buf), &size);
            if (ret < 0) {
                av_log(whip, AV_LOG_ERROR, "WHIP: Failed to create STUN binding request, size=%d\n", size);
                goto end;
            }

            ret = ffurl_write(whip->udp, whip->buf, size);
            if (ret < 0) {
                av_log(whip, AV_LOG_ERROR, "WHIP: Failed to send STUN binding request, size=%d\n", size);
                goto end;
            }

            if (whip->state < WHIP_STATE_ICE_CONNECTING)
                whip->state = WHIP_STATE_ICE_CONNECTING;
        }

next_packet:
        if (whip->state >= WHIP_STATE_DTLS_FINISHED)
            /* DTLS handshake is done, exit the loop. */
            break;

        now = av_gettime();
        if (now - starttime >= whip->handshake_timeout * 1000) {
            av_log(whip, AV_LOG_ERROR, "WHIP: DTLS handshake timeout=%dms, cost=%dms, elapsed=%dms, state=%d\n",
                whip->handshake_timeout, ELAPSED(starttime, now), ELAPSED(whip->whip_starttime, now), whip->state);
            ret = AVERROR(ETIMEDOUT);
            goto end;
        }

        /* Read the STUN or DTLS messages from peer. */
        for (i = 0; i < ICE_DTLS_READ_INTERVAL / 5 && whip->state < WHIP_STATE_DTLS_CONNECTING; i++) {
            ret = ffurl_read(whip->udp, whip->buf, sizeof(whip->buf));
            if (ret > 0)
                break;
            if (ret == AVERROR(EAGAIN)) {
                av_usleep(5 * 1000);
                continue;
            }
            av_log(whip, AV_LOG_ERROR, "WHIP: Failed to read message\n");
            goto end;
        }

        /* Got nothing, continue to process handshake. */
        if (ret <= 0 && whip->state < WHIP_STATE_DTLS_CONNECTING)
            continue;

        /* Handle the ICE binding response. */
        if (ice_is_binding_response(whip->buf, ret)) {
            if (whip->state < WHIP_STATE_ICE_CONNECTED) {
                whip->state = WHIP_STATE_ICE_CONNECTED;
                whip->whip_ice_time = av_gettime();
                av_log(whip, AV_LOG_VERBOSE, "WHIP: ICE STUN ok, state=%d, url=udp://%s:%d, location=%s, username=%s:%s, res=%dB, elapsed=%dms\n",
                    whip->state, whip->ice_host, whip->ice_port, whip->whip_resource_url ? whip->whip_resource_url : "",
                    whip->ice_ufrag_remote, whip->ice_ufrag_local, ret, ELAPSED(whip->whip_starttime, av_gettime()));

                ff_url_join(buf, sizeof(buf), "dtls", NULL, whip->ice_host, whip->ice_port, NULL);
                snprintf(str, sizeof(str), "%d", whip->pkt_size);
                av_dict_set(&opts, "mtu", str, 0);
                if (whip->cert_file) {
                    av_dict_set(&opts, "cert_file", whip->cert_file, 0);
                } else
                    av_dict_set(&opts, "cert_buf", whip->cert_buf, 0);

                if (whip->key_file) {
                    av_dict_set(&opts, "key_file", whip->key_file, 0);
                } else
                    av_dict_set(&opts, "key_buf", whip->key_buf, 0);

                av_dict_set(&opts, "fingerprint", whip->dtls_fingerprint, 0);
                av_dict_set(&opts, "use_external_udp", "1", 0);
                av_dict_set(&opts, "listen", "1", 0);
                /* If got the first binding response, start DTLS handshake. */
                ret = ffurl_open_whitelist(&whip->dtls_uc, buf, AVIO_FLAG_READ_WRITE, &s->interrupt_callback,
                    &opts, s->protocol_whitelist, s->protocol_blacklist, NULL);
                if (ret < 0)
                    goto end;
                dtls_initialize(s);
            }
            goto next_packet;
        }

        /* When a binding request is received, it is necessary to respond immediately. */
        if (ice_is_binding_request(whip->buf, ret)) {
            if ((ret = ice_handle_binding_request(s, whip->buf, ret)) < 0)
                goto end;
            goto next_packet;
        }

        /* If got any DTLS messages, handle it. */
        if (is_dtls_packet(whip->buf, ret) && whip->state >= WHIP_STATE_ICE_CONNECTED || whip->state == WHIP_STATE_DTLS_CONNECTING) {
            whip->state = WHIP_STATE_DTLS_CONNECTING;
            if ((ret = ffurl_handshake(whip->dtls_uc)) < 0)
                goto end;
            dtls_context_on_state(s, NULL, NULL);
            goto next_packet;
        }
    }

end:
    if (cert_buf)
        av_free(cert_buf);
    if (key_buf)
        av_free(key_buf);
    return ret;
}

/**
 * Establish the SRTP context using the keying material exported from DTLS.
 *
 * Create separate SRTP contexts for sending video and audio, as their sequences differ
 * and should not share a single context. Generate a single SRTP context for receiving
 * RTCP only.
 *
 * @return 0 if OK, AVERROR_xxx on error
 */
static int setup_srtp(AVFormatContext *s)
{
    int ret;
    char recv_key[DTLS_SRTP_KEY_LEN + DTLS_SRTP_SALT_LEN];
    char send_key[DTLS_SRTP_KEY_LEN + DTLS_SRTP_SALT_LEN];
    char buf[AV_BASE64_SIZE(DTLS_SRTP_KEY_LEN + DTLS_SRTP_SALT_LEN)];
    /**
     * The profile for OpenSSL's SRTP is SRTP_AES128_CM_SHA1_80, see ssl/d1_srtp.c.
     * The profile for FFmpeg's SRTP is SRTP_AES128_CM_HMAC_SHA1_80, see libavformat/srtp.c.
     */
    const char* suite = "SRTP_AES128_CM_HMAC_SHA1_80";
    WHIPContext *whip = s->priv_data;
    ret = ff_dtls_export_materials(whip->dtls_uc, whip->dtls_srtp_materials, sizeof(whip->dtls_srtp_materials));
    if (ret < 0)
        goto end;
    /**
     * This represents the material used to build the SRTP master key. It is
     * generated by DTLS and has the following layout:
     *          16B         16B         14B             14B
     *      client_key | server_key | client_salt | server_salt
     */
    char *client_key = whip->dtls_srtp_materials;
    char *server_key = whip->dtls_srtp_materials + DTLS_SRTP_KEY_LEN;
    char *client_salt = server_key + DTLS_SRTP_KEY_LEN;
    char *server_salt = client_salt + DTLS_SRTP_SALT_LEN;

    /* As DTLS server, the recv key is client master key plus salt. */
    memcpy(recv_key, client_key, DTLS_SRTP_KEY_LEN);
    memcpy(recv_key + DTLS_SRTP_KEY_LEN, client_salt, DTLS_SRTP_SALT_LEN);

    /* As DTLS server, the send key is server master key plus salt. */
    memcpy(send_key, server_key, DTLS_SRTP_KEY_LEN);
    memcpy(send_key + DTLS_SRTP_KEY_LEN, server_salt, DTLS_SRTP_SALT_LEN);

    /* Setup SRTP context for outgoing packets */
    if (!av_base64_encode(buf, sizeof(buf), send_key, sizeof(send_key))) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Failed to encode send key\n");
        ret = AVERROR(EIO);
        goto end;
    }

    ret = ff_srtp_set_crypto(&whip->srtp_audio_send, suite, buf);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Failed to set crypto for audio send\n");
        goto end;
    }

    ret = ff_srtp_set_crypto(&whip->srtp_video_send, suite, buf);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Failed to set crypto for video send\n");
        goto end;
    }

    ret = ff_srtp_set_crypto(&whip->srtp_rtcp_send, suite, buf);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "Failed to set crypto for rtcp send\n");
        goto end;
    }

    /* Setup SRTP context for incoming packets */
    if (!av_base64_encode(buf, sizeof(buf), recv_key, sizeof(recv_key))) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Failed to encode recv key\n");
        ret = AVERROR(EIO);
        goto end;
    }

    ret = ff_srtp_set_crypto(&whip->srtp_recv, suite, buf);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Failed to set crypto for recv\n");
        goto end;
    }

    if (whip->state < WHIP_STATE_SRTP_FINISHED)
        whip->state = WHIP_STATE_SRTP_FINISHED;
    whip->whip_srtp_time = av_gettime();
    av_log(whip, AV_LOG_VERBOSE, "WHIP: SRTP setup done, state=%d, suite=%s, key=%luB, elapsed=%dms\n",
        whip->state, suite, sizeof(send_key), ELAPSED(whip->whip_starttime, av_gettime()));

end:
    return ret;
}

/**
 * Callback triggered by the RTP muxer when it creates and sends out an RTP packet.
 *
 * This function modifies the video STAP packet, removing the markers, and updating the
 * NRI of the first NALU. Additionally, it uses the corresponding SRTP context to encrypt
 * the RTP packet, where the video packet is handled by the video SRTP context.
 */
static int on_rtp_write_packet(void *opaque, const uint8_t *buf, int buf_size)
{
    int ret, cipher_size, is_rtcp, is_video;
    uint8_t payload_type;
    AVFormatContext *s = opaque;
    WHIPContext *whip = s->priv_data;
    SRTPContext *srtp;

    /* Ignore if not RTP or RTCP packet. */
    if (!media_is_rtp_rtcp(buf, buf_size))
        return 0;

    /* Only support audio, video and rtcp. */
    is_rtcp = media_is_rtcp(buf, buf_size);
    payload_type = buf[1] & 0x7f;
    is_video = payload_type == whip->video_payload_type;
    if (!is_rtcp && payload_type != whip->video_payload_type && payload_type != whip->audio_payload_type)
        return 0;

    /* Get the corresponding SRTP context. */
    srtp = is_rtcp ? &whip->srtp_rtcp_send : (is_video? &whip->srtp_video_send : &whip->srtp_audio_send);

    /* Encrypt by SRTP and send out. */
    cipher_size = ff_srtp_encrypt(srtp, buf, buf_size, whip->buf, sizeof(whip->buf));
    if (cipher_size <= 0 || cipher_size < buf_size) {
        av_log(whip, AV_LOG_WARNING, "WHIP: Failed to encrypt packet=%dB, cipher=%dB\n", buf_size, cipher_size);
        return 0;
    }

    ret = ffurl_write(whip->udp, whip->buf, cipher_size);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Failed to write packet=%dB, ret=%d\n", cipher_size, ret);
        return ret;
    }

    return ret;
}

/**
 * Creates dedicated RTP muxers for each stream in the AVFormatContext to build RTP
 * packets from the encoded frames.
 *
 * The corresponding SRTP context is utilized to encrypt each stream's RTP packets. For
 * example, a video SRTP context is used for the video stream. Additionally, the
 * "on_rtp_write_packet" callback function is set as the write function for each RTP
 * muxer to send out encrypted RTP packets.
 *
 * @return 0 if OK, AVERROR_xxx on error
 */
static int create_rtp_muxer(AVFormatContext *s)
{
    int ret, i, is_video, buffer_size, max_packet_size;
    AVFormatContext *rtp_ctx = NULL;
    AVDictionary *opts = NULL;
    uint8_t *buffer = NULL;
    char buf[64];
    WHIPContext *whip = s->priv_data;

    const AVOutputFormat *rtp_format = av_guess_format("rtp", NULL, NULL);
    if (!rtp_format) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Failed to guess rtp muxer\n");
        ret = AVERROR(ENOSYS);
        goto end;
    }

    /* The UDP buffer size, may greater than MTU. */
    buffer_size = MAX_UDP_BUFFER_SIZE;
    /* The RTP payload max size. Reserved some bytes for SRTP checksum and padding. */
    max_packet_size = whip->pkt_size - DTLS_SRTP_CHECKSUM_LEN;

    for (i = 0; i < s->nb_streams; i++) {
        rtp_ctx = avformat_alloc_context();
        if (!rtp_ctx) {
            ret = AVERROR(ENOMEM);
            goto end;
        }

        rtp_ctx->oformat = rtp_format;
        if (!avformat_new_stream(rtp_ctx, NULL)) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
        /* Pass the interrupt callback on */
        rtp_ctx->interrupt_callback = s->interrupt_callback;
        /* Copy the max delay setting; the rtp muxer reads this. */
        rtp_ctx->max_delay = s->max_delay;
        /* Copy other stream parameters. */
        rtp_ctx->streams[0]->sample_aspect_ratio = s->streams[i]->sample_aspect_ratio;
        rtp_ctx->flags |= s->flags & AVFMT_FLAG_BITEXACT;
        rtp_ctx->strict_std_compliance = s->strict_std_compliance;

        /* Set the synchronized start time. */
        rtp_ctx->start_time_realtime = s->start_time_realtime;

        avcodec_parameters_copy(rtp_ctx->streams[0]->codecpar, s->streams[i]->codecpar);
        rtp_ctx->streams[0]->time_base = s->streams[i]->time_base;

        /**
         * For H.264, consistently utilize the annexb format through the Bitstream Filter (BSF);
         * therefore, we deactivate the extradata detection for the RTP muxer.
         */
        if (s->streams[i]->codecpar->codec_id == AV_CODEC_ID_H264) {
            av_freep(&rtp_ctx->streams[i]->codecpar->extradata);
            rtp_ctx->streams[i]->codecpar->extradata_size = 0;
        }

        buffer = av_malloc(buffer_size);
        if (!buffer) {
            ret = AVERROR(ENOMEM);
            goto end;
        }

        rtp_ctx->pb = avio_alloc_context(buffer, buffer_size, 1, s, NULL, on_rtp_write_packet, NULL);
        if (!rtp_ctx->pb) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
        rtp_ctx->pb->max_packet_size = max_packet_size;
        rtp_ctx->pb->av_class = &ff_avio_class;

        is_video = s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
        snprintf(buf, sizeof(buf), "%d", is_video? whip->video_payload_type : whip->audio_payload_type);
        av_dict_set(&opts, "payload_type", buf, 0);
        snprintf(buf, sizeof(buf), "%d", is_video? whip->video_ssrc : whip->audio_ssrc);
        av_dict_set(&opts, "ssrc", buf, 0);

        ret = avformat_write_header(rtp_ctx, &opts);
        if (ret < 0) {
            av_log(whip, AV_LOG_ERROR, "WHIP: Failed to write rtp header\n");
            goto end;
        }

        ff_format_set_url(rtp_ctx, av_strdup(s->url));
        s->streams[i]->time_base = rtp_ctx->streams[0]->time_base;
        s->streams[i]->priv_data = rtp_ctx;
        rtp_ctx = NULL;
    }

    if (whip->state < WHIP_STATE_READY)
        whip->state = WHIP_STATE_READY;
    av_log(whip, AV_LOG_INFO, "WHIP: Muxer state=%d, buffer_size=%d, max_packet_size=%d, "
                           "elapsed=%dms(init:%d,offer:%d,answer:%d,udp:%d,ice:%d,dtls:%d,srtp:%d)\n",
        whip->state, buffer_size, max_packet_size, ELAPSED(whip->whip_starttime, av_gettime()),
        ELAPSED(whip->whip_starttime,   whip->whip_init_time),
        ELAPSED(whip->whip_init_time,   whip->whip_offer_time),
        ELAPSED(whip->whip_offer_time,  whip->whip_answer_time),
        ELAPSED(whip->whip_answer_time, whip->whip_udp_time),
        ELAPSED(whip->whip_udp_time,    whip->whip_ice_time),
        ELAPSED(whip->whip_ice_time,    whip->whip_dtls_time),
        ELAPSED(whip->whip_dtls_time,   whip->whip_srtp_time));

end:
    if (rtp_ctx)
        avio_context_free(&rtp_ctx->pb);
    avformat_free_context(rtp_ctx);
    av_dict_free(&opts);
    return ret;
}

/**
 * RTC is connectionless, for it's based on UDP, so it check whether sesison is
 * timeout. In such case, publishers can't republish the stream util the session
 * is timeout.
 * This function is called to notify the server that the stream is ended, server
 * should expire and close the session immediately, so that publishers can republish
 * the stream quickly.
 */
static int dispose_session(AVFormatContext *s)
{
    int ret;
    char buf[MAX_URL_SIZE];
    URLContext *whip_uc = NULL;
    AVDictionary *opts = NULL;
    WHIPContext *whip = s->priv_data;

    if (!whip->whip_resource_url)
        return 0;

    ret = snprintf(buf, sizeof(buf), "Cache-Control: no-cache\r\n");
    if (whip->authorization)
        ret += snprintf(buf + ret, sizeof(buf) - ret, "Authorization: Bearer %s\r\n", whip->authorization);
    if (ret <= 0 || ret >= sizeof(buf)) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Failed to generate headers, size=%d, %s\n", ret, buf);
        ret = AVERROR(EINVAL);
        goto end;
    }

    av_dict_set(&opts, "headers", buf, 0);
    av_dict_set_int(&opts, "chunked_post", 0, 0);
    av_dict_set(&opts, "method", "DELETE", 0);
    ret = ffurl_open_whitelist(&whip_uc, whip->whip_resource_url, AVIO_FLAG_READ_WRITE, &s->interrupt_callback,
        &opts, s->protocol_whitelist, s->protocol_blacklist, NULL);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Failed to DELETE url=%s\n", whip->whip_resource_url);
        goto end;
    }

    while (1) {
        ret = ffurl_read(whip_uc, buf, sizeof(buf));
        if (ret == AVERROR_EOF) {
            ret = 0;
            break;
        }
        if (ret < 0) {
            av_log(whip, AV_LOG_ERROR, "WHIP: Failed to read response from DELETE url=%s\n", whip->whip_resource_url);
            goto end;
        }
    }

    av_log(whip, AV_LOG_INFO, "WHIP: Dispose resource %s ok\n", whip->whip_resource_url);

end:
    ffurl_closep(&whip_uc);
    av_dict_free(&opts);
    return ret;
}

/**
 * Since the h264_mp4toannexb filter only processes the MP4 ISOM format and bypasses
 * the annexb format, it is necessary to manually insert encoder metadata before each
 * IDR when dealing with annexb format packets. For instance, in the case of H.264,
 * we must insert SPS and PPS before the IDR frame.
 */
static int h264_annexb_insert_sps_pps(AVFormatContext *s, AVPacket *pkt)
{
    int ret = 0;
    AVPacket *in = NULL;
    AVCodecParameters *par = s->streams[pkt->stream_index]->codecpar;
    uint32_t nal_size = 0, out_size = par ? par->extradata_size : 0;
    uint8_t unit_type, sps_seen = 0, pps_seen = 0, idr_seen = 0, *out;
    const uint8_t *buf, *buf_end, *r1;

    if (!pkt || !pkt->data || pkt->size <= 0)
        return ret;
    if (!par || !par->extradata || par->extradata_size <= 0)
        return ret;

    /* Discover NALU type from packet. */
    buf_end  = pkt->data + pkt->size;
    for (buf = ff_nal_find_startcode(pkt->data, buf_end); buf < buf_end; buf += nal_size) {
        while (!*(buf++));
        r1 = ff_nal_find_startcode(buf, buf_end);
        if ((nal_size = r1 - buf) > 0) {
            unit_type = *buf & 0x1f;
            if (unit_type == H264_NAL_SPS) {
                sps_seen = 1;
            } else if (unit_type == H264_NAL_PPS) {
                pps_seen = 1;
            } else if (unit_type == H264_NAL_IDR_SLICE) {
                idr_seen = 1;
            }

            out_size += 3 + nal_size;
        }
    }

    if (!idr_seen || (sps_seen && pps_seen))
        return ret;

    /* See av_bsf_send_packet */
    in = av_packet_alloc();
    if (!in)
        return AVERROR(ENOMEM);

    ret = av_packet_make_refcounted(pkt);
    if (ret < 0)
        goto fail;

    av_packet_move_ref(in, pkt);

    /* Create a new packet with sps/pps inserted. */
    ret = av_new_packet(pkt, out_size);
    if (ret < 0)
        goto fail;

    ret = av_packet_copy_props(pkt, in);
    if (ret < 0)
        goto fail;

    memcpy(pkt->data, par->extradata, par->extradata_size);
    out = pkt->data + par->extradata_size;
    buf_end  = in->data + in->size;
    for (buf = ff_nal_find_startcode(in->data, buf_end); buf < buf_end; buf += nal_size) {
        while (!*(buf++));
        r1 = ff_nal_find_startcode(buf, buf_end);
        if ((nal_size = r1 - buf) > 0) {
            AV_WB24(out, 0x00001);
            memcpy(out + 3, buf, nal_size);
            out += 3 + nal_size;
        }
    }

fail:
    if (ret < 0)
        av_packet_unref(pkt);
    av_packet_free(&in);

    return ret;
}

static av_cold int whip_init(AVFormatContext *s)
{
    int ret;
    WHIPContext *whip = s->priv_data;

    if ((ret = initialize(s)) < 0)
        goto end;

    if ((ret = parse_codec(s)) < 0)
        goto end;

    if ((ret = generate_sdp_offer(s)) < 0)
        goto end;

    if ((ret = exchange_sdp(s)) < 0)
        goto end;

    if ((ret = parse_answer(s)) < 0)
        goto end;

    if ((ret = udp_connect(s)) < 0)
        goto end;

    if ((ret = ice_dtls_handshake(s)) < 0)
        goto end;

    if ((ret = setup_srtp(s)) < 0)
        goto end;

    if ((ret = create_rtp_muxer(s)) < 0)
        goto end;

end:
    if (ret < 0 && whip->state < WHIP_STATE_FAILED)
        whip->state = WHIP_STATE_FAILED;
    if (ret >= 0 && whip->state >= WHIP_STATE_FAILED && whip->dtls_ret < 0)
        ret = whip->dtls_ret;
    return ret;
}

static int whip_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;
    WHIPContext *whip = s->priv_data;
    AVStream *st = s->streams[pkt->stream_index];
    AVFormatContext *rtp_ctx = st->priv_data;

    /* TODO: Send binding request every 1s as WebRTC heartbeat. */

    /**
     * Receive packets from the server such as ICE binding requests, DTLS messages,
     * and RTCP like PLI requests, then respond to them.
     */
    ret = ffurl_read(whip->udp, whip->buf, sizeof(whip->buf));
    if (ret > 0) {
        if (is_dtls_packet(whip->buf, ret)) {
            if ((ret = ffurl_write(whip->dtls_uc, whip->buf, ret)) < 0) {
                av_log(whip, AV_LOG_ERROR, "WHIP: Failed to handle DTLS message\n");
                goto end;
            }
        }
    } else if (ret != AVERROR(EAGAIN)) {
        av_log(whip, AV_LOG_ERROR, "WHIP: Failed to read from UDP socket\n");
        goto end;
    }

    if (whip->h264_annexb_insert_sps_pps && st->codecpar->codec_id == AV_CODEC_ID_H264) {
        if ((ret = h264_annexb_insert_sps_pps(s, pkt)) < 0) {
            av_log(whip, AV_LOG_ERROR, "WHIP: Failed to insert SPS/PPS before IDR\n");
            goto end;
        }
    }

    ret = ff_write_chained(rtp_ctx, 0, pkt, s, 0);
    if (ret < 0) {
        if (ret == AVERROR(EINVAL)) {
            av_log(whip, AV_LOG_WARNING, "WHIP: Ignore failed to write packet=%dB, ret=%d\n", pkt->size, ret);
            ret = 0;
        } else
            av_log(whip, AV_LOG_ERROR, "WHIP: Failed to write packet, size=%d\n", pkt->size);
        goto end;
    }

end:
    if (ret < 0 && whip->state < WHIP_STATE_FAILED)
        whip->state = WHIP_STATE_FAILED;
    if (ret >= 0 && whip->state >= WHIP_STATE_FAILED && whip->dtls_ret < 0)
        ret = whip->dtls_ret;
    if (ret >= 0 && whip->dtls_closed)
        ret = AVERROR(EIO);
    return ret;
}

static av_cold void whip_deinit(AVFormatContext *s)
{
    int i, ret;
    WHIPContext *whip = s->priv_data;

    ret = dispose_session(s);
    if (ret < 0)
        av_log(whip, AV_LOG_WARNING, "WHIP: Failed to dispose resource, ret=%d\n", ret);

    for (i = 0; i < s->nb_streams; i++) {
        AVFormatContext* rtp_ctx = s->streams[i]->priv_data;
        if (!rtp_ctx)
            continue;

        av_write_trailer(rtp_ctx);
        /**
         * Keep in mind that it is necessary to free the buffer of pb since we allocate
         * it and pass it to pb using avio_alloc_context, while avio_context_free does
         * not perform this action.
         */
        av_freep(&rtp_ctx->pb->buffer);
        avio_context_free(&rtp_ctx->pb);
        avformat_free_context(rtp_ctx);
        s->streams[i]->priv_data = NULL;
    }

    av_freep(&whip->sdp_offer);
    av_freep(&whip->sdp_answer);
    av_freep(&whip->whip_resource_url);
    av_freep(&whip->ice_ufrag_remote);
    av_freep(&whip->ice_pwd_remote);
    av_freep(&whip->ice_protocol);
    av_freep(&whip->ice_host);
    av_freep(&whip->authorization);
    av_freep(&whip->cert_file);
    av_freep(&whip->key_file);
    ffurl_closep(&whip->udp);
    ff_srtp_free(&whip->srtp_audio_send);
    ff_srtp_free(&whip->srtp_video_send);
    ff_srtp_free(&whip->srtp_rtcp_send);
    ff_srtp_free(&whip->srtp_recv);
    ffurl_close(whip->dtls_uc);
}

static int whip_check_bitstream(AVFormatContext *s, AVStream *st, const AVPacket *pkt)
{
    int ret = 1, extradata_isom = 0;
    uint8_t *b = pkt->data;
    WHIPContext *whip = s->priv_data;

    if (st->codecpar->codec_id == AV_CODEC_ID_H264) {
        extradata_isom = st->codecpar->extradata_size > 0 && st->codecpar->extradata[0] == 1;
        if (pkt->size >= 5 && AV_RB32(b) != 0x0000001 && (AV_RB24(b) != 0x000001 || extradata_isom)) {
            ret = ff_stream_add_bitstream_filter(st, "h264_mp4toannexb", NULL);
            av_log(whip, AV_LOG_VERBOSE, "WHIP: Enable BSF h264_mp4toannexb, packet=[%x %x %x %x %x ...], extradata_isom=%d\n",
                b[0], b[1], b[2], b[3], b[4], extradata_isom);
        } else
            whip->h264_annexb_insert_sps_pps = 1;
    }

    return ret;
}

#define OFFSET(x) offsetof(WHIPContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "handshake_timeout",  "Timeout in milliseconds for ICE and DTLS handshake.",      OFFSET(handshake_timeout),  AV_OPT_TYPE_INT,    { .i64 = 5000 },    -1, INT_MAX, ENC },
    { "pkt_size",           "The maximum size, in bytes, of RTP packets that send out", OFFSET(pkt_size),           AV_OPT_TYPE_INT,    { .i64 = 1200 },    -1, INT_MAX, ENC },
    { "authorization",      "The optional Bearer token for WHIP Authorization",         OFFSET(authorization),      AV_OPT_TYPE_STRING, { .str = NULL },     0,       0, ENC },
    { "cert_file",          "The optional certificate file path for DTLS",              OFFSET(cert_file),          AV_OPT_TYPE_STRING, { .str = NULL },     0,       0, ENC },
    { "key_file",           "The optional private key file path for DTLS",              OFFSET(key_file),      AV_OPT_TYPE_STRING, { .str = NULL },     0,       0, ENC },
    { NULL },
};

static const AVClass whip_muxer_class = {
    .class_name = "WHIP muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFOutputFormat ff_whip_muxer = {
    .p.name             = "whip",
    .p.long_name        = NULL_IF_CONFIG_SMALL("WHIP(WebRTC-HTTP ingestion protocol) muxer"),
    .p.audio_codec      = AV_CODEC_ID_OPUS,
    .p.video_codec      = AV_CODEC_ID_H264,
    .p.flags            = AVFMT_GLOBALHEADER | AVFMT_NOFILE | AVFMT_EXPERIMENTAL,
    .p.priv_class       = &whip_muxer_class,
    .priv_data_size     = sizeof(WHIPContext),
    .init               = whip_init,
    .write_packet       = whip_write_packet,
    .deinit             = whip_deinit,
    .check_bitstream    = whip_check_bitstream,
};
