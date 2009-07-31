/*
 * RTMP network protocol
 * Copyright (c) 2009 Kostya Shishkov
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
 * @file libavformat/rtmpproto.c
 * RTMP protocol
 */

#include "libavcodec/bytestream.h"
#include "libavutil/avstring.h"
#include "libavutil/lfg.h"
#include "libavutil/sha.h"
#include "avformat.h"

#include "network.h"

#include "flv.h"
#include "rtmp.h"
#include "rtmppkt.h"

/* we can't use av_log() with URLContext yet... */
#if LIBAVFORMAT_VERSION_MAJOR < 53
#define LOG_CONTEXT NULL
#else
#define LOG_CONTEXT s
#endif

/** RTMP protocol handler state */
typedef enum {
    STATE_START,      ///< client has not done anything yet
    STATE_HANDSHAKED, ///< client has performed handshake
    STATE_CONNECTING, ///< client connected to server successfully
    STATE_READY,      ///< client has sent all needed commands and waits for server reply
    STATE_PLAYING,    ///< client has started receiving multimedia data from server
} ClientState;

/** protocol handler context */
typedef struct RTMPContext {
    URLContext*   stream;                     ///< TCP stream used in interactions with RTMP server
    RTMPPacket    prev_pkt[2][RTMP_CHANNELS]; ///< packet history used when reading and sending packets
    int           chunk_size;                 ///< size of the chunks RTMP packets are divided into
    char          playpath[256];              ///< path to filename to play (with possible "mp4:" prefix)
    ClientState   state;                      ///< current state
    int           main_channel_id;            ///< an additional channel ID which is used for some invocations
    uint8_t*      flv_data;                   ///< buffer with data for demuxer
    int           flv_size;                   ///< current buffer size
    int           flv_off;                    ///< number of bytes read from current buffer
    uint32_t      video_ts;                   ///< current video timestamp in milliseconds
    uint32_t      audio_ts;                   ///< current audio timestamp in milliseconds
} RTMPContext;

#define PLAYER_KEY_OPEN_PART_LEN 30   ///< length of partial key used for first client digest signing
/** Client key used for digest signing */
static const uint8_t rtmp_player_key[] = {
    'G', 'e', 'n', 'u', 'i', 'n', 'e', ' ', 'A', 'd', 'o', 'b', 'e', ' ',
    'F', 'l', 'a', 's', 'h', ' ', 'P', 'l', 'a', 'y', 'e', 'r', ' ', '0', '0', '1',

    0xF0, 0xEE, 0xC2, 0x4A, 0x80, 0x68, 0xBE, 0xE8, 0x2E, 0x00, 0xD0, 0xD1, 0x02,
    0x9E, 0x7E, 0x57, 0x6E, 0xEC, 0x5D, 0x2D, 0x29, 0x80, 0x6F, 0xAB, 0x93, 0xB8,
    0xE6, 0x36, 0xCF, 0xEB, 0x31, 0xAE
};

#define SERVER_KEY_OPEN_PART_LEN 36   ///< length of partial key used for first server digest signing
/** Key used for RTMP server digest signing */
static const uint8_t rtmp_server_key[] = {
    'G', 'e', 'n', 'u', 'i', 'n', 'e', ' ', 'A', 'd', 'o', 'b', 'e', ' ',
    'F', 'l', 'a', 's', 'h', ' ', 'M', 'e', 'd', 'i', 'a', ' ',
    'S', 'e', 'r', 'v', 'e', 'r', ' ', '0', '0', '1',

    0xF0, 0xEE, 0xC2, 0x4A, 0x80, 0x68, 0xBE, 0xE8, 0x2E, 0x00, 0xD0, 0xD1, 0x02,
    0x9E, 0x7E, 0x57, 0x6E, 0xEC, 0x5D, 0x2D, 0x29, 0x80, 0x6F, 0xAB, 0x93, 0xB8,
    0xE6, 0x36, 0xCF, 0xEB, 0x31, 0xAE
};

