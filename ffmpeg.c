/*
 * FFmpeg main 
 * Copyright (c) 2000-2003 Fabrice Bellard
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
#define HAVE_AV_CONFIG_H
#include "avformat.h"
#include "framehook.h"

#ifndef CONFIG_WIN32
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#include <sys/resource.h>
#include <signal.h>
#endif
#ifdef CONFIG_OS2
#include <sys/types.h>
#include <sys/select.h>
#include <stdlib.h>
#endif
#include <time.h>

#include "cmdutils.h"

#if !defined(INFINITY) && defined(HUGE_VAL)
#define INFINITY HUGE_VAL
#endif

/* select an input stream for an output stream */
typedef struct AVStreamMap {
    int file_index;
    int stream_index;
} AVStreamMap;

extern const OptionDef options[];

static void show_help(void);
static void show_license(void);

#define MAX_FILES 20

static AVFormatContext *input_files[MAX_FILES];
static int nb_input_files = 0;

static AVFormatContext *output_files[MAX_FILES];
static int nb_output_files = 0;

static AVStreamMap stream_maps[MAX_FILES];
static int nb_stream_maps;

static AVInputFormat *file_iformat;
static AVOutputFormat *file_oformat;
static AVImageFormat *image_format;
static int frame_width  = 160;
static int frame_height = 128;
static float frame_aspect_ratio = 0;
static enum PixelFormat frame_pix_fmt = PIX_FMT_YUV420P;
static int frame_topBand  = 0;
static int frame_bottomBand = 0;
static int frame_leftBand  = 0;
static int frame_rightBand = 0;
static int frame_rate = 25;
static int frame_rate_base = 1;
static int video_bit_rate = 200*1000;
static int video_bit_rate_tolerance = 4000*1000;
static float video_qscale = 0;
static int video_qmin = 2;
static int video_qmax = 31;
static int video_mb_qmin = 2;
static int video_mb_qmax = 31;
static int video_qdiff = 3;
static float video_qblur = 0.5;
static float video_qcomp = 0.5;
static uint16_t *intra_matrix = NULL;
static uint16_t *inter_matrix = NULL;
#if 0 //experimental, (can be removed)
static float video_rc_qsquish=1.0;
static float video_rc_qmod_amp=0;
static int video_rc_qmod_freq=0;
#endif
static char *video_rc_override_string=NULL;
static char *video_rc_eq="tex^qComp";
static int video_rc_buffer_size=0;
static float video_rc_buffer_aggressivity=1.0;
static int video_rc_max_rate=0;
static int video_rc_min_rate=0;
static float video_rc_initial_cplx=0;
static float video_b_qfactor = 1.25;
static float video_b_qoffset = 1.25;
static float video_i_qfactor = -0.8;
static float video_i_qoffset = 0.0;
static int me_method = ME_EPZS;
static int video_disable = 0;
static int video_codec_id = CODEC_ID_NONE;
static int same_quality = 0;
static int b_frames = 0;
static int mb_decision = FF_MB_DECISION_SIMPLE;
static int use_4mv = 0;
static int use_aic = 0;
static int use_aiv = 0;
static int use_umv = 0;
static int do_deinterlace = 0;
static int do_interlace = 0;
static int workaround_bugs = FF_BUG_AUTODETECT;
static int error_resilience = 2;
static int error_concealment = 3;
static int dct_algo = 0;
static int idct_algo = 0;
static int use_part = 0;
static int packet_size = 0;
static int strict = 0;
static int debug = 0;
extern int loop_input; /* currently a hack */

static int gop_size = 12;
static int intra_only = 0;
static int audio_sample_rate = 44100;
static int audio_bit_rate = 64000;
static int audio_disable = 0;
static int audio_channels = 1;
static int audio_codec_id = CODEC_ID_NONE;

static int64_t recording_time = 0;
static int64_t start_time = 0;
static int file_overwrite = 0;
static char *str_title = NULL;
static char *str_author = NULL;
static char *str_copyright = NULL;
static char *str_comment = NULL;
static int do_benchmark = 0;
static int do_hex_dump = 0;
static int do_pkt_dump = 0;
static int do_psnr = 0;
static int do_vstats = 0;
static int do_pass = 0;
static int bitexact = 0;
static char *pass_logfilename = NULL;
static int audio_stream_copy = 0;
static int video_stream_copy = 0;

static int rate_emu = 0;

static char *video_grab_format = "video4linux";
static char *video_device = NULL;
static int  video_channel = 0;
static char *video_standard = "ntsc";

static char *audio_grab_format = "audio_device";
static char *audio_device = NULL;

static int using_stdin = 0;
static int using_vhook = 0;
static int verbose = 1;

#define DEFAULT_PASS_LOGFILENAME "ffmpeg2pass"

typedef struct AVOutputStream {
    int file_index;          /* file index */
    int index;               /* stream index in the output file */
    int source_index;        /* AVInputStream index */
    AVStream *st;            /* stream in the output file */
    int encoding_needed;     /* true if encoding needed for this stream */
    int frame_number;
    /* input pts and corresponding output pts
       for A/V sync */
    double sync_ipts;
    double sync_ipts_offset;
    int64_t sync_opts;
    /* video only */
    int video_resample;      /* video_resample and video_crop are mutually exclusive */
    AVPicture pict_tmp;      /* temporary image for resampling */
    ImgReSampleContext *img_resample_ctx; /* for image resampling */

    int video_crop;          /* video_resample and video_crop are mutually exclusive */
    int topBand;             /* cropping area sizes */
    int leftBand;
    
    /* audio only */
    int audio_resample;
    ReSampleContext *resample; /* for audio resampling */
    FifoBuffer fifo;     /* for compression: one audio fifo per codec */
    FILE *logfile;
} AVOutputStream;

typedef struct AVInputStream {
    int file_index;
    int index;
    AVStream *st;
    int discard;             /* true if stream data should be discarded */
    int decoding_needed;     /* true if the packets must be decoded in 'raw_fifo' */
    int64_t sample_index;      /* current sample */

    int64_t       start;     /* time when read started */
    unsigned long frame;     /* current frame */
    int64_t       next_pts;  /* synthetic pts for cases where pkt.pts
                                is not defined */
    int64_t       pts;       /* current pts */
} AVInputStream;

typedef struct AVInputFile {
    int eof_reached;      /* true if eof reached */
    int ist_index;        /* index of first stream in ist_table */
    int buffer_size;      /* current total buffer size */
    int buffer_size_max;  /* buffer size at which we consider we can stop
                             buffering */
    int nb_streams;       /* nb streams we are aware of */
} AVInputFile;

#ifndef CONFIG_WIN32

/* init terminal so that we can grab keys */
static struct termios oldtty;

static void term_exit(void)
{
    tcsetattr (0, TCSANOW, &oldtty);
}

static volatile sig_atomic_t received_sigterm = 0;

static void
sigterm_handler(int sig)
{
    received_sigterm = sig;
    term_exit();
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

    signal(SIGINT , sigterm_handler); /* Interrupt (ANSI).  */
    signal(SIGQUIT, sigterm_handler); /* Quit (POSIX).  */
    signal(SIGTERM, sigterm_handler); /* Termination (ANSI).  */
    /*
    register a function to be called at normal program termination
    */
    atexit(term_exit);
#ifdef CONFIG_BEOS_NETSERVER
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
#endif
}

/* read a key without blocking */
static int read_key(void)
{
    int n = 1;
    unsigned char ch;
#ifndef CONFIG_BEOS_NETSERVER
    struct timeval tv;
    fd_set rfds;

    FD_ZERO(&rfds);
    FD_SET(0, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    n = select(1, &rfds, NULL, NULL, &tv);
#endif
    if (n > 0) {
        n = read(0, &ch, 1);
        if (n == 1)
            return ch;

        return n;
    }
    return -1;
}

#else

static volatile int received_sigterm = 0;

/* no interactive support */
static void term_exit(void)
{
}

static void term_init(void)
{
}

static int read_key(void)
{
    return 0;
}

#endif

static int read_ffserver_streams(AVFormatContext *s, const char *filename)
{
    int i, err;
    AVFormatContext *ic;

    err = av_open_input_file(&ic, filename, NULL, FFM_PACKET_SIZE, NULL);
    if (err < 0)
        return err;
    /* copy stream format */
    s->nb_streams = ic->nb_streams;
    for(i=0;i<ic->nb_streams;i++) {
        AVStream *st;

        st = av_mallocz(sizeof(AVStream));
        memcpy(st, ic->streams[i], sizeof(AVStream));
        s->streams[i] = st;
    }

    av_close_input_file(ic);
    return 0;
}

#define MAX_AUDIO_PACKET_SIZE (128 * 1024)

static void do_audio_out(AVFormatContext *s, 
                         AVOutputStream *ost, 
                         AVInputStream *ist,
                         unsigned char *buf, int size)
{
    uint8_t *buftmp;
    static uint8_t *audio_buf = NULL;
    static uint8_t *audio_out = NULL;
    const int audio_out_size= 4*MAX_AUDIO_PACKET_SIZE;

    int size_out, frame_bytes, ret;
    AVCodecContext *enc;

    /* SC: dynamic allocation of buffers */
    if (!audio_buf)
        audio_buf = av_malloc(2*MAX_AUDIO_PACKET_SIZE);
    if (!audio_out)
        audio_out = av_malloc(audio_out_size);
    if (!audio_buf || !audio_out)
        return;               /* Should signal an error ! */

    
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
            ret = avcodec_encode_audio(enc, audio_out, audio_out_size, 
                                       (short *)audio_buf);
            av_write_frame(s, ost->index, audio_out, ret);
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
        av_write_frame(s, ost->index, audio_out, ret);
    }
}

static void pre_process_video_frame(AVInputStream *ist, AVPicture *picture, void **bufp)
{
    AVCodecContext *dec;
    AVPicture *picture2;
    AVPicture picture_tmp;
    uint8_t *buf = 0;

    dec = &ist->st->codec;

    /* deinterlace : must be done before any resize */
    if (do_deinterlace || using_vhook) {
        int size;

        /* create temporary picture */
        size = avpicture_get_size(dec->pix_fmt, dec->width, dec->height);
        buf = av_malloc(size);
        if (!buf)
            return;
        
        picture2 = &picture_tmp;
        avpicture_fill(picture2, buf, dec->pix_fmt, dec->width, dec->height);

        if (do_deinterlace){
            if(avpicture_deinterlace(picture2, picture, 
                                     dec->pix_fmt, dec->width, dec->height) < 0) {
                /* if error, do not deinterlace */
                av_free(buf);
                buf = NULL;
                picture2 = picture;
            }
        } else {
            if (img_convert(picture2, dec->pix_fmt, picture, 
                            dec->pix_fmt, dec->width, dec->height) < 0) {
                /* if error, do not copy */
                av_free(buf);
                buf = NULL;
                picture2 = picture;
            }
        }
    } else {
        picture2 = picture;
    }

    frame_hook_process(picture2, dec->pix_fmt, dec->width, dec->height);

    if (picture != picture2)
        *picture = *picture2;
    *bufp = buf;
}

