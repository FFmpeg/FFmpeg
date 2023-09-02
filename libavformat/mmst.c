/*
 * MMS protocol over TCP
 * Copyright (c) 2006,2007 Ryan Martell
 * Copyright (c) 2007 Björn Axelsson
 * Copyright (c) 2010 Zhentan Feng <spyfeng at gmail dot com>
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

/* References
 * MMS protocol specification:
 *  [1]http://msdn.microsoft.com/en-us/library/cc234711(PROT.10).aspx
 * ASF specification. Revision 01.20.03.
 *  [2]http://web.archive.org/web/20131203084402/http://msdn.microsoft.com/en-us/library/bb643323.aspx
 */

#include "avformat.h"
#include "mms.h"
#include "internal.h"
#include "avio_internal.h"
#include "libavutil/intreadwrite.h"
#include "libavcodec/bytestream.h"
#include "network.h"
#include "url.h"

#define LOCAL_ADDRESS 0xc0a80081    // FIXME get and use correct local ip address.
#define LOCAL_PORT    1037          // as above.
/** Client to server packet types. */
typedef enum {
    CS_PKT_INITIAL                  = 0x01,
    CS_PKT_PROTOCOL_SELECT          = 0x02,
    CS_PKT_MEDIA_FILE_REQUEST       = 0x05,
    CS_PKT_START_FROM_PKT_ID        = 0x07,
    CS_PKT_STREAM_PAUSE             = 0x09,
    CS_PKT_STREAM_CLOSE             = 0x0d,
    CS_PKT_MEDIA_HEADER_REQUEST     = 0x15,
    CS_PKT_TIMING_DATA_REQUEST      = 0x18,
    CS_PKT_USER_PASSWORD            = 0x1a,
    CS_PKT_KEEPALIVE                = 0x1b,
    CS_PKT_STREAM_ID_REQUEST        = 0x33,
} MMSCSPacketType;

/** Server to client packet types. */
typedef enum {
    /** Control packets. */
    /*@{*/
    SC_PKT_CLIENT_ACCEPTED          = 0x01,
    SC_PKT_PROTOCOL_ACCEPTED        = 0x02,
    SC_PKT_PROTOCOL_FAILED          = 0x03,
    SC_PKT_MEDIA_PKT_FOLLOWS        = 0x05,
    SC_PKT_MEDIA_FILE_DETAILS       = 0x06,
    SC_PKT_HEADER_REQUEST_ACCEPTED  = 0x11,
    SC_PKT_TIMING_TEST_REPLY        = 0x15,
    SC_PKT_PASSWORD_REQUIRED        = 0x1a,
    SC_PKT_KEEPALIVE                = 0x1b,
    SC_PKT_STREAM_STOPPED           = 0x1e,
    SC_PKT_STREAM_CHANGING          = 0x20,
    SC_PKT_STREAM_ID_ACCEPTED       = 0x21,
    /*@}*/

    /** Pseudo packets. */
    /*@{*/
    SC_PKT_CANCEL                   = -1,
    SC_PKT_NO_DATA                  = -2,
    /*@}*/

    /** Data packets. */
    /*@{*/
    SC_PKT_ASF_HEADER               = 0x010000,// make it bigger than 0xFF in case of
    SC_PKT_ASF_MEDIA                = 0x010001,// receiving false data packets.
    /*@}*/
} MMSSCPacketType;

typedef struct MMSTContext {
    MMSContext  mms;
    int outgoing_packet_seq;             ///< Outgoing packet sequence number.
    char path[256];                      ///< Path of the resource being asked for.
    char host[128];                      ///< Host of the resources.
    int incoming_packet_seq;             ///< Incoming packet sequence number.
    int incoming_flags;                  ///< Incoming packet flags.
    int packet_id;                       ///< Identifier for packets in the current stream.
    unsigned int header_packet_id;       ///< default is 2.
} MMSTContext;

/** Create MMST command packet header */
static void start_command_packet(MMSTContext *mmst, MMSCSPacketType packet_type)
{
    MMSContext *mms    = &mmst->mms;
    mms->write_out_ptr = mms->out_buffer;

    bytestream_put_le32(&mms->write_out_ptr, 1); // start sequence
    bytestream_put_le32(&mms->write_out_ptr, 0xb00bface);
    bytestream_put_le32(&mms->write_out_ptr, 0); // Length starts from after the protocol type bytes
    bytestream_put_le32(&mms->write_out_ptr, MKTAG('M','M','S',' '));
    bytestream_put_le32(&mms->write_out_ptr, 0);
    bytestream_put_le32(&mms->write_out_ptr, mmst->outgoing_packet_seq++);
    bytestream_put_le64(&mms->write_out_ptr, 0); // timestamp
    bytestream_put_le32(&mms->write_out_ptr, 0);
    bytestream_put_le16(&mms->write_out_ptr, packet_type);
    bytestream_put_le16(&mms->write_out_ptr, 3); // direction to server
}

