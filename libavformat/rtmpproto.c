/*
 * RTMP network protocol
 * Copyright (c) 2009 Konstantin Shishkov
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
 * RTMP protocol
 */

#include "libavcodec/bytestream.h"
#include "libavutil/avstring.h"
#include "libavutil/base64.h"
#include "libavutil/intfloat.h"
#include "libavutil/lfg.h"
#include "libavutil/md5.h"
#include "libavutil/opt.h"
#include "libavutil/random_seed.h"
#include "libavutil/sha.h"
#include "avformat.h"
#include "internal.h"

#include "network.h"

#include "flv.h"
#include "rtmp.h"
#include "rtmpcrypt.h"
#include "rtmppkt.h"
#include "url.h"

#if CONFIG_ZLIB
#include <zlib.h>
#endif

#define APP_MAX_LENGTH 1024
#define PLAYPATH_MAX_LENGTH 256
#define TCURL_MAX_LENGTH 512
#define FLASHVER_MAX_LENGTH 64
#define RTMP_PKTDATA_DEFAULT_SIZE 4096
#define RTMP_HEADER 11

/** RTMP protocol handler state */
typedef enum {
    STATE_START,      ///< client has not done anything yet
    STATE_HANDSHAKED, ///< client has performed handshake
    STATE_FCPUBLISH,  ///< client FCPublishing stream (for output)
    STATE_PLAYING,    ///< client has started receiving multimedia data from server
    STATE_SEEKING,    ///< client has started the seek operation. Back on STATE_PLAYING when the time comes
    STATE_PUBLISHING, ///< client has started sending multimedia data to server (for output)
    STATE_RECEIVING,  ///< received a publish command (for input)
    STATE_SENDING,    ///< received a play command (for output)
    STATE_STOPPED,    ///< the broadcast has been stopped
} ClientState;

typedef struct TrackedMethod {
    char *name;
    int id;
} TrackedMethod;

/** protocol handler context */
typedef struct RTMPContext {
    const AVClass *class;
    URLContext*   stream;                     ///< TCP stream used in interactions with RTMP server
    RTMPPacket    prev_pkt[2][RTMP_CHANNELS]; ///< packet history used when reading and sending packets ([0] for reading, [1] for writing)
    int           in_chunk_size;              ///< size of the chunks incoming RTMP packets are divided into
    int           out_chunk_size;             ///< size of the chunks outgoing RTMP packets are divided into
    int           is_input;                   ///< input/output flag
    char          *playpath;                  ///< stream identifier to play (with possible "mp4:" prefix)
    int           live;                       ///< 0: recorded, -1: live, -2: both
    char          *app;                       ///< name of application
    char          *conn;                      ///< append arbitrary AMF data to the Connect message
    ClientState   state;                      ///< current state
    int           stream_id;                  ///< ID assigned by the server for the stream
    uint8_t*      flv_data;                   ///< buffer with data for demuxer
    int           flv_size;                   ///< current buffer size
    int           flv_off;                    ///< number of bytes read from current buffer
    int           flv_nb_packets;             ///< number of flv packets published
    RTMPPacket    out_pkt;                    ///< rtmp packet, created from flv a/v or metadata (for output)
    uint32_t      client_report_size;         ///< number of bytes after which client should report to server
    uint32_t      bytes_read;                 ///< number of bytes read from server
    uint32_t      last_bytes_read;            ///< number of bytes read last reported to server
    int           skip_bytes;                 ///< number of bytes to skip from the input FLV stream in the next write call
    uint8_t       flv_header[RTMP_HEADER];    ///< partial incoming flv packet header
    int           flv_header_bytes;           ///< number of initialized bytes in flv_header
    int           nb_invokes;                 ///< keeps track of invoke messages
    char*         tcurl;                      ///< url of the target stream
    char*         flashver;                   ///< version of the flash plugin
    char*         swfhash;                    ///< SHA256 hash of the decompressed SWF file (32 bytes)
    int           swfhash_len;                ///< length of the SHA256 hash
    int           swfsize;                    ///< size of the decompressed SWF file
    char*         swfurl;                     ///< url of the swf player
    char*         swfverify;                  ///< URL to player swf file, compute hash/size automatically
    char          swfverification[42];        ///< hash of the SWF verification
    char*         pageurl;                    ///< url of the web page
    char*         subscribe;                  ///< name of live stream to subscribe
    int           server_bw;                  ///< server bandwidth
    int           client_buffer_time;         ///< client buffer time in ms
    int           flush_interval;             ///< number of packets flushed in the same request (RTMPT only)
    int           encrypted;                  ///< use an encrypted connection (RTMPE only)
    TrackedMethod*tracked_methods;            ///< tracked methods buffer
    int           nb_tracked_methods;         ///< number of tracked methods
    int           tracked_methods_size;       ///< size of the tracked methods buffer
    int           listen;                     ///< listen mode flag
    int           listen_timeout;             ///< listen timeout to wait for new connections
    int           nb_streamid;                ///< The next stream id to return on createStream calls
    char          username[50];
    char          password[50];
    char          auth_params[500];
    int           do_reconnect;
    int           auth_tried;
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

static int add_tracked_method(RTMPContext *rt, const char *name, int id)
{
    int err;

    if (rt->nb_tracked_methods + 1 > rt->tracked_methods_size) {
        rt->tracked_methods_size = (rt->nb_tracked_methods + 1) * 2;
        if ((err = av_reallocp(&rt->tracked_methods, rt->tracked_methods_size *
                               sizeof(*rt->tracked_methods))) < 0) {
            rt->nb_tracked_methods = 0;
            rt->tracked_methods_size = 0;
            return err;
        }
    }

    rt->tracked_methods[rt->nb_tracked_methods].name = av_strdup(name);
    if (!rt->tracked_methods[rt->nb_tracked_methods].name)
        return AVERROR(ENOMEM);
    rt->tracked_methods[rt->nb_tracked_methods].id = id;
    rt->nb_tracked_methods++;

    return 0;
}

static void del_tracked_method(RTMPContext *rt, int index)
{
    memmove(&rt->tracked_methods[index], &rt->tracked_methods[index + 1],
            sizeof(*rt->tracked_methods) * (rt->nb_tracked_methods - index - 1));
    rt->nb_tracked_methods--;
}

static int find_tracked_method(URLContext *s, RTMPPacket *pkt, int offset,
                               char **tracked_method)
{
    RTMPContext *rt = s->priv_data;
    GetByteContext gbc;
    double pkt_id;
    int ret;
    int i;

    bytestream2_init(&gbc, pkt->data + offset, pkt->size - offset);
    if ((ret = ff_amf_read_number(&gbc, &pkt_id)) < 0)
        return ret;

    for (i = 0; i < rt->nb_tracked_methods; i++) {
        if (rt->tracked_methods[i].id != pkt_id)
            continue;

        *tracked_method = rt->tracked_methods[i].name;
        del_tracked_method(rt, i);
        break;
    }

    return 0;
}

static void free_tracked_methods(RTMPContext *rt)
{
    int i;

    for (i = 0; i < rt->nb_tracked_methods; i ++)
        av_free(rt->tracked_methods[i].name);
    av_free(rt->tracked_methods);
    rt->tracked_methods      = NULL;
    rt->tracked_methods_size = 0;
    rt->nb_tracked_methods   = 0;
}

static int rtmp_send_packet(RTMPContext *rt, RTMPPacket *pkt, int track)
{
    int ret;

    if (pkt->type == RTMP_PT_INVOKE && track) {
        GetByteContext gbc;
        char name[128];
        double pkt_id;
        int len;

        bytestream2_init(&gbc, pkt->data, pkt->size);
        if ((ret = ff_amf_read_string(&gbc, name, sizeof(name), &len)) < 0)
            goto fail;

        if ((ret = ff_amf_read_number(&gbc, &pkt_id)) < 0)
            goto fail;

        if ((ret = add_tracked_method(rt, name, pkt_id)) < 0)
            goto fail;
    }

    ret = ff_rtmp_packet_write(rt->stream, pkt, rt->out_chunk_size,
                               rt->prev_pkt[1]);
fail:
    ff_rtmp_packet_destroy(pkt);
    return ret;
}

static int rtmp_write_amf_data(URLContext *s, char *param, uint8_t **p)
{
    char *field, *value;
    char type;

    /* The type must be B for Boolean, N for number, S for string, O for
     * object, or Z for null. For Booleans the data must be either 0 or 1 for
     * FALSE or TRUE, respectively. Likewise for Objects the data must be
     * 0 or 1 to end or begin an object, respectively. Data items in subobjects
     * may be named, by prefixing the type with 'N' and specifying the name
     * before the value (ie. NB:myFlag:1). This option may be used multiple times
     * to construct arbitrary AMF sequences. */
    if (param[0] && param[1] == ':') {
        type = param[0];
        value = param + 2;
    } else if (param[0] == 'N' && param[1] && param[2] == ':') {
        type = param[1];
        field = param + 3;
        value = strchr(field, ':');
        if (!value)
            goto fail;
        *value = '\0';
        value++;

        ff_amf_write_field_name(p, field);
    } else {
        goto fail;
    }

    switch (type) {
    case 'B':
        ff_amf_write_bool(p, value[0] != '0');
        break;
    case 'S':
        ff_amf_write_string(p, value);
        break;
    case 'N':
        ff_amf_write_number(p, strtod(value, NULL));
        break;
    case 'Z':
        ff_amf_write_null(p);
        break;
    case 'O':
        if (value[0] != '0')
            ff_amf_write_object_start(p);
        else
            ff_amf_write_object_end(p);
        break;
    default:
        goto fail;
        break;
    }

    return 0;

fail:
    av_log(s, AV_LOG_ERROR, "Invalid AMF parameter: %s\n", param);
    return AVERROR(EINVAL);
}

/**
 * Generate 'connect' call and send it to the server.
 */
static int gen_connect(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt;
    uint8_t *p;
    int ret;

    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_SYSTEM_CHANNEL, RTMP_PT_INVOKE,
                                     0, 4096 + APP_MAX_LENGTH)) < 0)
        return ret;

    p = pkt.data;

    ff_amf_write_string(&p, "connect");
    ff_amf_write_number(&p, ++rt->nb_invokes);
    ff_amf_write_object_start(&p);
    ff_amf_write_field_name(&p, "app");
    ff_amf_write_string2(&p, rt->app, rt->auth_params);

    if (!rt->is_input) {
        ff_amf_write_field_name(&p, "type");
        ff_amf_write_string(&p, "nonprivate");
    }
    ff_amf_write_field_name(&p, "flashVer");
    ff_amf_write_string(&p, rt->flashver);

    if (rt->swfurl) {
        ff_amf_write_field_name(&p, "swfUrl");
        ff_amf_write_string(&p, rt->swfurl);
    }

    ff_amf_write_field_name(&p, "tcUrl");
    ff_amf_write_string2(&p, rt->tcurl, rt->auth_params);
    if (rt->is_input) {
        ff_amf_write_field_name(&p, "fpad");
        ff_amf_write_bool(&p, 0);
        ff_amf_write_field_name(&p, "capabilities");
        ff_amf_write_number(&p, 15.0);

        /* Tell the server we support all the audio codecs except
         * SUPPORT_SND_INTEL (0x0008) and SUPPORT_SND_UNUSED (0x0010)
         * which are unused in the RTMP protocol implementation. */
        ff_amf_write_field_name(&p, "audioCodecs");
        ff_amf_write_number(&p, 4071.0);
        ff_amf_write_field_name(&p, "videoCodecs");
        ff_amf_write_number(&p, 252.0);
        ff_amf_write_field_name(&p, "videoFunction");
        ff_amf_write_number(&p, 1.0);

        if (rt->pageurl) {
            ff_amf_write_field_name(&p, "pageUrl");
            ff_amf_write_string(&p, rt->pageurl);
        }
    }
    ff_amf_write_object_end(&p);

    if (rt->conn) {
        char *param = rt->conn;

        // Write arbitrary AMF data to the Connect message.
        while (param != NULL) {
            char *sep;
            param += strspn(param, " ");
            if (!*param)
                break;
            sep = strchr(param, ' ');
            if (sep)
                *sep = '\0';
            if ((ret = rtmp_write_amf_data(s, param, &p)) < 0) {
                // Invalid AMF parameter.
                ff_rtmp_packet_destroy(&pkt);
                return ret;
            }

            if (sep)
                param = sep + 1;
            else
                break;
        }
    }

    pkt.size = p - pkt.data;

    return rtmp_send_packet(rt, &pkt, 1);
}