/* we begin to correct av delay at this threshold */
#define AV_DELAY_MAX 0.100

static void do_video_out(AVFormatContext *s, 
                         AVOutputStream *ost, 
                         AVInputStream *ist,
                         AVPicture *in_picture,
                         int *frame_size, AVOutputStream *audio_sync)
{
    int nb_frames, i, ret;
    AVPicture *final_picture, *formatted_picture;
    AVPicture picture_format_temp, picture_crop_temp;
    static uint8_t *video_buffer;
    uint8_t *buf = NULL, *buf1 = NULL;
    AVCodecContext *enc, *dec;
    enum PixelFormat target_pixfmt;

#define VIDEO_BUFFER_SIZE (1024*1024)

    enc = &ost->st->codec;
    dec = &ist->st->codec;

    /* by default, we output a single frame */
    nb_frames = 1;

    *frame_size = 0;

    /* NOTE: the A/V sync is always done by considering the audio is
       the master clock. It is suffisant for transcoding or playing,
       but not for the general case */
    if (audio_sync) {
        /* compute the A-V delay and duplicate/remove frames if needed */
        double adelta, vdelta, av_delay;

        adelta = audio_sync->sync_ipts - ((double)audio_sync->sync_opts * 
            s->pts_num / s->pts_den);

        vdelta = ost->sync_ipts - ((double)ost->sync_opts *
            s->pts_num / s->pts_den);

        av_delay = adelta - vdelta;
        //            printf("delay=%f\n", av_delay);
        if (av_delay < -AV_DELAY_MAX)
            nb_frames = 2;
        else if (av_delay > AV_DELAY_MAX)
            nb_frames = 0;
    } else {
        double vdelta;

        vdelta = (double)(ost->st->pts.val) * s->pts_num / s->pts_den - (ost->sync_ipts - ost->sync_ipts_offset);
        if (vdelta < 100 && vdelta > -100 && ost->sync_ipts_offset) {
            if (vdelta < -AV_DELAY_MAX)
                nb_frames = 2;
            else if (vdelta > AV_DELAY_MAX)
                nb_frames = 0;
        } else {
            ost->sync_ipts_offset -= vdelta;
            if (!ost->sync_ipts_offset)
                ost->sync_ipts_offset = 0.000001; /* one microsecond */
        }
    }
    
#if defined(AVSYNC_DEBUG)
    {
        static char *action[] = { "drop frame", "copy frame", "dup frame" };
        if (audio_sync)
            fprintf(stderr, "Input APTS %12.6f, output APTS %12.6f, ",
                    (double) audio_sync->sync_ipts, 
                    (double) audio_sync->st->pts.val * s->pts_num / s->pts_den);
        fprintf(stderr, "Input VPTS %12.6f, output VPTS %12.6f: %s\n",
                (double) ost->sync_ipts, 
                (double) ost->st->pts.val * s->pts_num / s->pts_den,
                action[nb_frames]);
    }
#endif

    if (nb_frames <= 0) 
        return;

    if (!video_buffer)
        video_buffer = av_malloc(VIDEO_BUFFER_SIZE);
    if (!video_buffer)
        return;

    /* convert pixel format if needed */
    target_pixfmt = ost->video_resample ? PIX_FMT_YUV420P : enc->pix_fmt;
    if (dec->pix_fmt != target_pixfmt) {
        int size;

        /* create temporary picture */
        size = avpicture_get_size(target_pixfmt, dec->width, dec->height);
        buf = av_malloc(size);
        if (!buf)
            return;
        formatted_picture = &picture_format_temp;
        avpicture_fill(formatted_picture, buf, target_pixfmt, dec->width, dec->height);
        
        if (img_convert(formatted_picture, target_pixfmt, 
                        in_picture, dec->pix_fmt, 
                        dec->width, dec->height) < 0) {
            fprintf(stderr, "pixel format conversion not handled\n");
            goto the_end;
        }
    } else {
        formatted_picture = in_picture;
    }

    /* XXX: resampling could be done before raw format convertion in
       some cases to go faster */
    /* XXX: only works for YUV420P */
    if (ost->video_resample) {
        final_picture = &ost->pict_tmp;
        img_resample(ost->img_resample_ctx, final_picture, formatted_picture);
	if (enc->pix_fmt != PIX_FMT_YUV420P) {
            int size;
	    
	    av_free(buf);
            /* create temporary picture */
            size = avpicture_get_size(enc->pix_fmt, enc->width, enc->height);
            buf = av_malloc(size);
            if (!buf)
                return;
            final_picture = &picture_format_temp;
            avpicture_fill(final_picture, buf, enc->pix_fmt, enc->width, enc->height);
        
            if (img_convert(final_picture, enc->pix_fmt, 
                            &ost->pict_tmp, PIX_FMT_YUV420P, 
                            enc->width, enc->height) < 0) {
                fprintf(stderr, "pixel format conversion not handled\n");
                goto the_end;
            }
	}
    } else if (ost->video_crop) {
        picture_crop_temp.data[0] = formatted_picture->data[0] +
                (ost->topBand * formatted_picture->linesize[0]) + ost->leftBand;

        picture_crop_temp.data[1] = formatted_picture->data[1] +
                ((ost->topBand >> 1) * formatted_picture->linesize[1]) +
                (ost->leftBand >> 1);

        picture_crop_temp.data[2] = formatted_picture->data[2] +
                ((ost->topBand >> 1) * formatted_picture->linesize[2]) +
                (ost->leftBand >> 1);

        picture_crop_temp.linesize[0] = formatted_picture->linesize[0];
        picture_crop_temp.linesize[1] = formatted_picture->linesize[1];
        picture_crop_temp.linesize[2] = formatted_picture->linesize[2];
        final_picture = &picture_crop_temp;
    } else {
        final_picture = formatted_picture;
    }
    /* duplicates frame if needed */
    /* XXX: pb because no interleaving */
    for(i=0;i<nb_frames;i++) {
        if (s->oformat->flags & AVFMT_RAWPICTURE) {
            /* raw pictures are written as AVPicture structure to
               avoid any copies. We support temorarily the older
               method. */
            AVFrame* old_frame = enc->coded_frame;
	    enc->coded_frame = dec->coded_frame;
            av_write_frame(s, ost->index, 
                           (uint8_t *)final_picture, sizeof(AVPicture));
	    enc->coded_frame = old_frame;
        } else {
            AVFrame big_picture;
            
            memset(&big_picture, 0, sizeof(AVFrame));
            *(AVPicture*)&big_picture= *final_picture;
                        
            /* handles sameq here. This is not correct because it may
               not be a global option */
            if (same_quality) {
                big_picture.quality = ist->st->quality;
            }else
                big_picture.quality = ost->st->quality;
            
            ret = avcodec_encode_video(enc, 
                                       video_buffer, VIDEO_BUFFER_SIZE,
                                       &big_picture);
            //enc->frame_number = enc->real_pict_num;
            av_write_frame(s, ost->index, video_buffer, ret);
            *frame_size = ret;
            //fprintf(stderr,"\nFrame: %3d %3d size: %5d type: %d",
            //        enc->frame_number-1, enc->real_pict_num, ret,
            //        enc->pict_type);
            /* if two pass, output log */
            if (ost->logfile && enc->stats_out) {
                fprintf(ost->logfile, "%s", enc->stats_out);
            }
        }
        ost->frame_number++;
    }
 the_end:
    av_free(buf);
    av_free(buf1);
}

static double psnr(double d){
    if(d==0) return INFINITY;
    return -10.0*log(d)/log(10.0);
}

static void do_video_stats(AVFormatContext *os, AVOutputStream *ost, 
                           int frame_size)
{
    static FILE *fvstats=NULL;
    static int64_t total_size = 0;
    char filename[40];
    time_t today2;
    struct tm *today;
    AVCodecContext *enc;
    int frame_number;
    int64_t ti;
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
        frame_number = ost->frame_number;
        fprintf(fvstats, "frame= %5d q= %2.1f ", frame_number, enc->coded_frame->quality/(float)FF_QP2LAMBDA);
        if (enc->flags&CODEC_FLAG_PSNR)
            fprintf(fvstats, "PSNR= %6.2f ", psnr(enc->coded_frame->error[0]/(enc->width*enc->height*255.0*255.0)));
        
        fprintf(fvstats,"f_size= %6d ", frame_size);
        /* compute pts value */
        ti1 = (double)ost->st->pts.val * os->pts_num / os->pts_den;
        if (ti1 < 0.01)
            ti1 = 0.01;
    
        bitrate = (double)(frame_size * 8) * enc->frame_rate / enc->frame_rate_base / 1000.0;
        avg_bitrate = (double)(total_size * 8) / ti1 / 1000.0;
        fprintf(fvstats, "s_size= %8.0fkB time= %0.3f br= %7.1fkbits/s avg_br= %7.1fkbits/s ",
            (double)total_size / 1024, ti1, bitrate, avg_bitrate);
        fprintf(fvstats,"type= %c\n", av_get_pict_type_char(enc->coded_frame->pict_type));        
    }
}

