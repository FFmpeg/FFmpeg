/*
 * MOV, 3GP, MP4 encoder.
 * Copyright (c) 2003 Thomas Raivio.
 * Copyright (c) 2004 Gildas Bazin <gbazin at videolan dot org>.
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
#include "avi.h"
#include "avio.h"

#undef NDEBUG
#include <assert.h>

#define MOV_INDEX_CLUSTER_SIZE 16384
#define globalTimescale 1000

#define MODE_MP4 0
#define MODE_MOV 1
#define MODE_3GP 2
#define MODE_PSP 3 // example working PSP command line: 
// ffmpeg -i testinput.avi  -f psp -r 14.985 -s 320x240 -b 768 -ar 24000 -ab 32 M4V00001.MP4
#define MODE_3G2 4

typedef struct MOVIentry {
    unsigned int flags, pos, size;
    unsigned int samplesInChunk;
    char         key_frame;
    unsigned int entries;
} MOVIentry;

typedef struct MOVIndex {
    int         mode;
    int         entry;
    int         mdat_size;
    int         ents_allocated;
    long        timescale;
    long        time;
    long        trackDuration;
    long        sampleCount;
    long        sampleDuration;
    int         hasKeyframes;
    int         trackID;
    AVCodecContext *enc;

    int         vosLen;
    uint8_t     *vosData;
    MOVIentry** cluster;
} MOVTrack;

typedef struct MOVContext {
    int     mode;
    long    time;
    int     nb_streams;
    int     mdat_written;
    offset_t mdat_pos;
    long    timescale;
    MOVTrack tracks[MAX_STREAMS];
} MOVContext;

static int mov_write_esds_tag(ByteIOContext *pb, MOVTrack* track);

const CodecTag ff_mov_obj_type[] = {
    { CODEC_ID_MPEG4     ,  32 },
    { CODEC_ID_AAC       ,  64 },
    { CODEC_ID_MPEG1VIDEO, 106 },
    { CODEC_ID_MPEG2VIDEO,  96 },//mpeg2 profiles
    { CODEC_ID_MP2       , 107 },//FIXME mpeg2 mpeg audio -> 105
    { CODEC_ID_MP3       , 107 },//FIXME mpeg2 mpeg audio -> 105
    { CODEC_ID_H264      ,  33 },
    { CODEC_ID_H263      , 242 },
    { CODEC_ID_H261      , 243 },
    { CODEC_ID_MJPEG     , 108 },
    { CODEC_ID_PCM_S16LE , 224 },
    { CODEC_ID_VORBIS    , 225 },
    { CODEC_ID_AC3       , 226 },
    { CODEC_ID_PCM_ALAW  , 227 },
    { CODEC_ID_PCM_MULAW , 228 },
    { CODEC_ID_PCM_S16BE , 230 },
    { 0,0  },
};

//FIXME supprt 64bit varaint with wide placeholders
static int updateSize (ByteIOContext *pb, int pos)
{
    long curpos = url_ftell(pb);
    url_fseek(pb, pos, SEEK_SET);
    put_be32(pb, curpos - pos); /* rewrite size */
    url_fseek(pb, curpos, SEEK_SET);

    return curpos - pos;
}

/* Chunk offset atom */
static int mov_write_stco_tag(ByteIOContext *pb, MOVTrack* track)
{
    int i;
    int pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "stco");
    put_be32(pb, 0); /* version & flags */
    put_be32(pb, track->entry); /* entry count */
    for (i=0; i<track->entry; i++) {
        int cl = i / MOV_INDEX_CLUSTER_SIZE;
        int id = i % MOV_INDEX_CLUSTER_SIZE;
        put_be32(pb, track->cluster[cl][id].pos);
    }
    return updateSize (pb, pos);
}

/* Sample size atom */
static int mov_write_stsz_tag(ByteIOContext *pb, MOVTrack* track)
{
    int equalChunks = 1;
    int i, j, entries = 0, tst = -1, oldtst = -1;

    int pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "stsz");
    put_be32(pb, 0); /* version & flags */

    for (i=0; i<track->entry; i++) {
        int cl = i / MOV_INDEX_CLUSTER_SIZE;
        int id = i % MOV_INDEX_CLUSTER_SIZE;
        tst = track->cluster[cl][id].size/track->cluster[cl][id].entries;
        if(oldtst != -1 && tst != oldtst) {
            equalChunks = 0;
        }
        oldtst = tst;
        entries += track->cluster[cl][id].entries;
    }
    if (equalChunks) {
        int sSize = track->cluster[0][0].size/track->cluster[0][0].entries;
        put_be32(pb, sSize); // sample size 
        put_be32(pb, entries); // sample count
    }
    else {
        put_be32(pb, 0); // sample size 
        put_be32(pb, entries); // sample count 
        for (i=0; i<track->entry; i++) {
            int cl = i / MOV_INDEX_CLUSTER_SIZE;
            int id = i % MOV_INDEX_CLUSTER_SIZE;
            for ( j=0; j<track->cluster[cl][id].entries; j++) {
                put_be32(pb, track->cluster[cl][id].size /
                         track->cluster[cl][id].entries);
            }
        }
    }
    return updateSize (pb, pos);
}

/* Sample to chunk atom */
static int mov_write_stsc_tag(ByteIOContext *pb, MOVTrack* track)
{
    int index = 0, oldval = -1, i, entryPos, curpos;

    int pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "stsc");
    put_be32(pb, 0); // version & flags 
    entryPos = url_ftell(pb);
    put_be32(pb, track->entry); // entry count 
    for (i=0; i<track->entry; i++) {
        int cl = i / MOV_INDEX_CLUSTER_SIZE;
        int id = i % MOV_INDEX_CLUSTER_SIZE;
        if(oldval != track->cluster[cl][id].samplesInChunk)
        {
            put_be32(pb, i+1); // first chunk 
            put_be32(pb, track->cluster[cl][id].samplesInChunk); // samples per chunk
            put_be32(pb, 0x1); // sample description index 
            oldval = track->cluster[cl][id].samplesInChunk;
            index++;
        }
    }
    curpos = url_ftell(pb);
    url_fseek(pb, entryPos, SEEK_SET);
    put_be32(pb, index); // rewrite size 
    url_fseek(pb, curpos, SEEK_SET);

    return updateSize (pb, pos);
}

