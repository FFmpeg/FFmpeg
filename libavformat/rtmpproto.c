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
#include "internal.h"

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

//#define DEBUG

/** RTMP protocol handler state */
typedef enum {
    STATE_START,      ///< client has not done anything yet
    STATE_HANDSHAKED, ///< client has performed handshake
    STATE_RELEASING,  ///< client releasing stream before publish it (for output)
    STATE_FCPUBLISH,  ///< client FCPublishing stream (for output)
    STATE_CONNECTING, ///< client connected to server successfully
    STATE_READY,      ///< client has sent all needed commands and waits for server reply
    STATE_PLAYING,    ///< client has started receiving multimedia data from server
    STATE_PUBLISHING, ///< client has started sending multimedia data to server (for output)
    STATE_STOPPED,    ///< the broadcast has been stopped
} ClientState;

/** protocol handler context */
typedef struct RTMPContext {
    URLContext*   stream;                     ///< TCP stream used in interactions with RTMP server
    RTMPPacket    prev_pkt[2][RTMP_CHANNELS]; ///< packet history used when reading and sending packets
    int           chunk_size;                 ///< size of the chunks RTMP packets are divided into
    int           is_input;                   ///< input/output flag
    char          playpath[256];              ///< path to filename to play (with possible "mp4:" prefix)
    char          app[128];                   ///< application
    ClientState   state;                      ///< current state
    int           main_channel_id;            ///< an additional channel ID which is used for some invocations
    uint8_t*      flv_data;                   ///< buffer with data for demuxer
    int           flv_size;                   ///< current buffer size
    int           flv_off;                    ///< number of bytes read from current buffer
    RTMPPacket    out_pkt;                    ///< rtmp packet, created from flv a/v or metadata (for output)
    uint32_t      client_report_size;         ///< number of bytes after which client should report to server
    uint32_t      bytes_read;                 ///< number of bytes read from server
    uint32_t      last_bytes_read;            ///< number of bytes read last reported to server
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
                        const char *host, int port)
{
    RTMPPacket pkt;
    uint8_t ver[64], *p;
    char tcurl[512];

    ff_rtmp_packet_create(&pkt, RTMP_SYSTEM_CHANNEL, RTMP_PT_INVOKE, 0, 4096);
    p = pkt.data;

    ff_url_join(tcurl, sizeof(tcurl), proto, NULL, host, port, "/%s", rt->app);
    ff_amf_write_string(&p, "connect");
    ff_amf_write_number(&p, 1.0);
    ff_amf_write_object_start(&p);
    ff_amf_write_field_name(&p, "app");
    ff_amf_write_string(&p, rt->app);

    if (rt->is_input) {
        snprintf(ver, sizeof(ver), "%s %d,%d,%d,%d", RTMP_CLIENT_PLATFORM, RTMP_CLIENT_VER1,
                 RTMP_CLIENT_VER2, RTMP_CLIENT_VER3, RTMP_CLIENT_VER4);
    } else {
        snprintf(ver, sizeof(ver), "FMLE/3.0 (compatible; %s)", LIBAVFORMAT_IDENT);
        ff_amf_write_field_name(&p, "type");
        ff_amf_write_string(&p, "nonprivate");
    }
    ff_amf_write_field_name(&p, "flashVer");
    ff_amf_write_string(&p, ver);
    ff_amf_write_field_name(&p, "tcUrl");
    ff_amf_write_string(&p, tcurl);
    if (rt->is_input) {
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
    }
    ff_amf_write_object_end(&p);

    pkt.data_size = p - pkt.data;

    ff_rtmp_packet_write(rt->stream, &pkt, rt->chunk_size, rt->prev_pkt[1]);
    ff_rtmp_packet_destroy(&pkt);
}

/**
 * Generates 'releaseStream' call and sends it to the server. It should make
 * the server release some channel for media streams.
 */
static void gen_release_stream(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt;
    uint8_t *p;

    ff_rtmp_packet_create(&pkt, RTMP_SYSTEM_CHANNEL, RTMP_PT_INVOKE, 0,
                          29 + strlen(rt->playpath));

    av_log(LOG_CONTEXT, AV_LOG_DEBUG, "Releasing stream...\n");
    p = pkt.data;
    ff_amf_write_string(&p, "releaseStream");
    ff_amf_write_number(&p, 2.0);
    ff_amf_write_null(&p);
    ff_amf_write_string(&p, rt->playpath);

    ff_rtmp_packet_write(rt->stream, &pkt, rt->chunk_size, rt->prev_pkt[1]);
    ff_rtmp_packet_destroy(&pkt);
}

