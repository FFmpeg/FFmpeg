/*
 * Matroska file demuxer (no muxer yet)
 * Copyright (c) 2003-2004 The ffmpeg Project
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
 * @file matroskadec.c
 * Matroska file demuxer
 * by Ronald Bultje <rbultje@ronald.bitfreak.net>
 * with a little help from Moritz Bunkus <moritz@bunkus.org>
 * Specs available on the matroska project page:
 * http://www.matroska.org/.
 */

#include "avformat.h"
/* For codec_get_id(). */
#include "riff.h"
#include "intfloat_readwrite.h"
#include "matroska.h"
#include "libavcodec/mpeg4audio.h"

typedef struct Track {
    MatroskaTrackType type;

    /* Unique track number and track ID. stream_index is the index that
     * the calling app uses for this track. */
    uint32_t num;
    uint32_t uid;
    int stream_index;

    char *name;
    char language[4];

    char *codec_id;
    char *codec_name;

    unsigned char *codec_priv;
    int codec_priv_size;

    uint64_t default_duration;
    MatroskaTrackFlags flags;
} MatroskaTrack;

typedef struct MatroskaVideoTrack {
    MatroskaTrack track;

    int pixel_width;
    int pixel_height;
    int display_width;
    int display_height;

    uint32_t fourcc;

    MatroskaAspectRatioMode ar_mode;
    MatroskaEyeMode eye_mode;

    //..
} MatroskaVideoTrack;

typedef struct MatroskaAudioTrack {
    MatroskaTrack track;

    int channels;
    int bitdepth;
    int internal_samplerate;
    int samplerate;
    int block_align;

    /* real audio header */
    int coded_framesize;
    int sub_packet_h;
    int frame_size;
    int sub_packet_size;
    int sub_packet_cnt;
    int pkt_cnt;
    uint8_t *buf;
    //..
} MatroskaAudioTrack;

typedef struct MatroskaSubtitleTrack {
    MatroskaTrack track;
    //..
} MatroskaSubtitleTrack;

#define MAX_TRACK_SIZE (FFMAX(FFMAX(sizeof(MatroskaVideoTrack), \
                                    sizeof(MatroskaAudioTrack)), \
                                    sizeof(MatroskaSubtitleTrack)))

typedef struct MatroskaLevel {
    uint64_t start;
    uint64_t length;
} MatroskaLevel;

typedef struct MatroskaDemuxIndex {
  uint64_t        pos;   /* of the corresponding *cluster*! */
  uint16_t        track; /* reference to 'num' */
  uint64_t        time;  /* in nanoseconds */
} MatroskaDemuxIndex;

typedef struct MatroskaDemuxContext {
    AVFormatContext *ctx;

    /* ebml stuff */
    int num_levels;
    MatroskaLevel levels[EBML_MAX_DEPTH];
    int level_up;

    /* matroska stuff */
    char *writing_app;
    char *muxing_app;
    int64_t created;

    /* timescale in the file */
    int64_t time_scale;

    /* num_streams is the number of streams that av_new_stream() was called
     * for ( = that are available to the calling program). */
    int num_tracks;
    int num_streams;
    MatroskaTrack *tracks[MAX_STREAMS];

    /* cache for ID peeking */
    uint32_t peek_id;

    /* byte position of the segment inside the stream */
    offset_t segment_start;

    /* The packet queue. */
    AVPacket **packets;
    int num_packets;

    /* have we already parse metadata/cues/clusters? */
    int metadata_parsed;
    int index_parsed;
    int done;

    /* The index for seeking. */
    int num_indexes;
    MatroskaDemuxIndex *index;

    /* What to skip before effectively reading a packet. */
    int skip_to_keyframe;
    AVStream *skip_to_stream;
} MatroskaDemuxContext;

/*
 * The first few functions handle EBML file parsing. The rest
 * is the document interpretation. Matroska really just is a
 * EBML file.
 */

/*
 * Return: the amount of levels in the hierarchy that the
 * current element lies higher than the previous one.
 * The opposite isn't done - that's auto-done using master
 * element reading.
 */

static int
ebml_read_element_level_up (MatroskaDemuxContext *matroska)
{
    ByteIOContext *pb = matroska->ctx->pb;
    offset_t pos = url_ftell(pb);
    int num = 0;

    while (matroska->num_levels > 0) {
        MatroskaLevel *level = &matroska->levels[matroska->num_levels - 1];

        if (pos >= level->start + level->length) {
            matroska->num_levels--;
            num++;
        } else {
            break;
        }
    }

    return num;
}

/*
 * Read: an "EBML number", which is defined as a variable-length
 * array of bytes. The first byte indicates the length by giving a
 * number of 0-bits followed by a one. The position of the first
 * "one" bit inside the first byte indicates the length of this
 * number.
 * Returns: num. of bytes read. < 0 on error.
 */

static int
ebml_read_num (MatroskaDemuxContext *matroska,
               int                   max_size,
               uint64_t             *number)
{
    ByteIOContext *pb = matroska->ctx->pb;
    int len_mask = 0x80, read = 1, n = 1;
    int64_t total = 0;

    /* the first byte tells us the length in bytes - get_byte() can normally
     * return 0, but since that's not a valid first ebmlID byte, we can
     * use it safely here to catch EOS. */
    if (!(total = get_byte(pb))) {
        /* we might encounter EOS here */
        if (!url_feof(pb)) {
            offset_t pos = url_ftell(pb);
            av_log(matroska->ctx, AV_LOG_ERROR,
                   "Read error at pos. %"PRIu64" (0x%"PRIx64")\n",
                   pos, pos);
        }
        return AVERROR(EIO); /* EOS or actual I/O error */
    }

    /* get the length of the EBML number */
    while (read <= max_size && !(total & len_mask)) {
        read++;
        len_mask >>= 1;
    }
    if (read > max_size) {
        offset_t pos = url_ftell(pb) - 1;
        av_log(matroska->ctx, AV_LOG_ERROR,
               "Invalid EBML number size tag 0x%02x at pos %"PRIu64" (0x%"PRIx64")\n",
               (uint8_t) total, pos, pos);
        return AVERROR_INVALIDDATA;
    }

    /* read out length */
    total &= ~len_mask;
    while (n++ < read)
        total = (total << 8) | get_byte(pb);

    *number = total;

    return read;
}

/*
 * Read: the element content data ID.
 * Return: the number of bytes read or < 0 on error.
 */

static int
ebml_read_element_id (MatroskaDemuxContext *matroska,
                      uint32_t             *id,
                      int                  *level_up)
{
    int read;
    uint64_t total;

    /* if we re-call this, use our cached ID */
    if (matroska->peek_id != 0) {
        if (level_up)
            *level_up = 0;
        *id = matroska->peek_id;
        return 0;
    }

    /* read out the "EBML number", include tag in ID */
    if ((read = ebml_read_num(matroska, 4, &total)) < 0)
        return read;
    *id = matroska->peek_id  = total | (1 << (read * 7));

    /* level tracking */
    if (level_up)
        *level_up = ebml_read_element_level_up(matroska);

    return read;
}

/*
 * Read: element content length.
 * Return: the number of bytes read or < 0 on error.
 */

static int
ebml_read_element_length (MatroskaDemuxContext *matroska,
                          uint64_t             *length)
{
    /* clear cache since we're now beyond that data point */
    matroska->peek_id = 0;

    /* read out the "EBML number", include tag in ID */
    return ebml_read_num(matroska, 8, length);
}

/*
 * Return: the ID of the next element, or 0 on error.
 * Level_up contains the amount of levels that this
 * next element lies higher than the previous one.
 */

static uint32_t
ebml_peek_id (MatroskaDemuxContext *matroska,
              int                  *level_up)
{
    uint32_t id;

    if (ebml_read_element_id(matroska, &id, level_up) < 0)
        return 0;

    return id;
}

/*
 * Seek to a given offset.
 * 0 is success, -1 is failure.
 */

static int
ebml_read_seek (MatroskaDemuxContext *matroska,
                offset_t              offset)
{
    ByteIOContext *pb = matroska->ctx->pb;

    /* clear ID cache, if any */
    matroska->peek_id = 0;

    return (url_fseek(pb, offset, SEEK_SET) == offset) ? 0 : -1;
}

/*
 * Skip the next element.
 * 0 is success, -1 is failure.
 */

static int
ebml_read_skip (MatroskaDemuxContext *matroska)
{
    ByteIOContext *pb = matroska->ctx->pb;
    uint32_t id;
    uint64_t length;
    int res;

    if ((res = ebml_read_element_id(matroska, &id, NULL)) < 0 ||
        (res = ebml_read_element_length(matroska, &length)) < 0)
        return res;

    url_fskip(pb, length);

    return 0;
}

/*
 * Read the next element as an unsigned int.
 * 0 is success, < 0 is failure.
 */

static int
ebml_read_uint (MatroskaDemuxContext *matroska,
                uint32_t             *id,
                uint64_t             *num)
{
    ByteIOContext *pb = matroska->ctx->pb;
    int n = 0, size, res;
    uint64_t rlength;

    if ((res = ebml_read_element_id(matroska, id, NULL)) < 0 ||
        (res = ebml_read_element_length(matroska, &rlength)) < 0)
        return res;
    size = rlength;
    if (size < 1 || size > 8) {
        offset_t pos = url_ftell(pb);
        av_log(matroska->ctx, AV_LOG_ERROR,
               "Invalid uint element size %d at position %"PRId64" (0x%"PRIx64")\n",
                size, pos, pos);
        return AVERROR_INVALIDDATA;
    }

    /* big-endian ordening; build up number */
    *num = 0;
    while (n++ < size)
        *num = (*num << 8) | get_byte(pb);

    return 0;
}

/*
 * Read the next element as a signed int.
 * 0 is success, < 0 is failure.
 */

static int
ebml_read_sint (MatroskaDemuxContext *matroska,
                uint32_t             *id,
                int64_t              *num)
{
    ByteIOContext *pb = matroska->ctx->pb;
    int size, n = 1, negative = 0, res;
    uint64_t rlength;

    if ((res = ebml_read_element_id(matroska, id, NULL)) < 0 ||
        (res = ebml_read_element_length(matroska, &rlength)) < 0)
        return res;
    size = rlength;
    if (size < 1 || size > 8) {
        offset_t pos = url_ftell(pb);
        av_log(matroska->ctx, AV_LOG_ERROR,
               "Invalid sint element size %d at position %"PRId64" (0x%"PRIx64")\n",
                size, pos, pos);
        return AVERROR_INVALIDDATA;
    }
    if ((*num = get_byte(pb)) & 0x80) {
        negative = 1;
        *num &= ~0x80;
    }
    while (n++ < size)
        *num = (*num << 8) | get_byte(pb);

    /* make signed */
    if (negative)
        *num = *num - (1LL << ((8 * size) - 1));

    return 0;
}

