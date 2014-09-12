/*
 * log functions
 * Copyright (c) 2003 Michel Bardiaux
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * logging functions
 */

#include "config.h"

#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_IO_H
#include <io.h>
#endif
#include <stdarg.h>
#include <stdlib.h>
#include "avstring.h"
#include "avutil.h"
#include "common.h"
#include "internal.h"
#include "log.h"

static int av_log_level = AV_LOG_INFO;
static int flags;

#if HAVE_SETCONSOLETEXTATTRIBUTE
#include <windows.h>
static const uint8_t color[] = { 12, 12, 12, 14, 7, 10, 11 };
static int16_t background, attr_orig;
static HANDLE con;
#define set_color(x)  SetConsoleTextAttribute(con, background | color[x])
#define reset_color() SetConsoleTextAttribute(con, attr_orig)
#define print_256color(x)
#else
static const uint8_t color[] = { 0x41, 0x41, 0x11, 0x03, 9, 0x02, 0x06 };
#define set_color(x)  fprintf(stderr, "\033[%d;3%dm", color[x] >> 4, color[x]&15)
#define print_256color(x) fprintf(stderr, "\033[38;5;%dm", x)
#define reset_color() fprintf(stderr, "\033[0m")
#endif
static int use_color = -1;

static void check_color_terminal(void)
{
#if HAVE_SETCONSOLETEXTATTRIBUTE
    CONSOLE_SCREEN_BUFFER_INFO con_info;
    con = GetStdHandle(STD_ERROR_HANDLE);
    use_color = (con != INVALID_HANDLE_VALUE) && !getenv("NO_COLOR") &&
                !getenv("AV_LOG_FORCE_NOCOLOR");
    if (use_color) {
        GetConsoleScreenBufferInfo(con, &con_info);
        attr_orig  = con_info.wAttributes;
        background = attr_orig & 0xF0;
    }
#elif HAVE_ISATTY
    char *term = getenv("TERM");
    use_color = !getenv("NO_COLOR") && !getenv("AV_LOG_FORCE_NOCOLOR") &&
                (getenv("TERM") && isatty(2) || getenv("AV_LOG_FORCE_COLOR"));
    if (use_color)
        use_color += term && strstr(term, "256color");
#else
    use_color = getenv("AV_LOG_FORCE_COLOR") && !getenv("NO_COLOR") &&
               !getenv("AV_LOG_FORCE_NOCOLOR");
#endif
}

static void colored_fputs(int level, int tint, const char *str)
{
    if (use_color < 0)
        check_color_terminal();

    switch (use_color) {
    case 1:
        set_color(level);
        break;
    case 2:
        set_color(level);
        if (tint)
            print_256color(tint);
        break;
    default:
        break;
    }
    fputs(str, stderr);
    if (use_color) {
        reset_color();
    }
}

const char *av_default_item_name(void *ptr)
{
    return (*(AVClass **) ptr)->class_name;
}

void av_log_default_callback(void *avcl, int level, const char *fmt, va_list vl)
{
    static int print_prefix = 1;
    static int count;
    static char prev[1024];
    char line[1024];
    static int is_atty;
    AVClass* avc = avcl ? *(AVClass **) avcl : NULL;
    unsigned tint = level & 0xff00;

    level &= 0xff;

    if (level > av_log_level)
        return;
    line[0] = 0;
    if (print_prefix && avc) {
        if (avc->parent_log_context_offset) {
            AVClass** parent = *(AVClass ***) (((uint8_t *) avcl) +
                                   avc->parent_log_context_offset);
            if (parent && *parent) {
                snprintf(line, sizeof(line), "[%s @ %p] ",
                         (*parent)->item_name(parent), parent);
            }
        }
        snprintf(line + strlen(line), sizeof(line) - strlen(line), "[%s @ %p] ",
                 avc->item_name(avcl), avcl);
    }

    vsnprintf(line + strlen(line), sizeof(line) - strlen(line), fmt, vl);

    print_prefix = strlen(line) && line[strlen(line) - 1] == '\n';

#if HAVE_ISATTY
    if (!is_atty)
        is_atty = isatty(2) ? 1 : -1;
#endif

    if (print_prefix && (flags & AV_LOG_SKIP_REPEATED) &&
        !strncmp(line, prev, sizeof line)) {
        count++;
        if (is_atty == 1)
            fprintf(stderr, "    Last message repeated %d times\r", count);
        return;
    }
    if (count > 0) {
        fprintf(stderr, "    Last message repeated %d times\n", count);
        count = 0;
    }
    colored_fputs(av_clip(level >> 3, 0, 6), tint >> 8, line);
    av_strlcpy(prev, line, sizeof line);
}

static void (*av_log_callback)(void*, int, const char*, va_list) =
    av_log_default_callback;

void av_log(void* avcl, int level, const char *fmt, ...)
{
    AVClass* avc = avcl ? *(AVClass **) avcl : NULL;
    va_list vl;
    va_start(vl, fmt);
    if (avc && avc->version >= (50 << 16 | 15 << 8 | 2) &&
        avc->log_level_offset_offset && level >= AV_LOG_FATAL)
        level += *(int *) (((uint8_t *) avcl) + avc->log_level_offset_offset);
    av_vlog(avcl, level, fmt, vl);
    va_end(vl);
}

void av_vlog(void* avcl, int level, const char *fmt, va_list vl)
{
    av_log_callback(avcl, level, fmt, vl);
}

int av_log_get_level(void)
{
    return av_log_level;
}

void av_log_set_level(int level)
{
    av_log_level = level;
}

void av_log_set_flags(int arg)
{
    flags = arg;
}

void av_log_set_callback(void (*callback)(void*, int, const char*, va_list))
{
    av_log_callback = callback;
}

static void missing_feature_sample(int sample, void *avc, const char *msg,
                                   va_list argument_list)
{
    av_vlog(avc, AV_LOG_WARNING, msg, argument_list);
    av_log(avc, AV_LOG_WARNING, " is not implemented. Update your Libav "
           "version to the newest one from Git. If the problem still "
           "occurs, it means that your file has a feature which has not "
           "been implemented.\n");
    if (sample)
        av_log(avc, AV_LOG_WARNING, "If you want to help, upload a sample "
               "of this file to ftp://upload.libav.org/incoming/ "
               "and contact the libav-devel mailing list.\n");
}

void avpriv_request_sample(void *avc, const char *msg, ...)
{
    va_list argument_list;

    va_start(argument_list, msg);
    missing_feature_sample(1, avc, msg, argument_list);
    va_end(argument_list);
}

void avpriv_report_missing_feature(void *avc, const char *msg, ...)
{
    va_list argument_list;

    va_start(argument_list, msg);
    missing_feature_sample(0, avc, msg, argument_list);
    va_end(argument_list);
}
