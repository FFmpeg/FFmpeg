/*
 * Basic user interface for ffmpeg system
 * Copyright (c) 2000 Gerard Lantau
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
#include <netinet/in.h>
#include <linux/videodev.h>
#include <linux/soundcard.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/time.h>
#include <getopt.h>

#include "mpegenc.h"
#include "mpegvideo.h"

static AVFormat *file_format;
static int frame_width  = 160;
static int frame_height = 128;
static int frame_rate = 25;
static int bit_rate = 200000;
static int video_disable = 0;

static const char *video_filename, *audio_filename;
static float recording_time = 10.0;
static int nb_frames;
static int gop_size = 12;
static int intra_only = 0;
static int audio_freq = 44100;
static int audio_bit_rate = 64000;
static int audio_disable = 0;
static int audio_channels = 1;

static long long time_start;


static int file_read_picture(AVEncodeContext *s, 
                             UINT8 *picture[3],
                             int width, int height,
                             int picture_number)
{
    FILE *f;
    char buf[1024];
    static int init = 0;
    static UINT8 *pict[3];
    if (!init) {
        pict[0] = malloc(width * height);
        pict[1] = malloc(width * height / 4);
        pict[2] = malloc(width * height / 4);
        init = 1;
    }
    
    picture[0] = pict[0];
    picture[1] = pict[1];
    picture[2] = pict[2];

    sprintf(buf, "%s%d.Y", video_filename, picture_number);
    f=fopen(buf, "r");
    if (!f) {
        return -1;
    }

    fread(picture[0], 1, width * height, f);
    fclose(f);

    sprintf(buf, "%s%d.U", video_filename, picture_number);
    f=fopen(buf, "r");
    if (!f) {
        perror(buf);
        exit(1);
    }
    fread(picture[1], 1, width * height / 4, f);
    fclose(f);
    
    sprintf(buf, "%s%d.V", video_filename, picture_number);
    f=fopen(buf, "r");
    if (!f) {
        perror(buf);
        exit(1);
    }
    fread(picture[2], 1, width * height / 4, f);
    fclose(f);
    return 0;
}

static void display_stats(AVEncodeContext *video_ctx, 
                          AVEncodeContext *audio_ctx,
                          int batch_mode, int the_end)
{
    if (video_ctx && 
        ((video_ctx->frame_number % video_ctx->rate) == 0 || 
        the_end)) {
        float ti;
        
        if (batch_mode) {
            ti = (float)video_ctx->frame_number / video_ctx->rate;
        } else {
            ti = (gettime() - time_start) / 1000000.0;
            if (ti < 0.1)
                    ti = 0.1;
        }
        
        fprintf(stderr,
                "frame=%5d size=%8dkB time=%0.1f fps=%4.1f bitrate=%6.1fkbits/s q=%2d\r", 
                video_ctx->frame_number,
                data_out_size / 1024,
                ti,
                video_ctx->frame_number / ti,
                (data_out_size * 8 / ti / 1000),
                ((MpegEncContext *)video_ctx->priv_data)->qscale);
        if (the_end) {
            fprintf(stderr,"\n");
        }
        fflush(stderr);
    }
#if 0
    if (the_end && batch_mode && audio_ctx) {
        duration = (gettime() - ti) / 1000000.0;
        factor = 0;
        if (ti > 0) {
            factor = (float)nb_samples / s->sample_rate / duration;
        }
        fprintf(stderr, "%0.1f seconds compressed in %0.1f seconds (speed factor: %0.1f)\n",
                (float)nb_samples / s->sample_rate, 
                duration,
                factor);
    }
#endif
}

void raw_write_data(void *opaque, 
                    unsigned char *buf, int size)
{
    FILE *outfile = opaque;
    fwrite(buf, 1, size, outfile);
    data_out_size += size;
}

int raw_seek(void *opaque, long long offset, int whence)
{
    FILE *outfile = opaque;
    fseek(outfile, offset, whence);
    return 0;
}

static void av_encode(AVFormatContext *ctx,
                      const char *video_filename,
                      const char *audio_filename)
{
    UINT8 audio_buffer[4096];
    UINT8 video_buffer[128*1024];
    char buf[256];
    short *samples;
    int ret;
    int audio_fd;
    FILE *infile;
    int sample_count;
    int batch_mode;
    AVEncodeContext *audio_enc, *video_enc;
    int frame_size, frame_bytes;
    AVEncoder *audio_encoder, *video_encoder;
    UINT8 *picture[3];

    /* audio */
    audio_enc = ctx->audio_enc;
    sample_count = 0;
    infile = NULL;
    frame_size = 0;
    samples = NULL;
    audio_fd = -1;
    frame_bytes = 0;
    batch_mode = 0;
    if (audio_filename ||
        video_filename)
        batch_mode = 1;
    
    if (audio_enc) {
        if (batch_mode) {
            if (!audio_filename) {
                fprintf(stderr, "Must give audio input file\n");
                exit(1);
            }
            infile = fopen(audio_filename, "r");
            if (!infile) {
                fprintf(stderr, "Could not open '%s'\n", audio_filename);
                exit(1);
            }
            audio_fd = -1;
        } else {
            audio_fd = audio_open(audio_enc->rate, audio_enc->channels);
            if (audio_fd < 0) {
                fprintf(stderr, "Could not open audio device\n");
                exit(1);
            }
        }
        
        audio_encoder = avencoder_find(ctx->format->audio_codec);
        if (avencoder_open(audio_enc, audio_encoder) < 0) {
            fprintf(stderr, "Audio encoder: incorrect audio frequency or bitrate\n");
            exit(1);
        }
        avencoder_string(buf, sizeof(buf), audio_enc);
        fprintf(stderr, "  %s\n", buf);
        
        frame_size = audio_enc->frame_size;
        
        frame_bytes = frame_size * 2 * audio_enc->channels;
        samples = malloc(frame_bytes);
    }

    /* video */
    video_enc = ctx->video_enc;
    if (video_enc) {
        if (batch_mode) {
            if (!video_filename) {
                fprintf(stderr, "Must give video input file\n");
                exit(1);
            }
        } else {
            ret = v4l_init(video_enc->rate, video_enc->width, video_enc->height);
            if (ret < 0) {
                fprintf(stderr,"Could not init video 4 linux capture\n");
                exit(1);
            }
        }
        
        video_encoder = avencoder_find(ctx->format->video_codec);
        if (avencoder_open(video_enc, video_encoder) < 0) {
            fprintf(stderr, "Error while initializing video codec\n");
            exit(1);
        }

        avencoder_string(buf, sizeof(buf), video_enc);
        fprintf(stderr, "  %s\n", buf);
    }
    
    ctx->format->write_header(ctx);
    time_start = gettime();

    for(;;) {
        /* read & compression audio frames */
        if (audio_enc) {
            if (!batch_mode) {
                for(;;) {
                    ret = read(audio_fd, samples, frame_bytes);
                    if (ret != frame_bytes)
                        break;
                    ret = avencoder_encode(audio_enc,
                                           audio_buffer, sizeof(audio_buffer), samples);
                    ctx->format->write_audio_frame(ctx, audio_buffer, ret);
                }
            } else {
                if (video_enc)
                    sample_count += audio_enc->rate / video_enc->rate;
                else
                    sample_count += frame_size;
                while (sample_count > frame_size) {
                    if (fread(samples, 1, frame_bytes, infile) == 0)
                        goto the_end;
                    
                    ret = avencoder_encode(audio_enc,
                                           audio_buffer, sizeof(audio_buffer), samples);
                    ctx->format->write_audio_frame(ctx, audio_buffer, ret);

                    sample_count -= frame_size;
                }
            }
        }

        if (video_enc) {
            /* read video image */
            if (batch_mode) {
                ret = file_read_picture (video_enc, picture, 
                                         video_enc->width, video_enc->height, 
                                         video_enc->frame_number);
            } else {
                ret = v4l_read_picture (picture, 
                                        video_enc->width, video_enc->height, 
                                        video_enc->frame_number);
            }
            if (ret < 0)
                break;
            ret = avencoder_encode(video_enc, video_buffer, sizeof(video_buffer), picture);
            ctx->format->write_video_picture(ctx, video_buffer, ret);
        }
        
        display_stats(video_enc, NULL, batch_mode, 0);
        if (video_enc && video_enc->frame_number >= nb_frames)
            break;
    }
 the_end:
    display_stats(video_enc, NULL, batch_mode, 1);

    if (video_enc)
        avencoder_close(video_enc);

    if (audio_enc)
        avencoder_close(audio_enc);
    
    ctx->format->write_trailer(ctx);

    if (!infile) {
        close(audio_fd);
    } else {
        fclose(infile);
    }
}

