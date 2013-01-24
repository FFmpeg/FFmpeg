/*
 * log functions
 * Copyright (c) 2003 Michel Bardiaux
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
#include <stdlib.h>
#include "avutil.h"
#include "common.h"
#include "log.h"

#define LINE_SZ 1024

static int av_log_level = AV_LOG_INFO;
static int flags;

#if HAVE_SETCONSOLETEXTATTRIBUTE
#include <windows.h>
static const uint8_t color[16 + AV_CLASS_CATEGORY_NB] = {
    [AV_LOG_PANIC  /8] = 12,
    [AV_LOG_FATAL  /8] = 12,
    [AV_LOG_ERROR  /8] = 12,
    [AV_LOG_WARNING/8] = 14,
    [AV_LOG_INFO   /8] =  7,
    [AV_LOG_VERBOSE/8] = 10,
    [AV_LOG_DEBUG  /8] = 10,
    [16+AV_CLASS_CATEGORY_NA              ] =  7,
    [16+AV_CLASS_CATEGORY_INPUT           ] = 13,
    [16+AV_CLASS_CATEGORY_OUTPUT          ] =  5,
    [16+AV_CLASS_CATEGORY_MUXER           ] = 13,
    [16+AV_CLASS_CATEGORY_DEMUXER         ] =  5,
    [16+AV_CLASS_CATEGORY_ENCODER         ] = 11,
    [16+AV_CLASS_CATEGORY_DECODER         ] =  3,
    [16+AV_CLASS_CATEGORY_FILTER          ] = 10,
    [16+AV_CLASS_CATEGORY_BITSTREAM_FILTER] =  9,
    [16+AV_CLASS_CATEGORY_SWSCALER        ] =  7,
    [16+AV_CLASS_CATEGORY_SWRESAMPLER     ] =  7,
};

static int16_t background, attr_orig;
static HANDLE con;
#define set_color(x)  SetConsoleTextAttribute(con, background | color[x])
#define set_256color set_color
#define reset_color() SetConsoleTextAttribute(con, attr_orig)
#else

static const uint32_t color[16 + AV_CLASS_CATEGORY_NB] = {
    [AV_LOG_PANIC  /8] =  52 << 16 | 196 << 8 | 0x41,
    [AV_LOG_FATAL  /8] = 208 <<  8 | 0x41,
    [AV_LOG_ERROR  /8] = 196 <<  8 | 0x11,
    [AV_LOG_WARNING/8] = 226 <<  8 | 0x03,
    [AV_LOG_INFO   /8] = 253 <<  8 | 0x09,
    [AV_LOG_VERBOSE/8] =  40 <<  8 | 0x02,
    [AV_LOG_DEBUG  /8] =  34 <<  8 | 0x02,
    [16+AV_CLASS_CATEGORY_NA              ] = 250 << 8 | 0x09,
    [16+AV_CLASS_CATEGORY_INPUT           ] = 219 << 8 | 0x15,
    [16+AV_CLASS_CATEGORY_OUTPUT          ] = 201 << 8 | 0x05,
    [16+AV_CLASS_CATEGORY_MUXER           ] = 213 << 8 | 0x15,
    [16+AV_CLASS_CATEGORY_DEMUXER         ] = 207 << 8 | 0x05,
    [16+AV_CLASS_CATEGORY_ENCODER         ] =  51 << 8 | 0x16,
    [16+AV_CLASS_CATEGORY_DECODER         ] =  39 << 8 | 0x06,
    [16+AV_CLASS_CATEGORY_FILTER          ] = 155 << 8 | 0x12,
    [16+AV_CLASS_CATEGORY_BITSTREAM_FILTER] = 192 << 8 | 0x14,
    [16+AV_CLASS_CATEGORY_SWSCALER        ] = 153 << 8 | 0x14,
    [16+AV_CLASS_CATEGORY_SWRESAMPLER     ] = 147 << 8 | 0x14,
};

#define set_color(x)  fprintf(stderr, "\033[%d;3%dm", (color[x] >> 4) & 15, color[x] & 15)
#define set_256color(x) fprintf(stderr, "\033[48;5;%dm\033[38;5;%dm", (color[x] >> 16) & 0xff, (color[x] >> 8) & 0xff)
#define reset_color() fprintf(stderr, "\033[0m")
#endif
static int use_color = -1;

static void colored_fputs(int level, const char *str)
{
    if (use_color < 0) {
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
        use_color = !getenv("NO_COLOR") && !getenv("AV_LOG_FORCE_NOCOLOR") &&
                    (getenv("TERM") && isatty(2) ||
                     getenv("AV_LOG_FORCE_COLOR"));
        if (getenv("AV_LOG_FORCE_256COLOR"))
            use_color *= 256;
#else
        use_color = getenv("AV_LOG_FORCE_COLOR") && !getenv("NO_COLOR") &&
                   !getenv("AV_LOG_FORCE_NOCOLOR");
#endif
    }

    if (use_color == 1) {
        set_color(level);
    } else if (use_color == 256)
        set_256color(level);
    fputs(str, stderr);
    if (use_color) {
        reset_color();
    }
}

const char *av_default_item_name(void *ptr)
{
    return (*(AVClass **) ptr)->class_name;
}

AVClassCategory av_default_get_category(void *ptr)
{
    return (*(AVClass **) ptr)->category;
}

static void sanitize(uint8_t *line){
    while(*line){
        if(*line < 0x08 || (*line > 0x0D && *line < 0x20))
            *line='?';
        line++;
    }
}

static int get_category(void *ptr){
    AVClass *avc = *(AVClass **) ptr;
    if(    !avc
        || (avc->version&0xFF)<100
        ||  avc->version < (51 << 16 | 59 << 8)
        ||  avc->category >= AV_CLASS_CATEGORY_NB) return AV_CLASS_CATEGORY_NA + 16;

    if(avc->get_category)
        return avc->get_category(ptr) + 16;

    return avc->category + 16;
}

static void format_line(void *ptr, int level, const char *fmt, va_list vl,
                        char part[3][LINE_SZ], int part_size, int *print_prefix, int type[2])
{
    AVClass* avc = ptr ? *(AVClass **) ptr : NULL;
    part[0][0] = part[1][0] = part[2][0] = 0;
    if(type) type[0] = type[1] = AV_CLASS_CATEGORY_NA + 16;
    if (*print_prefix && avc) {
        if (avc->parent_log_context_offset) {
            AVClass** parent = *(AVClass ***) (((uint8_t *) ptr) +
                                   avc->parent_log_context_offset);
            if (parent && *parent) {
                snprintf(part[0], part_size, "[%s @ %p] ",
                         (*parent)->item_name(parent), parent);
                if(type) type[0] = get_category(((uint8_t *) ptr) + avc->parent_log_context_offset);
            }
        }
        snprintf(part[1], part_size, "[%s @ %p] ",
                 avc->item_name(ptr), ptr);
        if(type) type[1] = get_category(ptr);
    }

    vsnprintf(part[2], part_size, fmt, vl);

    *print_prefix = strlen(part[2]) && part[2][strlen(part[2]) - 1] == '\n';
}

void av_log_format_line(void *ptr, int level, const char *fmt, va_list vl,
                        char *line, int line_size, int *print_prefix)
{
    char part[3][LINE_SZ];
    format_line(ptr, level, fmt, vl, part, sizeof(part[0]), print_prefix, NULL);
    snprintf(line, line_size, "%s%s%s", part[0], part[1], part[2]);
}

void av_log_default_callback(void* ptr, int level, const char* fmt, va_list vl)
{
    static int print_prefix = 1;
    static int count;
    static char prev[LINE_SZ];
    char part[3][LINE_SZ];
    char line[LINE_SZ];
    static int is_atty;
    int type[2];

    if (level > av_log_level)
        return;
    format_line(ptr, level, fmt, vl, part, sizeof(part[0]), &print_prefix, type);
    snprintf(line, sizeof(line), "%s%s%s", part[0], part[1], part[2]);

#if HAVE_ISATTY
    if (!is_atty)
        is_atty = isatty(2) ? 1 : -1;
#endif

    if (print_prefix && (flags & AV_LOG_SKIP_REPEATED) && !strcmp(line, prev)){
        count++;
        if (is_atty == 1)
            fprintf(stderr, "    Last message repeated %d times\r", count);
        return;
    }
    if (count > 0) {
        fprintf(stderr, "    Last message repeated %d times\n", count);
        count = 0;
    }
    strcpy(prev, line);
    sanitize(part[0]);
    colored_fputs(type[0], part[0]);
    sanitize(part[1]);
    colored_fputs(type[1], part[1]);
    sanitize(part[2]);
    colored_fputs(av_clip(level >> 3, 0, 6), part[2]);
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
    if(av_log_callback)
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