/** Add prefixes to MMST command packet. */
static void insert_command_prefixes(MMSContext *mms,
        uint32_t prefix1, uint32_t prefix2)
{
    bytestream_put_le32(&mms->write_out_ptr, prefix1); // first prefix
    bytestream_put_le32(&mms->write_out_ptr, prefix2); // second prefix
}

/** Send a prepared MMST command packet. */
static int send_command_packet(MMSTContext *mmst)
{
    MMSContext *mms  = &mmst->mms;
    int len= mms->write_out_ptr - mms->out_buffer;
    int exact_length = FFALIGN(len, 8);
    int first_length= exact_length - 16;
    int len8= first_length/8;
    int write_result;

    // update packet length fields.
    AV_WL32(mms->out_buffer + 8, first_length);
    AV_WL32(mms->out_buffer + 16, len8);
    AV_WL32(mms->out_buffer + 32, len8-2);
    memset(mms->write_out_ptr, 0, exact_length - len);

    // write it out.
    write_result= ffurl_write(mms->mms_hd, mms->out_buffer, exact_length);
    if(write_result != exact_length) {
        av_log(mms->mms_hd, AV_LOG_ERROR,
               "Failed to write data of length %d: %d (%s)\n",
               exact_length, write_result,
               write_result < 0 ? strerror(AVUNERROR(write_result)) :
                   "The server closed the connection");
        return AVERROR(EIO);
    }

    return 0;
}

static int mms_put_utf16(MMSContext *mms, const uint8_t *src)
{
    FFIOContext bic;
    int size = mms->write_out_ptr - mms->out_buffer;
    int len;
    ffio_init_write_context(&bic, mms->write_out_ptr,
                            sizeof(mms->out_buffer) - size);

    len = avio_put_str16le(&bic.pub, src);
    if (len < 0)
        return len;
    mms->write_out_ptr += len;
    return 0;
}

static int send_time_test_data(MMSTContext *mmst)
{
    start_command_packet(mmst, CS_PKT_TIMING_DATA_REQUEST);
    insert_command_prefixes(&mmst->mms, 0x00f0f0f0, 0x0004000b);
    return send_command_packet(mmst);
}

static int send_protocol_select(MMSTContext *mmst)
{
    int ret;
    char data_string[256];
    MMSContext *mms = &mmst->mms;

    start_command_packet(mmst, CS_PKT_PROTOCOL_SELECT);
    insert_command_prefixes(mms, 0, 0xffffffff);
    bytestream_put_le32(&mms->write_out_ptr, 0);          // maxFunnelBytes
    bytestream_put_le32(&mms->write_out_ptr, 0x00989680); // maxbitRate
    bytestream_put_le32(&mms->write_out_ptr, 2);          // funnelMode
    snprintf(data_string, sizeof(data_string), "\\\\%d.%d.%d.%d\\%s\\%d",
            (LOCAL_ADDRESS>>24)&0xff,
            (LOCAL_ADDRESS>>16)&0xff,
            (LOCAL_ADDRESS>>8)&0xff,
            LOCAL_ADDRESS&0xff,
            "TCP",                                        // or UDP
            LOCAL_PORT);

    if ((ret = mms_put_utf16(mms, data_string)) < 0)
        return ret;
    return send_command_packet(mmst);
}

static int send_media_file_request(MMSTContext *mmst)
{
    int ret;
    MMSContext *mms = &mmst->mms;
    start_command_packet(mmst, CS_PKT_MEDIA_FILE_REQUEST);
    insert_command_prefixes(mms, 1, 0xffffffff);
    bytestream_put_le32(&mms->write_out_ptr, 0);
    bytestream_put_le32(&mms->write_out_ptr, 0);
    if ((ret = mms_put_utf16(mms, mmst->path + 1)) < 0) // +1 for skip "/"
        return ret;

    return send_command_packet(mmst);
}