static int read_connect(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt = { 0 };
    uint8_t *p;
    const uint8_t *cp;
    int ret;
    char command[64];
    int stringlen;
    double seqnum;
    uint8_t tmpstr[256];
    GetByteContext gbc;

    if ((ret = ff_rtmp_packet_read(rt->stream, &pkt, rt->in_chunk_size,
                                   rt->prev_pkt[0])) < 0)
        return ret;
    cp = pkt.data;
    bytestream2_init(&gbc, cp, pkt.size);
    if (ff_amf_read_string(&gbc, command, sizeof(command), &stringlen)) {
        av_log(s, AV_LOG_ERROR, "Unable to read command string\n");
        ff_rtmp_packet_destroy(&pkt);
        return AVERROR_INVALIDDATA;
    }
    if (strcmp(command, "connect")) {
        av_log(s, AV_LOG_ERROR, "Expecting connect, got %s\n", command);
        ff_rtmp_packet_destroy(&pkt);
        return AVERROR_INVALIDDATA;
    }
    ret = ff_amf_read_number(&gbc, &seqnum);
    if (ret)
        av_log(s, AV_LOG_WARNING, "SeqNum not found\n");
    /* Here one could parse an AMF Object with data as flashVers and others. */
    ret = ff_amf_get_field_value(gbc.buffer,
                                 gbc.buffer + bytestream2_get_bytes_left(&gbc),
                                 "app", tmpstr, sizeof(tmpstr));
    if (ret)
        av_log(s, AV_LOG_WARNING, "App field not found in connect\n");
    if (!ret && strcmp(tmpstr, rt->app))
        av_log(s, AV_LOG_WARNING, "App field don't match up: %s <-> %s\n",
               tmpstr, rt->app);
    ff_rtmp_packet_destroy(&pkt);

    // Send Window Acknowledgement Size (as defined in speficication)
    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_NETWORK_CHANNEL,
                                     RTMP_PT_SERVER_BW, 0, 4)) < 0)
        return ret;
    p = pkt.data;
    bytestream_put_be32(&p, rt->server_bw);
    pkt.size = p - pkt.data;
    ret = ff_rtmp_packet_write(rt->stream, &pkt, rt->out_chunk_size,
                               rt->prev_pkt[1]);
    ff_rtmp_packet_destroy(&pkt);
    if (ret < 0)
        return ret;
    // Send Peer Bandwidth
    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_NETWORK_CHANNEL,
                                     RTMP_PT_CLIENT_BW, 0, 5)) < 0)
        return ret;
    p = pkt.data;
    bytestream_put_be32(&p, rt->server_bw);
    bytestream_put_byte(&p, 2); // dynamic
    pkt.size = p - pkt.data;
    ret = ff_rtmp_packet_write(rt->stream, &pkt, rt->out_chunk_size,
                               rt->prev_pkt[1]);
    ff_rtmp_packet_destroy(&pkt);
    if (ret < 0)
        return ret;

    // Ping request
    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_NETWORK_CHANNEL,
                                     RTMP_PT_PING, 0, 6)) < 0)
        return ret;

    p = pkt.data;
    bytestream_put_be16(&p, 0); // 0 -> Stream Begin
    bytestream_put_be32(&p, 0);
    ret = ff_rtmp_packet_write(rt->stream, &pkt, rt->out_chunk_size,
                               rt->prev_pkt[1]);
    ff_rtmp_packet_destroy(&pkt);
    if (ret < 0)
        return ret;

    // Chunk size
    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_SYSTEM_CHANNEL,
                                     RTMP_PT_CHUNK_SIZE, 0, 4)) < 0)
        return ret;

    p = pkt.data;
    bytestream_put_be32(&p, rt->out_chunk_size);
    ret = ff_rtmp_packet_write(rt->stream, &pkt, rt->out_chunk_size,
                               rt->prev_pkt[1]);
    ff_rtmp_packet_destroy(&pkt);
    if (ret < 0)
        return ret;

    // Send result_ NetConnection.Connect.Success to connect
    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_SYSTEM_CHANNEL,
                                     RTMP_PT_INVOKE, 0,
                                     RTMP_PKTDATA_DEFAULT_SIZE)) < 0)
        return ret;

    p = pkt.data;
    ff_amf_write_string(&p, "_result");
    ff_amf_write_number(&p, seqnum);

    ff_amf_write_object_start(&p);
    ff_amf_write_field_name(&p, "fmsVer");
    ff_amf_write_string(&p, "FMS/3,0,1,123");
    ff_amf_write_field_name(&p, "capabilities");
    ff_amf_write_number(&p, 31);
    ff_amf_write_object_end(&p);

    ff_amf_write_object_start(&p);
    ff_amf_write_field_name(&p, "level");
    ff_amf_write_string(&p, "status");
    ff_amf_write_field_name(&p, "code");
    ff_amf_write_string(&p, "NetConnection.Connect.Success");
    ff_amf_write_field_name(&p, "description");
    ff_amf_write_string(&p, "Connection succeeded.");
    ff_amf_write_field_name(&p, "objectEncoding");
    ff_amf_write_number(&p, 0);
    ff_amf_write_object_end(&p);

    pkt.size = p - pkt.data;
    ret = ff_rtmp_packet_write(rt->stream, &pkt, rt->out_chunk_size,
                               rt->prev_pkt[1]);
    ff_rtmp_packet_destroy(&pkt);
    if (ret < 0)
        return ret;

    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_SYSTEM_CHANNEL,
                                     RTMP_PT_INVOKE, 0, 30)) < 0)
        return ret;
    p = pkt.data;
    ff_amf_write_string(&p, "onBWDone");
    ff_amf_write_number(&p, 0);
    ff_amf_write_null(&p);
    ff_amf_write_number(&p, 8192);
    pkt.size = p - pkt.data;
    ret = ff_rtmp_packet_write(rt->stream, &pkt, rt->out_chunk_size,
                               rt->prev_pkt[1]);
    ff_rtmp_packet_destroy(&pkt);

    return ret;
}

/**
 * Generate 'releaseStream' call and send it to the server. It should make
 * the server release some channel for media streams.
 */
static int gen_release_stream(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt;
    uint8_t *p;
    int ret;

    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_SYSTEM_CHANNEL, RTMP_PT_INVOKE,
                                     0, 29 + strlen(rt->playpath))) < 0)
        return ret;

    av_log(s, AV_LOG_DEBUG, "Releasing stream...\n");
    p = pkt.data;
    ff_amf_write_string(&p, "releaseStream");
    ff_amf_write_number(&p, ++rt->nb_invokes);
    ff_amf_write_null(&p);
    ff_amf_write_string(&p, rt->playpath);

    return rtmp_send_packet(rt, &pkt, 1);
}

/**
 * Generate 'FCPublish' call and send it to the server. It should make
 * the server preapare for receiving media streams.
 */
static int gen_fcpublish_stream(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt;
    uint8_t *p;
    int ret;

    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_SYSTEM_CHANNEL, RTMP_PT_INVOKE,
                                     0, 25 + strlen(rt->playpath))) < 0)
        return ret;

    av_log(s, AV_LOG_DEBUG, "FCPublish stream...\n");
    p = pkt.data;
    ff_amf_write_string(&p, "FCPublish");
    ff_amf_write_number(&p, ++rt->nb_invokes);
    ff_amf_write_null(&p);
    ff_amf_write_string(&p, rt->playpath);

    return rtmp_send_packet(rt, &pkt, 1);
}

/**
 * Generate 'FCUnpublish' call and send it to the server. It should make
 * the server destroy stream.
 */
static int gen_fcunpublish_stream(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt;
    uint8_t *p;
    int ret;

    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_SYSTEM_CHANNEL, RTMP_PT_INVOKE,
                                     0, 27 + strlen(rt->playpath))) < 0)
        return ret;

    av_log(s, AV_LOG_DEBUG, "UnPublishing stream...\n");
    p = pkt.data;
    ff_amf_write_string(&p, "FCUnpublish");
    ff_amf_write_number(&p, ++rt->nb_invokes);
    ff_amf_write_null(&p);
    ff_amf_write_string(&p, rt->playpath);

    return rtmp_send_packet(rt, &pkt, 0);
}

/**
 * Generate 'createStream' call and send it to the server. It should make
 * the server allocate some channel for media streams.
 */
static int gen_create_stream(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt;
    uint8_t *p;
    int ret;

    av_log(s, AV_LOG_DEBUG, "Creating stream...\n");

    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_SYSTEM_CHANNEL, RTMP_PT_INVOKE,
                                     0, 25)) < 0)
        return ret;

    p = pkt.data;
    ff_amf_write_string(&p, "createStream");
    ff_amf_write_number(&p, ++rt->nb_invokes);
    ff_amf_write_null(&p);

    return rtmp_send_packet(rt, &pkt, 1);
}


/**
 * Generate 'deleteStream' call and send it to the server. It should make
 * the server remove some channel for media streams.
 */
static int gen_delete_stream(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt;
    uint8_t *p;
    int ret;

    av_log(s, AV_LOG_DEBUG, "Deleting stream...\n");

    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_SYSTEM_CHANNEL, RTMP_PT_INVOKE,
                                     0, 34)) < 0)
        return ret;

    p = pkt.data;
    ff_amf_write_string(&p, "deleteStream");
    ff_amf_write_number(&p, ++rt->nb_invokes);
    ff_amf_write_null(&p);
    ff_amf_write_number(&p, rt->stream_id);

    return rtmp_send_packet(rt, &pkt, 0);
}

/**
 * Generate client buffer time and send it to the server.
 */
static int gen_buffer_time(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt;
    uint8_t *p;
    int ret;

    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_NETWORK_CHANNEL, RTMP_PT_PING,
                                     1, 10)) < 0)
        return ret;

    p = pkt.data;
    bytestream_put_be16(&p, 3);
    bytestream_put_be32(&p, rt->stream_id);
    bytestream_put_be32(&p, rt->client_buffer_time);

    return rtmp_send_packet(rt, &pkt, 0);
}

/**
 * Generate 'play' call and send it to the server, then ping the server
 * to start actual playing.
 */
static int gen_play(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt;
    uint8_t *p;
    int ret;

    av_log(s, AV_LOG_DEBUG, "Sending play command for '%s'\n", rt->playpath);

    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_SOURCE_CHANNEL, RTMP_PT_INVOKE,
                                     0, 29 + strlen(rt->playpath))) < 0)
        return ret;

    pkt.extra = rt->stream_id;

    p = pkt.data;
    ff_amf_write_string(&p, "play");
    ff_amf_write_number(&p, ++rt->nb_invokes);
    ff_amf_write_null(&p);
    ff_amf_write_string(&p, rt->playpath);
    ff_amf_write_number(&p, rt->live * 1000);

    return rtmp_send_packet(rt, &pkt, 1);
}

static int gen_seek(URLContext *s, RTMPContext *rt, int64_t timestamp)
{
    RTMPPacket pkt;
    uint8_t *p;
    int ret;

    av_log(s, AV_LOG_DEBUG, "Sending seek command for timestamp %"PRId64"\n",
           timestamp);

    if ((ret = ff_rtmp_packet_create(&pkt, 3, RTMP_PT_INVOKE, 0, 26)) < 0)
        return ret;

    pkt.extra = rt->stream_id;

    p = pkt.data;
    ff_amf_write_string(&p, "seek");
    ff_amf_write_number(&p, 0); //no tracking back responses
    ff_amf_write_null(&p); //as usual, the first null param
    ff_amf_write_number(&p, timestamp); //where we want to jump

    return rtmp_send_packet(rt, &pkt, 1);
}

/**
 * Generate 'publish' call and send it to the server.
 */