/**
 * Generates 'connect' call and sends it to the server.
 */
static void gen_connect(URLContext *s, RTMPContext *rt, const char *proto,
                        const char *host, int port, const char *app)
{
    RTMPPacket pkt;
    uint8_t ver[32], *p;
    char tcurl[512];

    ff_rtmp_packet_create(&pkt, RTMP_VIDEO_CHANNEL, RTMP_PT_INVOKE, 0, 4096);
    p = pkt.data;

    snprintf(tcurl, sizeof(tcurl), "%s://%s:%d/%s", proto, host, port, app);
    ff_amf_write_string(&p, "connect");
    ff_amf_write_number(&p, 1.0);
    ff_amf_write_object_start(&p);
    ff_amf_write_field_name(&p, "app");
    ff_amf_write_string(&p, app);

    snprintf(ver, sizeof(ver), "%s %d,%d,%d,%d", RTMP_CLIENT_PLATFORM, RTMP_CLIENT_VER1,
             RTMP_CLIENT_VER2, RTMP_CLIENT_VER3, RTMP_CLIENT_VER4);
    ff_amf_write_field_name(&p, "flashVer");
    ff_amf_write_string(&p, ver);
    ff_amf_write_field_name(&p, "tcUrl");
    ff_amf_write_string(&p, tcurl);
    ff_amf_write_field_name(&p, "fpad");
    ff_amf_write_bool(&p, 0);
    ff_amf_write_field_name(&p, "capabilities");
    ff_amf_write_number(&p, 15.0);
    ff_amf_write_field_name(&p, "audioCodecs");
    ff_amf_write_number(&p, 1639.0);
    ff_amf_write_field_name(&p, "videoCodecs");
    ff_amf_write_number(&p, 252.0);
    ff_amf_write_field_name(&p, "videoFunction");
    ff_amf_write_number(&p, 1.0);
    ff_amf_write_object_end(&p);

    pkt.data_size = p - pkt.data;

    ff_rtmp_packet_write(rt->stream, &pkt, rt->chunk_size, rt->prev_pkt[1]);
}

/**
 * Generates 'createStream' call and sends it to the server. It should make
 * the server allocate some channel for media streams.
 */
static void gen_create_stream(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt;
    uint8_t *p;

    av_log(LOG_CONTEXT, AV_LOG_DEBUG, "Creating stream...\n");
    ff_rtmp_packet_create(&pkt, RTMP_VIDEO_CHANNEL, RTMP_PT_INVOKE, 0, 25);

    p = pkt.data;
    ff_amf_write_string(&p, "createStream");
    ff_amf_write_number(&p, 3.0);
    ff_amf_write_null(&p);

    ff_rtmp_packet_write(rt->stream, &pkt, rt->chunk_size, rt->prev_pkt[1]);
    ff_rtmp_packet_destroy(&pkt);
}

/**
 * Generates 'play' call and sends it to the server, then pings the server
 * to start actual playing.
 */
static void gen_play(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt;
    uint8_t *p;

    av_log(LOG_CONTEXT, AV_LOG_DEBUG, "Sending play command for '%s'\n", rt->playpath);
    ff_rtmp_packet_create(&pkt, RTMP_VIDEO_CHANNEL, RTMP_PT_INVOKE, 0,
                          29 + strlen(rt->playpath));
    pkt.extra = rt->main_channel_id;

    p = pkt.data;
    ff_amf_write_string(&p, "play");
    ff_amf_write_number(&p, 0.0);
    ff_amf_write_null(&p);
    ff_amf_write_string(&p, rt->playpath);
    ff_amf_write_number(&p, 0.0);

    ff_rtmp_packet_write(rt->stream, &pkt, rt->chunk_size, rt->prev_pkt[1]);
    ff_rtmp_packet_destroy(&pkt);

    // set client buffer time disguised in ping packet
    ff_rtmp_packet_create(&pkt, RTMP_NETWORK_CHANNEL, RTMP_PT_PING, 1, 10);

    p = pkt.data;
    bytestream_put_be16(&p, 3);
    bytestream_put_be32(&p, 1);
    bytestream_put_be32(&p, 256); //TODO: what is a good value here?

    ff_rtmp_packet_write(rt->stream, &pkt, rt->chunk_size, rt->prev_pkt[1]);
    ff_rtmp_packet_destroy(&pkt);
}