typedef struct {
    const char *str;
    int width, height;
} SizeEntry;

SizeEntry sizes[] = {
    { "sqcif", 128, 96 },
    { "qcif", 176, 144 },
    { "cif", 352, 288 },
    { "4cif", 704, 576 },
};
    
enum {
    OPT_AR=256,
    OPT_AB,
    OPT_AN,
    OPT_AC,
    OPT_VN,
};

struct option long_options[] =
{
    { "ar", required_argument, NULL, OPT_AR },
    { "ab", required_argument, NULL, OPT_AB },
    { "an", no_argument, NULL, OPT_AN },
    { "ac", required_argument, NULL, OPT_AC },
    { "vn", no_argument, NULL, OPT_VN },
};

enum {
    OUT_FILE,
    OUT_PIPE,
    OUT_UDP,
};


void help(void)
{
    AVFormat *f;

    printf("ffmpeg version 1.0, Copyright (c) 2000 Gerard Lantau\n"
           "usage: ffmpeg [options] outfile [video_infile] [audio_infile]\n"
           "Hyper fast MPEG1 video/H263/RV and AC3/MPEG audio layer 2 encoder\n"
           "\n"
           "Main options are:\n"
           "\n"
           "-L           print the LICENSE\n"
           "-s size      set frame size                   [%dx%d]\n"
           "-f format    set encoding format              [%s]\n"
           "-r fps       set frame rate                   [%d]\n"
           "-b bitrate   set the total bitrate in kbit/s  [%d]\n"
           "-t time      set recording time in seconds    [%0.1f]\n"
           "-ar freq     set the audio sampling freq      [%d]\n"
           "-ab bitrate  set the audio bitrate in kbit/s  [%d]\n"
           "-ac channels set the number of audio channels [%d]\n"
           "-an          disable audio recording          [%s]\n"
           "-vn          disable video recording          [%s]\n"
           "\n"
           "Frame sizes abbreviations: sqcif qcif cif 4cif\n",
           frame_width, frame_height,
           file_format->name,
           frame_rate,
           bit_rate / 1000,
           recording_time,
           audio_freq,
           audio_bit_rate / 1000,
           audio_channels,
           audio_disable ? "yes" : "no",
           video_disable ? "yes" : "no");

    printf("Encoding video formats:");
    for(f = first_format; f != NULL; f = f->next)
        printf(" %s", f->name);
    printf("\n");

    printf("outfile can be a file name, - (pipe) or 'udp:host:port'\n"
           "\n"
           "Advanced options are:\n"
           "-d device    set video4linux device name\n"
           "-g gop_size  set the group of picture size    [%d]\n"
           "-i           use only intra frames            [%s]\n"
           "-c comment   set the comment string\n"
           "\n",
           gop_size,
           intra_only ? "yes" : "no");
}