static int gen_publish(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt;
    uint8_t *p;
    int ret;

    av_log(s, AV_LOG_DEBUG, "Sending publish command for '%s'\n", rt->playpath);

    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_SOURCE_CHANNEL, RTMP_PT_INVOKE,
                                     0, 30 + strlen(rt->playpath))) < 0)
        return ret;

    pkt.extra = rt->stream_id;

    p = pkt.data;
    ff_amf_write_string(&p, "publish");
    ff_amf_write_number(&p, ++rt->nb_invokes);
    ff_amf_write_null(&p);
    ff_amf_write_string(&p, rt->playpath);
    ff_amf_write_string(&p, "live");

    return rtmp_send_packet(rt, &pkt, 1);
}

/**
 * Generate ping reply and send it to the server.
 */
static int gen_pong(URLContext *s, RTMPContext *rt, RTMPPacket *ppkt)
{
    RTMPPacket pkt;
    uint8_t *p;
    int ret;

    if (ppkt->size < 6) {
        av_log(s, AV_LOG_ERROR, "Too short ping packet (%d)\n",
               ppkt->size);
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_NETWORK_CHANNEL, RTMP_PT_PING,
                                     ppkt->timestamp + 1, 6)) < 0)
        return ret;

    p = pkt.data;
    bytestream_put_be16(&p, 7);
    bytestream_put_be32(&p, AV_RB32(ppkt->data+2));

    return rtmp_send_packet(rt, &pkt, 0);
}

/**
 * Generate SWF verification message and send it to the server.
 */
static int gen_swf_verification(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt;
    uint8_t *p;
    int ret;

    av_log(s, AV_LOG_DEBUG, "Sending SWF verification...\n");
    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_NETWORK_CHANNEL, RTMP_PT_PING,
                                     0, 44)) < 0)
        return ret;

    p = pkt.data;
    bytestream_put_be16(&p, 27);
    memcpy(p, rt->swfverification, 42);

    return rtmp_send_packet(rt, &pkt, 0);
}

/**
 * Generate server bandwidth message and send it to the server.
 */
static int gen_server_bw(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt;
    uint8_t *p;
    int ret;

    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_NETWORK_CHANNEL, RTMP_PT_SERVER_BW,
                                     0, 4)) < 0)
        return ret;

    p = pkt.data;
    bytestream_put_be32(&p, rt->server_bw);

    return rtmp_send_packet(rt, &pkt, 0);
}

/**
 * Generate check bandwidth message and send it to the server.
 */
static int gen_check_bw(URLContext *s, RTMPContext *rt)
{
    RTMPPacket pkt;
    uint8_t *p;
    int ret;

    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_SYSTEM_CHANNEL, RTMP_PT_INVOKE,
                                     0, 21)) < 0)
        return ret;

    p = pkt.data;
    ff_amf_write_string(&p, "_checkbw");
    ff_amf_write_number(&p, ++rt->nb_invokes);
    ff_amf_write_null(&p);

    return rtmp_send_packet(rt, &pkt, 1);
}

/**
 * Generate report on bytes read so far and send it to the server.
 */
static int gen_bytes_read(URLContext *s, RTMPContext *rt, uint32_t ts)
{
    RTMPPacket pkt;
    uint8_t *p;
    int ret;

    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_NETWORK_CHANNEL, RTMP_PT_BYTES_READ,
                                     ts, 4)) < 0)
        return ret;

    p = pkt.data;
    bytestream_put_be32(&p, rt->bytes_read);

    return rtmp_send_packet(rt, &pkt, 0);
}

static int gen_fcsubscribe_stream(URLContext *s, RTMPContext *rt,
                                  const char *subscribe)
{
    RTMPPacket pkt;
    uint8_t *p;
    int ret;

    if ((ret = ff_rtmp_packet_create(&pkt, RTMP_SYSTEM_CHANNEL, RTMP_PT_INVOKE,
                                     0, 27 + strlen(subscribe))) < 0)
        return ret;

    p = pkt.data;
    ff_amf_write_string(&p, "FCSubscribe");
    ff_amf_write_number(&p, ++rt->nb_invokes);
    ff_amf_write_null(&p);
    ff_amf_write_string(&p, subscribe);

    return rtmp_send_packet(rt, &pkt, 1);
}

int ff_rtmp_calc_digest(const uint8_t *src, int len, int gap,
                        const uint8_t *key, int keylen, uint8_t *dst)
{
    struct AVSHA *sha;
    uint8_t hmac_buf[64+32] = {0};
    int i;

    sha = av_sha_alloc();
    if (!sha)
        return AVERROR(ENOMEM);

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

    return 0;
}

int ff_rtmp_calc_digest_pos(const uint8_t *buf, int off, int mod_val,
                            int add_val)
{
    int i, digest_pos = 0;

    for (i = 0; i < 4; i++)
        digest_pos += buf[i + off];
    digest_pos = digest_pos % mod_val + add_val;

    return digest_pos;
}

/**
 * Put HMAC-SHA2 digest of packet data (except for the bytes where this digest
 * will be stored) into that packet.
 *
 * @param buf handshake data (1536 bytes)
 * @param encrypted use an encrypted connection (RTMPE)
 * @return offset to the digest inside input data
 */
static int rtmp_handshake_imprint_with_digest(uint8_t *buf, int encrypted)
{
    int ret, digest_pos;

    if (encrypted)
        digest_pos = ff_rtmp_calc_digest_pos(buf, 772, 728, 776);
    else
        digest_pos = ff_rtmp_calc_digest_pos(buf, 8, 728, 12);

    ret = ff_rtmp_calc_digest(buf, RTMP_HANDSHAKE_PACKET_SIZE, digest_pos,
                              rtmp_player_key, PLAYER_KEY_OPEN_PART_LEN,
                              buf + digest_pos);
    if (ret < 0)
        return ret;

    return digest_pos;
}

/**
 * Verify that the received server response has the expected digest value.
 *
 * @param buf handshake data received from the server (1536 bytes)
 * @param off position to search digest offset from
 * @return 0 if digest is valid, digest position otherwise
 */
static int rtmp_validate_digest(uint8_t *buf, int off)
{
    uint8_t digest[32];
    int ret, digest_pos;

    digest_pos = ff_rtmp_calc_digest_pos(buf, off, 728, off + 4);

    ret = ff_rtmp_calc_digest(buf, RTMP_HANDSHAKE_PACKET_SIZE, digest_pos,
                              rtmp_server_key, SERVER_KEY_OPEN_PART_LEN,
                              digest);
    if (ret < 0)
        return ret;

    if (!memcmp(digest, buf + digest_pos, 32))
        return digest_pos;
    return 0;
}

static int rtmp_calc_swf_verification(URLContext *s, RTMPContext *rt,
                                      uint8_t *buf)
{
    uint8_t *p;
    int ret;

    if (rt->swfhash_len != 32) {
        av_log(s, AV_LOG_ERROR,
               "Hash of the decompressed SWF file is not 32 bytes long.\n");
        return AVERROR(EINVAL);
    }

    p = &rt->swfverification[0];
    bytestream_put_byte(&p, 1);
    bytestream_put_byte(&p, 1);
    bytestream_put_be32(&p, rt->swfsize);
    bytestream_put_be32(&p, rt->swfsize);

    if ((ret = ff_rtmp_calc_digest(rt->swfhash, 32, 0, buf, 32, p)) < 0)
        return ret;

    return 0;
}

#if CONFIG_ZLIB
static int rtmp_uncompress_swfplayer(uint8_t *in_data, int64_t in_size,
                                     uint8_t **out_data, int64_t *out_size)
{
    z_stream zs = { 0 };
    void *ptr;
    int size;
    int ret = 0;

    zs.avail_in = in_size;
    zs.next_in  = in_data;
    ret = inflateInit(&zs);
    if (ret != Z_OK)
        return AVERROR_UNKNOWN;

    do {
        uint8_t tmp_buf[16384];

        zs.avail_out = sizeof(tmp_buf);
        zs.next_out  = tmp_buf;

        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            ret = AVERROR_UNKNOWN;
            goto fail;
        }

        size = sizeof(tmp_buf) - zs.avail_out;
        if (!(ptr = av_realloc(*out_data, *out_size + size))) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        *out_data = ptr;

        memcpy(*out_data + *out_size, tmp_buf, size);
        *out_size += size;
    } while (zs.avail_out == 0);

fail:
    inflateEnd(&zs);
    return ret;
}
#endif