static void handle_packet_stream_changing_type(MMSTContext *mmst)
{
    MMSContext *mms = &mmst->mms;
    av_log(mms->mms_hd, AV_LOG_TRACE, "Stream changing!\n");

    // 40 is the packet header size, 7 is the prefix size.
    mmst->header_packet_id= AV_RL32(mms->in_buffer + 40 + 7);
    av_log(mms->mms_hd, AV_LOG_TRACE, "Changed header prefix to 0x%x", mmst->header_packet_id);
}

static int send_keepalive_packet(MMSTContext *mmst)
{
    // respond to a keepalive with a keepalive...
    start_command_packet(mmst, CS_PKT_KEEPALIVE);
    insert_command_prefixes(&mmst->mms, 1, 0x100FFFF);
    return send_command_packet(mmst);
}

/** Pad media packets smaller than max_packet_size and/or adjust read position
  * after a seek. */
static void pad_media_packet(MMSContext *mms)
{
    if(mms->remaining_in_len<mms->asf_packet_len) {
        int padding_size = mms->asf_packet_len - mms->remaining_in_len;
        memset(mms->in_buffer + mms->remaining_in_len, 0, padding_size);
        mms->remaining_in_len += padding_size;
    }
}

/** Read incoming MMST media, header or command packet. */
static MMSSCPacketType get_tcp_server_response(MMSTContext *mmst)
{
    int read_result;
    MMSSCPacketType packet_type= -1;
    MMSContext *mms = &mmst->mms;
    for(;;) {
        read_result = ffurl_read_complete(mms->mms_hd, mms->in_buffer, 8);
        if (read_result != 8) {
            if(read_result < 0) {
                av_log(mms->mms_hd, AV_LOG_ERROR,
                       "Error reading packet header: %d (%s)\n",
                       read_result, strerror(AVUNERROR(read_result)));
                packet_type = SC_PKT_CANCEL;
            } else {
                av_log(mms->mms_hd, AV_LOG_ERROR,
                       "The server closed the connection\n");
                packet_type = SC_PKT_NO_DATA;
            }
            return packet_type;
        }

        // handle command packet.
        if(AV_RL32(mms->in_buffer + 4)==0xb00bface) {
            int length_remaining, hr;

            mmst->incoming_flags= mms->in_buffer[3];
            read_result= ffurl_read_complete(mms->mms_hd, mms->in_buffer+8, 4);
            if(read_result != 4) {
                av_log(mms->mms_hd, AV_LOG_ERROR,
                       "Reading command packet length failed: %d (%s)\n",
                       read_result,
                       read_result < 0 ? strerror(AVUNERROR(read_result)) :
                           "The server closed the connection");
                return read_result < 0 ? read_result : AVERROR(EIO);
            }

            length_remaining= AV_RL32(mms->in_buffer+8) + 4;
            av_log(mms->mms_hd, AV_LOG_TRACE, "Length remaining is %d\n", length_remaining);
            // read the rest of the packet.
            if (length_remaining < 0
                || length_remaining > sizeof(mms->in_buffer) - 12) {
                av_log(mms->mms_hd, AV_LOG_ERROR,
                       "Incoming packet length %d exceeds bufsize %"SIZE_SPECIFIER"\n",
                       length_remaining, sizeof(mms->in_buffer) - 12);
                return AVERROR_INVALIDDATA;
            }
            read_result = ffurl_read_complete(mms->mms_hd, mms->in_buffer + 12,
                                            length_remaining) ;
            if (read_result != length_remaining) {
                av_log(mms->mms_hd, AV_LOG_ERROR,
                       "Reading pkt data (length=%d) failed: %d (%s)\n",
                       length_remaining, read_result,
                       read_result < 0 ? strerror(AVUNERROR(read_result)) :
                           "The server closed the connection");
                return read_result < 0 ? read_result : AVERROR(EIO);
            }
            packet_type= AV_RL16(mms->in_buffer+36);
            if (read_result >= 44 && (hr = AV_RL32(mms->in_buffer + 40))) {
                av_log(mms->mms_hd, AV_LOG_ERROR,
                       "Server sent a message with packet type 0x%x and error status code 0x%08x\n", packet_type, hr);
                return AVERROR(EINVAL);
            }
        } else {
            int length_remaining;
            int packet_id_type;
            int tmp;

            // note we cache the first 8 bytes,
            // then fill up the buffer with the others
            tmp                       = AV_RL16(mms->in_buffer + 6);
            length_remaining          = (tmp - 8) & 0xffff;
            mmst->incoming_packet_seq = AV_RL32(mms->in_buffer);
            packet_id_type            = mms->in_buffer[4];
            mmst->incoming_flags      = mms->in_buffer[5];

            if (length_remaining < 0
                || length_remaining > sizeof(mms->in_buffer) - 8) {
                av_log(mms->mms_hd, AV_LOG_ERROR,
                       "Data length %d is invalid or too large (max=%"SIZE_SPECIFIER")\n",
                       length_remaining, sizeof(mms->in_buffer));
                return AVERROR_INVALIDDATA;
            }
            mms->remaining_in_len    = length_remaining;
            mms->read_in_ptr         = mms->in_buffer;
            read_result= ffurl_read_complete(mms->mms_hd, mms->in_buffer, length_remaining);
            if(read_result != length_remaining) {
                av_log(mms->mms_hd, AV_LOG_ERROR,
                       "Failed to read packet data of size %d: %d (%s)\n",
                       length_remaining, read_result,
                       read_result < 0 ? strerror(AVUNERROR(read_result)) :
                           "The server closed the connection");
                return read_result < 0 ? read_result : AVERROR(EIO);
            }

            // if we successfully read everything.
            if(packet_id_type == mmst->header_packet_id) {
                int err;
                packet_type = SC_PKT_ASF_HEADER;
                // Store the asf header
                if(!mms->header_parsed) {
                    if ((err = av_reallocp(&mms->asf_header,
                                           mms->asf_header_size +
                                           mms->remaining_in_len)) < 0) {
                        mms->asf_header_size = 0;
                        return err;
                    }
                    memcpy(mms->asf_header + mms->asf_header_size,
                           mms->read_in_ptr, mms->remaining_in_len);
                    mms->asf_header_size += mms->remaining_in_len;
                }
                // 0x04 means asf header is sent in multiple packets.
                if (mmst->incoming_flags == 0x04)
                    continue;
            } else if(packet_id_type == mmst->packet_id) {
                packet_type = SC_PKT_ASF_MEDIA;
            } else {
                av_log(mms->mms_hd, AV_LOG_TRACE, "packet id type %d is old.", packet_id_type);
                continue;
            }
        }

        // preprocess some packet type
        if(packet_type == SC_PKT_KEEPALIVE) {
            send_keepalive_packet(mmst);
            continue;
        } else if(packet_type == SC_PKT_STREAM_CHANGING) {
            handle_packet_stream_changing_type(mmst);
        } else if(packet_type == SC_PKT_ASF_MEDIA) {
            pad_media_packet(mms);
        }
        return packet_type;
    }
}