/* Sync sample atom */
static int mov_write_stss_tag(ByteIOContext *pb, MOVTrack* track)
{
    long curpos;
    int i, index = 0, entryPos;
    int pos = url_ftell(pb);
    put_be32(pb, 0); // size 
    put_tag(pb, "stss");
    put_be32(pb, 0); // version & flags 
    entryPos = url_ftell(pb);
    put_be32(pb, track->entry); // entry count 
    for (i=0; i<track->entry; i++) {
        int cl = i / MOV_INDEX_CLUSTER_SIZE;
        int id = i % MOV_INDEX_CLUSTER_SIZE;
        if(track->cluster[cl][id].key_frame == 1) {
            put_be32(pb, i+1);
            index++;
        }
    }
    curpos = url_ftell(pb);
    url_fseek(pb, entryPos, SEEK_SET);
    put_be32(pb, index); // rewrite size 
    url_fseek(pb, curpos, SEEK_SET);
    return updateSize (pb, pos);
}

static int mov_write_damr_tag(ByteIOContext *pb)
{
    put_be32(pb, 0x11); /* size */
    put_tag(pb, "damr");
    put_tag(pb, "FFMP");
    put_byte(pb, 0);

    put_be16(pb, 0x80); /* Mode set (all modes for AMR_NB) */
    put_be16(pb, 0xa); /* Mode change period (no restriction) */
    //put_be16(pb, 0x81ff); /* Mode set (all modes for AMR_NB) */
    //put_be16(pb, 1); /* Mode change period (no restriction) */
    return 0x11;
}

static int mov_write_wave_tag(ByteIOContext *pb, MOVTrack* track)
{
    int pos = url_ftell(pb);

    put_be32(pb, 0);     /* size */
    put_tag(pb, "wave");

    put_be32(pb, 12);    /* size */
    put_tag(pb, "frma");
    put_tag(pb, "mp4a");

    put_be32(pb, 12);    /* size */
    put_tag(pb, "mp4a");
    put_be32(pb, 0);

    mov_write_esds_tag(pb, track);

    put_be32(pb, 12);    /* size */
    put_tag(pb, "srcq");
    put_be32(pb, 0x40);

    put_be32(pb, 8);     /* size */
    put_be32(pb, 0);     /* null tag */

    return updateSize (pb, pos);
}

static const CodecTag codec_movaudio_tags[] = {
    { CODEC_ID_PCM_MULAW, MKTAG('u', 'l', 'a', 'w') },
    { CODEC_ID_PCM_ALAW, MKTAG('a', 'l', 'a', 'w') },
    { CODEC_ID_ADPCM_IMA_QT, MKTAG('i', 'm', 'a', '4') },
    { CODEC_ID_MACE3, MKTAG('M', 'A', 'C', '3') },
    { CODEC_ID_MACE6, MKTAG('M', 'A', 'C', '6') },
    { CODEC_ID_AAC, MKTAG('m', 'p', '4', 'a') },
    { CODEC_ID_AMR_NB, MKTAG('s', 'a', 'm', 'r') },
    { CODEC_ID_AMR_WB, MKTAG('s', 'a', 'w', 'b') },
    { CODEC_ID_PCM_S16BE, MKTAG('t', 'w', 'o', 's') },
    { CODEC_ID_PCM_S16LE, MKTAG('s', 'o', 'w', 't') },
    { CODEC_ID_MP3, MKTAG('.', 'm', 'p', '3') },
    { 0, 0 },
};

static int mov_write_audio_tag(ByteIOContext *pb, MOVTrack* track)
{
    int pos = url_ftell(pb);
    int tag;
    
    put_be32(pb, 0); /* size */

    tag = track->enc->codec_tag;
    if (!tag)
    tag = codec_get_tag(codec_movaudio_tags, track->enc->codec_id);
    // if no mac fcc found, try with Microsoft tags
    if (!tag)
    {
	int tmp = codec_get_tag(codec_wav_tags, track->enc->codec_id);
        tag = MKTAG('m', 's', ((tmp >> 8) & 0xff), (tmp & 0xff));
    }
    put_le32(pb, tag); // store it byteswapped

    put_be32(pb, 0); /* Reserved */
    put_be16(pb, 0); /* Reserved */
    put_be16(pb, 1); /* Data-reference index, XXX  == 1 */

    /* SoundDescription */
    if(track->mode == MODE_MOV && track->enc->codec_id == CODEC_ID_AAC)
        put_be16(pb, 1); /* Version 1 */
    else
        put_be16(pb, 0); /* Version 0 */
    put_be16(pb, 0); /* Revision level */
    put_be32(pb, 0); /* Reserved */

    put_be16(pb, track->enc->channels); /* Number of channels */
    /* TODO: Currently hard-coded to 16-bit, there doesn't seem
                 to be a good way to get number of bits of audio */
    put_be16(pb, 0x10); /* Reserved */

    if(track->enc->codec_id == CODEC_ID_AAC ||
       track->enc->codec_id == CODEC_ID_MP3)
    {
        put_be16(pb, 0xfffe); /* compression ID (vbr)*/
    }
    else
    {
        put_be16(pb, 0); /* compression ID (= 0) */
    }
    put_be16(pb, 0); /* packet size (= 0) */
    put_be16(pb, track->timescale); /* Time scale */
    put_be16(pb, 0); /* Reserved */

    if(track->mode == MODE_MOV && track->enc->codec_id == CODEC_ID_AAC)
    {
        /* SoundDescription V1 extended info */
        put_be32(pb, track->enc->frame_size); /* Samples per packet  */
        put_be32(pb, 1536); /* Bytes per packet */
        put_be32(pb, 2); /* Bytes per frame */
        put_be32(pb, 2); /* Bytes per sample */
    }

    if(track->enc->codec_id == CODEC_ID_AAC) {
        if( track->mode == MODE_MOV ) mov_write_wave_tag(pb, track);
        else mov_write_esds_tag(pb, track);
    }
    if(track->enc->codec_id == CODEC_ID_AMR_NB)
        mov_write_damr_tag(pb);
    return updateSize (pb, pos);
}

static int mov_write_d263_tag(ByteIOContext *pb)
{
    put_be32(pb, 0xf); /* size */
    put_tag(pb, "d263");
    put_tag(pb, "FFMP");
    put_be16(pb, 0x0a);
    put_byte(pb, 0);
    return 0xf;
}

/* TODO: No idea about these values */
static int mov_write_svq3_tag(ByteIOContext *pb)
{
    put_be32(pb, 0x15);
    put_tag(pb, "SMI ");
    put_tag(pb, "SEQH");
    put_be32(pb, 0x5);
    put_be32(pb, 0xe2c0211d);
    put_be32(pb, 0xc0000000);
    put_byte(pb, 0);   
    return 0x15;
}

static unsigned int descrLength(unsigned int len)
{
    if (len < 0x00000080)
        return 2 + len;
    else if (len < 0x00004000)
        return 3 + len;
    else if(len < 0x00200000)
        return 4 + len;
    else
        return 5 + len;
}