/**
 * Generates ping reply and sends it to the server.
 */
static void gen_pong(URLContext *s, RTMPContext *rt, RTMPPacket *ppkt)
{
    RTMPPacket pkt;
    uint8_t *p;

    ff_rtmp_packet_create(&pkt, RTMP_NETWORK_CHANNEL, RTMP_PT_PING, ppkt->timestamp + 1, 6);
    p = pkt.data;
    bytestream_put_be16(&p, 7);
    bytestream_put_be32(&p, AV_RB32(ppkt->data+2) + 1);
    ff_rtmp_packet_write(rt->stream, &pkt, rt->chunk_size, rt->prev_pkt[1]);
    ff_rtmp_packet_destroy(&pkt);
}

//TODO: Move HMAC code somewhere. Eventually.
#define HMAC_IPAD_VAL 0x36
#define HMAC_OPAD_VAL 0x5C

/**
 * Calculates HMAC-SHA2 digest for RTMP handshake packets.
 *
 * @param src    input buffer
 * @param len    input buffer length (should be 1536)
 * @param gap    offset in buffer where 32 bytes should not be taken into account
 *               when calculating digest (since it will be used to store that digest)
 * @param key    digest key
 * @param keylen digest key length
 * @param dst    buffer where calculated digest will be stored (32 bytes)
 */
static void rtmp_calc_digest(const uint8_t *src, int len, int gap,
                             const uint8_t *key, int keylen, uint8_t *dst)
{
    struct AVSHA *sha;
    uint8_t hmac_buf[64+32] = {0};
    int i;

    sha = av_mallocz(av_sha_size);

    if (keylen < 64) {
        memcpy(hmac_buf, key, keylen);
    } else {
        av_sha_init(sha, 256);
        av_sha_update(sha,key, keylen);
        av_sha_final(sha, hmac_buf);
    }
    for (i = 0; i < 64; i++)
        hmac_buf[i] ^= HMAC_IPAD_VAL;

    av_sha_init(sha, 256);
    av_sha_update(sha, hmac_buf, 64);
    if (gap <= 0) {
        av_sha_update(sha, src, len);
    } else { //skip 32 bytes used for storing digest
        av_sha_update(sha, src, gap);
        av_sha_update(sha, src + gap + 32, len - gap - 32);
    }
    av_sha_final(sha, hmac_buf + 64);

    for (i = 0; i < 64; i++)
        hmac_buf[i] ^= HMAC_IPAD_VAL ^ HMAC_OPAD_VAL; //reuse XORed key for opad
    av_sha_init(sha, 256);
    av_sha_update(sha, hmac_buf, 64+32);
    av_sha_final(sha, dst);

    av_free(sha);
}

/**
 * Puts HMAC-SHA2 digest of packet data (except for the bytes where this digest
 * will be stored) into that packet.
 *
 * @param buf handshake data (1536 bytes)
 * @return offset to the digest inside input data
 */
static int rtmp_handshake_imprint_with_digest(uint8_t *buf)
{
    int i, digest_pos = 0;

    for (i = 8; i < 12; i++)
        digest_pos += buf[i];
    digest_pos = (digest_pos % 728) + 12;

    rtmp_calc_digest(buf, RTMP_HANDSHAKE_PACKET_SIZE, digest_pos,
                     rtmp_player_key, PLAYER_KEY_OPEN_PART_LEN,
                     buf + digest_pos);
    return digest_pos;
}

/**
 * Verifies that the received server response has the expected digest value.
 *
 * @param buf handshake data received from the server (1536 bytes)
 * @param off position to search digest offset from
 * @return 0 if digest is valid, digest position otherwise
 */