static int mms_safe_send_recv(MMSTContext *mmst,
                              int (*send_fun)(MMSTContext *mmst),
                              const MMSSCPacketType expect_type)
{
    MMSSCPacketType type;
    if(send_fun) {
        int ret = send_fun(mmst);
        if (ret < 0) {
            av_log(NULL, AV_LOG_TRACE, "Send Packet error before expecting recv packet %d\n", expect_type);
            return ret;
        }
    }

    if ((type = get_tcp_server_response(mmst)) != expect_type) {
        av_log(NULL, AV_LOG_ERROR,
               "Corrupt stream (unexpected packet type 0x%x, expected 0x%x)\n",
               type, expect_type);
        return AVERROR_INVALIDDATA;
    } else {
        return 0;
    }
}

static int send_media_header_request(MMSTContext *mmst)
{
    MMSContext *mms = &mmst->mms;
    start_command_packet(mmst, CS_PKT_MEDIA_HEADER_REQUEST);
    insert_command_prefixes(mms, 1, 0);
    bytestream_put_le32(&mms->write_out_ptr, 0);
    bytestream_put_le32(&mms->write_out_ptr, 0x00800000);
    bytestream_put_le32(&mms->write_out_ptr, 0xffffffff);
    bytestream_put_le32(&mms->write_out_ptr, 0);
    bytestream_put_le32(&mms->write_out_ptr, 0);
    bytestream_put_le32(&mms->write_out_ptr, 0);

    // the media preroll value in milliseconds?
    bytestream_put_le32(&mms->write_out_ptr, 0);
    bytestream_put_le32(&mms->write_out_ptr, 0x40AC2000);
    bytestream_put_le32(&mms->write_out_ptr, 2);
    bytestream_put_le32(&mms->write_out_ptr, 0);

    return send_command_packet(mmst);
}

