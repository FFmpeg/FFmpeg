/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>

#include "config_components.h"

#include "checkasm.h"

#include "libavcodec/ttadata.h"
#include "libavcodec/ttadsp.h"
#include "libavcodec/ttaencdsp.h"
#include "libavutil/mem_internal.h"

#define randomize_buffer(NAME)                                \
do {                                                          \
    checkasm_randomize(NAME ## _ref, sizeof(NAME ## _ref));   \
    memcpy(NAME ## _new, NAME ## _ref, sizeof(NAME ## _new)); \
} while (0)

static void check_filter_process(void)
{
    DECLARE_ALIGNED_16(int32_t, qm_ref)[MAX_ORDER];
    DECLARE_ALIGNED_16(int32_t, dx_ref)[MAX_ORDER];
    DECLARE_ALIGNED_16(int32_t, dl_ref)[MAX_ORDER];
    DECLARE_ALIGNED_16(int32_t, qm_new)[MAX_ORDER];
    DECLARE_ALIGNED_16(int32_t, dx_new)[MAX_ORDER];
    DECLARE_ALIGNED_16(int32_t, dl_new)[MAX_ORDER];
    int bps = 1 + rnd() % 3;
    int32_t shift = ff_tta_filter_configs[bps - 1], round = ff_tta_shift_1[shift - 1];
    int32_t in = rnd(), error_ref = rnd(), error_new = error_ref;

    declare_func(int32_t, int32_t *qm, int32_t *dx, int32_t *dl, int32_t *error,
                          int32_t in,  int32_t shift, int32_t round);

    randomize_buffer(qm);
    randomize_buffer(dx);
    randomize_buffer(dl);

    int32_t out_ref = call_ref(qm_ref, dx_ref, dl_ref, &error_ref, in, shift, round);
    int32_t out_new = call_new(qm_new, dx_new, dl_new, &error_new, in, shift, round);

    if (out_ref != out_new || error_ref != error_new ||
        memcmp(qm_ref, qm_new, sizeof(qm_ref))       ||
        memcmp(dx_ref, dx_new, sizeof(dx_ref))       ||
        memcmp(dl_ref, dl_new, sizeof(dl_ref)))
        fail();
#define alt(var) checkasm_alternate(var ## _ref, var ## _new)
    bench_new(alt(qm), alt(dx), alt(dl), alt(&error), in, shift, round);
}

#if CONFIG_TTA_DECODER
void checkasm_check_ttadsp(void)
{
    TTADSPContext ttadsp;

    ff_ttadsp_init(&ttadsp);

    if (check_func(ttadsp.filter_process, "filter_process"))
        check_filter_process();
    report("filter_process");
}
#endif

#if CONFIG_TTA_ENCODER
void checkasm_check_ttaencdsp(void)
{
    TTAEncDSPContext ttaencdsp;

    ff_ttaencdsp_init(&ttaencdsp);

    if (check_func(ttaencdsp.filter_process, "enc_filter_process"))
        check_filter_process();
    report("filter_process");
}
#endif
