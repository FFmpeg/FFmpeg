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
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/time.h>
#include <string.h>
#include <sys/poll.h>
#include <termios.h>
#include <ctype.h>

#include "avformat.h"

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
static int video_qscale = 0;
static int video_disable = 0;
static int video_codec_id = CODEC_ID_NONE;
static int same_quality = 0;

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

#define AUDIO_FIFO_SIZE 8192

/* main loop for grabbing */
int av_grab(AVFormatContext *s)
{
    UINT8 audio_buf[AUDIO_FIFO_SIZE/2];
    UINT8 audio_buf1[AUDIO_FIFO_SIZE/2];
    UINT8 audio_out[AUDIO_FIFO_SIZE/2];
    UINT8 video_buffer[128*1024];
    char buf[256];
    short *samples;
    URLContext *audio_handle = NULL, *video_handle = NULL;
    int ret;
    AVCodecContext *enc, *first_video_enc = NULL;
    int frame_size, frame_bytes;
    int use_audio, use_video;
    int frame_rate, sample_rate, channels;
    int width, height, frame_number, i, pix_fmt = 0;
    AVOutputStream *ost_table[s->nb_streams], *ost;
    UINT8 *picture_in_buf = NULL, *picture_420p = NULL;
    int audio_fifo_size = 0, picture_size = 0;
    INT64 time_start;

    /* init output stream info */
    for(i=0;i<s->nb_streams;i++)
        ost_table[i] = NULL;

    /* output stream init */
    for(i=0;i<s->nb_streams;i++) {
        ost = av_mallocz(sizeof(AVOutputStream));
        if (!ost)
            goto fail;
        ost->index = i;
        ost->st = s->streams[i];
        ost_table[i] = ost;
    }

    use_audio = 0;
    use_video = 0;
    frame_rate = 0;
    sample_rate = 0;
    frame_size = 0;
    channels = 1;
    width = 0;
    height = 0;
    frame_number = 0;
    
    for(i=0;i<s->nb_streams;i++) {
        AVCodec *codec;

        ost = ost_table[i];
        enc = &ost->st->codec;
        codec = avcodec_find_encoder(enc->codec_id);
        if (!codec) {
            fprintf(stderr, "Unknown codec\n");
            return -1;
        }
        if (avcodec_open(enc, codec) < 0) {
            fprintf(stderr, "Incorrect encode parameters\n");
            return -1;
        }
        switch(enc->codec_type) {
        case CODEC_TYPE_AUDIO:
            use_audio = 1;
            if (enc->sample_rate > sample_rate)
                sample_rate = enc->sample_rate;
            if (enc->frame_size > frame_size)
                frame_size = enc->frame_size;
            if (enc->channels > channels)
                channels = enc->channels;
            break;
        case CODEC_TYPE_VIDEO:
            if (!first_video_enc)
                first_video_enc = enc;
            use_video = 1;
            if (enc->frame_rate > frame_rate)
                frame_rate = enc->frame_rate;
            if (enc->width > width)
                width = enc->width;
            if (enc->height > height)
                height = enc->height;
            break;
        }
    }

    /* audio */
    samples = NULL;
    if (use_audio) {
        snprintf(buf, sizeof(buf), "audio:%d,%d", sample_rate, channels);
        ret = url_open(&audio_handle, buf, URL_RDONLY);
        if (ret < 0) {
            fprintf(stderr, "Could not open audio device: disabling audio capture\n");
            use_audio = 0;
        } else {
            URLFormat f;
            /* read back exact grab parameters */
            if (url_getformat(audio_handle, &f) < 0) {
                fprintf(stderr, "could not read back video grab parameters\n");
                goto fail;
            }
            sample_rate = f.sample_rate;
            channels = f.channels;
            audio_fifo_size = ((AUDIO_FIFO_SIZE / 2) / audio_handle->packet_size) * 
                audio_handle->packet_size;
            fprintf(stderr, "Audio sampling: %d Hz, %s\n", 
                    sample_rate, channels == 2 ? "stereo" : "mono");
        }
    }
    
    /* video */
    if (use_video) {
        snprintf(buf, sizeof(buf), "video:%d,%d,%f", 
                 width, height, (float)frame_rate / FRAME_RATE_BASE);

        ret = url_open(&video_handle, buf, URL_RDONLY);
        if (ret < 0) {
            fprintf(stderr,"Could not init video 4 linux capture: disabling video capture\n");
            use_video = 0;
        } else {
            URLFormat f;
            const char *pix_fmt_str;
            /* read back exact grab parameters */
            if (url_getformat(video_handle, &f) < 0) {
                fprintf(stderr, "could not read back video grab parameters\n");
                goto fail;
            }
            width = f.width;
            height = f.height;
            pix_fmt = f.pix_fmt;
            switch(pix_fmt) {
            case PIX_FMT_YUV420P:
                pix_fmt_str = "420P";
                break;
            case PIX_FMT_YUV422:
                pix_fmt_str = "422";
                break;
            case PIX_FMT_RGB24:
                pix_fmt_str = "RGB24";
                break;
            case PIX_FMT_BGR24:
                pix_fmt_str = "BGR24";
                break;
            default:
                pix_fmt_str = "???";
                break;
            }
            picture_size = video_handle->packet_size;
            picture_in_buf = malloc(picture_size);
            if (!picture_in_buf)
                goto fail;
            /* allocate a temporary picture if not grabbing in 420P format */
            if (pix_fmt != PIX_FMT_YUV420P) {
                picture_420p = malloc((width * height * 3) / 2);
            }
            fprintf(stderr, "Video sampling: %dx%d, %s format, %0.2f fps\n", 
                    width, height, pix_fmt_str, (float)frame_rate / FRAME_RATE_BASE);
        }
    }

    if (!use_video && !use_audio) {
        fprintf(stderr,"Could not open grab devices : exiting\n");
        exit(1);
    }

    /* init built in conversion functions */
    for(i=0;i<s->nb_streams;i++) {
        ost = ost_table[i];
        enc = &ost->st->codec;
        switch(enc->codec_type) {
        case CODEC_TYPE_AUDIO:
            ost->audio_resample = 0;
            if ((enc->channels != channels ||
                 enc->sample_rate != sample_rate)) {
                ost->audio_resample = 1;
                ost->resample = audio_resample_init(enc->channels, channels,
                                                    enc->sample_rate, sample_rate);
            }
            if (fifo_init(&ost->fifo, (2 * audio_fifo_size * enc->sample_rate) / 
                          sample_rate))
                goto fail;
            break;
        case CODEC_TYPE_VIDEO:
            ost->video_resample = 0;
            if (enc->width != width ||
                enc->height != height) {
                UINT8 *buf;
                ost->video_resample = 1;
                buf = malloc((enc->width * enc->height * 3) / 2);
                if (!buf)
                    goto fail;
                ost->pict_tmp.data[0] = buf;
                ost->pict_tmp.data[1] = buf + enc->width * height;
                ost->pict_tmp.data[2] = ost->pict_tmp.data[1] + (enc->width * height) / 4;
                ost->pict_tmp.linesize[0] = enc->width;
                ost->pict_tmp.linesize[1] = enc->width / 2;
                ost->pict_tmp.linesize[2] = enc->width / 2;
                ost->img_resample_ctx = img_resample_init(
                                  ost->st->codec.width, ost->st->codec.height,
                                  width, height);
            }
        }
    }

    fprintf(stderr, "Press [q] to stop encoding\n");

    s->format->write_header(s);
    time_start = gettime();
    term_init();
    
    for(;;) {
        /* if 'q' pressed, exits */
        if (read_key() == 'q')
            break;

        /* read & compress audio frames */
        if (use_audio) {
            int ret, nb_samples, nb_samples_out;
            UINT8 *buftmp;

            for(;;) {
                ret = url_read(audio_handle, audio_buf, audio_fifo_size);
                if (ret <= 0)
                    break;
                /* fill each codec fifo by doing the right sample
                   rate conversion. This is not optimal because we
                   do too much work, but it is easy to do */
                nb_samples = ret / (channels * 2);
                for(i=0;i<s->nb_streams;i++) {
                    ost = ost_table[i];
                    enc = &ost->st->codec;
                    if (enc->codec_type == CODEC_TYPE_AUDIO) {
                        /* rate & stereo convertion */
                        if (!ost->audio_resample) {
                            buftmp = audio_buf;
                            nb_samples_out = nb_samples;
                        } else {
                            buftmp = audio_buf1;
                            nb_samples_out = audio_resample(ost->resample, 
                                                            (short *)buftmp, (short *)audio_buf,
                                                            nb_samples);
                        }
                        fifo_write(&ost->fifo, buftmp, nb_samples_out * enc->channels * 2, 
                                   &ost->fifo.wptr);
                    }
                }
                
                /* compress as many frame as possible with each audio codec */
                for(i=0;i<s->nb_streams;i++) {
                    ost = ost_table[i];
                    enc = &ost->st->codec;
                    if (enc->codec_type == CODEC_TYPE_AUDIO) {
                        frame_bytes = enc->frame_size * 2 * enc->channels;
                        
                        while (fifo_read(&ost->fifo, audio_buf, 
                                         frame_bytes, &ost->fifo.rptr) == 0) {
                            ret = avcodec_encode_audio(enc,
                                                       audio_out, sizeof(audio_out), 
                                                       (short *)audio_buf);
                            s->format->write_packet(s, ost->index, audio_out, ret);
                        }
                    }
                }
            }
        }

        if (use_video) {
            AVPicture *picture1;
            AVPicture picture;
            UINT8 *pict_buffer;

            ret = url_read(video_handle, picture_in_buf, picture_size);
            if (ret < 0)
                break;
            if (pix_fmt != PIX_FMT_YUV420P) {
                pict_buffer = picture_420p;
                img_convert_to_yuv420(pict_buffer, picture_in_buf, pix_fmt, width, height);
            } else {
                pict_buffer = picture_in_buf;
            }
            /* build a picture storage */
            picture.data[0] = pict_buffer;
            picture.data[1] = picture.data[0] + width * height;
            picture.data[2] = picture.data[1] + (width * height) / 4;
            picture.linesize[0] = width;
            picture.linesize[1] = width / 2;
            picture.linesize[2] = width / 2;
            
            for(i=0;i<s->nb_streams;i++) {
                ost = ost_table[i];
                enc = &ost->st->codec;
                if (enc->codec_type == CODEC_TYPE_VIDEO) {
                    int n1, n2, nb;

                    /* feed each codec with its requested frame rate */
                    n1 = ((INT64)frame_number * enc->frame_rate) / frame_rate;
                    n2 = (((INT64)frame_number + 1) * enc->frame_rate) / frame_rate;
                    nb = n2 - n1;
                    if (nb > 0) {
                        /* resize the picture if needed */
                        if (ost->video_resample) {
                            picture1 = &ost->pict_tmp;
                            img_resample(ost->img_resample_ctx, 
                                         picture1, &picture);
                        } else {
                            picture1 = &picture;
                        }
                        ret = avcodec_encode_video(enc, video_buffer, 
                                                   sizeof(video_buffer), 
                                                   picture1);
                        s->format->write_packet(s, ost->index, video_buffer, ret);
                    }
                }
            }
            frame_number++;
        }
        
        /* write report */
        {
            char buf[1024];
            INT64 total_size;
            float ti, bitrate;
            static float last_ti;
            INT64 ti1;

            total_size = url_ftell(&s->pb);
            ti1 = gettime() - time_start;
            /* check elapsed time */
            if (recording_time && ti1 >= recording_time)
                break;

            ti = ti1 / 1000000.0;
            if (ti < 0.1)
                ti = 0.1;
            /* dispaly twice per second */
            if ((ti - last_ti) >= 0.5) {
                last_ti = ti;
                bitrate = (int)((total_size * 8) / ti / 1000.0);
                
                buf[0] = '\0';
                if (use_video) {
                    sprintf(buf + strlen(buf), "frame=%5d fps=%4.1f q=%2d ",
                            frame_number, (float)frame_number / ti, first_video_enc->quality);
                }
                
                sprintf(buf + strlen(buf), "size=%8LdkB time=%0.1f bitrate=%6.1fkbits/s", 
                        total_size / 1024, ti, bitrate);
                fprintf(stderr, "%s    \r", buf);
                fflush(stderr);
            }
        }
    }
    term_exit();

    for(i=0;i<s->nb_streams;i++) {
        ost = ost_table[i];
        enc = &ost->st->codec;
        avcodec_close(enc);
    }
    s->format->write_trailer(s);
    
    if (audio_handle)
        url_close(audio_handle);

    if (video_handle)
        url_close(video_handle);

    /* write report */
    {
        float ti, bitrate;
        INT64 total_size;

        total_size = url_ftell(&s->pb);

        ti = (gettime() - time_start) / 1000000.0;
        if (ti < 0.1)
            ti = 0.1;
        bitrate = (int)((total_size * 8) / ti / 1000.0);

        fprintf(stderr, "\033[K\nTotal time = %0.1f s, %Ld KBytes, %0.1f kbits/s\n", 
                ti, total_size / 1024, bitrate);
        if (use_video) {
            fprintf(stderr, "Total frames = %d\n", frame_number);
        }
    }

    ret = 0;
 fail1:
    if (picture_in_buf)
        free(picture_in_buf);
    if (picture_420p)
        free(picture_420p);
    for(i=0;i<s->nb_streams;i++) {
        ost = ost_table[i];
        if (ost) {
            if (ost->fifo.buffer)
                fifo_free(&ost->fifo);
            if (ost->pict_tmp.data[0])
                free(ost->pict_tmp.data[0]);
            if (ost->video_resample)
                img_resample_close(ost->img_resample_ctx);
            if (ost->audio_resample)
                audio_resample_close(ost->resample);
            free(ost);
        }
    }
    return ret;
 fail:
    ret = -ENOMEM;
    goto fail1;
}