static void putDescr(ByteIOContext *pb, int tag, int size)
{
    uint32_t len;
    uint8_t  vals[4];

    len = size;
    vals[3] = (uint8_t)(len & 0x7f);
    len >>= 7;
    vals[2] = (uint8_t)((len & 0x7f) | 0x80); 
    len >>= 7;
    vals[1] = (uint8_t)((len & 0x7f) | 0x80); 
    len >>= 7;
    vals[0] = (uint8_t)((len & 0x7f) | 0x80);

    put_byte(pb, tag); // DescriptorTag

    if (size < 0x00000080)
    {
        put_byte(pb, vals[3]);
    }
    else if (size < 0x00004000)
    {
        put_byte(pb, vals[2]);
        put_byte(pb, vals[3]);
    }
    else if (size < 0x00200000)
    {
        put_byte(pb, vals[1]);
        put_byte(pb, vals[2]);
        put_byte(pb, vals[3]);
    }
    else if (size < 0x10000000)
    {
        put_byte(pb, vals[0]);
        put_byte(pb, vals[1]);
        put_byte(pb, vals[2]);
        put_byte(pb, vals[3]);
    }
}

static int mov_write_esds_tag(ByteIOContext *pb, MOVTrack* track) // Basic
{
    int decoderSpecificInfoLen;
    int pos = url_ftell(pb);
    void *vosDataBackup=track->vosData;
    int vosLenBackup=track->vosLen;
    
    // we should be able to have these passed in, via vosData, then we wouldn't need to attack this routine at all
    static const char PSPAACData[]={0x13,0x10};
    static const char PSPMP4Data[]={0x00,0x00,0x01,0xB0,0x03,0x00,0x00,0x01,0xB5,0x09,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x20,0x00,0x84,0x5D,0x4C,0x28,0x50,0x20,0xF0,0xA3,0x1F };
    
    
    if (track->mode == MODE_PSP)  // fails on psp if this is not here
    {
        if (track->enc->codec_id == CODEC_ID_AAC)
        {
            track->vosLen = 2;
            track->vosData = (uint8_t *) PSPAACData;
        }

        if (track->enc->codec_id == CODEC_ID_MPEG4)
        {
            track->vosLen = 28;
            track->vosData = (uint8_t *) PSPMP4Data;
        }
    }

    decoderSpecificInfoLen = track->vosLen ? descrLength(track->vosLen):0;

    put_be32(pb, 0);               // size
    put_tag(pb, "esds");
    put_be32(pb, 0);               // Version

    // ES descriptor
    putDescr(pb, 0x03, 3 + descrLength(13 + decoderSpecificInfoLen) +
             descrLength(1));
    put_be16(pb, track->trackID);
    put_byte(pb, 0x00);            // flags (= no flags)

    // DecoderConfig descriptor
    putDescr(pb, 0x04, 13 + decoderSpecificInfoLen);

    // Object type indication
    put_byte(pb, codec_get_tag(ff_mov_obj_type, track->enc->codec_id));

    // the following fields is made of 6 bits to identify the streamtype (4 for video, 5 for audio)
    // plus 1 bit to indicate upstream and 1 bit set to 1 (reserved)
    if(track->enc->codec_type == CODEC_TYPE_AUDIO)
        put_byte(pb, 0x15);            // flags (= Audiostream)
    else
        put_byte(pb, 0x11);            // flags (= Visualstream)

    put_byte(pb,  track->enc->rc_buffer_size>>(3+16));             // Buffersize DB (24 bits)
    put_be16(pb, (track->enc->rc_buffer_size>>3)&0xFFFF);          // Buffersize DB

    put_be32(pb, FFMAX(track->enc->bit_rate, track->enc->rc_max_rate));     // maxbitrate  (FIXME should be max rate in any 1 sec window)
    if(track->enc->rc_max_rate != track->enc->rc_min_rate || track->enc->rc_min_rate==0)
        put_be32(pb, 0);     // vbr
    else
        put_be32(pb, track->enc->rc_max_rate);     // avg bitrate

    if (track->vosLen)
    {
        // DecoderSpecific info descriptor
        putDescr(pb, 0x05, track->vosLen);
        put_buffer(pb, track->vosData, track->vosLen);
    }

    track->vosData = vosDataBackup;
    track->vosLen = vosLenBackup;

    // SL descriptor
    putDescr(pb, 0x06, 1);
    put_byte(pb, 0x02);
    return updateSize (pb, pos);
}

static const CodecTag codec_movvideo_tags[] = {
    { CODEC_ID_SVQ1, MKTAG('S', 'V', 'Q', '1') },
    { CODEC_ID_SVQ3, MKTAG('S', 'V', 'Q', '3') },
    { CODEC_ID_MPEG4, MKTAG('m', 'p', '4', 'v') },
    { CODEC_ID_H263, MKTAG('s', '2', '6', '3') },
    { CODEC_ID_H264, MKTAG('a', 'v', 'c', '1') },
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'c', ' ') },
    { 0, 0 },
};

static int mov_write_video_tag(ByteIOContext *pb, MOVTrack* track)
{
    int pos = url_ftell(pb);
    char compressor_name[32];
    int tag;

    put_be32(pb, 0); /* size */

    tag = track->enc->codec_tag;
    if (!tag)
    tag = codec_get_tag(codec_movvideo_tags, track->enc->codec_id);
    // if no mac fcc found, try with Microsoft tags
    if (!tag)
	tag = codec_get_tag(codec_bmp_tags, track->enc->codec_id);
    put_le32(pb, tag); // store it byteswapped

    put_be32(pb, 0); /* Reserved */
    put_be16(pb, 0); /* Reserved */
    put_be16(pb, 1); /* Data-reference index */

    put_be16(pb, 0); /* Codec stream version */
    put_be16(pb, 0); /* Codec stream revision (=0) */
    put_tag(pb, "FFMP"); /* Vendor */
    if(track->enc->codec_id == CODEC_ID_RAWVIDEO) {
        put_be32(pb, 0); /* Temporal Quality */
        put_be32(pb, 0x400); /* Spatial Quality = lossless*/
    } else {
        put_be32(pb, 0x200); /* Temporal Quality = normal */
        put_be32(pb, 0x200); /* Spatial Quality = normal */
    }
    put_be16(pb, track->enc->width); /* Video width */
    put_be16(pb, track->enc->height); /* Video height */
    put_be32(pb, 0x00480000); /* Horizontal resolution 72dpi */
    put_be32(pb, 0x00480000); /* Vertical resolution 72dpi */
    put_be32(pb, 0); /* Data size (= 0) */
    put_be16(pb, 1); /* Frame count (= 1) */
    
    memset(compressor_name,0,32);
    if (track->enc->codec && track->enc->codec->name)
        strncpy(compressor_name,track->enc->codec->name,31);
    put_byte(pb, strlen(compressor_name));
    put_buffer(pb, compressor_name, 31);
    
    put_be16(pb, 0x18); /* Reserved */
    put_be16(pb, 0xffff); /* Reserved */
    if(track->enc->codec_id == CODEC_ID_MPEG4)
        mov_write_esds_tag(pb, track);
    else if(track->enc->codec_id == CODEC_ID_H263)
        mov_write_d263_tag(pb);
    else if(track->enc->codec_id == CODEC_ID_SVQ3)
        mov_write_svq3_tag(pb);    

    return updateSize (pb, pos);
}

