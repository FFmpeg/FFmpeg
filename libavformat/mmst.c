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
#include "avformat.h"
#include "internal.h"
#include "libavutil/intreadwrite.h"
#include "libavcodec/bytestream.h"
#include "network.h"
#include "asf.h"

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

typedef struct {
    int id;
}MMSStream;

typedef struct {
    int outgoing_packet_seq;             ///< Outgoing packet sequence number.
    char path[256];                      ///< Path of the resource being asked for.
    char host[128];                      ///< Host of the resources.

    URLContext *mms_hd;                  ///< TCP connection handle
    MMSStream streams[MAX_STREAMS];

    /** Buffer for outgoing packets. */
    /*@{*/
    uint8_t *write_out_ptr;              ///< Pointer for writting the buffer.
    uint8_t out_buffer[512];             ///< Buffer for outgoing packet.
    /*@}*/

    /** Buffer for incoming packets. */
    /*@{*/
    uint8_t in_buffer[8192];             ///< Buffer for incoming packets.
    uint8_t *read_in_ptr;                ///< Pointer for reading from incoming buffer.
    int remaining_in_len;                ///< Reading length from incoming buffer.
    /*@}*/

    int incoming_packet_seq;             ///< Incoming packet sequence number.
    int incoming_flags;                  ///< Incoming packet flags.

    int packet_id;                       ///< Identifier for packets in the current stream.
    unsigned int header_packet_id;       ///< default is 2.

    /** Internal handling of the ASF header */
    /*@{*/
    uint8_t *asf_header;                 ///< Stored ASF header.
    int asf_header_size;                 ///< Size of stored ASF header.
    int header_parsed;                   ///< The header has been received and parsed.
    int asf_packet_len;
    /*@}*/

    int stream_num;                      ///< stream numbers.
    int is_playing;
} MMSContext;