/** Send the initial handshake. */
static int send_startup_packet(MMSTContext *mmst)
{
    char data_string[256];
    int ret;
    MMSContext *mms = &mmst->mms;
    // SubscriberName is defined in MS specification linked below.
    // The GUID value can be any valid value.
    // http://download.microsoft.com/
    // download/9/5/E/95EF66AF-9026-4BB0-A41D-A4F81802D92C/%5BMS-WMSP%5D.pdf
    snprintf(data_string, sizeof(data_string),
            "NSPlayer/7.0.0.1956; {%s}; Host: %s",
            "7E667F5D-A661-495E-A512-F55686DDA178", mmst->host);

    start_command_packet(mmst, CS_PKT_INITIAL);
    insert_command_prefixes(mms, 0, 0x0004000b);
    bytestream_put_le32(&mms->write_out_ptr, 0x0003001c);
    if ((ret = mms_put_utf16(mms, data_string)) < 0)
        return ret;
    return send_command_packet(mmst);
}

/** Send MMST stream selection command based on the AVStream->discard values. */
static int send_stream_selection_request(MMSTContext *mmst)
{
    int i;
    MMSContext *mms = &mmst->mms;
    //  send the streams we want back...
    start_command_packet(mmst, CS_PKT_STREAM_ID_REQUEST);
    bytestream_put_le32(&mms->write_out_ptr, mms->stream_num);         // stream nums
    for(i= 0; i<mms->stream_num; i++) {
        bytestream_put_le16(&mms->write_out_ptr, 0xffff);              // flags
        bytestream_put_le16(&mms->write_out_ptr, mms->streams[i].id);  // stream id
        bytestream_put_le16(&mms->write_out_ptr, 0);                   // selection
    }
    return send_command_packet(mmst);
}

static int send_close_packet(MMSTContext *mmst)
{
    start_command_packet(mmst, CS_PKT_STREAM_CLOSE);
    insert_command_prefixes(&mmst->mms, 1, 1);

    return send_command_packet(mmst);
}

/** Close the MMSH/MMST connection */
static int mms_close(URLContext *h)
{
    MMSTContext *mmst = (MMSTContext *)h->priv_data;
    MMSContext *mms   = &mmst->mms;
    if(mms->mms_hd) {
        send_close_packet(mmst);
        ffurl_closep(&mms->mms_hd);
    }

    /* free all separately allocated pointers in mms */
    av_freep(&mms->streams);
    av_freep(&mms->asf_header);

    return 0;
}

static int send_media_packet_request(MMSTContext *mmst)
{
    MMSContext *mms = &mmst->mms;
    start_command_packet(mmst, CS_PKT_START_FROM_PKT_ID);
    insert_command_prefixes(mms, 1, 0x0001FFFF);
    bytestream_put_le64(&mms->write_out_ptr, 0);          // seek timestamp
    bytestream_put_le32(&mms->write_out_ptr, 0xffffffff); // unknown
    bytestream_put_le32(&mms->write_out_ptr, 0xffffffff); // packet offset
    bytestream_put_byte(&mms->write_out_ptr, 0xff);       // max stream time limit
    bytestream_put_byte(&mms->write_out_ptr, 0xff);       // max stream time limit
    bytestream_put_byte(&mms->write_out_ptr, 0xff);       // max stream time limit
    bytestream_put_byte(&mms->write_out_ptr, 0x00);       // stream time limit flag

    mmst->packet_id++;                                     // new packet_id
    bytestream_put_le32(&mms->write_out_ptr, mmst->packet_id);
    return send_command_packet(mmst);
}


static void clear_stream_buffers(MMSContext *mms)
{
    mms->remaining_in_len = 0;
    mms->read_in_ptr      = mms->in_buffer;
}

