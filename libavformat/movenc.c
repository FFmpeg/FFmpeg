/*
 * MOV, 3GP, MP4 encoder.
 * Copyright (c) 2003 Thomas Raivio.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avformat.h"
#include "avio.h"
#include <time.h>

/*
 * Limitations
 * - Currently supports h.263, MPEG4 video codecs, and AMR audio codec.
 */

#define MOV_INDEX_CLUSTER_SIZE 16384
#define globalTimescale 1000

typedef struct MOVIentry {
    unsigned int flags, pos, len;
    unsigned int entries;
} MOVIentry;

typedef struct MOVIndex {
    int         entry;
    int         samples;
    int         mdat_size;
    offset_t    mdat_pos;
    int         ents_allocated;
    long        timescale;
    long        time;
    long        frameDuration;
    long        sampleDelta;
    int         trackID;
    AVCodecContext *enc;

    int         vosLen;
    char       *vosData;
    MOVIentry** cluster;
} MOVTrack;

typedef struct {
    long    time;
    int     nb_streams;
    offset_t movi_list;
    long    timescale;
    MOVTrack tracks[MAX_STREAMS];
} MOVContext;

void writeSize (ByteIOContext *pb, int pos, int size)
{
    long curpos = url_ftell(pb);
    url_fseek(pb, pos, SEEK_SET);
    put_be32(pb, size); /* rewrite size */
    url_fseek(pb, curpos, SEEK_SET);
}

int mov_write_stco_tag(ByteIOContext *pb, MOVTrack* track)
{
    int i;
    put_be32(pb, 16+track->entry*4); /* size */
    put_tag(pb, "stco");
    put_be32(pb, 0); /* version & flags */
    put_be32(pb, track->entry); /* entry count */
    for (i=0; i<track->entry; i++) {
        int cl = i / MOV_INDEX_CLUSTER_SIZE;
        int id = i % MOV_INDEX_CLUSTER_SIZE;
        put_be32(pb, track->cluster[cl][id].pos);
    }
    return 16+track->entry*4;
}

int mov_write_stsz_tag(ByteIOContext *pb, MOVTrack* track)
{
    int i, size;

    if(track->cluster[0][0].entries != 0) 
        size = 20;
    else
        size = 20+track->samples*4;
    put_be32(pb, size); /* size */
    put_tag(pb, "stsz");
    put_be32(pb, 0); /* version & flags */

    /* TODO: Ugly (and false) assumption: if we have a chunk of samples, assume
     * all sizes are same */
    if(track->cluster[0][0].entries != 0)   {
        int sSize = track->cluster[0][0].len/track->cluster[0][0].entries;
        put_be32(pb, sSize); /* sample size */
        put_be32(pb, track->samples); /* sample count */
    }
    else 
    {
        put_be32(pb, 0); /* sample size */
        put_be32(pb, track->samples); /* sample count */
        for (i=0; i<track->samples; i++) {
            int cl = i / MOV_INDEX_CLUSTER_SIZE;
            int id = i % MOV_INDEX_CLUSTER_SIZE;
            put_be32(pb, track->cluster[cl][id].len);
        }
    }
    return size;
}

int mov_write_stsc_tag(ByteIOContext *pb, MOVTrack* track)
{
    int size;
    if(track->cluster[0][0].entries != 0)
        size = 16+track->entry*4*3;
    else
        size = 0x1c;
    put_be32(pb, size); // size 
    put_tag(pb, "stsc");
    put_be32(pb, 0); // version & flags 
    if(track->cluster[0][0].entries != 0) {
        int i;
        put_be32(pb, track->entry); // entry count 
        for (i=0; i<track->entry; i++) {
            int cl = i / MOV_INDEX_CLUSTER_SIZE;
            int id = i % MOV_INDEX_CLUSTER_SIZE;
            put_be32(pb, i+1); // first chunk 
            put_be32(pb, track->cluster[cl][id].entries);
            put_be32(pb, 0x1); // sample description index 
        }
    }
    else {
        put_be32(pb, 1); // entry count 
        put_be32(pb, 0x1); // first chunk 
        put_be32(pb, 0x1); // samples per chunk 
        put_be32(pb, 0x1); // sample description index 
    }

    return size;
}