static void print_report(AVFormatContext **output_files,
			 AVOutputStream **ost_table, int nb_ostreams,
			 int is_last_report)
{
    char buf[1024];
    AVOutputStream *ost;
    AVFormatContext *oc, *os;
    int64_t total_size;
    AVCodecContext *enc;
    int frame_number, vid, i;
    double bitrate, ti1, pts;
    static int64_t last_time = -1;
    
    if (!is_last_report) {
        int64_t cur_time;
        /* display the report every 0.5 seconds */
        cur_time = av_gettime();
        if (last_time == -1) {
            last_time = cur_time;
            return;
        } 
        if ((cur_time - last_time) < 500000)
            return;
        last_time = cur_time;
    }


    oc = output_files[0];

    total_size = url_ftell(&oc->pb);
    
    buf[0] = '\0';
    ti1 = 1e10;
    vid = 0;
    for(i=0;i<nb_ostreams;i++) {
        ost = ost_table[i];
        os = output_files[ost->file_index];
        enc = &ost->st->codec;
        if (vid && enc->codec_type == CODEC_TYPE_VIDEO) {
            sprintf(buf + strlen(buf), "q=%2.1f ",
                    enc->coded_frame->quality/(float)FF_QP2LAMBDA);
        }
        if (!vid && enc->codec_type == CODEC_TYPE_VIDEO) {
            frame_number = ost->frame_number;
            sprintf(buf + strlen(buf), "frame=%5d q=%2.1f ",
                    frame_number, enc->coded_frame ? enc->coded_frame->quality/(float)FF_QP2LAMBDA : 0);
            if (enc->flags&CODEC_FLAG_PSNR)
                sprintf(buf + strlen(buf), "PSNR= %6.2f ", psnr(enc->coded_frame->error[0]/(enc->width*enc->height*255.0*255.0)));
            vid = 1;
        }
        /* compute min output value */
        pts = (double)ost->st->pts.val * os->pts_num / os->pts_den;
        if ((pts < ti1) && (pts > 0))
            ti1 = pts;
    }
    if (ti1 < 0.01)
        ti1 = 0.01;
    
    if (verbose || is_last_report) {
        bitrate = (double)(total_size * 8) / ti1 / 1000.0;
        
        sprintf(buf + strlen(buf), 
            "size=%8.0fkB time=%0.1f bitrate=%6.1fkbits/s",
            (double)total_size / 1024, ti1, bitrate);
        
        fprintf(stderr, "%s    \r", buf);
        fflush(stderr);
    }
        
    if (is_last_report)
        fprintf(stderr, "\n");
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
    AVInputFile *file_table;
    AVFormatContext *stream_no_data;
    int key;

    file_table= (AVInputFile*) av_mallocz(nb_input_files * sizeof(AVInputFile));
    if (!file_table)
        goto fail;

    /* input stream init */
    j = 0;
    for(i=0;i<nb_input_files;i++) {
        is = input_files[i];
        file_table[i].ist_index = j;
        file_table[i].nb_streams = is->nb_streams;
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

            if (ist->st->codec.rate_emu) {
                ist->start = av_gettime();
                ist->frame = 0;
            }
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

    /* Sanity check the mapping args -- do the input files & streams exist? */
    for(i=0;i<nb_stream_maps;i++) {
        int fi = stream_maps[i].file_index;
        int si = stream_maps[i].stream_index;
        
        if (fi < 0 || fi > nb_input_files - 1 ||
            si < 0 || si > file_table[fi].nb_streams - 1) {
            fprintf(stderr,"Could not find input stream #%d.%d\n", fi, si);
            exit(1);
        }
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
                    
                /* Sanity check that the stream types match */
                if (ist_table[ost->source_index]->st->codec.codec_type != ost->st->codec.codec_type) {
                    fprintf(stderr, "Codec type mismatch for mapping #%d.%d -> #%d.%d\n",
                        stream_maps[n-1].file_index, stream_maps[n-1].stream_index,
                        ost->file_index, ost->index);
                    exit(1);
                }
                
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

    /* for each output stream, we compute the right encoding parameters */
    for(i=0;i<nb_ostreams;i++) {
        ost = ost_table[i];
        ist = ist_table[ost->source_index];

        codec = &ost->st->codec;
        icodec = &ist->st->codec;

        if (ost->st->stream_copy) {
            /* if stream_copy is selected, no need to decode or encode */
            codec->codec_id = icodec->codec_id;
            codec->codec_type = icodec->codec_type;
            codec->codec_tag = icodec->codec_tag;
            codec->bit_rate = icodec->bit_rate;
            switch(codec->codec_type) {
            case CODEC_TYPE_AUDIO:
                codec->sample_rate = icodec->sample_rate;
                codec->channels = icodec->channels;
                break;
            case CODEC_TYPE_VIDEO:
                codec->frame_rate = icodec->frame_rate;
                codec->frame_rate_base = icodec->frame_rate_base;
                codec->width = icodec->width;
                codec->height = icodec->height;
                break;
            default:
                av_abort();
            }
        } else {
            switch(codec->codec_type) {
            case CODEC_TYPE_AUDIO:
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
                        /* Request specific number of channels */
                        icodec->channels = codec->channels;
                        if (codec->sample_rate == icodec->sample_rate)
                            ost->audio_resample = 0;
                        else {
                            ost->audio_resample = 1;
                            ost->resample = audio_resample_init(codec->channels, icodec->channels,
                                                        codec->sample_rate, 
                                                        icodec->sample_rate);
			    if(!ost->resample)
			      {
				printf("Can't resample.  Aborting.\n");
				av_abort();
			      }
                        }
                        /* Request specific number of channels */
                        icodec->channels = codec->channels;
                    } else {
                        ost->audio_resample = 1; 
                        ost->resample = audio_resample_init(codec->channels, icodec->channels,
                                                        codec->sample_rate, 
                                                        icodec->sample_rate);
			if(!ost->resample)
			  {
			    printf("Can't resample.  Aborting.\n");
			    av_abort();
			  }
                    }
                }
                ist->decoding_needed = 1;
                ost->encoding_needed = 1;
                break;
            case CODEC_TYPE_VIDEO:
                if (codec->width == icodec->width &&
                    codec->height == icodec->height &&
                    frame_topBand == 0 &&
                    frame_bottomBand == 0 &&
                    frame_leftBand == 0 &&
                    frame_rightBand == 0)
                {
                    ost->video_resample = 0;
                    ost->video_crop = 0;
                } else if ((codec->width == icodec->width -
                                (frame_leftBand + frame_rightBand)) &&
                        (codec->height == icodec->height -
                                (frame_topBand  + frame_bottomBand)))
                {
                    ost->video_resample = 0;
                    ost->video_crop = 1;
                    ost->topBand = frame_topBand;
                    ost->leftBand = frame_leftBand;
                } else {
                    uint8_t *buf;
                    ost->video_resample = 1;
                    ost->video_crop = 0; // cropping is handled as part of resample
                    buf = av_malloc((codec->width * codec->height * 3) / 2);
                    if (!buf)
                        goto fail;
                    ost->pict_tmp.data[0] = buf;
                    ost->pict_tmp.data[1] = ost->pict_tmp.data[0] + (codec->width * codec->height);
                    ost->pict_tmp.data[2] = ost->pict_tmp.data[1] + (codec->width * codec->height) / 4;
                    ost->pict_tmp.linesize[0] = codec->width;
                    ost->pict_tmp.linesize[1] = codec->width / 2;
                    ost->pict_tmp.linesize[2] = codec->width / 2;

                    ost->img_resample_ctx = img_resample_full_init( 
                                      ost->st->codec.width, ost->st->codec.height,
                                      ist->st->codec.width, ist->st->codec.height,
                                      frame_topBand, frame_bottomBand,
                                      frame_leftBand, frame_rightBand);
                }
                ost->encoding_needed = 1;
                ist->decoding_needed = 1;
                break;
            default:
                av_abort();
            }
            /* two pass mode */
            if (ost->encoding_needed && 
                (codec->flags & (CODEC_FLAG_PASS1 | CODEC_FLAG_PASS2))) {
                char logfilename[1024];
                FILE *f;
                int size;
                char *logbuffer;
                
                snprintf(logfilename, sizeof(logfilename), "%s-%d.log", 
                         pass_logfilename ? 
                         pass_logfilename : DEFAULT_PASS_LOGFILENAME, i);
                if (codec->flags & CODEC_FLAG_PASS1) {
                    f = fopen(logfilename, "w");
                    if (!f) {
                        perror(logfilename);
                        exit(1);
                    }
                    ost->logfile = f;
                } else {
                    /* read the log file */
                    f = fopen(logfilename, "r");
                    if (!f) {
                        perror(logfilename);
                        exit(1);
                    }
                    fseek(f, 0, SEEK_END);
                    size = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    logbuffer = av_malloc(size + 1);
                    if (!logbuffer) {
                        fprintf(stderr, "Could not allocate log buffer\n");
                        exit(1);
                    }
                    fread(logbuffer, 1, size, f);
                    fclose(f);
                    logbuffer[size] = '\0';
                    codec->stats_in = logbuffer;
                }
            }
        }
    }

    /* dump the file output parameters - cannot be done before in case
       of stream copy */
    for(i=0;i<nb_output_files;i++) {
        dump_format(output_files[i], i, output_files[i]->filename, 1);
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
                fprintf(stderr, "Unsupported codec (id=%d) for input stream #%d.%d\n", 
                        ist->st->codec.codec_id, ist->file_index, ist->index);
                exit(1);
            }
            if (avcodec_open(&ist->st->codec, codec) < 0) {
                fprintf(stderr, "Error while opening codec for input stream #%d.%d\n", 
                        ist->file_index, ist->index);
                exit(1);
            }
            //if (ist->st->codec.codec_type == CODEC_TYPE_VIDEO)
            //    ist->st->codec.flags |= CODEC_FLAG_REPEAT_FIELD;
        }
    }

    /* init pts */
    for(i=0;i<nb_istreams;i++) {
        ist = ist_table[i];
	is = input_files[ist->file_index];
        ist->pts = 0;
        ist->next_pts = 0;
    }
    
    /* compute buffer size max (should use a complete heuristic) */
    for(i=0;i<nb_input_files;i++) {
        file_table[i].buffer_size_max = 2048;
    }

    /* open files and write file headers */
    for(i=0;i<nb_output_files;i++) {
        os = output_files[i];
        if (av_write_header(os) < 0) {
            fprintf(stderr, "Could not write header for output file #%d (incorrect codec paramters ?)\n", i);
            ret = -EINVAL;
            goto fail;
        }
    }

#ifndef CONFIG_WIN32
    if ( !using_stdin )
        fprintf(stderr, "Press [q] to stop encoding\n");