static int mms_open(URLContext *h, const char *uri, int flags)
{
    MMSTContext *mmst = h->priv_data;
    MMSContext *mms;
    int port, err;
    char tcpname[256];

    h->is_streamed = 1;
    mms = &mmst->mms;

    // only for MMS over TCP, so set proto = NULL
    av_url_split(NULL, 0, NULL, 0,
            mmst->host, sizeof(mmst->host), &port, mmst->path,
            sizeof(mmst->path), uri);

    if(port<0)
        port = 1755; // default MMS protocol port

    // establish tcp connection.
    ff_url_join(tcpname, sizeof(tcpname), "tcp", NULL, mmst->host, port, NULL);
    err = ffurl_open_whitelist(&mms->mms_hd, tcpname, AVIO_FLAG_READ_WRITE,
                               &h->interrupt_callback, NULL,
                               h->protocol_whitelist, h->protocol_blacklist, h);
    if (err)
        goto fail;

    mmst->packet_id        = 3;          // default, initial value.
    mmst->header_packet_id = 2;          // default, initial value.
    err = mms_safe_send_recv(mmst, send_startup_packet, SC_PKT_CLIENT_ACCEPTED);
    if (err)
        goto fail;
    err = mms_safe_send_recv(mmst, send_time_test_data, SC_PKT_TIMING_TEST_REPLY);
    if (err)
        goto fail;
    err = mms_safe_send_recv(mmst, send_protocol_select, SC_PKT_PROTOCOL_ACCEPTED);
    if (err)
        goto fail;
    err = mms_safe_send_recv(mmst, send_media_file_request, SC_PKT_MEDIA_FILE_DETAILS);
    if (err)
        goto fail;
    err = mms_safe_send_recv(mmst, send_media_header_request, SC_PKT_HEADER_REQUEST_ACCEPTED);
    if (err)
        goto fail;
    err = mms_safe_send_recv(mmst, NULL, SC_PKT_ASF_HEADER);
    if (err)
        goto fail;
    if((mmst->incoming_flags != 0X08) && (mmst->incoming_flags != 0X0C)) {
        av_log(h, AV_LOG_ERROR,
               "The server does not support MMST (try MMSH or RTSP)\n");
        err = AVERROR(EINVAL);
        goto fail;
    }
    err = ff_mms_asf_header_parser(mms);
    if (err) {
        av_log(h, AV_LOG_TRACE, "asf header parsed failed!\n");
        goto fail;
    }
    mms->header_parsed = 1;

    if (!mms->asf_packet_len || !mms->stream_num)
        goto fail;

    clear_stream_buffers(mms);
    err = mms_safe_send_recv(mmst, send_stream_selection_request, SC_PKT_STREAM_ID_ACCEPTED);
    if (err)
        goto fail;
    // send media packet request
    err = mms_safe_send_recv(mmst, send_media_packet_request, SC_PKT_MEDIA_PKT_FOLLOWS);
    if (err) {
        goto fail;
    }
    av_log(h, AV_LOG_TRACE, "Leaving open (success)\n");
    return 0;
fail:
    mms_close(h);
    av_log(mms->mms_hd, AV_LOG_TRACE, "Leaving open (failure: %d)\n", err);
    return err;
}

/** Read ASF data through the protocol. */
static int mms_read(URLContext *h, uint8_t *buf, int size)
{
    /* TODO: see tcp.c:tcp_read() about a possible timeout scheme */
    MMSTContext *mmst = h->priv_data;
    MMSContext *mms   = &mmst->mms;
    int result = 0;

    do {
        if(mms->asf_header_read_size < mms->asf_header_size) {
            /* Read from ASF header buffer */
            result = ff_mms_read_header(mms, buf, size);
        } else if(mms->remaining_in_len) {
            /* Read remaining packet data to buffer.
             * the result can not be zero because remaining_in_len is positive.*/
            result = ff_mms_read_data(mms, buf, size);
        } else {
            /* Read from network */
            int err = mms_safe_send_recv(mmst, NULL, SC_PKT_ASF_MEDIA);
            if (err == 0) {
                if(mms->remaining_in_len>mms->asf_packet_len) {
                    av_log(h, AV_LOG_ERROR,
                           "Incoming pktlen %d is larger than ASF pktsize %d\n",
                           mms->remaining_in_len, mms->asf_packet_len);
                    result= AVERROR(EIO);
                } else {
                    // copy the data to the packet buffer.
                    result = ff_mms_read_data(mms, buf, size);
                    if (result == 0) {
                        av_log(h, AV_LOG_TRACE, "Read ASF media packet size is zero!\n");
                        break;
                    }
                }
            } else {
                av_log(h, AV_LOG_TRACE, "read packet error!\n");
                break;
            }
        }
    } while(!result); // only return one packet.
    return result;
}

const URLProtocol ff_mmst_protocol = {
    .name           = "mmst",
    .url_open       = mms_open,
    .url_read       = mms_read,
    .url_close      = mms_close,
    .priv_data_size = sizeof(MMSTContext),
    .flags          = URL_PROTOCOL_FLAG_NETWORK,
};