/**
 * Generates 'FCPublish' call and sends it to the server. It should make
 * the server preapare for receiving media streams.
 */
static void gen_fcpublish_stream(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt;
    uint8_t *p;

    ff_rtmp_packet_create(&pkt, RTMP_SYSTEM_CHANNEL, RTMP_PT_INVOKE, 0,
                          25 + strlen(rt->playpath));

    av_log(LOG_CONTEXT, AV_LOG_DEBUG, "FCPublish stream...\n");
    p = pkt.data;
    ff_amf_write_string(&p, "FCPublish");
    ff_amf_write_number(&p, 3.0);
    ff_amf_write_null(&p);
    ff_amf_write_string(&p, rt->playpath);

    ff_rtmp_packet_write(rt->stream, &pkt, rt->chunk_size, rt->prev_pkt[1]);
    ff_rtmp_packet_destroy(&pkt);
}

/**
 * Generates 'FCUnpublish' call and sends it to the server. It should make
 * the server destroy stream.
 */
static void gen_fcunpublish_stream(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt;
    uint8_t *p;

    ff_rtmp_packet_create(&pkt, RTMP_SYSTEM_CHANNEL, RTMP_PT_INVOKE, 0,
                          27 + strlen(rt->playpath));

    av_log(LOG_CONTEXT, AV_LOG_DEBUG, "UnPublishing stream...\n");
    p = pkt.data;
    ff_amf_write_string(&p, "FCUnpublish");
    ff_amf_write_number(&p, 5.0);
    ff_amf_write_null(&p);
    ff_amf_write_string(&p, rt->playpath);

    ff_rtmp_packet_write(rt->stream, &pkt, rt->chunk_size, rt->prev_pkt[1]);
    ff_rtmp_packet_destroy(&pkt);
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
    ff_rtmp_packet_create(&pkt, RTMP_SYSTEM_CHANNEL, RTMP_PT_INVOKE, 0, 25);

    p = pkt.data;
    ff_amf_write_string(&p, "createStream");
    ff_amf_write_number(&p, rt->is_input ? 3.0 : 4.0);
    ff_amf_write_null(&p);

    ff_rtmp_packet_write(rt->stream, &pkt, rt->chunk_size, rt->prev_pkt[1]);
    ff_rtmp_packet_destroy(&pkt);
}


/**
 * Generates 'deleteStream' call and sends it to the server. It should make
 * the server remove some channel for media streams.
 */
static void gen_delete_stream(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt;
    uint8_t *p;

    av_log(LOG_CONTEXT, AV_LOG_DEBUG, "Deleting stream...\n");
    ff_rtmp_packet_create(&pkt, RTMP_SYSTEM_CHANNEL, RTMP_PT_INVOKE, 0, 34);

    p = pkt.data;
    ff_amf_write_string(&p, "deleteStream");
    ff_amf_write_number(&p, 0.0);
    ff_amf_write_null(&p);
    ff_amf_write_number(&p, rt->main_channel_id);

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
                          20 + strlen(rt->playpath));
    pkt.extra = rt->main_channel_id;

    p = pkt.data;
    ff_amf_write_string(&p, "play");
    ff_amf_write_number(&p, 0.0);
    ff_amf_write_null(&p);
    ff_amf_write_string(&p, rt->playpath);

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
 * Generates 'publish' call and sends it to the server.
 */
static void gen_publish(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt;
    uint8_t *p;

    av_log(LOG_CONTEXT, AV_LOG_DEBUG, "Sending publish command for '%s'\n", rt->playpath);
    ff_rtmp_packet_create(&pkt, RTMP_SOURCE_CHANNEL, RTMP_PT_INVOKE, 0,
                          30 + strlen(rt->playpath));
    pkt.extra = rt->main_channel_id;

    p = pkt.data;
    ff_amf_write_string(&p, "publish");
    ff_amf_write_number(&p, 0.0);
    ff_amf_write_null(&p);
    ff_amf_write_string(&p, rt->playpath);
    ff_amf_write_string(&p, "live");

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
    bytestream_put_be32(&p, AV_RB32(ppkt->data+2));
    ff_rtmp_packet_write(rt->stream, &pkt, rt->chunk_size, rt->prev_pkt[1]);
    ff_rtmp_packet_destroy(&pkt);
}