int read_ffserver_streams(AVFormatContext *s, const char *filename)
{
    int i;
    AVFormatContext *ic;

    ic = av_open_input_file(filename, FFM_PACKET_SIZE);
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
    if (enc->codec_id != CODEC_ID_PCM) {
        /* output resampled raw samples */
        fifo_write(&ost->fifo, buftmp, size_out, 
                   &ost->fifo.wptr);

        frame_bytes = enc->frame_size * 2 * enc->channels;
        
        while (fifo_read(&ost->fifo, audio_buf, frame_bytes, 
                     &ost->fifo.rptr) == 0) {
            ret = avcodec_encode_audio(enc,
                                       audio_out, sizeof(audio_out), (short *)audio_buf);
            s->format->write_packet(s, ost->index, audio_out, ret);
        }
    } else {
        /* XXX: handle endianness */
        s->format->write_packet(s, ost->index, buftmp, size_out);
    }
}

/* write a picture to a raw mux */
static void write_picture(AVFormatContext *s, int index, AVPicture *picture, int w, int h)
{
    UINT8 *buf, *src, *dest;
    int size, j, i;
    /* XXX: not efficient, should add test if we can take
       directly the AVPicture */
    size = (w * h) * 3 / 2; 
    buf = malloc(size);
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
    s->format->write_packet(s, index, buf, size);
    free(buf);
}


