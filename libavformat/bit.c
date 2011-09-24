#include "avformat.h"
#include "libavcodec/get_bits.h"
#include "libavcodec/put_bits.h"

#define MAX_FRAME_SIZE 10

#define SYNC_WORD  0x6b21
#define BIT_0      0x7f
#define BIT_1      0x81

static int probe(AVProbeData *p)
{
    int i;
    i=AV_RL16(&p->buf[0]);
    if(i != SYNC_WORD)
        return 0;

    switch(AV_RL16(&p->buf[2]))
    {
    case 0x40:
    case 0x50:
        return AVPROBE_SCORE_MAX/2;
    default:
        return 0;
    }
}

static int read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AVStream* st;
    ByteIOContext *pb = s->pb;

    st=av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    url_fskip(pb, 2);
    switch(get_le16(pb))
    {
        case 0x40:
            st->codec->bit_rate = 6400;
            break;
        case 0x50:
            st->codec->bit_rate = 8000;
            break;
    }

    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id=CODEC_ID_G729;
    st->codec->sample_rate=8000;
    st->codec->block_align = 16;
    st->codec->channels=1;

    av_set_pts_info(st, 64, 1, 100);
    url_fseek(pb, 0, SEEK_SET);
    return 0;
}

static int read_packet(AVFormatContext *s,
                          AVPacket *pkt)
{
    ByteIOContext *pb = s->pb;
    PutBitContext pbo;
    uint16_t buf[8 * MAX_FRAME_SIZE + 2];
    int packet_size;
    int sync;
    uint16_t* src=buf;
    int i, j, ret;

    if(url_feof(pb))
        return AVERROR(EIO);

    sync = get_le16(pb); // sync word
    packet_size = get_le16(pb) / 8;
    assert(packet_size < 8 * MAX_FRAME_SIZE);

    ret = get_buffer(pb, (uint8_t*)buf, (8 * packet_size) * sizeof(uint16_t));
    if(ret<0)
        return ret;
    if(ret != 8 * packet_size * sizeof(uint16_t))
        return AVERROR(EIO);

    av_new_packet(pkt, packet_size);

    init_put_bits(&pbo, pkt->data, packet_size);
    for(j=0; j < packet_size; j++)
        for(i=0; i<8;i++)
            put_bits(&pbo,1, AV_RL16(src++) == BIT_1 ? 1 : 0);

    flush_put_bits(&pbo);

    pkt->duration=1;
    return 0;
}

AVInputFormat ff_bit_demuxer = {
    "bit",
    "G.729 BIT file format",
    0,
    probe,
    read_header,
    read_packet,
    .extensions = "bit"
};

#ifdef CONFIG_MUXERS
static int write_header(AVFormatContext *s)
{
    AVCodecContext *enc = s->streams[0]->codec;

    enc->codec_id = CODEC_ID_G729;
    enc->channels = 1;
    enc->bits_per_coded_sample = 16;
    enc->block_align = (enc->bits_per_coded_sample * enc->channels) >> 3;

    return 0;
}

static int write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *pb = s->pb;
    GetBitContext gb;
    int i;
    uint16_t b;

    put_le16(pb, SYNC_WORD);
    put_le16(pb, 8 * 10);

    init_get_bits(&gb, pkt->data, 8*10);
    for(i=0; i< 8 * 10; i++)
        put_le16(pb, get_bits1(&gb) ? BIT_1 : BIT_0);
    put_flush_packet(pb);

    return 0;
}

AVOutputFormat ff_bit_muxer = {
    "bit",
    "G.729 BIT file format",
    "audio/bit",
    "bit",
    0,
    CODEC_ID_G729,
    CODEC_ID_NONE,
    write_header,
    write_packet,
    .extensions = "bit"
};
#endif
