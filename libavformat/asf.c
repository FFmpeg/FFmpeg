/*
 * ASF compatible decoder.
 * Copyright (c) 2000, 2001 Fabrice Bellard.
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
#include "mpegaudio.h"
#include "asf.h"

#undef NDEBUG
#include <assert.h>

#define FRAME_HEADER_SIZE 17
// Fix Me! FRAME_HEADER_SIZE may be different. 

static const GUID index_guid = {
    0x33000890, 0xe5b1, 0x11cf, { 0x89, 0xf4, 0x00, 0xa0, 0xc9, 0x03, 0x49, 0xcb },
};

/**********************************/
/* decoding */

//#define DEBUG

#ifdef DEBUG
#define PRINT_IF_GUID(g,cmp) \
if (!memcmp(g, &cmp, sizeof(GUID))) \
    printf("(GUID: %s) ", #cmp)

static void print_guid(const GUID *g)
{
    int i;
    PRINT_IF_GUID(g, asf_header);
    else PRINT_IF_GUID(g, file_header);
    else PRINT_IF_GUID(g, stream_header);
    else PRINT_IF_GUID(g, audio_stream);
    else PRINT_IF_GUID(g, audio_conceal_none);
    else PRINT_IF_GUID(g, video_stream);
    else PRINT_IF_GUID(g, video_conceal_none);
    else PRINT_IF_GUID(g, comment_header);
    else PRINT_IF_GUID(g, codec_comment_header);
    else PRINT_IF_GUID(g, codec_comment1_header);
    else PRINT_IF_GUID(g, data_header);
    else PRINT_IF_GUID(g, index_guid);
    else PRINT_IF_GUID(g, head1_guid);
    else PRINT_IF_GUID(g, head2_guid);
    else PRINT_IF_GUID(g, my_guid);
    else
        printf("(GUID: unknown) ");
    printf("0x%08x, 0x%04x, 0x%04x, {", g->v1, g->v2, g->v3);
    for(i=0;i<8;i++)
        printf(" 0x%02x,", g->v4[i]);
    printf("}\n");
}
#undef PRINT_IF_GUID(g,cmp)
#endif

static void get_guid(ByteIOContext *s, GUID *g)
{
    int i;

    g->v1 = get_le32(s);
    g->v2 = get_le16(s);
    g->v3 = get_le16(s);
    for(i=0;i<8;i++)
        g->v4[i] = get_byte(s);
}

#if 0
static void get_str16(ByteIOContext *pb, char *buf, int buf_size)
{
    int len, c;
    char *q;

    len = get_le16(pb);
    q = buf;
    while (len > 0) {
        c = get_le16(pb);
        if ((q - buf) < buf_size - 1)
            *q++ = c;
        len--;
    }
    *q = '\0';
}
#endif

static void get_str16_nolen(ByteIOContext *pb, int len, char *buf, int buf_size)
{
    int c;
    char *q;

    q = buf;
    while (len > 0) {
        c = get_le16(pb);
        if ((q - buf) < buf_size - 1)
            *q++ = c;
        len-=2;
    }
    *q = '\0';
}

static int asf_probe(AVProbeData *pd)
{
    GUID g;
    const unsigned char *p;
    int i;

    /* check file header */
    if (pd->buf_size <= 32)
        return 0;
    p = pd->buf;
    g.v1 = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    p += 4;
    g.v2 = p[0] | (p[1] << 8);
    p += 2;
    g.v3 = p[0] | (p[1] << 8);
    p += 2;
    for(i=0;i<8;i++)
        g.v4[i] = *p++;

    if (!memcmp(&g, &asf_header, sizeof(GUID)))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int asf_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    ASFContext *asf = s->priv_data;
    GUID g;
    ByteIOContext *pb = &s->pb;
    AVStream *st;
    ASFStream *asf_st;
    int size, i;
    int64_t gsize;

    get_guid(pb, &g);
    if (memcmp(&g, &asf_header, sizeof(GUID)))
        goto fail;
    get_le64(pb);
    get_le32(pb);
    get_byte(pb);
    get_byte(pb);
    memset(&asf->asfid2avid, -1, sizeof(asf->asfid2avid));
    for(;;) {
        get_guid(pb, &g);
        gsize = get_le64(pb);
#ifdef DEBUG
        printf("%08Lx: ", url_ftell(pb) - 24);
        print_guid(&g);
        printf("  size=0x%Lx\n", gsize);
#endif
        if (gsize < 24)
            goto fail;
        if (!memcmp(&g, &file_header, sizeof(GUID))) {
            get_guid(pb, &asf->hdr.guid);
	    asf->hdr.file_size		= get_le64(pb);
	    asf->hdr.create_time	= get_le64(pb);
	    asf->hdr.packets_count	= get_le64(pb);
	    asf->hdr.play_time		= get_le64(pb);
	    asf->hdr.send_time		= get_le64(pb);
	    asf->hdr.preroll		= get_le32(pb);
	    asf->hdr.ignore		= get_le32(pb);
	    asf->hdr.flags		= get_le32(pb);
	    asf->hdr.min_pktsize	= get_le32(pb);
	    asf->hdr.max_pktsize	= get_le32(pb);
	    asf->hdr.max_bitrate	= get_le32(pb);
	    asf->packet_size = asf->hdr.max_pktsize;
            asf->nb_packets = asf->hdr.packets_count;
        } else if (!memcmp(&g, &stream_header, sizeof(GUID))) {
            int type, total_size, type_specific_size;
            unsigned int tag1;
            int64_t pos1, pos2;

            pos1 = url_ftell(pb);

            st = av_new_stream(s, 0);
            if (!st)
                goto fail;
            av_set_pts_info(st, 32, 1, 1000); /* 32 bit pts in ms */
            asf_st = av_mallocz(sizeof(ASFStream));
            if (!asf_st)
                goto fail;
            st->priv_data = asf_st;
            st->start_time = asf->hdr.preroll / (10000000 / AV_TIME_BASE);
	    st->duration = (asf->hdr.send_time - asf->hdr.preroll) / 
                (10000000 / AV_TIME_BASE);
            get_guid(pb, &g);
            if (!memcmp(&g, &audio_stream, sizeof(GUID))) {
                type = CODEC_TYPE_AUDIO;
            } else if (!memcmp(&g, &video_stream, sizeof(GUID))) {
                type = CODEC_TYPE_VIDEO;
            } else {
                goto fail;
            }
            get_guid(pb, &g);
            total_size = get_le64(pb);
            type_specific_size = get_le32(pb);
            get_le32(pb);
	    st->id = get_le16(pb) & 0x7f; /* stream id */
            // mapping of asf ID to AV stream ID;
            asf->asfid2avid[st->id] = s->nb_streams - 1;

            get_le32(pb);
	    st->codec.codec_type = type;
            /* 1 fps default (XXX: put 0 fps instead) */
            st->codec.frame_rate = 1; 
            st->codec.frame_rate_base = 1;
            if (type == CODEC_TYPE_AUDIO) {
                get_wav_header(pb, &st->codec, type_specific_size);
                st->need_parsing = 1;
		/* We have to init the frame size at some point .... */
		pos2 = url_ftell(pb);
		if (gsize > (pos2 + 8 - pos1 + 24)) {
		    asf_st->ds_span = get_byte(pb);
		    asf_st->ds_packet_size = get_le16(pb);
		    asf_st->ds_chunk_size = get_le16(pb);
		    asf_st->ds_data_size = get_le16(pb);
		    asf_st->ds_silence_data = get_byte(pb);
		}
		//printf("Descrambling: ps:%d cs:%d ds:%d s:%d  sd:%d\n",
		//       asf_st->ds_packet_size, asf_st->ds_chunk_size,
		//       asf_st->ds_data_size, asf_st->ds_span, asf_st->ds_silence_data);
		if (asf_st->ds_span > 1) {
		    if (!asf_st->ds_chunk_size
			|| (asf_st->ds_packet_size/asf_st->ds_chunk_size <= 1))
			asf_st->ds_span = 0; // disable descrambling
		}
                switch (st->codec.codec_id) {
                case CODEC_ID_MP3:
                    st->codec.frame_size = MPA_FRAME_SIZE;
                    break;
                case CODEC_ID_PCM_S16LE:
                case CODEC_ID_PCM_S16BE:
                case CODEC_ID_PCM_U16LE:
                case CODEC_ID_PCM_U16BE:
                case CODEC_ID_PCM_S8:
                case CODEC_ID_PCM_U8:
                case CODEC_ID_PCM_ALAW:
                case CODEC_ID_PCM_MULAW:
                    st->codec.frame_size = 1;
                    break;
                default:
                    /* This is probably wrong, but it prevents a crash later */
                    st->codec.frame_size = 1;
                    break;
                }
            } else {
		get_le32(pb);
                get_le32(pb);
                get_byte(pb);
                size = get_le16(pb); /* size */
                get_le32(pb); /* size */
                st->codec.width = get_le32(pb);
		st->codec.height = get_le32(pb);
                /* not available for asf */
                get_le16(pb); /* panes */
		st->codec.bits_per_sample = get_le16(pb); /* depth */
                tag1 = get_le32(pb);
		url_fskip(pb, 20);
		if (size > 40) {
		    st->codec.extradata_size = size - 40;
		    st->codec.extradata = av_mallocz(st->codec.extradata_size);
		    get_buffer(pb, st->codec.extradata, st->codec.extradata_size);
		}

        /* Extract palette from extradata if bpp <= 8 */
        /* This code assumes that extradata contains only palette */
        /* This is true for all paletted codecs implemented in ffmpeg */
        if (st->codec.extradata_size && (st->codec.bits_per_sample <= 8)) {
            st->codec.palctrl = av_mallocz(sizeof(AVPaletteControl));
#ifdef WORDS_BIGENDIAN
            for (i = 0; i < FFMIN(st->codec.extradata_size, AVPALETTE_SIZE)/4; i++)
                st->codec.palctrl->palette[i] = bswap_32(((uint32_t*)st->codec.extradata)[i]);
#else
            memcpy(st->codec.palctrl->palette, st->codec.extradata,
                   FFMIN(st->codec.extradata_size, AVPALETTE_SIZE));
#endif
            st->codec.palctrl->palette_changed = 1;
        }

                st->codec.codec_tag = tag1;
		st->codec.codec_id = codec_get_id(codec_bmp_tags, tag1);
            }
            pos2 = url_ftell(pb);
            url_fskip(pb, gsize - (pos2 - pos1 + 24));
        } else if (!memcmp(&g, &data_header, sizeof(GUID))) {
            break;
        } else if (!memcmp(&g, &comment_header, sizeof(GUID))) {
            int len1, len2, len3, len4, len5;

            len1 = get_le16(pb);
            len2 = get_le16(pb);
            len3 = get_le16(pb);
            len4 = get_le16(pb);
            len5 = get_le16(pb);
            get_str16_nolen(pb, len1, s->title, sizeof(s->title));
            get_str16_nolen(pb, len2, s->author, sizeof(s->author));
            get_str16_nolen(pb, len3, s->copyright, sizeof(s->copyright));
            get_str16_nolen(pb, len4, s->comment, sizeof(s->comment));
	    url_fskip(pb, len5);
       } else if (!memcmp(&g, &extended_content_header, sizeof(GUID))) {
                int desc_count, i;

                desc_count = get_le16(pb);
                for(i=0;i<desc_count;i++)
                {
                        int name_len,value_type,value_len,value_num = 0;
                        char *name, *value;

                        name_len = get_le16(pb);
                        name = (char *)av_mallocz(name_len);
                        get_str16_nolen(pb, name_len, name, name_len);
                        value_type = get_le16(pb);
                        value_len = get_le16(pb);
                        if ((value_type == 0) || (value_type == 1)) // unicode or byte
                        {
                                value = (char *)av_mallocz(value_len);
                                get_str16_nolen(pb, value_len, value, value_len);
                                if (strcmp(name,"WM/AlbumTitle")==0) { strcpy(s->album, value); }
                                av_free(value);
                        }
                        if ((value_type >= 2) || (value_type <= 5)) // boolean or DWORD or QWORD or WORD
                        {
                                if (value_type==2) value_num = get_le32(pb);
                                if (value_type==3) value_num = get_le32(pb);
                                if (value_type==4) value_num = get_le64(pb);
                                if (value_type==5) value_num = get_le16(pb);
                                if (strcmp(name,"WM/Track")==0) s->track = value_num + 1;
                                if (strcmp(name,"WM/TrackNumber")==0) s->track = value_num;
                        }
                        av_free(name);
                }
#if 0
        } else if (!memcmp(&g, &head1_guid, sizeof(GUID))) {
            int v1, v2;
            get_guid(pb, &g);
            v1 = get_le32(pb);
            v2 = get_le16(pb);
        } else if (!memcmp(&g, &codec_comment_header, sizeof(GUID))) {
            int len, v1, n, num;
            char str[256], *q;
            char tag[16];

            get_guid(pb, &g);
            print_guid(&g);

            n = get_le32(pb);
            for(i=0;i<n;i++) {
                num = get_le16(pb); /* stream number */
                get_str16(pb, str, sizeof(str));
                get_str16(pb, str, sizeof(str));
                len = get_le16(pb);
                q = tag;
                while (len > 0) {
                    v1 = get_byte(pb);
                    if ((q - tag) < sizeof(tag) - 1)
                        *q++ = v1;
                    len--;
                }
                *q = '\0';
            }
#endif
        } else if (url_feof(pb)) {
            goto fail;
        } else {
            url_fseek(pb, gsize - 24, SEEK_CUR);
        }
    }
    get_guid(pb, &g);
    get_le64(pb);
    get_byte(pb);
    get_byte(pb);
    if (url_feof(pb))
        goto fail;
    asf->data_offset = url_ftell(pb);
    asf->packet_size_left = 0;

    return 0;

 fail:
     for(i=0;i<s->nb_streams;i++) {
        AVStream *st = s->streams[i];
	if (st) {
	    av_free(st->priv_data);
            av_free(st->codec.extradata);
	}
        av_free(st);
    }
    return -1;
}

#define DO_2BITS(bits, var, defval) \
    switch (bits & 3) \
    { \
    case 3: var = get_le32(pb); rsize += 4; break; \
    case 2: var = get_le16(pb); rsize += 2; break; \
    case 1: var = get_byte(pb); rsize++; break; \
    default: var = defval; break; \
    }

static int asf_get_packet(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;
    ByteIOContext *pb = &s->pb;
    uint32_t packet_length, padsize;
    int rsize = 9;
    int c;
    
    if((url_ftell(&s->pb) - s->data_offset) % asf->packet_size)
        return -1;
    assert((url_ftell(&s->pb) - s->data_offset) % asf->packet_size == 0);
    
    c = get_byte(pb);
    if (c != 0x82) {
        if (!url_feof(pb))
	    av_log(s, AV_LOG_ERROR, "ff asf bad header %x  at:%lld\n", c, url_ftell(pb));
    }
    if ((c & 0x0f) == 2) { // always true for now
	if (get_le16(pb) != 0) {
            if (!url_feof(pb))
		av_log(s, AV_LOG_ERROR, "ff asf bad non zero\n");
	    return AVERROR_IO;
	}
        rsize+=2;
/*    }else{
        if (!url_feof(pb))
	    printf("ff asf bad header %x  at:%lld\n", c, url_ftell(pb));
	return AVERROR_IO;*/
    }

    asf->packet_flags = get_byte(pb);
    asf->packet_property = get_byte(pb);

    DO_2BITS(asf->packet_flags >> 5, packet_length, asf->packet_size);
    DO_2BITS(asf->packet_flags >> 1, padsize, 0); // sequence ignored
    DO_2BITS(asf->packet_flags >> 3, padsize, 0); // padding length

    asf->packet_timestamp = get_le32(pb);
    get_le16(pb); /* duration */
    // rsize has at least 11 bytes which have to be present

    if (asf->packet_flags & 0x01) {
	asf->packet_segsizetype = get_byte(pb); rsize++;
        asf->packet_segments = asf->packet_segsizetype & 0x3f;
    } else {
	asf->packet_segments = 1;
        asf->packet_segsizetype = 0x80;
    }
    asf->packet_size_left = packet_length - padsize - rsize;
    if (packet_length < asf->hdr.min_pktsize)
        padsize += asf->hdr.min_pktsize - packet_length;
    asf->packet_padsize = padsize;
#ifdef DEBUG
    printf("packet: size=%d padsize=%d  left=%d\n", asf->packet_size, asf->packet_padsize, asf->packet_size_left);
#endif
    return 0;
}

static int asf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ASFContext *asf = s->priv_data;
    ASFStream *asf_st = 0;
    ByteIOContext *pb = &s->pb;
    //static int pc = 0;
    for (;;) {
	int rsize = 0;
	if (asf->packet_size_left < FRAME_HEADER_SIZE
	    || asf->packet_segments < 1) {
	    //asf->packet_size_left <= asf->packet_padsize) {
	    int ret = asf->packet_size_left + asf->packet_padsize;
	    //printf("PacketLeftSize:%d  Pad:%d Pos:%Ld\n", asf->packet_size_left, asf->packet_padsize, url_ftell(pb));
	    /* fail safe */
	    url_fskip(pb, ret);
            asf->packet_pos= url_ftell(&s->pb);
	    ret = asf_get_packet(s);
	    //printf("READ ASF PACKET  %d   r:%d   c:%d\n", ret, asf->packet_size_left, pc++);
	    if (ret < 0 || url_feof(pb))
		return AVERROR_IO;
            asf->packet_time_start = 0;
            continue;
	}
	if (asf->packet_time_start == 0) {
	    /* read frame header */
            int num = get_byte(pb);
	    asf->packet_segments--;
	    rsize++;
	    asf->packet_key_frame = (num & 0x80) >> 7;
	    asf->stream_index = asf->asfid2avid[num & 0x7f];
	    // sequence should be ignored!
	    DO_2BITS(asf->packet_property >> 4, asf->packet_seq, 0);
	    DO_2BITS(asf->packet_property >> 2, asf->packet_frag_offset, 0);
	    DO_2BITS(asf->packet_property, asf->packet_replic_size, 0);
//printf("key:%d stream:%d seq:%d offset:%d replic_size:%d\n", asf->packet_key_frame, asf->stream_index, asf->packet_seq, //asf->packet_frag_offset, asf->packet_replic_size);
	    if (asf->packet_replic_size > 1) {
                assert(asf->packet_replic_size >= 8);
                // it should be always at least 8 bytes - FIXME validate
		asf->packet_obj_size = get_le32(pb);
		asf->packet_frag_timestamp = get_le32(pb); // timestamp
		if (asf->packet_replic_size > 8)
		    url_fskip(pb, asf->packet_replic_size - 8);
		rsize += asf->packet_replic_size; // FIXME - check validity
	    } else if (asf->packet_replic_size==1){
		// multipacket - frag_offset is begining timestamp
		asf->packet_time_start = asf->packet_frag_offset;
                asf->packet_frag_offset = 0;
		asf->packet_frag_timestamp = asf->packet_timestamp;

                asf->packet_time_delta = get_byte(pb);
		rsize++;
	    }else{
                assert(asf->packet_replic_size==0);
            }
	    if (asf->packet_flags & 0x01) {
		DO_2BITS(asf->packet_segsizetype >> 6, asf->packet_frag_size, 0); // 0 is illegal
#undef DO_2BITS
		//printf("Fragsize %d\n", asf->packet_frag_size);
	    } else {
		asf->packet_frag_size = asf->packet_size_left - rsize;
		//printf("Using rest  %d %d %d\n", asf->packet_frag_size, asf->packet_size_left, rsize);
	    }
	    if (asf->packet_replic_size == 1) {
		asf->packet_multi_size = asf->packet_frag_size;
		if (asf->packet_multi_size > asf->packet_size_left) {
		    asf->packet_segments = 0;
                    continue;
		}
	    }
	    asf->packet_size_left -= rsize;
	    //printf("___objsize____  %d   %d    rs:%d\n", asf->packet_obj_size, asf->packet_frag_offset, rsize);

	    if (asf->stream_index < 0) {
                asf->packet_time_start = 0;
		/* unhandled packet (should not happen) */
		url_fskip(pb, asf->packet_frag_size);
		asf->packet_size_left -= asf->packet_frag_size;
		av_log(s, AV_LOG_ERROR, "ff asf skip %d  %d\n", asf->packet_frag_size, num & 0x7f);
                continue;
	    }
	    asf->asf_st = s->streams[asf->stream_index]->priv_data;
	}
	asf_st = asf->asf_st;

	if ((asf->packet_frag_offset != asf_st->frag_offset
	     || (asf->packet_frag_offset
		 && asf->packet_seq != asf_st->seq)) // seq should be ignored
	   ) {
	    /* cannot continue current packet: free it */
	    // FIXME better check if packet was already allocated
	    av_log(s, AV_LOG_INFO, "ff asf parser skips: %d - %d     o:%d - %d    %d %d   fl:%d\n",
		   asf_st->pkt.size,
		   asf->packet_obj_size,
		   asf->packet_frag_offset, asf_st->frag_offset,
		   asf->packet_seq, asf_st->seq, asf->packet_frag_size);
	    if (asf_st->pkt.size)
		av_free_packet(&asf_st->pkt);
	    asf_st->frag_offset = 0;
	    if (asf->packet_frag_offset != 0) {
		url_fskip(pb, asf->packet_frag_size);
		av_log(s, AV_LOG_INFO, "ff asf parser skiping %db\n", asf->packet_frag_size);
		asf->packet_size_left -= asf->packet_frag_size;
		continue;
	    }
	}
	if (asf->packet_replic_size == 1) {
	    // frag_offset is here used as the begining timestamp
	    asf->packet_frag_timestamp = asf->packet_time_start;
	    asf->packet_time_start += asf->packet_time_delta;
	    asf->packet_obj_size = asf->packet_frag_size = get_byte(pb);
	    asf->packet_size_left--;
            asf->packet_multi_size--;
	    if (asf->packet_multi_size < asf->packet_obj_size)
	    {
		asf->packet_time_start = 0;
		url_fskip(pb, asf->packet_multi_size);
		asf->packet_size_left -= asf->packet_multi_size;
                continue;
	    }
	    asf->packet_multi_size -= asf->packet_obj_size;
	    //printf("COMPRESS size  %d  %d  %d   ms:%d\n", asf->packet_obj_size, asf->packet_frag_timestamp, asf->packet_size_left, asf->packet_multi_size);
	}
	if (asf_st->frag_offset == 0) {
	    /* new packet */
	    av_new_packet(&asf_st->pkt, asf->packet_obj_size);
	    asf_st->seq = asf->packet_seq;
	    asf_st->pkt.pts = asf->packet_frag_timestamp - asf->hdr.preroll;
	    asf_st->pkt.stream_index = asf->stream_index;
            asf_st->packet_pos= asf->packet_pos;            
//printf("new packet: stream:%d key:%d packet_key:%d audio:%d size:%d\n", 
//asf->stream_index, asf->packet_key_frame, asf_st->pkt.flags & PKT_FLAG_KEY,
//s->streams[asf->stream_index]->codec.codec_type == CODEC_TYPE_AUDIO, asf->packet_obj_size);
	    if (s->streams[asf->stream_index]->codec.codec_type == CODEC_TYPE_AUDIO) 
		asf->packet_key_frame = 1;
	    if (asf->packet_key_frame)
		asf_st->pkt.flags |= PKT_FLAG_KEY;
	}

	/* read data */
	//printf("READ PACKET s:%d  os:%d  o:%d,%d  l:%d   DATA:%p\n",
	//       asf->packet_size, asf_st->pkt.size, asf->packet_frag_offset,
	//       asf_st->frag_offset, asf->packet_frag_size, asf_st->pkt.data);
	asf->packet_size_left -= asf->packet_frag_size;
	if (asf->packet_size_left < 0)
            continue;
	get_buffer(pb, asf_st->pkt.data + asf->packet_frag_offset,
		   asf->packet_frag_size);
	asf_st->frag_offset += asf->packet_frag_size;
	/* test if whole packet is read */
	if (asf_st->frag_offset == asf_st->pkt.size) {
	    /* return packet */
	    if (asf_st->ds_span > 1) {
		/* packet descrambling */
		char* newdata = av_malloc(asf_st->pkt.size);
		if (newdata) {
		    int offset = 0;
		    while (offset < asf_st->pkt.size) {
			int off = offset / asf_st->ds_chunk_size;
			int row = off / asf_st->ds_span;
			int col = off % asf_st->ds_span;
			int idx = row + col * asf_st->ds_packet_size / asf_st->ds_chunk_size;
			//printf("off:%d  row:%d  col:%d  idx:%d\n", off, row, col, idx);
			memcpy(newdata + offset,
			       asf_st->pkt.data + idx * asf_st->ds_chunk_size,
			       asf_st->ds_chunk_size);
			offset += asf_st->ds_chunk_size;
		    }
		    av_free(asf_st->pkt.data);
		    asf_st->pkt.data = newdata;
		}
	    }
	    asf_st->frag_offset = 0;
	    memcpy(pkt, &asf_st->pkt, sizeof(AVPacket));
	    //printf("packet %d %d\n", asf_st->pkt.size, asf->packet_frag_size);
	    asf_st->pkt.size = 0;
	    asf_st->pkt.data = 0;
	    break; // packet completed
	}
    }
    return 0;
}

