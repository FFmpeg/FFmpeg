/*
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
#include <stdint.h>
#include <stdlib.h>

#include "decode_simple.h"

#include "libavutil/common.h"
#include "libavutil/dict.h"
#include "libavutil/error.h"
#include "libavutil/video_enc_params.h"

#include "libavformat/avformat.h"

#include "libavcodec/avcodec.h"

static int process_frame(DecodeContext *dc, AVFrame *frame)
{
    AVFrameSideData *sd;

    if (!frame)
        return 0;

    fprintf(stdout, "frame %d\n", dc->decoder->frame_number - 1);

    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_VIDEO_ENC_PARAMS);
    if (sd) {
        AVVideoEncParams *par = (AVVideoEncParams*)sd->data;

        fprintf(stdout, "AVVideoEncParams %d\n", par->type);
        fprintf(stdout, "qp %d\n", par->qp);
        for (int i = 0; i < FF_ARRAY_ELEMS(par->delta_qp); i++)
            for (int j = 0; j < FF_ARRAY_ELEMS(par->delta_qp[i]); j++) {
                if (par->delta_qp[i][j])
                    fprintf(stdout, "delta_qp[%d][%d] %"PRId32"\n", i, j, par->delta_qp[i][j]);
            }

        if (par->nb_blocks) {
            fprintf(stdout, "nb_blocks %d\n", par->nb_blocks);
            for (int i = 0; i < par->nb_blocks; i++) {
                AVVideoBlockParams *b = av_video_enc_params_block(par, i);

                fprintf(stdout, "block %d %d:%d %dx%d %"PRId32"\n",
                        i, b->src_x, b->src_y, b->w, b->h, b->delta_qp);
            }
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    DecodeContext dc;

    unsigned int stream_idx, max_frames;
    const char *filename, *thread_type = NULL, *nb_threads = NULL;
    int ret = 0;

    if (argc <= 3) {
        fprintf(stderr, "Usage: %s <input file> <stream index> <max frame count> [<thread count> <thread type>]\n", argv[0]);
        return 0;
    }

    filename    = argv[1];
    stream_idx  = strtol(argv[2], NULL, 0);
    max_frames  = strtol(argv[3], NULL, 0);
    if (argc > 5) {
        nb_threads  = argv[4];
        thread_type = argv[5];
    }

    ret = ds_open(&dc, filename, stream_idx);
    if (ret < 0)
        goto finish;

    dc.process_frame = process_frame;
    dc.max_frames    = max_frames;

    ret  = av_dict_set(&dc.decoder_opts, "threads",          nb_threads,    0);
    ret |= av_dict_set(&dc.decoder_opts, "thread_type",      thread_type,   0);
    ret |= av_dict_set(&dc.decoder_opts, "export_side_data", "venc_params", 0);

    if (ret < 0)
        goto finish;

    ret = ds_run(&dc);

finish:
    ds_free(&dc);
    return ret;
}
