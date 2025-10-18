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

#include <cairo.h>
#include <stdarg.h>
#include <stdio.h>

#include "libavutil/log.h"
#include "libavutil/pixdesc.h"

static void mock_av_log(void *ptr, int level, const char *fmt, va_list vl) {
    printf("av_log[%d]: ", level);
    vprintf(fmt, vl);
}

#include "libavfilter/vf_drawvg.c"

// Mock for cairo functions.
//
// `MOCK_FN_n` macros define wrappers for functions that only receive `n`
// arguments of type `double`.
//
// `MOCK_FN_I` macro wrap a function that receives a single integer value.

struct _cairo {
    double current_point_x;
    double current_point_y;
};

static void update_current_point(cairo_t *cr, const char *func, double x, double y) {
    // Update current point only if the function name contains `_to`.
    if (strstr(func, "_to") == NULL) {
        return;
    }

    if (strstr(func, "_rel_") == NULL) {
        cr->current_point_x = x;
        cr->current_point_y = y;
    } else {
        cr->current_point_x += x;
        cr->current_point_y += y;
    }
}

#define MOCK_FN_0(func)      \
    void func(cairo_t* cr) { \
        puts(#func);         \
    }

#define MOCK_FN_1(func)                 \
    void func(cairo_t* cr, double a0) { \
        printf(#func " %.1f\n", a0);    \
    }

#define MOCK_FN_2(func)                            \
    void func(cairo_t* cr, double a0, double a1) { \
        update_current_point(cr, #func, a0, a1);   \
        printf(#func " %.1f %.1f\n", a0, a1);      \
    }

#define MOCK_FN_4(func)                                                  \
    void func(cairo_t* cr, double a0, double a1, double a2, double a3) { \
        printf(#func " %.1f %.1f %.1f %.1f\n", a0, a1, a2, a3);          \
    }

#define MOCK_FN_5(func)                                                             \
    void func(cairo_t* cr, double a0, double a1, double a2, double a3, double a4) { \
        printf(#func " %.1f %.1f %.1f %.1f %.1f\n", a0, a1, a2, a3, a4);            \
    }

#define MOCK_FN_6(func)                                                                        \
    void func(cairo_t* cr, double a0, double a1, double a2, double a3, double a4, double a5) { \
        update_current_point(cr, #func, a4, a5);                                               \
        printf(#func " %.1f %.1f %.1f %.1f %.1f %.1f\n", a0, a1, a2, a3, a4, a5);              \
    }

#define MOCK_FN_I(func, type)          \
    void func(cairo_t* cr, type i) {   \
        printf(#func " %d\n", (int)i); \
    }

MOCK_FN_5(cairo_arc);
MOCK_FN_0(cairo_clip);
MOCK_FN_0(cairo_clip_preserve);
MOCK_FN_0(cairo_close_path);
MOCK_FN_6(cairo_curve_to);
MOCK_FN_0(cairo_fill);
MOCK_FN_0(cairo_fill_preserve);
MOCK_FN_0(cairo_identity_matrix);
MOCK_FN_2(cairo_line_to);
MOCK_FN_2(cairo_move_to);
MOCK_FN_0(cairo_new_path);
MOCK_FN_0(cairo_new_sub_path);
MOCK_FN_4(cairo_rectangle);
MOCK_FN_6(cairo_rel_curve_to);
MOCK_FN_2(cairo_rel_line_to);
MOCK_FN_2(cairo_rel_move_to);
MOCK_FN_0(cairo_reset_clip);
MOCK_FN_0(cairo_restore);
MOCK_FN_1(cairo_rotate);
MOCK_FN_0(cairo_save);
MOCK_FN_2(cairo_scale);
MOCK_FN_I(cairo_set_fill_rule, cairo_fill_rule_t);
MOCK_FN_1(cairo_set_font_size);
MOCK_FN_I(cairo_set_line_cap, cairo_line_cap_t);
MOCK_FN_I(cairo_set_line_join, cairo_line_join_t);
MOCK_FN_1(cairo_set_line_width);
MOCK_FN_1(cairo_set_miter_limit);
MOCK_FN_4(cairo_set_source_rgba);
MOCK_FN_0(cairo_stroke);
MOCK_FN_0(cairo_stroke_preserve);
MOCK_FN_2(cairo_translate);

cairo_bool_t cairo_get_dash_count(cairo_t *cr) {
    return 1;
}

cairo_status_t cairo_status(cairo_t *cr) {
    return CAIRO_STATUS_SUCCESS;
}

void cairo_get_dash(cairo_t *cr, double *dashes, double *offset) {
    // Return a dummy value to verify that it is included in
    // the next call to `cairo_set_dash`.
    *dashes = -1;

    if (offset)
        *offset = -2;
}

void cairo_set_dash(cairo_t *cr, const double *dashes, int num_dashes, double offset) {
    printf("%s [", __func__);
    for (int i = 0; i < num_dashes; i++)
        printf(" %.1f", dashes[i]);
    printf(" ] %.1f\n", offset);
}

cairo_bool_t cairo_has_current_point(cairo_t *cr) {
    return 1;
}

void cairo_get_current_point(cairo_t *cr, double *x, double *y) {
    *x = cr->current_point_x;
    *y = cr->current_point_y;
}

void cairo_set_source(cairo_t *cr, cairo_pattern_t *source) {
    int count;
    double r, g, b, a;
    double x0, y0, x1, y1, r0, r1;

    printf("%s", __func__);

#define PRINT_COLOR(prefix) \
    printf(prefix "#%02x%02x%02x%02x", (int)(r*255), (int)(g*255), (int)(b*255), (int)(a*255))

    switch (cairo_pattern_get_type(source)) {
    case CAIRO_PATTERN_TYPE_SOLID:
        cairo_pattern_get_rgba(source, &r, &g, &b, &a);
        PRINT_COLOR(" ");
        break;

    case CAIRO_PATTERN_TYPE_LINEAR:
        cairo_pattern_get_linear_points(source, &x0, &y0, &x1, &y1);
        printf(" lineargrad(%.1f %.1f %.1f %.1f)", x0, y0, x1, y1);
        break;

    case CAIRO_PATTERN_TYPE_RADIAL:
        cairo_pattern_get_radial_circles(source, &x0, &y0, &r0, &x1, &y1, &r1);
        printf(" radialgrad(%.1f %.1f %.1f %.1f %.1f %.1f)", x0, y0, r0, x1, y1, r1);
        break;
    }

    if (cairo_pattern_get_color_stop_count(source, &count) == CAIRO_STATUS_SUCCESS) {
        for (int i = 0; i < count; i++) {
            cairo_pattern_get_color_stop_rgba(source, i, &x0, &r, &g, &b, &a);
            printf(" %.1f/", x0);
            PRINT_COLOR("");
        }
    }

    printf("\n");
}

// Verify that the `vgs_commands` array is sorted, so it can
// be used with `bsearch(3)`.
static void check_sorted_cmds_array(void) {
    int failures = 0;

    for (int i = 0; i < FF_ARRAY_ELEMS(vgs_commands) - 1; i++) {
        if (vgs_comp_command_spec(&vgs_commands[i], &vgs_commands[i]) != 0) {
            printf("%s: comparator must return 0 for item %d\n", __func__, i);
            failures++;
        }

        if (vgs_comp_command_spec(&vgs_commands[i], &vgs_commands[i + 1]) >= 0) {
            printf("%s: entry for '%s' must appear after '%s', at index %d\n",
                __func__, vgs_commands[i].name, vgs_commands[i + 1].name, i);
            failures++;
        }
    }

    printf("%s: %d failures\n", __func__, failures);
}

// Compile and run a script.
static void check_script(int is_file, const char* source) {
    int ret;

    AVDictionary *metadata = NULL;

    struct VGSEvalState state;
    struct VGSParser parser;
    struct VGSProgram program;

    struct _cairo cairo_ctx = { 0, 0 };

    if (is_file) {
        uint8_t *s = NULL;

        printf("\n--- %s: %s\n", __func__, av_basename(source));

        ret = ff_load_textfile(NULL, source, &s, NULL);
        if (ret != 0) {
            printf("Failed to read %s: %d\n", source, ret);
            return;
        }

        source = s;
    } else {
        printf("\n--- %s: %s\n", __func__, source);
    }

    ret = av_dict_parse_string(&metadata, "m.a=1:m.b=2", "=", ":", 0);
    av_assert0(ret == 0);

    vgs_parser_init(&parser, source);

    ret = vgs_parse(NULL, &parser, &program, 0);

    int init_ret = vgs_eval_state_init(&state, &program, NULL, NULL);
    av_assert0(init_ret == 0);

    for (int i = 0; i < VAR_COUNT; i++)
        state.vars[i] = 1 << i;

    vgs_parser_free(&parser);

    if (ret != 0) {
        printf("%s: vgs_parse = %d\n", __func__, ret);
        goto exit;
    }

    state.metadata = metadata;
    state.cairo_ctx = &cairo_ctx;

    ret = vgs_eval(&state, &program);
    vgs_eval_state_free(&state);

    if (ret != 0)
        printf("%s: vgs_eval = %d\n", __func__, ret);

exit:
    av_dict_free(&metadata);

    if (is_file)
        av_free((void*)source);

    vgs_free(&program);
}

int main(int argc, const char **argv)
{
    char buf[512];

    av_log_set_callback(mock_av_log);

    check_sorted_cmds_array();

    for (int i = 1; i < argc; i++)
        check_script(1, argv[i]);

    // Detect unclosed expressions.
    check_script(0, "M 0 (1*(t+1)");

    // Invalid command.
    check_script(0, "save invalid 1 2");

    // Invalid constant.
    check_script(0, "setlinecap unknown m 10 20");

    // Missing arguments.
    check_script(0, "M 0 1 2");

    // Invalid variable names.
    check_script(0, "setvar ba^d 0");

    // Reserved names.
    check_script(0, "setvar cx 0");

    // Max number of user variables.
    memset(buf, 0, sizeof(buf));
    for (int i = 0; i < USER_VAR_COUNT; i++) {
        av_strlcatf(buf, sizeof(buf), " setvar v%d %d", i, i);
    }
    av_strlcatf(buf, sizeof(buf), " M (v0) (v%d) 1 (unknown_var)", USER_VAR_COUNT - 1);
    check_script(0, buf);

    // Too many variables.
    memset(buf, 0, sizeof(buf));
    for (int i = 0; i < USER_VAR_COUNT + 1; i++) {
        av_strlcatf(buf, sizeof(buf), " setvar v%d %d", i + 1, i);
    }
    check_script(0, buf);

    // Invalid procedure names.
    check_script(0, "call a");
    check_script(0, "proc a { call b } call a");

    // Invalid arguments list.
    check_script(0, "proc p0 a1 a2 a3 a4 a5 a6 a7 a8 { break }");
    check_script(0, "proc p0 a1 a2 { break } call p0 break");
    check_script(0, "proc p0 a1 a2 { break } call p0 1 2 3");

    // Long expressions.
    memset(buf, 0, sizeof(buf));
    strncat(buf, "M 0 (1", sizeof(buf) - 1);
    for (int i = 0; i < 100; i++) {
        strncat(buf, " + n", sizeof(buf) - 1);
    }
    strncat(buf, ")", sizeof(buf) - 1);
    check_script(0, buf);

    return 0;
}
