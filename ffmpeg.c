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
#include <limits.h>
#include "avformat.h"
#include "framehook.h"
#include "dsputil.h"

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
#undef time //needed because HAVE_AV_CONFIG_H is defined on top
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

/** select an input file for an output file */
typedef struct AVMetaDataMap {
    int out_file;
    int in_file;
} AVMetaDataMap;

extern const OptionDef options[];

static void show_help(void);
static void show_license(void);

#define MAX_FILES 20

static AVFormatContext *input_files[MAX_FILES];
static int64_t input_files_ts_offset[MAX_FILES];
static int nb_input_files = 0;

static AVFormatContext *output_files[MAX_FILES];
static int nb_output_files = 0;

static AVStreamMap stream_maps[MAX_FILES];
static int nb_stream_maps;

static AVMetaDataMap meta_data_maps[MAX_FILES];
static int nb_meta_data_maps;

static AVInputFormat *file_iformat;
static AVOutputFormat *file_oformat;
static AVImageFormat *image_format;
static int frame_width  = 160;
static int frame_height = 128;
static float frame_aspect_ratio = 0;
static enum PixelFormat frame_pix_fmt = PIX_FMT_YUV420P;
static int frame_padtop  = 0;
static int frame_padbottom = 0;
static int frame_padleft  = 0;
static int frame_padright = 0;
static int padcolor[3] = {16,128,128}; /* default to black */
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
static int video_lmin = 2*FF_QP2LAMBDA;
static int video_lmax = 31*FF_QP2LAMBDA;
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
static int video_intra_quant_bias= FF_DEFAULT_QUANT_BIAS;
static int video_inter_quant_bias= FF_DEFAULT_QUANT_BIAS;
static int me_method = ME_EPZS;
static int video_disable = 0;
static int video_codec_id = CODEC_ID_NONE;
static int same_quality = 0;
static int b_frames = 0;
static int mb_decision = FF_MB_DECISION_SIMPLE;
static int ildct_cmp = FF_CMP_VSAD;
static int mb_cmp = FF_CMP_SAD;
static int sub_cmp = FF_CMP_SAD;
static int cmp = FF_CMP_SAD;
static int pre_cmp = FF_CMP_SAD;
static int pre_me = 0;
static float lumi_mask = 0;
static float dark_mask = 0;
static float scplx_mask = 0;
static float tcplx_mask = 0;
static float p_mask = 0;
static int use_4mv = 0;
static int use_obmc = 0;
static int use_loop = 0;
static int use_aic = 0;
static int use_aiv = 0;
static int use_umv = 0;
static int use_ss = 0;
static int use_alt_scan = 0;
static int use_trell = 0;
static int use_scan_offset = 0;
static int use_qpel = 0;
static int use_qprd = 0;
static int use_cbprd = 0;
static int qns = 0;
static int closed_gop = 0;
static int do_deinterlace = 0;
static int do_interlace_dct = 0;
static int do_interlace_me = 0;
static int workaround_bugs = FF_BUG_AUTODETECT;
static int error_resilience = 2;
static int error_concealment = 3;
static int dct_algo = 0;
static int idct_algo = 0;
static int use_part = 0;
static int packet_size = 0;
static int error_rate = 0;
static int strict = 0;
static int top_field_first = -1;
static int noise_reduction = 0;
static int sc_threshold = 0;
static int debug = 0;
static int debug_mv = 0;
static int me_threshold = 0;
static int mb_threshold = 0;
static int intra_dc_precision = 8;
static int coder = 0;
static int context = 0;
static int predictor = 0;
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
static int64_t rec_timestamp = 0;
static int64_t input_ts_offset = 0;
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
static int video_sync_method= 1;
static int audio_sync_method= 0;
static int copy_ts= 0;

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
static int thread_count= 1;
static int q_pressed = 0;
static int me_range = 0;
static int64_t video_size = 0;
static int64_t audio_size = 0;
static int64_t extra_size = 0;
static int nb_frames_dup = 0;
static int nb_frames_drop = 0;
static int input_sync;

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
    double sync_ipts;        /* dts from the AVPacket of the demuxer in second units */
    int64_t sync_opts;       /* output frame counter, could be changed to some true timestamp */ //FIXME look at frame_number
    /* video only */
    int video_resample;      /* video_resample and video_crop are mutually exclusive */
    AVFrame pict_tmp;      /* temporary image for resampling */
    ImgReSampleContext *img_resample_ctx; /* for image resampling */

    int video_crop;          /* video_resample and video_crop are mutually exclusive */
    int topBand;             /* cropping area sizes */
    int leftBand;
    
    int video_pad;           /* video_resample and video_pad are mutually exclusive */
    int padtop;              /* padding area sizes */
    int padbottom;
    int padleft;
    int padright;
    
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
    int is_start;            /* is 1 at the start and after a discontinuity */
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