int mov_write_stss_tag(ByteIOContext *pb) //TRA OK
{
    put_be32(pb, 0x14); /* size */
    put_tag(pb, "stss");
    put_be32(pb, 0); /* version & flags */
    put_be32(pb, 1); /* entry count */
    put_be32(pb, 0x1); /* sample number */
    return 0x14;
}

int mov_write_damr_tag(ByteIOContext *pb)
{
    put_be32(pb, 0x11); /* size */
    put_tag(pb, "damr");
    put_tag(pb, "FFMP");
    put_byte(pb, 0);
    put_be16(pb, 0x81ff); /* Mode set (all modes for AMR_NB) */
    put_be16(pb, 1); /* Mode change period (no restriction) */
    return 0x11;
}

int mov_write_samr_tag(ByteIOContext *pb, MOVTrack* track)
{
    int size = 0x24;
    int pos;
    pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    /* "samr" for AMR NB, "sawb" for AMR WB */
    put_tag(pb, "samr");
    put_be32(pb, 0); /* Reserved */
    put_be16(pb, 0); /* Reserved */
    put_be16(pb, 1); /* Data-reference index, XXX  == 1 */
    put_be32(pb, 0); /* Reserved */
    put_be32(pb, 0); /* Reserved */

    put_be16(pb, 2); /* Reserved */
    put_be16(pb, 0x10); /* Reserved */
    put_be32(pb, 0); /* Reserved */
    put_be16(pb, track->timescale); /* Time scale */
    put_be16(pb, 0); /* Reserved */

    size += mov_write_damr_tag(pb);
    writeSize (pb, pos, size);
    return size;
}

int mov_write_d263_tag(ByteIOContext *pb)
{
    put_be32(pb, 0xf); /* size */
    put_tag(pb, "d263");
    put_tag(pb, "FFMP");
    put_be16(pb, 0x0a);
    put_byte(pb, 0);
    return 0xf;
}

int mov_write_s263_tag(ByteIOContext *pb, MOVTrack* track)
{
    int size = 0x56;
    int pos;
    pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "s263");
    put_be32(pb, 0); /* Reserved */
    put_be16(pb, 0); /* Reserved */
    put_be16(pb, 1); /* Data-reference index */
    put_be32(pb, 0); /* Reserved */
    put_be32(pb, 0); /* Reserved */
    put_be32(pb, 0); /* Reserved */
    put_be32(pb, 0); /* Reserved */
    put_be16(pb, track->enc->width); /* Video width */
    put_be16(pb, track->enc->height); /* Video height */
    put_be32(pb, 0x00480000); /* Reserved */
    put_be32(pb, 0x00480000); /* Reserved */
    put_be32(pb, 0); /* Reserved */
    put_be16(pb, 1); /* Reserved */
    put_be32(pb, 0); /* Reserved */
    put_be32(pb, 0); /* Reserved */
    put_be32(pb, 0); /* Reserved */
    put_be32(pb, 0); /* Reserved */
    put_be32(pb, 0); /* Reserved */
    put_be32(pb, 0); /* Reserved */
    put_be32(pb, 0); /* Reserved */
    put_be32(pb, 0); /* Reserved */
    put_be16(pb, 0x18); /* Reserved */
    put_be16(pb, 0xffff); /* Reserved */
    size += mov_write_d263_tag(pb);
    writeSize (pb, pos, size);
    return size;
}

static unsigned int esdsLength(unsigned int len)
{
    unsigned int result = 0;
    unsigned char b = len & 0x7f;
    result += b;
    b = (len >> 8) & 0x7f;
    result += (b + 0x80) << 8;
    b = (len >> 16) & 0x7f;
    result += (b + 0x80) << 16;
    b = (len >> 24) & 0x7f;
    result += (b + 0x80) << 24;
    return result;
}