void licence(void)
{
    printf(
    "ffmpeg version 1.0\n"
    "Copyright (c) 2000 Gerard Lantau\n"
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
}

static unsigned char output_buffer[32768];

int main(int argc, char **argv)
{
    AVEncodeContext video_enc1, *video_enc = &video_enc1;
    AVEncodeContext audio_enc1, *audio_enc = &audio_enc1;
    UDPContext udp_ctx1, *udp_ctx = &udp_ctx1;
    AVFormatContext av_ctx1, *av_ctx = &av_ctx1;
    FILE *outfile;
    int i, c;
    char *filename;
    int output_type;
    int use_video, use_audio;

    register_avencoder(&ac3_encoder);
    register_avencoder(&mp2_encoder);
    register_avencoder(&mpeg1video_encoder);
    register_avencoder(&h263_encoder);
    register_avencoder(&rv10_encoder);
    register_avencoder(&mjpeg_encoder);

    register_avformat(&mp2_format);
    register_avformat(&ac3_format);
    register_avformat(&mpeg1video_format);
    register_avformat(&h263_format);
    register_avformat(&mpeg_mux_format);
    register_avformat(&ra_format);
    register_avformat(&rm_format);
    register_avformat(&asf_format);
    register_avformat(&mpjpeg_format);
    register_avformat(&swf_format);

    file_format = NULL;
    
    for(;;) {
        c = getopt_long_only(argc, argv, "s:f:r:b:t:hd:g:ic:L", 
                             long_options, NULL);
        if (c == -1)
            break;
        switch(c) {
        case 'L':
            licence();
            exit(1);
        case 'h':
            help();
            exit(1);
        case 's':
            {
                int n = sizeof(sizes) / sizeof(SizeEntry);
                const char *p;

                for(i=0;i<n;i++) {
                    if (!strcmp(sizes[i].str, optarg)) {
                        frame_width = sizes[i].width;
                        frame_height = sizes[i].height;
                        break;
                    }
                }
                if (i == n) {
                    p = optarg;
                    frame_width = strtol(p, (char **)&p, 10);
                    if (*p)
                        p++;
                    frame_height = strtol(p, (char **)&p, 10);
                }
            }
            break;
        case 'f':
            {
                AVFormat *f;
                f = first_format;
                while (f != NULL && strcmp(f->name, optarg) != 0) f = f->next;
                if (f == NULL) {
                    fprintf(stderr, "Invalid format: %s\n", optarg);
                    exit(1);
                }
                file_format = f;
            }
            break;
        case 'r':
            {
                frame_rate = atoi(optarg);
            }
            break;
        case 'b':
            {
                bit_rate = atoi(optarg) * 1000;
            }
            break;
        case 't':
            {
                recording_time = atof(optarg);
                break;
            }
            /* audio specific */
        case OPT_AR:
            {
                audio_freq = atoi(optarg);
                break;
            }
        case OPT_AB:
            {
                audio_bit_rate = atoi(optarg) * 1000;
                break;
            }
        case OPT_AN:
            audio_disable = 1;
            break;
        case OPT_VN:
            video_disable = 1;
            break;
        case OPT_AC:
            {
                audio_channels = atoi(optarg);
                if (audio_channels != 1 && 
                    audio_channels != 2) {
                    fprintf(stderr, "Incorrect number of channels: %d\n", audio_channels);
                    exit(1);
                }
            }
            break;
            /* advanced options */
        case 'd':
            v4l_device = optarg;
            break;
        case 'g':
            gop_size = atoi(optarg);
            break;
        case 'i':
            intra_only = 1;
            break;
        case 'c':
            comment_string = optarg;
            break;
        default:
            exit(2);
        }
    }

    if (optind >= argc) {
        help();
        exit(1);
    }

    filename = argv[optind++];
    video_filename = NULL;
    audio_filename = NULL;

    /* auto detect format */
    if (file_format == NULL)
        file_format = guess_format(NULL, filename, NULL);

    if (file_format == NULL)
        file_format = &mpeg_mux_format;

    /* check parameters */
    if (frame_width <= 0 || frame_height <= 0) {
        fprintf(stderr, "Incorrect frame size\n");
        exit(1);
    }
    if ((frame_width % 16) != 0 || (frame_height % 16) != 0) {
        fprintf(stderr, "Frame size must be a multiple of 16\n");
        exit(1);
    }
    
    if (bit_rate < 5000 || bit_rate >= 10000000) {
        fprintf(stderr, "Invalid bit rate\n");
        exit(1);
    }

    if (frame_rate < 1 || frame_rate >= 60) {
        fprintf(stderr, "Invalid frame rate\n");
        exit(1);
    }

    nb_frames = (int)(recording_time * frame_rate);
    if (nb_frames < 1) {
        fprintf(stderr, "Invalid recording time\n");
        exit(1);
    }

    use_video = file_format->video_codec != CODEC_ID_NONE;
    use_audio = file_format->audio_codec != CODEC_ID_NONE;
    if (audio_disable) {
        use_audio = 0;
    }
    if (video_disable) {
        use_video = 0;
    }
        
    if (use_video == 0 && use_audio == 0) {
        fprintf(stderr, "No audio or video selected\n");
        exit(1);
    }

    fprintf(stderr, "Recording: %s, %0.1f seconds\n",
            file_format->name, 
            recording_time);

    /* open output media */

    if (strstart(filename, "udp:", NULL)) {
        output_type = OUT_UDP;
        outfile = NULL;
        memset(udp_ctx, 0, sizeof(*udp_ctx));
        if (udp_tx_open(udp_ctx, filename, 0) < 0) {
            fprintf(stderr, "Could not open UDP socket\n");
            exit(1);
        }
    } else if (!strcmp(filename, "-")) {
        output_type = OUT_PIPE;
        outfile = stdout;
    } else {
        output_type = OUT_FILE;
        outfile = fopen(filename, "w");
        if (!outfile) {
            perror(filename);
            exit(1);
        }
    }

    av_ctx->video_enc = NULL;
    av_ctx->audio_enc = NULL;

    if (output_type == OUT_UDP) {
        init_put_byte(&av_ctx->pb, output_buffer, sizeof(output_buffer),
                      udp_ctx, udp_write_data, NULL);
    } else {
        init_put_byte(&av_ctx->pb, output_buffer, sizeof(output_buffer),
                      outfile, raw_write_data, raw_seek);
    }

    if (use_video) {
        if (optind < argc) {
            video_filename = argv[optind++];
        }
        /* init mpeg video encoding context */
        memset(video_enc, 0, sizeof(*video_enc));
        video_enc->bit_rate = bit_rate;
        video_enc->rate = frame_rate; 

        video_enc->width = frame_width;
        video_enc->height = frame_height;
        if (!intra_only)
            video_enc->gop_size = gop_size;
        else
            video_enc->gop_size = 0;

        av_ctx->video_enc = video_enc;
        av_ctx->format = file_format;
    }

    if (use_audio) {
        if (optind < argc) {
            audio_filename = argv[optind++];
        }
        audio_enc->bit_rate = audio_bit_rate;
        audio_enc->rate = audio_freq;
        audio_enc->channels = audio_channels;
        av_ctx->audio_enc = audio_enc;
    }
    av_ctx->format = file_format;
    av_ctx->is_streamed = 0;

    av_encode(av_ctx, video_filename, audio_filename);

    /* close output media */

    switch(output_type) {
    case OUT_FILE:
        fclose(outfile);
        break;
    case OUT_PIPE:
        break;
    case OUT_UDP:
        udp_tx_close(udp_ctx);
        break;
    }
    fprintf(stderr, "\n");
    
    return 0;
}
    