#endif
    term_init();

    stream_no_data = 0;
    key = -1;

    for(; received_sigterm == 0;) {
        int file_index, ist_index;
        AVPacket pkt;
        uint8_t *ptr;
        int len;
        uint8_t *data_buf;
        int data_size, got_picture;
        AVPicture picture;
        short samples[AVCODEC_MAX_AUDIO_FRAME_SIZE / 2];
        void *buffer_to_free;
        double pts_min;
        
    redo:
        /* if 'q' pressed, exits */
        if (!using_stdin) {
            /* read_key() returns 0 on EOF */
            key = read_key();
            if (key == 'q')
                break;
        }

        /* select the stream that we must read now by looking at the
           smallest output pts */
        file_index = -1;
        pts_min = 1e10;
        for(i=0;i<nb_ostreams;i++) {
            double pts;
            ost = ost_table[i];
            os = output_files[ost->file_index];
            ist = ist_table[ost->source_index];
            pts = (double)ost->st->pts.val * os->pts_num / os->pts_den;
            if (!file_table[ist->file_index].eof_reached && 
                pts < pts_min) {
                pts_min = pts;
                file_index = ist->file_index;
            }
        }
        /* if none, if is finished */
        if (file_index < 0) {
            break;
        }

        /* finish if recording time exhausted */
        if (recording_time > 0 && pts_min >= (recording_time / 1000000.0))
            break;

        /* read a frame from it and output it in the fifo */
        is = input_files[file_index];
        if (av_read_frame(is, &pkt) < 0) {
            file_table[file_index].eof_reached = 1;
            continue;
        }
        if (!pkt.size) {
            stream_no_data = is;
        } else {
            stream_no_data = 0;
        }
        if (do_pkt_dump) {
            av_pkt_dump(stdout, &pkt, do_hex_dump);
        }
        /* the following test is needed in case new streams appear
           dynamically in stream : we ignore them */
        if (pkt.stream_index >= file_table[file_index].nb_streams)
            goto discard_packet;
        ist_index = file_table[file_index].ist_index + pkt.stream_index;
        ist = ist_table[ist_index];
        if (ist->discard)
            goto discard_packet;

        //fprintf(stderr,"read #%d.%d size=%d\n", ist->file_index, ist->index, pkt.size);
        
        if (pkt.pts != AV_NOPTS_VALUE) {
            ist->pts = pkt.pts;
        } else {
            ist->pts = ist->next_pts;
        }

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
                    ptr += ret;
                    len -= ret;
                    /* Some bug in mpeg audio decoder gives */
                    /* data_size < 0, it seems they are overflows */
                    if (data_size <= 0) {
                        /* no audio frame */
                        continue;
                    }
                    data_buf = (uint8_t *)samples;
                    ist->next_pts += ((int64_t)AV_TIME_BASE * data_size) / 
                        (2 * ist->st->codec.channels);
                    break;
                case CODEC_TYPE_VIDEO:
                    {
                        AVFrame big_picture;

                        data_size = (ist->st->codec.width * ist->st->codec.height * 3) / 2;
                        ret = avcodec_decode_video(&ist->st->codec, 
                                                   &big_picture, &got_picture, ptr, len);
                        picture= *(AVPicture*)&big_picture;
                        ist->st->quality= big_picture.quality;
                        if (ret < 0) {
                        fail_decode:
                            fprintf(stderr, "Error while decoding stream #%d.%d\n",
                                    ist->file_index, ist->index);
                            av_free_packet(&pkt);
                            goto redo;
                        }
                        if (!got_picture) {
                            /* no picture yet */
                            goto discard_packet;
                        }
                        if (ist->st->codec.frame_rate_base != 0) {
                            ist->next_pts += ((int64_t)AV_TIME_BASE * 
                                ist->st->codec.frame_rate_base) /
                                ist->st->codec.frame_rate;
                        }
                        len = 0;
                    }
                    break;
                default:
                    goto fail_decode;
                }
            } else {
                data_buf = ptr;
                data_size = len;
                ret = len;
                len = 0;
            }

            buffer_to_free = NULL;
            if (ist->st->codec.codec_type == CODEC_TYPE_VIDEO) {
                pre_process_video_frame(ist, &picture, &buffer_to_free);
            }

            /* frame rate emulation */
            if (ist->st->codec.rate_emu) {
                int64_t pts = av_rescale((int64_t) ist->frame * ist->st->codec.frame_rate_base, 1000000, ist->st->codec.frame_rate);
                int64_t now = av_gettime() - ist->start;
                if (pts > now)
                    usleep(pts - now);

                ist->frame++;
            }

#if 0
            /* mpeg PTS deordering : if it is a P or I frame, the PTS
               is the one of the next displayed one */
            /* XXX: add mpeg4 too ? */
            if (ist->st->codec.codec_id == CODEC_ID_MPEG1VIDEO) {
                if (ist->st->codec.pict_type != B_TYPE) {
                    int64_t tmp;
                    tmp = ist->last_ip_pts;
                    ist->last_ip_pts  = ist->frac_pts.val;
                    ist->frac_pts.val = tmp;
                }
            }