static int asf_read_close(AVFormatContext *s)
{
    int i;

    for(i=0;i<s->nb_streams;i++) {
	AVStream *st = s->streams[i];
	av_free(st->priv_data);
	av_free(st->codec.extradata);
    av_free(st->codec.palctrl);
    }
    return 0;
}

// Added to support seeking after packets have been read
// If information is not reset, read_packet fails due to
// leftover information from previous reads
static void asf_reset_header(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;
    ASFStream *asf_st;
    int i;

    asf->packet_nb_frames = 0;
    asf->packet_timestamp_start = -1;
    asf->packet_timestamp_end = -1;
    asf->packet_size_left = 0;
    asf->packet_segments = 0;
    asf->packet_flags = 0;
    asf->packet_property = 0;
    asf->packet_timestamp = 0;
    asf->packet_segsizetype = 0;
    asf->packet_segments = 0;
    asf->packet_seq = 0;
    asf->packet_replic_size = 0;
    asf->packet_key_frame = 0;
    asf->packet_padsize = 0;
    asf->packet_frag_offset = 0;
    asf->packet_frag_size = 0;
    asf->packet_frag_timestamp = 0;
    asf->packet_multi_size = 0;
    asf->packet_obj_size = 0;
    asf->packet_time_delta = 0;
    asf->packet_time_start = 0;
    
    for(i=0; i<s->nb_streams; i++){
        asf_st= s->streams[i]->priv_data;
        av_free_packet(&asf_st->pkt);
        asf_st->frag_offset=0;
        asf_st->seq=0;
    }
    asf->asf_st= NULL;
}