static void do_video_out(AVFormatContext *s, 
                         AVOutputStream *ost, 
                         AVInputStream *ist,
                         AVPicture *pict)
{
    int n1, n2, nb, i, ret, frame_number;
    AVPicture *picture;
    UINT8 video_buffer[128*1024];
    AVCodecContext *enc;

    enc = &ost->st->codec;

    frame_number = ist->frame_number;
    /* first drop frame if needed */
    n1 = ((INT64)frame_number * enc->frame_rate) / ist->st->codec.frame_rate;
    n2 = (((INT64)frame_number + 1) * enc->frame_rate) / ist->st->codec.frame_rate;
    nb = n2 - n1;
    if (nb <= 0)
        return;
    
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
                ost->st->codec.quality = ist->st->codec.quality;
            }
            ret = avcodec_encode_video(&ost->st->codec, 
                                       video_buffer, sizeof(video_buffer), 
                                       picture);
            s->format->write_packet(s, ost->index, video_buffer, ret);
        } else {
            write_picture(s, ost->index, picture, enc->width, enc->height);
        }
    }
}

//#define HEX_DUMP

#ifdef HEX_DUMP
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
#endif

/*
 * The following code is the main loop of the file converter
 */
static int av_encode(AVFormatContext **output_files,
                     int nb_output_files,
                     AVFormatContext **input_files,
                     int nb_input_files,
                     AVStreamMap *stream_maps, int nb_stream_maps)
{
    int ret, i, j, k, n, nb_istreams, nb_ostreams = 0;
    AVFormatContext *is, *os;
    AVCodecContext *codec, *icodec;
    AVOutputStream *ost, **ost_table = NULL;
    AVInputStream *ist, **ist_table = NULL;
    INT64 min_pts, start_time;
    AVInputFile file_table[nb_input_files];

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
        return -ENOMEM;
    
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
            } else {
                if (fifo_init(&ost->fifo, 2 * MAX_AUDIO_PACKET_SIZE))
                    goto fail;

                if (codec->channels == icodec->channels &&
                    codec->sample_rate == icodec->sample_rate) {
                    ost->audio_resample = 0;
                } else {
                    ost->audio_resample = 1;
                    ost->resample = audio_resample_init(codec->channels, icodec->channels,
                                                        codec->sample_rate, 
                                                        icodec->sample_rate);
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
        os->format->write_header(os);
    }

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

        /* select the input file with the smallest pts */
    redo:
        file_index = -1;
        min_pts = (1ULL << 63) - 1;
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

#ifdef HEX_DUMP
        printf("stream #%d, size=%d:\n", pkt.stream_index, pkt.size);
        hex_dump(pkt.data, pkt.size);
#endif

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
                    if (ist->st->codec.codec_id == CODEC_ID_PCM) {
                        /* no need to call a codec */
                        data_buf = ptr;
                        data_size = len;
                        ret = len;
                    } else {
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
                    }
                    break;
                case CODEC_TYPE_VIDEO:
                    if (ist->st->codec.codec_id == CODEC_ID_RAWVIDEO) {
                        int size;
                        size = (ist->st->codec.width * ist->st->codec.height);
                        
                        picture.data[0] = ptr;
                        picture.data[1] = picture.data[0] + size;
                        picture.data[2] = picture.data[1] + size / 4;
                        picture.linesize[0] = ist->st->codec.width;
                        picture.linesize[1] = ist->st->codec.width / 2;
                        picture.linesize[2] = ist->st->codec.width / 2;
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
                ost = ost_table[i];
                if (ost->source_index == ist_index) {
                    os = output_files[ost->file_index];

                    if (ost->encoding_needed) {
                        switch(ost->st->codec.codec_type) {
                        case CODEC_TYPE_AUDIO:
                            do_audio_out(os, ost, ist, data_buf, data_size);
                            break;
                        case CODEC_TYPE_VIDEO:
                            do_video_out(os, ost, ist, &picture);
                            break;
                        }
                    } else {
                        /* no reencoding needed : output the packet directly */
                        os->format->write_packet(os, ost->index, data_buf, data_size);
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
                ti = (1ULL << 63) - 1;
                vid = 0;
                for(i=0;i<nb_ostreams;i++) {
                    ost = ost_table[i];
                    enc = &ost->st->codec;
                    ist = ist_table[ost->source_index];
                    if (!vid && enc->codec_type == CODEC_TYPE_VIDEO) {
                        frame_number = ist->frame_number;
                        sprintf(buf + strlen(buf), "frame=%5d q=%2d ",
                                frame_number, enc->quality);
                        vid = 1;
                    }
                    /* compute min pts value */
                    if (!ist->discard && ist->pts < ti) {
                        ti = ist->pts;
                    }
                }

                ti1 = ti / 1000000.0;
                if (ti1 < 0.1)
                    ti1 = 0.1;
                bitrate = (double)(total_size * 8) / ti1 / 1000.0;

                sprintf(buf + strlen(buf), "size=%8LdkB time=%0.1f bitrate=%6.1fkbits/s", 
                        total_size / 1024, ti1, bitrate);
                
                fprintf(stderr, "%s    \r", buf);
                fflush(stderr);
            }
        }
    }

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
        ti = (1ULL << 63) - 1;
        vid = 0;
        for(i=0;i<nb_ostreams;i++) {
            ost = ost_table[i];
            enc = &ost->st->codec;
            ist = ist_table[ost->source_index];
            if (!vid && enc->codec_type == CODEC_TYPE_VIDEO) {
                frame_number = ist->frame_number;
                sprintf(buf + strlen(buf), "frame=%5d q=%2d ",
                        frame_number, enc->quality);
                vid = 1;
            }
            /* compute min pts value */
            if (!ist->discard && ist->pts < ti) {
                ti = ist->pts;
            }
        }
        
        ti1 = ti / 1000000.0;
        if (ti1 < 0.1)
            ti1 = 0.1;
        bitrate = (double)(total_size * 8) / ti1 / 1000.0;
        
        sprintf(buf + strlen(buf), "size=%8LdkB time=%0.1f bitrate=%6.1fkbits/s", 
                total_size / 1024, ti1, bitrate);
        
        fprintf(stderr, "%s    \n", buf);
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

void opt_qscale(const char *arg)
{
    video_qscale = atoi(arg);
    if (video_qscale < 0 ||
        video_qscale > 31) {
        fprintf(stderr, "qscale must be >= 1 and <= 31\n");
        exit(1);
    }
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

void opt_video_device(const char *arg)
{
    v4l_device = strdup(arg);
}

void opt_audio_device(const char *arg)
{
    audio_device = strdup(arg);
}

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
    motion_estimation_method = p - motion_str;
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

void opt_output_file(const char *filename)
{
    AVStream *st;
    AVFormatContext *oc;
    int use_video, use_audio, nb_streams;
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
            audio_enc->channels = audio_channels;
            oc->streams[nb_streams] = st;
            nb_streams++;
        }

        oc->nb_streams = nb_streams;

        if (!nb_streams) {
            fprintf(stderr, "No audio or video selected\n");
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
        if ((pp - motion_str) == ME_ZERO) 
            printf("(fastest)");
        else if ((pp - motion_str) == ME_FULL) 
            printf("(slowest)");
        else if ((pp - motion_str) == ME_LOG) 
            printf("(default)");
        pp++;
    }
    printf("\n");
    exit(1);
}

void show_help(void)
{
    const OptionDef *po;
    int i, expert;

    printf("ffmpeg version " FFMPEG_VERSION ", Copyright (c) 2000,2001 Gerard Lantau\n"
           "usage: ffmpeg [[options] -i input_file]... {[options] outfile}...\n"
           "Hyper fast MPEG1/MPEG4/H263/RV and AC3/MPEG audio encoder\n"
           "\n"
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
    { "L", 0, {show_licence}, "show license" },
    { "h", 0, {show_help}, "show help" },
    { "formats", 0, {show_formats}, "show available formats, codecs, protocols, ..." },
    { "f", HAS_ARG, {opt_format}, "force format", "fmt" },
    { "i", HAS_ARG, {opt_input_file}, "input file name", "filename" },
    { "y", OPT_BOOL, {int_arg:&file_overwrite}, "overwrite output files" },
    { "map", HAS_ARG | OPT_EXPERT, {opt_map}, "set input stream mapping", "file:stream" },
    { "t", HAS_ARG, {opt_recording_time}, "set the recording time", "duration" },
    { "title", HAS_ARG | OPT_STRING, {str_arg: &str_title}, "set the title", "string" },
    { "author", HAS_ARG | OPT_STRING, {str_arg: &str_author}, "set the author", "string" },
    { "copyright", HAS_ARG | OPT_STRING, {str_arg: &str_copyright}, "set the copyright", "string" },
    { "comment", HAS_ARG | OPT_STRING, {str_arg: &str_comment}, "set the comment", "string" },
    /* video options */
    { "b", HAS_ARG, {opt_video_bitrate}, "set video bitrate (in kbit/s)", "bitrate" },
    { "r", HAS_ARG, {opt_frame_rate}, "set frame rate (in Hz)", "rate" },
    { "s", HAS_ARG, {opt_frame_size}, "set frame size (WxH or abbreviation)", "size" },
    { "g", HAS_ARG | OPT_EXPERT, {opt_gop_size}, "set the group of picture size", "gop_size" },
    { "intra", OPT_BOOL | OPT_EXPERT, {int_arg: &intra_only}, "use only intra frames"},
    { "vn", OPT_BOOL, {int_arg: &video_disable}, "disable video" },
    { "qscale", HAS_ARG | OPT_EXPERT, {opt_qscale}, "use fixed video quantiser scale (VBR)", "q" },
    { "vd", HAS_ARG | OPT_EXPERT, {opt_video_device}, "set video device", "device" },
    { "vcodec", HAS_ARG | OPT_EXPERT, {opt_video_codec}, "force video codec", "codec" },
    { "me", HAS_ARG | OPT_EXPERT, {opt_motion_estimation}, "set motion estimation method", 
      "method" },
    { "sameq", OPT_BOOL, {int_arg: &same_quality}, 
      "use same video quality as source (implies VBR)" },
    /* audio options */
    { "ab", HAS_ARG, {opt_audio_bitrate}, "set audio bitrate (in kbit/s)", "bitrate", },
    { "ar", HAS_ARG, {opt_audio_rate}, "set audio sampling rate (in Hz)", "rate" },
    { "ac", HAS_ARG, {opt_audio_channels}, "set number of audio channels", "channels" },
    { "an", OPT_BOOL, {int_arg: &audio_disable}, "disable audio" },
    { "ad", HAS_ARG | OPT_EXPERT, {opt_audio_device}, "set audio device", "device" },
    { "acodec", HAS_ARG | OPT_EXPERT, {opt_audio_codec}, "force audio codec", "codec" },

    { NULL, },
};

int main(int argc, char **argv)
{
    int optindex, i;
    const char *opt, *arg;
    const OptionDef *po;
    
    register_all();

    if (argc <= 1)
        show_help();

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
            opt_output_file(opt);
        }
    }


    if (nb_input_files == 0) {
        if (nb_output_files != 1) {
            fprintf(stderr, "Only one output file supported when grabbing\n");
            exit(1);
        }
        av_grab(output_files[0]);
    } else {
        if (nb_output_files <= 0) {
            fprintf(stderr, "Must supply at least one output file\n");
            exit(1);
        }
        av_encode(output_files, nb_output_files, input_files, nb_input_files, 
                  stream_maps, nb_stream_maps);
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
