/*
 * PPM Video Hook
 * Copyright (c) 2003 Charles Yates
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include "libavutil/avstring.h"
#include "libavformat/framehook.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#undef fprintf

static int sws_flags = SWS_BICUBIC;

/** Bi-directional pipe structure.
*/

typedef struct rwpipe
{
    int pid;
    FILE *reader;
    FILE *writer;
}
rwpipe;

/** Create a bidirectional pipe for the given command.
*/

static rwpipe *rwpipe_open( int argc, char *argv[] )
{
    rwpipe *this = av_mallocz( sizeof( rwpipe ) );

    if ( this != NULL )
    {
        int input[ 2 ];
        int output[ 2 ];

        if (!pipe( input ))
            return NULL;

        if (!pipe( output ))
            return NULL;

        this->pid = fork();

        if ( this->pid == 0 )
        {
#define COMMAND_SIZE 10240
            char *command = av_mallocz( COMMAND_SIZE );
            int i;

            strcpy( command, "" );
            for ( i = 0; i < argc; i ++ )
            {
                av_strlcat( command, argv[ i ], COMMAND_SIZE );
                av_strlcat( command, " ", COMMAND_SIZE );
            }

            dup2( output[ 0 ], STDIN_FILENO );
            dup2( input[ 1 ], STDOUT_FILENO );

            close( input[ 0 ] );
            close( input[ 1 ] );
            close( output[ 0 ] );
            close( output[ 1 ] );

            execl("/bin/sh", "sh", "-c", command, (char*)NULL );
            _exit( 255 );
        }
        else
        {
            close( input[ 1 ] );
            close( output[ 0 ] );

            this->reader = fdopen( input[ 0 ], "r" );
            this->writer = fdopen( output[ 1 ], "w" );
        }
    }

    return this;
}

/** Read data from the pipe.
*/

static FILE *rwpipe_reader( rwpipe *this )
{
    if ( this != NULL )
        return this->reader;
    else
        return NULL;
}

/** Write data to the pipe.
*/

static FILE *rwpipe_writer( rwpipe *this )
{
    if ( this != NULL )
        return this->writer;
    else
        return NULL;
}

/* Read a number from the pipe - assumes PNM style headers.
*/

static int rwpipe_read_number( rwpipe *rw )
{
    int value = 0;
    int c = 0;
    FILE *in = rwpipe_reader( rw );

    do
    {
        c = fgetc( in );

        while( c != EOF && !isdigit( c ) && c != '#' )
            c = fgetc( in );

        if ( c == '#' )
            while( c != EOF && c != '\n' )
                c = fgetc( in );
    }
    while ( c != EOF && !isdigit( c ) );

    while( c != EOF && isdigit( c ) )
    {
        value = value * 10 + ( c - '0' );
        c = fgetc( in );
    }

    return value;
}

/** Read a PPM P6 header.
*/

static int rwpipe_read_ppm_header( rwpipe *rw, int *width, int *height )
{
    char line[ 3 ];
    FILE *in = rwpipe_reader( rw );
    int max;

    if (!fgets( line, 3, in ))
        return -1;

    if ( !strncmp( line, "P6", 2 ) )
    {
        *width = rwpipe_read_number( rw );
        *height = rwpipe_read_number( rw );
        max = rwpipe_read_number( rw );
        return max != 255 || *width <= 0 || *height <= 0;
    }
    return 1;
}

/** Close the pipe and process.
*/

static void rwpipe_close( rwpipe *this )
{
    if ( this != NULL )
    {
        fclose( this->reader );
        fclose( this->writer );
        waitpid( this->pid, NULL, 0 );
        av_free( this );
    }
}

/** Context info for this vhook - stores the pipe and image buffers.
*/

typedef struct
{
    rwpipe *rw;
    int size1;
    char *buf1;
    int size2;
    char *buf2;

    // This vhook first converts frame to RGB ...
    struct SwsContext *toRGB_convert_ctx;
    // ... then processes it via a PPM command pipe ...
    // ... and finally converts back frame from RGB to initial format
    struct SwsContext *fromRGB_convert_ctx;
}
ContextInfo;

/** Initialise the context info for this vhook.
*/

int Configure(void **ctxp, int argc, char *argv[])
{
    if ( argc > 1 )
    {
        *ctxp = av_mallocz(sizeof(ContextInfo));
        if ( *ctxp != NULL && argc > 1 )
        {
            ContextInfo *info = (ContextInfo *)*ctxp;
            info->rw = rwpipe_open( argc - 1, &argv[ 1 ] );
            return 0;
        }
    }
    return 1;
}

/** Process a frame.
*/