/*
 * Read the next element as a float.
 * 0 is success, < 0 is failure.
 */

static int
ebml_read_float (MatroskaDemuxContext *matroska,
                 uint32_t             *id,
                 double               *num)
{
    ByteIOContext *pb = matroska->ctx->pb;
    int size, res;
    uint64_t rlength;

    if ((res = ebml_read_element_id(matroska, id, NULL)) < 0 ||
        (res = ebml_read_element_length(matroska, &rlength)) < 0)
        return res;
    size = rlength;

    if (size == 4) {
        *num= av_int2flt(get_be32(pb));
    } else if(size==8){
        *num= av_int2dbl(get_be64(pb));
    } else{
        offset_t pos = url_ftell(pb);
        av_log(matroska->ctx, AV_LOG_ERROR,
               "Invalid float element size %d at position %"PRIu64" (0x%"PRIx64")\n",
               size, pos, pos);
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

/*
 * Read the next element as an ASCII string.
 * 0 is success, < 0 is failure.
 */

static int
ebml_read_ascii (MatroskaDemuxContext *matroska,
                 uint32_t             *id,
                 char                **str)
{
    ByteIOContext *pb = matroska->ctx->pb;
    int size, res;
    uint64_t rlength;

    if ((res = ebml_read_element_id(matroska, id, NULL)) < 0 ||
        (res = ebml_read_element_length(matroska, &rlength)) < 0)
        return res;
    size = rlength;

    /* ebml strings are usually not 0-terminated, so we allocate one
     * byte more, read the string and NULL-terminate it ourselves. */
    if (size < 0 || !(*str = av_malloc(size + 1))) {
        av_log(matroska->ctx, AV_LOG_ERROR, "Memory allocation failed\n");
        return AVERROR(ENOMEM);
    }
    if (get_buffer(pb, (uint8_t *) *str, size) != size) {
        offset_t pos = url_ftell(pb);
        av_log(matroska->ctx, AV_LOG_ERROR,
               "Read error at pos. %"PRIu64" (0x%"PRIx64")\n", pos, pos);
        return AVERROR(EIO);
    }
    (*str)[size] = '\0';

    return 0;
}

/*
 * Read the next element as a UTF-8 string.
 * 0 is success, < 0 is failure.
 */

static int
ebml_read_utf8 (MatroskaDemuxContext *matroska,
                uint32_t             *id,
                char                **str)
{
  return ebml_read_ascii(matroska, id, str);
}

/*
 * Read the next element as a date (nanoseconds since 1/1/2000).
 * 0 is success, < 0 is failure.
 */

static int
ebml_read_date (MatroskaDemuxContext *matroska,
                uint32_t             *id,
                int64_t              *date)
{
  return ebml_read_sint(matroska, id, date);
}

/*
 * Read the next element, but only the header. The contents
 * are supposed to be sub-elements which can be read separately.
 * 0 is success, < 0 is failure.
 */

static int
ebml_read_master (MatroskaDemuxContext *matroska,
                  uint32_t             *id)
{
    ByteIOContext *pb = matroska->ctx->pb;
    uint64_t length;
    MatroskaLevel *level;
    int res;

    if ((res = ebml_read_element_id(matroska, id, NULL)) < 0 ||
        (res = ebml_read_element_length(matroska, &length)) < 0)
        return res;

    /* protect... (Heaven forbids that the '>' is true) */
    if (matroska->num_levels >= EBML_MAX_DEPTH) {
        av_log(matroska->ctx, AV_LOG_ERROR,
               "File moves beyond max. allowed depth (%d)\n", EBML_MAX_DEPTH);
        return AVERROR(ENOSYS);
    }

    /* remember level */
    level = &matroska->levels[matroska->num_levels++];
    level->start = url_ftell(pb);
    level->length = length;

    return 0;
}

/*
 * Read the next element as binary data.
 * 0 is success, < 0 is failure.
 */

static int
ebml_read_binary (MatroskaDemuxContext *matroska,
                  uint32_t             *id,
                  uint8_t             **binary,
                  int                  *size)
{
    ByteIOContext *pb = matroska->ctx->pb;
    uint64_t rlength;
    int res;

    if ((res = ebml_read_element_id(matroska, id, NULL)) < 0 ||
        (res = ebml_read_element_length(matroska, &rlength)) < 0)
        return res;
    *size = rlength;

    if (!(*binary = av_malloc(*size))) {
        av_log(matroska->ctx, AV_LOG_ERROR,
               "Memory allocation error\n");
        return AVERROR(ENOMEM);
    }

    if (get_buffer(pb, *binary, *size) != *size) {
        offset_t pos = url_ftell(pb);
        av_log(matroska->ctx, AV_LOG_ERROR,
               "Read error at pos. %"PRIu64" (0x%"PRIx64")\n", pos, pos);
        return AVERROR(EIO);
    }

    return 0;
}

/*
 * Read signed/unsigned "EBML" numbers.
 * Return: number of bytes processed, < 0 on error.
 * XXX: use ebml_read_num().
 */

static int
matroska_ebmlnum_uint (uint8_t  *data,
                       uint32_t  size,
                       uint64_t *num)
{
    int len_mask = 0x80, read = 1, n = 1, num_ffs = 0;
    uint64_t total;

    if (size <= 0)
        return AVERROR_INVALIDDATA;

    total = data[0];
    while (read <= 8 && !(total & len_mask)) {
        read++;
        len_mask >>= 1;
    }
    if (read > 8)
        return AVERROR_INVALIDDATA;

    if ((total &= (len_mask - 1)) == len_mask - 1)
        num_ffs++;
    if (size < read)
        return AVERROR_INVALIDDATA;
    while (n < read) {
        if (data[n] == 0xff)
            num_ffs++;
        total = (total << 8) | data[n];
        n++;
    }

    if (read == num_ffs)
        *num = (uint64_t)-1;
    else
        *num = total;

    return read;
}

/*
 * Same as above, but signed.
 */

static int
matroska_ebmlnum_sint (uint8_t  *data,
                       uint32_t  size,
                       int64_t  *num)
{
    uint64_t unum;
    int res;

    /* read as unsigned number first */
    if ((res = matroska_ebmlnum_uint(data, size, &unum)) < 0)
        return res;

    /* make signed (weird way) */
    if (unum == (uint64_t)-1)
        *num = INT64_MAX;
    else
        *num = unum - ((1LL << ((7 * res) - 1)) - 1);

    return res;
}

/*
 * Read an EBML header.
 * 0 is success, < 0 is failure.
 */

static int
ebml_read_header (MatroskaDemuxContext *matroska,
                  char                **doctype,
                  int                  *version)
{
    uint32_t id;
    int level_up, res = 0;

    /* default init */
    if (doctype)
        *doctype = NULL;
    if (version)
        *version = 1;

    if (!(id = ebml_peek_id(matroska, &level_up)) ||
        level_up != 0 || id != EBML_ID_HEADER) {
        av_log(matroska->ctx, AV_LOG_ERROR,
               "This is not an EBML file (id=0x%x/0x%x)\n", id, EBML_ID_HEADER);
        return AVERROR_INVALIDDATA;
    }
    if ((res = ebml_read_master(matroska, &id)) < 0)
        return res;

    while (res == 0) {
        if (!(id = ebml_peek_id(matroska, &level_up)))
            return AVERROR(EIO);

        /* end-of-header */
        if (level_up)
            break;

        switch (id) {
            /* is our read version uptodate? */
            case EBML_ID_EBMLREADVERSION: {
                uint64_t num;

                if ((res = ebml_read_uint(matroska, &id, &num)) < 0)
                    return res;
                if (num > EBML_VERSION) {
                    av_log(matroska->ctx, AV_LOG_ERROR,
                           "EBML version %"PRIu64" (> %d) is not supported\n",
                           num, EBML_VERSION);
                    return AVERROR_INVALIDDATA;
                }
                break;
            }

            /* we only handle 8 byte lengths at max */
            case EBML_ID_EBMLMAXSIZELENGTH: {
                uint64_t num;

                if ((res = ebml_read_uint(matroska, &id, &num)) < 0)
                    return res;
                if (num > sizeof(uint64_t)) {
                    av_log(matroska->ctx, AV_LOG_ERROR,
                           "Integers of size %"PRIu64" (> %zd) not supported\n",
                           num, sizeof(uint64_t));
                    return AVERROR_INVALIDDATA;
                }
                break;
            }

            /* we handle 4 byte IDs at max */
            case EBML_ID_EBMLMAXIDLENGTH: {
                uint64_t num;

                if ((res = ebml_read_uint(matroska, &id, &num)) < 0)
                    return res;
                if (num > sizeof(uint32_t)) {
                    av_log(matroska->ctx, AV_LOG_ERROR,
                           "IDs of size %"PRIu64" (> %zu) not supported\n",
                            num, sizeof(uint32_t));
                    return AVERROR_INVALIDDATA;
                }
                break;
            }

            case EBML_ID_DOCTYPE: {
                char *text;

                if ((res = ebml_read_ascii(matroska, &id, &text)) < 0)
                    return res;
                if (doctype) {
                    if (*doctype)
                        av_free(*doctype);
                    *doctype = text;
                } else
                    av_free(text);
                break;
            }

            case EBML_ID_DOCTYPEREADVERSION: {
                uint64_t num;

                if ((res = ebml_read_uint(matroska, &id, &num)) < 0)
                    return res;
                if (version)
                    *version = num;
                break;
            }

            default:
                av_log(matroska->ctx, AV_LOG_INFO,
                       "Unknown data type 0x%x in EBML header", id);
                /* pass-through */

            case EBML_ID_VOID:
            /* we ignore these two, as they don't tell us anything we
             * care about */
            case EBML_ID_EBMLVERSION:
            case EBML_ID_DOCTYPEVERSION:
                res = ebml_read_skip (matroska);
                break;
        }
    }

    return 0;
}


static int
matroska_find_track_by_num (MatroskaDemuxContext *matroska,
                            int                   num)
{
    int i;

    for (i = 0; i < matroska->num_tracks; i++)
        if (matroska->tracks[i]->num == num)
            return i;

    return -1;
}


/*
 * Put one packet in an application-supplied AVPacket struct.
 * Returns 0 on success or -1 on failure.
 */

static int
matroska_deliver_packet (MatroskaDemuxContext *matroska,
                         AVPacket             *pkt)
{
    if (matroska->num_packets > 0) {
        memcpy(pkt, matroska->packets[0], sizeof(AVPacket));
        av_free(matroska->packets[0]);
        if (matroska->num_packets > 1) {
            memmove(&matroska->packets[0], &matroska->packets[1],
                    (matroska->num_packets - 1) * sizeof(AVPacket *));
            matroska->packets =
                av_realloc(matroska->packets, (matroska->num_packets - 1) *
                           sizeof(AVPacket *));
        } else {
            av_freep(&matroska->packets);
        }
        matroska->num_packets--;
        return 0;
    }

    return -1;
}

/*
 * Put a packet into our internal queue. Will be delivered to the
 * user/application during the next get_packet() call.
 */

static void
matroska_queue_packet (MatroskaDemuxContext *matroska,
                       AVPacket             *pkt)
{
    matroska->packets =
        av_realloc(matroska->packets, (matroska->num_packets + 1) *
                   sizeof(AVPacket *));
    matroska->packets[matroska->num_packets] = pkt;
    matroska->num_packets++;
}

/*
 * Free all packets in our internal queue.
 */
static void
matroska_clear_queue (MatroskaDemuxContext *matroska)
{
    if (matroska->packets) {
        int n;
        for (n = 0; n < matroska->num_packets; n++) {
            av_free_packet(matroska->packets[n]);
            av_free(matroska->packets[n]);
        }
        av_free(matroska->packets);
        matroska->packets = NULL;
        matroska->num_packets = 0;
    }
}


/*
 * Autodetecting...
 */

static int
matroska_probe (AVProbeData *p)
{
    uint64_t total = 0;
    int len_mask = 0x80, size = 1, n = 1;
    uint8_t probe_data[] = { 'm', 'a', 't', 'r', 'o', 's', 'k', 'a' };

    /* ebml header? */
    if (AV_RB32(p->buf) != EBML_ID_HEADER)
        return 0;

    /* length of header */
    total = p->buf[4];
    while (size <= 8 && !(total & len_mask)) {
        size++;
        len_mask >>= 1;
    }
    if (size > 8)
      return 0;
    total &= (len_mask - 1);
    while (n < size)
        total = (total << 8) | p->buf[4 + n++];

    /* does the probe data contain the whole header? */
    if (p->buf_size < 4 + size + total)
      return 0;

    /* the header must contain the document type 'matroska'. For now,
     * we don't parse the whole header but simply check for the
     * availability of that array of characters inside the header.
     * Not fully fool-proof, but good enough. */
    for (n = 4 + size; n <= 4 + size + total - sizeof(probe_data); n++)
        if (!memcmp (&p->buf[n], probe_data, sizeof(probe_data)))
            return AVPROBE_SCORE_MAX;

    return 0;
}

/*
 * From here on, it's all XML-style DTD stuff... Needs no comments.
 */

static int
matroska_parse_info (MatroskaDemuxContext *matroska)
{
    int res = 0;
    uint32_t id;

    av_log(matroska->ctx, AV_LOG_DEBUG, "Parsing info...\n");

    while (res == 0) {
        if (!(id = ebml_peek_id(matroska, &matroska->level_up))) {
            res = AVERROR(EIO);
            break;
        } else if (matroska->level_up) {
            matroska->level_up--;
            break;
        }

        switch (id) {
            /* cluster timecode */
            case MATROSKA_ID_TIMECODESCALE: {
                uint64_t num;
                if ((res = ebml_read_uint(matroska, &id, &num)) < 0)
                    break;
                matroska->time_scale = num;
                break;
            }

            case MATROSKA_ID_DURATION: {
                double num;
                if ((res = ebml_read_float(matroska, &id, &num)) < 0)
                    break;
                matroska->ctx->duration = num * matroska->time_scale * 1000 / AV_TIME_BASE;
                break;
            }

            case MATROSKA_ID_TITLE: {
                char *text;
                if ((res = ebml_read_utf8(matroska, &id, &text)) < 0)
                    break;
                strncpy(matroska->ctx->title, text,
                        sizeof(matroska->ctx->title)-1);
                av_free(text);
                break;
            }

            case MATROSKA_ID_WRITINGAPP: {
                char *text;
                if ((res = ebml_read_utf8(matroska, &id, &text)) < 0)
                    break;
                matroska->writing_app = text;
                break;
            }

            case MATROSKA_ID_MUXINGAPP: {
                char *text;
                if ((res = ebml_read_utf8(matroska, &id, &text)) < 0)
                    break;
                matroska->muxing_app = text;
                break;
            }

            case MATROSKA_ID_DATEUTC: {
                int64_t time;
                if ((res = ebml_read_date(matroska, &id, &time)) < 0)
                    break;
                matroska->created = time;
                break;
            }

            default:
                av_log(matroska->ctx, AV_LOG_INFO,
                       "Unknown entry 0x%x in info header\n", id);
                /* fall-through */

            case EBML_ID_VOID:
                res = ebml_read_skip(matroska);
                break;
        }

        if (matroska->level_up) {
            matroska->level_up--;
            break;
        }
    }

    return res;
}

static int
matroska_add_stream (MatroskaDemuxContext *matroska)
{
    int res = 0;
    uint32_t id;
    MatroskaTrack *track;

    av_log(matroska->ctx, AV_LOG_DEBUG, "parsing track, adding stream..,\n");

    /* Allocate a generic track. As soon as we know its type we'll realloc. */
    track = av_mallocz(MAX_TRACK_SIZE);
    matroska->num_tracks++;
    strcpy(track->language, "eng");

    /* start with the master */
    if ((res = ebml_read_master(matroska, &id)) < 0)
        return res;

    /* try reading the trackentry headers */
    while (res == 0) {
        if (!(id = ebml_peek_id(matroska, &matroska->level_up))) {
            res = AVERROR(EIO);
            break;
        } else if (matroska->level_up > 0) {
            matroska->level_up--;
            break;
        }

        switch (id) {
            /* track number (unique stream ID) */
            case MATROSKA_ID_TRACKNUMBER: {
                uint64_t num;
                if ((res = ebml_read_uint(matroska, &id, &num)) < 0)
                    break;
                track->num = num;
                break;
            }

            /* track UID (unique identifier) */
            case MATROSKA_ID_TRACKUID: {
                uint64_t num;
                if ((res = ebml_read_uint(matroska, &id, &num)) < 0)
                    break;
                track->uid = num;
                break;
            }

            /* track type (video, audio, combined, subtitle, etc.) */
            case MATROSKA_ID_TRACKTYPE: {
                uint64_t num;
                if ((res = ebml_read_uint(matroska, &id, &num)) < 0)
                    break;
                if (track->type && track->type != num) {
                    av_log(matroska->ctx, AV_LOG_INFO,
                           "More than one tracktype in an entry - skip\n");
                    break;
                }
                track->type = num;

                switch (track->type) {
                    case MATROSKA_TRACK_TYPE_VIDEO:
                    case MATROSKA_TRACK_TYPE_AUDIO:
                    case MATROSKA_TRACK_TYPE_SUBTITLE:
                        break;
                    case MATROSKA_TRACK_TYPE_COMPLEX:
                    case MATROSKA_TRACK_TYPE_LOGO:
                    case MATROSKA_TRACK_TYPE_CONTROL:
                    default:
                        av_log(matroska->ctx, AV_LOG_INFO,
                               "Unknown or unsupported track type 0x%x\n",
                               track->type);
                        track->type = 0;
                        break;
                }
                matroska->tracks[matroska->num_tracks - 1] = track;
                break;
            }

            /* tracktype specific stuff for video */
            case MATROSKA_ID_TRACKVIDEO: {
                MatroskaVideoTrack *videotrack;
                if (!track->type)
                    track->type = MATROSKA_TRACK_TYPE_VIDEO;
                if (track->type != MATROSKA_TRACK_TYPE_VIDEO) {
                    av_log(matroska->ctx, AV_LOG_INFO,
                           "video data in non-video track - ignoring\n");
                    res = AVERROR_INVALIDDATA;
                    break;
                } else if ((res = ebml_read_master(matroska, &id)) < 0)
                    break;
                videotrack = (MatroskaVideoTrack *)track;

                while (res == 0) {
                    if (!(id = ebml_peek_id(matroska, &matroska->level_up))) {
                        res = AVERROR(EIO);
                        break;
                    } else if (matroska->level_up > 0) {
                        matroska->level_up--;
                        break;
                    }

                    switch (id) {
                        /* fixme, this should be one-up, but I get it here */
                        case MATROSKA_ID_TRACKDEFAULTDURATION: {
                            uint64_t num;
                            if ((res = ebml_read_uint (matroska, &id,
                                                       &num)) < 0)
                                break;
                            track->default_duration = num;
                            break;
                        }

                        /* video framerate */
                        case MATROSKA_ID_VIDEOFRAMERATE: {
                            double num;
                            if ((res = ebml_read_float(matroska, &id,
                                                       &num)) < 0)
                                break;
                            if (!track->default_duration)
                                track->default_duration = 1000000000/num;
                            break;
                        }

                        /* width of the size to display the video at */
                        case MATROSKA_ID_VIDEODISPLAYWIDTH: {
                            uint64_t num;
                            if ((res = ebml_read_uint(matroska, &id,
                                                      &num)) < 0)
                                break;
                            videotrack->display_width = num;
                            break;
                        }

                        /* height of the size to display the video at */
                        case MATROSKA_ID_VIDEODISPLAYHEIGHT: {
                            uint64_t num;
                            if ((res = ebml_read_uint(matroska, &id,
                                                      &num)) < 0)
                                break;
                            videotrack->display_height = num;
                            break;
                        }

                        /* width of the video in the file */
                        case MATROSKA_ID_VIDEOPIXELWIDTH: {
                            uint64_t num;
                            if ((res = ebml_read_uint(matroska, &id,
                                                      &num)) < 0)
                                break;
                            videotrack->pixel_width = num;
                            break;
                        }

                        /* height of the video in the file */
                        case MATROSKA_ID_VIDEOPIXELHEIGHT: {
                            uint64_t num;
                            if ((res = ebml_read_uint(matroska, &id,
                                                      &num)) < 0)
                                break;
                            videotrack->pixel_height = num;
                            break;
                        }

                        /* whether the video is interlaced */
                        case MATROSKA_ID_VIDEOFLAGINTERLACED: {
                            uint64_t num;
                            if ((res = ebml_read_uint(matroska, &id,
                                                      &num)) < 0)
                                break;
                            if (num)
                                track->flags |=
                                    MATROSKA_VIDEOTRACK_INTERLACED;
                            else
                                track->flags &=
                                    ~MATROSKA_VIDEOTRACK_INTERLACED;
                            break;
                        }

                        /* stereo mode (whether the video has two streams,
                         * where one is for the left eye and the other for
                         * the right eye, which creates a 3D-like
                         * effect) */
                        case MATROSKA_ID_VIDEOSTEREOMODE: {
                            uint64_t num;
                            if ((res = ebml_read_uint(matroska, &id,
                                                      &num)) < 0)
                                break;
                            if (num != MATROSKA_EYE_MODE_MONO &&
                                num != MATROSKA_EYE_MODE_LEFT &&
                                num != MATROSKA_EYE_MODE_RIGHT &&
                                num != MATROSKA_EYE_MODE_BOTH) {
                                av_log(matroska->ctx, AV_LOG_INFO,
                                       "Ignoring unknown eye mode 0x%x\n",
                                       (uint32_t) num);
                                break;
                            }
                            videotrack->eye_mode = num;
                            break;
                        }

                        /* aspect ratio behaviour */
                        case MATROSKA_ID_VIDEOASPECTRATIO: {
                            uint64_t num;
                            if ((res = ebml_read_uint(matroska, &id,
                                                      &num)) < 0)
                                break;
                            if (num != MATROSKA_ASPECT_RATIO_MODE_FREE &&
                                num != MATROSKA_ASPECT_RATIO_MODE_KEEP &&
                                num != MATROSKA_ASPECT_RATIO_MODE_FIXED) {
                                av_log(matroska->ctx, AV_LOG_INFO,
                                       "Ignoring unknown aspect ratio 0x%x\n",
                                       (uint32_t) num);
                                break;
                            }
                            videotrack->ar_mode = num;
                            break;
                        }

                        /* colorspace (only matters for raw video)
                         * fourcc */
                        case MATROSKA_ID_VIDEOCOLORSPACE: {
                            uint64_t num;
                            if ((res = ebml_read_uint(matroska, &id,
                                                      &num)) < 0)
                                break;
                            videotrack->fourcc = num;
                            break;
                        }

                        default:
                            av_log(matroska->ctx, AV_LOG_INFO,
                                   "Unknown video track header entry "
                                   "0x%x - ignoring\n", id);
                            /* pass-through */

                        case EBML_ID_VOID:
                            res = ebml_read_skip(matroska);
                            break;
                    }

                    if (matroska->level_up) {
                        matroska->level_up--;
                        break;
                    }
                }
                break;
            }

            /* tracktype specific stuff for audio */
            case MATROSKA_ID_TRACKAUDIO: {
                MatroskaAudioTrack *audiotrack;
                if (!track->type)
                    track->type = MATROSKA_TRACK_TYPE_AUDIO;
                if (track->type != MATROSKA_TRACK_TYPE_AUDIO) {
                    av_log(matroska->ctx, AV_LOG_INFO,
                           "audio data in non-audio track - ignoring\n");
                    res = AVERROR_INVALIDDATA;
                    break;
                } else if ((res = ebml_read_master(matroska, &id)) < 0)
                    break;
                audiotrack = (MatroskaAudioTrack *)track;
                audiotrack->channels = 1;
                audiotrack->samplerate = 8000;

                while (res == 0) {
                    if (!(id = ebml_peek_id(matroska, &matroska->level_up))) {
                        res = AVERROR(EIO);
                        break;
                    } else if (matroska->level_up > 0) {
                        matroska->level_up--;
                        break;
                    }

                    switch (id) {
                        /* samplerate */
                        case MATROSKA_ID_AUDIOSAMPLINGFREQ: {
                            double num;
                            if ((res = ebml_read_float(matroska, &id,
                                                       &num)) < 0)
                                break;
                            audiotrack->internal_samplerate =
                            audiotrack->samplerate = num;
                            break;
                        }

                        case MATROSKA_ID_AUDIOOUTSAMPLINGFREQ: {
                            double num;
                            if ((res = ebml_read_float(matroska, &id,
                                                       &num)) < 0)
                                break;
                            audiotrack->samplerate = num;
                            break;
                        }

                            /* bitdepth */
                        case MATROSKA_ID_AUDIOBITDEPTH: {
                            uint64_t num;
                            if ((res = ebml_read_uint(matroska, &id,
                                                      &num)) < 0)
                                break;
                            audiotrack->bitdepth = num;
                            break;
                        }

                            /* channels */
                        case MATROSKA_ID_AUDIOCHANNELS: {
                            uint64_t num;
                            if ((res = ebml_read_uint(matroska, &id,
                                                      &num)) < 0)
                                break;
                            audiotrack->channels = num;
                            break;
                        }

                        default:
                            av_log(matroska->ctx, AV_LOG_INFO,
                                   "Unknown audio track header entry "
                                   "0x%x - ignoring\n", id);
                            /* pass-through */

                        case EBML_ID_VOID:
                            res = ebml_read_skip(matroska);
                            break;
                    }

                    if (matroska->level_up) {
                        matroska->level_up--;
                        break;
                    }
                }
                break;
            }

                /* codec identifier */
            case MATROSKA_ID_CODECID: {
                char *text;
                if ((res = ebml_read_ascii(matroska, &id, &text)) < 0)
                    break;
                track->codec_id = text;
                break;
            }

                /* codec private data */
            case MATROSKA_ID_CODECPRIVATE: {
                uint8_t *data;
                int size;
                if ((res = ebml_read_binary(matroska, &id, &data, &size) < 0))
                    break;
                track->codec_priv = data;
                track->codec_priv_size = size;
                break;
            }

                /* name of the codec */
            case MATROSKA_ID_CODECNAME: {
                char *text;
                if ((res = ebml_read_utf8(matroska, &id, &text)) < 0)
                    break;
                track->codec_name = text;
                break;
            }

                /* name of this track */
            case MATROSKA_ID_TRACKNAME: {
                char *text;
                if ((res = ebml_read_utf8(matroska, &id, &text)) < 0)
                    break;
                track->name = text;
                break;
            }

                /* language (matters for audio/subtitles, mostly) */
            case MATROSKA_ID_TRACKLANGUAGE: {
                char *text, *end;
                if ((res = ebml_read_utf8(matroska, &id, &text)) < 0)
                    break;
                if ((end = strchr(text, '-')))
                    *end = '\0';
                if (strlen(text) == 3)
                    strcpy(track->language, text);
                av_free(text);
                break;
            }

                /* whether this is actually used */
            case MATROSKA_ID_TRACKFLAGENABLED: {
                uint64_t num;
                if ((res = ebml_read_uint(matroska, &id, &num)) < 0)
                    break;
                if (num)
                    track->flags |= MATROSKA_TRACK_ENABLED;
                else
                    track->flags &= ~MATROSKA_TRACK_ENABLED;
                break;
            }

                /* whether it's the default for this track type */
            case MATROSKA_ID_TRACKFLAGDEFAULT: {
                uint64_t num;
                if ((res = ebml_read_uint(matroska, &id, &num)) < 0)
                    break;
                if (num)
                    track->flags |= MATROSKA_TRACK_DEFAULT;
                else
                    track->flags &= ~MATROSKA_TRACK_DEFAULT;
                break;
            }

                /* lacing (like MPEG, where blocks don't end/start on frame
                 * boundaries) */
            case MATROSKA_ID_TRACKFLAGLACING: {
                uint64_t num;
                if ((res = ebml_read_uint(matroska, &id, &num)) < 0)
                    break;
                if (num)
                    track->flags |= MATROSKA_TRACK_LACING;
                else
                    track->flags &= ~MATROSKA_TRACK_LACING;
                break;
            }

                /* default length (in time) of one data block in this track */
            case MATROSKA_ID_TRACKDEFAULTDURATION: {
                uint64_t num;
                if ((res = ebml_read_uint(matroska, &id, &num)) < 0)
                    break;
                track->default_duration = num;
                break;
            }

            default:
                av_log(matroska->ctx, AV_LOG_INFO,
                       "Unknown track header entry 0x%x - ignoring\n", id);
                /* pass-through */

            case EBML_ID_VOID:
            /* we ignore these because they're nothing useful. */
            case MATROSKA_ID_CODECINFOURL:
            case MATROSKA_ID_CODECDOWNLOADURL:
            case MATROSKA_ID_TRACKMINCACHE:
            case MATROSKA_ID_TRACKMAXCACHE:
                res = ebml_read_skip(matroska);
                break;
        }

        if (matroska->level_up) {
            matroska->level_up--;
            break;
        }
    }

    return res;
}

static int
matroska_parse_tracks (MatroskaDemuxContext *matroska)
{
    int res = 0;
    uint32_t id;

    av_log(matroska->ctx, AV_LOG_DEBUG, "parsing tracks...\n");

    while (res == 0) {
        if (!(id = ebml_peek_id(matroska, &matroska->level_up))) {
            res = AVERROR(EIO);
            break;
        } else if (matroska->level_up) {
            matroska->level_up--;
            break;
        }

        switch (id) {
            /* one track within the "all-tracks" header */
            case MATROSKA_ID_TRACKENTRY:
                res = matroska_add_stream(matroska);
                break;

            default:
                av_log(matroska->ctx, AV_LOG_INFO,
                       "Unknown entry 0x%x in track header\n", id);
                /* fall-through */

            case EBML_ID_VOID:
                res = ebml_read_skip(matroska);
                break;
        }

        if (matroska->level_up) {
            matroska->level_up--;
            break;
        }
    }

    return res;
}

static int
matroska_parse_index (MatroskaDemuxContext *matroska)
{
    int res = 0;
    uint32_t id;
    MatroskaDemuxIndex idx;

    av_log(matroska->ctx, AV_LOG_DEBUG, "parsing index...\n");

    while (res == 0) {
        if (!(id = ebml_peek_id(matroska, &matroska->level_up))) {
            res = AVERROR(EIO);
            break;
        } else if (matroska->level_up) {
            matroska->level_up--;
            break;
        }

        switch (id) {
            /* one single index entry ('point') */
            case MATROSKA_ID_POINTENTRY:
                if ((res = ebml_read_master(matroska, &id)) < 0)
                    break;

                /* in the end, we hope to fill one entry with a
                 * timestamp, a file position and a tracknum */
                idx.pos   = (uint64_t) -1;
                idx.time  = (uint64_t) -1;
                idx.track = (uint16_t) -1;

                while (res == 0) {
                    if (!(id = ebml_peek_id(matroska, &matroska->level_up))) {
                        res = AVERROR(EIO);
                        break;
                    } else if (matroska->level_up) {
                        matroska->level_up--;
                        break;
                    }

                    switch (id) {
                        /* one single index entry ('point') */
                        case MATROSKA_ID_CUETIME: {
                            uint64_t time;
                            if ((res = ebml_read_uint(matroska, &id,
                                                      &time)) < 0)
                                break;
                            idx.time = time * matroska->time_scale;
                            break;
                        }

                        /* position in the file + track to which it
                         * belongs */
                        case MATROSKA_ID_CUETRACKPOSITION:
                            if ((res = ebml_read_master(matroska, &id)) < 0)
                                break;

                            while (res == 0) {
                                if (!(id = ebml_peek_id (matroska,
                                                    &matroska->level_up))) {
                                    res = AVERROR(EIO);
                                    break;
                                } else if (matroska->level_up) {
                                    matroska->level_up--;
                                    break;
                                }

                                switch (id) {
                                    /* track number */
                                    case MATROSKA_ID_CUETRACK: {
                                        uint64_t num;
                                        if ((res = ebml_read_uint(matroska,
                                                          &id, &num)) < 0)
                                            break;
                                        idx.track = num;
                                        break;
                                    }

                                        /* position in file */
                                    case MATROSKA_ID_CUECLUSTERPOSITION: {
                                        uint64_t num;
                                        if ((res = ebml_read_uint(matroska,
                                                          &id, &num)) < 0)
                                            break;
                                        idx.pos = num+matroska->segment_start;
                                        break;
                                    }

                                    default:
                                        av_log(matroska->ctx, AV_LOG_INFO,
                                               "Unknown entry 0x%x in "
                                               "CuesTrackPositions\n", id);
                                        /* fall-through */

                                    case EBML_ID_VOID:
                                        res = ebml_read_skip(matroska);
                                        break;
                                }

                                if (matroska->level_up) {
                                    matroska->level_up--;
                                    break;
                                }
                            }

                            break;

                        default:
                            av_log(matroska->ctx, AV_LOG_INFO,
                                   "Unknown entry 0x%x in cuespoint "
                                   "index\n", id);
                            /* fall-through */

                        case EBML_ID_VOID:
                            res = ebml_read_skip(matroska);
                            break;
                    }

                    if (matroska->level_up) {
                        matroska->level_up--;
                        break;
                    }
                }

                /* so let's see if we got what we wanted */
                if (idx.pos   != (uint64_t) -1 &&
                    idx.time  != (uint64_t) -1 &&
                    idx.track != (uint16_t) -1) {
                    if (matroska->num_indexes % 32 == 0) {
                        /* re-allocate bigger index */
                        matroska->index =
                            av_realloc(matroska->index,
                                       (matroska->num_indexes + 32) *
                                       sizeof(MatroskaDemuxIndex));
                    }
                    matroska->index[matroska->num_indexes] = idx;
                    matroska->num_indexes++;
                }
                break;

            default:
                av_log(matroska->ctx, AV_LOG_INFO,
                       "Unknown entry 0x%x in cues header\n", id);
                /* fall-through */

            case EBML_ID_VOID:
                res = ebml_read_skip(matroska);
                break;
        }

        if (matroska->level_up) {
            matroska->level_up--;
            break;
        }
    }

    return res;
}

static int
matroska_parse_metadata (MatroskaDemuxContext *matroska)
{
    int res = 0;
    uint32_t id;

    while (res == 0) {
        if (!(id = ebml_peek_id(matroska, &matroska->level_up))) {
            res = AVERROR(EIO);
            break;
        } else if (matroska->level_up) {
            matroska->level_up--;
            break;
        }

        switch (id) {
            /* Hm, this is unsupported... */
            default:
                av_log(matroska->ctx, AV_LOG_INFO,
                       "Unknown entry 0x%x in metadata header\n", id);
                /* fall-through */

            case EBML_ID_VOID:
                res = ebml_read_skip(matroska);
                break;
        }

        if (matroska->level_up) {
            matroska->level_up--;
            break;
        }
    }

    return res;
}

static int
matroska_parse_seekhead (MatroskaDemuxContext *matroska)
{
    int res = 0;
    uint32_t id;

    av_log(matroska->ctx, AV_LOG_DEBUG, "parsing seekhead...\n");

    while (res == 0) {
        if (!(id = ebml_peek_id(matroska, &matroska->level_up))) {
            res = AVERROR(EIO);
            break;
        } else if (matroska->level_up) {
            matroska->level_up--;
            break;
        }

        switch (id) {
            case MATROSKA_ID_SEEKENTRY: {
                uint32_t seek_id = 0, peek_id_cache = 0;
                uint64_t seek_pos = (uint64_t) -1, t;

                if ((res = ebml_read_master(matroska, &id)) < 0)
                    break;

                while (res == 0) {
                    if (!(id = ebml_peek_id(matroska, &matroska->level_up))) {
                        res = AVERROR(EIO);
                        break;
                    } else if (matroska->level_up) {
                        matroska->level_up--;
                        break;
                    }

                    switch (id) {
                        case MATROSKA_ID_SEEKID:
                            res = ebml_read_uint(matroska, &id, &t);
                            seek_id = t;
                            break;

                        case MATROSKA_ID_SEEKPOSITION:
                            res = ebml_read_uint(matroska, &id, &seek_pos);
                            break;

                        default:
                            av_log(matroska->ctx, AV_LOG_INFO,
                                   "Unknown seekhead ID 0x%x\n", id);
                            /* fall-through */

                        case EBML_ID_VOID:
                            res = ebml_read_skip(matroska);
                            break;
                    }

                    if (matroska->level_up) {
                        matroska->level_up--;
                        break;
                    }
                }

                if (!seek_id || seek_pos == (uint64_t) -1) {
                    av_log(matroska->ctx, AV_LOG_INFO,
                           "Incomplete seekhead entry (0x%x/%"PRIu64")\n",
                           seek_id, seek_pos);
                    break;
                }

                switch (seek_id) {
                    case MATROSKA_ID_CUES:
                    case MATROSKA_ID_TAGS: {
                        uint32_t level_up = matroska->level_up;
                        offset_t before_pos;
                        uint64_t length;
                        MatroskaLevel level;

                        /* remember the peeked ID and the current position */
                        peek_id_cache = matroska->peek_id;
                        before_pos = url_ftell(matroska->ctx->pb);

                        /* seek */
                        if ((res = ebml_read_seek(matroska, seek_pos +
                                               matroska->segment_start)) < 0)
                            return res;

                        /* we don't want to lose our seekhead level, so we add
                         * a dummy. This is a crude hack. */
                        if (matroska->num_levels == EBML_MAX_DEPTH) {
                            av_log(matroska->ctx, AV_LOG_INFO,
                                   "Max EBML element depth (%d) reached, "
                                   "cannot parse further.\n", EBML_MAX_DEPTH);
                            return AVERROR_UNKNOWN;
                        }

                        level.start = 0;
                        level.length = (uint64_t)-1;
                        matroska->levels[matroska->num_levels] = level;
                        matroska->num_levels++;

                        /* check ID */
                        if (!(id = ebml_peek_id (matroska,
                                                 &matroska->level_up)))
                            goto finish;
                        if (id != seek_id) {
                            av_log(matroska->ctx, AV_LOG_INFO,
                                   "We looked for ID=0x%x but got "
                                   "ID=0x%x (pos=%"PRIu64")",
                                   seek_id, id, seek_pos +
                                   matroska->segment_start);
                            goto finish;
                        }

                        /* read master + parse */
                        if ((res = ebml_read_master(matroska, &id)) < 0)
                            goto finish;
                        switch (id) {
                            case MATROSKA_ID_CUES:
                                if (!(res = matroska_parse_index(matroska)) ||
                                    url_feof(matroska->ctx->pb)) {
                                    matroska->index_parsed = 1;
                                    res = 0;
                                }
                                break;
                            case MATROSKA_ID_TAGS:
                                if (!(res = matroska_parse_metadata(matroska)) ||
                                   url_feof(matroska->ctx->pb)) {
                                    matroska->metadata_parsed = 1;
                                    res = 0;
                                }
                                break;
                        }

                    finish:
                        /* remove dummy level */
                        while (matroska->num_levels) {
                            matroska->num_levels--;
                            length =
                                matroska->levels[matroska->num_levels].length;
                            if (length == (uint64_t)-1)
                                break;
                        }

                        /* seek back */
                        if ((res = ebml_read_seek(matroska, before_pos)) < 0)
                            return res;
                        matroska->peek_id = peek_id_cache;
                        matroska->level_up = level_up;
                        break;
                    }

                    default:
                        av_log(matroska->ctx, AV_LOG_INFO,
                               "Ignoring seekhead entry for ID=0x%x\n",
                               seek_id);
                        break;
                }

                break;
            }

            default:
                av_log(matroska->ctx, AV_LOG_INFO,
                       "Unknown seekhead ID 0x%x\n", id);
                /* fall-through */

            case EBML_ID_VOID:
                res = ebml_read_skip(matroska);
                break;
        }

        if (matroska->level_up) {
            matroska->level_up--;
            break;
        }
    }

    return res;
}

static int
matroska_parse_attachments(AVFormatContext *s)
{
    MatroskaDemuxContext *matroska = s->priv_data;
    int res = 0;
    uint32_t id;

    av_log(matroska->ctx, AV_LOG_DEBUG, "parsing attachments...\n");

    while (res == 0) {
        if (!(id = ebml_peek_id(matroska, &matroska->level_up))) {
            res = AVERROR(EIO);
            break;
        } else if (matroska->level_up) {
            matroska->level_up--;
            break;
        }

        switch (id) {
        case MATROSKA_ID_ATTACHEDFILE: {
            char* name = NULL;
            char* mime = NULL;
            uint8_t* data = NULL;
            int i, data_size = 0;
            AVStream *st;

            if ((res = ebml_read_master(matroska, &id)) < 0)
                break;

            while (res == 0) {
                if (!(id = ebml_peek_id(matroska, &matroska->level_up))) {
                    res = AVERROR(EIO);
                    break;
                } else if (matroska->level_up) {
                    matroska->level_up--;
                    break;
                }

                switch (id) {
                case MATROSKA_ID_FILENAME:
                    res = ebml_read_utf8 (matroska, &id, &name);
                    break;

                case MATROSKA_ID_FILEMIMETYPE:
                    res = ebml_read_ascii (matroska, &id, &mime);
                    break;

                case MATROSKA_ID_FILEDATA:
                    res = ebml_read_binary(matroska, &id, &data, &data_size);
                    break;

                default:
                    av_log(matroska->ctx, AV_LOG_INFO,
                           "Unknown attachedfile ID 0x%x\n", id);
                case EBML_ID_VOID:
                    res = ebml_read_skip(matroska);
                    break;
                }

                if (matroska->level_up) {
                    matroska->level_up--;
                    break;
                }
            }

            if (!(name && mime && data && data_size > 0)) {
                av_log(matroska->ctx, AV_LOG_ERROR, "incomplete attachment\n");
                break;
            }

            st = av_new_stream(s, matroska->num_streams++);
            if (st == NULL)
                return AVERROR(ENOMEM);
            st->filename = av_strdup(name);
            st->codec->codec_id = CODEC_ID_NONE;
            st->codec->codec_type = CODEC_TYPE_ATTACHMENT;
            st->codec->extradata = av_malloc(data_size);
            if(st->codec->extradata == NULL)
                return AVERROR(ENOMEM);
            st->codec->extradata_size = data_size;
            memcpy(st->codec->extradata, data, data_size);

            for (i=0; ff_mkv_mime_tags[i].id != CODEC_ID_NONE; i++) {
                if (!strncmp(ff_mkv_mime_tags[i].str, mime,
                             strlen(ff_mkv_mime_tags[i].str))) {
                    st->codec->codec_id = ff_mkv_mime_tags[i].id;
                    break;
                }
            }

            av_log(matroska->ctx, AV_LOG_DEBUG, "new attachment: %s, %s, size %d \n", name, mime, data_size);
            break;
        }

        default:
            av_log(matroska->ctx, AV_LOG_INFO,
                   "Unknown attachments ID 0x%x\n", id);
            /* fall-through */

        case EBML_ID_VOID:
            res = ebml_read_skip(matroska);
            break;
        }

        if (matroska->level_up) {
            matroska->level_up--;
            break;
        }
    }

    return res;
}

#define ARRAY_SIZE(x)  (sizeof(x)/sizeof(*x))

static int
matroska_aac_profile (char *codec_id)
{
    static const char *aac_profiles[] = {
        "MAIN", "LC", "SSR"
    };
    int profile;

    for (profile=0; profile<ARRAY_SIZE(aac_profiles); profile++)
        if (strstr(codec_id, aac_profiles[profile]))
            break;
    return profile + 1;
}

static int
matroska_aac_sri (int samplerate)
{
    int sri;

    for (sri=0; sri<ARRAY_SIZE(ff_mpeg4audio_sample_rates); sri++)
        if (ff_mpeg4audio_sample_rates[sri] == samplerate)
            break;
    return sri;
}

static int
matroska_read_header (AVFormatContext    *s,
                      AVFormatParameters *ap)
{
    MatroskaDemuxContext *matroska = s->priv_data;
    char *doctype;
    int version, last_level, res = 0;
    uint32_t id;

    matroska->ctx = s;

    /* First read the EBML header. */
    doctype = NULL;
    if ((res = ebml_read_header(matroska, &doctype, &version)) < 0)
        return res;
    if ((doctype == NULL) || strcmp(doctype, "matroska")) {
        av_log(matroska->ctx, AV_LOG_ERROR,
               "Wrong EBML doctype ('%s' != 'matroska').\n",
               doctype ? doctype : "(none)");
        if (doctype)
            av_free(doctype);
        return AVERROR_NOFMT;
    }
    av_free(doctype);
    if (version > 2) {
        av_log(matroska->ctx, AV_LOG_ERROR,
               "Matroska demuxer version 2 too old for file version %d\n",
               version);
        return AVERROR_NOFMT;
    }

    /* The next thing is a segment. */
    while (1) {
        if (!(id = ebml_peek_id(matroska, &last_level)))
            return AVERROR(EIO);
        if (id == MATROSKA_ID_SEGMENT)
            break;

        /* oi! */
        av_log(matroska->ctx, AV_LOG_INFO,
               "Expected a Segment ID (0x%x), but received 0x%x!\n",
               MATROSKA_ID_SEGMENT, id);
        if ((res = ebml_read_skip(matroska)) < 0)
            return res;
    }

    /* We now have a Matroska segment.
     * Seeks are from the beginning of the segment,
     * after the segment ID/length. */
    if ((res = ebml_read_master(matroska, &id)) < 0)
        return res;
    matroska->segment_start = url_ftell(s->pb);

    matroska->time_scale = 1000000;
    /* we've found our segment, start reading the different contents in here */
    while (res == 0) {
        if (!(id = ebml_peek_id(matroska, &matroska->level_up))) {
            res = AVERROR(EIO);
            break;
        } else if (matroska->level_up) {
            matroska->level_up--;
            break;
        }

        switch (id) {
            /* stream info */
            case MATROSKA_ID_INFO: {
                if ((res = ebml_read_master(matroska, &id)) < 0)
                    break;
                res = matroska_parse_info(matroska);
                break;
            }

            /* track info headers */
            case MATROSKA_ID_TRACKS: {
                if ((res = ebml_read_master(matroska, &id)) < 0)
                    break;
                res = matroska_parse_tracks(matroska);
                break;
            }

            /* stream index */
            case MATROSKA_ID_CUES: {
                if (!matroska->index_parsed) {
                    if ((res = ebml_read_master(matroska, &id)) < 0)
                        break;
                    res = matroska_parse_index(matroska);
                } else
                    res = ebml_read_skip(matroska);
                break;
            }

            /* metadata */
            case MATROSKA_ID_TAGS: {
                if (!matroska->metadata_parsed) {
                    if ((res = ebml_read_master(matroska, &id)) < 0)
                        break;
                    res = matroska_parse_metadata(matroska);
                } else
                    res = ebml_read_skip(matroska);
                break;
            }

            /* file index (if seekable, seek to Cues/Tags to parse it) */
            case MATROSKA_ID_SEEKHEAD: {
                if ((res = ebml_read_master(matroska, &id)) < 0)
                    break;
                res = matroska_parse_seekhead(matroska);
                break;
            }

            case MATROSKA_ID_ATTACHMENTS: {
                if ((res = ebml_read_master(matroska, &id)) < 0)
                    break;
                res = matroska_parse_attachments(s);
                break;
            }

            case MATROSKA_ID_CLUSTER: {
                /* Do not read the master - this will be done in the next
                 * call to matroska_read_packet. */
                res = 1;
                break;
            }

            default:
                av_log(matroska->ctx, AV_LOG_INFO,
                       "Unknown matroska file header ID 0x%x\n", id);
            /* fall-through */

            case EBML_ID_VOID:
                res = ebml_read_skip(matroska);
                break;
        }

        if (matroska->level_up) {
            matroska->level_up--;
            break;
        }
    }

    /* Have we found a cluster? */
    if (ebml_peek_id(matroska, NULL) == MATROSKA_ID_CLUSTER) {
        int i, j;
        MatroskaTrack *track;
        AVStream *st;

        for (i = 0; i < matroska->num_tracks; i++) {
            enum CodecID codec_id = CODEC_ID_NONE;
            uint8_t *extradata = NULL;
            int extradata_size = 0;
            int extradata_offset = 0;
            track = matroska->tracks[i];
            track->stream_index = -1;

            /* Apply some sanity checks. */
            if (track->codec_id == NULL)
                continue;

            for(j=0; ff_mkv_codec_tags[j].id != CODEC_ID_NONE; j++){
                if(!strncmp(ff_mkv_codec_tags[j].str, track->codec_id,
                            strlen(ff_mkv_codec_tags[j].str))){
                    codec_id= ff_mkv_codec_tags[j].id;
                    break;
                }
            }

            /* Set the FourCC from the CodecID. */
            /* This is the MS compatibility mode which stores a
             * BITMAPINFOHEADER in the CodecPrivate. */
            if (!strcmp(track->codec_id,
                        MATROSKA_CODEC_ID_VIDEO_VFW_FOURCC) &&
                (track->codec_priv_size >= 40) &&
                (track->codec_priv != NULL)) {
                MatroskaVideoTrack *vtrack = (MatroskaVideoTrack *) track;

                /* Offset of biCompression. Stored in LE. */
                vtrack->fourcc = AV_RL32(track->codec_priv + 16);
                codec_id = codec_get_id(codec_bmp_tags, vtrack->fourcc);

            }

            /* This is the MS compatibility mode which stores a
             * WAVEFORMATEX in the CodecPrivate. */
            else if (!strcmp(track->codec_id,
                             MATROSKA_CODEC_ID_AUDIO_ACM) &&
                (track->codec_priv_size >= 18) &&
                (track->codec_priv != NULL)) {
                uint16_t tag;

                /* Offset of wFormatTag. Stored in LE. */
                tag = AV_RL16(track->codec_priv);
                codec_id = codec_get_id(codec_wav_tags, tag);

            }

            else if (codec_id == CODEC_ID_AAC && !track->codec_priv_size) {
                MatroskaAudioTrack *audiotrack = (MatroskaAudioTrack *) track;
                int profile = matroska_aac_profile(track->codec_id);
                int sri = matroska_aac_sri(audiotrack->internal_samplerate);
                extradata = av_malloc(5);
                if (extradata == NULL)
                    return AVERROR(ENOMEM);
                extradata[0] = (profile << 3) | ((sri&0x0E) >> 1);
                extradata[1] = ((sri&0x01) << 7) | (audiotrack->channels<<3);
                if (strstr(track->codec_id, "SBR")) {
                    sri = matroska_aac_sri(audiotrack->samplerate);
                    extradata[2] = 0x56;
                    extradata[3] = 0xE5;
                    extradata[4] = 0x80 | (sri<<3);
                    extradata_size = 5;
                } else {
                    extradata_size = 2;
                }
            }

            else if (codec_id == CODEC_ID_TTA) {
                MatroskaAudioTrack *audiotrack = (MatroskaAudioTrack *) track;
                ByteIOContext b;
                extradata_size = 30;
                extradata = av_mallocz(extradata_size);
                if (extradata == NULL)
                    return AVERROR(ENOMEM);
                init_put_byte(&b, extradata, extradata_size, 1,
                              NULL, NULL, NULL, NULL);
                put_buffer(&b, "TTA1", 4);
                put_le16(&b, 1);
                put_le16(&b, audiotrack->channels);
                put_le16(&b, audiotrack->bitdepth);
                put_le32(&b, audiotrack->samplerate);
                put_le32(&b, matroska->ctx->duration * audiotrack->samplerate);
            }

            else if (codec_id == CODEC_ID_RV10 || codec_id == CODEC_ID_RV20 ||
                     codec_id == CODEC_ID_RV30 || codec_id == CODEC_ID_RV40) {
                extradata_offset = 26;
                track->codec_priv_size -= extradata_offset;
            }

            else if (codec_id == CODEC_ID_RA_144) {
                MatroskaAudioTrack *audiotrack = (MatroskaAudioTrack *)track;
                audiotrack->samplerate = 8000;
                audiotrack->channels = 1;
            }

            else if (codec_id == CODEC_ID_RA_288 ||
                     codec_id == CODEC_ID_COOK ||
                     codec_id == CODEC_ID_ATRAC3) {
                MatroskaAudioTrack *audiotrack = (MatroskaAudioTrack *)track;
                ByteIOContext b;

                init_put_byte(&b, track->codec_priv, track->codec_priv_size, 0,
                              NULL, NULL, NULL, NULL);
                url_fskip(&b, 24);
                audiotrack->coded_framesize = get_be32(&b);
                url_fskip(&b, 12);
                audiotrack->sub_packet_h    = get_be16(&b);
                audiotrack->frame_size      = get_be16(&b);
                audiotrack->sub_packet_size = get_be16(&b);
                audiotrack->buf = av_malloc(audiotrack->frame_size * audiotrack->sub_packet_h);
                if (codec_id == CODEC_ID_RA_288) {
                    audiotrack->block_align = audiotrack->coded_framesize;
                    track->codec_priv_size = 0;
                } else {
                    audiotrack->block_align = audiotrack->sub_packet_size;
                    extradata_offset = 78;
                    track->codec_priv_size -= extradata_offset;
                }
            }

            if (codec_id == CODEC_ID_NONE) {
                av_log(matroska->ctx, AV_LOG_INFO,
                       "Unknown/unsupported CodecID %s.\n",
                       track->codec_id);
            }

            track->stream_index = matroska->num_streams;

            matroska->num_streams++;
            st = av_new_stream(s, track->stream_index);
            if (st == NULL)
                return AVERROR(ENOMEM);
            av_set_pts_info(st, 64, matroska->time_scale, 1000*1000*1000); /* 64 bit pts in ns */

            st->codec->codec_id = codec_id;
            st->start_time = 0;
            if (strcmp(track->language, "und"))
                strcpy(st->language, track->language);

            if (track->flags & MATROSKA_TRACK_DEFAULT)
                st->disposition |= AV_DISPOSITION_DEFAULT;

            if (track->default_duration)
                av_reduce(&st->codec->time_base.num, &st->codec->time_base.den,
                          track->default_duration, 1000000000, 30000);

            if(extradata){
                st->codec->extradata = extradata;
                st->codec->extradata_size = extradata_size;
            } else if(track->codec_priv && track->codec_priv_size > 0){
                st->codec->extradata = av_malloc(track->codec_priv_size);
                if(st->codec->extradata == NULL)
                    return AVERROR(ENOMEM);
                st->codec->extradata_size = track->codec_priv_size;
                memcpy(st->codec->extradata,track->codec_priv+extradata_offset,
                       track->codec_priv_size);
            }

            if (track->type == MATROSKA_TRACK_TYPE_VIDEO) {
                MatroskaVideoTrack *videotrack = (MatroskaVideoTrack *)track;

                st->codec->codec_type = CODEC_TYPE_VIDEO;
                st->codec->codec_tag = videotrack->fourcc;
                st->codec->width = videotrack->pixel_width;
                st->codec->height = videotrack->pixel_height;
                if (videotrack->display_width == 0)
                    videotrack->display_width= videotrack->pixel_width;
                if (videotrack->display_height == 0)
                    videotrack->display_height= videotrack->pixel_height;
                av_reduce(&st->codec->sample_aspect_ratio.num,
                          &st->codec->sample_aspect_ratio.den,
                          st->codec->height * videotrack->display_width,
                          st->codec-> width * videotrack->display_height,
                          255);
                st->need_parsing = AVSTREAM_PARSE_HEADERS;
            } else if (track->type == MATROSKA_TRACK_TYPE_AUDIO) {
                MatroskaAudioTrack *audiotrack = (MatroskaAudioTrack *)track;

                st->codec->codec_type = CODEC_TYPE_AUDIO;
                st->codec->sample_rate = audiotrack->samplerate;
                st->codec->channels = audiotrack->channels;
                st->codec->block_align = audiotrack->block_align;
            } else if (track->type == MATROSKA_TRACK_TYPE_SUBTITLE) {
                st->codec->codec_type = CODEC_TYPE_SUBTITLE;
            }

            /* What do we do with private data? E.g. for Vorbis. */
        }
        res = 0;
    }

    if (matroska->index_parsed) {
        int i, track, stream;
        for (i=0; i<matroska->num_indexes; i++) {
            MatroskaDemuxIndex *idx = &matroska->index[i];
            track = matroska_find_track_by_num(matroska, idx->track);
            if (track < 0)  continue;
            stream = matroska->tracks[track]->stream_index;
            if (stream >= 0 && stream < matroska->ctx->nb_streams)
                av_add_index_entry(matroska->ctx->streams[stream],
                                   idx->pos, idx->time/matroska->time_scale,
                                   0, 0, AVINDEX_KEYFRAME);
        }
    }

    return res;
}

static int
matroska_parse_block(MatroskaDemuxContext *matroska, uint8_t *data, int size,
                     int64_t pos, uint64_t cluster_time, uint64_t duration,
                     int is_keyframe, int is_bframe)
{
    int res = 0;
    int track;
    AVStream *st;
    AVPacket *pkt;
    uint8_t *origdata = data;
    int16_t block_time;
    uint32_t *lace_size = NULL;
    int n, flags, laces = 0;
    uint64_t num;
    int stream_index;

    /* first byte(s): tracknum */
    if ((n = matroska_ebmlnum_uint(data, size, &num)) < 0) {
        av_log(matroska->ctx, AV_LOG_ERROR, "EBML block data error\n");
        av_free(origdata);
        return res;
    }
    data += n;
    size -= n;

    /* fetch track from num */
    track = matroska_find_track_by_num(matroska, num);
    if (size <= 3 || track < 0 || track >= matroska->num_tracks) {
        av_log(matroska->ctx, AV_LOG_INFO,
               "Invalid stream %d or size %u\n", track, size);
        av_free(origdata);
        return res;
    }
    stream_index = matroska->tracks[track]->stream_index;
    if (stream_index < 0 || stream_index >= matroska->ctx->nb_streams) {
        av_free(origdata);
        return res;
    }
    st = matroska->ctx->streams[stream_index];
    if (st->discard >= AVDISCARD_ALL) {
        av_free(origdata);
        return res;
    }
    if (duration == AV_NOPTS_VALUE)
        duration = matroska->tracks[track]->default_duration / matroska->time_scale;

    /* block_time (relative to cluster time) */
    block_time = AV_RB16(data);
    data += 2;
    flags = *data++;
    size -= 3;
    if (is_keyframe == -1)
        is_keyframe = flags & 0x80 ? PKT_FLAG_KEY : 0;

    if (matroska->skip_to_keyframe) {
        if (!is_keyframe || st != matroska->skip_to_stream) {
            av_free(origdata);
            return res;
        }
        matroska->skip_to_keyframe = 0;
    }

    switch ((flags & 0x06) >> 1) {
        case 0x0: /* no lacing */
            laces = 1;
            lace_size = av_mallocz(sizeof(int));
            lace_size[0] = size;
            break;

        case 0x1: /* xiph lacing */
        case 0x2: /* fixed-size lacing */
        case 0x3: /* EBML lacing */
            if (size == 0) {
                res = -1;
                break;
            }
            laces = (*data) + 1;
            data += 1;
            size -= 1;
            lace_size = av_mallocz(laces * sizeof(int));

            switch ((flags & 0x06) >> 1) {
                case 0x1: /* xiph lacing */ {
                    uint8_t temp;
                    uint32_t total = 0;
                    for (n = 0; res == 0 && n < laces - 1; n++) {
                        while (1) {
                            if (size == 0) {
                                res = -1;
                                break;
                            }
                            temp = *data;
                            lace_size[n] += temp;
                            data += 1;
                            size -= 1;
                            if (temp != 0xff)
                                break;
                        }
                        total += lace_size[n];
                    }
                    lace_size[n] = size - total;
                    break;
                }

                case 0x2: /* fixed-size lacing */
                    for (n = 0; n < laces; n++)
                        lace_size[n] = size / laces;
                    break;

                case 0x3: /* EBML lacing */ {
                    uint32_t total;
                    n = matroska_ebmlnum_uint(data, size, &num);
                    if (n < 0) {
                        av_log(matroska->ctx, AV_LOG_INFO,
                               "EBML block data error\n");
                        break;
                    }
                    data += n;
                    size -= n;
                    total = lace_size[0] = num;
                    for (n = 1; res == 0 && n < laces - 1; n++) {
                        int64_t snum;
                        int r;
                        r = matroska_ebmlnum_sint (data, size, &snum);
                        if (r < 0) {
                            av_log(matroska->ctx, AV_LOG_INFO,
                                   "EBML block data error\n");
                            break;
                        }
                        data += r;
                        size -= r;
                        lace_size[n] = lace_size[n - 1] + snum;
                        total += lace_size[n];
                    }
                    lace_size[n] = size - total;
                    break;
                }
            }
            break;
    }

    if (res == 0) {
        uint64_t timecode = AV_NOPTS_VALUE;

        if (cluster_time != (uint64_t)-1
            && (block_time >= 0 || cluster_time >= -block_time))
            timecode = cluster_time + block_time;

        for (n = 0; n < laces; n++) {
            if (st->codec->codec_id == CODEC_ID_RA_288 ||
                st->codec->codec_id == CODEC_ID_COOK ||
                st->codec->codec_id == CODEC_ID_ATRAC3) {
                MatroskaAudioTrack *audiotrack = (MatroskaAudioTrack *)matroska->tracks[track];
                int a = st->codec->block_align;
                int sps = audiotrack->sub_packet_size;
                int cfs = audiotrack->coded_framesize;
                int h = audiotrack->sub_packet_h;
                int y = audiotrack->sub_packet_cnt;
                int w = audiotrack->frame_size;
                int x;

                if (!audiotrack->pkt_cnt) {
                    if (st->codec->codec_id == CODEC_ID_RA_288)
                        for (x=0; x<h/2; x++)
                            memcpy(audiotrack->buf+x*2*w+y*cfs,
                                   data+x*cfs, cfs);
                    else
                        for (x=0; x<w/sps; x++)
                            memcpy(audiotrack->buf+sps*(h*x+((h+1)/2)*(y&1)+(y>>1)), data+x*sps, sps);

                    if (++audiotrack->sub_packet_cnt >= h) {
                        audiotrack->sub_packet_cnt = 0;
                        audiotrack->pkt_cnt = h*w / a;
                    }
                }
                while (audiotrack->pkt_cnt) {
                    pkt = av_mallocz(sizeof(AVPacket));
                    av_new_packet(pkt, a);
                    memcpy(pkt->data, audiotrack->buf
                           + a * (h*w / a - audiotrack->pkt_cnt--), a);
                    pkt->pos = pos;
                    pkt->stream_index = stream_index;
                    matroska_queue_packet(matroska, pkt);
                }
            } else {
                int offset = 0;

                pkt = av_mallocz(sizeof(AVPacket));
                /* XXX: prevent data copy... */
                if (av_new_packet(pkt, lace_size[n]-offset) < 0) {
                    res = AVERROR(ENOMEM);
                    n = laces-1;
                    break;
                }
                memcpy (pkt->data, data+offset, lace_size[n]-offset);

                if (n == 0)
                    pkt->flags = is_keyframe;
                pkt->stream_index = stream_index;

                pkt->pts = timecode;
                pkt->pos = pos;
                pkt->duration = duration;

                matroska_queue_packet(matroska, pkt);
            }

            if (timecode != AV_NOPTS_VALUE)
                timecode = duration ? timecode + duration : AV_NOPTS_VALUE;
            data += lace_size[n];
        }
    }

    av_free(lace_size);
    av_free(origdata);
    return res;
}

static int
matroska_parse_blockgroup (MatroskaDemuxContext *matroska,
                           uint64_t              cluster_time)
{
    int res = 0;
    uint32_t id;
    int is_bframe = 0;
    int is_keyframe = PKT_FLAG_KEY, last_num_packets = matroska->num_packets;
    uint64_t duration = AV_NOPTS_VALUE;
    uint8_t *data;
    int size = 0;
    int64_t pos = 0;

    av_log(matroska->ctx, AV_LOG_DEBUG, "parsing blockgroup...\n");

    while (res == 0) {
        if (!(id = ebml_peek_id(matroska, &matroska->level_up))) {
            res = AVERROR(EIO);
            break;
        } else if (matroska->level_up) {
            matroska->level_up--;
            break;
        }

        switch (id) {
            /* one block inside the group. Note, block parsing is one
             * of the harder things, so this code is a bit complicated.
             * See http://www.matroska.org/ for documentation. */
            case MATROSKA_ID_BLOCK: {
                pos = url_ftell(matroska->ctx->pb);
                res = ebml_read_binary(matroska, &id, &data, &size);
                break;
            }

            case MATROSKA_ID_BLOCKDURATION: {
                if ((res = ebml_read_uint(matroska, &id, &duration)) < 0)
                    break;
                break;
            }

            case MATROSKA_ID_BLOCKREFERENCE: {
                int64_t num;
                /* We've found a reference, so not even the first frame in
                 * the lace is a key frame. */
                is_keyframe = 0;
                if (last_num_packets != matroska->num_packets)
                    matroska->packets[last_num_packets]->flags = 0;
                if ((res = ebml_read_sint(matroska, &id, &num)) < 0)
                    break;
                if (num > 0)
                    is_bframe = 1;
                break;
            }

            default:
                av_log(matroska->ctx, AV_LOG_INFO,
                       "Unknown entry 0x%x in blockgroup data\n", id);
                /* fall-through */

            case EBML_ID_VOID:
                res = ebml_read_skip(matroska);
                break;
        }

        if (matroska->level_up) {
            matroska->level_up--;
            break;
        }
    }

    if (res)
        return res;

    if (size > 0)
        res = matroska_parse_block(matroska, data, size, pos, cluster_time,
                                   duration, is_keyframe, is_bframe);

    return res;
}

static int
matroska_parse_cluster (MatroskaDemuxContext *matroska)
{
    int res = 0;
    uint32_t id;
    uint64_t cluster_time = 0;
    uint8_t *data;
    int64_t pos;
    int size;

    av_log(matroska->ctx, AV_LOG_DEBUG,
           "parsing cluster at %"PRId64"\n", url_ftell(matroska->ctx->pb));

    while (res == 0) {
        if (!(id = ebml_peek_id(matroska, &matroska->level_up))) {
            res = AVERROR(EIO);
            break;
        } else if (matroska->level_up) {
            matroska->level_up--;
            break;
        }

        switch (id) {
            /* cluster timecode */
            case MATROSKA_ID_CLUSTERTIMECODE: {
                uint64_t num;
                if ((res = ebml_read_uint(matroska, &id, &num)) < 0)
                    break;
                cluster_time = num;
                break;
            }

                /* a group of blocks inside a cluster */
            case MATROSKA_ID_BLOCKGROUP:
                if ((res = ebml_read_master(matroska, &id)) < 0)
                    break;
                res = matroska_parse_blockgroup(matroska, cluster_time);
                break;

            case MATROSKA_ID_SIMPLEBLOCK:
                pos = url_ftell(matroska->ctx->pb);
                res = ebml_read_binary(matroska, &id, &data, &size);
                if (res == 0)
                    res = matroska_parse_block(matroska, data, size, pos,
                                               cluster_time, AV_NOPTS_VALUE,
                                               -1, 0);
                break;

            default:
                av_log(matroska->ctx, AV_LOG_INFO,
                       "Unknown entry 0x%x in cluster data\n", id);
                /* fall-through */

            case EBML_ID_VOID:
                res = ebml_read_skip(matroska);
                break;
        }

        if (matroska->level_up) {
            matroska->level_up--;
            break;
        }
    }

    return res;
}

static int
matroska_read_packet (AVFormatContext *s,
                      AVPacket        *pkt)
{
    MatroskaDemuxContext *matroska = s->priv_data;
    int res;
    uint32_t id;

    /* Read stream until we have a packet queued. */
    while (matroska_deliver_packet(matroska, pkt)) {

        /* Have we already reached the end? */
        if (matroska->done)
            return AVERROR(EIO);

        res = 0;
        while (res == 0) {
            if (!(id = ebml_peek_id(matroska, &matroska->level_up))) {
                return AVERROR(EIO);
            } else if (matroska->level_up) {
                matroska->level_up--;
                break;
            }

            switch (id) {
                case MATROSKA_ID_CLUSTER:
                    if ((res = ebml_read_master(matroska, &id)) < 0)
                        break;
                    if ((res = matroska_parse_cluster(matroska)) == 0)
                        res = 1; /* Parsed one cluster, let's get out. */
                    break;

                default:
                case EBML_ID_VOID:
                    res = ebml_read_skip(matroska);
                    break;
            }

            if (matroska->level_up) {
                matroska->level_up--;
                break;
            }
        }

        if (res == -1)
            matroska->done = 1;
    }

    return 0;
}

static int
matroska_read_seek (AVFormatContext *s, int stream_index, int64_t timestamp,
                    int flags)
{
    MatroskaDemuxContext *matroska = s->priv_data;
    AVStream *st = s->streams[stream_index];
    int index;

    /* find index entry */
    index = av_index_search_timestamp(st, timestamp, flags);
    if (index < 0)
        return 0;

    matroska_clear_queue(matroska);

    /* do the seek */
    url_fseek(s->pb, st->index_entries[index].pos, SEEK_SET);
    matroska->skip_to_keyframe = !(flags & AVSEEK_FLAG_ANY);
    matroska->skip_to_stream = st;
    matroska->peek_id = 0;
    return 0;
}

static int
matroska_read_close (AVFormatContext *s)
{
    MatroskaDemuxContext *matroska = s->priv_data;
    int n = 0;

    av_free(matroska->writing_app);
    av_free(matroska->muxing_app);
    av_free(matroska->index);

    matroska_clear_queue(matroska);

    for (n = 0; n < matroska->num_tracks; n++) {
        MatroskaTrack *track = matroska->tracks[n];
        av_free(track->codec_id);
        av_free(track->codec_name);
        av_free(track->codec_priv);
        av_free(track->name);

        if (track->type == MATROSKA_TRACK_TYPE_AUDIO) {
            MatroskaAudioTrack *audiotrack = (MatroskaAudioTrack *)track;
            av_free(audiotrack->buf);
        }

        av_free(track);
    }

    return 0;
}

AVInputFormat matroska_demuxer = {
    "matroska",
    "Matroska file format",
    sizeof(MatroskaDemuxContext),
    matroska_probe,
    matroska_read_header,
    matroska_read_packet,
    matroska_read_close,
    matroska_read_seek,
};