static int rtmp_calc_swfhash(URLContext *s)
{
    RTMPContext *rt = s->priv_data;
    uint8_t *in_data = NULL, *out_data = NULL, *swfdata;
    int64_t in_size, out_size;
    URLContext *stream;
    char swfhash[32];
    int swfsize;
    int ret = 0;

    /* Get the SWF player file. */
    if ((ret = ffurl_open(&stream, rt->swfverify, AVIO_FLAG_READ,
                          &s->interrupt_callback, NULL)) < 0) {
        av_log(s, AV_LOG_ERROR, "Cannot open connection %s.\n", rt->swfverify);
        goto fail;
    }

    if ((in_size = ffurl_seek(stream, 0, AVSEEK_SIZE)) < 0) {
        ret = AVERROR(EIO);
        goto fail;
    }

    if (!(in_data = av_malloc(in_size))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if ((ret = ffurl_read_complete(stream, in_data, in_size)) < 0)
        goto fail;

    if (in_size < 3) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    if (!memcmp(in_data, "CWS", 3)) {
        /* Decompress the SWF player file using Zlib. */
        if (!(out_data = av_malloc(8))) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        *in_data = 'F'; // magic stuff
        memcpy(out_data, in_data, 8);
        out_size = 8;

#if CONFIG_ZLIB
        if ((ret = rtmp_uncompress_swfplayer(in_data + 8, in_size - 8,
                                             &out_data, &out_size)) < 0)
            goto fail;
#else
        av_log(s, AV_LOG_ERROR,
               "Zlib is required for decompressing the SWF player file.\n");
        ret = AVERROR(EINVAL);
        goto fail;
#endif
        swfsize = out_size;
        swfdata = out_data;
    } else {
        swfsize = in_size;
        swfdata = in_data;
    }

    /* Compute the SHA256 hash of the SWF player file. */
    if ((ret = ff_rtmp_calc_digest(swfdata, swfsize, 0,
                                   "Genuine Adobe Flash Player 001", 30,
                                   swfhash)) < 0)
        goto fail;

    /* Set SWFVerification parameters. */
    av_opt_set_bin(rt, "rtmp_swfhash", swfhash, 32, 0);
    rt->swfsize = swfsize;

fail:
    av_freep(&in_data);
    av_freep(&out_data);
    ffurl_close(stream);
    return ret;
}

/**
 * Perform handshake with the server by means of exchanging pseudorandom data
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
    uint8_t digest[32], signature[32];
    int ret, type = 0;

    av_log(s, AV_LOG_DEBUG, "Handshaking...\n");

    av_lfg_init(&rnd, 0xDEADC0DE);
    // generate handshake packet - 1536 bytes of pseudorandom data
    for (i = 9; i <= RTMP_HANDSHAKE_PACKET_SIZE; i++)
        tosend[i] = av_lfg_get(&rnd) >> 24;

    if (CONFIG_FFRTMPCRYPT_PROTOCOL && rt->encrypted) {
        /* When the client wants to use RTMPE, we have to change the command
         * byte to 0x06 which means to use encrypted data and we have to set
         * the flash version to at least 9.0.115.0. */
        tosend[0] = 6;
        tosend[5] = 128;
        tosend[6] = 0;
        tosend[7] = 3;
        tosend[8] = 2;

        /* Initialize the Diffie-Hellmann context and generate the public key
         * to send to the server. */
        if ((ret = ff_rtmpe_gen_pub_key(rt->stream, tosend + 1)) < 0)
            return ret;
    }

    client_pos = rtmp_handshake_imprint_with_digest(tosend + 1, rt->encrypted);
    if (client_pos < 0)
        return client_pos;

    if ((ret = ffurl_write(rt->stream, tosend,
                           RTMP_HANDSHAKE_PACKET_SIZE + 1)) < 0) {
        av_log(s, AV_LOG_ERROR, "Cannot write RTMP handshake request\n");
        return ret;
    }

    if ((ret = ffurl_read_complete(rt->stream, serverdata,
                                   RTMP_HANDSHAKE_PACKET_SIZE + 1)) < 0) {
        av_log(s, AV_LOG_ERROR, "Cannot read RTMP handshake response\n");
        return ret;
    }

    if ((ret = ffurl_read_complete(rt->stream, clientdata,
                                   RTMP_HANDSHAKE_PACKET_SIZE)) < 0) {
        av_log(s, AV_LOG_ERROR, "Cannot read RTMP handshake response\n");
        return ret;
    }

    av_log(s, AV_LOG_DEBUG, "Type answer %d\n", serverdata[0]);
    av_log(s, AV_LOG_DEBUG, "Server version %d.%d.%d.%d\n",
           serverdata[5], serverdata[6], serverdata[7], serverdata[8]);

    if (rt->is_input && serverdata[5] >= 3) {
        server_pos = rtmp_validate_digest(serverdata + 1, 772);
        if (server_pos < 0)
            return server_pos;

        if (!server_pos) {
            type = 1;
            server_pos = rtmp_validate_digest(serverdata + 1, 8);
            if (server_pos < 0)
                return server_pos;

            if (!server_pos) {
                av_log(s, AV_LOG_ERROR, "Server response validating failed\n");
                return AVERROR(EIO);
            }
        }

        /* Generate SWFVerification token (SHA256 HMAC hash of decompressed SWF,
         * key are the last 32 bytes of the server handshake. */
        if (rt->swfsize) {
            if ((ret = rtmp_calc_swf_verification(s, rt, serverdata + 1 +
                                                  RTMP_HANDSHAKE_PACKET_SIZE - 32)) < 0)
                return ret;
        }

        ret = ff_rtmp_calc_digest(tosend + 1 + client_pos, 32, 0,
                                  rtmp_server_key, sizeof(rtmp_server_key),
                                  digest);
        if (ret < 0)
            return ret;

        ret = ff_rtmp_calc_digest(clientdata, RTMP_HANDSHAKE_PACKET_SIZE - 32,
                                  0, digest, 32, signature);
        if (ret < 0)
            return ret;

        if (CONFIG_FFRTMPCRYPT_PROTOCOL && rt->encrypted) {
            /* Compute the shared secret key sent by the server and initialize
             * the RC4 encryption. */
            if ((ret = ff_rtmpe_compute_secret_key(rt->stream, serverdata + 1,
                                                   tosend + 1, type)) < 0)
                return ret;

            /* Encrypt the signature received by the server. */
            ff_rtmpe_encrypt_sig(rt->stream, signature, digest, serverdata[0]);
        }

        if (memcmp(signature, clientdata + RTMP_HANDSHAKE_PACKET_SIZE - 32, 32)) {
            av_log(s, AV_LOG_ERROR, "Signature mismatch\n");
            return AVERROR(EIO);
        }

        for (i = 0; i < RTMP_HANDSHAKE_PACKET_SIZE; i++)
            tosend[i] = av_lfg_get(&rnd) >> 24;
        ret = ff_rtmp_calc_digest(serverdata + 1 + server_pos, 32, 0,
                                  rtmp_player_key, sizeof(rtmp_player_key),
                                  digest);
        if (ret < 0)
            return ret;

        ret = ff_rtmp_calc_digest(tosend, RTMP_HANDSHAKE_PACKET_SIZE - 32, 0,
                                  digest, 32,
                                  tosend + RTMP_HANDSHAKE_PACKET_SIZE - 32);
        if (ret < 0)
            return ret;

        if (CONFIG_FFRTMPCRYPT_PROTOCOL && rt->encrypted) {
            /* Encrypt the signature to be send to the server. */
            ff_rtmpe_encrypt_sig(rt->stream, tosend +
                                 RTMP_HANDSHAKE_PACKET_SIZE - 32, digest,
                                 serverdata[0]);
        }

        // write reply back to the server
        if ((ret = ffurl_write(rt->stream, tosend,
                               RTMP_HANDSHAKE_PACKET_SIZE)) < 0)
            return ret;

        if (CONFIG_FFRTMPCRYPT_PROTOCOL && rt->encrypted) {
            /* Set RC4 keys for encryption and update the keystreams. */
            if ((ret = ff_rtmpe_update_keystream(rt->stream)) < 0)
                return ret;
        }
    } else {
        if (CONFIG_FFRTMPCRYPT_PROTOCOL && rt->encrypted) {
            /* Compute the shared secret key sent by the server and initialize
             * the RC4 encryption. */
            if ((ret = ff_rtmpe_compute_secret_key(rt->stream, serverdata + 1,
                            tosend + 1, 1)) < 0)
                return ret;

            if (serverdata[0] == 9) {
                /* Encrypt the signature received by the server. */
                ff_rtmpe_encrypt_sig(rt->stream, signature, digest,
                                     serverdata[0]);
            }
        }

        if ((ret = ffurl_write(rt->stream, serverdata + 1,
                               RTMP_HANDSHAKE_PACKET_SIZE)) < 0)
            return ret;

        if (CONFIG_FFRTMPCRYPT_PROTOCOL && rt->encrypted) {
            /* Set RC4 keys for encryption and update the keystreams. */
            if ((ret = ff_rtmpe_update_keystream(rt->stream)) < 0)
                return ret;
        }
    }

    return 0;
}

static int rtmp_receive_hs_packet(RTMPContext* rt, uint32_t *first_int,
                                  uint32_t *second_int, char *arraydata,
                                  int size)
{
    int inoutsize;

    inoutsize = ffurl_read_complete(rt->stream, arraydata,
                                    RTMP_HANDSHAKE_PACKET_SIZE);
    if (inoutsize <= 0)
        return AVERROR(EIO);
    if (inoutsize != RTMP_HANDSHAKE_PACKET_SIZE) {
        av_log(rt, AV_LOG_ERROR, "Erroneous Message size %d"
               " not following standard\n", (int)inoutsize);
        return AVERROR(EINVAL);
    }

    *first_int  = AV_RB32(arraydata);
    *second_int = AV_RB32(arraydata + 4);
    return 0;
}

static int rtmp_send_hs_packet(RTMPContext* rt, uint32_t first_int,
                               uint32_t second_int, char *arraydata, int size)
{
    int inoutsize;

    AV_WB32(arraydata, first_int);
    AV_WB32(arraydata + 4, second_int);
    inoutsize = ffurl_write(rt->stream, arraydata,
                            RTMP_HANDSHAKE_PACKET_SIZE);
    if (inoutsize != RTMP_HANDSHAKE_PACKET_SIZE) {
        av_log(rt, AV_LOG_ERROR, "Unable to write answer\n");
        return AVERROR(EIO);
    }

    return 0;
}

/**
 * rtmp handshake server side
 */
static int rtmp_server_handshake(URLContext *s, RTMPContext *rt)
{
    uint8_t buffer[RTMP_HANDSHAKE_PACKET_SIZE];
    uint32_t hs_epoch;
    uint32_t hs_my_epoch;
    uint8_t hs_c1[RTMP_HANDSHAKE_PACKET_SIZE];
    uint8_t hs_s1[RTMP_HANDSHAKE_PACKET_SIZE];
    uint32_t zeroes;
    uint32_t temp       = 0;
    int randomidx       = 0;
    int inoutsize       = 0;
    int ret;

    inoutsize = ffurl_read_complete(rt->stream, buffer, 1);       // Receive C0
    if (inoutsize <= 0) {
        av_log(s, AV_LOG_ERROR, "Unable to read handshake\n");
        return AVERROR(EIO);
    }
    // Check Version
    if (buffer[0] != 3) {
        av_log(s, AV_LOG_ERROR, "RTMP protocol version mismatch\n");
        return AVERROR(EIO);
    }
    if (ffurl_write(rt->stream, buffer, 1) <= 0) {                 // Send S0
        av_log(s, AV_LOG_ERROR,
               "Unable to write answer - RTMP S0\n");
        return AVERROR(EIO);
    }
    /* Receive C1 */
    ret = rtmp_receive_hs_packet(rt, &hs_epoch, &zeroes, hs_c1,
                                 RTMP_HANDSHAKE_PACKET_SIZE);
    if (ret) {
        av_log(s, AV_LOG_ERROR, "RTMP Handshake C1 Error\n");
        return ret;
    }
    /* Send S1 */
    /* By now same epoch will be sent */
    hs_my_epoch = hs_epoch;
    /* Generate random */
    for (randomidx = 8; randomidx < (RTMP_HANDSHAKE_PACKET_SIZE);
         randomidx += 4)
        AV_WB32(hs_s1 + randomidx, av_get_random_seed());

    ret = rtmp_send_hs_packet(rt, hs_my_epoch, 0, hs_s1,
                              RTMP_HANDSHAKE_PACKET_SIZE);
    if (ret) {
        av_log(s, AV_LOG_ERROR, "RTMP Handshake S1 Error\n");
        return ret;
    }
    /* Send S2 */
    ret = rtmp_send_hs_packet(rt, hs_epoch, 0, hs_c1,
                              RTMP_HANDSHAKE_PACKET_SIZE);
    if (ret) {
        av_log(s, AV_LOG_ERROR, "RTMP Handshake S2 Error\n");
        return ret;
    }
    /* Receive C2 */
    ret = rtmp_receive_hs_packet(rt, &temp, &zeroes, buffer,
                                 RTMP_HANDSHAKE_PACKET_SIZE);
    if (ret) {
        av_log(s, AV_LOG_ERROR, "RTMP Handshake C2 Error\n");
        return ret;
    }
    if (temp != hs_my_epoch)
        av_log(s, AV_LOG_WARNING,
               "Erroneous C2 Message epoch does not match up with C1 epoch\n");
    if (memcmp(buffer + 8, hs_s1 + 8,
               RTMP_HANDSHAKE_PACKET_SIZE - 8))
        av_log(s, AV_LOG_WARNING,
               "Erroneous C2 Message random does not match up\n");

    return 0;
}

static int handle_chunk_size(URLContext *s, RTMPPacket *pkt)
{
    RTMPContext *rt = s->priv_data;
    int ret;

    if (pkt->size < 4) {
        av_log(s, AV_LOG_ERROR,
               "Too short chunk size change packet (%d)\n",
               pkt->size);
        return AVERROR_INVALIDDATA;
    }

    if (!rt->is_input) {
        /* Send the same chunk size change packet back to the server,
         * setting the outgoing chunk size to the same as the incoming one. */
        if ((ret = ff_rtmp_packet_write(rt->stream, pkt, rt->out_chunk_size,
                                        rt->prev_pkt[1])) < 0)
            return ret;
        rt->out_chunk_size = AV_RB32(pkt->data);
    }

    rt->in_chunk_size = AV_RB32(pkt->data);
    if (rt->in_chunk_size <= 0) {
        av_log(s, AV_LOG_ERROR, "Incorrect chunk size %d\n",
               rt->in_chunk_size);
        return AVERROR_INVALIDDATA;
    }
    av_log(s, AV_LOG_DEBUG, "New incoming chunk size = %d\n",
           rt->in_chunk_size);

    return 0;
}

static int handle_ping(URLContext *s, RTMPPacket *pkt)
{
    RTMPContext *rt = s->priv_data;
    int t, ret;

    if (pkt->size < 2) {
        av_log(s, AV_LOG_ERROR, "Too short ping packet (%d)\n",
               pkt->size);
        return AVERROR_INVALIDDATA;
    }

    t = AV_RB16(pkt->data);
    if (t == 6) {
        if ((ret = gen_pong(s, rt, pkt)) < 0)
            return ret;
    } else if (t == 26) {
        if (rt->swfsize) {
            if ((ret = gen_swf_verification(s, rt)) < 0)
                return ret;
        } else {
            av_log(s, AV_LOG_WARNING, "Ignoring SWFVerification request.\n");
        }
    }

    return 0;
}

static int handle_client_bw(URLContext *s, RTMPPacket *pkt)
{
    RTMPContext *rt = s->priv_data;

    if (pkt->size < 4) {
        av_log(s, AV_LOG_ERROR,
               "Client bandwidth report packet is less than 4 bytes long (%d)\n",
               pkt->size);
        return AVERROR_INVALIDDATA;
    }

    rt->client_report_size = AV_RB32(pkt->data);
    if (rt->client_report_size <= 0) {
        av_log(s, AV_LOG_ERROR, "Incorrect client bandwidth %d\n",
                rt->client_report_size);
        return AVERROR_INVALIDDATA;

    }
    av_log(s, AV_LOG_DEBUG, "Client bandwidth = %d\n", rt->client_report_size);
    rt->client_report_size >>= 1;

    return 0;
}