/** Create MMST command packet header */
static void start_command_packet(MMSContext *mms, MMSCSPacketType packet_type)
{
    mms->write_out_ptr = mms->out_buffer;

    bytestream_put_le32(&mms->write_out_ptr, 1); // start sequence
    bytestream_put_le32(&mms->write_out_ptr, 0xb00bface);
    bytestream_put_le32(&mms->write_out_ptr, 0); // Length starts from after the protocol type bytes
    bytestream_put_le32(&mms->write_out_ptr, MKTAG('M','M','S',' '));
    bytestream_put_le32(&mms->write_out_ptr, 0);
    bytestream_put_le32(&mms->write_out_ptr, mms->outgoing_packet_seq++);
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
static int send_command_packet(MMSContext *mms)
{
    int exact_length= mms->write_out_ptr - mms->out_buffer;
    int first_length= exact_length - 16;
    int len8= first_length/8;
    int write_result;

    // update packet length fields.
    AV_WL32(mms->out_buffer + 8, first_length);
    AV_WL32(mms->out_buffer + 16, len8);
    AV_WL32(mms->out_buffer + 32, len8-2);

    // write it out.
    write_result= url_write(mms->mms_hd, mms->out_buffer, exact_length);
    if(write_result != exact_length) {
        dprintf(NULL, "url_write returned: %d != %d\n",
                write_result, exact_length);
        return AVERROR_IO;
    }

    return 0;
}

static void mms_put_utf16(MMSContext *mms, uint8_t *src)
{
    ByteIOContext bic;
    int size = mms->write_out_ptr - mms->out_buffer;
    int len;
    init_put_byte(&bic, mms->write_out_ptr,
            sizeof(mms->out_buffer) - size, 1, NULL, NULL, NULL, NULL);

    len = ff_put_str16_nolen(&bic, src);
    mms->write_out_ptr += len;
}

static int send_protocol_select(MMSContext *mms)
{
    char data_string[256];

    start_command_packet(mms, CS_PKT_PROTOCOL_SELECT);
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

    mms_put_utf16(mms, data_string);
    return send_command_packet(mms);
}

static int send_media_file_request(MMSContext *mms)
{
    start_command_packet(mms, CS_PKT_MEDIA_FILE_REQUEST);
    insert_command_prefixes(mms, 1, 0xffffffff);
    bytestream_put_le32(&mms->write_out_ptr, 0);
    bytestream_put_le32(&mms->write_out_ptr, 0);
    mms_put_utf16(mms, mms->path + 1); // +1 for skip "/"

    return send_command_packet(mms);
}

static void handle_packet_stream_changing_type(MMSContext *mms)
{
    dprintf(NULL, "Stream changing!\n");

    // 40 is the packet header size, 7 is the prefix size.
    mms->header_packet_id= AV_RL32(mms->in_buffer + 40 + 7);
    dprintf(NULL, "Changed header prefix to 0x%x", mms->header_packet_id);
}

static int send_keepalive_packet(MMSContext *mms)
{
    // respond to a keepalive with a keepalive...
    start_command_packet(mms, CS_PKT_KEEPALIVE);
    insert_command_prefixes(mms, 1, 0x100FFFF);
    return send_command_packet(mms);
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
static MMSSCPacketType get_tcp_server_response(MMSContext *mms)
{
    int read_result;
    MMSSCPacketType packet_type= -1;

    for(;;) {
        if((read_result= url_read_complete(mms->mms_hd, mms->in_buffer, 8))==8) {
            // handle command packet.
            if(AV_RL32(mms->in_buffer + 4)==0xb00bface) {
                mms->incoming_flags= mms->in_buffer[3];
                read_result= url_read_complete(mms->mms_hd, mms->in_buffer+8, 4);
                if(read_result == 4) {
                    int length_remaining= AV_RL32(mms->in_buffer+8) + 4;

                    dprintf(NULL, "Length remaining is %d\n", length_remaining);
                    // read the rest of the packet.
                    if (length_remaining < 0
                        || length_remaining > sizeof(mms->in_buffer) - 12) {
                        dprintf(NULL, "Incoming message len %d exceeds buffer len %d\n",
                            length_remaining, sizeof(mms->in_buffer) - 12);
                        return -1;
                    }
                    read_result = url_read_complete(mms->mms_hd, mms->in_buffer + 12,
                                                  length_remaining) ;
                    if (read_result == length_remaining) {
                        packet_type= AV_RL16(mms->in_buffer+36);
                    } else {
                        dprintf(NULL, "read for packet type failed%d!\n", read_result);
                        return -1;
                    }
                } else {
                    dprintf(NULL, "read for length remaining failed%d!\n", read_result);
                    return -1;
                }
            } else {
                int length_remaining;
                int packet_id_type;
                int tmp;

                assert(mms->remaining_in_len==0);

                // note we cache the first 8 bytes,
                // then fill up the buffer with the others
                tmp                       = AV_RL16(mms->in_buffer + 6);
                length_remaining          = (tmp - 8) & 0xffff;
                mms->incoming_packet_seq  = AV_RL32(mms->in_buffer);
                packet_id_type            = mms->in_buffer[4];
                mms->incoming_flags       = mms->in_buffer[5];

                if (length_remaining < 0
                        || length_remaining > sizeof(mms->in_buffer) - 8) {
                    dprintf(NULL, "Incoming data len %d exceeds buffer len %d\n",
                            length_remaining, sizeof(mms->in_buffer));
                    return -1;
                }
                mms->remaining_in_len    = length_remaining;
                mms->read_in_ptr         = mms->in_buffer;
                read_result= url_read_complete(mms->mms_hd, mms->in_buffer, length_remaining);
                if(read_result != length_remaining) {
                    dprintf(NULL, "read_bytes result: %d asking for %d\n",
                            read_result, length_remaining);
                    return -1;
                } else {
                    // if we successfully read everything.
                    if(packet_id_type == mms->header_packet_id) {
                        packet_type = SC_PKT_ASF_HEADER;
                        // Store the asf header
                        if(!mms->header_parsed) {
                            void *p = av_realloc(mms->asf_header,
                                              mms->asf_header_size
                                              + mms->remaining_in_len);
                            if (!p) {
                                av_freep(&mms->asf_header);
                                return AVERROR(ENOMEM);
                            }
                            mms->asf_header = p;
                            memcpy(mms->asf_header + mms->asf_header_size,
                                                 mms->read_in_ptr,
                                                 mms->remaining_in_len);
                            mms->asf_header_size += mms->remaining_in_len;
                        }
                    } else if(packet_id_type == mms->packet_id) {
                        packet_type = SC_PKT_ASF_MEDIA;
                    } else {
                        dprintf(NULL, "packet id type %d is old.", packet_id_type);
                        continue;
                    }
                }
            }

            // preprocess some packet type
            if(packet_type == SC_PKT_KEEPALIVE) {
                send_keepalive_packet(mms);
                continue;
            } else if(packet_type == SC_PKT_STREAM_CHANGING) {
                handle_packet_stream_changing_type(mms);
            } else if(packet_type == SC_PKT_ASF_MEDIA) {
                pad_media_packet(mms);
            }
            return packet_type;
        } else {
            if(read_result<0) {
                dprintf(NULL, "Read error (or cancelled) returned %d!\n", read_result);
                packet_type = SC_PKT_CANCEL;
            } else {
                dprintf(NULL, "Read result of zero?!\n");
                packet_type = SC_PKT_NO_DATA;
            }
            return packet_type;
        }
    }
}

static int mms_safe_send_recv(MMSContext *mms,
                              int (*send_fun)(MMSContext *mms),
                              const MMSSCPacketType expect_type)
{
    MMSSCPacketType type;
    if(send_fun) {
        int ret = send_fun(mms);
        if (ret < 0) {
            dprintf(NULL, "Send Packet error before expecting recv packet %d\n", expect_type);
            return ret;
        }
    }

    if ((type = get_tcp_server_response(mms)) != expect_type) {
        dprintf(NULL,"Unexpected packet type %d with type %d\n", type, expect_type);
        return -1;
    } else {
        return 0;
    }
}

static int send_media_header_request(MMSContext *mms)
{
    start_command_packet(mms, CS_PKT_MEDIA_HEADER_REQUEST);
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

    return send_command_packet(mms);
}

/** Send the initial handshake. */
static int send_startup_packet(MMSContext *mms)
{
    char data_string[256];
    // SubscriberName is defined in MS specification linked below.
    // The guid value can be any valid value.
    // http://download.microsoft.com/
    // download/9/5/E/95EF66AF-9026-4BB0-A41D-A4F81802D92C/%5BMS-WMSP%5D.pdf
    snprintf(data_string, sizeof(data_string),
            "NSPlayer/7.0.0.1956; {%s}; Host: %s",
            "7E667F5D-A661-495E-A512-F55686DDA178", mms->host);

    start_command_packet(mms, CS_PKT_INITIAL);
    insert_command_prefixes(mms, 0, 0x0004000b);
    bytestream_put_le32(&mms->write_out_ptr, 0x0003001c);
    mms_put_utf16(mms, data_string);
    return send_command_packet(mms);
}

static int asf_header_parser(MMSContext *mms)
{
    uint8_t *p = mms->asf_header;
    uint8_t *end;
    int flags, stream_id, real_header_size;
    mms->stream_num = 0;

    if (mms->asf_header_size < sizeof(ff_asf_guid) * 2 + 22 ||
        memcmp(p, ff_asf_header, sizeof(ff_asf_guid)))
        return -1;

    real_header_size = AV_RL64(p + sizeof(ff_asf_guid));
    end = mms->asf_header + real_header_size;

    p += sizeof(ff_asf_guid) + 14;
    while(end - p >= sizeof(ff_asf_guid) + 8) {
        uint64_t chunksize = AV_RL64(p + sizeof(ff_asf_guid));
        if (!chunksize || chunksize > end - p) {
            dprintf(NULL, "chunksize is exceptional value:%d!\n", chunksize);
            return -1;
        }
        if (!memcmp(p, ff_asf_file_header, sizeof(ff_asf_guid))) {
            /* read packet size */
            if (end - p > sizeof(ff_asf_guid) * 2 + 68) {
                mms->asf_packet_len = AV_RL32(p + sizeof(ff_asf_guid) * 2 + 64);
                if (mms->asf_packet_len <= 0 || mms->asf_packet_len > sizeof(mms->in_buffer)) {
                    dprintf(NULL,"Too large packet len:%d"
                        " may overwrite in_buffer when padding", mms->asf_packet_len);
                    return -1;
                }
            }
        } else if (!memcmp(p, ff_asf_stream_header, sizeof(ff_asf_guid))) {
            flags     = AV_RL16(p + sizeof(ff_asf_guid)*3 + 24);
            stream_id = flags & 0x7F;
            //The second condition is for checking CS_PKT_STREAM_ID_REQUEST packet size,
            //we can calcuate the packet size by stream_num.
            //Please see function send_stream_selection_request().
            if (mms->stream_num < MAX_STREAMS &&
                    46 + mms->stream_num * 6 < sizeof(mms->out_buffer)) {
                mms->streams[mms->stream_num].id = stream_id;
                mms->stream_num++;
            } else {
                dprintf(NULL, "Too many streams.\n");
                return -1;
            }
        }
        p += chunksize;
    }

    return 0;
}

/** Send MMST stream selection command based on the AVStream->discard values. */
static int send_stream_selection_request(MMSContext *mms)
{
    int i;

    //  send the streams we want back...
    start_command_packet(mms, CS_PKT_STREAM_ID_REQUEST);
    bytestream_put_le32(&mms->write_out_ptr, mms->stream_num);         // stream nums
    for(i= 0; i<mms->stream_num; i++) {
        bytestream_put_le16(&mms->write_out_ptr, 0xffff);              // flags
        bytestream_put_le16(&mms->write_out_ptr, mms->streams[i].id);  // stream id
        bytestream_put_le16(&mms->write_out_ptr, 0);                   // selection
    }

    bytestream_put_le16(&mms->write_out_ptr, 0);

    return send_command_packet(mms);
}

static int read_data(MMSContext *mms, uint8_t *buf, const int buf_size)
{
    int read_size;
    read_size = FFMIN(buf_size, mms->remaining_in_len);
    memcpy(buf, mms->read_in_ptr, read_size);
    mms->remaining_in_len -= read_size;
    mms->read_in_ptr      += read_size;
    return read_size;
}

/** Read at most one media packet (or a whole header). */
static int read_mms_packet(MMSContext *mms, uint8_t *buf, int buf_size)
{
    int result = 0, read_header_size = 0;
    int size_to_copy;

    do {
        if(read_header_size < mms->asf_header_size && !mms->is_playing) {
            /* Read from ASF header buffer */
            size_to_copy= FFMIN(buf_size,
                                mms->asf_header_size - read_header_size);
            memcpy(buf, mms->asf_header + read_header_size, size_to_copy);
            read_header_size += size_to_copy;
            result += size_to_copy;
            dprintf(NULL, "Copied %d bytes from stored header. left: %d\n",
                   size_to_copy, mms->asf_header_size - read_header_size);
            if (mms->asf_header_size == read_header_size) {
                av_freep(&mms->asf_header);
                mms->is_playing = 1;
            }
        } else if(mms->remaining_in_len) {
            /* Read remaining packet data to buffer.
             * the result can not be zero because remaining_in_len is positive.*/
            result = read_data(mms, buf, buf_size);
        } else {
            /* Read from network */
            int err = mms_safe_send_recv(mms, NULL, SC_PKT_ASF_MEDIA);
            if (err == 0) {
                if(mms->remaining_in_len>mms->asf_packet_len) {
                    dprintf(NULL, "Incoming packet"
                            "larger than the asf packet size stated (%d>%d)\n",
                            mms->remaining_in_len, mms->asf_packet_len);
                    result= AVERROR_IO;
                } else {
                    // copy the data to the packet buffer.
                    result = read_data(mms, buf, buf_size);
                    if (result == 0) {
                        dprintf(NULL, "read asf media paket size is zero!\n");
                        break;
                    }
                }
            } else {
                dprintf(NULL, "read packet error!\n");
                break;
            }
        }
    } while(!result); // only return one packet.
    return result;
}

static int send_close_packet(MMSContext *mms)
{
    start_command_packet(mms, CS_PKT_STREAM_CLOSE);
    insert_command_prefixes(mms, 1, 1);

    return send_command_packet(mms);
}

/** Close the MMSH/MMST connection */
static int mms_close(URLContext *h)
{
    MMSContext *mms = (MMSContext *)h->priv_data;

    if(mms->mms_hd) {
        send_close_packet(mms);
        url_close(mms->mms_hd);
    }

    /* free all separately allocated pointers in mms */
    av_free(mms->asf_header);
    av_freep(&h->priv_data);

    return 0;
}

static int mms_open(URLContext *h, const char *uri, int flags)
{
    MMSContext *mms;
    int port, err;
    char tcpname[256];

    h->is_streamed = 1;
    mms = h->priv_data = av_mallocz(sizeof(MMSContext));
    if (!h->priv_data)
        return AVERROR(ENOMEM);

    // only for MMS over TCP, so set proto = NULL
    ff_url_split(NULL, 0, NULL, 0,
            mms->host, sizeof(mms->host), &port, mms->path,
            sizeof(mms->path), uri);

    if(port<0)
        port = 1755; // defaut mms protocol port

    // establish tcp connection.
    ff_url_join(tcpname, sizeof(tcpname), "tcp", NULL, mms->host, port, NULL);
    err = url_open(&mms->mms_hd, tcpname, URL_RDWR);
    if (err)
        goto fail;

    mms->packet_id        = 3;          // default, initial value.
    mms->header_packet_id = 2;          // default, initial value.
    err = mms_safe_send_recv(mms, send_startup_packet, SC_PKT_CLIENT_ACCEPTED);
    if (err)
        goto fail;
    err = mms_safe_send_recv(mms, send_protocol_select, SC_PKT_PROTOCOL_ACCEPTED);
    if (err)
        goto fail;
    err = mms_safe_send_recv(mms, send_media_file_request, SC_PKT_MEDIA_FILE_DETAILS);
    if (err)
        goto fail;
    err = mms_safe_send_recv(mms, send_media_header_request, SC_PKT_HEADER_REQUEST_ACCEPTED);
    if (err)
        goto fail;
    err = mms_safe_send_recv(mms, NULL, SC_PKT_ASF_HEADER);
    if (err)
        goto fail;
    if((mms->incoming_flags != 0X08) && (mms->incoming_flags != 0X0C))
        goto fail;
    err = asf_header_parser(mms);
    if (err) {
        dprintf(NULL, "asf header parsed failed!\n");
        goto fail;
    }
    mms->header_parsed = 1;

    if (!mms->asf_packet_len || !mms->stream_num)
        goto fail;

    dprintf(NULL, "Leaving open (success)\n");
    return 0;
fail:
    mms_close(h);
    dprintf(NULL, "Leaving open (failure: %d)\n", err);
    return err;
}

static int send_media_packet_request(MMSContext *mms)
{
    start_command_packet(mms, CS_PKT_START_FROM_PKT_ID);
    insert_command_prefixes(mms, 1, 0x0001FFFF);
    bytestream_put_le64(&mms->write_out_ptr, 0);          // seek timestamp
    bytestream_put_le32(&mms->write_out_ptr, 0xffffffff); // unknown
    bytestream_put_le32(&mms->write_out_ptr, 0xffffffff); // packet offset
    bytestream_put_byte(&mms->write_out_ptr, 0xff);       // max stream time limit
    bytestream_put_byte(&mms->write_out_ptr, 0xff);       // max stream time limit
    bytestream_put_byte(&mms->write_out_ptr, 0xff);       // max stream time limit
    bytestream_put_byte(&mms->write_out_ptr, 0x00);       // stream time limit flag

    mms->packet_id++;                                     // new packet_id
    bytestream_put_le32(&mms->write_out_ptr, mms->packet_id);
    return send_command_packet(mms);
}


static void clear_stream_buffers(MMSContext *mms)
{
    mms->remaining_in_len = 0;
    mms->read_in_ptr      = mms->in_buffer;
}

/** Read ASF data through the protocol. */
static int mms_read(URLContext *h, uint8_t *buf, int size)
{
    /* TODO: see tcp.c:tcp_read() about a possible timeout scheme */
    MMSContext *mms = h->priv_data;
    int result = 0;

    /* Since we read the header at open(), this shouldn't be possible */
    assert(mms->header_parsed);

    if (!mms->is_playing) {
        dprintf(NULL, "mms_read() before play().\n");
        clear_stream_buffers(mms);
        result = mms_safe_send_recv(mms, send_stream_selection_request, SC_PKT_STREAM_ID_ACCEPTED);
        if (result)
            return result;
        // send media packet request
        result = mms_safe_send_recv(mms, send_media_packet_request, SC_PKT_MEDIA_PKT_FOLLOWS);
        if (result) {
            return result;
        }
    }
    return read_mms_packet(mms, buf, size);
}

URLProtocol mmst_protocol = {
    "mmst",
    mms_open,
    mms_read,
    NULL, // write
    NULL, // seek
    mms_close,
};