#endif
            /* if output time reached then transcode raw format, 
	       encode packets and output them */
            if (start_time == 0 || ist->pts >= start_time)
                for(i=0;i<nb_ostreams;i++) {
                    int frame_size;

                    ost = ost_table[i];
                    if (ost->source_index == ist_index) {
                        os = output_files[ost->file_index];

#if 0
                        printf("%d: got pts=%0.3f %0.3f\n", i, 
                               (double)pkt.pts / AV_TIME_BASE, 
                               ((double)ist->pts / AV_TIME_BASE) - 
                               ((double)ost->st->pts.val * os->pts_num / os->pts_den));
#endif
                        /* set the input output pts pairs */
                        ost->sync_ipts = (double)ist->pts / AV_TIME_BASE;
                        /* XXX: take into account the various fifos,
                           in particular for audio */
                        ost->sync_opts = ost->st->pts.val;
                        //printf("ipts=%lld sync_ipts=%f sync_opts=%lld pts.val=%lld pkt.pts=%lld\n", ist->pts, ost->sync_ipts, ost->sync_opts, ost->st->pts.val, pkt.pts); 

                        if (ost->encoding_needed) {
                            switch(ost->st->codec.codec_type) {
                            case CODEC_TYPE_AUDIO:
                                do_audio_out(os, ost, ist, data_buf, data_size);
                                break;
                            case CODEC_TYPE_VIDEO:
                                /* find an audio stream for synchro */
                                {
                                    int i;
                                    AVOutputStream *audio_sync, *ost1;
                                    audio_sync = NULL;
                                    for(i=0;i<nb_ostreams;i++) {
                                        ost1 = ost_table[i];
                                        if (ost1->file_index == ost->file_index &&
                                            ost1->st->codec.codec_type == CODEC_TYPE_AUDIO) {
                                            audio_sync = ost1;
                                            break;
                                        }
                                    }

                                    do_video_out(os, ost, ist, &picture, &frame_size, audio_sync);
                                    if (do_vstats && frame_size)
                                        do_video_stats(os, ost, frame_size);
                                }
                                break;
                            default:
                                av_abort();
                            }
                        } else {
                            AVFrame avframe;
                                                
                            /* no reencoding needed : output the packet directly */
                            /* force the input stream PTS */
                        
                            memset(&avframe, 0, sizeof(AVFrame));
                            ost->st->codec.coded_frame= &avframe;
                            avframe.key_frame = pkt.flags & PKT_FLAG_KEY; 
                        
                            av_write_frame(os, ost->index, data_buf, data_size);
                            ost->st->codec.frame_number++;
                            ost->frame_number++;
                        }
                    }
                }
            av_free(buffer_to_free);
        }
    discard_packet:
        av_free_packet(&pkt);
        
        /* dump report by using the output first video and audio streams */
        print_report(output_files, ost_table, nb_ostreams, 0);
    }
    term_exit();

    /* dump report by using the first video and audio streams */
    print_report(output_files, ost_table, nb_ostreams, 1);

    /* close each encoder */
    for(i=0;i<nb_ostreams;i++) {
        ost = ost_table[i];
        if (ost->encoding_needed) {
            av_freep(&ost->st->codec.stats_in);
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
        av_write_trailer(os);
    }
    /* finished ! */
    
    ret = 0;
 fail1:
    av_free(file_table);

    if (ist_table) {
        for(i=0;i<nb_istreams;i++) {
            ist = ist_table[i];
            av_free(ist);
        }
        av_free(ist_table);
    }
    if (ost_table) {
        for(i=0;i<nb_ostreams;i++) {
            ost = ost_table[i];
            if (ost) {
                if (ost->logfile) {
                    fclose(ost->logfile);
                    ost->logfile = NULL;
                }
                fifo_free(&ost->fifo); /* works even if fifo is not
                                          initialized but set to zero */
                av_free(ost->pict_tmp.data[0]);
                if (ost->video_resample)
                    img_resample_close(ost->img_resample_ctx);
                if (ost->audio_resample)
                    audio_resample_close(ost->resample);
                av_free(ost);
            }
        }
        av_free(ost_table);
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

static void opt_image_format(const char *arg)
{
    AVImageFormat *f;
    
    for(f = first_image_format; f != NULL; f = f->next) {
        if (!strcmp(arg, f->name))
            break;
    }
    if (!f) {
        fprintf(stderr, "Unknown image format: '%s'\n", arg);
        exit(1);
    }
    image_format = f;
}

static void opt_format(const char *arg)
{
    /* compatibility stuff for pgmyuv */
    if (!strcmp(arg, "pgmyuv")) {
        opt_image_format(arg);
        arg = "image";
    }

    file_iformat = av_find_input_format(arg);
    file_oformat = guess_format(arg, NULL, NULL);
    if (!file_iformat && !file_oformat) {
        fprintf(stderr, "Unknown input or output format: %s\n", arg);
        exit(1);
    }
}

static void opt_video_bitrate(const char *arg)
{
    video_bit_rate = atoi(arg) * 1000;
}

static void opt_video_bitrate_tolerance(const char *arg)
{
    video_bit_rate_tolerance = atoi(arg) * 1000;
}

static void opt_video_bitrate_max(const char *arg)
{
    video_rc_max_rate = atoi(arg) * 1000;
}

static void opt_video_bitrate_min(const char *arg)
{
    video_rc_min_rate = atoi(arg) * 1000;
}

static void opt_video_buffer_size(const char *arg)
{
    video_rc_buffer_size = atoi(arg) * 1000;
}

static void opt_video_rc_eq(char *arg)
{
    video_rc_eq = arg;
}

static void opt_video_rc_override_string(char *arg)
{
    video_rc_override_string = arg;
}


static void opt_workaround_bugs(const char *arg)
{
    workaround_bugs = atoi(arg);
}

static void opt_dct_algo(const char *arg)
{
    dct_algo = atoi(arg);
}

static void opt_idct_algo(const char *arg)
{
    idct_algo = atoi(arg);
}


static void opt_error_resilience(const char *arg)
{
    error_resilience = atoi(arg);
}

static void opt_error_concealment(const char *arg)
{
    error_concealment = atoi(arg);
}

static void opt_debug(const char *arg)
{
    debug = atoi(arg);
}

static void opt_verbose(const char *arg)
{
    verbose = atoi(arg);
}

static void opt_frame_rate(const char *arg)
{
    if (parse_frame_rate(&frame_rate, &frame_rate_base, arg) < 0) {
        fprintf(stderr, "Incorrect frame rate\n");
	exit(1);
    }
}

static void opt_frame_crop_top(const char *arg)
{
    frame_topBand = atoi(arg); 
    if (frame_topBand < 0) {
        fprintf(stderr, "Incorrect top crop size\n");
        exit(1);
    }
    if ((frame_topBand % 2) != 0) {
        fprintf(stderr, "Top crop size must be a multiple of 2\n");
        exit(1);
    }
    if ((frame_topBand) >= frame_height){
    	fprintf(stderr, "Vertical crop dimensions are outside the range of the original image.\nRemember to crop first and scale second.\n");
        exit(1);
    }
    frame_height -= frame_topBand;
}

static void opt_frame_crop_bottom(const char *arg)
{
    frame_bottomBand = atoi(arg);
    if (frame_bottomBand < 0) {
        fprintf(stderr, "Incorrect bottom crop size\n");
        exit(1);
    }
    if ((frame_bottomBand % 2) != 0) {
        fprintf(stderr, "Bottom crop size must be a multiple of 2\n");
        exit(1);        
    }
    if ((frame_bottomBand) >= frame_height){
    	fprintf(stderr, "Vertical crop dimensions are outside the range of the original image.\nRemember to crop first and scale second.\n");
        exit(1);
    }
    frame_height -= frame_bottomBand;
}

static void opt_frame_crop_left(const char *arg)
{
    frame_leftBand = atoi(arg);
    if (frame_leftBand < 0) {
        fprintf(stderr, "Incorrect left crop size\n");
        exit(1);
    }
    if ((frame_leftBand % 2) != 0) {
        fprintf(stderr, "Left crop size must be a multiple of 2\n");
        exit(1);
    }
    if ((frame_leftBand) >= frame_width){
    	fprintf(stderr, "Horizontal crop dimensions are outside the range of the original image.\nRemember to crop first and scale second.\n");
        exit(1);
    }
    frame_width -= frame_leftBand;
}

static void opt_frame_crop_right(const char *arg)
{
    frame_rightBand = atoi(arg);
    if (frame_rightBand < 0) {
        fprintf(stderr, "Incorrect right crop size\n");
        exit(1);
    }
    if ((frame_rightBand % 2) != 0) {
        fprintf(stderr, "Right crop size must be a multiple of 2\n");
        exit(1);        
    }
    if ((frame_rightBand) >= frame_width){
    	fprintf(stderr, "Horizontal crop dimensions are outside the range of the original image.\nRemember to crop first and scale second.\n");
        exit(1);
    }
    frame_width -= frame_rightBand;
}

static void opt_frame_size(const char *arg)
{
    if (parse_image_size(&frame_width, &frame_height, arg) < 0) {
        fprintf(stderr, "Incorrect frame size\n");
        exit(1);
    }
    if ((frame_width % 2) != 0 || (frame_height % 2) != 0) {
        fprintf(stderr, "Frame size must be a multiple of 2\n");
        exit(1);
    }
}

static void opt_frame_pix_fmt(const char *arg)
{
    frame_pix_fmt = avcodec_get_pix_fmt(arg);
}

static void opt_frame_aspect_ratio(const char *arg)
{
    int x = 0, y = 0;
    double ar = 0;
    const char *p;
    
    p = strchr(arg, ':');
    if (p) {
        x = strtol(arg, (char **)&arg, 10);
	if (arg == p)
	    y = strtol(arg+1, (char **)&arg, 10);
	if (x > 0 && y > 0)
	    ar = (double)x / (double)y;
    } else
        ar = strtod(arg, (char **)&arg);

    if (!ar) {
        fprintf(stderr, "Incorrect aspect ratio specification.\n");
	exit(1);
    }
    frame_aspect_ratio = ar;
}

static void opt_gop_size(const char *arg)
{
    gop_size = atoi(arg);
}

static void opt_b_frames(const char *arg)
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

static void opt_mb_decision(const char *arg)
{
    mb_decision = atoi(arg);
}

static void opt_qscale(const char *arg)
{
    video_qscale = atof(arg);
    if (video_qscale < 0.01 ||
        video_qscale > 255) {
        fprintf(stderr, "qscale must be >= 0.01 and <= 255\n");
        exit(1);
    }
}

static void opt_qmin(const char *arg)
{
    video_qmin = atoi(arg);
    if (video_qmin < 0 ||
        video_qmin > 31) {
        fprintf(stderr, "qmin must be >= 1 and <= 31\n");
        exit(1);
    }
}

static void opt_qmax(const char *arg)
{
    video_qmax = atoi(arg);
    if (video_qmax < 0 ||
        video_qmax > 31) {
        fprintf(stderr, "qmax must be >= 1 and <= 31\n");
        exit(1);
    }
}

static void opt_mb_qmin(const char *arg)
{
    video_mb_qmin = atoi(arg);
    if (video_mb_qmin < 0 ||
        video_mb_qmin > 31) {
        fprintf(stderr, "qmin must be >= 1 and <= 31\n");
        exit(1);
    }
}

static void opt_mb_qmax(const char *arg)
{
    video_mb_qmax = atoi(arg);
    if (video_mb_qmax < 0 ||
        video_mb_qmax > 31) {
        fprintf(stderr, "qmax must be >= 1 and <= 31\n");
        exit(1);
    }
}

static void opt_qdiff(const char *arg)
{
    video_qdiff = atoi(arg);
    if (video_qdiff < 0 ||
        video_qdiff > 31) {
        fprintf(stderr, "qdiff must be >= 1 and <= 31\n");
        exit(1);
    }
}

static void opt_qblur(const char *arg)
{
    video_qblur = atof(arg);
}

static void opt_qcomp(const char *arg)
{
    video_qcomp = atof(arg);
}

static void opt_rc_initial_cplx(const char *arg)
{
    video_rc_initial_cplx = atof(arg);
}
static void opt_b_qfactor(const char *arg)
{
    video_b_qfactor = atof(arg);
}
static void opt_i_qfactor(const char *arg)
{
    video_i_qfactor = atof(arg);
}
static void opt_b_qoffset(const char *arg)
{
    video_b_qoffset = atof(arg);
}
static void opt_i_qoffset(const char *arg)
{
    video_i_qoffset = atof(arg);
}

static void opt_packet_size(const char *arg)
{
    packet_size= atoi(arg);
}

static void opt_strict(const char *arg)
{
    strict= atoi(arg);
}

static void opt_audio_bitrate(const char *arg)
{
    audio_bit_rate = atoi(arg) * 1000;
}

static void opt_audio_rate(const char *arg)
{
    audio_sample_rate = atoi(arg);
}

static void opt_audio_channels(const char *arg)
{
    audio_channels = atoi(arg);
}

static void opt_video_device(const char *arg)
{
    video_device = av_strdup(arg);
}

static void opt_video_channel(const char *arg)
{
    video_channel = strtol(arg, NULL, 0);
}

static void opt_video_standard(const char *arg)
{
    video_standard = av_strdup(arg);
}

static void opt_audio_device(const char *arg)
{
    audio_device = av_strdup(arg);
}

static void opt_dv1394(const char *arg)
{
    video_grab_format = "dv1394";
    audio_grab_format = NULL;
}

static void opt_audio_codec(const char *arg)
{
    AVCodec *p;

    if (!strcmp(arg, "copy")) {
        audio_stream_copy = 1;
    } else {
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
}

static void add_frame_hooker(const char *arg)
{
    int argc = 0;
    char *argv[64];
    int i;
    char *args = av_strdup(arg);

    using_vhook = 1;

    argv[0] = strtok(args, " ");
    while (argc < 62 && (argv[++argc] = strtok(NULL, " "))) {
    }

    i = frame_hook_add(argc, argv);

    if (i != 0) {
        fprintf(stderr, "Failed to add video hook function: %s\n", arg);
        exit(1);
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

static void opt_motion_estimation(const char *arg)
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

static void opt_video_codec(const char *arg)
{
    AVCodec *p;

    if (!strcmp(arg, "copy")) {
        video_stream_copy = 1;
    } else {
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
}

static void opt_map(const char *arg)
{
    AVStreamMap *m;
    const char *p;

    p = arg;
    m = &stream_maps[nb_stream_maps++];

    m->file_index = strtol(arg, (char **)&p, 0);
    if (*p)
        p++;

    m->stream_index = strtol(p, (char **)&p, 0);
}

static void opt_recording_time(const char *arg)
{
    recording_time = parse_date(arg, 1);
}

static void opt_start_time(const char *arg)
{
    start_time = parse_date(arg, 1);
}

static void opt_input_file(const char *filename)
{
    AVFormatContext *ic;
    AVFormatParameters params, *ap = &params;
    int err, i, ret, rfps, rfps_base;

    if (!strcmp(filename, "-"))
        filename = "pipe:";

    using_stdin |= !strcmp(filename, "pipe:" ) || 
                   !strcmp( filename, "/dev/stdin" );

    /* get default parameters from command line */
    memset(ap, 0, sizeof(*ap));
    ap->sample_rate = audio_sample_rate;
    ap->channels = audio_channels;
    ap->frame_rate = frame_rate;
    ap->frame_rate_base = frame_rate_base;
    ap->width = frame_width;
    ap->height = frame_height;
    ap->image_format = image_format;
    ap->pix_fmt = frame_pix_fmt;

    /* open the input file with generic libav function */
    err = av_open_input_file(&ic, filename, file_iformat, 0, ap);
    if (err < 0) {
        print_error(filename, err);
        exit(1);
    }
    
    /* If not enough info to get the stream parameters, we decode the
       first frames to get it. (used in mpeg case for example) */
    ret = av_find_stream_info(ic);
    if (ret < 0) {
        fprintf(stderr, "%s: could not find codec parameters\n", filename);
        exit(1);
    }

    /* if seeking requested, we execute it */
    if (start_time != 0) {
        int64_t timestamp;

        timestamp = start_time;
        /* add the stream start time */
        if (ic->start_time != AV_NOPTS_VALUE)
            timestamp += ic->start_time;
        ret = av_seek_frame(ic, -1, timestamp);
        if (ret < 0) {
            fprintf(stderr, "%s: could not seek to position %0.3f\n", 
                    filename, (double)timestamp / AV_TIME_BASE);
        }
        /* reset seek info */
        start_time = 0;
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
	    frame_aspect_ratio = av_q2d(enc->sample_aspect_ratio) * enc->width / enc->height;
	    frame_pix_fmt = enc->pix_fmt;
            rfps      = ic->streams[i]->r_frame_rate;
            rfps_base = ic->streams[i]->r_frame_rate_base;
            enc->workaround_bugs = workaround_bugs;
            enc->error_resilience = error_resilience; 
            enc->error_concealment = error_concealment; 
            enc->idct_algo= idct_algo;
            enc->debug= debug;
            if(bitexact)
                enc->flags|= CODEC_FLAG_BITEXACT;

            assert(enc->frame_rate_base == rfps_base); // should be true for now
            if (enc->frame_rate != rfps) { 
                fprintf(stderr,"\nSeems that stream %d comes from film source: %2.2f->%2.2f\n",
                    i, (float)enc->frame_rate / enc->frame_rate_base,
                    (float)rfps / rfps_base);
            }
            /* update the current frame rate to match the stream frame rate */
            frame_rate      = rfps;
            frame_rate_base = rfps_base;

            enc->rate_emu = rate_emu;
            break;
        case CODEC_TYPE_DATA:
            break;
        default:
            av_abort();
        }
    }
    
    input_files[nb_input_files] = ic;
    /* dump the file content */
    dump_format(ic, nb_input_files, filename, 0);
    nb_input_files++;
    file_iformat = NULL;
    file_oformat = NULL;
    image_format = NULL;

    rate_emu = 0;
}

static void check_audio_video_inputs(int *has_video_ptr, int *has_audio_ptr)
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
            default:
                av_abort();
            }
        }
    }
    *has_video_ptr = has_video;
    *has_audio_ptr = has_audio;
}

static void opt_output_file(const char *filename)
{
    AVStream *st;
    AVFormatContext *oc;
    int use_video, use_audio, nb_streams, input_has_video, input_has_audio;
    int codec_id;
    AVFormatParameters params, *ap = &params;

    if (!strcmp(filename, "-"))
        filename = "pipe:";

    oc = av_mallocz(sizeof(AVFormatContext));

    if (!file_oformat) {
        file_oformat = guess_format(NULL, filename, NULL);
        if (!file_oformat) {
            fprintf(stderr, "Unable for find a suitable output format for '%s'\n",
                    filename);
            exit(1);
        }
    }
    
    oc->oformat = file_oformat;

    if (!strcmp(file_oformat->name, "ffm") && 
        strstart(filename, "http:", NULL)) {
        /* special case for files sent to ffserver: we get the stream
           parameters from ffserver */
        if (read_ffserver_streams(oc, filename) < 0) {
            fprintf(stderr, "Could not read stream parameters from '%s'\n", filename);
            exit(1);
        }
    } else {
        use_video = file_oformat->video_codec != CODEC_ID_NONE;
        use_audio = file_oformat->audio_codec != CODEC_ID_NONE;

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
            avcodec_get_context_defaults(&st->codec);

            video_enc = &st->codec;
            
            if(!strcmp(file_oformat->name, "mp4") || !strcmp(file_oformat->name, "mov") || !strcmp(file_oformat->name, "3gp"))
                video_enc->flags |= CODEC_FLAG_GLOBAL_HEADER;
            if (video_stream_copy) {
                st->stream_copy = 1;
                video_enc->codec_type = CODEC_TYPE_VIDEO;
            } else {
                char *p;
                int i;
            
                codec_id = file_oformat->video_codec;
                if (video_codec_id != CODEC_ID_NONE)
                    codec_id = video_codec_id;
                
                video_enc->codec_id = codec_id;
                
                video_enc->bit_rate = video_bit_rate;
                video_enc->bit_rate_tolerance = video_bit_rate_tolerance;
                video_enc->frame_rate = frame_rate; 
                video_enc->frame_rate_base = frame_rate_base; 
                
                video_enc->width = frame_width;
                video_enc->height = frame_height;
		video_enc->sample_aspect_ratio = av_d2q(frame_aspect_ratio*frame_height/frame_width, 255);
		video_enc->pix_fmt = frame_pix_fmt;

                if (!intra_only)
                    video_enc->gop_size = gop_size;
                else
                    video_enc->gop_size = 0;
                if (video_qscale || same_quality) {
                    video_enc->flags |= CODEC_FLAG_QSCALE;
                    st->quality = FF_QP2LAMBDA * video_qscale;
                }

                if(intra_matrix)
                    video_enc->intra_matrix = intra_matrix;
                if(inter_matrix)
                    video_enc->inter_matrix = inter_matrix;

                if(bitexact)
                    video_enc->flags |= CODEC_FLAG_BITEXACT;

                video_enc->mb_decision = mb_decision;
                
                if (use_umv) {
                    video_enc->flags |= CODEC_FLAG_H263P_UMV;
                }
           	if (use_aic) {
                    video_enc->flags |= CODEC_FLAG_H263P_AIC;
                }
           	if (use_aiv) {
                    video_enc->flags |= CODEC_FLAG_H263P_AIV;
                }
                if (use_4mv) {
                    video_enc->flags |= CODEC_FLAG_4MV;
                }
                if(use_part) {
                    video_enc->flags |= CODEC_FLAG_PART;
                }
                if (b_frames) {
                    video_enc->max_b_frames = b_frames;
                    video_enc->b_frame_strategy = 0;
                    video_enc->b_quant_factor = 2.0;
                }
                if (do_interlace) {
                    video_enc->flags |= CODEC_FLAG_INTERLACED_DCT;
                }
                video_enc->qmin = video_qmin;
                video_enc->qmax = video_qmax;
                video_enc->mb_qmin = video_mb_qmin;
                video_enc->mb_qmax = video_mb_qmax;
                video_enc->max_qdiff = video_qdiff;
                video_enc->qblur = video_qblur;
                video_enc->qcompress = video_qcomp;
                video_enc->rc_eq = video_rc_eq;
                video_enc->debug= debug;
                
                p= video_rc_override_string;
                for(i=0; p; i++){
                    int start, end, q;
                    int e=sscanf(p, "%d,%d,%d", &start, &end, &q);
                    if(e!=3){
                        fprintf(stderr, "error parsing rc_override\n");
                        exit(1);
                    }
                    video_enc->rc_override= 
                        av_realloc(video_enc->rc_override, 
                                   sizeof(RcOverride)*(i+1));
                    video_enc->rc_override[i].start_frame= start;
                    video_enc->rc_override[i].end_frame  = end;
                    if(q>0){
                        video_enc->rc_override[i].qscale= q;
                        video_enc->rc_override[i].quality_factor= 1.0;
                    }
                    else{
                        video_enc->rc_override[i].qscale= 0;
                        video_enc->rc_override[i].quality_factor= -q/100.0;
                    }
                    p= strchr(p, '/');
                    if(p) p++;
                }
                video_enc->rc_override_count=i;

                video_enc->rc_max_rate = video_rc_max_rate;
                video_enc->rc_min_rate = video_rc_min_rate;
                video_enc->rc_buffer_size = video_rc_buffer_size;
                video_enc->rc_buffer_aggressivity= video_rc_buffer_aggressivity;
                video_enc->rc_initial_cplx= video_rc_initial_cplx;
                video_enc->i_quant_factor = video_i_qfactor;
                video_enc->b_quant_factor = video_b_qfactor;
                video_enc->i_quant_offset = video_i_qoffset;
                video_enc->b_quant_offset = video_b_qoffset;
                video_enc->dct_algo = dct_algo;
                video_enc->idct_algo = idct_algo;
                video_enc->strict_std_compliance = strict;
                if(packet_size){
                    video_enc->rtp_mode= 1;
                    video_enc->rtp_payload_size= packet_size;
                }
            
                if (do_psnr)
                    video_enc->flags|= CODEC_FLAG_PSNR;
            
                video_enc->me_method = me_method;

                /* two pass mode */
                if (do_pass) {
                    if (do_pass == 1) {
                        video_enc->flags |= CODEC_FLAG_PASS1;
                    } else {
                        video_enc->flags |= CODEC_FLAG_PASS2;
                    }
                }
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
            avcodec_get_context_defaults(&st->codec);

            audio_enc = &st->codec;
            audio_enc->codec_type = CODEC_TYPE_AUDIO;

            if(!strcmp(file_oformat->name, "mp4") || !strcmp(file_oformat->name, "mov") || !strcmp(file_oformat->name, "3gp"))
                audio_enc->flags |= CODEC_FLAG_GLOBAL_HEADER;
            if (audio_stream_copy) {
                st->stream_copy = 1;
            } else {
                codec_id = file_oformat->audio_codec;
                if (audio_codec_id != CODEC_ID_NONE)
                    codec_id = audio_codec_id;
                audio_enc->codec_id = codec_id;
                
                audio_enc->bit_rate = audio_bit_rate;
                audio_enc->sample_rate = audio_sample_rate;
                /* For audio codecs other than AC3 we limit */
                /* the number of coded channels to stereo   */
                if (audio_channels > 2 && codec_id != CODEC_ID_AC3) {
                    audio_enc->channels = 2;
                } else
                    audio_enc->channels = audio_channels;
            }
            oc->streams[nb_streams] = st;
            nb_streams++;
        }

        oc->nb_streams = nb_streams;

        if (!nb_streams) {
            fprintf(stderr, "No audio or video streams available\n");
            exit(1);
        }

        if (str_title)
            pstrcpy(oc->title, sizeof(oc->title), str_title);
        if (str_author)
            pstrcpy(oc->author, sizeof(oc->author), str_author);
        if (str_copyright)
            pstrcpy(oc->copyright, sizeof(oc->copyright), str_copyright);
        if (str_comment)
            pstrcpy(oc->comment, sizeof(oc->comment), str_comment);
    }

    output_files[nb_output_files++] = oc;

    strcpy(oc->filename, filename);

    /* check filename in case of an image number is expected */
    if (oc->oformat->flags & AVFMT_NEEDNUMBER) {
        if (filename_number_test(oc->filename) < 0) {
            print_error(oc->filename, AVERROR_NUMEXPECTED);
            exit(1);
        }
    }

    if (!(oc->oformat->flags & AVFMT_NOFILE)) {
        /* test if it already exists to avoid loosing precious files */
        if (!file_overwrite && 
            (strchr(filename, ':') == NULL ||
             strstart(filename, "file:", NULL))) {
            if (url_exist(filename)) {
                int c;
                
                if ( !using_stdin ) {
                    fprintf(stderr,"File '%s' already exists. Overwrite ? [y/N] ", filename);
                    fflush(stderr);
                    c = getchar();
                    if (toupper(c) != 'Y') {
                        fprintf(stderr, "Not overwriting - exiting\n");
                        exit(1);
                    }
				}
				else {
                    fprintf(stderr,"File '%s' already exists. Exiting.\n", filename);
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

    memset(ap, 0, sizeof(*ap));
    ap->image_format = image_format;
    if (av_set_parameters(oc, ap) < 0) {
        fprintf(stderr, "%s: Invalid encoding parameters\n",
                oc->filename);
        exit(1);
    }

    /* reset some options */
    file_oformat = NULL;
    file_iformat = NULL;
    image_format = NULL;
    audio_disable = 0;
    video_disable = 0;
    audio_codec_id = CODEC_ID_NONE;
    video_codec_id = CODEC_ID_NONE;
    audio_stream_copy = 0;
    video_stream_copy = 0;
}

/* prepare dummy protocols for grab */
static void prepare_grab(void)
{
    int has_video, has_audio, i, j;
    AVFormatContext *oc;
    AVFormatContext *ic;
    AVFormatParameters vp1, *vp = &vp1;
    AVFormatParameters ap1, *ap = &ap1;
    
    /* see if audio/video inputs are needed */
    has_video = 0;
    has_audio = 0;
    memset(ap, 0, sizeof(*ap));
    memset(vp, 0, sizeof(*vp));
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
                if (enc->width > vp->width)
                    vp->width = enc->width;
                if (enc->height > vp->height)
                    vp->height = enc->height;
                
                assert(enc->frame_rate_base == DEFAULT_FRAME_RATE_BASE);
                if (enc->frame_rate > vp->frame_rate){
                    vp->frame_rate      = enc->frame_rate;
                    vp->frame_rate_base = enc->frame_rate_base;
                }
                has_video = 1;
                break;
            default:
                av_abort();
            }
        }
    }
    
    if (has_video == 0 && has_audio == 0) {
        fprintf(stderr, "Output file must have at least one audio or video stream\n");
        exit(1);
    }
    
    if (has_video) {
        AVInputFormat *fmt1;
        fmt1 = av_find_input_format(video_grab_format);
        vp->device  = video_device;
        vp->channel = video_channel;
	vp->standard = video_standard;
        if (av_open_input_file(&ic, "", fmt1, 0, vp) < 0) {
            fprintf(stderr, "Could not find video grab device\n");
            exit(1);
        }
        /* If not enough info to get the stream parameters, we decode the
           first frames to get it. */
	if ((ic->ctx_flags & AVFMTCTX_NOHEADER) && av_find_stream_info(ic) < 0) {
            fprintf(stderr, "Could not find video grab parameters\n");
            exit(1);
        }
        /* by now video grab has one stream */
        ic->streams[0]->r_frame_rate      = vp->frame_rate;
        ic->streams[0]->r_frame_rate_base = vp->frame_rate_base;
        input_files[nb_input_files] = ic;
        dump_format(ic, nb_input_files, "", 0);
        nb_input_files++;
    }
    if (has_audio && audio_grab_format) {
        AVInputFormat *fmt1;
        fmt1 = av_find_input_format(audio_grab_format);
        ap->device = audio_device;
        if (av_open_input_file(&ic, "", fmt1, 0, ap) < 0) {
            fprintf(stderr, "Could not find audio grab device\n");
            exit(1);
        }
        input_files[nb_input_files] = ic;
        dump_format(ic, nb_input_files, "", 0);
        nb_input_files++;
    }
}

/* same option as mencoder */
static void opt_pass(const char *pass_str)
{
    int pass;
    pass = atoi(pass_str);
    if (pass != 1 && pass != 2) {
        fprintf(stderr, "pass number can be only 1 or 2\n");
        exit(1);
    }
    do_pass = pass;
}

#if defined(CONFIG_WIN32) || defined(CONFIG_OS2)
static int64_t getutime(void)
{
  return av_gettime();
}
#else
static int64_t getutime(void)
{
    struct rusage rusage;

    getrusage(RUSAGE_SELF, &rusage);
    return (rusage.ru_utime.tv_sec * 1000000LL) + rusage.ru_utime.tv_usec;
}
#endif

extern int ffm_nopts;

static void opt_bitexact(void)
{
    bitexact=1;
    /* disable generate of real time pts in ffm (need to be supressed anyway) */
    ffm_nopts = 1;
}

static void show_formats(void)
{
    AVInputFormat *ifmt;
    AVOutputFormat *ofmt;
    AVImageFormat *image_fmt;
    URLProtocol *up;
    AVCodec *p;
    const char **pp;

    printf("Output audio/video file formats:");
    for(ofmt = first_oformat; ofmt != NULL; ofmt = ofmt->next) {
        printf(" %s", ofmt->name);
    }
    printf("\n");

    printf("Input audio/video file formats:");
    for(ifmt = first_iformat; ifmt != NULL; ifmt = ifmt->next) {
        printf(" %s", ifmt->name);
    }
    printf("\n");

    printf("Output image formats:");
    for(image_fmt = first_image_format; image_fmt != NULL; 
        image_fmt = image_fmt->next) {
        if (image_fmt->img_write)
            printf(" %s", image_fmt->name);
    }
    printf("\n");

    printf("Input image formats:");
    for(image_fmt = first_image_format; image_fmt != NULL; 
        image_fmt = image_fmt->next) {
        if (image_fmt->img_read)
            printf(" %s", image_fmt->name);
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
    
    printf("Frame size, frame rate abbreviations: ntsc pal qntsc qpal sntsc spal film ntsc-film sqcif qcif cif 4cif\n");
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

void parse_matrix_coeffs(uint16_t *dest, const char *str)
{
    int i;
    const char *p = str;
    for(i = 0;; i++) {
        dest[i] = atoi(p);
        if(i == 63)
            break;
        p = strchr(p, ',');
        if(!p) {
            fprintf(stderr, "Syntax error in matrix \"%s\" at coeff %d\n", str, i);
            exit(1);
        }
        p++;
    }
}

void opt_inter_matrix(const char *arg)
{
    inter_matrix = av_mallocz(sizeof(uint16_t) * 64);
    parse_matrix_coeffs(inter_matrix, arg);
}

void opt_intra_matrix(const char *arg)
{
    intra_matrix = av_mallocz(sizeof(uint16_t) * 64);
    parse_matrix_coeffs(intra_matrix, arg);
}

static void opt_target(const char *arg)
{
    int norm = -1;

    if(!strncmp(arg, "pal-", 4)) {
        norm = 0;
        arg += 4;
    } else if(!strncmp(arg, "ntsc-", 5)) {
        norm = 1;
        arg += 5;
    } else {
        int fr;
        /* Calculate FR via float to avoid int overflow */
        fr = (int)(frame_rate * 1000.0 / frame_rate_base);
        if(fr == 25000) {
            norm = 0;
        } else if((fr == 29970) || (fr == 23976)) {
            norm = 1;
        } else {
            /* Try to determine PAL/NTSC by peeking in the input files */
            if(nb_input_files) {
                int i, j;
                for(j = 0; j < nb_input_files; j++) {
                    for(i = 0; i < input_files[j]->nb_streams; i++) {
                        AVCodecContext *c = &input_files[j]->streams[i]->codec;
                        if(c->codec_type != CODEC_TYPE_VIDEO)
                            continue;
                        fr = c->frame_rate * 1000 / c->frame_rate_base;
                        if(fr == 25000) {
                            norm = 0;
                            break;
                        } else if((fr == 29970) || (fr == 23976)) {
                            norm = 1;
                            break;
                        }
                    }
                    if(norm >= 0)
                        break;
                }
            }
        }
        if(verbose && norm >= 0)
            printf("Assuming %s for target.\n", norm ? "NTSC" : "PAL");
    }

    if(norm < 0) {
        fprintf(stderr, "Could not determine norm (PAL/NTSC) for target.\n");
        fprintf(stderr, "Please prefix target with \"pal-\" or \"ntsc-\",\n");
        fprintf(stderr, "or set a framerate with \"-r xxx\".\n");
        exit(1);
    }

    if(!strcmp(arg, "vcd")) {

        opt_video_codec("mpeg1video");
        opt_audio_codec("mp2");
        opt_format("vcd");

        opt_frame_size(norm ? "352x240" : "352x288");

        video_bit_rate = 1150000;
        video_rc_max_rate = 1150000;
        video_rc_min_rate = 1150000;

        audio_bit_rate = 224000;
        audio_sample_rate = 44100;

    } else if(!strcmp(arg, "svcd")) {

        opt_video_codec("mpeg2video");
        opt_audio_codec("mp2");
        opt_format("vcd");

        opt_frame_size(norm ? "480x480" : "480x576");
        opt_gop_size(norm ? "18" : "15");

        video_bit_rate = 2040000;
        video_rc_max_rate = 2516000;
        video_rc_min_rate = 1145000;

        audio_bit_rate = 224000;
        audio_sample_rate = 44100;

    } else if(!strcmp(arg, "dvd")) {

        opt_video_codec("mpeg2video");
        opt_audio_codec("ac3");
        opt_format("vob");

        opt_frame_size(norm ? "720x480" : "720x576");
        opt_gop_size(norm ? "18" : "15");

        video_bit_rate = 6000000;
        video_rc_max_rate = 9000000;
        video_rc_min_rate = 1500000;

        audio_bit_rate = 448000;
        audio_sample_rate = 48000;

    } else {
        fprintf(stderr, "Unknown target: %s\n", arg);
        exit(1);
    }
}

const OptionDef options[] = {
    /* main options */
    { "L", 0, {(void*)show_license}, "show license" },
    { "h", 0, {(void*)show_help}, "show help" },
    { "formats", 0, {(void*)show_formats}, "show available formats, codecs, protocols, ..." },
    { "f", HAS_ARG, {(void*)opt_format}, "force format", "fmt" },
    { "img", HAS_ARG, {(void*)opt_image_format}, "force image format", "img_fmt" },
    { "i", HAS_ARG, {(void*)opt_input_file}, "input file name", "filename" },
    { "y", OPT_BOOL, {(void*)&file_overwrite}, "overwrite output files" },
    { "map", HAS_ARG | OPT_EXPERT, {(void*)opt_map}, "set input stream mapping", "file:stream" },
    { "t", HAS_ARG, {(void*)opt_recording_time}, "set the recording time", "duration" },
    { "ss", HAS_ARG, {(void*)opt_start_time}, "set the start time offset", "time_off" },
    { "title", HAS_ARG | OPT_STRING, {(void*)&str_title}, "set the title", "string" },
    { "author", HAS_ARG | OPT_STRING, {(void*)&str_author}, "set the author", "string" },
    { "copyright", HAS_ARG | OPT_STRING, {(void*)&str_copyright}, "set the copyright", "string" },
    { "comment", HAS_ARG | OPT_STRING, {(void*)&str_comment}, "set the comment", "string" },
    { "debug", HAS_ARG | OPT_EXPERT, {(void*)opt_debug}, "print specific debug info", "" },
    { "benchmark", OPT_BOOL | OPT_EXPERT, {(void*)&do_benchmark}, 
      "add timings for benchmarking" },
    { "dump", OPT_BOOL | OPT_EXPERT, {(void*)&do_pkt_dump}, 
      "dump each input packet" },
    { "hex", OPT_BOOL | OPT_EXPERT, {(void*)&do_hex_dump}, 
      "when dumping packets, also dump the payload" },
    { "bitexact", OPT_EXPERT, {(void*)opt_bitexact}, "only use bit exact algorithms (for codec testing)" }, 
    { "re", OPT_BOOL | OPT_EXPERT, {(void*)&rate_emu}, "read input at native frame rate", "" },
    { "loop", OPT_BOOL | OPT_EXPERT, {(void*)&loop_input}, "loop (current only works with images)" },
    { "v", HAS_ARG, {(void*)opt_verbose}, "control amount of logging", "verbose" },
    { "target", HAS_ARG, {(void*)opt_target}, "specify target file type (\"vcd\", \"svcd\" or \"dvd\")", "type" },

    /* video options */
    { "b", HAS_ARG | OPT_VIDEO, {(void*)opt_video_bitrate}, "set video bitrate (in kbit/s)", "bitrate" },
    { "r", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_rate}, "set frame rate (Hz value, fraction or abbreviation)", "rate" },
    { "s", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_size}, "set frame size (WxH or abbreviation)", "size" },
    { "aspect", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_aspect_ratio}, "set aspect ratio (4:3, 16:9 or 1.3333, 1.7777)", "aspect" },
    { "pix_fmt", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_frame_pix_fmt}, "set pixel format", "format" },
    { "croptop", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_crop_top}, "set top crop band size (in pixels)", "size" },
    { "cropbottom", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_crop_bottom}, "set bottom crop band size (in pixels)", "size" },
    { "cropleft", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_crop_left}, "set left crop band size (in pixels)", "size" },
    { "cropright", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_crop_right}, "set right crop band size (in pixels)", "size" },
    { "g", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_gop_size}, "set the group of picture size", "gop_size" },
    { "intra", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&intra_only}, "use only intra frames"},
    { "vn", OPT_BOOL | OPT_VIDEO, {(void*)&video_disable}, "disable video" },
    { "qscale", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_qscale}, "use fixed video quantiser scale (VBR)", "q" },
    { "qmin", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_qmin}, "min video quantiser scale (VBR)", "q" },
    { "qmax", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_qmax}, "max video quantiser scale (VBR)", "q" },
    { "mbqmin", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_mb_qmin}, "min macroblock quantiser scale (VBR)", "q" },
    { "mbqmax", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_mb_qmax}, "max macroblock quantiser scale (VBR)", "q" },
    { "qdiff", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_qdiff}, "max difference between the quantiser scale (VBR)", "q" },
    { "qblur", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_qblur}, "video quantiser scale blur (VBR)", "blur" },
    { "qcomp", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_qcomp}, "video quantiser scale compression (VBR)", "compression" },
    { "rc_init_cplx", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_rc_initial_cplx}, "initial complexity for 1-pass encoding", "complexity" },
    { "b_qfactor", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_b_qfactor}, "qp factor between p and b frames", "factor" },
    { "i_qfactor", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_i_qfactor}, "qp factor between p and i frames", "factor" },
    { "b_qoffset", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_b_qoffset}, "qp offset between p and b frames", "offset" },
    { "i_qoffset", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_i_qoffset}, "qp offset between p and i frames", "offset" },