int mov_write_esds_tag(ByteIOContext *pb, MOVTrack* track) // Basic
{
    put_be32(pb, track->vosLen+18+14+17);
    put_tag(pb, "esds");
    put_be32(pb, 0);              // Version

    put_byte(pb, 0x03);            // tag = ES_DescriptorTag
    put_be32(pb, esdsLength(track->vosLen+18+14));  // Length
    put_be16(pb, 0x0001);         // ID (= 1)
    put_byte(pb, 0x00);            // flags (= no flags)

// Decoderconfigdescriptor = 4
    put_byte(pb, 0x04);            // tag = DecoderConfigDescriptor
    put_be32(pb, esdsLength(track->vosLen+18));  // Length
    put_byte(pb, 0x20);            // Object type indication (Visual 14496-2)
    put_byte(pb, 0x11);            // flags (= Visualstream)
    put_byte(pb, 0x0);             // Buffersize DB (24 bits)
    put_be16(pb, 0x0dd2);          // Buffersize DB

    // TODO: find real values for these
    put_be32(pb, 0x0002e918);     // maxbitrate
    put_be32(pb, 0x00017e6b);     // avg bitrate

// Decoderspecific info Tag = 5
    put_byte(pb, 0x05);           // tag = Decoderspecific info
    put_be32(pb, esdsLength(track->vosLen));   // length
    put_buffer(pb, track->vosData, track->vosLen);

    put_byte(pb, 0x06);
    put_be32(pb, esdsLength(1));  // length
    put_byte(pb, 0x02);
    return track->vosLen+18+14+17;
}

int mov_write_mp4v_tag(ByteIOContext *pb, MOVTrack* track) // Basic
{
    put_be32(pb, 194);
    put_tag(pb, "mp4v");
    put_be32(pb, 0);  // Reserved
    put_be16(pb, 0);  // Reserved
    put_be16(pb, 1);  // Data-reference index
    put_be32(pb, 0);  // Reserved
    put_be32(pb, 0);  // Reserved
    put_be32(pb, 0);  // Reserved
    put_be32(pb, 0);  // Reserved
    put_be16(pb, track->enc->width);  // Width
    put_be16(pb, track->enc->height);  // Height
    put_be32(pb, 0x00480000);  // Reserved
    put_be32(pb, 0x00480000);  // Reserved
    put_be32(pb, 0);  // Reserved
    put_be16(pb, 1);  // Reserved
    put_be32(pb, 0);  // Reserved
    put_be32(pb, 0);  // Reserved
    put_be32(pb, 0);  // Reserved
    put_be32(pb, 0);  // Reserved
    put_be32(pb, 0);  // Reserved
    put_be32(pb, 0);  // Reserved
    put_be32(pb, 0);  // Reserved
    put_be32(pb, 0);  // Reserved
    put_be16(pb, 24); // Reserved
    put_be16(pb, 0xFFFF); // Reserved
    mov_write_esds_tag(pb, track);
    return 194;
}

int mov_write_stsd_tag(ByteIOContext *pb, MOVTrack* track)
{
    int size = 16;
    int pos;
    pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "stsd");
    put_be32(pb, 0); /* version & flags */
    put_be32(pb, 1); /* entry count */
    if (track->enc->codec_type == CODEC_TYPE_VIDEO) {
        if (track->enc->codec_id == CODEC_ID_H263) 
            size += mov_write_s263_tag(pb, track);
        else if (track->enc->codec_id == CODEC_ID_MPEG4) 
            size += mov_write_mp4v_tag(pb, track);
    }
    else if (track->enc->codec_type == CODEC_TYPE_AUDIO) {
        if (track->enc->codec_id == CODEC_ID_AMR_NB) 
            size += mov_write_samr_tag(pb, track);
    }
    writeSize (pb, pos, size);
    return size;
}

int mov_write_stts_tag(ByteIOContext *pb, MOVTrack* track)
{
    put_be32(pb, 0x18); /* size */
    put_tag(pb, "stts");
    put_be32(pb, 0); /* version & flags */
    put_be32(pb, 1); /* entry count */

    put_be32(pb, track->samples); /* sample count */
    put_be32(pb, track->sampleDelta); /* sample delta */
    return 0x18;
}

int mov_write_dref_tag(ByteIOContext *pb)
{
    put_be32(pb, 28); /* size */
    put_tag(pb, "dref");
    put_be32(pb, 0); /* version & flags */
    put_be32(pb, 1); /* entry count */

    put_be32(pb, 0xc); /* size */
    put_tag(pb, "url ");
    put_be32(pb, 1); /* version & flags */

    return 28;
}

