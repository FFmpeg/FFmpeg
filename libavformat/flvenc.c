/*
 * FLV encoder.
 * Copyright (c) 2003 The FFmpeg Project.
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

#undef NDEBUG
#include <assert.h>

#define VIDEO_FIFO_SIZE 512

typedef struct FLVFrame {
    int type;
    int timestamp;
    int reserved;
    int flags;
    uint8_t *data;
    int size;
    struct FLVFrame *next;
} FLVFrame;

typedef struct FLVContext {
    int hasAudio;
    int hasVideo;
    int initDelay;
    int64_t sampleCount;
    int64_t frameCount;
    int reserved;
    FLVFrame *frames;
} FLVContext;

#ifdef CONFIG_MP3LAME

static const int sSampleRates[3][4] = {
    {44100, 48000, 32000, 0},
    {22050, 24000, 16000, 0},
    {11025, 12000,  8000, 0},
};

static const int sBitRates[2][3][15] = {
    {   {  0, 32, 64, 96,128,160,192,224,256,288,320,352,384,416,448},
        {  0, 32, 48, 56, 64, 80, 96,112,128,160,192,224,256,320,384},
        {  0, 32, 40, 48, 56, 64, 80, 96,112,128,160,192,224,256,320}
    },
    {   {  0, 32, 48, 56, 64, 80, 96,112,128,144,160,176,192,224,256},
        {  0,  8, 16, 24, 32, 40, 48, 56, 64, 80, 96,112,128,144,160},
        {  0,  8, 16, 24, 32, 40, 48, 56, 64, 80, 96,112,128,144,160}
    },
};

static const int sSamplesPerFrame[3][3] =
{
    {  384,     1152,    1152 },
    {  384,     1152,     576 },
    {  384,     1152,     576 }
};

static const int sBitsPerSlot[3] = {
    32,
    8,
    8
};

static int mp3info(void *data, int *byteSize, int *samplesPerFrame, int *sampleRate, int *isMono )
{
    uint8_t *dataTmp = (uint8_t *)data;
    uint32_t header = ( (uint32_t)dataTmp[0] << 24 ) | ( (uint32_t)dataTmp[1] << 16 ) | ( (uint32_t)dataTmp[2] << 8 ) | (uint32_t)dataTmp[3];
    int layerID = 3 - ((header >> 17) & 0x03);
    int bitRateID = ((header >> 12) & 0x0f);
    int sampleRateID = ((header >> 10) & 0x03);
    int bitRate = 0;
    int bitsPerSlot = sBitsPerSlot[layerID];
    int isPadded = ((header >> 9) & 0x01);

    if ( (( header >> 21 ) & 0x7ff) != 0x7ff ) {
        return 0;
    }

    if ( !isPadded ) {
//        printf("Fatal error: mp3 data is not padded!\n");
//        exit(0);
    }

    *isMono = ((header >>  6) & 0x03) == 0x03;

    if ( (header >> 19 ) & 0x01 ) {
        //MPEG1
        *sampleRate = sSampleRates[0][sampleRateID];
        bitRate = sBitRates[0][layerID][bitRateID] * 1000;
        *samplesPerFrame = sSamplesPerFrame[0][layerID];
    } else {
        if ( (header >> 20) & 0x01 ) {
            //MPEG2
            *sampleRate = sSampleRates[1][sampleRateID];
            bitRate = sBitRates[1][layerID][bitRateID] * 1000;
            *samplesPerFrame = sSamplesPerFrame[1][layerID];
        } else {
            //MPEG2.5
            *sampleRate = sSampleRates[2][sampleRateID];
            bitRate = sBitRates[1][layerID][bitRateID] * 1000;
            *samplesPerFrame = sSamplesPerFrame[2][layerID];
        }
    }
    
    *byteSize = ( ( ( ( *samplesPerFrame * (bitRate / bitsPerSlot) ) / *sampleRate ) + isPadded ) );
    return 1;
}
#endif // CONFIG_MP3LAME

static void put_be24(ByteIOContext *pb, int value)
{
    put_byte(pb, (value>>16) & 0xFF );
    put_byte(pb, (value>> 8) & 0xFF );
    put_byte(pb, (value>> 0) & 0xFF );
}

static int flv_write_header(AVFormatContext *s)
{
    ByteIOContext *pb = &s->pb;
    FLVContext *flv = s->priv_data;
    int i;

    av_set_pts_info(s, 24, 1, 1000); /* 24 bit pts in ms */

    flv->hasAudio = 0;
    flv->hasVideo = 0;

    flv->initDelay = -1;

    flv->frames = 0;

    put_tag(pb,"FLV");
    put_byte(pb,1);
    put_byte(pb,0); // delayed write
    put_be32(pb,9);
    put_be32(pb,0);
    
    for(i=0; i<s->nb_streams; i++){
        AVCodecContext *enc = &s->streams[i]->codec;
        if(enc->codec_tag == 5){
            put_byte(pb,8); // message type
            put_be24(pb,0); // include flags
            put_be24(pb,0); // time stamp
            put_be32(pb,0); // reserved
            put_be32(pb,11); // size
            flv->reserved=5;
        }
    }

    return 0;
}