//    { "b_strategy", HAS_ARG | OPT_EXPERT, {(void*)opt_b_strategy}, "dynamic b frame selection strategy", "strategy" },
    { "rc_eq", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_video_rc_eq}, "set rate control equation", "equation" },
    { "rc_override", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_video_rc_override_string}, "rate control override for specific intervals", "override" },
    { "bt", HAS_ARG | OPT_VIDEO, {(void*)opt_video_bitrate_tolerance}, "set video bitrate tolerance (in kbit/s)", "tolerance" },
    { "maxrate", HAS_ARG | OPT_VIDEO, {(void*)opt_video_bitrate_max}, "set max video bitrate tolerance (in kbit/s)", "bitrate" },
    { "minrate", HAS_ARG | OPT_VIDEO, {(void*)opt_video_bitrate_min}, "set min video bitrate tolerance (in kbit/s)", "bitrate" },
    { "bufsize", HAS_ARG | OPT_VIDEO, {(void*)opt_video_buffer_size}, "set ratecontrol buffere size (in kbit)", "size" },
    { "vcodec", HAS_ARG | OPT_VIDEO, {(void*)opt_video_codec}, "force video codec ('copy' to copy stream)", "codec" },
    { "me", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_motion_estimation}, "set motion estimation method", 
      "method" },
    { "dct_algo", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_dct_algo}, "set dct algo",  "algo" },
    { "idct_algo", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_idct_algo}, "set idct algo",  "algo" },
    { "er", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_error_resilience}, "set error resilience",  "n" },
    { "ec", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_error_concealment}, "set error concealment",  "bit_mask" },
    { "bf", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_b_frames}, "use 'frames' B frames", "frames" },
    { "hq", OPT_BOOL, {(void*)&mb_decision}, "activate high quality settings" },
    { "mbd", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_mb_decision}, "macroblock decision", "mode" },
    { "4mv", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&use_4mv}, "use four motion vector by macroblock (MPEG4)" },
    { "part", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&use_part}, "use data partitioning (MPEG4)" },
    { "bug", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_workaround_bugs}, "workaround not auto detected encoder bugs", "param" },
    { "ps", HAS_ARG | OPT_EXPERT, {(void*)opt_packet_size}, "set packet size in bits", "size" },
    { "strict", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_strict}, "how strictly to follow the standarts", "strictness" },
    { "sameq", OPT_BOOL | OPT_VIDEO, {(void*)&same_quality}, 
      "use same video quality as source (implies VBR)" },
    { "pass", HAS_ARG | OPT_VIDEO, {(void*)&opt_pass}, "select the pass number (1 or 2)", "n" },
    { "passlogfile", HAS_ARG | OPT_STRING | OPT_VIDEO, {(void*)&pass_logfilename}, "select two pass log file name", "file" },
    { "deinterlace", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&do_deinterlace}, 
      "deinterlace pictures" },
    { "interlace", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&do_interlace}, 
      "force interlacing support in encoder (MPEG4)" },
    { "psnr", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&do_psnr}, "calculate PSNR of compressed frames" },
    { "vstats", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&do_vstats}, "dump video coding statistics to file" }, 
    { "vhook", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)add_frame_hooker}, "insert video processing module", "module" },
    { "aic", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&use_aic}, "enable Advanced intra coding (h263+)" },
    { "aiv", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&use_aiv}, "enable Alternative inter vlc (h263+)" },
    { "umv", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&use_umv}, "enable Unlimited Motion Vector (h263+)" },
    { "intra_matrix", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_intra_matrix}, "specify intra matrix coeffs", "matrix" },
    { "inter_matrix", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_inter_matrix}, "specify inter matrix coeffs", "matrix" },

    /* audio options */
    { "ab", HAS_ARG | OPT_AUDIO, {(void*)opt_audio_bitrate}, "set audio bitrate (in kbit/s)", "bitrate", },
    { "ar", HAS_ARG | OPT_AUDIO, {(void*)opt_audio_rate}, "set audio sampling rate (in Hz)", "rate" },
    { "ac", HAS_ARG | OPT_AUDIO, {(void*)opt_audio_channels}, "set number of audio channels", "channels" },
    { "an", OPT_BOOL | OPT_AUDIO, {(void*)&audio_disable}, "disable audio" },
    { "acodec", HAS_ARG | OPT_AUDIO, {(void*)opt_audio_codec}, "force audio codec ('copy' to copy stream)", "codec" },

    /* grab options */
    { "vd", HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_GRAB, {(void*)opt_video_device}, "set video grab device", "device" },
    { "vc", HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_GRAB, {(void*)opt_video_channel}, "set video grab channel (DV1394 only)", "channel" },
    { "tvstd", HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_GRAB, {(void*)opt_video_standard}, "set television standard (NTSC, PAL (SECAM))", "standard" },
    { "dv1394", OPT_EXPERT | OPT_GRAB, {(void*)opt_dv1394}, "set DV1394 grab", "" },
    { "ad", HAS_ARG | OPT_EXPERT | OPT_AUDIO | OPT_GRAB, {(void*)opt_audio_device}, "set audio device", "device" },
    { NULL, },
};