static int decode_interrupt_cb(void)
{
    return q_pressed || (q_pressed = read_key() == 'q');
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
    AVCodecContext *enc= &ost->st->codec;

    /* SC: dynamic allocation of buffers */
    if (!audio_buf)
        audio_buf = av_malloc(2*MAX_AUDIO_PACKET_SIZE);
    if (!audio_out)
        audio_out = av_malloc(audio_out_size);
    if (!audio_buf || !audio_out)
        return;               /* Should signal an error ! */

    if(audio_sync_method){
        double delta = ost->sync_ipts * enc->sample_rate - ost->sync_opts 
                - fifo_size(&ost->fifo, ost->fifo.rptr)/(ost->st->codec.channels * 2);
        double idelta= delta*ist->st->codec.sample_rate / enc->sample_rate;
        int byte_delta= ((int)idelta)*2*ist->st->codec.channels;

        //FIXME resample delay
        if(fabs(delta) > 50){
            if(ist->is_start){
                if(byte_delta < 0){
                    byte_delta= FFMAX(byte_delta, -size);
                    size += byte_delta;
                    buf  -= byte_delta;
                    if(verbose > 2)
                        fprintf(stderr, "discarding %d audio samples\n", (int)-delta);
                    if(!size)
                        return;
                    ist->is_start=0;
                }else{
                    static uint8_t *input_tmp= NULL;
                    input_tmp= av_realloc(input_tmp, byte_delta + size);

                    if(byte_delta + size <= MAX_AUDIO_PACKET_SIZE)
                        ist->is_start=0;
                    else
                        byte_delta= MAX_AUDIO_PACKET_SIZE - size;

                    memset(input_tmp, 0, byte_delta);
                    memcpy(input_tmp + byte_delta, buf, size);
                    buf= input_tmp;
                    size += byte_delta;
                    if(verbose > 2)
                        fprintf(stderr, "adding %d audio samples of silence\n", (int)delta);
                }
            }else if(audio_sync_method>1){
                int comp= clip(delta, -audio_sync_method, audio_sync_method);
                assert(ost->audio_resample);
                if(verbose > 2)
                    fprintf(stderr, "compensating audio timestamp drift:%f compensation:%d in:%d\n", delta, comp, enc->sample_rate);
                fprintf(stderr, "drift:%f len:%d opts:%lld ipts:%lld fifo:%d\n", delta, -1, ost->sync_opts, (int64_t)(ost->sync_ipts * enc->sample_rate), fifo_size(&ost->fifo, ost->fifo.rptr)/(ost->st->codec.channels * 2));
                av_resample_compensate(*(struct AVResampleContext**)ost->resample, comp, enc->sample_rate);
            }
        } 
    }else
        ost->sync_opts= lrintf(ost->sync_ipts * enc->sample_rate)
                        - fifo_size(&ost->fifo, ost->fifo.rptr)/(ost->st->codec.channels * 2); //FIXME wrong

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
            AVPacket pkt;
            av_init_packet(&pkt);

            ret = avcodec_encode_audio(enc, audio_out, audio_out_size, 
                                       (short *)audio_buf);
            audio_size += ret;
            pkt.stream_index= ost->index;
            pkt.data= audio_out;
            pkt.size= ret;
            if(enc->coded_frame)
                pkt.pts= enc->coded_frame->pts;
            pkt.flags |= PKT_FLAG_KEY;
            av_interleaved_write_frame(s, &pkt);
            
            ost->sync_opts += enc->frame_size;
        }
    } else {
        AVPacket pkt;
        av_init_packet(&pkt);

        ost->sync_opts += size_out / (2 * enc->channels);

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
        audio_size += ret;
        pkt.stream_index= ost->index;
        pkt.data= audio_out;
        pkt.size= ret;
        if(enc->coded_frame)
            pkt.pts= enc->coded_frame->pts;
        pkt.flags |= PKT_FLAG_KEY;
        av_interleaved_write_frame(s, &pkt);
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


/* Expects img to be yuv420 */
static void fill_pad_region(AVPicture* img, int height, int width,
        int padtop, int padbottom, int padleft, int padright, int *color) {
  
    int i, y, shift;
    uint8_t *optr;
    
    for (i = 0; i < 3; i++) {
        shift = (i == 0) ? 0 : 1;
        
        if (padtop || padleft) {
            memset(img->data[i], color[i], (((img->linesize[i] * padtop) + 
                            padleft) >> shift));
        }

        if (padleft || padright) {
            optr = img->data[i] + (img->linesize[i] * (padtop >> shift)) +
                (img->linesize[i] - (padright >> shift));

            for (y = 0; y < ((height - (padtop + padbottom)) >> shift); y++) {
                memset(optr, color[i], (padleft + padright) >> shift);
                optr += img->linesize[i];
            }
        }
      
        if (padbottom) {
            optr = img->data[i] + (img->linesize[i] * ((height - padbottom) >> shift));
            memset(optr, color[i], ((img->linesize[i] * padbottom) >> shift));
        }
    }
}

static uint8_t *bit_buffer= NULL;

static void do_video_out(AVFormatContext *s, 
                         AVOutputStream *ost, 
                         AVInputStream *ist,
                         AVFrame *in_picture,
                         int *frame_size)
{
    int nb_frames, i, ret;
    AVFrame *final_picture, *formatted_picture;
    AVFrame picture_format_temp, picture_crop_temp;
    uint8_t *buf = NULL, *buf1 = NULL;
    AVCodecContext *enc, *dec;
    enum PixelFormat target_pixfmt;
    
#define VIDEO_BUFFER_SIZE (1024*1024)

    avcodec_get_frame_defaults(&picture_format_temp);
    avcodec_get_frame_defaults(&picture_crop_temp);

    enc = &ost->st->codec;
    dec = &ist->st->codec;

    /* by default, we output a single frame */
    nb_frames = 1;

    *frame_size = 0;

    if(video_sync_method){
        double vdelta;
        vdelta = ost->sync_ipts * enc->frame_rate / enc->frame_rate_base - ost->sync_opts;
        //FIXME set to 0.5 after we fix some dts/pts bugs like in avidec.c
        if (vdelta < -1.1)
            nb_frames = 0;
        else if (vdelta > 1.1)
            nb_frames = lrintf(vdelta - 1.1 + 0.5);
//fprintf(stderr, "vdelta:%f, ost->sync_opts:%lld, ost->sync_ipts:%f nb_frames:%d\n", vdelta, ost->sync_opts, ost->sync_ipts, nb_frames);
        if (nb_frames == 0){
            ++nb_frames_drop;
            if (verbose>2)
                fprintf(stderr, "*** drop!\n");
        }else if (nb_frames > 1) {
            nb_frames_dup += nb_frames;
            if (verbose>2)
                fprintf(stderr, "*** %d dup!\n", nb_frames-1);
        }
    }else
        ost->sync_opts= lrintf(ost->sync_ipts * enc->frame_rate / enc->frame_rate_base);

    if (nb_frames <= 0) 
        return;

    /* convert pixel format if needed */
    target_pixfmt = ost->video_resample || ost->video_pad
        ? PIX_FMT_YUV420P : enc->pix_fmt;
    if (dec->pix_fmt != target_pixfmt) {
        int size;

        /* create temporary picture */
        size = avpicture_get_size(target_pixfmt, dec->width, dec->height);
        buf = av_malloc(size);
        if (!buf)
            return;
        formatted_picture = &picture_format_temp;
        avpicture_fill((AVPicture*)formatted_picture, buf, target_pixfmt, dec->width, dec->height);
        
        if (img_convert((AVPicture*)formatted_picture, target_pixfmt, 
                        (AVPicture *)in_picture, dec->pix_fmt, 
                        dec->width, dec->height) < 0) {

            if (verbose >= 0)
                fprintf(stderr, "pixel format conversion not handled\n");

            goto the_end;
        }
    } else {
        formatted_picture = in_picture;
    }

    /* XXX: resampling could be done before raw format conversion in
       some cases to go faster */
    /* XXX: only works for YUV420P */
    if (ost->video_resample) {
        final_picture = &ost->pict_tmp;
        img_resample(ost->img_resample_ctx, (AVPicture*)final_picture, (AVPicture*)formatted_picture);
       
        if (ost->padtop || ost->padbottom || ost->padleft || ost->padright) {
            fill_pad_region((AVPicture*)final_picture, enc->height, enc->width,
                    ost->padtop, ost->padbottom, ost->padleft, ost->padright,
                    padcolor);
        }
        
	if (enc->pix_fmt != PIX_FMT_YUV420P) {
            int size;
	    
	    av_free(buf);
            /* create temporary picture */
            size = avpicture_get_size(enc->pix_fmt, enc->width, enc->height);
            buf = av_malloc(size);
            if (!buf)
                return;
            final_picture = &picture_format_temp;
            avpicture_fill((AVPicture*)final_picture, buf, enc->pix_fmt, enc->width, enc->height);
        
            if (img_convert((AVPicture*)final_picture, enc->pix_fmt, 
                            (AVPicture*)&ost->pict_tmp, PIX_FMT_YUV420P, 
                            enc->width, enc->height) < 0) {

                if (verbose >= 0)
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
    } else if (ost->video_pad) {
        final_picture = &ost->pict_tmp;

        for (i = 0; i < 3; i++) {
            uint8_t *optr, *iptr;
            int shift = (i == 0) ? 0 : 1;
            int y, yheight;
            
            /* set offset to start writing image into */
            optr = final_picture->data[i] + (((final_picture->linesize[i] * 
                            ost->padtop) + ost->padleft) >> shift);
            iptr = formatted_picture->data[i];

            yheight = (enc->height - ost->padtop - ost->padbottom) >> shift;
            for (y = 0; y < yheight; y++) {
                /* copy unpadded image row into padded image row */
                memcpy(optr, iptr, formatted_picture->linesize[i]);
                optr += final_picture->linesize[i];
                iptr += formatted_picture->linesize[i];
            }
        }

        fill_pad_region((AVPicture*)final_picture, enc->height, enc->width,
                ost->padtop, ost->padbottom, ost->padleft, ost->padright,
                padcolor);
        
        if (enc->pix_fmt != PIX_FMT_YUV420P) {
            int size;

            av_free(buf);
            /* create temporary picture */
            size = avpicture_get_size(enc->pix_fmt, enc->width, enc->height);
            buf = av_malloc(size);
            if (!buf)
                return;
            final_picture = &picture_format_temp;
            avpicture_fill((AVPicture*)final_picture, buf, enc->pix_fmt, enc->width, enc->height);

            if (img_convert((AVPicture*)final_picture, enc->pix_fmt, 
                        (AVPicture*)&ost->pict_tmp, PIX_FMT_YUV420P, 
                        enc->width, enc->height) < 0) {

                if (verbose >= 0)
                    fprintf(stderr, "pixel format conversion not handled\n");

                goto the_end;
            }
        }
    } else {
        final_picture = formatted_picture;
    }
    /* duplicates frame if needed */
    for(i=0;i<nb_frames;i++) {
        AVPacket pkt;
        av_init_packet(&pkt);
        pkt.stream_index= ost->index;

        if (s->oformat->flags & AVFMT_RAWPICTURE) {
            /* raw pictures are written as AVPicture structure to
               avoid any copies. We support temorarily the older
               method. */
            AVFrame* old_frame = enc->coded_frame;
	    enc->coded_frame = dec->coded_frame; //FIXME/XXX remove this hack
            pkt.data= (uint8_t *)final_picture;
            pkt.size=  sizeof(AVPicture);
            if(dec->coded_frame)
                pkt.pts= dec->coded_frame->pts;
            if(dec->coded_frame && dec->coded_frame->key_frame)
                pkt.flags |= PKT_FLAG_KEY;

            av_interleaved_write_frame(s, &pkt);
	    enc->coded_frame = old_frame;
        } else {
            AVFrame big_picture;

            big_picture= *final_picture;
            /* better than nothing: use input picture interlaced
               settings */
            big_picture.interlaced_frame = in_picture->interlaced_frame;
            if(do_interlace_me || do_interlace_dct){
                if(top_field_first == -1)
                    big_picture.top_field_first = in_picture->top_field_first;
                else
                    big_picture.top_field_first = top_field_first;
            }

            /* handles sameq here. This is not correct because it may
               not be a global option */
            if (same_quality) {
                big_picture.quality = ist->st->quality;
            }else
                big_picture.quality = ost->st->quality;
            if(!me_threshold)
                big_picture.pict_type = 0;
//            big_picture.pts = AV_NOPTS_VALUE;
            big_picture.pts= av_rescale(ost->sync_opts, AV_TIME_BASE*(int64_t)enc->frame_rate_base, enc->frame_rate);
//av_log(NULL, AV_LOG_DEBUG, "%lld -> encoder\n", ost->sync_opts);
            ret = avcodec_encode_video(enc, 
                                       bit_buffer, VIDEO_BUFFER_SIZE,
                                       &big_picture);
            //enc->frame_number = enc->real_pict_num;
            if(ret){
                pkt.data= bit_buffer;
                pkt.size= ret;
                if(enc->coded_frame)
                    pkt.pts= enc->coded_frame->pts;
/*av_log(NULL, AV_LOG_DEBUG, "encoder -> %lld/%lld\n", 
   pkt.pts != AV_NOPTS_VALUE ? av_rescale(pkt.pts, enc->frame_rate, AV_TIME_BASE*(int64_t)enc->frame_rate_base) : -1,
   pkt.dts != AV_NOPTS_VALUE ? av_rescale(pkt.dts, enc->frame_rate, AV_TIME_BASE*(int64_t)enc->frame_rate_base) : -1);*/

                if(enc->coded_frame && enc->coded_frame->key_frame)
                    pkt.flags |= PKT_FLAG_KEY;
                av_interleaved_write_frame(s, &pkt);
                *frame_size = ret;
                //fprintf(stderr,"\nFrame: %3d %3d size: %5d type: %d",
                //        enc->frame_number-1, enc->real_pict_num, ret,
                //        enc->pict_type);
                /* if two pass, output log */
                if (ost->logfile && enc->stats_out) {
                    fprintf(ost->logfile, "%s", enc->stats_out);
                }
            }
        }
        ost->sync_opts++;
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
    if (enc->codec_type == CODEC_TYPE_VIDEO) {
        frame_number = ost->frame_number;
        fprintf(fvstats, "frame= %5d q= %2.1f ", frame_number, enc->coded_frame->quality/(float)FF_QP2LAMBDA);
        if (enc->flags&CODEC_FLAG_PSNR)
            fprintf(fvstats, "PSNR= %6.2f ", psnr(enc->coded_frame->error[0]/(enc->width*enc->height*255.0*255.0)));
        
        fprintf(fvstats,"f_size= %6d ", frame_size);
        /* compute pts value */
        ti1 = (double)ost->sync_opts *enc->frame_rate_base / enc->frame_rate;
        if (ti1 < 0.01)
            ti1 = 0.01;
    
        bitrate = (double)(frame_size * 8) * enc->frame_rate / enc->frame_rate_base / 1000.0;
        avg_bitrate = (double)(video_size * 8) / ti1 / 1000.0;
        fprintf(fvstats, "s_size= %8.0fkB time= %0.3f br= %7.1fkbits/s avg_br= %7.1fkbits/s ",
            (double)video_size / 1024, ti1, bitrate, avg_bitrate);
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
            if(is_last_report)
                sprintf(buf + strlen(buf), "L");
            if (enc->flags&CODEC_FLAG_PSNR){
                int j;
                double error, error_sum=0;
                double scale, scale_sum=0;
                char type[3]= {'Y','U','V'};
                sprintf(buf + strlen(buf), "PSNR=");
                for(j=0; j<3; j++){
                    if(is_last_report){
                        error= enc->error[j];
                        scale= enc->width*enc->height*255.0*255.0*frame_number;
                    }else{
                        error= enc->coded_frame->error[j];
                        scale= enc->width*enc->height*255.0*255.0;
                    }
                    if(j) scale/=4;
                    error_sum += error;
                    scale_sum += scale;
                    sprintf(buf + strlen(buf), "%c:%2.2f ", type[j], psnr(error/scale));
                }
                sprintf(buf + strlen(buf), "*:%2.2f ", psnr(error_sum/scale_sum));
            }
            vid = 1;
        }
        /* compute min output value */
        pts = (double)ost->st->pts.val * ost->st->time_base.num / ost->st->time_base.den;
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

	if (verbose > 1)
	  sprintf(buf + strlen(buf), " dup=%d drop=%d",
		  nb_frames_dup, nb_frames_drop);
        
        if (verbose >= 0)
            fprintf(stderr, "%s    \r", buf);

        fflush(stderr);
    }
        
    if (is_last_report && verbose >= 0){
        int64_t raw= audio_size + video_size + extra_size;
        fprintf(stderr, "\n");
        fprintf(stderr, "video:%1.0fkB audio:%1.0fkB global headers:%1.0fkB muxing overhead %f%%\n",
                video_size/1024.0,
                audio_size/1024.0,
                extra_size/1024.0,
                100.0*(total_size - raw)/raw
        );
    }
}

/* pkt = NULL means EOF (needed to flush decoder buffers) */
static int output_packet(AVInputStream *ist, int ist_index,
                         AVOutputStream **ost_table, int nb_ostreams,
                         const AVPacket *pkt)
{
    AVFormatContext *os;
    AVOutputStream *ost;
    uint8_t *ptr;
    int len, ret, i;
    uint8_t *data_buf;
    int data_size, got_picture;
    AVFrame picture;
    short samples[pkt && pkt->size > AVCODEC_MAX_AUDIO_FRAME_SIZE/2 ? pkt->size : AVCODEC_MAX_AUDIO_FRAME_SIZE/2];
    void *buffer_to_free;

    if (pkt && pkt->dts != AV_NOPTS_VALUE) { //FIXME seems redundant, as libavformat does this too
        ist->next_pts = ist->pts = pkt->dts;
    } else {
        assert(ist->pts == ist->next_pts);
    }
    
    if (pkt == NULL) {
        /* EOF handling */
        ptr = NULL;
        len = 0;
        goto handle_eof;
    }

    len = pkt->size;
    ptr = pkt->data;
    while (len > 0) {
    handle_eof:
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
                ist->next_pts += ((int64_t)AV_TIME_BASE/2 * data_size) / 
                    (ist->st->codec.sample_rate * ist->st->codec.channels);
                break;
            case CODEC_TYPE_VIDEO:
                    data_size = (ist->st->codec.width * ist->st->codec.height * 3) / 2;
                    /* XXX: allocate picture correctly */
                    avcodec_get_frame_defaults(&picture);

                    ret = avcodec_decode_video(&ist->st->codec, 
                                               &picture, &got_picture, ptr, len);
                    ist->st->quality= picture.quality;
                    if (ret < 0) 
                        goto fail_decode;
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
                pre_process_video_frame(ist, (AVPicture *)&picture, 
                                        &buffer_to_free);
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
                               (double)pkt->pts / AV_TIME_BASE, 
                               ((double)ist->pts / AV_TIME_BASE) - 
                               ((double)ost->st->pts.val * ost->st->time_base.num / ost->st->time_base.den));
#endif
                        /* set the input output pts pairs */
                        ost->sync_ipts = (double)(ist->pts + input_files_ts_offset[ist->file_index])/ AV_TIME_BASE;

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

                                    do_video_out(os, ost, ist, &picture, &frame_size);
                                    video_size += frame_size;
                                    if (do_vstats && frame_size)
                                        do_video_stats(os, ost, frame_size);
                                }
                                break;
                            default:
                                av_abort();
                            }
                        } else {
                            AVFrame avframe; //FIXME/XXX remove this
                            AVPacket opkt;
                            av_init_packet(&opkt);

                            /* no reencoding needed : output the packet directly */
                            /* force the input stream PTS */
                        
                            avcodec_get_frame_defaults(&avframe);
                            ost->st->codec.coded_frame= &avframe;
                            avframe.key_frame = pkt->flags & PKT_FLAG_KEY; 

                            if(ost->st->codec.codec_type == CODEC_TYPE_AUDIO)
                                audio_size += data_size;
                            else if (ost->st->codec.codec_type == CODEC_TYPE_VIDEO)
                                video_size += data_size;

                            opkt.stream_index= ost->index;
                            opkt.data= data_buf;
                            opkt.size= data_size;
                            opkt.pts= pkt->pts + input_files_ts_offset[ist->file_index];
                            opkt.dts= pkt->dts + input_files_ts_offset[ist->file_index];
                            opkt.flags= pkt->flags;
                            
                            av_interleaved_write_frame(os, &opkt);
                            ost->st->codec.frame_number++;
                            ost->frame_number++;
                        }
                    }
                }
            av_free(buffer_to_free);
        }
 discard_packet:
    if (pkt == NULL) {
        /* EOF handling */
  
        for(i=0;i<nb_ostreams;i++) {
            ost = ost_table[i];
            if (ost->source_index == ist_index) {
                AVCodecContext *enc= &ost->st->codec;
                os = output_files[ost->file_index];
                
                if(ost->st->codec.codec_type == CODEC_TYPE_AUDIO && enc->frame_size <=1)
                    continue;
                if(ost->st->codec.codec_type == CODEC_TYPE_VIDEO && (os->oformat->flags & AVFMT_RAWPICTURE))
                    continue;

                if (ost->encoding_needed) {
                    for(;;) {
                        AVPacket pkt;
                        av_init_packet(&pkt);
                        pkt.stream_index= ost->index;
 
                        switch(ost->st->codec.codec_type) {
                        case CODEC_TYPE_AUDIO:        
                            ret = avcodec_encode_audio(enc, bit_buffer, VIDEO_BUFFER_SIZE, NULL);
                            audio_size += ret;
                            pkt.flags |= PKT_FLAG_KEY;
                            break;
                        case CODEC_TYPE_VIDEO:
                            ret = avcodec_encode_video(enc, bit_buffer, VIDEO_BUFFER_SIZE, NULL);
                            video_size += ret;
                            if(enc->coded_frame && enc->coded_frame->key_frame)
                                pkt.flags |= PKT_FLAG_KEY;
                            if (ost->logfile && enc->stats_out) {
                                fprintf(ost->logfile, "%s", enc->stats_out);
                            }
                            break;
                        default:
                            ret=-1;
                        }
                            
                        if(ret<=0)
                            break;
                        pkt.data= bit_buffer;
                        pkt.size= ret;
                        if(enc->coded_frame)
                            pkt.pts= enc->coded_frame->pts;
                        av_interleaved_write_frame(os, &pkt);
                    }
                }
            }
        }
    }
 
    return 0;
 fail_decode:
    return -1;
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

    if (!bit_buffer)
        bit_buffer = av_malloc(VIDEO_BUFFER_SIZE);
    if (!bit_buffer)
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
                codec->frame_size = icodec->frame_size;
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
                        (icodec->codec_id == CODEC_ID_AC3 ||
                         icodec->codec_id == CODEC_ID_DTS)) {
                        /* Special case for 5:1 AC3 and DTS input */
                        /* and mono or stereo output      */
                        /* Request specific number of channels */
                        icodec->channels = codec->channels;
                        if (codec->sample_rate == icodec->sample_rate)
                            ost->audio_resample = 0;
                        else {
                            ost->audio_resample = 1;
                        }
                    } else {
                        ost->audio_resample = 1; 
                    }
                }
                if(audio_sync_method>1)
                    ost->audio_resample = 1;

                if(ost->audio_resample){
                    ost->resample = audio_resample_init(codec->channels, icodec->channels,
                                                    codec->sample_rate, icodec->sample_rate);
                    if(!ost->resample){
                        printf("Can't resample.  Aborting.\n");
                        av_abort();
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
                    frame_rightBand == 0 && 
                    frame_padtop == 0 &&
                    frame_padbottom == 0 &&
                    frame_padleft == 0 &&
                    frame_padright == 0)
                {
                    ost->video_resample = 0;
                    ost->video_crop = 0;
                    ost->video_pad = 0;
                } else if ((codec->width == icodec->width -
                                (frame_leftBand + frame_rightBand)) &&
                        (codec->height == icodec->height -
                                (frame_topBand  + frame_bottomBand)))
                {
                    ost->video_resample = 0;
                    ost->video_crop = 1;
                    ost->topBand = frame_topBand;
                    ost->leftBand = frame_leftBand;
                } else if ((codec->width == icodec->width + 
                                (frame_padleft + frame_padright)) &&
                        (codec->height == icodec->height +
                                (frame_padtop + frame_padbottom))) {
                    ost->video_resample = 0;
                    ost->video_crop = 0;
                    ost->video_pad = 1;
                    ost->padtop = frame_padtop;
                    ost->padleft = frame_padleft;
                    ost->padbottom = frame_padbottom;
                    ost->padright = frame_padright;
                    avcodec_get_frame_defaults(&ost->pict_tmp);
                    if( avpicture_alloc( (AVPicture*)&ost->pict_tmp, PIX_FMT_YUV420P,
                                codec->width, codec->height ) )
                        goto fail;
                } else {
                    ost->video_resample = 1;
                    ost->video_crop = 0; // cropping is handled as part of resample
                    avcodec_get_frame_defaults(&ost->pict_tmp);
                    if( avpicture_alloc( (AVPicture*)&ost->pict_tmp, PIX_FMT_YUV420P,
                                         codec->width, codec->height ) )
                        goto fail;

                    ost->img_resample_ctx = img_resample_full_init( 
                                      ost->st->codec.width, ost->st->codec.height,
                                      ist->st->codec.width, ist->st->codec.height,
                                      frame_topBand, frame_bottomBand,
                            frame_leftBand, frame_rightBand, 
                            frame_padtop, frame_padbottom, 
                            frame_padleft, frame_padright);
                    
                    ost->padtop = frame_padtop;
                    ost->padleft = frame_padleft;
                    ost->padbottom = frame_padbottom;
                    ost->padright = frame_padright;
                   
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
                    size = fread(logbuffer, 1, size, f);
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
    if (verbose >= 0) {
        fprintf(stderr, "Stream mapping:\n");
        for(i=0;i<nb_ostreams;i++) {
            ost = ost_table[i];
            fprintf(stderr, "  Stream #%d.%d -> #%d.%d\n",
                    ist_table[ost->source_index]->file_index,
                    ist_table[ost->source_index]->index,
                    ost->file_index, 
                    ost->index);
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
            extra_size += ost->st->codec.extradata_size;
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
        ist->is_start = 1;
    }
    
    /* compute buffer size max (should use a complete heuristic) */
    for(i=0;i<nb_input_files;i++) {
        file_table[i].buffer_size_max = 2048;
    }

    /* set meta data information from input file if required */
    for (i=0;i<nb_meta_data_maps;i++) {
        AVFormatContext *out_file;
        AVFormatContext *in_file;

        int out_file_index = meta_data_maps[i].out_file;
        int in_file_index = meta_data_maps[i].in_file;
        if ( out_file_index < 0 || out_file_index >= nb_output_files ) {
            fprintf(stderr, "Invalid output file index %d map_meta_data(%d,%d)\n", out_file_index, out_file_index, in_file_index);
            ret = -EINVAL;
            goto fail;
        }
        if ( in_file_index < 0 || in_file_index >= nb_input_files ) {
            fprintf(stderr, "Invalid input file index %d map_meta_data(%d,%d)\n", in_file_index, out_file_index, in_file_index);
            ret = -EINVAL;
            goto fail;
        }		
		 
        out_file = output_files[out_file_index];
        in_file = input_files[in_file_index];

        strcpy(out_file->title, in_file->title);
        strcpy(out_file->author, in_file->author);
        strcpy(out_file->copyright, in_file->copyright);
        strcpy(out_file->comment, in_file->comment);
        strcpy(out_file->album, in_file->album);
        out_file->year = in_file->year;
        out_file->track = in_file->track;
        strcpy(out_file->genre, in_file->genre);
    }
	
    /* open files and write file headers */
    for(i=0;i<nb_output_files;i++) {
        os = output_files[i];
        if (av_write_header(os) < 0) {
            fprintf(stderr, "Could not write header for output file #%d (incorrect codec parameters ?)\n", i);
            ret = -EINVAL;
            goto fail;
        }
    }

#ifndef CONFIG_WIN32
    if ( !using_stdin && verbose >= 0) {
        fprintf(stderr, "Press [q] to stop encoding\n");
        url_set_interrupt_cb(decode_interrupt_cb);
    }
#endif
    term_init();

    stream_no_data = 0;
    key = -1;

    for(; received_sigterm == 0;) {
        int file_index, ist_index;
        AVPacket pkt;
        double ipts_min;
        double opts_min;

    redo:
        ipts_min= 1e100;
        opts_min= 1e100;
        /* if 'q' pressed, exits */
        if (!using_stdin) {
            if (q_pressed)
                break;
            /* read_key() returns 0 on EOF */
            key = read_key();
            if (key == 'q')
                break;
        }

        /* select the stream that we must read now by looking at the
           smallest output pts */
        file_index = -1;
        for(i=0;i<nb_ostreams;i++) {
            double ipts, opts;
            ost = ost_table[i];
            os = output_files[ost->file_index];
            ist = ist_table[ost->source_index];
            if(ost->st->codec.codec_type == CODEC_TYPE_VIDEO)
                opts = (double)ost->sync_opts * ost->st->codec.frame_rate_base / ost->st->codec.frame_rate;
            else
                opts = (double)ost->st->pts.val * ost->st->time_base.num / ost->st->time_base.den;
            ipts = (double)ist->pts;
            if (!file_table[ist->file_index].eof_reached){
                if(ipts < ipts_min) {
                    ipts_min = ipts;
                    if(input_sync ) file_index = ist->file_index;
                }
                if(opts < opts_min) {
                    opts_min = opts;
                    if(!input_sync) file_index = ist->file_index;
                }
            }
        }
        /* if none, if is finished */
        if (file_index < 0) {
            break;
        }

        /* finish if recording time exhausted */
        if (recording_time > 0 && opts_min >= (recording_time / 1000000.0))
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

//        fprintf(stderr, "next:%lld dts:%lld off:%lld %d\n", ist->next_pts, pkt.dts, input_files_ts_offset[ist->file_index], ist->st->codec.codec_type);
        if (pkt.dts != AV_NOPTS_VALUE) {
            int64_t delta= pkt.dts - ist->next_pts;
            if(ABS(delta) > 10LL*AV_TIME_BASE && !copy_ts){
                input_files_ts_offset[ist->file_index]-= delta;
                if (verbose > 2)
                    fprintf(stderr, "timestamp discontinuity %lld, new offset= %lld\n", delta, input_files_ts_offset[ist->file_index]);
                for(i=0; i<file_table[file_index].nb_streams; i++){
                    int index= file_table[file_index].ist_index + i;
                    ist_table[index]->next_pts += delta;
                    ist_table[index]->is_start=1;
                }
            }
        }

        //fprintf(stderr,"read #%d.%d size=%d\n", ist->file_index, ist->index, pkt.size);
        if (output_packet(ist, ist_index, ost_table, nb_ostreams, &pkt) < 0) {

            if (verbose >= 0)
                fprintf(stderr, "Error while decoding stream #%d.%d\n",
                        ist->file_index, ist->index);

            av_free_packet(&pkt);
            goto redo;
        }
        
    discard_packet:
        av_free_packet(&pkt);
        
        /* dump report by using the output first video and audio streams */
        print_report(output_files, ost_table, nb_ostreams, 0);
    }

    /* at the end of stream, we must flush the decoder buffers */
    for(i=0;i<nb_istreams;i++) {
        ist = ist_table[i];
        if (ist->decoding_needed) {
            output_packet(ist, i, ost_table, nb_ostreams, NULL);
        }
    }

    term_exit();

    /* write the trailer if needed and close file */
    for(i=0;i<nb_output_files;i++) {
        os = output_files[i];
        av_write_trailer(os);
    }

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
    video_rc_buffer_size = atoi(arg) * 8*1024;
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

static void opt_me_threshold(const char *arg)
{
    me_threshold = atoi(arg);
}

static void opt_mb_threshold(const char *arg)
{
    mb_threshold = atoi(arg);
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

static void opt_vismv(const char *arg)
{
    debug_mv = atoi(arg);
}
    
static void opt_verbose(const char *arg)
{
    verbose = atoi(arg);
    av_log_set_level(atoi(arg));
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


#define SCALEBITS 10
#define ONE_HALF  (1 << (SCALEBITS - 1))
#define FIX(x)	  ((int) ((x) * (1<<SCALEBITS) + 0.5))

#define RGB_TO_Y(r, g, b) \
((FIX(0.29900) * (r) + FIX(0.58700) * (g) + \
  FIX(0.11400) * (b) + ONE_HALF) >> SCALEBITS)

#define RGB_TO_U(r1, g1, b1, shift)\
(((- FIX(0.16874) * r1 - FIX(0.33126) * g1 +         \
     FIX(0.50000) * b1 + (ONE_HALF << shift) - 1) >> (SCALEBITS + shift)) + 128)

#define RGB_TO_V(r1, g1, b1, shift)\
(((FIX(0.50000) * r1 - FIX(0.41869) * g1 -           \
   FIX(0.08131) * b1 + (ONE_HALF << shift) - 1) >> (SCALEBITS + shift)) + 128)

static void opt_pad_color(const char *arg) {
    /* Input is expected to be six hex digits similar to
       how colors are expressed in html tags (but without the #) */
    int rgb = strtol(arg, NULL, 16);
    int r,g,b;
    
    r = (rgb >> 16); 
    g = ((rgb >> 8) & 255);
    b = (rgb & 255);

    padcolor[0] = RGB_TO_Y(r,g,b);
    padcolor[1] = RGB_TO_U(r,g,b,0);
    padcolor[2] = RGB_TO_V(r,g,b,0);
}

static void opt_frame_pad_top(const char *arg)
{
    frame_padtop = atoi(arg); 
    if (frame_padtop < 0) {
        fprintf(stderr, "Incorrect top pad size\n");
        exit(1);
    }
    if ((frame_padtop % 2) != 0) {
        fprintf(stderr, "Top pad size must be a multiple of 2\n");
        exit(1);
    }
}

static void opt_frame_pad_bottom(const char *arg)
{
    frame_padbottom = atoi(arg); 
    if (frame_padbottom < 0) {
        fprintf(stderr, "Incorrect bottom pad size\n");
        exit(1);
    }
    if ((frame_padbottom % 2) != 0) {
        fprintf(stderr, "Bottom pad size must be a multiple of 2\n");
        exit(1);
    }
}


static void opt_frame_pad_left(const char *arg)
{
    frame_padleft = atoi(arg); 
    if (frame_padleft < 0) {
        fprintf(stderr, "Incorrect left pad size\n");
        exit(1);
    }
    if ((frame_padleft % 2) != 0) {
        fprintf(stderr, "Left pad size must be a multiple of 2\n");
        exit(1);
    }
}


static void opt_frame_pad_right(const char *arg)
{
    frame_padright = atoi(arg); 
    if (frame_padright < 0) {
        fprintf(stderr, "Incorrect right pad size\n");
        exit(1);
    }
    if ((frame_padright % 2) != 0) {
        fprintf(stderr, "Right pad size must be a multiple of 2\n");
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

static void opt_mb_cmp(const char *arg)
{
    mb_cmp = atoi(arg);
}

static void opt_ildct_cmp(const char *arg)
{
    ildct_cmp = atoi(arg);
}

static void opt_sub_cmp(const char *arg)
{
    sub_cmp = atoi(arg);
}

static void opt_cmp(const char *arg)
{
    cmp = atoi(arg);
}

static void opt_pre_cmp(const char *arg)
{
    pre_cmp = atoi(arg);
}

static void opt_pre_me(const char *arg)
{
    pre_me = atoi(arg);
}

static void opt_lumi_mask(const char *arg)
{
    lumi_mask = atof(arg);
}

static void opt_dark_mask(const char *arg)
{
    dark_mask = atof(arg);
}

static void opt_scplx_mask(const char *arg)
{
    scplx_mask = atof(arg);
}

static void opt_tcplx_mask(const char *arg)
{
    tcplx_mask = atof(arg);
}

static void opt_p_mask(const char *arg)
{
    p_mask = atof(arg);
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

static void opt_lmax(const char *arg)
{
    video_lmax = atof(arg)*FF_QP2LAMBDA;
}

static void opt_lmin(const char *arg)
{
    video_lmin = atof(arg)*FF_QP2LAMBDA;
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

static void opt_ibias(const char *arg)
{
    video_intra_quant_bias = atoi(arg);
}
static void opt_pbias(const char *arg)
{
    video_inter_quant_bias = atoi(arg);
}

static void opt_packet_size(const char *arg)
{
    packet_size= atoi(arg);
}

static void opt_error_rate(const char *arg)
{
    error_rate= atoi(arg);
}

static void opt_strict(const char *arg)
{
    strict= atoi(arg);
}

static void opt_top_field_first(const char *arg)
{
    top_field_first= atoi(arg);
}

static void opt_noise_reduction(const char *arg)
{
    noise_reduction= atoi(arg);
}

static void opt_qns(const char *arg)
{
    qns= atoi(arg);
}

static void opt_sc_threshold(const char *arg)
{
    sc_threshold= atoi(arg);
}

static void opt_me_range(const char *arg)
{
    me_range = atoi(arg);
}

static void opt_thread_count(const char *arg)
{
    thread_count= atoi(arg);
#if !defined(HAVE_PTHREADS) && !defined(HAVE_W32THREADS)
    if (verbose >= 0)
        fprintf(stderr, "Warning: not compiled with thread support, using thread emulation\n");
#endif
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

static void opt_map_meta_data(const char *arg)
{
    AVMetaDataMap *m;
    const char *p;
	
    p = arg;
    m = &meta_data_maps[nb_meta_data_maps++];

    m->out_file = strtol(arg, (char **)&p, 0);
    if (*p)
        p++;

    m->in_file = strtol(p, (char **)&p, 0);
}

static void opt_recording_time(const char *arg)
{
    recording_time = parse_date(arg, 1);
}

static void opt_start_time(const char *arg)
{
    start_time = parse_date(arg, 1);
}

static void opt_rec_timestamp(const char *arg)
{
    rec_timestamp = parse_date(arg, 0) / 1000000;
}

static void opt_input_ts_offset(const char *arg)
{
    input_ts_offset = parse_date(arg, 1);
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
    ap->width = frame_width + frame_padleft + frame_padright;
    ap->height = frame_height + frame_padtop + frame_padbottom;
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
    if (ret < 0 && verbose >= 0) {
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
#if defined(HAVE_PTHREADS) || defined(HAVE_W32THREADS)
        if(thread_count>1)
            avcodec_thread_init(enc, thread_count);
#endif
        enc->thread_count= thread_count;
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
            enc->idct_algo = idct_algo;
            enc->debug = debug;
            enc->debug_mv = debug_mv;            
            if(bitexact)
                enc->flags|= CODEC_FLAG_BITEXACT;
            if(me_threshold)
                enc->debug |= FF_DEBUG_MV;

            assert(enc->frame_rate_base == rfps_base); // should be true for now
            if (enc->frame_rate != rfps) { 

                if (verbose >= 0)
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
    input_files_ts_offset[nb_input_files] = input_ts_offset;
    /* dump the file content */
    if (verbose >= 0)
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
            case CODEC_TYPE_DATA:
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

    oc = av_alloc_format_context();

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
        use_video = file_oformat->video_codec != CODEC_ID_NONE || video_stream_copy;
        use_audio = file_oformat->audio_codec != CODEC_ID_NONE || audio_stream_copy;

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
            
            st = av_new_stream(oc, nb_streams++);
            if (!st) {
                fprintf(stderr, "Could not alloc stream\n");
                exit(1);
            }
#if defined(HAVE_PTHREADS) || defined(HAVE_W32THREADS)
            if(thread_count>1)
                avcodec_thread_init(&st->codec, thread_count);
#endif

            video_enc = &st->codec;
            
            if(!strcmp(file_oformat->name, "mp4") || !strcmp(file_oformat->name, "mov") || !strcmp(file_oformat->name, "3gp"))
                video_enc->flags |= CODEC_FLAG_GLOBAL_HEADER;
            if (video_stream_copy) {
                st->stream_copy = 1;
                video_enc->codec_type = CODEC_TYPE_VIDEO;
            } else {
                char *p;
                int i;
                AVCodec *codec;
            
                codec_id = file_oformat->video_codec;
                if (video_codec_id != CODEC_ID_NONE)
                    codec_id = video_codec_id;
                
                video_enc->codec_id = codec_id;
                codec = avcodec_find_encoder(codec_id);
                
                video_enc->bit_rate = video_bit_rate;
                video_enc->bit_rate_tolerance = video_bit_rate_tolerance;
                video_enc->frame_rate = frame_rate; 
                video_enc->frame_rate_base = frame_rate_base; 
                if(codec && codec->supported_framerates){
                    const AVRational *p= codec->supported_framerates;
                    AVRational req= (AVRational){frame_rate, frame_rate_base};
                    const AVRational *best=NULL;
                    AVRational best_error= (AVRational){INT_MAX, 1};
                    for(; p->den!=0; p++){
                        AVRational error= av_sub_q(req, *p);
                        if(error.num <0) error.num *= -1;
                        if(av_cmp_q(error, best_error) < 0){
                            best_error= error;
                            best= p;
                        }
                    }
                    video_enc->frame_rate     = best->num;
                    video_enc->frame_rate_base= best->den;
                }
                
                video_enc->width = frame_width + frame_padright + frame_padleft;
                video_enc->height = frame_height + frame_padtop + frame_padbottom;
		video_enc->sample_aspect_ratio = av_d2q(frame_aspect_ratio*frame_height/frame_width, 255);
                video_enc->pix_fmt = frame_pix_fmt;

                if(codec && codec->pix_fmts){
                    const enum PixelFormat *p= codec->pix_fmts;
                    for(; *p!=-1; p++){
                        if(*p == video_enc->pix_fmt)
                            break;
                    }
                    if(*p == -1)
                        video_enc->pix_fmt = codec->pix_fmts[0];
                }

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
                video_enc->mb_cmp = mb_cmp;
                video_enc->ildct_cmp = ildct_cmp;
                video_enc->me_sub_cmp = sub_cmp;
                video_enc->me_cmp = cmp;
                video_enc->me_pre_cmp = pre_cmp;
                video_enc->pre_me = pre_me;
                video_enc->lumi_masking = lumi_mask;
                video_enc->dark_masking = dark_mask;
                video_enc->spatial_cplx_masking = scplx_mask;
                video_enc->temporal_cplx_masking = tcplx_mask;
                video_enc->p_masking = p_mask;
                video_enc->quantizer_noise_shaping= qns;
                
                if (use_umv) {
                    video_enc->flags |= CODEC_FLAG_H263P_UMV;
                }
                if (use_ss) {
                    video_enc->flags |= CODEC_FLAG_H263P_SLICE_STRUCT;
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
                if (use_obmc) {
                    video_enc->flags |= CODEC_FLAG_OBMC;
                }
                if (use_loop) {
                    video_enc->flags |= CODEC_FLAG_LOOP_FILTER;
                }
            
                if(use_part) {
                    video_enc->flags |= CODEC_FLAG_PART;
                }
           	if (use_alt_scan) {
                    video_enc->flags |= CODEC_FLAG_ALT_SCAN;
                }
           	if (use_trell) {
                    video_enc->flags |= CODEC_FLAG_TRELLIS_QUANT;
                }
           	if (use_scan_offset) {
                    video_enc->flags |= CODEC_FLAG_SVCD_SCAN_OFFSET;
                }
           	if (closed_gop) {
                    video_enc->flags |= CODEC_FLAG_CLOSED_GOP;
                }
           	if (use_qpel) {
                    video_enc->flags |= CODEC_FLAG_QPEL;
                }
           	if (use_qprd) {
                    video_enc->flags |= CODEC_FLAG_QP_RD;
                }
           	if (use_cbprd) {
                    video_enc->flags |= CODEC_FLAG_CBP_RD;
                }
                if (b_frames) {
                    video_enc->max_b_frames = b_frames;
                    video_enc->b_frame_strategy = 0;
                    video_enc->b_quant_factor = 2.0;
                }
                if (do_interlace_dct) {
                    video_enc->flags |= CODEC_FLAG_INTERLACED_DCT;
                }
                if (do_interlace_me) {
                    video_enc->flags |= CODEC_FLAG_INTERLACED_ME;
                }
                video_enc->qmin = video_qmin;
                video_enc->qmax = video_qmax;
                video_enc->lmin = video_lmin;
                video_enc->lmax = video_lmax;
                video_enc->mb_qmin = video_mb_qmin;
                video_enc->mb_qmax = video_mb_qmax;
                video_enc->max_qdiff = video_qdiff;
                video_enc->qblur = video_qblur;
                video_enc->qcompress = video_qcomp;
                video_enc->rc_eq = video_rc_eq;
                video_enc->debug = debug;
                video_enc->debug_mv = debug_mv;
                video_enc->thread_count = thread_count;
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
                video_enc->intra_quant_bias = video_intra_quant_bias;
                video_enc->inter_quant_bias = video_inter_quant_bias;
                video_enc->dct_algo = dct_algo;
                video_enc->idct_algo = idct_algo;
                video_enc->me_threshold= me_threshold;
                video_enc->mb_threshold= mb_threshold;
                video_enc->intra_dc_precision= intra_dc_precision - 8;
                video_enc->strict_std_compliance = strict;
                video_enc->error_rate = error_rate;
                video_enc->noise_reduction= noise_reduction;
                video_enc->scenechange_threshold= sc_threshold;
                video_enc->me_range = me_range;
                video_enc->coder_type= coder;
                video_enc->context_model= context;
                video_enc->prediction_method= predictor;

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
        }
    
        if (use_audio) {
            AVCodecContext *audio_enc;

            st = av_new_stream(oc, nb_streams++);
            if (!st) {
                fprintf(stderr, "Could not alloc stream\n");
                exit(1);
            }
#if defined(HAVE_PTHREADS) || defined(HAVE_W32THREADS)
            if(thread_count>1)
                avcodec_thread_init(&st->codec, thread_count);
#endif

            audio_enc = &st->codec;
            audio_enc->codec_type = CODEC_TYPE_AUDIO;

            if(!strcmp(file_oformat->name, "mp4") || !strcmp(file_oformat->name, "mov") || !strcmp(file_oformat->name, "3gp"))
                audio_enc->flags |= CODEC_FLAG_GLOBAL_HEADER;
            if (audio_stream_copy) {
                st->stream_copy = 1;
		audio_enc->channels = audio_channels;
            } else {
                codec_id = file_oformat->audio_codec;
                if (audio_codec_id != CODEC_ID_NONE)
                    codec_id = audio_codec_id;
                audio_enc->codec_id = codec_id;
                
                audio_enc->bit_rate = audio_bit_rate;
                audio_enc->strict_std_compliance = strict;
                audio_enc->thread_count = thread_count;
                /* For audio codecs other than AC3 or DTS we limit */
                /* the number of coded channels to stereo   */
                if (audio_channels > 2 && codec_id != CODEC_ID_AC3
                    && codec_id != CODEC_ID_DTS) {
                    audio_enc->channels = 2;
                } else
                    audio_enc->channels = audio_channels;
            }
	    audio_enc->sample_rate = audio_sample_rate;
        }

        oc->nb_streams = nb_streams;

        if (!nb_streams) {
            fprintf(stderr, "No audio or video streams available\n");
            exit(1);
        }

        oc->timestamp = rec_timestamp;
	    
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

        if (verbose >= 0)
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

        if (verbose >= 0)
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
    AVCodec *p, *p2;
    const char **pp, *last_name;

    printf("File formats:\n");
    last_name= "000";
    for(;;){
        int decode=0;
        int encode=0;
        const char *name=NULL;

        for(ofmt = first_oformat; ofmt != NULL; ofmt = ofmt->next) {
            if((name == NULL || strcmp(ofmt->name, name)<0) &&
                strcmp(ofmt->name, last_name)>0){
                name= ofmt->name;
                encode=1;
            }
        }
        for(ifmt = first_iformat; ifmt != NULL; ifmt = ifmt->next) {
            if((name == NULL || strcmp(ifmt->name, name)<0) &&
                strcmp(ifmt->name, last_name)>0){
                name= ifmt->name;
                encode=0;
            }
            if(name && strcmp(ifmt->name, name)==0)
                decode=1;
        }
        if(name==NULL)
            break;
        last_name= name;
        
        printf(
            " %s%s %s\n", 
            decode ? "D":" ", 
            encode ? "E":" ", 
            name);
    }
    printf("\n");

    printf("Image formats:\n");
    for(image_fmt = first_image_format; image_fmt != NULL; 
        image_fmt = image_fmt->next) {
        printf(
            " %s%s %s\n",
            image_fmt->img_read  ? "D":" ",
            image_fmt->img_write ? "E":" ",
            image_fmt->name);
    }
    printf("\n");

    printf("Codecs:\n");
    last_name= "000";
    for(;;){
        int decode=0;
        int encode=0;
        int cap=0;

        p2=NULL;
        for(p = first_avcodec; p != NULL; p = p->next) {
            if((p2==NULL || strcmp(p->name, p2->name)<0) &&
                strcmp(p->name, last_name)>0){
                p2= p;
                decode= encode= cap=0;
            }
            if(p2 && strcmp(p->name, p2->name)==0){
                if(p->decode) decode=1;
                if(p->encode) encode=1;
                cap |= p->capabilities;
            }
        }
        if(p2==NULL)
            break;
        last_name= p2->name;
        
        printf(
            " %s%s%s%s%s%s %s", 
            decode ? "D": (/*p2->decoder ? "d":*/" "), 
            encode ? "E":" ", 
            p2->type == CODEC_TYPE_AUDIO ? "A":"V",
            cap & CODEC_CAP_DRAW_HORIZ_BAND ? "S":" ",
            cap & CODEC_CAP_DR1 ? "D":" ",
            cap & CODEC_CAP_TRUNCATED ? "T":" ",
            p2->name);
       /* if(p2->decoder && decode==0)
            printf(" use %s for decoding", p2->decoder->name);*/
        printf("\n");
    }
    printf("\n");

    printf("Supported file protocols:\n");
    for(up = first_protocol; up != NULL; up = up->next)
        printf(" %s:", up->name);
    printf("\n");
    
    printf("Frame size, frame rate abbreviations:\n ntsc pal qntsc qpal sntsc spal film ntsc-film sqcif qcif cif 4cif\n");
    printf("Motion estimation methods:\n");
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
    printf("\n\n");
    printf(
"Note, the names of encoders and decoders dont always match, so there are\n"
"several cases where the above table shows encoder only or decoder only entries\n"
"even though both encoding and decoding are supported for example, the h263\n"
"decoder corresponds to the h263 and h263p encoders, for file formats its even\n"
"worse\n");
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
        video_rc_buffer_size = 40*1024*8;

        audio_bit_rate = 224000;
        audio_sample_rate = 44100;

    } else if(!strcmp(arg, "svcd")) {

        opt_video_codec("mpeg2video");
        opt_audio_codec("mp2");
        opt_format("svcd");

        opt_frame_size(norm ? "480x480" : "480x576");
        opt_gop_size(norm ? "18" : "15");

        video_bit_rate = 2040000;
        video_rc_max_rate = 2516000;
        video_rc_min_rate = 0; //1145000;
        video_rc_buffer_size = 224*1024*8;
        use_scan_offset = 1;

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
        video_rc_min_rate = 0; //1500000;
        video_rc_buffer_size = 224*1024*8;

        audio_bit_rate = 448000;
        audio_sample_rate = 48000;

    } else {
        fprintf(stderr, "Unknown target: %s\n", arg);
        exit(1);
    }
}

static void show_version(void)
{
    printf("ffmpeg      " FFMPEG_VERSION "\n"
           "libavcodec  %d\n"
           "libavformat %d\n", 
           avcodec_build(), LIBAVFORMAT_BUILD);
    exit(1);
}

const OptionDef options[] = {
    /* main options */
    { "L", 0, {(void*)show_license}, "show license" },
    { "h", 0, {(void*)show_help}, "show help" },
    { "version", 0, {(void*)show_version}, "show version" },
    { "formats", 0, {(void*)show_formats}, "show available formats, codecs, protocols, ..." },
    { "f", HAS_ARG, {(void*)opt_format}, "force format", "fmt" },
    { "img", HAS_ARG, {(void*)opt_image_format}, "force image format", "img_fmt" },
    { "i", HAS_ARG, {(void*)opt_input_file}, "input file name", "filename" },
    { "y", OPT_BOOL, {(void*)&file_overwrite}, "overwrite output files" },
    { "map", HAS_ARG | OPT_EXPERT, {(void*)opt_map}, "set input stream mapping", "file:stream" },
    { "map_meta_data", HAS_ARG | OPT_EXPERT, {(void*)opt_map_meta_data}, "set meta data information of outfile from infile", "outfile:infile" },
    { "t", HAS_ARG, {(void*)opt_recording_time}, "set the recording time", "duration" },
    { "ss", HAS_ARG, {(void*)opt_start_time}, "set the start time offset", "time_off" },
    { "itsoffset", HAS_ARG, {(void*)opt_input_ts_offset}, "set the input ts offset", "time_off" },
    { "title", HAS_ARG | OPT_STRING, {(void*)&str_title}, "set the title", "string" },
    { "timestamp", HAS_ARG, {(void*)&opt_rec_timestamp}, "set the timestamp", "time" },
    { "author", HAS_ARG | OPT_STRING, {(void*)&str_author}, "set the author", "string" },
    { "copyright", HAS_ARG | OPT_STRING, {(void*)&str_copyright}, "set the copyright", "string" },
    { "comment", HAS_ARG | OPT_STRING, {(void*)&str_comment}, "set the comment", "string" },
    { "debug", HAS_ARG | OPT_EXPERT, {(void*)opt_debug}, "print specific debug info", "" },
    { "vismv", HAS_ARG | OPT_EXPERT, {(void*)opt_vismv}, "visualize motion vectors", "" },
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
    { "threads", HAS_ARG | OPT_EXPERT, {(void*)opt_thread_count}, "thread count", "count" },
    { "vsync", HAS_ARG | OPT_INT | OPT_EXPERT, {(void*)&video_sync_method}, "video sync method", "" },
    { "async", HAS_ARG | OPT_INT | OPT_EXPERT, {(void*)&audio_sync_method}, "audio sync method", "" },
    { "copyts", OPT_BOOL | OPT_EXPERT, {(void*)&copy_ts}, "copy timestamps" },

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
    { "padtop", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_pad_top}, "set top pad band size (in pixels)", "size" },
    { "padbottom", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_pad_bottom}, "set bottom pad band size (in pixels)", "size" },
    { "padleft", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_pad_left}, "set left pad band size (in pixels)", "size" },
    { "padright", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_pad_right}, "set right pad band size (in pixels)", "size" },
    { "padcolor", HAS_ARG | OPT_VIDEO, {(void*)opt_pad_color}, "set color of pad bands (Hex 000000 thru FFFFFF)", "color" },
    { "g", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_gop_size}, "set the group of picture size", "gop_size" },
    { "intra", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&intra_only}, "use only intra frames"},
    { "vn", OPT_BOOL | OPT_VIDEO, {(void*)&video_disable}, "disable video" },
    { "qscale", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_qscale}, "use fixed video quantiser scale (VBR)", "q" },
    { "qmin", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_qmin}, "min video quantiser scale (VBR)", "q" },
    { "qmax", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_qmax}, "max video quantiser scale (VBR)", "q" },
    { "lmin", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_lmin}, "min video lagrange factor (VBR)", "lambda" },
    { "lmax", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_lmax}, "max video lagrange factor (VBR)", "lambda" },
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
    { "ibias", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_ibias}, "intra quant bias", "bias" },
    { "pbias", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_pbias}, "inter quant bias", "bias" },