static void InsertSorted(FLVContext *flv, FLVFrame *frame)
{
    if ( !flv->frames ) {
        flv->frames = frame;
    } else {
        FLVFrame *trav = flv->frames;
        FLVFrame *prev = 0;
        for (;trav;) {
            if ( trav->timestamp > frame->timestamp) {
                frame->next = trav;
                if ( prev ) {
                    prev->next = frame;
                } else {
                    flv->frames = frame;
                }
                break;
            }
            prev = trav;
            trav = trav->next;
        }
        if ( !trav ) {
            prev->next = frame;
        }
    }
}

static void DumpFrame(ByteIOContext *pb, FLVFrame *frame)
{
//av_log(NULL, AV_LOG_DEBUG, "T%02X S%d T%d R%d F%02X ... R%08X\n", frame->type, frame->size+1, frame->timestamp, 0, frame->flags, frame->size+1+11);
    put_byte(pb,frame->type); // message type
    put_be24(pb,frame->size+1); // include flags
    put_be24(pb,frame->timestamp); // time stamp
    put_be32(pb,frame->reserved); // reserved
    put_byte(pb,frame->flags);
    put_buffer(pb, frame->data, frame->size);
    put_be32(pb,frame->size+1+11); // reserved
    av_free(frame->data);
}

static void Dump(FLVContext *flv, ByteIOContext *pb, int count)
{
    int c=0;
    FLVFrame *trav = flv->frames;
    FLVFrame *prev = 0;
    for (;trav;c++) {
        trav = trav->next;
    }
    trav = flv->frames;
    for ( ; c >= count; c-- ) {
        DumpFrame(pb,trav);
        prev = trav;
        trav = trav->next;
        av_free(prev);
    }
     flv->frames = trav;
}

static int flv_write_trailer(AVFormatContext *s)
{
    int64_t file_size;
    int flags = 0;

    ByteIOContext *pb = &s->pb;
    FLVContext *flv = s->priv_data;

    Dump(flv,pb,1);

    file_size = url_ftell(pb);
    flags |= flv->hasAudio ? 4 : 0;
    flags |= flv->hasVideo ? 1 : 0;
    url_fseek(pb, 4, SEEK_SET);
    put_byte(pb,flags);
    url_fseek(pb, file_size, SEEK_SET);
    return 0;
}