/**
 * Generates report on bytes read so far and sends it to the server.
 */
static void gen_bytes_read(URLContext *s, RTMPContext *rt, uint32_t ts)
{
    RTMPPacket pkt;
    uint8_t *p;

    ff_rtmp_packet_create(&pkt, RTMP_NETWORK_CHANNEL, RTMP_PT_BYTES_READ, ts, 4);
    p = pkt.data;
    bytestream_put_be32(&p, rt->bytes_read);
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

    if (rt->is_input && serverdata[5] >= 3) {
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
    } else {
        url_write(rt->stream, serverdata+1, RTMP_HANDSHAKE_PACKET_SIZE);
    }

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

#ifdef DEBUG
    ff_rtmp_packet_dump(LOG_CONTEXT, pkt);
#endif

    switch (pkt->type) {
    case RTMP_PT_CHUNK_SIZE:
        if (pkt->data_size != 4) {
            av_log(LOG_CONTEXT, AV_LOG_ERROR,
                   "Chunk size change packet is not 4 bytes long (%d)\n", pkt->data_size);
            return -1;
        }
        if (!rt->is_input)
            ff_rtmp_packet_write(rt->stream, pkt, rt->chunk_size, rt->prev_pkt[1]);
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
    case RTMP_PT_CLIENT_BW:
        if (pkt->data_size < 4) {
            av_log(LOG_CONTEXT, AV_LOG_ERROR,
                   "Client bandwidth report packet is less than 4 bytes long (%d)\n",
                   pkt->data_size);
            return -1;
        }
        av_log(LOG_CONTEXT, AV_LOG_DEBUG, "Client bandwidth = %d\n", AV_RB32(pkt->data));
        rt->client_report_size = AV_RB32(pkt->data) >> 1;
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
                if (!rt->is_input) {
                    gen_release_stream(s, rt);
                    gen_fcpublish_stream(s, rt);
                    rt->state = STATE_RELEASING;
                } else {
                    rt->state = STATE_CONNECTING;
                }
                gen_create_stream(s, rt);
                break;
            case STATE_FCPUBLISH:
                rt->state = STATE_CONNECTING;
                break;
            case STATE_RELEASING:
                rt->state = STATE_FCPUBLISH;
                /* hack for Wowza Media Server, it does not send result for
                 * releaseStream and FCPublish calls */
                if (!pkt->data[10]) {
                    int pkt_id = (int) av_int2dbl(AV_RB64(pkt->data + 11));
                    if (pkt_id == 4)
                        rt->state = STATE_CONNECTING;
                }
                if (rt->state != STATE_CONNECTING)
                    break;
            case STATE_CONNECTING:
                //extract a number from the result
                if (pkt->data[10] || pkt->data[19] != 5 || pkt->data[20]) {
                    av_log(LOG_CONTEXT, AV_LOG_WARNING, "Unexpected reply on connect()\n");
                } else {
                    rt->main_channel_id = (int) av_int2dbl(AV_RB64(pkt->data + 21));
                }
                if (rt->is_input) {
                    gen_play(s, rt);
                } else {
                    gen_publish(s, rt);
                }
                rt->state = STATE_READY;
                break;
            }
        } else if (!memcmp(pkt->data, "\002\000\010onStatus", 11)) {
            const uint8_t* ptr = pkt->data + 11;
            uint8_t tmpstr[256];

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
            if (!t && !strcmp(tmpstr, "NetStream.Play.Start")) rt->state = STATE_PLAYING;
            if (!t && !strcmp(tmpstr, "NetStream.Play.Stop")) rt->state = STATE_STOPPED;
            if (!t && !strcmp(tmpstr, "NetStream.Play.UnpublishNotify")) rt->state = STATE_STOPPED;
            if (!t && !strcmp(tmpstr, "NetStream.Publish.Start")) rt->state = STATE_PUBLISHING;
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
 * @param for_header non-zero value tells function to work until it
 * gets notification from the server that playing has been started,
 * otherwise function will work until some media data is received (or
 * an error happens)
 * @return 0 for successful operation, negative value in case of error
 */
static int get_packet(URLContext *s, int for_header)
{
    RTMPContext *rt = s->priv_data;
    int ret;
    uint8_t *p;
    const uint8_t *next;
    uint32_t data_size;
    uint32_t ts, cts, pts=0;

    if (rt->state == STATE_STOPPED)
        return AVERROR_EOF;

    for (;;) {
        RTMPPacket rpkt;
        if ((ret = ff_rtmp_packet_read(rt->stream, &rpkt,
                                       rt->chunk_size, rt->prev_pkt[0])) <= 0) {
            if (ret == 0) {
                return AVERROR(EAGAIN);
            } else {
                return AVERROR(EIO);
            }
        }
        rt->bytes_read += ret;
        if (rt->bytes_read > rt->last_bytes_read + rt->client_report_size) {
            av_log(LOG_CONTEXT, AV_LOG_DEBUG, "Sending bytes read report\n");
            gen_bytes_read(s, rt, rpkt.timestamp + 1);
            rt->last_bytes_read = rt->bytes_read;
        }

        ret = rtmp_parse_result(s, rt, &rpkt);
        if (ret < 0) {//serious error in current packet
            ff_rtmp_packet_destroy(&rpkt);
            return -1;
        }
        if (rt->state == STATE_STOPPED) {
            ff_rtmp_packet_destroy(&rpkt);
            return AVERROR_EOF;
        }
        if (for_header && (rt->state == STATE_PLAYING || rt->state == STATE_PUBLISHING)) {
            ff_rtmp_packet_destroy(&rpkt);
            return 0;
        }
        if (!rpkt.data_size || !rt->is_input) {
            ff_rtmp_packet_destroy(&rpkt);
            continue;
        }
        if (rpkt.type == RTMP_PT_VIDEO || rpkt.type == RTMP_PT_AUDIO ||
           (rpkt.type == RTMP_PT_NOTIFY && !memcmp("\002\000\012onMetaData", rpkt.data, 13))) {
            ts = rpkt.timestamp;

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
            /* rewrite timestamps */
            next = rpkt.data;
            ts = rpkt.timestamp;
            while (next - rpkt.data < rpkt.data_size - 11) {
                next++;
                data_size = bytestream_get_be24(&next);
                p=next;
                cts = bytestream_get_be24(&next);
                cts |= bytestream_get_byte(&next);
                if (pts==0)
                    pts=cts;
                ts += cts - pts;
                pts = cts;
                bytestream_put_be24(&p, ts);
                bytestream_put_byte(&p, ts >> 24);
                next += data_size + 3 + 4;
            }
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

    if (!rt->is_input) {
        rt->flv_data = NULL;
        if (rt->out_pkt.data_size)
            ff_rtmp_packet_destroy(&rt->out_pkt);
        if (rt->state > STATE_FCPUBLISH)
            gen_fcunpublish_stream(h, rt);
    }
    if (rt->state > STATE_HANDSHAKED)
        gen_delete_stream(h, rt);

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
    char proto[8], hostname[256], path[1024], *fname;
    uint8_t buf[2048];
    int port;
    int ret;

    rt = av_mallocz(sizeof(RTMPContext));
    if (!rt)
        return AVERROR(ENOMEM);
    s->priv_data = rt;
    rt->is_input = !(flags & URL_WRONLY);

    ff_url_split(proto, sizeof(proto), NULL, 0, hostname, sizeof(hostname), &port,
                 path, sizeof(path), s->filename);

    if (port < 0)
        port = RTMP_DEFAULT_PORT;
    ff_url_join(buf, sizeof(buf), "tcp", NULL, hostname, port, NULL);

    if (url_open(&rt->stream, buf, URL_RDWR) < 0) {
        av_log(LOG_CONTEXT, AV_LOG_ERROR, "Cannot open connection %s\n", buf);
        goto fail;
    }

    rt->state = STATE_START;
    if (rtmp_handshake(s, rt))
        return -1;

    rt->chunk_size = 128;
    rt->state = STATE_HANDSHAKED;
    //extract "app" part from path
    if (!strncmp(path, "/ondemand/", 10)) {
        fname = path + 10;
        memcpy(rt->app, "ondemand", 9);
    } else {
        char *p = strchr(path + 1, '/');
        if (!p) {
            fname = path + 1;
            rt->app[0] = '\0';
        } else {
            char *c = strchr(p + 1, ':');
            fname = strchr(p + 1, '/');
            if (!fname || c < fname) {
                fname = p + 1;
                av_strlcpy(rt->app, path + 1, p - path);
            } else {
                fname++;
                av_strlcpy(rt->app, path + 1, fname - path - 1);
            }
        }
    }
    if (!strchr(fname, ':') &&
        (!strcmp(fname + strlen(fname) - 4, ".f4v") ||
         !strcmp(fname + strlen(fname) - 4, ".mp4"))) {
        memcpy(rt->playpath, "mp4:", 5);
    } else {
        rt->playpath[0] = 0;
    }
    strncat(rt->playpath, fname, sizeof(rt->playpath) - 5);

    rt->client_report_size = 1048576;
    rt->bytes_read = 0;
    rt->last_bytes_read = 0;

    av_log(LOG_CONTEXT, AV_LOG_DEBUG, "Proto = %s, path = %s, app = %s, fname = %s\n",
           proto, path, rt->app, rt->playpath);
    gen_connect(s, rt, proto, hostname, port);

    do {
        ret = get_packet(s, 1);
    } while (ret == EAGAIN);
    if (ret < 0)
        goto fail;

    if (rt->is_input) {
        // generate FLV header for demuxer
        rt->flv_size = 13;
        rt->flv_data = av_realloc(rt->flv_data, rt->flv_size);
        rt->flv_off  = 0;
        memcpy(rt->flv_data, "FLV\1\5\0\0\0\011\0\0\0\0", rt->flv_size);
    } else {
        rt->flv_size = 0;
        rt->flv_data = NULL;
        rt->flv_off  = 0;
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
    RTMPContext *rt = h->priv_data;
    int size_temp = size;
    int pktsize, pkttype;
    uint32_t ts;
    const uint8_t *buf_temp = buf;

    if (size < 11) {
        av_log(LOG_CONTEXT, AV_LOG_DEBUG, "FLV packet too small %d\n", size);
        return 0;
    }

    do {
        if (!rt->flv_off) {
            //skip flv header
            if (buf_temp[0] == 'F' && buf_temp[1] == 'L' && buf_temp[2] == 'V') {
                buf_temp += 9 + 4;
                size_temp -= 9 + 4;
            }

            pkttype = bytestream_get_byte(&buf_temp);
            pktsize = bytestream_get_be24(&buf_temp);
            ts = bytestream_get_be24(&buf_temp);
            ts |= bytestream_get_byte(&buf_temp) << 24;
            bytestream_get_be24(&buf_temp);
            size_temp -= 11;
            rt->flv_size = pktsize;

            //force 12bytes header
            if (((pkttype == RTMP_PT_VIDEO || pkttype == RTMP_PT_AUDIO) && ts == 0) ||
                pkttype == RTMP_PT_NOTIFY) {
                if (pkttype == RTMP_PT_NOTIFY)
                    pktsize += 16;
                rt->prev_pkt[1][RTMP_SOURCE_CHANNEL].channel_id = 0;
            }

            //this can be a big packet, it's better to send it right here
            ff_rtmp_packet_create(&rt->out_pkt, RTMP_SOURCE_CHANNEL, pkttype, ts, pktsize);
            rt->out_pkt.extra = rt->main_channel_id;
            rt->flv_data = rt->out_pkt.data;

            if (pkttype == RTMP_PT_NOTIFY)
                ff_amf_write_string(&rt->flv_data, "@setDataFrame");
        }

        if (rt->flv_size - rt->flv_off > size_temp) {
            bytestream_get_buffer(&buf_temp, rt->flv_data + rt->flv_off, size_temp);
            rt->flv_off += size_temp;
        } else {
            bytestream_get_buffer(&buf_temp, rt->flv_data + rt->flv_off, rt->flv_size - rt->flv_off);
            rt->flv_off += rt->flv_size - rt->flv_off;
        }

        if (rt->flv_off == rt->flv_size) {
            bytestream_get_be32(&buf_temp);

            ff_rtmp_packet_write(rt->stream, &rt->out_pkt, rt->chunk_size, rt->prev_pkt[1]);
            ff_rtmp_packet_destroy(&rt->out_pkt);
            rt->flv_size = 0;
            rt->flv_off = 0;
        }
    } while (buf_temp - buf < size_temp);
    return size;
}

URLProtocol rtmp_protocol = {
    "rtmp",
    rtmp_open,
    rtmp_read,
    rtmp_write,
    NULL, /* seek */
    rtmp_close,
};