static void show_banner(void)
{
    printf("ffmpeg version " FFMPEG_VERSION ", Copyright (c) 2000-2003 Fabrice Bellard\n");
}

static void show_license(void)
{
    show_banner();
    printf(
    "This library is free software; you can redistribute it and/or\n"
    "modify it under the terms of the GNU Lesser General Public\n"
    "License as published by the Free Software Foundation; either\n"
    "version 2 of the License, or (at your option) any later version.\n"
    "\n"
    "This library is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU\n"
    "Lesser General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU Lesser General Public\n"
    "License along with this library; if not, write to the Free Software\n"
    "Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA\n"
    );
    exit(1);
}

static void show_help(void)
{
    show_banner();
    printf("usage: ffmpeg [[options] -i input_file]... {[options] outfile}...\n"
           "Hyper fast Audio and Video encoder\n");
    printf("\n");
    show_help_options(options, "Main options:\n",
                      OPT_EXPERT | OPT_AUDIO | OPT_VIDEO, 0);
    show_help_options(options, "\nVideo options:\n",
                      OPT_EXPERT | OPT_AUDIO | OPT_VIDEO | OPT_GRAB, 
                      OPT_VIDEO);
    show_help_options(options, "\nAdvanced Video options:\n",
                      OPT_EXPERT | OPT_AUDIO | OPT_VIDEO | OPT_GRAB, 
                      OPT_VIDEO | OPT_EXPERT);
    show_help_options(options, "\nAudio options:\n",
                      OPT_EXPERT | OPT_AUDIO | OPT_VIDEO | OPT_GRAB, 
                      OPT_AUDIO);
    show_help_options(options, "\nAdvanced Audio options:\n",
                      OPT_EXPERT | OPT_AUDIO | OPT_VIDEO | OPT_GRAB, 
                      OPT_AUDIO | OPT_EXPERT);
    show_help_options(options, "\nAudio/Video grab options:\n",
                      OPT_GRAB, 
                      OPT_GRAB);
    show_help_options(options, "\nAdvanced options:\n",
                      OPT_EXPERT | OPT_AUDIO | OPT_VIDEO | OPT_GRAB, 
                      OPT_EXPERT);
    exit(1);
}

