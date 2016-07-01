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

#include "config.h"
#include "libavutil/mem.h"
#include "libavcodec/avfft.h"

int main(int argc, char **argv)
{
    int i;
#define LEN 1024
    FFTSample *ref  = av_malloc_array(LEN, sizeof(*ref));
    FFTSample *data = av_malloc_array(LEN, sizeof(*data));
    RDFTContext *rdft_context  = av_rdft_init(10, DFT_R2C);
    RDFTContext *irdft_context = av_rdft_init(10, IDFT_C2R);

    if (!ref || !data || !rdft_context || !irdft_context)
        return 2;
    for (i=0; i<LEN; i++) {
        ref[i] = data[i] = i*456 + 123 + i*i;
    }
    av_rdft_calc(rdft_context, data);
    av_rdft_calc(irdft_context, data);

    for (i=0; i<LEN; i++) {
        if (fabs(ref[i] - data[i]/LEN*2) > 1) {
            fprintf(stderr, "Failed at %d (%f %f)\n", i, ref[i], data[i]/LEN*2);
            return 1;
        }
    }

    av_rdft_end(rdft_context);
    av_rdft_end(irdft_context);
    av_free(data);
    av_free(ref);

    return 0;
}