int mov_write_stbl_tag(ByteIOContext *pb, MOVTrack* track)
{
    int size = 8;
    int pos;
    pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "stbl");
    size += mov_write_stsd_tag(pb, track);
    size += mov_write_stts_tag(pb, track);
    if (track->enc->codec_type == CODEC_TYPE_VIDEO)
        size += mov_write_stss_tag(pb);
    size += mov_write_stsc_tag(pb, track);
    size += mov_write_stsz_tag(pb, track);
    size += mov_write_stco_tag(pb, track);
    writeSize (pb, pos, size);
    return size;
}

int mov_write_dinf_tag(ByteIOContext *pb)
{
    int size = 8;
    int pos;
    pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "dinf");
    size += mov_write_dref_tag(pb);
    writeSize (pb, pos, size);
    return size;
}

int mov_write_smhd_tag(ByteIOContext *pb)
{
    put_be32(pb, 16); /* size */
    put_tag(pb, "smhd");
    put_be32(pb, 0); /* version & flags */
    put_be16(pb, 0); /* reserved (balance, normally = 0) */
    put_be16(pb, 0); /* reserved */
    return 16;
}

int mov_write_vmhd_tag(ByteIOContext *pb)
{
    put_be32(pb, 0x14); /* size (always 0x14) */
    put_tag(pb, "vmhd");
    put_be32(pb, 0x01); /* version & flags */
    put_be64(pb, 0); /* reserved (graphics mode = copy) */
    return 0x14;
}

int mov_write_minf_tag(ByteIOContext *pb, MOVTrack* track)
{
    int size = 8;
    int pos;
    pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "minf");
    if(track->enc->codec_type == CODEC_TYPE_VIDEO)
        size += mov_write_vmhd_tag(pb);
    else
        size += mov_write_smhd_tag(pb);
    size += mov_write_dinf_tag(pb);
    size += mov_write_stbl_tag(pb, track);
    writeSize (pb, pos, size);
    return size;
}

int mov_write_hdlr_tag(ByteIOContext *pb, MOVTrack* track)
{
    int size = 0;
    size = 45;
    put_be32(pb, size); /* size */
    put_tag(pb, "hdlr");
    put_be32(pb, 0); /* Version & flags */
    put_be32(pb, 0); /* reserved */
    if(track->enc->codec_type == CODEC_TYPE_VIDEO)
        put_tag(pb, "vide"); /* handler type */
    else
        put_tag(pb, "soun"); /* handler type */
    put_byte(pb, 0); /* reserved */
    put_byte(pb, 0); /* reserved */
    put_byte(pb, 0); /* reserved */
    put_byte(pb, 0); /* reserved */
    put_byte(pb, 0); /* reserved */
    put_byte(pb, 0); /* reserved */
    put_byte(pb, 0); /* reserved */
    put_byte(pb, 0); /* reserved */
    put_byte(pb, 0); /* reserved */
    put_byte(pb, 0); /* reserved */
    put_byte(pb, 0); /* reserved */
    put_byte(pb, 0); /* reserved */
    if(track->enc->codec_type == CODEC_TYPE_VIDEO)
        put_buffer(pb, "VideoHandler", 13);
    else
        put_buffer(pb, "SoundHandler", 13);
    return size;
}

int mov_write_mdhd_tag(ByteIOContext *pb, MOVTrack* track)
{
    put_be32(pb, 32); /* size */
    put_tag(pb, "mdhd");
    put_be32(pb, 0); /* Version & flags */
    put_be32(pb, track->time); /* creation time */
    put_be32(pb, track->time); /* modification time */
    put_be32(pb, track->timescale); /* time scale */

    put_be32(pb, track->timescale*track->entry*track->frameDuration/globalTimescale); /* duration */
    put_be16(pb, 0); /* language, 0 = english */
    put_be16(pb, 0); /* reserved (quality) */
    return 32;
}

int mov_write_mdia_tag(ByteIOContext *pb, MOVTrack* track)
{
    int size = 8;
    int pos;
    pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "mdia");
    size += mov_write_mdhd_tag(pb, track);
    size += mov_write_hdlr_tag(pb, track);
    size += mov_write_minf_tag(pb, track);
    writeSize (pb, pos, size);
    return size;
}