static int64_t asf_read_pts(AVFormatContext *s, int stream_index, int64_t *ppos, int64_t pos_limit)
{
    ASFContext *asf = s->priv_data;
    AVPacket pkt1, *pkt = &pkt1;
    ASFStream *asf_st;
    int64_t pts;
    int64_t pos= *ppos;
    int i;
    int64_t start_pos[s->nb_streams];
    
    for(i=0; i<s->nb_streams; i++){
        start_pos[i]= pos;
    }
    
    pos= (pos+asf->packet_size-1-s->data_offset)/asf->packet_size*asf->packet_size+ s->data_offset;
    *ppos= pos;
    url_fseek(&s->pb, pos, SEEK_SET);
    
//printf("asf_read_pts\n");
    asf_reset_header(s);
    for(;;){
        if (av_read_frame(s, pkt) < 0){
            av_log(s, AV_LOG_INFO, "seek failed\n");
    	    return AV_NOPTS_VALUE;
        }
        
        pts= pkt->pts * 1000 / AV_TIME_BASE;

        av_free_packet(pkt);
        if(pkt->flags&PKT_FLAG_KEY){
            i= pkt->stream_index;

            asf_st= s->streams[i]->priv_data;

            assert((asf_st->packet_pos - s->data_offset) % asf->packet_size == 0);
            pos= asf_st->packet_pos;

            av_add_index_entry(s->streams[i], pos, pts, pos - start_pos[i] + 1, AVINDEX_KEYFRAME);
            start_pos[i]= asf_st->packet_pos + 1;
            
            if(pkt->stream_index == stream_index)
               break;
        }
    }

    *ppos= pos;
//printf("found keyframe at %Ld stream %d stamp:%Ld\n", *ppos, stream_index, pts);

    return pts;
}

static int asf_read_seek(AVFormatContext *s, int stream_index, int64_t pts)
{
    ASFContext *asf = s->priv_data;
    
    if (asf->packet_size <= 0)
        return -1;

    if(av_seek_frame_binary(s, stream_index, pts)<0)
        return -1;

    asf_reset_header(s);
    return 0;
}

static AVInputFormat asf_iformat = {
    "asf",
    "asf format",
    sizeof(ASFContext),
    asf_probe,
    asf_read_header,
    asf_read_packet,
    asf_read_close,
    asf_read_seek,
    asf_read_pts,
};

#ifdef CONFIG_ENCODERS
    extern AVOutputFormat asf_oformat;
    extern AVOutputFormat asf_stream_oformat;
#endif //CONFIG_ENCODERS

int asf_init(void)
{
    av_register_input_format(&asf_iformat);
#ifdef CONFIG_ENCODERS
    av_register_output_format(&asf_oformat);
    av_register_output_format(&asf_stream_oformat);
#endif //CONFIG_ENCODERS
    return 0;
}