static int rtmp_validate_digest(uint8_t *buf, int off)
{
    int i, digest_pos = 0;
    uint8_t digest[32];

    for (i = 0; i < 4; i++)
        digest_pos += buf[i + off];
    digest_pos = (digest_pos % 728) + off + 4;

    rtmp_calc_digest(buf, RTMP_HANDSHAKE_PACKET_SIZE, digest_pos,
                     rtmp_server_key, SERVER_KEY_OPEN_PART_LEN,
                     digest);
    if (!memcmp(digest, buf + digest_pos, 32))
        return digest_pos;
    return 0;
}

/**
 * Performs handshake with the server by means of exchanging pseudorandom data
 * signed with HMAC-SHA2 digest.
 *
 * @return 0 if handshake succeeds, negative value otherwise
 */
static int rtmp_handshake(URLContext *s, RTMPContext *rt)
{
    AVLFG rnd;
    uint8_t tosend    [RTMP_HANDSHAKE_PACKET_SIZE+1] = {
        3,                // unencrypted data
        0, 0, 0, 0,       // client uptime
        RTMP_CLIENT_VER1,
        RTMP_CLIENT_VER2,
        RTMP_CLIENT_VER3,
        RTMP_CLIENT_VER4,
    };
    uint8_t clientdata[RTMP_HANDSHAKE_PACKET_SIZE];
    uint8_t serverdata[RTMP_HANDSHAKE_PACKET_SIZE+1];
    int i;
    int server_pos, client_pos;
    uint8_t digest[32];

    av_log(LOG_CONTEXT, AV_LOG_DEBUG, "Handshaking...\n");

    av_lfg_init(&rnd, 0xDEADC0DE);
    // generate handshake packet - 1536 bytes of pseudorandom data
    for (i = 9; i <= RTMP_HANDSHAKE_PACKET_SIZE; i++)
        tosend[i] = av_lfg_get(&rnd) >> 24;
    client_pos = rtmp_handshake_imprint_with_digest(tosend + 1);

    url_write(rt->stream, tosend, RTMP_HANDSHAKE_PACKET_SIZE + 1);
    i = url_read_complete(rt->stream, serverdata, RTMP_HANDSHAKE_PACKET_SIZE + 1);
    if (i != RTMP_HANDSHAKE_PACKET_SIZE + 1) {
        av_log(LOG_CONTEXT, AV_LOG_ERROR, "Cannot read RTMP handshake response\n");
        return -1;
    }
    i = url_read_complete(rt->stream, clientdata, RTMP_HANDSHAKE_PACKET_SIZE);
    if (i != RTMP_HANDSHAKE_PACKET_SIZE) {
        av_log(LOG_CONTEXT, AV_LOG_ERROR, "Cannot read RTMP handshake response\n");
        return -1;
    }

    av_log(LOG_CONTEXT, AV_LOG_DEBUG, "Server version %d.%d.%d.%d\n",
           serverdata[5], serverdata[6], serverdata[7], serverdata[8]);

    server_pos = rtmp_validate_digest(serverdata + 1, 772);
    if (!server_pos) {
        server_pos = rtmp_validate_digest(serverdata + 1, 8);
        if (!server_pos) {
            av_log(LOG_CONTEXT, AV_LOG_ERROR, "Server response validating failed\n");
            return -1;
        }
    }

    rtmp_calc_digest(tosend + 1 + client_pos, 32, 0,
                     rtmp_server_key, sizeof(rtmp_server_key),
                     digest);
    rtmp_calc_digest(clientdata, RTMP_HANDSHAKE_PACKET_SIZE-32, 0,
                     digest, 32,
                     digest);
    if (memcmp(digest, clientdata + RTMP_HANDSHAKE_PACKET_SIZE - 32, 32)) {
        av_log(LOG_CONTEXT, AV_LOG_ERROR, "Signature mismatch\n");
        return -1;
    }

    for (i = 0; i < RTMP_HANDSHAKE_PACKET_SIZE; i++)
        tosend[i] = av_lfg_get(&rnd) >> 24;
    rtmp_calc_digest(serverdata + 1 + server_pos, 32, 0,
                     rtmp_player_key, sizeof(rtmp_player_key),
                     digest);
    rtmp_calc_digest(tosend,  RTMP_HANDSHAKE_PACKET_SIZE - 32, 0,
                     digest, 32,
                     tosend + RTMP_HANDSHAKE_PACKET_SIZE - 32);

    // write reply back to the server
    url_write(rt->stream, tosend, RTMP_HANDSHAKE_PACKET_SIZE);
    return 0;
}