void Process(void *ctx, AVPicture *picture, enum PixelFormat pix_fmt, int width, int height, int64_t pts)
{
    int err = 0;
    ContextInfo *ci = (ContextInfo *) ctx;
    AVPicture picture1;
    AVPicture picture2;
    AVPicture *pict = picture;
    int out_width;
    int out_height;
    int i;
    uint8_t *ptr = NULL;
    FILE *in = rwpipe_reader( ci->rw );
    FILE *out = rwpipe_writer( ci->rw );

    /* Check that we have a pipe to talk to. */
    if ( in == NULL || out == NULL )
        err = 1;

    /* Convert to RGB24 if necessary */
    if ( !err && pix_fmt != PIX_FMT_RGB24 )
    {
        int size = avpicture_get_size(PIX_FMT_RGB24, width, height);

        if ( size != ci->size1 )
        {
            av_free( ci->buf1 );
            ci->buf1 = av_malloc(size);
            ci->size1 = size;
            err = ci->buf1 == NULL;
        }

        if ( !err )
        {
            avpicture_fill(&picture1, ci->buf1, PIX_FMT_RGB24, width, height);

            // if we already got a SWS context, let's realloc if is not re-useable
            ci->toRGB_convert_ctx = sws_getCachedContext(ci->toRGB_convert_ctx,
                                        width, height, pix_fmt,
                                        width, height, PIX_FMT_RGB24,
                                        sws_flags, NULL, NULL, NULL);
            if (ci->toRGB_convert_ctx == NULL) {
                av_log(NULL, AV_LOG_ERROR,
                       "Cannot initialize the toRGB conversion context\n");
                return;
            }

// img_convert parameters are          2 first destination, then 4 source
// sws_scale   parameters are context, 4 first source,      then 2 destination
            sws_scale(ci->toRGB_convert_ctx,
                     picture->data, picture->linesize, 0, height,
                     picture1.data, picture1.linesize);

            pict = &picture1;
        }
    }

    /* Write out the PPM */
    if ( !err )
    {
        ptr = pict->data[ 0 ];
        fprintf( out, "P6\n%d %d\n255\n", width, height );
        for ( i = 0; !err && i < height; i ++ )
        {
            err = !fwrite( ptr, width * 3, 1, out );
            ptr += pict->linesize[ 0 ];
        }
        if ( !err )
            err = fflush( out );
    }

    /* Read the PPM returned. */
    if ( !err && !rwpipe_read_ppm_header( ci->rw, &out_width, &out_height ) )
    {
        int size = avpicture_get_size(PIX_FMT_RGB24, out_width, out_height);

        if ( size != ci->size2 )
        {
            av_free( ci->buf2 );
            ci->buf2 = av_malloc(size);
            ci->size2 = size;
            err = ci->buf2 == NULL;
        }

        if ( !err )
        {
            avpicture_fill(&picture2, ci->buf2, PIX_FMT_RGB24, out_width, out_height);
            ptr = picture2.data[ 0 ];
            for ( i = 0; !err && i < out_height; i ++ )
            {
                err = !fread( ptr, out_width * 3, 1, in );
                ptr += picture2.linesize[ 0 ];
            }
        }
    }

    /* Convert the returned PPM back to the input format */
    if ( !err )
    {
        /* The out_width/out_height returned from the PPM
         * filter won't necessarily be the same as width and height
         * but it will be scaled anyway to width/height.
         */
        av_log(NULL, AV_LOG_DEBUG,
                  "PPM vhook: Input dimensions: %d x %d Output dimensions: %d x %d\n",
                  width, height, out_width, out_height);
        ci->fromRGB_convert_ctx = sws_getCachedContext(ci->fromRGB_convert_ctx,
                                        out_width, out_height, PIX_FMT_RGB24,
                                        width,     height,     pix_fmt,
                                        sws_flags, NULL, NULL, NULL);
        if (ci->fromRGB_convert_ctx == NULL) {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot initialize the fromRGB conversion context\n");
            return;
        }

// img_convert parameters are          2 first destination, then 4 source
// sws_scale   parameters are context, 4 first source,      then 2 destination
        sws_scale(ci->fromRGB_convert_ctx,
                 picture2.data, picture2.linesize, 0, out_height,
                 picture->data, picture->linesize);
    }
}

/** Clean up the effect.
*/

void Release(void *ctx)
{
    ContextInfo *ci;
    ci = (ContextInfo *) ctx;

    if (ctx)
    {
        rwpipe_close( ci->rw );
        av_free( ci->buf1 );
        av_free( ci->buf2 );
        sws_freeContext(ci->toRGB_convert_ctx);
        sws_freeContext(ci->fromRGB_convert_ctx);
        av_free(ctx);
    }
}