static int mov_write_stsd_tag(ByteIOContext *pb, MOVTrack* track)
{
    int pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "stsd");
    put_be32(pb, 0); /* version & flags */
    put_be32(pb, 1); /* entry count */
    if (track->enc->codec_type == CODEC_TYPE_VIDEO)
        mov_write_video_tag(pb, track);
    else if (track->enc->codec_type == CODEC_TYPE_AUDIO)
        mov_write_audio_tag(pb, track);
    return updateSize(pb, pos);
}

/* TODO: */
/* Time to sample atom */
static int mov_write_stts_tag(ByteIOContext *pb, MOVTrack* track)
{
    put_be32(pb, 0x18); /* size */
    put_tag(pb, "stts");
    put_be32(pb, 0); /* version & flags */
    put_be32(pb, 1); /* entry count */

    put_be32(pb, track->sampleCount); /* sample count */
    put_be32(pb, track->sampleDuration); /* sample duration */
    return 0x18;
}

static int mov_write_dref_tag(ByteIOContext *pb)
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

static int mov_write_stbl_tag(ByteIOContext *pb, MOVTrack* track)
{
    int pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "stbl");
    mov_write_stsd_tag(pb, track);
    mov_write_stts_tag(pb, track);
    if (track->enc->codec_type == CODEC_TYPE_VIDEO &&
        track->hasKeyframes)
        mov_write_stss_tag(pb, track);
    mov_write_stsc_tag(pb, track);
    mov_write_stsz_tag(pb, track);
    mov_write_stco_tag(pb, track);
    return updateSize(pb, pos);
}

static int mov_write_dinf_tag(ByteIOContext *pb)
{
    int pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "dinf");
    mov_write_dref_tag(pb);
    return updateSize(pb, pos);
}

static int mov_write_smhd_tag(ByteIOContext *pb)
{
    put_be32(pb, 16); /* size */
    put_tag(pb, "smhd");
    put_be32(pb, 0); /* version & flags */
    put_be16(pb, 0); /* reserved (balance, normally = 0) */
    put_be16(pb, 0); /* reserved */
    return 16;
}

static int mov_write_vmhd_tag(ByteIOContext *pb)
{
    put_be32(pb, 0x14); /* size (always 0x14) */
    put_tag(pb, "vmhd");
    put_be32(pb, 0x01); /* version & flags */
    put_be64(pb, 0); /* reserved (graphics mode = copy) */
    return 0x14;
}

static int mov_write_hdlr_tag(ByteIOContext *pb, MOVTrack* track)
{
    char *descr, *hdlr, *hdlr_type;
    int pos = url_ftell(pb);
    
    if (!track) { /* no media --> data handler */
	hdlr = "dhlr";
	hdlr_type = "url ";
	descr = "DataHandler";
    } else {
	hdlr = (track->mode == MODE_MOV) ? "mhlr" : "\0\0\0\0";
	if (track->enc->codec_type == CODEC_TYPE_VIDEO) {
	    hdlr_type = "vide";
	    descr = "VideoHandler";
	} else {
	    hdlr_type = "soun";
	    descr = "SoundHandler";
	}
    }
    
    put_be32(pb, 0); /* size */
    put_tag(pb, "hdlr");
    put_be32(pb, 0); /* Version & flags */
    put_buffer(pb, hdlr, 4); /* handler */
    put_tag(pb, hdlr_type); /* handler type */
    put_be32(pb ,0); /* reserved */
    put_be32(pb ,0); /* reserved */
    put_be32(pb ,0); /* reserved */
    put_byte(pb, strlen(descr)); /* string counter */
    put_buffer(pb, descr, strlen(descr)); /* handler description */
    return updateSize(pb, pos);
}

static int mov_write_minf_tag(ByteIOContext *pb, MOVTrack* track)
{
    int pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "minf");
    if(track->enc->codec_type == CODEC_TYPE_VIDEO)
        mov_write_vmhd_tag(pb);
    else
        mov_write_smhd_tag(pb);
    if (track->mode == MODE_MOV) /* FIXME: Why do it for MODE_MOV only ? */
        mov_write_hdlr_tag(pb, NULL);
    mov_write_dinf_tag(pb);
    mov_write_stbl_tag(pb, track);
    return updateSize(pb, pos);
}

static int mov_write_mdhd_tag(ByteIOContext *pb, MOVTrack* track)
{
    put_be32(pb, 32); /* size */
    put_tag(pb, "mdhd");
    put_be32(pb, 0); /* Version & flags */
    put_be32(pb, track->time); /* creation time */
    put_be32(pb, track->time); /* modification time */
    put_be32(pb, track->timescale); /* time scale (sample rate for audio) */ 
    put_be32(pb, track->trackDuration); /* duration */
    put_be16(pb, 0); /* language, 0 = english */
    put_be16(pb, 0); /* reserved (quality) */
    return 32;
}

static int mov_write_mdia_tag(ByteIOContext *pb, MOVTrack* track)
{
    int pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "mdia");
    mov_write_mdhd_tag(pb, track);
    mov_write_hdlr_tag(pb, track);
    mov_write_minf_tag(pb, track);
    return updateSize(pb, pos);
}

static int mov_write_tkhd_tag(ByteIOContext *pb, MOVTrack* track)
{
    put_be32(pb, 0x5c); /* size (always 0x5c) */
    put_tag(pb, "tkhd");
    put_be32(pb, 0xf); /* version & flags (track enabled) */
    put_be32(pb, track->time); /* creation time */
    put_be32(pb, track->time); /* modification time */
    put_be32(pb, track->trackID); /* track-id */
    put_be32(pb, 0); /* reserved */
    put_be32(pb, av_rescale_rnd(track->trackDuration, globalTimescale, track->timescale, AV_ROUND_UP)); /* duration */

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
        double sample_aspect_ratio = av_q2d(track->enc->sample_aspect_ratio);
        if( !sample_aspect_ratio ) sample_aspect_ratio = 1;
        put_be32(pb, sample_aspect_ratio * track->enc->width*0x10000);
        put_be32(pb, track->enc->height*0x10000);
    }
    else {
        put_be32(pb, 0);
        put_be32(pb, 0);
    }
    return 0x5c;
}