void parse_arg_file(const char *filename)
{
    opt_output_file(filename);
}

int main(int argc, char **argv)
{
    int i;
    int64_t ti;

    av_register_all();

    if (argc <= 1)
        show_help();
    
    /* parse options */
    parse_options(argc, argv, options);

    /* file converter / grab */
    if (nb_output_files <= 0) {
        fprintf(stderr, "Must supply at least one output file\n");
        exit(1);
    }
    
    if (nb_input_files == 0) {
        prepare_grab();
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
        /* maybe av_close_output_file ??? */
        AVFormatContext *s = output_files[i];
	int j;
        if (!(s->oformat->flags & AVFMT_NOFILE))
	    url_fclose(&s->pb);
	for(j=0;j<s->nb_streams;j++)
	    av_free(s->streams[j]);
        av_free(s);
    }
    for(i=0;i<nb_input_files;i++)
        av_close_input_file(input_files[i]);

    av_free_static();

    if(intra_matrix)
        av_free(intra_matrix);
    if(inter_matrix)
        av_free(inter_matrix);
    
#ifdef POWERPC_PERFORMANCE_REPORT
    extern void powerpc_display_perf_report(void);
    powerpc_display_perf_report();
#endif /* POWERPC_PERFORMANCE_REPORT */

#ifndef CONFIG_WIN32
    if (received_sigterm) {
        fprintf(stderr,
            "Received signal %d: terminating.\n",
            (int) received_sigterm);
        exit (255);
    }
#endif
    exit(0); /* not all OS-es handle main() return value */
    return 0;
}