/**
 * Parses received packet and may perform some action depending on
 * the packet contents.
 * @return 0 for no errors, negative values for serious errors which prevent
 *         further communications, positive values for uncritical errors
 */
static int rtmp_parse_result(URLContext *s, RTMPContext *rt, RTMPPacket *pkt)
{
    int i, t;
    const uint8_t *data_end = pkt->data + pkt->data_size;

    switch (pkt->type) {
    case RTMP_PT_CHUNK_SIZE:
        if (pkt->data_size != 4) {
            av_log(LOG_CONTEXT, AV_LOG_ERROR,
                   "Chunk size change packet is not 4 bytes long (%d)\n", pkt->data_size);
            return -1;
        }
        rt->chunk_size = AV_RB32(pkt->data);
        if (rt->chunk_size <= 0) {
            av_log(LOG_CONTEXT, AV_LOG_ERROR, "Incorrect chunk size %d\n", rt->chunk_size);
            return -1;
        }
        av_log(LOG_CONTEXT, AV_LOG_DEBUG, "New chunk size = %d\n", rt->chunk_size);
        break;
    case RTMP_PT_PING:
        t = AV_RB16(pkt->data);
        if (t == 6)
            gen_pong(s, rt, pkt);
        break;
    case RTMP_PT_INVOKE:
        //TODO: check for the messages sent for wrong state?
        if (!memcmp(pkt->data, "\002\000\006_error", 9)) {
            uint8_t tmpstr[256];

            if (!ff_amf_get_field_value(pkt->data + 9, data_end,
                                        "description", tmpstr, sizeof(tmpstr)))
                av_log(LOG_CONTEXT, AV_LOG_ERROR, "Server error: %s\n",tmpstr);
            return -1;
        } else if (!memcmp(pkt->data, "\002\000\007_result", 10)) {
            switch (rt->state) {
            case STATE_HANDSHAKED:
                gen_create_stream(s, rt);
                rt->state = STATE_CONNECTING;
                break;
            case STATE_CONNECTING:
                //extract a number from the result
                if (pkt->data[10] || pkt->data[19] != 5 || pkt->data[20]) {
                    av_log(LOG_CONTEXT, AV_LOG_WARNING, "Unexpected reply on connect()\n");
                } else {
                    rt->main_channel_id = (int) av_int2dbl(AV_RB64(pkt->data + 21));
                }
                gen_play(s, rt);
                rt->state = STATE_READY;
                break;
            }
        } else if (!memcmp(pkt->data, "\002\000\010onStatus", 11)) {
            const uint8_t* ptr = pkt->data + 11;
            uint8_t tmpstr[256];
            int t;

            for (i = 0; i < 2; i++) {
                t = ff_amf_tag_size(ptr, data_end);
                if (t < 0)
                    return 1;
                ptr += t;
            }
            t = ff_amf_get_field_value(ptr, data_end,
                                       "level", tmpstr, sizeof(tmpstr));
            if (!t && !strcmp(tmpstr, "error")) {
                if (!ff_amf_get_field_value(ptr, data_end,
                                            "description", tmpstr, sizeof(tmpstr)))
                    av_log(LOG_CONTEXT, AV_LOG_ERROR, "Server error: %s\n",tmpstr);
                return -1;
            }
            t = ff_amf_get_field_value(ptr, data_end,
                                       "code", tmpstr, sizeof(tmpstr));
            if (!t && !strcmp(tmpstr, "NetStream.Play.Start")) {
                rt->state = STATE_PLAYING;
                return 0;
            }
        }
        break;
    }
    return 0;
}