int mov_write_tkhd_tag(ByteIOContext *pb, MOVTrack* track)
{
    put_be32(pb, 0x5c); /* size (always 0x5c) */
    put_tag(pb, "tkhd");
    put_be32(pb, 1); /* version & flags (track enabled) */
    put_be32(pb, track->time); /* creation time */
    put_be32(pb, track->time); /* modification time */
    put_be32(pb, track->trackID); /* track-id */
    put_be32(pb, 0); /* reserved */
    put_be32(pb, track->entry*track->frameDuration); /* duration */

    put_be32(pb, 0); /* reserved */
    put_be32(pb, 0); /* reserved */
    put_be32(pb, 0x0); /* reserved (Layer & Alternate group) */
    /* Volume, only for audio */
    if(track->enc->codec_type == CODEC_TYPE_AUDIO)
        put_be16(pb, 0x0100);
    else
        put_be16(pb, 0);
    put_be16(pb, 0); /* reserved */

    /* Matrix structure */
    put_be32(pb, 0x00010000); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x00010000); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x40000000); /* reserved */

    /* Track width and height, for visual only */
    if(track->enc->codec_type == CODEC_TYPE_VIDEO) {
        put_be32(pb, 0x01400000);
        put_be32(pb, 0x00f00000);
    }
    else {
        put_be32(pb, 0);
        put_be32(pb, 0);
    }
    return 0x5c;
}

int mov_write_trak_tag(ByteIOContext *pb, MOVTrack* track)
{
    int size = 8;
    int pos;
    pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "trak");
    size += mov_write_tkhd_tag(pb, track);
    size += mov_write_mdia_tag(pb, track);
    writeSize (pb, pos, size);
    return size;
}

/* TODO: Not sorted out, but not necessary either */
int mov_write_iods_tag(ByteIOContext *pb, MOVContext *mov)
{
    put_be32(pb, 0x15); /* size */
    put_tag(pb, "iods");
    put_be32(pb, 0);    /* version & flags */
    put_be16(pb, 0x1007);
    put_byte(pb, 0);
    put_be16(pb, 0x4fff);
    put_be16(pb, 0xfffe);
    put_be16(pb, 0x01ff);
    return 0x15;
}

int mov_write_mvhd_tag(ByteIOContext *pb, MOVContext *mov)
{
    int maxTrackID = 1, maxTrackLen = 0, i;

    put_be32(pb, 0x6c); /* size (always 0x6c) */
    put_tag(pb, "mvhd");
    put_be32(pb, 0); /* version & flags */
    put_be32(pb, mov->time); /* creation time */
    put_be32(pb, mov->time); /* modification time */
    put_be32(pb, mov->timescale); /* timescale */
    for (i=0; i<MAX_STREAMS; i++) {
        if(mov->tracks[i].entry > 0) {
            if(maxTrackLen < mov->tracks[i].entry*mov->tracks[i].frameDuration)
                maxTrackLen = mov->tracks[i].entry*mov->tracks[i].frameDuration;
            if(maxTrackID < mov->tracks[i].trackID)
                maxTrackID = mov->tracks[i].trackID;
        }
    }
    put_be32(pb, maxTrackLen); /* duration of longest track */

    put_be32(pb, 0x00010000); /* reserved (preferred rate) 1.0 = normal */
    put_be16(pb, 0x0100); /* reserved (preferred volume) 1.0 = normal */
    put_be16(pb, 0); /* reserved */
    put_be32(pb, 0); /* reserved */
    put_be32(pb, 0); /* reserved */

    /* Matrix structure */
    put_be32(pb, 0x00010000); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x00010000); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x40000000); /* reserved */

    put_be32(pb, 0); /* reserved (preview time) */
    put_be32(pb, 0); /* reserved (preview duration) */
    put_be32(pb, 0); /* reserved (poster time) */
    put_be32(pb, 0); /* reserved (selection time) */
    put_be32(pb, 0); /* reserved (selection duration) */
    put_be32(pb, 0); /* reserved (current time) */
    put_be32(pb, maxTrackID+1); /* Next track id */
    return 0x6c;
}