// This box seems important for the psp playback ... without it the movie seems to hang
static int mov_write_edts_tag(ByteIOContext *pb, MOVTrack *track)
{
    put_be32(pb, 0x24); /* size  */
    put_tag(pb, "edts");
    put_be32(pb, 0x1c); /* size  */
    put_tag(pb, "elst");
    put_be32(pb, 0x0);
    put_be32(pb, 0x1);

    put_be32(pb, av_rescale_rnd(track->trackDuration, globalTimescale, track->timescale, AV_ROUND_UP)); /* duration   ... doesn't seem to effect psp */

    put_be32(pb, 0x0);
    put_be32(pb, 0x00010000);
    return 0x24;
}

// goes at the end of each track!  ... Critical for PSP playback ("Incompatible data" without it)
static int mov_write_uuid_tag_psp(ByteIOContext *pb, MOVTrack *mov)
{
    put_be32(pb, 0x34); /* size ... reports as 28 in mp4box! */
    put_tag(pb, "uuid");
    put_tag(pb, "USMT");
    put_be32(pb, 0x21d24fce);
    put_be32(pb, 0xbb88695c);
    put_be32(pb, 0xfac9c740);
    put_be32(pb, 0x1c);     // another size here!
    put_tag(pb, "MTDT");
    put_be32(pb, 0x00010012);
    put_be32(pb, 0x0a);
    put_be32(pb, 0x55c40000);
    put_be32(pb, 0x1);
    put_be32(pb, 0x0);
    return 0x34;
}

static int mov_write_trak_tag(ByteIOContext *pb, MOVTrack* track)
{
    int pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "trak");
    mov_write_tkhd_tag(pb, track);
    if (track->mode == MODE_PSP) 
        mov_write_edts_tag(pb, track);  // PSP Movies require edts box
    mov_write_mdia_tag(pb, track);
    if (track->mode == MODE_PSP) 
        mov_write_uuid_tag_psp(pb,track);  // PSP Movies require this uuid box
    return updateSize(pb, pos);
}

#if 0
/* TODO: Not sorted out, but not necessary either */
static int mov_write_iods_tag(ByteIOContext *pb, MOVContext *mov)
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
#endif

static int mov_write_mvhd_tag(ByteIOContext *pb, MOVContext *mov)
{
    int maxTrackID = 1, maxTrackLen = 0, i;
    int64_t maxTrackLenTemp;

    put_be32(pb, 0x6c); /* size (always 0x6c) */
    put_tag(pb, "mvhd");
    put_be32(pb, 0); /* version & flags */
    put_be32(pb, mov->time); /* creation time */
    put_be32(pb, mov->time); /* modification time */
    put_be32(pb, mov->timescale); /* timescale */
    for (i=0; i<MAX_STREAMS; i++) {
        if(mov->tracks[i].entry > 0) {
            maxTrackLenTemp = av_rescale_rnd(mov->tracks[i].trackDuration, globalTimescale, mov->tracks[i].timescale, AV_ROUND_UP);
            if(maxTrackLen < maxTrackLenTemp)
                maxTrackLen = maxTrackLenTemp;
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

static int mov_write_itunes_hdlr_tag(ByteIOContext *pb, MOVContext* mov,
                                     AVFormatContext *s)
{
    int pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "hdlr");
    put_be32(pb, 0);
    put_be32(pb, 0);
    put_tag(pb, "mdir");
    put_tag(pb, "appl");
    put_be32(pb, 0);
    put_be32(pb, 0);
    put_be16(pb, 0);
    return updateSize(pb, pos);
}

/* helper function to write a data tag with the specified string as data */
static int mov_write_string_data_tag(ByteIOContext *pb, MOVContext* mov,
                                     AVFormatContext *s, const char *data)
{
    int pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "data");
    put_be32(pb, 1);
    put_be32(pb, 0);
    put_buffer(pb, data, strlen(data));
    return updateSize(pb, pos);
}

/* iTunes name of the song/movie */
static int mov_write_nam_tag(ByteIOContext *pb, MOVContext* mov,
                             AVFormatContext *s)
{
    int size = 0;
    if ( s->title[0] ) {
        int pos = url_ftell(pb);
        put_be32(pb, 0); /* size */
        put_tag(pb, "\251nam");
        mov_write_string_data_tag(pb, mov, s, s->title);
        size = updateSize(pb, pos);
    }
    return size;
}

/* iTunes name of the artist/performer */
static int mov_write_ART_tag(ByteIOContext *pb, MOVContext* mov,
                             AVFormatContext *s)
{
    int size = 0;
    if ( s->author[0] ) {
        int pos = url_ftell(pb);
        put_be32(pb, 0); /* size */
        put_tag(pb, "\251ART");
        // we use the author here as this is the only thing that we have...
        mov_write_string_data_tag(pb, mov, s, s->author);
        size = updateSize(pb, pos);
    }
    return size;
}

/* iTunes name of the writer */
static int mov_write_wrt_tag(ByteIOContext *pb, MOVContext* mov,
                             AVFormatContext *s)
{
    int size = 0;
    if ( s->author[0] ) {
        int pos = url_ftell(pb);
        put_be32(pb, 0); /* size */
        put_tag(pb, "\251wrt");
        mov_write_string_data_tag(pb, mov, s, s->author);
        size = updateSize(pb, pos);
    }
    return size;
}

/* iTunes name of the album */
static int mov_write_alb_tag(ByteIOContext *pb, MOVContext* mov,
                             AVFormatContext *s)
{
    int size = 0;
    if ( s->album[0] ) {
        int pos = url_ftell(pb);
        put_be32(pb, 0); /* size */
        put_tag(pb, "\251alb");
        mov_write_string_data_tag(pb, mov, s, s->album);
        size = updateSize(pb, pos);
    }
    return size;
}

/* iTunes year */
static int mov_write_day_tag(ByteIOContext *pb, MOVContext* mov,
                             AVFormatContext *s)
{
    char year[5];
    int size = 0;
    if ( s->year ) {
        int pos = url_ftell(pb);
        put_be32(pb, 0); /* size */
        put_tag(pb, "\251day");
        snprintf(year, 5, "%04d", s->year);
        mov_write_string_data_tag(pb, mov, s, year);
        size = updateSize(pb, pos);
    }
    return size;
}

/* iTunes tool used to create the file */
static int mov_write_too_tag(ByteIOContext *pb, MOVContext* mov,
                             AVFormatContext *s)
{
    int pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "\251too");
    mov_write_string_data_tag(pb, mov, s, LIBAVFORMAT_IDENT);
    return updateSize(pb, pos);
}