static int handle_server_bw(URLContext *s, RTMPPacket *pkt)
{
    RTMPContext *rt = s->priv_data;

    if (pkt->size < 4) {
        av_log(s, AV_LOG_ERROR,
               "Too short server bandwidth report packet (%d)\n",
               pkt->size);
        return AVERROR_INVALIDDATA;
    }

    rt->server_bw = AV_RB32(pkt->data);
    if (rt->server_bw <= 0) {
        av_log(s, AV_LOG_ERROR, "Incorrect server bandwidth %d\n",
               rt->server_bw);
        return AVERROR_INVALIDDATA;
    }
    av_log(s, AV_LOG_DEBUG, "Server bandwidth = %d\n", rt->server_bw);

    return 0;
}

static int do_adobe_auth(RTMPContext *rt, const char *user, const char *salt,
                         const char *opaque, const char *challenge)
{
    uint8_t hash[16];
    char hashstr[AV_BASE64_SIZE(sizeof(hash))], challenge2[10];
    struct AVMD5 *md5 = av_md5_alloc();
    if (!md5)
        return AVERROR(ENOMEM);

    snprintf(challenge2, sizeof(challenge2), "%08x", av_get_random_seed());

    av_md5_init(md5);
    av_md5_update(md5, user, strlen(user));
    av_md5_update(md5, salt, strlen(salt));
    av_md5_update(md5, rt->password, strlen(rt->password));
    av_md5_final(md5, hash);
    av_base64_encode(hashstr, sizeof(hashstr), hash,
                     sizeof(hash));
    av_md5_init(md5);
    av_md5_update(md5, hashstr, strlen(hashstr));
    if (opaque)
        av_md5_update(md5, opaque, strlen(opaque));
    else if (challenge)
        av_md5_update(md5, challenge, strlen(challenge));
    av_md5_update(md5, challenge2, strlen(challenge2));
    av_md5_final(md5, hash);
    av_base64_encode(hashstr, sizeof(hashstr), hash,
                     sizeof(hash));
    snprintf(rt->auth_params, sizeof(rt->auth_params),
             "?authmod=%s&user=%s&challenge=%s&response=%s",
             "adobe", user, challenge2, hashstr);
    if (opaque)
        av_strlcatf(rt->auth_params, sizeof(rt->auth_params),
                    "&opaque=%s", opaque);

    av_free(md5);
    return 0;
}

static int do_llnw_auth(RTMPContext *rt, const char *user, const char *nonce)
{
    uint8_t hash[16];
    char hashstr1[33], hashstr2[33];
    const char *realm = "live";
    const char *method = "publish";
    const char *qop = "auth";
    const char *nc = "00000001";
    char cnonce[10];
    struct AVMD5 *md5 = av_md5_alloc();
    if (!md5)
        return AVERROR(ENOMEM);

    snprintf(cnonce, sizeof(cnonce), "%08x", av_get_random_seed());

    av_md5_init(md5);
    av_md5_update(md5, user, strlen(user));
    av_md5_update(md5, ":", 1);
    av_md5_update(md5, realm, strlen(realm));
    av_md5_update(md5, ":", 1);
    av_md5_update(md5, rt->password, strlen(rt->password));
    av_md5_final(md5, hash);
    ff_data_to_hex(hashstr1, hash, 16, 1);
    hashstr1[32] = '\0';

    av_md5_init(md5);
    av_md5_update(md5, method, strlen(method));
    av_md5_update(md5, ":/", 2);
    av_md5_update(md5, rt->app, strlen(rt->app));
    if (!strchr(rt->app, '/'))
        av_md5_update(md5, "/_definst_", strlen("/_definst_"));
    av_md5_final(md5, hash);
    ff_data_to_hex(hashstr2, hash, 16, 1);
    hashstr2[32] = '\0';

    av_md5_init(md5);
    av_md5_update(md5, hashstr1, strlen(hashstr1));
    av_md5_update(md5, ":", 1);
    if (nonce)
        av_md5_update(md5, nonce, strlen(nonce));
    av_md5_update(md5, ":", 1);
    av_md5_update(md5, nc, strlen(nc));
    av_md5_update(md5, ":", 1);
    av_md5_update(md5, cnonce, strlen(cnonce));
    av_md5_update(md5, ":", 1);
    av_md5_update(md5, qop, strlen(qop));
    av_md5_update(md5, ":", 1);
    av_md5_update(md5, hashstr2, strlen(hashstr2));
    av_md5_final(md5, hash);
    ff_data_to_hex(hashstr1, hash, 16, 1);

    snprintf(rt->auth_params, sizeof(rt->auth_params),
             "?authmod=%s&user=%s&nonce=%s&cnonce=%s&nc=%s&response=%s",
             "llnw", user, nonce, cnonce, nc, hashstr1);

    av_free(md5);
    return 0;
}

static int handle_connect_error(URLContext *s, const char *desc)
{
    RTMPContext *rt = s->priv_data;
    char buf[300], *ptr, authmod[15];
    int i = 0, ret = 0;
    const char *user = "", *salt = "", *opaque = NULL,
               *challenge = NULL, *cptr = NULL, *nonce = NULL;

    if (!(cptr = strstr(desc, "authmod=adobe")) &&
        !(cptr = strstr(desc, "authmod=llnw"))) {
        av_log(s, AV_LOG_ERROR,
               "Unknown connect error (unsupported authentication method?)\n");
        return AVERROR_UNKNOWN;
    }
    cptr += strlen("authmod=");
    while (*cptr && *cptr != ' ' && i < sizeof(authmod) - 1)
        authmod[i++] = *cptr++;
    authmod[i] = '\0';

    if (!rt->username[0] || !rt->password[0]) {
        av_log(s, AV_LOG_ERROR, "No credentials set\n");
        return AVERROR_UNKNOWN;
    }

    if (strstr(desc, "?reason=authfailed")) {
        av_log(s, AV_LOG_ERROR, "Incorrect username/password\n");
        return AVERROR_UNKNOWN;
    } else if (strstr(desc, "?reason=nosuchuser")) {
        av_log(s, AV_LOG_ERROR, "Incorrect username\n");
        return AVERROR_UNKNOWN;
    }

    if (rt->auth_tried) {
        av_log(s, AV_LOG_ERROR, "Authentication failed\n");
        return AVERROR_UNKNOWN;
    }

    rt->auth_params[0] = '\0';

    if (strstr(desc, "code=403 need auth")) {
        snprintf(rt->auth_params, sizeof(rt->auth_params),
                 "?authmod=%s&user=%s", authmod, rt->username);
        return 0;
    }

    if (!(cptr = strstr(desc, "?reason=needauth"))) {
        av_log(s, AV_LOG_ERROR, "No auth parameters found\n");
        return AVERROR_UNKNOWN;
    }

    av_strlcpy(buf, cptr + 1, sizeof(buf));
    ptr = buf;

    while (ptr) {
        char *next  = strchr(ptr, '&');
        char *value = strchr(ptr, '=');
        if (next)
            *next++ = '\0';
        if (value)
            *value++ = '\0';
        if (!strcmp(ptr, "user")) {
            user = value;
        } else if (!strcmp(ptr, "salt")) {
            salt = value;
        } else if (!strcmp(ptr, "opaque")) {
            opaque = value;
        } else if (!strcmp(ptr, "challenge")) {
            challenge = value;
        } else if (!strcmp(ptr, "nonce")) {
            nonce = value;
        }
        ptr = next;
    }

    if (!strcmp(authmod, "adobe")) {
        if ((ret = do_adobe_auth(rt, user, salt, opaque, challenge)) < 0)
            return ret;
    } else {
        if ((ret = do_llnw_auth(rt, user, nonce)) < 0)
            return ret;
    }

    rt->auth_tried = 1;
    return 0;
}

static int handle_invoke_error(URLContext *s, RTMPPacket *pkt)
{
    RTMPContext *rt = s->priv_data;
    const uint8_t *data_end = pkt->data + pkt->size;
    char *tracked_method = NULL;
    int level = AV_LOG_ERROR;
    uint8_t tmpstr[256];
    int ret;

    if ((ret = find_tracked_method(s, pkt, 9, &tracked_method)) < 0)
        return ret;

    if (!ff_amf_get_field_value(pkt->data + 9, data_end,
                                "description", tmpstr, sizeof(tmpstr))) {
        if (tracked_method && (!strcmp(tracked_method, "_checkbw")      ||
                               !strcmp(tracked_method, "releaseStream") ||
                               !strcmp(tracked_method, "FCSubscribe")   ||
                               !strcmp(tracked_method, "FCPublish"))) {
            /* Gracefully ignore Adobe-specific historical artifact errors. */
            level = AV_LOG_WARNING;
            ret = 0;
        } else if (tracked_method && !strcmp(tracked_method, "connect")) {
            ret = handle_connect_error(s, tmpstr);
            if (!ret) {
                rt->do_reconnect = 1;
                level = AV_LOG_VERBOSE;
            }
        } else
            ret = AVERROR_UNKNOWN;
        av_log(s, level, "Server error: %s\n", tmpstr);
    }

    av_free(tracked_method);
    return ret;
}

static int write_begin(URLContext *s)
{
    RTMPContext *rt = s->priv_data;
    PutByteContext pbc;
    RTMPPacket spkt = { 0 };
    int ret;

    // Send Stream Begin 1
    if ((ret = ff_rtmp_packet_create(&spkt, RTMP_NETWORK_CHANNEL,
                                     RTMP_PT_PING, 0, 6)) < 0) {
        av_log(s, AV_LOG_ERROR, "Unable to create response packet\n");
        return ret;
    }

    bytestream2_init_writer(&pbc, spkt.data, spkt.size);
    bytestream2_put_be16(&pbc, 0);          // 0 -> Stream Begin
    bytestream2_put_be32(&pbc, rt->nb_streamid);

    ret = ff_rtmp_packet_write(rt->stream, &spkt, rt->out_chunk_size,
                               rt->prev_pkt[1]);

    ff_rtmp_packet_destroy(&spkt);

    return ret;
}

static int write_status(URLContext *s, RTMPPacket *pkt,
                        const char *status, const char *filename)
{
    RTMPContext *rt = s->priv_data;
    RTMPPacket spkt = { 0 };
    char statusmsg[128];
    uint8_t *pp;
    int ret;

    if ((ret = ff_rtmp_packet_create(&spkt, RTMP_SYSTEM_CHANNEL,
                                     RTMP_PT_INVOKE, 0,
                                     RTMP_PKTDATA_DEFAULT_SIZE)) < 0) {
        av_log(s, AV_LOG_ERROR, "Unable to create response packet\n");
        return ret;
    }

    pp = spkt.data;
    spkt.extra = pkt->extra;
    ff_amf_write_string(&pp, "onStatus");
    ff_amf_write_number(&pp, 0);
    ff_amf_write_null(&pp);

    ff_amf_write_object_start(&pp);
    ff_amf_write_field_name(&pp, "level");
    ff_amf_write_string(&pp, "status");
    ff_amf_write_field_name(&pp, "code");
    ff_amf_write_string(&pp, status);
    ff_amf_write_field_name(&pp, "description");
    snprintf(statusmsg, sizeof(statusmsg),
             "%s is now published", filename);
    ff_amf_write_string(&pp, statusmsg);
    ff_amf_write_field_name(&pp, "details");
    ff_amf_write_string(&pp, filename);
    ff_amf_write_field_name(&pp, "clientid");
    snprintf(statusmsg, sizeof(statusmsg), "%s", LIBAVFORMAT_IDENT);
    ff_amf_write_string(&pp, statusmsg);
    ff_amf_write_object_end(&pp);

    spkt.size = pp - spkt.data;
    ret = ff_rtmp_packet_write(rt->stream, &spkt, rt->out_chunk_size,
                               rt->prev_pkt[1]);
    ff_rtmp_packet_destroy(&spkt);

    return ret;
}

