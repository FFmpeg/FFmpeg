/*
 * FFmpeg main 
 * Copyright (c) 2000,2001 Gerard Lantau
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#define HAVE_AV_CONFIG_H
#include "avformat.h"

#ifndef CONFIG_WIN32
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <sys/resource.h>
#include <ctype.h>
#endif


#define MAXINT64 INT64_C(0x7fffffffffffffff)

typedef struct {
    const char *name;
    int flags;
#define HAS_ARG    0x0001
#define OPT_BOOL   0x0002
#define OPT_EXPERT 0x0004
#define OPT_STRING 0x0008
    union {
        void (*func_arg)();
        int *int_arg;
        char **str_arg;
    } u;
    const char *help;
    const char *argname;
} OptionDef;

/* select an input stream for an output stream */
typedef struct AVStreamMap {
    int file_index;
    int stream_index;
} AVStreamMap;

extern const OptionDef options[];

void show_help(void);

#define MAX_FILES 20

static AVFormatContext *input_files[MAX_FILES];
static int nb_input_files = 0;

static AVFormatContext *output_files[MAX_FILES];
static int nb_output_files = 0;

static AVStreamMap stream_maps[MAX_FILES];
static int nb_stream_maps;

static AVFormat *file_format;
static int frame_width  = 160;
static int frame_height = 128;
static int frame_rate = 25 * FRAME_RATE_BASE;
static int video_bit_rate = 200000;
static int video_bit_rate_tolerance = 200000;
static int video_qscale = 0;
static int video_qmin = 3;
static int video_qmax = 15;
static int video_qdiff = 3;
static float video_qblur = 0.5;
static float video_qcomp = 0.5;
static int me_method = 0;
static int video_disable = 0;
static int video_codec_id = CODEC_ID_NONE;
static int same_quality = 0;
static int b_frames = 0;
static int use_hq = 0;
static int use_4mv = 0;
static int do_deinterlace = 0;

static int gop_size = 12;
static int intra_only = 0;
static int audio_sample_rate = 44100;
static int audio_bit_rate = 64000;
static int audio_disable = 0;
static int audio_channels = 1;
static int audio_codec_id = CODEC_ID_NONE;

static INT64 recording_time = 0;
static int file_overwrite = 0;
static char *str_title = NULL;
static char *str_author = NULL;
static char *str_copyright = NULL;
static char *str_comment = NULL;
static int do_benchmark = 0;
static int do_hex_dump = 0;
static int do_play = 0;
static int do_psnr = 0;
static int do_vstats = 0;

typedef struct AVOutputStream {
    int file_index;          /* file index */
    int index;               /* stream index in the output file */
    int source_index;        /* AVInputStream index */
    AVStream *st;            /* stream in the output file */
    int encoding_needed;   /* true if encoding needed for this stream */

    int fifo_packet_rptr;    /* read index in the corresponding
                                avinputstream packet fifo */
    /* video only */
    AVPicture pict_tmp;         /* temporary image for resizing */
    int video_resample;
    ImgReSampleContext *img_resample_ctx; /* for image resampling */
    
    /* audio only */
    int audio_resample;
    ReSampleContext *resample; /* for audio resampling */
    FifoBuffer fifo;     /* for compression: one audio fifo per codec */
} AVOutputStream;

typedef struct AVInputStream {
    int file_index;
    int index;
    AVStream *st;
    int discard;             /* true if stream data should be discarded */
    int decoding_needed;     /* true if the packets must be decoded in 'raw_fifo' */
    INT64 pts;               /* current pts */
    int frame_number;        /* current frame */
    INT64 sample_index;      /* current sample */
} AVInputStream;

typedef struct AVInputFile {
    int eof_reached;      /* true if eof reached */
    int ist_index;        /* index of first stream in ist_table */
    int buffer_size;      /* current total buffer size */
    int buffer_size_max;  /* buffer size at which we consider we can stop
                             buffering */
} AVInputFile;

#ifndef CONFIG_WIN32

/* init terminal so that we can grab keys */
static struct termios oldtty;

static void term_exit(void)
{
    tcsetattr (0, TCSANOW, &oldtty);
}

static void term_init(void)
{
    struct termios tty;

    tcgetattr (0, &tty);
    oldtty = tty;

    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                          |INLCR|IGNCR|ICRNL|IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN);
    tty.c_cflag &= ~(CSIZE|PARENB);
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    
    tcsetattr (0, TCSANOW, &tty);

    atexit(term_exit);
}