//    { "b_strategy", HAS_ARG | OPT_EXPERT, {(void*)opt_b_strategy}, "dynamic b frame selection strategy", "strategy" },
    { "rc_eq", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_video_rc_eq}, "set rate control equation", "equation" },
    { "rc_override", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_video_rc_override_string}, "rate control override for specific intervals", "override" },
    { "bt", HAS_ARG | OPT_VIDEO, {(void*)opt_video_bitrate_tolerance}, "set video bitrate tolerance (in kbit/s)", "tolerance" },
    { "maxrate", HAS_ARG | OPT_VIDEO, {(void*)opt_video_bitrate_max}, "set max video bitrate tolerance (in kbit/s)", "bitrate" },
    { "minrate", HAS_ARG | OPT_VIDEO, {(void*)opt_video_bitrate_min}, "set min video bitrate tolerance (in kbit/s)", "bitrate" },
    { "bufsize", HAS_ARG | OPT_VIDEO, {(void*)opt_video_buffer_size}, "set ratecontrol buffere size (in kByte)", "size" },
    { "vcodec", HAS_ARG | OPT_VIDEO, {(void*)opt_video_codec}, "force video codec ('copy' to copy stream)", "codec" },
    { "me", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_motion_estimation}, "set motion estimation method", 
      "method" },
    { "dct_algo", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_dct_algo}, "set dct algo",  "algo" },
    { "idct_algo", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_idct_algo}, "set idct algo",  "algo" },
    { "me_threshold", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_me_threshold}, "motion estimaton threshold",  "" },
    { "mb_threshold", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_mb_threshold}, "macroblock threshold",  "" },
    { "er", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_error_resilience}, "set error resilience",  "n" },
    { "ec", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_error_concealment}, "set error concealment",  "bit_mask" },
    { "bf", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_b_frames}, "use 'frames' B frames", "frames" },
    { "hq", OPT_BOOL, {(void*)&mb_decision}, "activate high quality settings" },
    { "mbd", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_mb_decision}, "macroblock decision", "mode" },
    { "mbcmp", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_mb_cmp}, "macroblock compare function", "cmp function" },
    { "ildctcmp", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_ildct_cmp}, "ildct compare function", "cmp function" },
    { "subcmp", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_sub_cmp}, "subpel compare function", "cmp function" },
    { "cmp", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_cmp}, "fullpel compare function", "cmp function" },
    { "precmp", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_pre_cmp}, "pre motion estimation compare function", "cmp function" },
    { "preme", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_pre_me}, "pre motion estimation", "" },
    { "lumi_mask", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_lumi_mask}, "luminance masking", "" },
    { "dark_mask", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_dark_mask}, "darkness masking", "" },
    { "scplx_mask", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_scplx_mask}, "spatial complexity masking", "" },
    { "tcplx_mask", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_tcplx_mask}, "teporal complexity masking", "" },
    { "p_mask", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_p_mask}, "inter masking", "" },
    { "4mv", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&use_4mv}, "use four motion vector by macroblock (MPEG4)" },
    { "obmc", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&use_obmc}, "use overlapped block motion compensation (h263+)" },
    { "lf", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&use_loop}, "use loop filter (h263+)" },
    { "part", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&use_part}, "use data partitioning (MPEG4)" },
    { "bug", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_workaround_bugs}, "workaround not auto detected encoder bugs", "param" },
    { "ps", HAS_ARG | OPT_EXPERT, {(void*)opt_packet_size}, "set packet size in bits", "size" },
    { "error", HAS_ARG | OPT_EXPERT, {(void*)opt_error_rate}, "error rate", "rate" },
    { "strict", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_strict}, "how strictly to follow the standards", "strictness" },
    { "sameq", OPT_BOOL | OPT_VIDEO, {(void*)&same_quality}, 
      "use same video quality as source (implies VBR)" },
    { "pass", HAS_ARG | OPT_VIDEO, {(void*)&opt_pass}, "select the pass number (1 or 2)", "n" },
    { "passlogfile", HAS_ARG | OPT_STRING | OPT_VIDEO, {(void*)&pass_logfilename}, "select two pass log file name", "file" },
    { "deinterlace", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&do_deinterlace}, 
      "deinterlace pictures" },
    { "ildct", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&do_interlace_dct}, 
      "force interlaced dct support in encoder (MPEG2/MPEG4)" },
    { "ilme", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&do_interlace_me}, 
      "force interlaced me support in encoder (MPEG2/MPEG4)" },
    { "psnr", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&do_psnr}, "calculate PSNR of compressed frames" },
    { "vstats", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&do_vstats}, "dump video coding statistics to file" }, 
    { "vhook", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)add_frame_hooker}, "insert video processing module", "module" },
    { "aic", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&use_aic}, "enable Advanced intra coding (h263+)" },
    { "aiv", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&use_aiv}, "enable Alternative inter vlc (h263+)" },
    { "umv", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&use_umv}, "enable Unlimited Motion Vector (h263+)" },
    { "ssm", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&use_ss}, "enable Slice Structured mode (h263+)" },
    { "alt", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&use_alt_scan}, "enable alternate scantable (MPEG2/MPEG4)" },
    { "qprd", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&use_qprd}, "" },
    { "cbp", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&use_cbprd}, "" },
    { "trell", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&use_trell}, "enable trellis quantization" },
    { "cgop", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&closed_gop}, "closed gop" },
    { "scan_offset", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&use_scan_offset}, "enable SVCD Scan Offset placeholder" },
    { "qpel", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&use_qpel}, "enable 1/4-pel" },
    { "intra_matrix", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_intra_matrix}, "specify intra matrix coeffs", "matrix" },
    { "inter_matrix", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_inter_matrix}, "specify inter matrix coeffs", "matrix" },
    { "top", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_top_field_first}, "top=1/bottom=0/auto=-1 field first", "" },
    { "nr", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_noise_reduction}, "noise reduction", "" },
    { "qns", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_qns}, "quantization noise shaping", "" },
    { "sc_threshold", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_sc_threshold}, "scene change threshold", "threshold" },
    { "me_range", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_me_range}, "limit motion vectors range (1023 for DivX player)", "range" },
    { "dc", OPT_INT | HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)&intra_dc_precision}, "intra_dc_precision", "precision" },
    { "coder", OPT_INT | HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)&coder}, "coder type", "" },
    { "context", OPT_INT | HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)&context}, "context model", "" },
    { "pred", OPT_INT | HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)&predictor}, "prediction method", "" },

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
    printf("ffmpeg version " FFMPEG_VERSION ", build %d, Copyright (c) 2000-2004 Fabrice Bellard\n",
        LIBAVCODEC_BUILD);
    printf("  built on " __DATE__ " " __TIME__);
#ifdef __GNUC__
    printf(", gcc: %s\n", __VERSION__);
#else
    printf(", using a non-gcc compiler\n");
#endif
}

static void show_license(void)
{
    show_banner();
#ifdef CONFIG_GPL
    printf(
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
    "Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA\n"
    );
#else
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
#endif
    exit(1);
}

static void show_help(void)
{
    show_banner();
    printf("usage: ffmpeg [[infile options] -i infile]... {[outfile options] outfile}...\n"
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
    else
        show_banner();
    
    /* parse options */
    parse_options(argc, argv, options);

    /* file converter / grab */
    if (nb_output_files <= 0) {
        fprintf(stderr, "Must supply at least one output file\n");
        exit(1);
    }
    
    if (nb_input_files == 0) {
        input_sync = 1;
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