int mov_write_moov_tag(ByteIOContext *pb, MOVContext *mov)
{
    int pos, size = 8, i;
    pos = url_ftell(pb);
    put_be32(pb, 0); /* size placeholder*/
    put_tag(pb, "moov");
    mov->timescale = globalTimescale;

    for (i=0; i<MAX_STREAMS; i++) {
        if(mov->tracks[i].entry > 0) {
            if(mov->tracks[i].enc->codec_type == CODEC_TYPE_VIDEO) {
                mov->tracks[i].timescale = globalTimescale;
                mov->tracks[i].sampleDelta = mov->tracks[i].frameDuration =
                    globalTimescale*mov->tracks[i].enc->frame_rate_base/mov->tracks[i].enc->frame_rate;
            }
            else if(mov->tracks[i].enc->codec_type == CODEC_TYPE_AUDIO) {
                /* If AMR, track timescale = 8000, AMR_WB = 16000 */
                if(mov->tracks[i].enc->codec_id == CODEC_ID_AMR_NB) {
                    mov->tracks[i].frameDuration = 20;
                    mov->tracks[i].sampleDelta = 160;
                    mov->tracks[i].timescale = 8000;
                }
                else {
                    mov->tracks[i].timescale = globalTimescale;
                    mov->tracks[i].frameDuration =
                    globalTimescale*mov->tracks[i].enc->frame_rate_base/mov->tracks[i].enc->frame_rate;
                }
            }
            mov->tracks[i].time = mov->time;
            mov->tracks[i].trackID = i+1;
        }
    }

    size += mov_write_mvhd_tag(pb, mov);
    //size += mov_write_iods_tag(pb, mov);
    for (i=0; i<MAX_STREAMS; i++) {
        if(mov->tracks[i].entry > 0) {
            size += mov_write_trak_tag(pb, &(mov->tracks[i]));
        }
    }

    writeSize (pb, pos, size);

    return size;
}

int mov_write_mdat_tag(ByteIOContext *pb, MOVTrack* track)
{
    track->mdat_pos = url_ftell(pb); 
    put_be32(pb, 0); /* size placeholder*/
    put_tag(pb, "mdat");
    return 0;
}

/* TODO: This needs to be more general */
int mov_write_ftyp_tag(ByteIOContext *pb)
{
    put_be32(pb, 0x14 ); /* size */
    put_tag(pb, "ftyp");
    put_tag(pb, "3gp4");
    put_be32(pb, 0x200 );
    put_tag(pb, "3gp4");
    return 0x14;
}

static int mov_write_header(AVFormatContext *s)
{
    ByteIOContext *pb = &s->pb;

    /* write ftyp */
    mov_write_ftyp_tag(pb);
    
    put_flush_packet(pb);

    return 0;
}

static int Timestamp() {
    time_t ltime;
    time ( &ltime );
    return ltime+(24107*86400);
}