/**
 * Interacts with the server by receiving and sending RTMP packets until
 * there is some significant data (media data or expected status notification).
 *
 * @param s          reading context
 * @param for_header non-zero value tells function to work until it gets notification from the server that playing has been started, otherwise function will work until some media data is received (or an error happens)
 * @return 0 for successful operation, negative value in case of error
 */
static int get_packet(URLContext *s, int for_header)
{
    RTMPContext *rt = s->priv_data;
    int ret;

    for(;;) {
        RTMPPacket rpkt;
        if ((ret = ff_rtmp_packet_read(rt->stream, &rpkt,
                                       rt->chunk_size, rt->prev_pkt[0])) != 0) {
            if (ret > 0) {
                return AVERROR(EAGAIN);
            } else {
                return AVERROR(EIO);
            }
        }

        ret = rtmp_parse_result(s, rt, &rpkt);
        if (ret < 0) {//serious error in current packet
            ff_rtmp_packet_destroy(&rpkt);
            return -1;
        }
        if (for_header && rt->state == STATE_PLAYING) {
            ff_rtmp_packet_destroy(&rpkt);
            return 0;
        }
        if (!rpkt.data_size) {
            ff_rtmp_packet_destroy(&rpkt);
            continue;
        }
        if (rpkt.type == RTMP_PT_VIDEO || rpkt.type == RTMP_PT_AUDIO ||
            rpkt.type == RTMP_PT_NOTIFY) {
            uint8_t *p;
            uint32_t ts = rpkt.timestamp;

            if (rpkt.type == RTMP_PT_VIDEO) {
                rt->video_ts += rpkt.timestamp;
                ts = rt->video_ts;
            } else if (rpkt.type == RTMP_PT_AUDIO) {
                rt->audio_ts += rpkt.timestamp;
                ts = rt->audio_ts;
            }
            // generate packet header and put data into buffer for FLV demuxer
            rt->flv_off  = 0;
            rt->flv_size = rpkt.data_size + 15;
            rt->flv_data = p = av_realloc(rt->flv_data, rt->flv_size);
            bytestream_put_byte(&p, rpkt.type);
            bytestream_put_be24(&p, rpkt.data_size);
            bytestream_put_be24(&p, ts);
            bytestream_put_byte(&p, ts >> 24);
            bytestream_put_be24(&p, 0);
            bytestream_put_buffer(&p, rpkt.data, rpkt.data_size);
            bytestream_put_be32(&p, 0);
            ff_rtmp_packet_destroy(&rpkt);
            return 0;
        } else if (rpkt.type == RTMP_PT_METADATA) {
            // we got raw FLV data, make it available for FLV demuxer
            rt->flv_off  = 0;
            rt->flv_size = rpkt.data_size;
            rt->flv_data = av_realloc(rt->flv_data, rt->flv_size);
            memcpy(rt->flv_data, rpkt.data, rpkt.data_size);
            ff_rtmp_packet_destroy(&rpkt);
            return 0;
        }
        ff_rtmp_packet_destroy(&rpkt);
    }
    return 0;
}

static int rtmp_close(URLContext *h)
{
    RTMPContext *rt = h->priv_data;

    av_freep(&rt->flv_data);
    url_close(rt->stream);
    av_free(rt);
    return 0;
}

/**
 * Opens RTMP connection and verifies that the stream can be played.
 *
 * URL syntax: rtmp://server[:port][/app][/playpath]
 *             where 'app' is first one or two directories in the path
 *             (e.g. /ondemand/, /flash/live/, etc.)
 *             and 'playpath' is a file name (the rest of the path,
 *             may be prefixed with "mp4:")
 */