/* iTunes comment */
static int mov_write_cmt_tag(ByteIOContext *pb, MOVContext* mov,
                             AVFormatContext *s)
{
    int size = 0;
    if ( s->comment[0] ) {
        int pos = url_ftell(pb);
        put_be32(pb, 0); /* size */
        put_tag(pb, "\251cmt");
        mov_write_string_data_tag(pb, mov, s, s->comment);
        size = updateSize(pb, pos);
    }
    return size;
}

/* iTunes custom genre */
static int mov_write_gen_tag(ByteIOContext *pb, MOVContext* mov,
                             AVFormatContext *s)
{
    int size = 0;
    if ( s->genre[0] ) {
        int pos = url_ftell(pb);
        put_be32(pb, 0); /* size */
        put_tag(pb, "\251gen");
        mov_write_string_data_tag(pb, mov, s, s->genre);
        size = updateSize(pb, pos);
    }
    return size;
}

/* iTunes track number */
static int mov_write_trkn_tag(ByteIOContext *pb, MOVContext* mov,
                              AVFormatContext *s)
{
    int size = 0;
    if ( s->track ) {
        int pos = url_ftell(pb);
        put_be32(pb, 0); /* size */
        put_tag(pb, "trkn");
        {
            int pos = url_ftell(pb);
            put_be32(pb, 0); /* size */
            put_tag(pb, "data");
            put_be32(pb, 0);        // 8 bytes empty
            put_be32(pb, 0);
            put_be16(pb, 0);        // empty
            put_be16(pb, s->track); // track number
            put_be16(pb, 0);        // total track number
            put_be16(pb, 0);        // empty
            updateSize(pb, pos);
        }
        size = updateSize(pb, pos);
    }
    return size;
}

/* iTunes meta data list */
static int mov_write_ilst_tag(ByteIOContext *pb, MOVContext* mov,
                              AVFormatContext *s)
{
    int pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "ilst");
    mov_write_nam_tag(pb, mov, s);
    mov_write_ART_tag(pb, mov, s);
    mov_write_wrt_tag(pb, mov, s);
    mov_write_alb_tag(pb, mov, s);
    mov_write_day_tag(pb, mov, s);
    mov_write_too_tag(pb, mov, s);
    mov_write_cmt_tag(pb, mov, s);
    mov_write_gen_tag(pb, mov, s);
    mov_write_trkn_tag(pb, mov, s);
    return updateSize(pb, pos);
}

/* iTunes meta data tag */
static int mov_write_meta_tag(ByteIOContext *pb, MOVContext* mov,
                              AVFormatContext *s)
{
    int size = 0;

    // only save meta tag if required
    if ( s->title[0] || s->author[0] || s->album[0] || s->year || 
         s->comment[0] || s->genre[0] || s->track ) {
        int pos = url_ftell(pb);
        put_be32(pb, 0); /* size */
        put_tag(pb, "meta");
        put_be32(pb, 0);
        mov_write_itunes_hdlr_tag(pb, mov, s);
        mov_write_ilst_tag(pb, mov, s);
        size = updateSize(pb, pos);
    }
    return size;
}
    
static int mov_write_udta_tag(ByteIOContext *pb, MOVContext* mov,
                              AVFormatContext *s)
{
    int pos = url_ftell(pb);
    int i;

    put_be32(pb, 0); /* size */
    put_tag(pb, "udta");

    /* iTunes meta data */
    mov_write_meta_tag(pb, mov, s);

    /* Requirements */
    for (i=0; i<MAX_STREAMS; i++) {
        if(mov->tracks[i].entry <= 0) continue;
        if (mov->tracks[i].enc->codec_id == CODEC_ID_AAC ||
            mov->tracks[i].enc->codec_id == CODEC_ID_MPEG4) {
            int pos = url_ftell(pb);
            put_be32(pb, 0); /* size */
            put_tag(pb, "\251req");
            put_be16(pb, sizeof("QuickTime 6.0 or greater") - 1);
            put_be16(pb, 0);
            put_buffer(pb, "QuickTime 6.0 or greater",
                       sizeof("QuickTime 6.0 or greater") - 1);
            updateSize(pb, pos);
            break;
        }
    }

    /* Encoder */
    if(mov->tracks[0].enc && !(mov->tracks[0].enc->flags & CODEC_FLAG_BITEXACT))
    {
        int pos = url_ftell(pb);
        put_be32(pb, 0); /* size */
        put_tag(pb, "\251enc");
        put_be16(pb, sizeof(LIBAVFORMAT_IDENT) - 1); /* string length */
        put_be16(pb, 0);
        put_buffer(pb, LIBAVFORMAT_IDENT, sizeof(LIBAVFORMAT_IDENT) - 1);
        updateSize(pb, pos);
    }

    if( s->title[0] )
    {
        int pos = url_ftell(pb);
        put_be32(pb, 0); /* size */
        put_tag(pb, "\251nam");
        put_be16(pb, strlen(s->title)); /* string length */
        put_be16(pb, 0);
        put_buffer(pb, s->title, strlen(s->title));
        updateSize(pb, pos);
    }

    if( s->author[0] )
    {
        int pos = url_ftell(pb);
        put_be32(pb, 0); /* size */
        put_tag(pb, /*"\251aut"*/ "\251day" );
        put_be16(pb, strlen(s->author)); /* string length */
        put_be16(pb, 0);
        put_buffer(pb, s->author, strlen(s->author));
        updateSize(pb, pos);
    }

    if( s->comment[0] )
    {
        int pos = url_ftell(pb);
        put_be32(pb, 0); /* size */
        put_tag(pb, "\251des");
        put_be16(pb, strlen(s->comment)); /* string length */
        put_be16(pb, 0);
        put_buffer(pb, s->comment, strlen(s->comment));
        updateSize(pb, pos);
    }

    return updateSize(pb, pos);
}