static int mov_write_packet(AVFormatContext *s, int stream_index,
                            uint8_t *buf, int size, int force_pts)
{
    MOVContext *mov = s->priv_data;
    ByteIOContext *pb = &s->pb;
    AVCodecContext *enc;

    enc = &s->streams[stream_index]->codec;
    if (!url_is_streamed(&s->pb)) {
        MOVTrack* trk = &mov->tracks[stream_index];
        int sampleCount = 0;

        /* We must find out how many AMR blocks there are in one packet */
        if(enc->codec_type == CODEC_TYPE_AUDIO &&
           enc->codec_id == CODEC_ID_AMR_NB) {
            static uint16_t packed_size[16] = {13, 14, 16, 18, 20, 21, 27, 32, 6, 0, 0, 0, 0, 0, 0, 0};             
            int len = 0;

            while(len < size && sampleCount < 100) {
                len += packed_size[(buf[len] >> 3) & 0x0F];
                sampleCount++;
            }
        }
        /* TODO: Is there some other way to get VOS block from MPEG4 stream? */
        if(enc->codec_type == CODEC_TYPE_VIDEO &&
           enc->codec_id == CODEC_ID_MPEG4 &&
           trk->vosLen == 0)
        {
            int index = 0;
            int vosStart = 0;
            while(index < size) {
                if(buf[index] == 0 && buf[index+1] == 0 && buf[index+2] == 1) {
                    index+=3;
                    if(buf[index] == 0xB6) {
                        if(vosStart != 0) {
                            trk->vosLen = index-3 - (vosStart-3);
                            trk->vosData = av_malloc(trk->vosLen+2);
                            memcpy(trk->vosData, (char *)&buf[vosStart-3], trk->vosLen);
                            break;
                        }
                    }
                    else if(buf[index] == 0xb0) {
                        vosStart = index;
                    }
                }
                index++;
            }
        }

        int cl = trk->entry / MOV_INDEX_CLUSTER_SIZE;
        int id = trk->entry % MOV_INDEX_CLUSTER_SIZE;

        if (trk->ents_allocated <= trk->entry) {
            trk->cluster = av_realloc(trk->cluster, (cl+1)*sizeof(void*)); 
            if (!trk->cluster)
                return -1;
            trk->cluster[cl] = av_malloc(MOV_INDEX_CLUSTER_SIZE*sizeof(MOVIentry));
            if (!trk->cluster[cl])
                return -1;
            trk->ents_allocated += MOV_INDEX_CLUSTER_SIZE;
        }
        if(stream_index == 0 && trk->entry == 0) {
            mov_write_mdat_tag(pb, trk);
            mov->time = Timestamp();
        }
        trk->cluster[cl][id].pos = url_ftell(pb) - mov->movi_list;
        trk->cluster[cl][id].len = size;
        trk->cluster[cl][id].entries = sampleCount;
        trk->enc = enc;
        trk->entry++;
        if(sampleCount == 0)
            trk->samples++;
        else
            trk->samples += sampleCount;
        trk->mdat_size += size;
    }
    put_buffer(pb, buf, size);

    put_flush_packet(pb);
    return 0;
}

static int mov_write_trailer(AVFormatContext *s)
{
    MOVContext *mov = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int res = 0;
    int i, j;
    offset_t file_size;

    file_size = url_ftell(pb);
    j = 0;

    /* Write size of mdat tag */
    for (i=0; i<MAX_STREAMS; i++) {
        if(mov->tracks[i].ents_allocated > 0) {
            j += mov->tracks[i].mdat_size;
        }
    }
    url_fseek(pb, mov->tracks[0].mdat_pos, SEEK_SET);
    put_be32(pb, j+8);
    url_fseek(pb, file_size, SEEK_SET);

    mov_write_moov_tag(pb, mov);

    for (i=0; i<MAX_STREAMS; i++) {
        for (j=0; j<mov->tracks[i].ents_allocated/MOV_INDEX_CLUSTER_SIZE; j++) {
            av_free(mov->tracks[i].cluster[j]);
        }
        av_free(mov->tracks[i].cluster);
        mov->tracks[i].cluster = NULL;
        mov->tracks[i].ents_allocated = mov->tracks[i].entry = 0;
    }
    put_flush_packet(pb);

    return res;
}

static AVOutputFormat mov_oformat = {
    "mov",
    "mov format",
    NULL,
    "mov",
    sizeof(MOVContext),
    CODEC_ID_MP2,
    CODEC_ID_SVQ1,
    mov_write_header,
    mov_write_packet,
    mov_write_trailer,
};

static AVOutputFormat _3gp_oformat = {
    "3gp",
    "3gp format",
    NULL,
    "3gp",
    sizeof(MOVContext),
    CODEC_ID_AMR_NB,
    CODEC_ID_H263,
    mov_write_header,
    mov_write_packet,
    mov_write_trailer,
};

static AVOutputFormat mp4_oformat = {
    "mp4",
    "mp4 format",
    NULL,
    "mp4",
    sizeof(MOVContext),
    CODEC_ID_AAC,
    CODEC_ID_MPEG4,
    mov_write_header,
    mov_write_packet,
    mov_write_trailer,
};

int movenc_init(void)
{
    av_register_output_format(&mov_oformat);
    av_register_output_format(&_3gp_oformat);
    av_register_output_format(&mp4_oformat);
    return 0;
}