static int flv_write_packet(AVFormatContext *s, int stream_index,
                            const uint8_t *buf, int size, int64_t timestamp)
{
    ByteIOContext *pb = &s->pb;
    AVCodecContext *enc = &s->streams[stream_index]->codec;
    FLVContext *flv = s->priv_data;
    FLVFrame *frame = av_malloc(sizeof(FLVFrame));

    frame->next = 0;
    frame->size = size;
    frame->data = av_malloc(size);
    frame->timestamp = timestamp;
    frame->reserved= flv->reserved;
    memcpy(frame->data,buf,size);
    
//    av_log(s, AV_LOG_DEBUG, "type:%d pts: %lld size:%d\n", enc->codec_type, timestamp, size);
    
    if (enc->codec_type == CODEC_TYPE_VIDEO) {
        frame->type = 9;
        frame->flags = 2; // choose h263
        frame->flags |= enc->coded_frame->key_frame ? 0x10 : 0x20; // add keyframe indicator
        //frame->timestamp = ( ( flv->frameCount * (int64_t)FRAME_RATE_BASE * (int64_t)1000 ) / (int64_t)enc->frame_rate );
        //printf("%08x %f %f\n",frame->timestamp,(double)enc->frame_rate/(double)FRAME_RATE_BASE,1000*(double)FRAME_RATE_BASE/(double)enc->frame_rate);
        flv->hasVideo = 1;

        InsertSorted(flv,frame);

        flv->frameCount ++;
    }
    else if (enc->codec_type == CODEC_TYPE_AUDIO) {
        int soundFormat = 0x02;

        switch (enc->sample_rate) {
            case    44100:
                soundFormat |= 0x0C;
                break;
            case    22050:
                soundFormat |= 0x08;
                break;
            case    11025:
                soundFormat |= 0x04;
                break;
            case     8000: //nellymoser only
            case     5512: //not mp3
                soundFormat |= 0x00;
                break;
            default:
                assert(0);
        }

        if (enc->channels > 1) {
            soundFormat |= 0x01;
        }
        
        switch(enc->codec_id){
        case CODEC_ID_MP3:
            soundFormat |= 0x20;
            break;
        case 0:
            soundFormat |= enc->codec_tag<<4;
            break;
        default:
            assert(0);
        }

        assert(size);
        if ( flv->initDelay == -1 ) {
            flv->initDelay = timestamp;
        }

        frame->type = 8;
        frame->flags = soundFormat;

//            if ( flv->audioTime == -1 ) {
//                flv->audioTime = ( ( ( flv->sampleCount - enc->delay ) * 8000 ) / flv->audioRate ) - flv->initDelay - 250;
//                if ( flv->audioTime < 0 ) {
//                    flv->audioTime = 0;
//                }
//            }

#ifdef CONFIG_MP3LAME
        if (enc->codec_id == CODEC_ID_MP3 ) {
            int mp3FrameSize = 0;
            int mp3SampleRate = 0;
            int mp3IsMono = 0;
            int mp3SamplesPerFrame = 0;

            /* copy out mp3 header from ring buffer */
            if(!mp3info(buf,&mp3FrameSize,&mp3SamplesPerFrame,&mp3SampleRate,&mp3IsMono))
                assert(0);
            assert ( size == mp3FrameSize );
            assert(enc->sample_rate == mp3SampleRate);
//            assert(enc->frame_size == mp3SamplesPerFrame);
//av_log(NULL, AV_LOG_DEBUG, "sizes: %d %d\n", enc->frame_size, mp3SamplesPerFrame);

            frame->timestamp = (1000*flv->sampleCount + enc->sample_rate/2)/(enc->sample_rate);
            flv->sampleCount += mp3SamplesPerFrame;
        }
#endif

        // We got audio! Make sure we set this to the global flags on closure
        flv->hasAudio = 1;
        InsertSorted(flv,frame);
    }else
        assert(0);
    Dump(flv,pb,128);
    put_flush_packet(pb);
    return 0;
}

static AVOutputFormat flv_oformat = {
    "flv",
    "flv format",
    "video/x-flashvideo",
    "flv",
    sizeof(FLVContext),
#ifdef CONFIG_MP3LAME
    CODEC_ID_MP3,
#else // CONFIG_MP3LAME
    CODEC_ID_NONE,
#endif // CONFIG_MP3LAME
    CODEC_ID_FLV1,
    flv_write_header,
    flv_write_packet,
    flv_write_trailer,
};

int flvenc_init(void)
{
    av_register_output_format(&flv_oformat);
    return 0;
}