static int send_invoke_response(URLContext *s, RTMPPacket *pkt)
{
    RTMPContext *rt = s->priv_data;
    double seqnum;
    char filename[64];
    char command[64];
    int stringlen;
    char *pchar;
    const uint8_t *p = pkt->data;
    uint8_t *pp      = NULL;
    RTMPPacket spkt  = { 0 };
    GetByteContext gbc;
    int ret;

    bytestream2_init(&gbc, p, pkt->size);
    if (ff_amf_read_string(&gbc, command, sizeof(command),
                           &stringlen)) {
        av_log(s, AV_LOG_ERROR, "Error in PT_INVOKE\n");
        return AVERROR_INVALIDDATA;
    }

    ret = ff_amf_read_number(&gbc, &seqnum);
    if (ret)
        return ret;
    ret = ff_amf_read_null(&gbc);
    if (ret)
        return ret;
    if (!strcmp(command, "FCPublish") ||
        !strcmp(command, "publish")) {
        ret = ff_amf_read_string(&gbc, filename,
                                 sizeof(filename), &stringlen);
        // check with url
        if (s->filename) {
            pchar = strrchr(s->filename, '/');
            if (!pchar) {
                av_log(s, AV_LOG_WARNING,
                       "Unable to find / in url %s, bad format\n",
                       s->filename);
                pchar = s->filename;
            }
            pchar++;
            if (strcmp(pchar, filename))
                av_log(s, AV_LOG_WARNING, "Unexpected stream %s, expecting"
                       " %s\n", filename, pchar);
        }
        rt->state = STATE_RECEIVING;
    }

    if (!strcmp(command, "FCPublish")) {
        if ((ret = ff_rtmp_packet_create(&spkt, RTMP_SYSTEM_CHANNEL,
                                         RTMP_PT_INVOKE, 0,
                                         RTMP_PKTDATA_DEFAULT_SIZE)) < 0) {
            av_log(s, AV_LOG_ERROR, "Unable to create response packet\n");
            return ret;
        }
        pp = spkt.data;
        ff_amf_write_string(&pp, "onFCPublish");
    } else if (!strcmp(command, "publish")) {
        ret = write_begin(s);
        if (ret < 0)
            return ret;

        // Send onStatus(NetStream.Publish.Start)
        return write_status(s, pkt, "NetStream.Publish.Start",
                           filename);
    } else if (!strcmp(command, "play")) {
        ret = write_begin(s);
        if (ret < 0)
            return ret;
        rt->state = STATE_SENDING;
        return write_status(s, pkt, "NetStream.Play.Start",
                            filename);
    } else {
        if ((ret = ff_rtmp_packet_create(&spkt, RTMP_SYSTEM_CHANNEL,
                                         RTMP_PT_INVOKE, 0,
                                         RTMP_PKTDATA_DEFAULT_SIZE)) < 0) {
            av_log(s, AV_LOG_ERROR, "Unable to create response packet\n");
            return ret;
        }
        pp = spkt.data;
        ff_amf_write_string(&pp, "_result");
        ff_amf_write_number(&pp, seqnum);
        ff_amf_write_null(&pp);
        if (!strcmp(command, "createStream")) {
            rt->nb_streamid++;
            if (rt->nb_streamid == 0 || rt->nb_streamid == 2)
                rt->nb_streamid++; /* Values 0 and 2 are reserved */
            ff_amf_write_number(&pp, rt->nb_streamid);
            /* By now we don't control which streams are removed in
             * deleteStream. There is no stream creation control
             * if a client creates more than 2^32 - 2 streams. */
        }
    }
    spkt.size = pp - spkt.data;
    ret = ff_rtmp_packet_write(rt->stream, &spkt, rt->out_chunk_size,
                               rt->prev_pkt[1]);
    ff_rtmp_packet_destroy(&spkt);
    return ret;
}

static int handle_invoke_result(URLContext *s, RTMPPacket *pkt)
{
    RTMPContext *rt = s->priv_data;
    char *tracked_method = NULL;
    int ret = 0;

    if ((ret = find_tracked_method(s, pkt, 10, &tracked_method)) < 0)
        return ret;

    if (!tracked_method) {
        /* Ignore this reply when the current method is not tracked. */
        return ret;
    }

    if (!strcmp(tracked_method, "connect")) {
        if (!rt->is_input) {
            if ((ret = gen_release_stream(s, rt)) < 0)
                goto fail;

            if ((ret = gen_fcpublish_stream(s, rt)) < 0)
                goto fail;
        } else {
            if ((ret = gen_server_bw(s, rt)) < 0)
                goto fail;
        }

        if ((ret = gen_create_stream(s, rt)) < 0)
            goto fail;

        if (rt->is_input) {
            /* Send the FCSubscribe command when the name of live
             * stream is defined by the user or if it's a live stream. */
            if (rt->subscribe) {
                if ((ret = gen_fcsubscribe_stream(s, rt, rt->subscribe)) < 0)
                    goto fail;
            } else if (rt->live == -1) {
                if ((ret = gen_fcsubscribe_stream(s, rt, rt->playpath)) < 0)
                    goto fail;
            }
        }
    } else if (!strcmp(tracked_method, "createStream")) {
        //extract a number from the result
        if (pkt->data[10] || pkt->data[19] != 5 || pkt->data[20]) {
            av_log(s, AV_LOG_WARNING, "Unexpected reply on connect()\n");
        } else {
            rt->stream_id = av_int2double(AV_RB64(pkt->data + 21));
        }

        if (!rt->is_input) {
            if ((ret = gen_publish(s, rt)) < 0)
                goto fail;
        } else {
            if ((ret = gen_play(s, rt)) < 0)
                goto fail;
            if ((ret = gen_buffer_time(s, rt)) < 0)
                goto fail;
        }
    }

fail:
    av_free(tracked_method);
    return ret;
}

static int handle_invoke_status(URLContext *s, RTMPPacket *pkt)
{
    RTMPContext *rt = s->priv_data;
    const uint8_t *data_end = pkt->data + pkt->size;
    const uint8_t *ptr = pkt->data + RTMP_HEADER;
    uint8_t tmpstr[256];
    int i, t;

    for (i = 0; i < 2; i++) {
        t = ff_amf_tag_size(ptr, data_end);
        if (t < 0)
            return 1;
        ptr += t;
    }

    t = ff_amf_get_field_value(ptr, data_end, "level", tmpstr, sizeof(tmpstr));
    if (!t && !strcmp(tmpstr, "error")) {
        t = ff_amf_get_field_value(ptr, data_end,
                                   "description", tmpstr, sizeof(tmpstr));
        if (t || !tmpstr[0])
            t = ff_amf_get_field_value(ptr, data_end, "code",
                                       tmpstr, sizeof(tmpstr));
        if (!t)
            av_log(s, AV_LOG_ERROR, "Server error: %s\n", tmpstr);
        return -1;
    }

    t = ff_amf_get_field_value(ptr, data_end, "code", tmpstr, sizeof(tmpstr));
    if (!t && !strcmp(tmpstr, "NetStream.Play.Start")) rt->state = STATE_PLAYING;
    if (!t && !strcmp(tmpstr, "NetStream.Play.Stop")) rt->state = STATE_STOPPED;
    if (!t && !strcmp(tmpstr, "NetStream.Play.UnpublishNotify")) rt->state = STATE_STOPPED;
    if (!t && !strcmp(tmpstr, "NetStream.Publish.Start")) rt->state = STATE_PUBLISHING;
    if (!t && !strcmp(tmpstr, "NetStream.Seek.Notify")) rt->state = STATE_PLAYING;

    return 0;
}

static int handle_invoke(URLContext *s, RTMPPacket *pkt)
{
    RTMPContext *rt = s->priv_data;
    int ret = 0;

    //TODO: check for the messages sent for wrong state?
    if (ff_amf_match_string(pkt->data, pkt->size, "_error")) {
        if ((ret = handle_invoke_error(s, pkt)) < 0)
            return ret;
    } else if (ff_amf_match_string(pkt->data, pkt->size, "_result")) {
        if ((ret = handle_invoke_result(s, pkt)) < 0)
            return ret;
    } else if (ff_amf_match_string(pkt->data, pkt->size, "onStatus")) {
        if ((ret = handle_invoke_status(s, pkt)) < 0)
            return ret;
    } else if (ff_amf_match_string(pkt->data, pkt->size, "onBWDone")) {
        if ((ret = gen_check_bw(s, rt)) < 0)
            return ret;
    } else if (ff_amf_match_string(pkt->data, pkt->size, "releaseStream") ||
               ff_amf_match_string(pkt->data, pkt->size, "FCPublish")     ||
               ff_amf_match_string(pkt->data, pkt->size, "publish")       ||
               ff_amf_match_string(pkt->data, pkt->size, "play")          ||
               ff_amf_match_string(pkt->data, pkt->size, "_checkbw")      ||
               ff_amf_match_string(pkt->data, pkt->size, "createStream")) {
        if ((ret = send_invoke_response(s, pkt)) < 0)
            return ret;
    }

    return ret;
}

static int update_offset(RTMPContext *rt, int size)
{
    int old_flv_size;

    // generate packet header and put data into buffer for FLV demuxer
    if (rt->flv_off < rt->flv_size) {
        // There is old unread data in the buffer, thus append at the end
        old_flv_size  = rt->flv_size;
        rt->flv_size += size;
    } else {
        // All data has been read, write the new data at the start of the buffer
        old_flv_size = 0;
        rt->flv_size = size;
        rt->flv_off  = 0;
    }

    return old_flv_size;
}

static int append_flv_data(RTMPContext *rt, RTMPPacket *pkt, int skip)
{
    int old_flv_size, ret;
    PutByteContext pbc;
    const uint8_t *data = pkt->data + skip;
    const int size      = pkt->size - skip;
    uint32_t ts         = pkt->timestamp;

    old_flv_size = update_offset(rt, size + 15);

    if ((ret = av_reallocp(&rt->flv_data, rt->flv_size)) < 0) {
        rt->flv_size = rt->flv_off = 0;
        return ret;
    }
    bytestream2_init_writer(&pbc, rt->flv_data, rt->flv_size);
    bytestream2_skip_p(&pbc, old_flv_size);
    bytestream2_put_byte(&pbc, pkt->type);
    bytestream2_put_be24(&pbc, size);
    bytestream2_put_be24(&pbc, ts);
    bytestream2_put_byte(&pbc, ts >> 24);
    bytestream2_put_be24(&pbc, 0);
    bytestream2_put_buffer(&pbc, data, size);
    bytestream2_put_be32(&pbc, 0);

    return 0;
}

static int handle_notify(URLContext *s, RTMPPacket *pkt)
{
    RTMPContext *rt  = s->priv_data;
    uint8_t commandbuffer[64];
    char statusmsg[128];
    int stringlen, ret, skip = 0;
    GetByteContext gbc;

    bytestream2_init(&gbc, pkt->data, pkt->size);
    if (ff_amf_read_string(&gbc, commandbuffer, sizeof(commandbuffer),
                           &stringlen))
        return AVERROR_INVALIDDATA;

    // Skip the @setDataFrame string and validate it is a notification
    if (!strcmp(commandbuffer, "@setDataFrame")) {
        skip = gbc.buffer - pkt->data;
        ret = ff_amf_read_string(&gbc, statusmsg,
                                 sizeof(statusmsg), &stringlen);
        if (ret < 0)
            return AVERROR_INVALIDDATA;
    }

    return append_flv_data(rt, pkt, skip);
}

/**
 * Parse received packet and possibly perform some action depending on
 * the packet contents.
 * @return 0 for no errors, negative values for serious errors which prevent
 *         further communications, positive values for uncritical errors
 */