static int mov_write_moov_tag(ByteIOContext *pb, MOVContext *mov,
                              AVFormatContext *s)
{
    int pos, i;
    pos = url_ftell(pb);
    put_be32(pb, 0); /* size placeholder*/
    put_tag(pb, "moov");
    mov->timescale = globalTimescale;

    for (i=0; i<MAX_STREAMS; i++) {
        if(mov->tracks[i].entry <= 0) continue;

        if(mov->tracks[i].enc->codec_type == CODEC_TYPE_VIDEO) {
            mov->tracks[i].timescale = mov->tracks[i].enc->time_base.den;
            mov->tracks[i].sampleDuration = mov->tracks[i].enc->time_base.num;
        }
        else if(mov->tracks[i].enc->codec_type == CODEC_TYPE_AUDIO) {
            /* If AMR, track timescale = 8000, AMR_WB = 16000 */
            if(mov->tracks[i].enc->codec_id == CODEC_ID_AMR_NB) {
                mov->tracks[i].sampleDuration = 160;  // Bytes per chunk
                mov->tracks[i].timescale = 8000;
            }
            else {
                mov->tracks[i].timescale = mov->tracks[i].enc->sample_rate;
                mov->tracks[i].sampleDuration = mov->tracks[i].enc->frame_size;
            }
        }

        mov->tracks[i].trackDuration = 
            mov->tracks[i].sampleCount * mov->tracks[i].sampleDuration;
        mov->tracks[i].time = mov->time;
        mov->tracks[i].trackID = i+1;
    }

    mov_write_mvhd_tag(pb, mov);
    //mov_write_iods_tag(pb, mov);
    for (i=0; i<MAX_STREAMS; i++) {
        if(mov->tracks[i].entry > 0) {
            mov_write_trak_tag(pb, &(mov->tracks[i]));
        }
    }

    mov_write_udta_tag(pb, mov, s);

    return updateSize(pb, pos);
}

int mov_write_mdat_tag(ByteIOContext *pb, MOVContext* mov)
{
    mov->mdat_pos = url_ftell(pb); 
    put_be32(pb, 0); /* size placeholder*/
    put_tag(pb, "mdat");
    return 0;
}

/* TODO: This needs to be more general */
int mov_write_ftyp_tag(ByteIOContext *pb, AVFormatContext *s)
{
    MOVContext *mov = s->priv_data;

    put_be32(pb, 0x14 ); /* size */
    put_tag(pb, "ftyp");

    if ( mov->mode == MODE_3GP )
        put_tag(pb, "3gp4");
    else if ( mov->mode == MODE_3G2 )
        put_tag(pb, "3g2a");
    else if ( mov->mode == MODE_PSP )
        put_tag(pb, "MSNV");
    else
        put_tag(pb, "isom");

    put_be32(pb, 0x200 );

    if ( mov->mode == MODE_3GP )
        put_tag(pb, "3gp4");
    else if ( mov->mode == MODE_3G2 )
        put_tag(pb, "3g2a");
    else if ( mov->mode == MODE_PSP )
        put_tag(pb, "MSNV");
    else
        put_tag(pb, "mp41");

    return 0x14;
}

static void mov_write_uuidprof_tag(ByteIOContext *pb, AVFormatContext *s)
{
    int AudioRate = s->streams[1]->codec->sample_rate;
    int FrameRate = ((s->streams[0]->codec->time_base.den) * (0x10000))/ (s->streams[0]->codec->time_base.num);
 
    //printf("audiorate = %d\n",AudioRate);
    //printf("framerate = %d / %d = 0x%x\n",s->streams[0]->codec->time_base.den,s->streams[0]->codec->time_base.num,FrameRate);

    put_be32(pb, 0x94 ); /* size */
    put_tag(pb, "uuid");
    put_tag(pb, "PROF");

    put_be32(pb, 0x21d24fce ); /* 96 bit UUID */
    put_be32(pb, 0xbb88695c );
    put_be32(pb, 0xfac9c740 );

    put_be32(pb, 0x0 );  /* ? */
    put_be32(pb, 0x3 );  /* 3 sections ? */

    put_be32(pb, 0x14 ); /* size */
    put_tag(pb, "FPRF");
    put_be32(pb, 0x0 );  /* ? */
    put_be32(pb, 0x0 );  /* ? */
    put_be32(pb, 0x0 );  /* ? */

    put_be32(pb, 0x2c );  /* size */
    put_tag(pb, "APRF");   /* audio */
    put_be32(pb, 0x0 );
    put_be32(pb, 0x2 );
    put_tag(pb, "mp4a");
    put_be32(pb, 0x20f );
    put_be32(pb, 0x0 );
    put_be32(pb, 0x40 );
    put_be32(pb, 0x40 );
    put_be32(pb, AudioRate ); //24000   ... audio rate?
    put_be32(pb, 0x2 );

    put_be32(pb, 0x34 );  /* size */
    put_tag(pb, "VPRF");   /* video */
    put_be32(pb, 0x0 );
    put_be32(pb, 0x1 );
    put_tag(pb, "mp4v");
    put_be32(pb, 0x103 );
    put_be32(pb, 0x0 );
    put_be32(pb, 0xc0 );
    put_be32(pb, 0xc0 );
    put_be32(pb, FrameRate);  // was 0xefc29   
    put_be32(pb, FrameRate );  // was 0xefc29
    put_be16(pb, s->streams[0]->codec->width);
    put_be16(pb, s->streams[0]->codec->height);
    put_be32(pb, 0x010001 );
}

static int mov_write_header(AVFormatContext *s)
{
    ByteIOContext *pb = &s->pb;
    MOVContext *mov = s->priv_data;
    int i;

    for(i=0; i<s->nb_streams; i++){
        AVCodecContext *c= s->streams[i]->codec;

        if      (c->codec_type == CODEC_TYPE_VIDEO){
            if (!codec_get_tag(codec_movvideo_tags, c->codec_id)){
                if(!codec_get_tag(codec_bmp_tags, c->codec_id))
                    return -1;
                else
                    av_log(s, AV_LOG_INFO, "Warning, using MS style video codec tag, the file may be unplayable!\n");
            }
        }else if(c->codec_type == CODEC_TYPE_AUDIO){
            if (!codec_get_tag(codec_movaudio_tags, c->codec_id)){
                if(!codec_get_tag(codec_wav_tags, c->codec_id))
                    return -1;
                else
                    av_log(s, AV_LOG_INFO, "Warning, using MS style audio codec tag, the file may be unplayable!\n");
            }
        }
    }

    /* Default mode == MP4 */
    mov->mode = MODE_MP4;

    if (s->oformat != NULL) {
        if (!strcmp("3gp", s->oformat->name)) mov->mode = MODE_3GP;
        else if (!strcmp("3g2", s->oformat->name)) mov->mode = MODE_3G2;
        else if (!strcmp("mov", s->oformat->name)) mov->mode = MODE_MOV;
        else if (!strcmp("psp", s->oformat->name)) mov->mode = MODE_PSP;

        if ( mov->mode == MODE_3GP || mov->mode == MODE_3G2 ||
             mov->mode == MODE_MP4 || mov->mode == MODE_PSP )
            mov_write_ftyp_tag(pb,s);
        if ( mov->mode == MODE_PSP ) {
            if ( s->nb_streams != 2 ) {
                av_log(s, AV_LOG_ERROR, "PSP mode need one video and one audio stream\n");
                return -1;
            }
            mov_write_uuidprof_tag(pb,s);
        }
    }

    for (i=0; i<MAX_STREAMS; i++) {
        mov->tracks[i].mode = mov->mode;
    }

    put_flush_packet(pb);

    return 0;
}