/* read a key without blocking */
static int read_key(void)
{
    struct timeval tv;
    int n;
    unsigned char ch;
    fd_set rfds;

    FD_ZERO(&rfds);
    FD_SET(0, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    n = select(1, &rfds, NULL, NULL, &tv);
    if (n > 0) {
        if (read(0, &ch, 1) == 1)
            return ch;
    }
    return -1;
}

#else

/* no interactive support */
static void term_exit(void)
{
}

static void term_init(void)
{
}

static int read_key(void)
{
    return -1;
}

#endif

int read_ffserver_streams(AVFormatContext *s, const char *filename)
{
    int i;
    AVFormatContext *ic;

    ic = av_open_input_file(filename, NULL, FFM_PACKET_SIZE, NULL);
    if (!ic)
        return -EIO;
    /* copy stream format */
    s->nb_streams = ic->nb_streams;
    for(i=0;i<ic->nb_streams;i++) {
        AVStream *st;
        st = av_mallocz(sizeof(AVFormatContext));
        memcpy(st, ic->streams[i], sizeof(AVStream));
        s->streams[i] = st;
    }

    av_close_input_file(ic);
    return 0;
}

#define MAX_AUDIO_PACKET_SIZE 16384

static void do_audio_out(AVFormatContext *s, 
                         AVOutputStream *ost, 
                         AVInputStream *ist,
                         unsigned char *buf, int size)
{
    UINT8 *buftmp;
    UINT8 audio_buf[2*MAX_AUDIO_PACKET_SIZE]; /* XXX: allocate it */
    UINT8 audio_out[MAX_AUDIO_PACKET_SIZE]; /* XXX: allocate it */
    int size_out, frame_bytes, ret;
    AVCodecContext *enc;

    enc = &ost->st->codec;

    if (ost->audio_resample) {
        buftmp = audio_buf;
        size_out = audio_resample(ost->resample, 
                                  (short *)buftmp, (short *)buf,
                                  size / (ist->st->codec.channels * 2));
        size_out = size_out * enc->channels * 2;
    } else {
        buftmp = buf;
        size_out = size;
    }

    /* now encode as many frames as possible */
    if (enc->frame_size > 1) {
        /* output resampled raw samples */
        fifo_write(&ost->fifo, buftmp, size_out, 
                   &ost->fifo.wptr);

        frame_bytes = enc->frame_size * 2 * enc->channels;
        
        while (fifo_read(&ost->fifo, audio_buf, frame_bytes, 
                     &ost->fifo.rptr) == 0) {
            ret = avcodec_encode_audio(enc, audio_out, sizeof(audio_out), 
                                       (short *)audio_buf);
            s->format->write_packet(s, ost->index, audio_out, ret, 0);
        }
    } else {
        /* output a pcm frame */
        /* XXX: change encoding codec API to avoid this ? */
        switch(enc->codec->id) {
        case CODEC_ID_PCM_S16LE:
        case CODEC_ID_PCM_S16BE:
        case CODEC_ID_PCM_U16LE:
        case CODEC_ID_PCM_U16BE:
            break;
        default:
            size_out = size_out >> 1;
            break;
        }
        ret = avcodec_encode_audio(enc, audio_out, size_out, 
                                   (short *)buftmp);
        s->format->write_packet(s, ost->index, audio_out, ret, 0);
    }
}

/* write a picture to a raw mux */
static void write_picture(AVFormatContext *s, int index, AVPicture *picture, 
                          int pix_fmt, int w, int h)
{
    UINT8 *buf, *src, *dest;
    int size, j, i;

    /* XXX: not efficient, should add test if we can take
       directly the AVPicture */
    switch(pix_fmt) {
    case PIX_FMT_YUV420P:
        size = avpicture_get_size(pix_fmt, w, h);
        buf = malloc(size);
        if (!buf)
            return;
        dest = buf;
        for(i=0;i<3;i++) {
            if (i == 1) {
                w >>= 1;
                h >>= 1;
            }
            src = picture->data[i];
            for(j=0;j<h;j++) {
                memcpy(dest, src, w);
                dest += w;
                src += picture->linesize[i];
            }
        }
        break;
    case PIX_FMT_YUV422P:
        size = (w * h) * 2; 
        buf = malloc(size);
        if (!buf)
            return;
        dest = buf;
        for(i=0;i<3;i++) {
            if (i == 1) {
                w >>= 1;
            }
            src = picture->data[i];
            for(j=0;j<h;j++) {
                memcpy(dest, src, w);
                dest += w;
                src += picture->linesize[i];
            }
        }
        break;
    case PIX_FMT_YUV444P:
        size = (w * h) * 3; 
        buf = malloc(size);
        if (!buf)
            return;
        dest = buf;
        for(i=0;i<3;i++) {
            src = picture->data[i];
            for(j=0;j<h;j++) {
                memcpy(dest, src, w);
                dest += w;
                src += picture->linesize[i];
            }
        }
        break;
    case PIX_FMT_YUV422:
        size = (w * h) * 2; 
        buf = malloc(size);
        if (!buf)
            return;
        dest = buf;
        src = picture->data[0];
        for(j=0;j<h;j++) {
            memcpy(dest, src, w * 2);
            dest += w * 2;
            src += picture->linesize[0];
        }
        break;
    case PIX_FMT_RGB24:
    case PIX_FMT_BGR24:
        size = (w * h) * 3; 
        buf = malloc(size);
        if (!buf)
            return;
        dest = buf;
        src = picture->data[0];
        for(j=0;j<h;j++) {
            memcpy(dest, src, w * 3);
            dest += w * 3;
            src += picture->linesize[0];
        }
        break;
    default:
        return;
    }
    s->format->write_packet(s, index, buf, size, 0);
    free(buf);
}


static void do_video_out(AVFormatContext *s, 
                         AVOutputStream *ost, 
                         AVInputStream *ist,
                         AVPicture *picture1,
                         int *frame_size)
{
    int n1, n2, nb, i, ret, frame_number;
    AVPicture *picture, *picture2, *pict;
    AVPicture picture_tmp1, picture_tmp2;
    UINT8 video_buffer[1024*1024];
    UINT8 *buf = NULL, *buf1 = NULL;
    AVCodecContext *enc, *dec;

    enc = &ost->st->codec;
    dec = &ist->st->codec;

    frame_number = ist->frame_number;
    /* first drop frame if needed */
    n1 = ((INT64)frame_number * enc->frame_rate) / dec->frame_rate;
    n2 = (((INT64)frame_number + 1) * enc->frame_rate) / dec->frame_rate;
    nb = n2 - n1;
    if (nb <= 0) 
        return;

    /* deinterlace : must be done before any resize */
    if (do_deinterlace) {
        int size;

        /* create temporary picture */
        size = avpicture_get_size(dec->pix_fmt, dec->width, dec->height);
        buf1 = malloc(size);
        if (!buf1)
            return;
        
        picture2 = &picture_tmp2;
        avpicture_fill(picture2, buf1, dec->pix_fmt, dec->width, dec->height);

        if (avpicture_deinterlace(picture2, picture1, 
                                  dec->pix_fmt, dec->width, dec->height) < 0) {
            /* if error, do not deinterlace */
            free(buf1);
            buf1 = NULL;
            picture2 = picture1;
        }
    } else {
        picture2 = picture1;
    }

    /* convert pixel format if needed */
    if (enc->pix_fmt != dec->pix_fmt) {
        int size;

        /* create temporary picture */
        size = avpicture_get_size(enc->pix_fmt, dec->width, dec->height);
        buf = malloc(size);
        if (!buf)
            return;
        pict = &picture_tmp1;
        avpicture_fill(pict, buf, enc->pix_fmt, dec->width, dec->height);
        
        if (img_convert(pict, enc->pix_fmt, 
                        picture2, dec->pix_fmt, 
                        dec->width, dec->height) < 0) {
            fprintf(stderr, "pixel format conversion not handled\n");
            goto the_end;
        }
    } else {
        pict = picture2;
    }

    /* XXX: resampling could be done before raw format convertion in
       some cases to go faster */
    /* XXX: only works for YUV420P */
    if (ost->video_resample) {
        picture = &ost->pict_tmp;
        img_resample(ost->img_resample_ctx, picture, pict);
    } else {
        picture = pict;
    }

    /* duplicates frame if needed */
    /* XXX: pb because no interleaving */
    for(i=0;i<nb;i++) {
        if (enc->codec_id != CODEC_ID_RAWVIDEO) {
            /* handles sameq here. This is not correct because it may
               not be a global option */
            if (same_quality) {
                enc->quality = dec->quality;
            }
            ret = avcodec_encode_video(enc, 
                                       video_buffer, sizeof(video_buffer), 
                                       picture);
            s->format->write_packet(s, ost->index, video_buffer, ret, 0);
            *frame_size = ret;
        } else {
            write_picture(s, ost->index, picture, enc->pix_fmt, enc->width, enc->height);
        }
    }
    the_end:
    if (buf)
        free(buf);
    if (buf1)
        free(buf1);
}

static void do_video_stats(AVOutputStream *ost, 
                         AVInputStream *ist,
                         int frame_size)
{
    static FILE *fvstats=NULL;
    static INT64 total_size = 0;
    struct tm *today;
    time_t today2;
    char filename[40];
    AVCodecContext *enc;
    int frame_number;
    INT64 ti;
    double ti1, bitrate, avg_bitrate;
    
    if (!fvstats) {
        today2 = time(NULL);
        today = localtime(&today2);
        sprintf(filename, "vstats_%02d%02d%02d.log", today->tm_hour,
                                               today->tm_min,
                                               today->tm_sec);
        fvstats = fopen(filename,"w");
        if (!fvstats) {
            perror("fopen");
            exit(1);
        }
    }
    
    ti = MAXINT64;
    enc = &ost->st->codec;
    total_size += frame_size;
    if (enc->codec_type == CODEC_TYPE_VIDEO) {
        frame_number = ist->frame_number;
        fprintf(fvstats, "frame= %5d q= %2d ", frame_number, enc->quality);
        if (do_psnr)
            fprintf(fvstats, "PSNR= %6.2f ", enc->psnr_y);
        
        fprintf(fvstats,"f_size= %6d ", frame_size);
        /* compute min pts value */
        if (!ist->discard && ist->pts < ti) {
            ti = ist->pts;
        }
        ti1 = (double)ti / 1000000.0;
        if (ti1 < 0.01)
            ti1 = 0.01;
    
        bitrate = (double)(frame_size * 8) * enc->frame_rate / FRAME_RATE_BASE / 1000.0;
        avg_bitrate = (double)(total_size * 8) / ti1 / 1000.0;
        fprintf(fvstats, "s_size= %8.0fkB time= %0.3f br= %7.1fkbits/s avg_br= %7.1fkbits/s ",
            (double)total_size / 1024, ti1, bitrate, avg_bitrate);
        fprintf(fvstats,"type= %s\n", enc->key_frame == 1 ? "I" : "P");        
    }

    
    
}

static void hex_dump(UINT8 *buf, int size)
{
    int len, i, j, c;

    for(i=0;i<size;i+=16) {
        len = size - i;
        if (len > 16)
            len = 16;
        printf("%08x ", i);
        for(j=0;j<16;j++) {
            if (j < len)
                printf(" %02x", buf[i+j]);
            else
                printf("   ");
        }
        printf(" ");
        for(j=0;j<len;j++) {
            c = buf[i+j];
            if (c < ' ' || c > '~')
                c = '.';
            printf("%c", c);
        }
        printf("\n");
    }
}

/*
 * The following code is the main loop of the file converter
 */
static int av_encode(AVFormatContext **output_files,
                     int nb_output_files,
                     AVFormatContext **input_files,
                     int nb_input_files,
                     AVStreamMap *stream_maps, int nb_stream_maps)
{
    int ret, i, j, k, n, nb_istreams = 0, nb_ostreams = 0;
    AVFormatContext *is, *os;
    AVCodecContext *codec, *icodec;
    AVOutputStream *ost, **ost_table = NULL;
    AVInputStream *ist, **ist_table = NULL;
    INT64 min_pts, start_time;
    AVInputFile *file_table;

    file_table= (AVInputFile*) malloc(nb_input_files * sizeof(AVInputFile));
    if (!file_table)
        goto fail;

    memset(file_table, 0, sizeof(file_table));
    
    /* input stream init */
    j = 0;
    for(i=0;i<nb_input_files;i++) {
        is = input_files[i];
        file_table[i].ist_index = j;
        j += is->nb_streams;
    }
    nb_istreams = j;

    ist_table = av_mallocz(nb_istreams * sizeof(AVInputStream *));
    if (!ist_table)
        goto fail;
    
    for(i=0;i<nb_istreams;i++) {
        ist = av_mallocz(sizeof(AVInputStream));
        if (!ist)
            goto fail;
        ist_table[i] = ist;
    }
    j = 0;
    for(i=0;i<nb_input_files;i++) {
        is = input_files[i];
        for(k=0;k<is->nb_streams;k++) {
            ist = ist_table[j++];
            ist->st = is->streams[k];
            ist->file_index = i;
            ist->index = k;
            ist->discard = 1; /* the stream is discarded by default
                                 (changed later) */
        }
    }

    /* output stream init */
    nb_ostreams = 0;
    for(i=0;i<nb_output_files;i++) {
        os = output_files[i];
        nb_ostreams += os->nb_streams;
    }
    if (nb_stream_maps > 0 && nb_stream_maps != nb_ostreams) {
        fprintf(stderr, "Number of stream maps must match number of output streams\n");
        exit(1);
    }

    ost_table = av_mallocz(sizeof(AVOutputStream *) * nb_ostreams);
    if (!ost_table)
        goto fail;
    for(i=0;i<nb_ostreams;i++) {
        ost = av_mallocz(sizeof(AVOutputStream));
        if (!ost)
            goto fail;
        ost_table[i] = ost;
    }
    
    n = 0;
    for(k=0;k<nb_output_files;k++) {
        os = output_files[k];
        for(i=0;i<os->nb_streams;i++) {
            int found;
            ost = ost_table[n++];
            ost->file_index = k;
            ost->index = i;
            ost->st = os->streams[i];
            if (nb_stream_maps > 0) {
                ost->source_index = file_table[stream_maps[n-1].file_index].ist_index + 
                    stream_maps[n-1].stream_index;
            } else {
                /* get corresponding input stream index : we select the first one with the right type */
                found = 0;
                for(j=0;j<nb_istreams;j++) {
                    ist = ist_table[j];
                    if (ist->discard && 
                        ist->st->codec.codec_type == ost->st->codec.codec_type) {
                        ost->source_index = j;
                        found = 1;
                    }
                }
                
                if (!found) {
                    /* try again and reuse existing stream */
                    for(j=0;j<nb_istreams;j++) {
                        ist = ist_table[j];
                        if (ist->st->codec.codec_type == ost->st->codec.codec_type) {
                            ost->source_index = j;
                            found = 1;
                        }
                    }
                    if (!found) {
                        fprintf(stderr, "Could not find input stream matching output stream #%d.%d\n",
                                ost->file_index, ost->index);
                        exit(1);
                    }
                }
            }
            ist = ist_table[ost->source_index];
            ist->discard = 0;
        }
    }

    /* dump the stream mapping */
    fprintf(stderr, "Stream mapping:\n");
    for(i=0;i<nb_ostreams;i++) {
        ost = ost_table[i];
        fprintf(stderr, "  Stream #%d.%d -> #%d.%d\n",
                ist_table[ost->source_index]->file_index,
                ist_table[ost->source_index]->index,
                ost->file_index, 
                ost->index);
    }

    /* for each output stream, we compute the right encoding parameters */
    for(i=0;i<nb_ostreams;i++) {
        ost = ost_table[i];
        ist = ist_table[ost->source_index];

        codec = &ost->st->codec;
        icodec = &ist->st->codec;

        switch(codec->codec_type) {
        case CODEC_TYPE_AUDIO:
            /* check if same codec with same parameters. If so, no
               reencoding is needed */
            if (codec->codec_id == icodec->codec_id &&
                codec->bit_rate == icodec->bit_rate &&
                codec->sample_rate == icodec->sample_rate &&
                codec->channels == icodec->channels) {
                /* no reencoding */
                /* use the same frame size */
                codec->frame_size = icodec->frame_size;
                //codec->frame_size = 8*icodec->sample_rate*icodec->frame_size/
                //                    icodec->bit_rate;
                //fprintf(stderr,"\nFrame size: %d", codec->frame_size);
            } else {
                if (fifo_init(&ost->fifo, 2 * MAX_AUDIO_PACKET_SIZE))
                    goto fail;

                if (codec->channels == icodec->channels &&
                    codec->sample_rate == icodec->sample_rate) {
                    ost->audio_resample = 0;
                } else {
                    if (codec->channels != icodec->channels &&
                        icodec->codec_id == CODEC_ID_AC3) {
                        /* Special case for 5:1 AC3 input */
                        /* and mono or stereo output      */
                        ost->audio_resample = 0;
                        /* Request specific number of channels */
                        icodec->channels = codec->channels;
                    } else {
                        ost->audio_resample = 1; 
                        ost->resample = audio_resample_init(codec->channels, icodec->channels,
                                                        codec->sample_rate, 
                                                        icodec->sample_rate);
                    }
                }
                ist->decoding_needed = 1;
                ost->encoding_needed = 1;
            }
            break;
        case CODEC_TYPE_VIDEO:
            /* check if same codec with same parameters. If so, no
               reencoding is needed */
            if (codec->codec_id == icodec->codec_id &&
                codec->bit_rate == icodec->bit_rate &&
                codec->frame_rate == icodec->frame_rate &&
                codec->width == icodec->width &&
                codec->height == icodec->height) {
                /* no reencoding */
            } else {
                if (codec->width == icodec->width &&
                    codec->height == icodec->height) {
                    ost->video_resample = 0;
                } else {
                    UINT8 *buf;
                    ost->video_resample = 1;
                    buf = malloc((codec->width * codec->height * 3) / 2);
                    if (!buf)
                        goto fail;
                    ost->pict_tmp.data[0] = buf;
                    ost->pict_tmp.data[1] = ost->pict_tmp.data[0] + (codec->width * codec->height);
                    ost->pict_tmp.data[2] = ost->pict_tmp.data[1] + (codec->width * codec->height) / 4;
                    ost->pict_tmp.linesize[0] = codec->width;
                    ost->pict_tmp.linesize[1] = codec->width / 2;
                    ost->pict_tmp.linesize[2] = codec->width / 2;

                    ost->img_resample_ctx = img_resample_init( 
                                      ost->st->codec.width, ost->st->codec.height,
                                      ist->st->codec.width, ist->st->codec.height);
                }
                ost->encoding_needed = 1;
                ist->decoding_needed = 1;
            }
            break;
        }
    }

    /* open each encoder */
    for(i=0;i<nb_ostreams;i++) {
        ost = ost_table[i];
        if (ost->encoding_needed) {
            AVCodec *codec;
            codec = avcodec_find_encoder(ost->st->codec.codec_id);
            if (!codec) {
                fprintf(stderr, "Unsupported codec for output stream #%d.%d\n", 
                        ost->file_index, ost->index);
                exit(1);
            }
            if (avcodec_open(&ost->st->codec, codec) < 0) {
                fprintf(stderr, "Error while opening codec for stream #%d.%d - maybe incorrect parameters such as bit_rate, rate, width or height\n", 
                        ost->file_index, ost->index);
                exit(1);
            }
        }
    }

    /* open each decoder */
    for(i=0;i<nb_istreams;i++) {
        ist = ist_table[i];
        if (ist->decoding_needed) {
            AVCodec *codec;
            codec = avcodec_find_decoder(ist->st->codec.codec_id);
            if (!codec) {
                fprintf(stderr, "Unsupported codec for input stream #%d.%d\n", 
                        ist->file_index, ist->index);
                exit(1);
            }
            if (avcodec_open(&ist->st->codec, codec) < 0) {
                fprintf(stderr, "Error while opening codec for input stream #%d.%d\n", 
                        ist->file_index, ist->index);
                exit(1);
            }
        }
    }

    /* init pts */
    for(i=0;i<nb_istreams;i++) {
        ist = ist_table[i];
        ist->pts = 0;
        ist->frame_number = 0;
    }
    
    /* compute buffer size max (should use a complete heuristic) */
    for(i=0;i<nb_input_files;i++) {
        file_table[i].buffer_size_max = 2048;
    }

    /* open files and write file headers */
    for(i=0;i<nb_output_files;i++) {
        os = output_files[i];
        if (os->format->write_header(os) < 0) {
            fprintf(stderr, "Could not write header for output file #%d (incorrect codec paramters ?)\n", i);
            ret = -EINVAL;
            goto fail;
        }
    }

#ifndef CONFIG_WIN32
    if (!do_play) {
        fprintf(stderr, "Press [q] to stop encoding\n");
    } else {
        fprintf(stderr, "Press [q] to stop playing\n");
    }
#endif
    term_init();

    start_time = gettime();
    min_pts = 0;
    for(;;) {
        int file_index, ist_index;
        AVPacket pkt;
        UINT8 *ptr;
        int len;
        UINT8 *data_buf;
        int data_size, got_picture;
        AVPicture picture;
        short samples[AVCODEC_MAX_AUDIO_FRAME_SIZE / 2];

    redo:
        /* if 'q' pressed, exits */
        if (read_key() == 'q')
            break;

        /* select the input file with the smallest pts */
        file_index = -1;
        min_pts = MAXINT64;
        for(i=0;i<nb_istreams;i++) {
            ist = ist_table[i];
            if (!ist->discard && !file_table[ist->file_index].eof_reached && ist->pts < min_pts) {
                min_pts = ist->pts;
                file_index = ist->file_index;
            }
        }
        /* if none, if is finished */
        if (file_index < 0)
            break;
        /* finish if recording time exhausted */
        if (recording_time > 0 && min_pts >= recording_time)
            break;
        /* read a packet from it and output it in the fifo */
        is = input_files[file_index];
        if (av_read_packet(is, &pkt) < 0) {
            file_table[file_index].eof_reached = 1;
            continue;
        }
        ist_index = file_table[file_index].ist_index + pkt.stream_index;
        ist = ist_table[ist_index];
        if (ist->discard) {
            continue;
        }

        if (do_hex_dump) {
            printf("stream #%d, size=%d:\n", pkt.stream_index, pkt.size);
            hex_dump(pkt.data, pkt.size);
        }

        //        printf("read #%d.%d size=%d\n", ist->file_index, ist->index, pkt.size);

        len = pkt.size;
        ptr = pkt.data;
        while (len > 0) {

            /* decode the packet if needed */
            data_buf = NULL; /* fail safe */
            data_size = 0;
            if (ist->decoding_needed) {
                switch(ist->st->codec.codec_type) {
                case CODEC_TYPE_AUDIO:
                    /* XXX: could avoid copy if PCM 16 bits with same
                       endianness as CPU */
                    ret = avcodec_decode_audio(&ist->st->codec, samples, &data_size,
                                               ptr, len);
                    if (ret < 0)
                        goto fail_decode;
                    if (data_size == 0) {
                        /* no audio frame */
                        ptr += ret;
                        len -= ret;
                        continue;
                    }
                    data_buf = (UINT8 *)samples;
                    break;
                case CODEC_TYPE_VIDEO:
                    if (ist->st->codec.codec_id == CODEC_ID_RAWVIDEO) {
                        int size;
                        size = (ist->st->codec.width * ist->st->codec.height);
                        avpicture_fill(&picture, ptr, 
                                     ist->st->codec.pix_fmt,
                                     ist->st->codec.width,
                                     ist->st->codec.height);
                        ret = len;
                    } else {
                        data_size = (ist->st->codec.width * ist->st->codec.height * 3) / 2;
                        ret = avcodec_decode_video(&ist->st->codec, 
                                                   &picture, &got_picture, ptr, len);
                        if (ret < 0) {
                        fail_decode:
                            fprintf(stderr, "Error while decoding stream #%d.%d\n",
                                    ist->file_index, ist->index);
                            av_free_packet(&pkt);
                            goto redo;
                        }
                        if (!got_picture) {
                            /* no picture yet */
                            ptr += ret;
                            len -= ret;
                            continue;
                        }
                    }
                    break;
                default:
                    goto fail_decode;
                }
            } else {
                data_buf = ptr;
                data_size = len;
                ret = len;
            }
            /* update pts */
            switch(ist->st->codec.codec_type) {
            case CODEC_TYPE_AUDIO:
                ist->pts = (INT64)1000000 * ist->sample_index / ist->st->codec.sample_rate;
                ist->sample_index += data_size / (2 * ist->st->codec.channels);
                break;
            case CODEC_TYPE_VIDEO:
                ist->frame_number++;
                ist->pts = ((INT64)ist->frame_number * 1000000 * FRAME_RATE_BASE) / 
                    ist->st->codec.frame_rate;
                break;
            }
            ptr += ret;
            len -= ret;

            /* transcode raw format, encode packets and output them */
            
            for(i=0;i<nb_ostreams;i++) {
                int frame_size;
                ost = ost_table[i];
                if (ost->source_index == ist_index) {
                    os = output_files[ost->file_index];

                    if (ost->encoding_needed) {
                        switch(ost->st->codec.codec_type) {
                        case CODEC_TYPE_AUDIO:
                            do_audio_out(os, ost, ist, data_buf, data_size);
                            break;
                        case CODEC_TYPE_VIDEO:
                            do_video_out(os, ost, ist, &picture, &frame_size);
                            if (do_vstats)
                                do_video_stats(ost, ist, frame_size);
                            break;
                        }
                    } else {
                        /* no reencoding needed : output the packet directly */
                        /* force the input stream PTS */
                        os->format->write_packet(os, ost->index, data_buf, data_size, pkt.pts);
                    }
                }
            }
        }
        av_free_packet(&pkt);
        
        /* dump report by using the first video and audio streams */
        {
            char buf[1024];
            AVFormatContext *oc;
            INT64 total_size, ti;
            AVCodecContext *enc;
            int frame_number, vid;
            double bitrate, ti1;
            static INT64 last_time;

            if ((min_pts - last_time) >= 500000) {
                last_time = min_pts;
                
                oc = output_files[0];
                
                total_size = url_ftell(&oc->pb);
                
                buf[0] = '\0';
                ti = MAXINT64;
                vid = 0;
                for(i=0;i<nb_ostreams;i++) {
                    ost = ost_table[i];
                    enc = &ost->st->codec;
                    ist = ist_table[ost->source_index];
                    if (!vid && enc->codec_type == CODEC_TYPE_VIDEO) {
                        frame_number = ist->frame_number;
                        sprintf(buf + strlen(buf), "frame=%5d q=%2d ",
                                frame_number, enc->quality);
                        if (do_psnr)
                            sprintf(buf + strlen(buf), "PSNR=%6.2f ", enc->psnr_y);
                        vid = 1;
                    }
                    /* compute min pts value */
                    if (!ist->discard && ist->pts < ti) {
                        ti = ist->pts;
                    }
                }

                ti1 = (double)ti / 1000000.0;
                if (ti1 < 0.01)
                    ti1 = 0.01;
                bitrate = (double)(total_size * 8) / ti1 / 1000.0;

                sprintf(buf + strlen(buf), 
                        "size=%8.0fkB time=%0.1f bitrate=%6.1fkbits/s",
                        (double)total_size / 1024, ti1, bitrate);
                
                fprintf(stderr, "%s   \r", buf);
                fflush(stderr);
            }
        }
    }
    term_exit();

    /* dump report by using the first video and audio streams */
    {
        char buf[1024];
        AVFormatContext *oc;
        INT64 total_size, ti;
        AVCodecContext *enc;
        int frame_number, vid;
        double bitrate, ti1;

        oc = output_files[0];
        
        total_size = url_ftell(&oc->pb);
        
        buf[0] = '\0';
        ti = MAXINT64;
        vid = 0;
        for(i=0;i<nb_ostreams;i++) {
            ost = ost_table[i];
            enc = &ost->st->codec;
            ist = ist_table[ost->source_index];
            if (!vid && enc->codec_type == CODEC_TYPE_VIDEO) {
                frame_number = ist->frame_number;
                sprintf(buf + strlen(buf), "frame=%5d q=%2d ",
                        frame_number, enc->quality);
                if (do_psnr)
                    sprintf(buf + strlen(buf), "PSNR=%6.2f ", enc->psnr_y);
                vid = 1;
            }
            /* compute min pts value */
            if (!ist->discard && ist->pts < ti) {
                ti = ist->pts;
            }
        }
        
        ti1 = ti / 1000000.0;
        if (ti1 < 0.01)
            ti1 = 0.01;
        bitrate = (double)(total_size * 8) / ti1 / 1000.0;
        
        sprintf(buf + strlen(buf), 
                "size=%8.0fkB time=%0.1f bitrate=%6.1fkbits/s",
                (double)total_size / 1024, ti1, bitrate);
        
        fprintf(stderr, "%s   \n", buf);
    }
    /* close each encoder */
    for(i=0;i<nb_ostreams;i++) {
        ost = ost_table[i];
        if (ost->encoding_needed) {
            avcodec_close(&ost->st->codec);
        }
    }
    
    /* close each decoder */
    for(i=0;i<nb_istreams;i++) {
        ist = ist_table[i];
        if (ist->decoding_needed) {
            avcodec_close(&ist->st->codec);
        }
    }
    

    /* write the trailer if needed and close file */
    for(i=0;i<nb_output_files;i++) {
        os = output_files[i];
        os->format->write_trailer(os);
    }
    /* finished ! */
    
    ret = 0;
 fail1:
    free(file_table);

    if (ist_table) {
        for(i=0;i<nb_istreams;i++) {
            ist = ist_table[i];
            if (ist) {
                free(ist);
            }
        }
        free(ist_table);
    }
    if (ost_table) {
        for(i=0;i<nb_ostreams;i++) {
            ost = ost_table[i];
            if (ost) {
                if (ost->pict_tmp.data[0])
                    free(ost->pict_tmp.data[0]);
                if (ost->video_resample)
                    img_resample_close(ost->img_resample_ctx);
                if (ost->audio_resample)
                    audio_resample_close(ost->resample);
                free(ost);
            }
        }
        free(ost_table);
    }
    return ret;
 fail:
    ret = -ENOMEM;
    goto fail1;
}

#if 0
int file_read(const char *filename)
{
    URLContext *h;
    unsigned char buffer[1024];
    int len, i;

    if (url_open(&h, filename, O_RDONLY) < 0) {
        printf("could not open '%s'\n", filename);
        return -1;
    }
    for(;;) {
        len = url_read(h, buffer, sizeof(buffer));
        if (len <= 0)
            break;
        for(i=0;i<len;i++) putchar(buffer[i]);
    }
    url_close(h);
    return 0;
}
#endif

void show_licence(void)
{
    printf(
    "ffmpeg version " FFMPEG_VERSION "\n"
    "Copyright (c) 2000,2001 Gerard Lantau\n"
    "This program is free software; you can redistribute it and/or modify\n"
    "it under the terms of the GNU General Public License as published by\n"
    "the Free Software Foundation; either version 2 of the License, or\n"
    "(at your option) any later version.\n"
    "\n"
    "This program is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
    "GNU General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU General Public License\n"
    "along with this program; if not, write to the Free Software\n"
    "Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.\n"
    );
    exit(1);
}

void opt_format(const char *arg)
{
    AVFormat *f;
    f = first_format;
    while (f != NULL && strcmp(f->name, arg) != 0) f = f->next;
    if (f == NULL) {
        fprintf(stderr, "Invalid format: %s\n", arg);
        exit(1);
    }
    file_format = f;
}

void opt_video_bitrate(const char *arg)
{
    video_bit_rate = atoi(arg) * 1000;
}

void opt_video_bitrate_tolerance(const char *arg)
{
    video_bit_rate_tolerance = atoi(arg) * 1000;
}

void opt_frame_rate(const char *arg)
{
    frame_rate = (int)(strtod(arg, 0) * FRAME_RATE_BASE);
}

void opt_frame_size(const char *arg)
{
    parse_image_size(&frame_width, &frame_height, arg);
    if (frame_width <= 0 || frame_height <= 0) {
        fprintf(stderr, "Incorrect frame size\n");
        exit(1);
    }
    if ((frame_width % 2) != 0 || (frame_height % 2) != 0) {
        fprintf(stderr, "Frame size must be a multiple of 2\n");
        exit(1);
    }
}

void opt_gop_size(const char *arg)
{
    gop_size = atoi(arg);
}

void opt_b_frames(const char *arg)
{
    b_frames = atoi(arg);
    if (b_frames > FF_MAX_B_FRAMES) {
        fprintf(stderr, "\nCannot have more than %d B frames, increase FF_MAX_B_FRAMES.\n", FF_MAX_B_FRAMES);
        exit(1);
    } else if (b_frames < 1) {
        fprintf(stderr, "\nNumber of B frames must be higher than 0\n");
        exit(1);
    }
}

void opt_qscale(const char *arg)
{
    video_qscale = atoi(arg);
    if (video_qscale < 0 ||
        video_qscale > 31) {
        fprintf(stderr, "qscale must be >= 1 and <= 31\n");
        exit(1);
    }
}

void opt_qmin(const char *arg)
{
    video_qmin = atoi(arg);
    if (video_qmin < 0 ||
        video_qmin > 31) {
        fprintf(stderr, "qmin must be >= 1 and <= 31\n");
        exit(1);
    }
}

void opt_qmax(const char *arg)
{
    video_qmax = atoi(arg);
    if (video_qmax < 0 ||
        video_qmax > 31) {
        fprintf(stderr, "qmax must be >= 1 and <= 31\n");
        exit(1);
    }
}

void opt_qdiff(const char *arg)
{
    video_qdiff = atoi(arg);
    if (video_qdiff < 0 ||
        video_qdiff > 31) {
        fprintf(stderr, "qdiff must be >= 1 and <= 31\n");
        exit(1);
    }
}

void opt_qblur(const char *arg)
{
    video_qblur = atof(arg);
}

void opt_qcomp(const char *arg)
{
    video_qcomp = atof(arg);
}

void opt_audio_bitrate(const char *arg)
{
    audio_bit_rate = atoi(arg) * 1000;
}

void opt_audio_rate(const char *arg)
{
    audio_sample_rate = atoi(arg);
}

void opt_audio_channels(const char *arg)
{
    audio_channels = atoi(arg);
}

#ifdef CONFIG_GRAB
void opt_video_device(const char *arg)
{
    v4l_device = strdup(arg);
}

void opt_audio_device(const char *arg)
{
    audio_device = strdup(arg);
}
#endif

void opt_audio_codec(const char *arg)
{
    AVCodec *p;

    p = first_avcodec;
    while (p) {
        if (!strcmp(p->name, arg) && p->type == CODEC_TYPE_AUDIO)
            break;
        p = p->next;
    }
    if (p == NULL) {
        fprintf(stderr, "Unknown audio codec '%s'\n", arg);
        exit(1);
    } else {
        audio_codec_id = p->id;
    }
}

const char *motion_str[] = {
    "zero",
    "full",
    "log",
    "phods",
    "epzs",
    "x1",
    NULL,
};

void opt_motion_estimation(const char *arg)
{
    const char **p;
    p = motion_str;
    for(;;) {
        if (!*p) {
            fprintf(stderr, "Unknown motion estimation method '%s'\n", arg);
            exit(1);
        }
        if (!strcmp(*p, arg))
            break;
        p++;
    }
    me_method = (p - motion_str) + 1;
}

void opt_video_codec(const char *arg)
{
    AVCodec *p;

    p = first_avcodec;
    while (p) {
        if (!strcmp(p->name, arg) && p->type == CODEC_TYPE_VIDEO)
            break;
        p = p->next;
    }
    if (p == NULL) {
        fprintf(stderr, "Unknown video codec '%s'\n", arg);
        exit(1);
    } else {
        video_codec_id = p->id;
    }
}

void opt_map(const char *arg)
{
    AVStreamMap *m;
    const char *p;

    p = arg;
    m = &stream_maps[nb_stream_maps++];

    m->file_index = strtol(arg, (char **)&p, 0);
    if (*p)
        p++;
    m->stream_index = strtol(arg, (char **)&p, 0);
}

void opt_recording_time(const char *arg)
{
    recording_time = parse_date(arg, 1);
}

/* return the number of packet read to find the codec parameters */
int find_codec_parameters(AVFormatContext *ic)
{
    int val, i, count, ret, got_picture, size;
    AVCodec *codec;
    AVCodecContext *enc;
    AVStream *st;
    AVPacket *pkt;
    AVPicture picture;
    AVPacketList *pktl, **ppktl;
    short samples[AVCODEC_MAX_AUDIO_FRAME_SIZE / 2];
    UINT8 *ptr;

    count = 0;
    ppktl = &ic->packet_buffer;
    for(;;) {
        for(i=0;i<ic->nb_streams;i++) {
            enc = &ic->streams[i]->codec;
            
            switch(enc->codec_type) {
            case CODEC_TYPE_AUDIO:
                val = enc->sample_rate;
                break;
            case CODEC_TYPE_VIDEO:
                val = enc->width;
                break;
            default:
                val = 1;
                break;
            }
            /* if no parameters supplied, then we should read it from
               the stream */
            if (val == 0)
                break;
        }
        if (i == ic->nb_streams) {
            ret = count;
            break;
        }

        if (count == 0) {
            /* open each codec */
            for(i=0;i<ic->nb_streams;i++) {
                st = ic->streams[i];
                codec = avcodec_find_decoder(st->codec.codec_id);
                if (codec == NULL) {
                    ret = -1;
                    goto the_end;
                }
                avcodec_open(&st->codec, codec);
            }
        }
        pktl = av_mallocz(sizeof(AVPacketList));
        if (!pktl) {
            ret = -1;
            break;
        }

        /* add the packet in the buffered packet list */
        *ppktl = pktl;
        ppktl = &pktl->next;

        pkt = &pktl->pkt;
        if (ic->format->read_packet(ic, pkt) < 0) {
            ret = -1;
            break;
        }
        st = ic->streams[pkt->stream_index];

        /* decode the data and update codec parameters */
        ptr = pkt->data;
        size = pkt->size;
        while (size > 0) {
            switch(st->codec.codec_type) {
            case CODEC_TYPE_VIDEO:
                ret = avcodec_decode_video(&st->codec, &picture, &got_picture, ptr, size);
                break;
            case CODEC_TYPE_AUDIO:
                ret = avcodec_decode_audio(&st->codec, samples, &got_picture, ptr, size);
                break;
            default:
                ret = -1;
                break;
            }
            if (ret < 0) {
                ret = -1;
                goto the_end;
            }
            if (got_picture)
                break;
            ptr += ret;
            size -= ret;
        }

        count++;
    }
 the_end:
    if (count > 0) {
        /* close each codec */
        for(i=0;i<ic->nb_streams;i++) {
            st = ic->streams[i];
            avcodec_close(&st->codec);
        }
    }
    return ret;
}

int filename_number_test(const char *filename)
{
    char buf[1024];

    if (get_frame_filename(buf, sizeof(buf), filename, 1) < 0) {
        fprintf(stderr, "%s: Incorrect image filename syntax.\n"
                "Use '%%d' to specify the image number:\n"
                "  for img1.jpg, img2.jpg, ..., use 'img%%d.jpg';\n"
                "  for img001.jpg, img002.jpg, ..., use 'img%%03d.jpg'.\n", 
                filename);
        return -1;
    } else {
        return 0;
    }
}

void opt_input_file(const char *filename)
{
    AVFormatContext *ic;
    AVFormatParameters params, *ap = &params;
    URLFormat url_format;
    AVFormat *fmt;
    int err, i, ret;

    ic = av_mallocz(sizeof(AVFormatContext));
    strcpy(ic->filename, filename);
    /* first format guess to know if we must open file */
    fmt = file_format;
    if (!fmt) 
        fmt = guess_format(NULL, filename, NULL);
    
    if (fmt == NULL || !(fmt->flags & AVFMT_NOFILE)) {
        /* open file */
        if (url_fopen(&ic->pb, filename, URL_RDONLY) < 0) {
            fprintf(stderr, "Could not open '%s'\n", filename);
            exit(1);
        }
    
        /* find format and set default parameters */
        fmt = file_format;
        err = url_getformat(url_fileno(&ic->pb), &url_format);
        if (err >= 0) {
            if (!fmt)
                fmt = guess_format(url_format.format_name, NULL, NULL);
            ap->sample_rate = url_format.sample_rate;
            ap->frame_rate = url_format.frame_rate;
            ap->channels = url_format.channels;
            ap->width = url_format.width;
            ap->height = url_format.height;
            ap->pix_fmt = url_format.pix_fmt;
        } else {
            if (!fmt)
                fmt = guess_format(NULL, filename, NULL);
            memset(ap, 0, sizeof(*ap));
        }
    } else {
        memset(ap, 0, sizeof(*ap));
    }

    if (!fmt || !fmt->read_header) {
        fprintf(stderr, "%s: Unknown file format\n", filename);
        exit(1);
    }
    ic->format = fmt;

    /* get default parameters from command line */
    if (!ap->sample_rate)
        ap->sample_rate = audio_sample_rate;
    if (!ap->channels)
        ap->channels = audio_channels;

    if (!ap->frame_rate)
        ap->frame_rate = frame_rate;
    if (!ap->width)
        ap->width = frame_width;
    if (!ap->height)
        ap->height = frame_height;

    /* check filename in case of an image number is expected */
    if (ic->format->flags & AVFMT_NEEDNUMBER) {
        if (filename_number_test(ic->filename) < 0)
            exit(1);
    }
    
    err = ic->format->read_header(ic, ap);
    if (err < 0) {
        fprintf(stderr, "%s: Error while parsing header\n", filename);
        exit(1);
    }
    
    /* If not enough info for the codecs, we decode the first frames
       to get it. (used in mpeg case for example) */
    ret = find_codec_parameters(ic);
    if (ret < 0) {
        fprintf(stderr, "%s: could not find codec parameters\n", filename);
        exit(1);
    }

    /* update the current parameters so that they match the one of the input stream */
    for(i=0;i<ic->nb_streams;i++) {
        AVCodecContext *enc = &ic->streams[i]->codec;
        switch(enc->codec_type) {
        case CODEC_TYPE_AUDIO:
            //fprintf(stderr, "\nInput Audio channels: %d", enc->channels);
            audio_channels = enc->channels;
            audio_sample_rate = enc->sample_rate;
            break;
        case CODEC_TYPE_VIDEO:
            frame_height = enc->height;
            frame_width = enc->width;
            frame_rate = enc->frame_rate;
            break;
        }
    }
    
    input_files[nb_input_files] = ic;
    /* dump the file content */
    dump_format(ic, nb_input_files, filename, 0);
    nb_input_files++;
    file_format = NULL;
}

void check_audio_video_inputs(int *has_video_ptr, int *has_audio_ptr)
{
    int has_video, has_audio, i, j;
    AVFormatContext *ic;

    has_video = 0;
    has_audio = 0;
    for(j=0;j<nb_input_files;j++) {
        ic = input_files[j];
        for(i=0;i<ic->nb_streams;i++) {
            AVCodecContext *enc = &ic->streams[i]->codec;
            switch(enc->codec_type) {
            case CODEC_TYPE_AUDIO:
                has_audio = 1;
                break;
            case CODEC_TYPE_VIDEO:
                has_video = 1;
                break;
            }
        }
    }
    *has_video_ptr = has_video;
    *has_audio_ptr = has_audio;
}

void opt_output_file(const char *filename)
{
    AVStream *st;
    AVFormatContext *oc;
    int use_video, use_audio, nb_streams, input_has_video, input_has_audio;
    int codec_id;

    if (!strcmp(filename, "-"))
        filename = "pipe:";

    oc = av_mallocz(sizeof(AVFormatContext));

    if (!file_format) {
        file_format = guess_format(NULL, filename, NULL);
        if (!file_format)
            file_format = &mpeg_mux_format;
    }
    
    oc->format = file_format;

    if (!strcmp(file_format->name, "ffm") && 
        strstart(filename, "http:", NULL)) {
        /* special case for files sent to ffserver: we get the stream
           parameters from ffserver */
        if (read_ffserver_streams(oc, filename) < 0) {
            fprintf(stderr, "Could not read stream parameters from '%s'\n", filename);
            exit(1);
        }
    } else {
        use_video = file_format->video_codec != CODEC_ID_NONE;
        use_audio = file_format->audio_codec != CODEC_ID_NONE;

        /* disable if no corresponding type found and at least one
           input file */
        if (nb_input_files > 0) {
            check_audio_video_inputs(&input_has_video, &input_has_audio);
            if (!input_has_video)
                use_video = 0;
            if (!input_has_audio)
                use_audio = 0;
        }

        /* manual disable */
        if (audio_disable) {
            use_audio = 0;
        }
        if (video_disable) {
            use_video = 0;
        }
        
        nb_streams = 0;
        if (use_video) {
            AVCodecContext *video_enc;
            
            st = av_mallocz(sizeof(AVStream));
            if (!st) {
                fprintf(stderr, "Could not alloc stream\n");
                exit(1);
            }
            video_enc = &st->codec;

            codec_id = file_format->video_codec;
            if (video_codec_id != CODEC_ID_NONE)
                codec_id = video_codec_id;

            video_enc->codec_id = codec_id;
            video_enc->codec_type = CODEC_TYPE_VIDEO;
            
            video_enc->bit_rate = video_bit_rate;
            video_enc->bit_rate_tolerance = video_bit_rate_tolerance;
            video_enc->frame_rate = frame_rate; 
            
            video_enc->width = frame_width;
            video_enc->height = frame_height;
            if (!intra_only)
                video_enc->gop_size = gop_size;
            else
                video_enc->gop_size = 0;
            if (video_qscale || same_quality) {
                video_enc->flags |= CODEC_FLAG_QSCALE;
                video_enc->quality = video_qscale;
            }
            
            if (use_hq) {
                video_enc->flags |= CODEC_FLAG_HQ;
            }
            
            if (use_4mv) {
                video_enc->flags |= CODEC_FLAG_HQ;
                video_enc->flags |= CODEC_FLAG_4MV;
            }
            
            if (b_frames) {
                if (codec_id != CODEC_ID_MPEG4) {
                    fprintf(stderr, "\nB frames encoding only supported by MPEG-4.\n");
                    exit(1);
                }
                video_enc->max_b_frames = b_frames;
                video_enc->b_frame_strategy = 0;
                video_enc->b_quant_factor = 2.0;
            }
            
            video_enc->qmin = video_qmin;
            video_enc->qmax = video_qmax;
            video_enc->max_qdiff = video_qdiff;
            video_enc->qblur = video_qblur;
            video_enc->qcompress = video_qcomp;
            
            if (do_psnr)
                video_enc->get_psnr = 1;
            else
                video_enc->get_psnr = 0;
            
            video_enc->me_method = me_method;
            
            /* XXX: need to find a way to set codec parameters */
            if (oc->format == &ppm_format ||
                oc->format == &ppmpipe_format) {
                video_enc->pix_fmt = PIX_FMT_RGB24;
            }

            oc->streams[nb_streams] = st;
            nb_streams++;
        }
    
        if (use_audio) {
            AVCodecContext *audio_enc;

            st = av_mallocz(sizeof(AVStream));
            if (!st) {
                fprintf(stderr, "Could not alloc stream\n");
                exit(1);
            }
            audio_enc = &st->codec;
            codec_id = file_format->audio_codec;
            if (audio_codec_id != CODEC_ID_NONE)
                codec_id = audio_codec_id;
            audio_enc->codec_id = codec_id;
            audio_enc->codec_type = CODEC_TYPE_AUDIO;
            
            audio_enc->bit_rate = audio_bit_rate;
            audio_enc->sample_rate = audio_sample_rate;
            /* For audio codecs other than AC3 we limit */
            /* the number of coded channels to stereo   */
            if (audio_channels > 2 && codec_id != CODEC_ID_AC3) {
                audio_enc->channels = 2;
            } else
                audio_enc->channels = audio_channels;
            oc->streams[nb_streams] = st;
            nb_streams++;
        }

        oc->nb_streams = nb_streams;

        if (!nb_streams) {
            fprintf(stderr, "No audio or video streams available\n");
            exit(1);
        }

        if (str_title)
            nstrcpy(oc->title, sizeof(oc->title), str_title);
        if (str_author)
            nstrcpy(oc->author, sizeof(oc->author), str_author);
        if (str_copyright)
            nstrcpy(oc->copyright, sizeof(oc->copyright), str_copyright);
        if (str_comment)
            nstrcpy(oc->comment, sizeof(oc->comment), str_comment);
    }

    output_files[nb_output_files] = oc;
    /* dump the file content */
    dump_format(oc, nb_output_files, filename, 1);
    nb_output_files++;

    strcpy(oc->filename, filename);

    /* check filename in case of an image number is expected */
    if (oc->format->flags & AVFMT_NEEDNUMBER) {
        if (filename_number_test(oc->filename) < 0)
            exit(1);
    }

    if (!(oc->format->flags & AVFMT_NOFILE)) {
        /* test if it already exists to avoid loosing precious files */
        if (!file_overwrite && 
            (strchr(filename, ':') == NULL ||
             strstart(filename, "file:", NULL))) {
            if (url_exist(filename)) {
                int c;
                
                printf("File '%s' already exists. Overwrite ? [y/N] ", filename);
                fflush(stdout);
                c = getchar();
                if (toupper(c) != 'Y') {
                    fprintf(stderr, "Not overwriting - exiting\n");
                    exit(1);
                }
            }
        }
        
        /* open the file */
        if (url_fopen(&oc->pb, filename, URL_WRONLY) < 0) {
            fprintf(stderr, "Could not open '%s'\n", filename);
            exit(1);
        }
    }

    /* reset some options */
    file_format = NULL;
    audio_disable = 0;
    video_disable = 0;
    audio_codec_id = CODEC_ID_NONE;
    video_codec_id = CODEC_ID_NONE;
}

#ifdef CONFIG_GRAB

/* prepare dummy protocols for grab */
void prepare_grab(void)
{
    int has_video, has_audio, i, j;
    AVFormatContext *oc;
    AVFormatContext *ic;
    AVFormatParameters ap1, *ap = &ap1;

    /* see if audio/video inputs are needed */
    has_video = 0;
    has_audio = 0;
    memset(ap, 0, sizeof(*ap));
    for(j=0;j<nb_output_files;j++) {
        oc = output_files[j];
        for(i=0;i<oc->nb_streams;i++) {
            AVCodecContext *enc = &oc->streams[i]->codec;
            switch(enc->codec_type) {
            case CODEC_TYPE_AUDIO:
                if (enc->sample_rate > ap->sample_rate)
                    ap->sample_rate = enc->sample_rate;
                if (enc->channels > ap->channels)
                    ap->channels = enc->channels;
                has_audio = 1;
                break;
            case CODEC_TYPE_VIDEO:
                if (enc->width > ap->width)
                    ap->width = enc->width;
                if (enc->height > ap->height)
                    ap->height = enc->height;
                if (enc->frame_rate > ap->frame_rate)
                    ap->frame_rate = enc->frame_rate;
                has_video = 1;
                break;
            }
        }
    }
    
    if (has_video == 0 && has_audio == 0) {
        fprintf(stderr, "Output file must have at least one audio or video stream\n");
        exit(1);
    }
    
    if (has_video) {
        ic = av_open_input_file("", "video_grab_device", 0, ap);
        if (!ic) {
            fprintf(stderr, "Could not open video grab device\n");
            exit(1);
        }
        input_files[nb_input_files] = ic;
        dump_format(ic, nb_input_files, v4l_device, 0);
        nb_input_files++;
    }
    if (has_audio) {
        ic = av_open_input_file("", "audio_device", 0, ap);
        if (!ic) {
            fprintf(stderr, "Could not open audio grab device\n");
            exit(1);
        }
        input_files[nb_input_files] = ic;
        dump_format(ic, nb_input_files, audio_device, 0);
        nb_input_files++;
    }
}

#else

void prepare_grab(void)
{
    fprintf(stderr, "Must supply at least one input file\n");
    exit(1);
}

#endif

/* open the necessary output devices for playing */
void prepare_play(void)
{
#ifndef __BEOS__
    file_format = guess_format("audio_device", NULL, NULL);
    if (!file_format) {
        fprintf(stderr, "Could not find audio device\n");
        exit(1);
    }
    
    opt_output_file(audio_device);
#endif
}


#ifndef CONFIG_WIN32
INT64 getutime(void)
{
    struct rusage rusage;

    getrusage(RUSAGE_SELF, &rusage);
    return (rusage.ru_utime.tv_sec * 1000000LL) + rusage.ru_utime.tv_usec;
}
#else
INT64 getutime(void)
{
  return gettime();
}
#endif

void show_formats(void)
{
    AVFormat *f;
    URLProtocol *up;
    AVCodec *p;
    const char **pp;

    printf("File formats:\n");
    printf("  Encoding:");
    for(f = first_format; f != NULL; f = f->next) {
        if (f->write_header)
            printf(" %s", f->name);
    }
    printf("\n");
    printf("  Decoding:");
    for(f = first_format; f != NULL; f = f->next) {
        if (f->read_header)
            printf(" %s", f->name);
    }
    printf("\n");

    printf("Codecs:\n");
    printf("  Encoders:");
    for(p = first_avcodec; p != NULL; p = p->next) {
        if (p->encode)
            printf(" %s", p->name);
    }
    printf("\n");

    printf("  Decoders:");
    for(p = first_avcodec; p != NULL; p = p->next) {
        if (p->decode)
            printf(" %s", p->name);
    }
    printf("\n");

    printf("Supported file protocols:");
    for(up = first_protocol; up != NULL; up = up->next)
        printf(" %s:", up->name);
    printf("\n");
    
    printf("Frame size abbreviations: sqcif qcif cif 4cif\n");
    printf("Motion estimation methods:");
    pp = motion_str;
    while (*pp) {
        printf(" %s", *pp);
        if ((pp - motion_str + 1) == ME_ZERO) 
            printf("(fastest)");
        else if ((pp - motion_str + 1) == ME_FULL) 
            printf("(slowest)");
        else if ((pp - motion_str + 1) == ME_EPZS) 
            printf("(default)");
        pp++;
    }
    printf("\n");
    exit(1);
}

void show_help(void)
{
    const char *prog;
    const OptionDef *po;
    int i, expert;
    
    prog = do_play ? "ffplay" : "ffmpeg";

    printf("%s version " FFMPEG_VERSION ", Copyright (c) 2000, 2001 Gerard Lantau\n", 
           prog);
    
    if (!do_play) {
        printf("usage: ffmpeg [[options] -i input_file]... {[options] outfile}...\n"
               "Hyper fast MPEG1/MPEG4/H263/RV and AC3/MPEG audio encoder\n");
    } else {
        printf("usage: ffplay [options] input_file...\n"
               "Simple audio player\n");
    }
           
    printf("\n"
           "Main options are:\n");
    for(i=0;i<2;i++) {
        if (i == 1)
            printf("\nAdvanced options are:\n");
        for(po = options; po->name != NULL; po++) {
            char buf[64];
            expert = (po->flags & OPT_EXPERT) != 0;
            if (expert == i) {
                strcpy(buf, po->name);
                if (po->flags & HAS_ARG) {
                    strcat(buf, " ");
                    strcat(buf, po->argname);
                }
                printf("-%-17s  %s\n", buf, po->help);
            }
        }
    }

    exit(1);
}

const OptionDef options[] = {
    { "L", 0, {(void*)show_licence}, "show license" },
    { "h", 0, {(void*)show_help}, "show help" },
    { "formats", 0, {(void*)show_formats}, "show available formats, codecs, protocols, ..." },
    { "f", HAS_ARG, {(void*)opt_format}, "force format", "fmt" },
    { "i", HAS_ARG, {(void*)opt_input_file}, "input file name", "filename" },
    { "y", OPT_BOOL, {(void*)&file_overwrite}, "overwrite output files" },
    { "map", HAS_ARG | OPT_EXPERT, {(void*)opt_map}, "set input stream mapping", "file:stream" },
    { "t", HAS_ARG, {(void*)opt_recording_time}, "set the recording time", "duration" },
    { "title", HAS_ARG | OPT_STRING, {(void*)&str_title}, "set the title", "string" },
    { "author", HAS_ARG | OPT_STRING, {(void*)&str_author}, "set the author", "string" },
    { "copyright", HAS_ARG | OPT_STRING, {(void*)&str_copyright}, "set the copyright", "string" },
    { "comment", HAS_ARG | OPT_STRING, {(void*)&str_comment}, "set the comment", "string" },
    /* video options */
    { "b", HAS_ARG, {(void*)opt_video_bitrate}, "set video bitrate (in kbit/s)", "bitrate" },
    { "r", HAS_ARG, {(void*)opt_frame_rate}, "set frame rate (in Hz)", "rate" },
    { "s", HAS_ARG, {(void*)opt_frame_size}, "set frame size (WxH or abbreviation)", "size" },
    { "g", HAS_ARG | OPT_EXPERT, {(void*)opt_gop_size}, "set the group of picture size", "gop_size" },
    { "intra", OPT_BOOL | OPT_EXPERT, {(void*)&intra_only}, "use only intra frames"},
    { "vn", OPT_BOOL, {(void*)&video_disable}, "disable video" },
    { "qscale", HAS_ARG | OPT_EXPERT, {(void*)opt_qscale}, "use fixed video quantiser scale (VBR)", "q" },
    { "qmin", HAS_ARG | OPT_EXPERT, {(void*)opt_qmin}, "min video quantiser scale (VBR)", "q" },
    { "qmax", HAS_ARG | OPT_EXPERT, {(void*)opt_qmax}, "max video quantiser scale (VBR)", "q" },
    { "qdiff", HAS_ARG | OPT_EXPERT, {(void*)opt_qdiff}, "max difference between the quantiser scale (VBR)", "q" },
    { "qblur", HAS_ARG | OPT_EXPERT, {(void*)opt_qblur}, "video quantiser scale blur (VBR)", "blur" },
    { "qcomp", HAS_ARG | OPT_EXPERT, {(void*)opt_qcomp}, "video quantiser scale compression (VBR)", "compression" },
    { "bt", HAS_ARG, {(void*)opt_video_bitrate_tolerance}, "set video bitrate tolerance (in kbit/s)", "tolerance" },
#ifdef CONFIG_GRAB
    { "vd", HAS_ARG | OPT_EXPERT, {(void*)opt_video_device}, "set video device", "device" },
#endif
    { "vcodec", HAS_ARG | OPT_EXPERT, {(void*)opt_video_codec}, "force video codec", "codec" },
    { "me", HAS_ARG | OPT_EXPERT, {(void*)opt_motion_estimation}, "set motion estimation method", 
      "method" },
    { "bf", HAS_ARG | OPT_EXPERT, {(void*)opt_b_frames}, "use 'frames' B frames (only MPEG-4)", "frames" },
    { "hq", OPT_BOOL | OPT_EXPERT, {(void*)&use_hq}, "activate high quality settings" },
    { "4mv", OPT_BOOL | OPT_EXPERT, {(void*)&use_4mv}, "use four motion vector by macroblock (only MPEG-4)" },
    { "sameq", OPT_BOOL, {(void*)&same_quality}, 
      "use same video quality as source (implies VBR)" },
    /* audio options */
    { "ab", HAS_ARG, {(void*)opt_audio_bitrate}, "set audio bitrate (in kbit/s)", "bitrate", },
    { "ar", HAS_ARG, {(void*)opt_audio_rate}, "set audio sampling rate (in Hz)", "rate" },
    { "ac", HAS_ARG, {(void*)opt_audio_channels}, "set number of audio channels", "channels" },
    { "an", OPT_BOOL, {(void*)&audio_disable}, "disable audio" },
#ifdef CONFIG_GRAB
    { "ad", HAS_ARG | OPT_EXPERT, {(void*)opt_audio_device}, "set audio device", "device" },
#endif
    { "acodec", HAS_ARG | OPT_EXPERT, {(void*)opt_audio_codec}, "force audio codec", "codec" },
    { "deinterlace", OPT_BOOL | OPT_EXPERT, {(void*)&do_deinterlace}, 
      "deinterlace pictures" },
    { "benchmark", OPT_BOOL | OPT_EXPERT, {(void*)&do_benchmark}, 
      "add timings for benchmarking" },
    { "hex", OPT_BOOL | OPT_EXPERT, {(void*)&do_hex_dump}, 
      "dump each input packet" },
    { "psnr", OPT_BOOL | OPT_EXPERT, {(void*)&do_psnr}, "calculate PSNR of compressed frames" },
    { "vstats", OPT_BOOL | OPT_EXPERT, {(void*)&do_vstats}, "dump video coding statistics to file" }, 

    { NULL, },
};

int main(int argc, char **argv)
{
    int optindex, i;
    const char *opt, *arg;
    const OptionDef *po;
    INT64 ti;
    
    register_all();

    /* detect if invoked as player */
    i = strlen(argv[0]);
    if (i >= 6 && !strcmp(argv[0] + i - 6, "ffplay"))
        do_play = 1;

    if (argc <= 1)
        show_help();
    
    /* parse options */
    optindex = 1;
    while (optindex < argc) {
        opt = argv[optindex++];
        
        if (opt[0] == '-' && opt[1] != '\0') {
            po = options;
            while (po->name != NULL) {
                if (!strcmp(opt + 1, po->name))
                    break;
                po++;
            }
            if (!po->name) {
                fprintf(stderr, "%s: unrecognized option '%s'\n", argv[0], opt);
                exit(1);
            }
            arg = NULL;
            if (po->flags & HAS_ARG)
                arg = argv[optindex++];
            if (po->flags & OPT_STRING) {
                char *str;
                str = strdup(arg);
                *po->u.str_arg = str;
            } else if (po->flags & OPT_BOOL) {
                *po->u.int_arg = 1;
            } else {
                po->u.func_arg(arg);
            }
        } else {
            if (!do_play) {
                opt_output_file(opt);
            } else {
                opt_input_file(opt);
            }
        }
    }


    if (!do_play) {
        /* file converter / grab */
        if (nb_output_files <= 0) {
            fprintf(stderr, "Must supply at least one output file\n");
            exit(1);
        }
        
        if (nb_input_files == 0) {
            prepare_grab();
        }
    } else {
        /* player */
        if (nb_input_files <= 0) {
            fprintf(stderr, "Must supply at least one input file\n");
            exit(1);
        }
        prepare_play();
    }

    ti = getutime();
    av_encode(output_files, nb_output_files, input_files, nb_input_files, 
              stream_maps, nb_stream_maps);
    ti = getutime() - ti;
    if (do_benchmark) {
        printf("bench: utime=%0.3fs\n", ti / 1000000.0);
    }

    /* close files */
    for(i=0;i<nb_output_files;i++) {
        if (!(output_files[i]->format->flags & AVFMT_NOFILE)) 
            url_fclose(&output_files[i]->pb);
    }
    for(i=0;i<nb_input_files;i++) {
        if (!(input_files[i]->format->flags & AVFMT_NOFILE)) 
            url_fclose(&input_files[i]->pb);
    }

    return 0;
}