static int rtmp_parse_result(URLContext *s, RTMPContext *rt, RTMPPacket *pkt)
{
    int ret;

#ifdef DEBUG
    ff_rtmp_packet_dump(s, pkt);
#endif

    switch (pkt->type) {
    case RTMP_PT_BYTES_READ:
        av_dlog(s, "received bytes read report\n");
        break;
    case RTMP_PT_CHUNK_SIZE:
        if ((ret = handle_chunk_size(s, pkt)) < 0)
            return ret;
        break;
    case RTMP_PT_PING:
        if ((ret = handle_ping(s, pkt)) < 0)
            return ret;
        break;
    case RTMP_PT_CLIENT_BW:
        if ((ret = handle_client_bw(s, pkt)) < 0)
            return ret;
        break;
    case RTMP_PT_SERVER_BW:
        if ((ret = handle_server_bw(s, pkt)) < 0)
            return ret;
        break;
    case RTMP_PT_INVOKE:
        if ((ret = handle_invoke(s, pkt)) < 0)
            return ret;
        break;
    case RTMP_PT_VIDEO:
    case RTMP_PT_AUDIO:
    case RTMP_PT_METADATA:
    case RTMP_PT_NOTIFY:
        /* Audio, Video and Metadata packets are parsed in get_packet() */
        break;
    default:
        av_log(s, AV_LOG_VERBOSE, "Unknown packet type received 0x%02X\n", pkt->type);
        break;
    }
    return 0;
}

static int handle_metadata(RTMPContext *rt, RTMPPacket *pkt)
{
    int ret, old_flv_size, type;
    const uint8_t *next;
    uint8_t *p;
    uint32_t size;
    uint32_t ts, cts, pts = 0;

    old_flv_size = update_offset(rt, pkt->size);

    if ((ret = av_reallocp(&rt->flv_data, rt->flv_size)) < 0) {
        rt->flv_size = rt->flv_off = 0;
        return ret;
    }

    next = pkt->data;
    p    = rt->flv_data + old_flv_size;

    /* copy data while rewriting timestamps */
    ts = pkt->timestamp;

    while (next - pkt->data < pkt->size - RTMP_HEADER) {
        type = bytestream_get_byte(&next);
        size = bytestream_get_be24(&next);
        cts  = bytestream_get_be24(&next);
        cts |= bytestream_get_byte(&next) << 24;
        if (!pts)
            pts = cts;
        ts += cts - pts;
        pts = cts;
        if (size + 3 + 4 > pkt->data + pkt->size - next)
            break;
        bytestream_put_byte(&p, type);
        bytestream_put_be24(&p, size);
        bytestream_put_be24(&p, ts);
        bytestream_put_byte(&p, ts >> 24);
        memcpy(p, next, size + 3 + 4);
        next += size + 3 + 4;
        p    += size + 3 + 4;
    }
    if (p != rt->flv_data + rt->flv_size) {
        av_log(NULL, AV_LOG_WARNING, "Incomplete flv packets in "
                                     "RTMP_PT_METADATA packet\n");
        rt->flv_size = p - rt->flv_data;
    }

    return 0;
}

/**
 * Interact with the server by receiving and sending RTMP packets until
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

    if (rt->state == STATE_STOPPED)
        return AVERROR_EOF;

    for (;;) {
        RTMPPacket rpkt = { 0 };
        if ((ret = ff_rtmp_packet_read(rt->stream, &rpkt,
                                       rt->in_chunk_size, rt->prev_pkt[0])) <= 0) {
            if (ret == 0) {
                return AVERROR(EAGAIN);
            } else {
                return AVERROR(EIO);
            }
        }
        rt->bytes_read += ret;
        if (rt->bytes_read - rt->last_bytes_read > rt->client_report_size) {
            av_log(s, AV_LOG_DEBUG, "Sending bytes read report\n");
            if ((ret = gen_bytes_read(s, rt, rpkt.timestamp + 1)) < 0)
                return ret;
            rt->last_bytes_read = rt->bytes_read;
        }

        ret = rtmp_parse_result(s, rt, &rpkt);

        // At this point we must check if we are in the seek state and continue
        // with the next packet. handle_invoke will get us out of this state
        // when the right message is encountered
        if (rt->state == STATE_SEEKING) {
            ff_rtmp_packet_destroy(&rpkt);
            // We continue, let the natural flow of things happen:
            // AVERROR(EAGAIN) or handle_invoke gets us out of here
            continue;
        }

        if (ret < 0) {//serious error in current packet
            ff_rtmp_packet_destroy(&rpkt);
            return ret;
        }
        if (rt->do_reconnect && for_header) {
            ff_rtmp_packet_destroy(&rpkt);
            return 0;
        }
        if (rt->state == STATE_STOPPED) {
            ff_rtmp_packet_destroy(&rpkt);
            return AVERROR_EOF;
        }
        if (for_header && (rt->state == STATE_PLAYING    ||
                           rt->state == STATE_PUBLISHING ||
                           rt->state == STATE_SENDING    ||
                           rt->state == STATE_RECEIVING)) {
            ff_rtmp_packet_destroy(&rpkt);
            return 0;
        }
        if (!rpkt.size || !rt->is_input) {
            ff_rtmp_packet_destroy(&rpkt);
            continue;
        }
        if (rpkt.type == RTMP_PT_VIDEO || rpkt.type == RTMP_PT_AUDIO) {
            ret = append_flv_data(rt, &rpkt, 0);
            ff_rtmp_packet_destroy(&rpkt);
            return ret;
        } else if (rpkt.type == RTMP_PT_NOTIFY) {
            ret = handle_notify(s, &rpkt);
            ff_rtmp_packet_destroy(&rpkt);
            return ret;
        } else if (rpkt.type == RTMP_PT_METADATA) {
            ret = handle_metadata(rt, &rpkt);
            ff_rtmp_packet_destroy(&rpkt);
            return 0;
        }
        ff_rtmp_packet_destroy(&rpkt);
    }
}

static int rtmp_close(URLContext *h)
{
    RTMPContext *rt = h->priv_data;
    int ret = 0, i, j;

    if (!rt->is_input) {
        rt->flv_data = NULL;
        if (rt->out_pkt.size)
            ff_rtmp_packet_destroy(&rt->out_pkt);
        if (rt->state > STATE_FCPUBLISH)
            ret = gen_fcunpublish_stream(h, rt);
    }
    if (rt->state > STATE_HANDSHAKED)
        ret = gen_delete_stream(h, rt);
    for (i = 0; i < 2; i++)
        for (j = 0; j < RTMP_CHANNELS; j++)
            ff_rtmp_packet_destroy(&rt->prev_pkt[i][j]);

    free_tracked_methods(rt);
    av_freep(&rt->flv_data);
    ffurl_close(rt->stream);
    return ret;
}

/**
 * Open RTMP connection and verify that the stream can be played.
 *
 * URL syntax: rtmp://server[:port][/app][/playpath]
 *             where 'app' is first one or two directories in the path
 *             (e.g. /ondemand/, /flash/live/, etc.)
 *             and 'playpath' is a file name (the rest of the path,
 *             may be prefixed with "mp4:")
 */
static int rtmp_open(URLContext *s, const char *uri, int flags)
{
    RTMPContext *rt = s->priv_data;
    char proto[8], hostname[256], path[1024], auth[100], *fname;
    char *old_app;
    uint8_t buf[2048];
    int port;
    AVDictionary *opts = NULL;
    int ret;

    if (rt->listen_timeout > 0)
        rt->listen = 1;

    rt->is_input = !(flags & AVIO_FLAG_WRITE);

    av_url_split(proto, sizeof(proto), auth, sizeof(auth),
                 hostname, sizeof(hostname), &port,
                 path, sizeof(path), s->filename);

    if (strchr(path, ' ')) {
        av_log(s, AV_LOG_WARNING,
               "Detected librtmp style URL parameters, these aren't supported "
               "by the libavformat internal RTMP handler currently enabled. "
               "See the documentation for the correct way to pass parameters.\n");
    }

    if (auth[0]) {
        char *ptr = strchr(auth, ':');
        if (ptr) {
            *ptr = '\0';
            av_strlcpy(rt->username, auth, sizeof(rt->username));
            av_strlcpy(rt->password, ptr + 1, sizeof(rt->password));
        }
    }

    if (rt->listen && strcmp(proto, "rtmp")) {
        av_log(s, AV_LOG_ERROR, "rtmp_listen not available for %s\n",
               proto);
        return AVERROR(EINVAL);
    }
    if (!strcmp(proto, "rtmpt") || !strcmp(proto, "rtmpts")) {
        if (!strcmp(proto, "rtmpts"))
            av_dict_set(&opts, "ffrtmphttp_tls", "1", 1);

        /* open the http tunneling connection */
        ff_url_join(buf, sizeof(buf), "ffrtmphttp", NULL, hostname, port, NULL);
    } else if (!strcmp(proto, "rtmps")) {
        /* open the tls connection */
        if (port < 0)
            port = RTMPS_DEFAULT_PORT;
        ff_url_join(buf, sizeof(buf), "tls", NULL, hostname, port, NULL);
    } else if (!strcmp(proto, "rtmpe") || (!strcmp(proto, "rtmpte"))) {
        if (!strcmp(proto, "rtmpte"))
            av_dict_set(&opts, "ffrtmpcrypt_tunneling", "1", 1);

        /* open the encrypted connection */
        ff_url_join(buf, sizeof(buf), "ffrtmpcrypt", NULL, hostname, port, NULL);
        rt->encrypted = 1;
    } else {
        /* open the tcp connection */
        if (port < 0)
            port = RTMP_DEFAULT_PORT;
        if (rt->listen)
            ff_url_join(buf, sizeof(buf), "tcp", NULL, hostname, port,
                        "?listen&listen_timeout=%d",
                        rt->listen_timeout * 1000);
        else
            ff_url_join(buf, sizeof(buf), "tcp", NULL, hostname, port, NULL);
    }

reconnect:
    if ((ret = ffurl_open(&rt->stream, buf, AVIO_FLAG_READ_WRITE,
                          &s->interrupt_callback, &opts)) < 0) {
        av_log(s , AV_LOG_ERROR, "Cannot open connection %s\n", buf);
        goto fail;
    }

    if (rt->swfverify) {
        if ((ret = rtmp_calc_swfhash(s)) < 0)
            goto fail;
    }

    rt->state = STATE_START;
    if (!rt->listen && (ret = rtmp_handshake(s, rt)) < 0)
        goto fail;
    if (rt->listen && (ret = rtmp_server_handshake(s, rt)) < 0)
        goto fail;

    rt->out_chunk_size = 128;
    rt->in_chunk_size  = 128; // Probably overwritten later
    rt->state = STATE_HANDSHAKED;

    // Keep the application name when it has been defined by the user.
    old_app = rt->app;

    rt->app = av_malloc(APP_MAX_LENGTH);
    if (!rt->app) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    //extract "app" part from path
    if (!strncmp(path, "/ondemand/", 10)) {
        fname = path + 10;
        memcpy(rt->app, "ondemand", 9);
    } else {
        char *next = *path ? path + 1 : path;
        char *p = strchr(next, '/');
        if (!p) {
            fname = next;
            rt->app[0] = '\0';
        } else {
            // make sure we do not mismatch a playpath for an application instance
            char *c = strchr(p + 1, ':');
            fname = strchr(p + 1, '/');
            if (!fname || (c && c < fname)) {
                fname = p + 1;
                av_strlcpy(rt->app, path + 1, FFMIN(p - path, APP_MAX_LENGTH));
            } else {
                fname++;
                av_strlcpy(rt->app, path + 1, FFMIN(fname - path - 1, APP_MAX_LENGTH));
            }
        }
    }

    if (old_app) {
        // The name of application has been defined by the user, override it.
        if (strlen(old_app) >= APP_MAX_LENGTH) {
            ret = AVERROR(EINVAL);
            goto fail;
        }
        av_free(rt->app);
        rt->app = old_app;
    }

    if (!rt->playpath) {
        int len = strlen(fname);

        rt->playpath = av_malloc(PLAYPATH_MAX_LENGTH);
        if (!rt->playpath) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        if (!strchr(fname, ':') && len >= 4 &&
            (!strcmp(fname + len - 4, ".f4v") ||
             !strcmp(fname + len - 4, ".mp4"))) {
            memcpy(rt->playpath, "mp4:", 5);
        } else if (len >= 4 && !strcmp(fname + len - 4, ".flv")) {
            fname[len - 4] = '\0';
        } else {
            rt->playpath[0] = 0;
        }
        av_strlcat(rt->playpath, fname, PLAYPATH_MAX_LENGTH);
    }

    if (!rt->tcurl) {
        rt->tcurl = av_malloc(TCURL_MAX_LENGTH);
        if (!rt->tcurl) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        ff_url_join(rt->tcurl, TCURL_MAX_LENGTH, proto, NULL, hostname,
                    port, "/%s", rt->app);
    }

    if (!rt->flashver) {
        rt->flashver = av_malloc(FLASHVER_MAX_LENGTH);
        if (!rt->flashver) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        if (rt->is_input) {
            snprintf(rt->flashver, FLASHVER_MAX_LENGTH, "%s %d,%d,%d,%d",
                    RTMP_CLIENT_PLATFORM, RTMP_CLIENT_VER1, RTMP_CLIENT_VER2,
                    RTMP_CLIENT_VER3, RTMP_CLIENT_VER4);
        } else {
            snprintf(rt->flashver, FLASHVER_MAX_LENGTH,
                    "FMLE/3.0 (compatible; %s)", LIBAVFORMAT_IDENT);
        }
    }

    rt->client_report_size = 1048576;
    rt->bytes_read = 0;
    rt->last_bytes_read = 0;
    rt->server_bw = 2500000;

    av_log(s, AV_LOG_DEBUG, "Proto = %s, path = %s, app = %s, fname = %s\n",
           proto, path, rt->app, rt->playpath);
    if (!rt->listen) {
        if ((ret = gen_connect(s, rt)) < 0)
            goto fail;
    } else {
        if (read_connect(s, s->priv_data) < 0)
            goto fail;
    }

    do {
        ret = get_packet(s, 1);
    } while (ret == AVERROR(EAGAIN));
    if (ret < 0)
        goto fail;

    if (rt->do_reconnect) {
        ffurl_close(rt->stream);
        rt->stream       = NULL;
        rt->do_reconnect = 0;
        rt->nb_invokes   = 0;
        memset(rt->prev_pkt, 0, sizeof(rt->prev_pkt));
        free_tracked_methods(rt);
        goto reconnect;
    }

    if (rt->is_input) {
        int err;
        // generate FLV header for demuxer
        rt->flv_size = 13;
        if ((err = av_reallocp(&rt->flv_data, rt->flv_size)) < 0)
            return err;
        rt->flv_off  = 0;
        memcpy(rt->flv_data, "FLV\1\5\0\0\0\011\0\0\0\0", rt->flv_size);
    } else {
        rt->flv_size = 0;
        rt->flv_data = NULL;
        rt->flv_off  = 0;
        rt->skip_bytes = 13;
    }

    s->max_packet_size = rt->stream->max_packet_size;
    s->is_streamed     = 1;
    return 0;

fail:
    av_dict_free(&opts);
    rtmp_close(s);
    return ret;
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
            return data_left;
        }
        if ((ret = get_packet(s, 0)) < 0)
           return ret;
    }
    return orig_size;
}