static int mov_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    MOVContext *mov = s->priv_data;
    ByteIOContext *pb = &s->pb;
    AVCodecContext *enc = s->streams[pkt->stream_index]->codec;
    MOVTrack* trk = &mov->tracks[pkt->stream_index];
    int cl, id;
    unsigned int samplesInChunk = 0;
    int size= pkt->size;

    if (url_is_streamed(&s->pb)) return 0; /* Can't handle that */
    if (!size) return 0; /* Discard 0 sized packets */

    if (enc->codec_type == CODEC_TYPE_VIDEO ) {
        samplesInChunk = 1;
    }
    else if (enc->codec_type == CODEC_TYPE_AUDIO ) {
        if( enc->codec_id == CODEC_ID_AMR_NB) {
            /* We must find out how many AMR blocks there are in one packet */
            static uint16_t packed_size[16] =
                {13, 14, 16, 18, 20, 21, 27, 32, 6, 0, 0, 0, 0, 0, 0, 0};
            int len = 0;

            while (len < size && samplesInChunk < 100) {
                len += packed_size[(pkt->data[len] >> 3) & 0x0F];
                samplesInChunk++;
            }
        }
        else if(enc->codec_id == CODEC_ID_PCM_ALAW) {
            samplesInChunk = size/enc->channels;
        }
	else if(enc->codec_id == CODEC_ID_PCM_S16BE || enc->codec_id == CODEC_ID_PCM_S16LE) {
	    samplesInChunk = size/(2*enc->channels);
        }	    
        else {
            samplesInChunk = 1;
        }
    }

    if ((enc->codec_id == CODEC_ID_MPEG4 || enc->codec_id == CODEC_ID_AAC)
        && trk->vosLen == 0) {
//        assert(enc->extradata_size);

        trk->vosLen = enc->extradata_size;
        trk->vosData = av_malloc(trk->vosLen);
        memcpy(trk->vosData, enc->extradata, trk->vosLen);
    }

    cl = trk->entry / MOV_INDEX_CLUSTER_SIZE;
    id = trk->entry % MOV_INDEX_CLUSTER_SIZE;

    if (trk->ents_allocated <= trk->entry) {
        trk->cluster = av_realloc(trk->cluster, (cl+1)*sizeof(void*)); 
        if (!trk->cluster)
            return -1;
        trk->cluster[cl] = av_malloc(MOV_INDEX_CLUSTER_SIZE*sizeof(MOVIentry));
        if (!trk->cluster[cl])
            return -1;
        trk->ents_allocated += MOV_INDEX_CLUSTER_SIZE;
    }
    if (mov->mdat_written == 0) {
        mov_write_mdat_tag(pb, mov);
        mov->mdat_written = 1;
        mov->time = s->timestamp + 0x7C25B080; //1970 based -> 1904 based
    }

    trk->cluster[cl][id].pos = url_ftell(pb);
    trk->cluster[cl][id].samplesInChunk = samplesInChunk;
    trk->cluster[cl][id].size = size;
    trk->cluster[cl][id].entries = samplesInChunk;
    if(enc->codec_type == CODEC_TYPE_VIDEO) {
        trk->cluster[cl][id].key_frame = !!(pkt->flags & PKT_FLAG_KEY);
        if(trk->cluster[cl][id].key_frame)
            trk->hasKeyframes = 1;
    }
    trk->enc = enc;
    trk->entry++;
    trk->sampleCount += samplesInChunk;
    trk->mdat_size += size;

    put_buffer(pb, pkt->data, size);

    put_flush_packet(pb);
    return 0;
}

static int mov_write_trailer(AVFormatContext *s)
{
    MOVContext *mov = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int res = 0;
    int i, j;

    offset_t moov_pos = url_ftell(pb);

    /* Write size of mdat tag */
    for (i=0, j=0; i<MAX_STREAMS; i++) {
        if(mov->tracks[i].ents_allocated > 0) {
            j += mov->tracks[i].mdat_size;
        }
    }
    url_fseek(pb, mov->mdat_pos, SEEK_SET);
    put_be32(pb, j+8);
    url_fseek(pb, moov_pos, SEEK_SET);

    mov_write_moov_tag(pb, mov, s);

    for (i=0; i<MAX_STREAMS; i++) {
        for (j=0; j<mov->tracks[i].ents_allocated/MOV_INDEX_CLUSTER_SIZE; j++) {
            av_free(mov->tracks[i].cluster[j]);
        }
        av_free(mov->tracks[i].cluster);
        if( mov->tracks[i].vosLen ) av_free( mov->tracks[i].vosData );

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
    CODEC_ID_AAC,
    CODEC_ID_MPEG4,
    mov_write_header,
    mov_write_packet,
    mov_write_trailer,
    .flags = AVFMT_GLOBALHEADER,
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
    .flags = AVFMT_GLOBALHEADER,
};

static AVOutputFormat mp4_oformat = {
    "mp4",
    "mp4 format",
    "application/mp4",
    "mp4,m4a",
    sizeof(MOVContext),
    CODEC_ID_AAC,
    CODEC_ID_MPEG4,
    mov_write_header,
    mov_write_packet,
    mov_write_trailer,
    .flags = AVFMT_GLOBALHEADER,
};

static AVOutputFormat psp_oformat = {
    "psp",
    "psp mp4 format",
    NULL,
    "mp4,psp",
    sizeof(MOVContext),
    CODEC_ID_AAC,
    CODEC_ID_MPEG4,
    mov_write_header,
    mov_write_packet,
    mov_write_trailer,
//    .flags = AVFMT_GLOBALHEADER,
};

static AVOutputFormat _3g2_oformat = {
    "3g2",
    "3gp2 format",
    NULL,
    "3g2",
    sizeof(MOVContext),
    CODEC_ID_AMR_NB,
    CODEC_ID_H263,
    mov_write_header,
    mov_write_packet,
    mov_write_trailer,
    .flags = AVFMT_GLOBALHEADER,
};

int movenc_init(void)
{
    av_register_output_format(&mov_oformat);
    av_register_output_format(&_3gp_oformat);
    av_register_output_format(&mp4_oformat);
    av_register_output_format(&psp_oformat);
    av_register_output_format(&_3g2_oformat);
    return 0;
}