static int rtmp_open(URLContext *s, const char *uri, int flags)
{
    RTMPContext *rt;
    char proto[8], hostname[256], path[1024], app[128], *fname;
    uint8_t buf[2048];
    int port, is_input;
    int ret;

    is_input = !(flags & URL_WRONLY);

    rt = av_mallocz(sizeof(RTMPContext));
    if (!rt)
        return AVERROR(ENOMEM);
    s->priv_data = rt;

    url_split(proto, sizeof(proto), NULL, 0, hostname, sizeof(hostname), &port,
              path, sizeof(path), s->filename);

    if (port < 0)
        port = RTMP_DEFAULT_PORT;
    snprintf(buf, sizeof(buf), "tcp://%s:%d", hostname, port);

    if (url_open(&rt->stream, buf, URL_RDWR) < 0)
        goto fail;

    if (!is_input) {
        av_log(LOG_CONTEXT, AV_LOG_ERROR, "RTMP output is not supported yet.\n");
        goto fail;
    } else {
        rt->state = STATE_START;
        if (rtmp_handshake(s, rt))
            return -1;

        rt->chunk_size = 128;
        rt->state = STATE_HANDSHAKED;
        //extract "app" part from path
        if (!strncmp(path, "/ondemand/", 10)) {
            fname = path + 10;
            memcpy(app, "ondemand", 9);
        } else {
            char *p = strchr(path + 1, '/');
            if (!p) {
                fname = path + 1;
                app[0] = '\0';
            } else {
                fname = strchr(p + 1, '/');
                if (!fname) {
                    fname = p + 1;
                    av_strlcpy(app, path + 1, p - path);
                } else {
                    fname++;
                    av_strlcpy(app, path + 1, fname - path - 1);
                }
            }
        }
        if (!strcmp(fname + strlen(fname) - 4, ".f4v") ||
            !strcmp(fname + strlen(fname) - 4, ".mp4")) {
            memcpy(rt->playpath, "mp4:", 5);
        } else {
            rt->playpath[0] = 0;
        }
        strncat(rt->playpath, fname, sizeof(rt->playpath) - 5);

        av_log(LOG_CONTEXT, AV_LOG_DEBUG, "Proto = %s, path = %s, app = %s, fname = %s\n",
               proto, path, app, rt->playpath);
        gen_connect(s, rt, proto, hostname, port, app);

        do {
            ret = get_packet(s, 1);
        } while (ret == EAGAIN);
        if (ret < 0)
            goto fail;
        // generate FLV header for demuxer
        rt->flv_size = 13;
        rt->flv_data = av_realloc(rt->flv_data, rt->flv_size);
        rt->flv_off  = 0;
        memcpy(rt->flv_data, "FLV\1\5\0\0\0\011\0\0\0\0", rt->flv_size);
    }

    s->max_packet_size = url_get_max_packet_size(rt->stream);
    s->is_streamed     = 1;
    return 0;

fail:
    rtmp_close(s);
    return AVERROR(EIO);
}

static int rtmp_read(URLContext *s, uint8_t *buf, int size)
{
    RTMPContext *rt = s->priv_data;
    int orig_size = size;
    int ret;

    while (size > 0) {
        int data_left = rt->flv_size - rt->flv_off;

        if (data_left >= size) {
            memcpy(buf, rt->flv_data + rt->flv_off, size);
            rt->flv_off += size;
            return orig_size;
        }
        if (data_left > 0) {
            memcpy(buf, rt->flv_data + rt->flv_off, data_left);
            buf  += data_left;
            size -= data_left;
            rt->flv_off = rt->flv_size;
        }
        if ((ret = get_packet(s, 0)) < 0)
           return ret;
    }
    return orig_size;
}

static int rtmp_write(URLContext *h, uint8_t *buf, int size)
{
    return 0;
}

URLProtocol rtmp_protocol = {
    "rtmp",
    rtmp_open,
    rtmp_read,
    rtmp_write,
    NULL, /* seek */
    rtmp_close,
};