static int64_t rtmp_seek(URLContext *s, int stream_index, int64_t timestamp,
                         int flags)
{
    RTMPContext *rt = s->priv_data;
    int ret;
    av_log(s, AV_LOG_DEBUG,
           "Seek on stream index %d at timestamp %"PRId64" with flags %08x\n",
           stream_index, timestamp, flags);
    if ((ret = gen_seek(s, rt, timestamp)) < 0) {
        av_log(s, AV_LOG_ERROR,
               "Unable to send seek command on stream index %d at timestamp "
               "%"PRId64" with flags %08x\n",
               stream_index, timestamp, flags);
        return ret;
    }
    rt->flv_off = rt->flv_size;
    rt->state = STATE_SEEKING;
    return timestamp;
}

static int rtmp_write(URLContext *s, const uint8_t *buf, int size)
{
    RTMPContext *rt = s->priv_data;
    int size_temp = size;
    int pktsize, pkttype;
    uint32_t ts;
    const uint8_t *buf_temp = buf;
    uint8_t c;
    int ret;

    do {
        if (rt->skip_bytes) {
            int skip = FFMIN(rt->skip_bytes, size_temp);
            buf_temp       += skip;
            size_temp      -= skip;
            rt->skip_bytes -= skip;
            continue;
        }

        if (rt->flv_header_bytes < RTMP_HEADER) {
            const uint8_t *header = rt->flv_header;
            int copy = FFMIN(RTMP_HEADER - rt->flv_header_bytes, size_temp);
            int channel = RTMP_AUDIO_CHANNEL;
            bytestream_get_buffer(&buf_temp, rt->flv_header + rt->flv_header_bytes, copy);
            rt->flv_header_bytes += copy;
            size_temp            -= copy;
            if (rt->flv_header_bytes < RTMP_HEADER)
                break;

            pkttype = bytestream_get_byte(&header);
            pktsize = bytestream_get_be24(&header);
            ts = bytestream_get_be24(&header);
            ts |= bytestream_get_byte(&header) << 24;
            bytestream_get_be24(&header);
            rt->flv_size = pktsize;

            if (pkttype == RTMP_PT_VIDEO)
                channel = RTMP_VIDEO_CHANNEL;

            //force 12bytes header
            if (((pkttype == RTMP_PT_VIDEO || pkttype == RTMP_PT_AUDIO) && ts == 0) ||
                pkttype == RTMP_PT_NOTIFY) {
                if (pkttype == RTMP_PT_NOTIFY)
                    pktsize += 16;
                rt->prev_pkt[1][channel].channel_id = 0;
            }

            //this can be a big packet, it's better to send it right here
            if ((ret = ff_rtmp_packet_create(&rt->out_pkt, channel,
                                             pkttype, ts, pktsize)) < 0)
                return ret;

            rt->out_pkt.extra = rt->stream_id;
            rt->flv_data = rt->out_pkt.data;

            if (pkttype == RTMP_PT_NOTIFY)
                ff_amf_write_string(&rt->flv_data, "@setDataFrame");
        }

        if (rt->flv_size - rt->flv_off > size_temp) {
            bytestream_get_buffer(&buf_temp, rt->flv_data + rt->flv_off, size_temp);
            rt->flv_off += size_temp;
            size_temp = 0;
        } else {
            bytestream_get_buffer(&buf_temp, rt->flv_data + rt->flv_off, rt->flv_size - rt->flv_off);
            size_temp   -= rt->flv_size - rt->flv_off;
            rt->flv_off += rt->flv_size - rt->flv_off;
        }

        if (rt->flv_off == rt->flv_size) {
            rt->skip_bytes = 4;

            if ((ret = rtmp_send_packet(rt, &rt->out_pkt, 0)) < 0)
                return ret;
            rt->flv_size = 0;
            rt->flv_off = 0;
            rt->flv_header_bytes = 0;
            rt->flv_nb_packets++;
        }
    } while (buf_temp - buf < size);

    if (rt->flv_nb_packets < rt->flush_interval)
        return size;
    rt->flv_nb_packets = 0;

    /* set stream into nonblocking mode */
    rt->stream->flags |= AVIO_FLAG_NONBLOCK;

    /* try to read one byte from the stream */
    ret = ffurl_read(rt->stream, &c, 1);

    /* switch the stream back into blocking mode */
    rt->stream->flags &= ~AVIO_FLAG_NONBLOCK;

    if (ret == AVERROR(EAGAIN)) {
        /* no incoming data to handle */
        return size;
    } else if (ret < 0) {
        return ret;
    } else if (ret == 1) {
        RTMPPacket rpkt = { 0 };

        if ((ret = ff_rtmp_packet_read_internal(rt->stream, &rpkt,
                                                rt->in_chunk_size,
                                                rt->prev_pkt[0], c)) <= 0)
             return ret;

        if ((ret = rtmp_parse_result(s, rt, &rpkt)) < 0)
            return ret;

        ff_rtmp_packet_destroy(&rpkt);
    }

    return size;
}

#define OFFSET(x) offsetof(RTMPContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
#define ENC AV_OPT_FLAG_ENCODING_PARAM

static const AVOption rtmp_options[] = {
    {"rtmp_app", "Name of application to connect to on the RTMP server", OFFSET(app), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    {"rtmp_buffer", "Set buffer time in milliseconds. The default is 3000.", OFFSET(client_buffer_time), AV_OPT_TYPE_INT, {.i64 = 3000}, 0, INT_MAX, DEC|ENC},
    {"rtmp_conn", "Append arbitrary AMF data to the Connect message", OFFSET(conn), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    {"rtmp_flashver", "Version of the Flash plugin used to run the SWF player.", OFFSET(flashver), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    {"rtmp_flush_interval", "Number of packets flushed in the same request (RTMPT only).", OFFSET(flush_interval), AV_OPT_TYPE_INT, {.i64 = 10}, 0, INT_MAX, ENC},
    {"rtmp_live", "Specify that the media is a live stream.", OFFSET(live), AV_OPT_TYPE_INT, {.i64 = -2}, INT_MIN, INT_MAX, DEC, "rtmp_live"},
    {"any", "both", 0, AV_OPT_TYPE_CONST, {.i64 = -2}, 0, 0, DEC, "rtmp_live"},
    {"live", "live stream", 0, AV_OPT_TYPE_CONST, {.i64 = -1}, 0, 0, DEC, "rtmp_live"},
    {"recorded", "recorded stream", 0, AV_OPT_TYPE_CONST, {.i64 = 0}, 0, 0, DEC, "rtmp_live"},
    {"rtmp_pageurl", "URL of the web page in which the media was embedded. By default no value will be sent.", OFFSET(pageurl), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC},
    {"rtmp_playpath", "Stream identifier to play or to publish", OFFSET(playpath), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    {"rtmp_subscribe", "Name of live stream to subscribe to. Defaults to rtmp_playpath.", OFFSET(subscribe), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC},
    {"rtmp_swfhash", "SHA256 hash of the decompressed SWF file (32 bytes).", OFFSET(swfhash), AV_OPT_TYPE_BINARY, .flags = DEC},
    {"rtmp_swfsize", "Size of the decompressed SWF file, required for SWFVerification.", OFFSET(swfsize), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, DEC},
    {"rtmp_swfurl", "URL of the SWF player. By default no value will be sent", OFFSET(swfurl), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    {"rtmp_swfverify", "URL to player swf file, compute hash/size automatically.", OFFSET(swfverify), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC},
    {"rtmp_tcurl", "URL of the target stream. Defaults to proto://host[:port]/app.", OFFSET(tcurl), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
    {"rtmp_listen", "Listen for incoming rtmp connections", OFFSET(listen), AV_OPT_TYPE_INT, {.i64 = 0}, INT_MIN, INT_MAX, DEC, "rtmp_listen" },
    {"listen",      "Listen for incoming rtmp connections", OFFSET(listen), AV_OPT_TYPE_INT, {.i64 = 0}, INT_MIN, INT_MAX, DEC, "rtmp_listen" },
    {"timeout", "Maximum timeout (in seconds) to wait for incoming connections. -1 is infinite. Implies -rtmp_listen 1",  OFFSET(listen_timeout), AV_OPT_TYPE_INT, {.i64 = -1}, INT_MIN, INT_MAX, DEC, "rtmp_listen" },
    { NULL },
};

#define RTMP_PROTOCOL(flavor)                    \
static const AVClass flavor##_class = {          \
    .class_name = #flavor,                       \
    .item_name  = av_default_item_name,          \
    .option     = rtmp_options,                  \
    .version    = LIBAVUTIL_VERSION_INT,         \
};                                               \
                                                 \
URLProtocol ff_##flavor##_protocol = {           \
    .name           = #flavor,                   \
    .url_open       = rtmp_open,                 \
    .url_read       = rtmp_read,                 \
    .url_read_seek  = rtmp_seek,                 \
    .url_write      = rtmp_write,                \
    .url_close      = rtmp_close,                \
    .priv_data_size = sizeof(RTMPContext),       \
    .flags          = URL_PROTOCOL_FLAG_NETWORK, \
    .priv_data_class= &flavor##_class,           \
};


RTMP_PROTOCOL(rtmp)
RTMP_PROTOCOL(rtmpe)
RTMP_PROTOCOL(rtmps)
RTMP_PROTOCOL(rtmpt)
RTMP_PROTOCOL(rtmpte)
RTMP_PROTOCOL(rtmpts)
