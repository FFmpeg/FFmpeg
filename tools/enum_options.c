/*
 * Copyright (c) 2011 Anton Khirnov
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

/*
 * enumerate avoptions and format them in texinfo format
 */

#include <string.h>

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"

static void print_usage(void)
{
    fprintf(stderr, "Usage: enum_options type\n"
            "type: format codec\n");
    exit(1);
}

static void print_option(const AVClass *class, const AVOption *o)
{
    printf("@item -%s @var{", o->name);
    switch (o->type) {
    case FF_OPT_TYPE_BINARY:   printf("hexadecimal string"); break;
    case FF_OPT_TYPE_STRING:   printf("string");             break;
    case FF_OPT_TYPE_INT:
    case FF_OPT_TYPE_INT64:    printf("integer");            break;
    case FF_OPT_TYPE_FLOAT:
    case FF_OPT_TYPE_DOUBLE:   printf("float");              break;
    case FF_OPT_TYPE_RATIONAL: printf("rational number");    break;
    case FF_OPT_TYPE_FLAGS:    printf("flags");              break;
    default:                   printf("value");              break;
    }
    printf("} (@emph{");

    if (o->flags & AV_OPT_FLAG_ENCODING_PARAM) {
        printf("input");
        if (o->flags & AV_OPT_FLAG_ENCODING_PARAM)
            printf("/");
    }
    if (o->flags & AV_OPT_FLAG_ENCODING_PARAM)
        printf("output");

    printf("})\n");
    if (o->help)
        printf("%s\n", o->help);

    if (o->unit) {
        const AVOption *u = NULL;
        printf("\nPossible values:\n@table @samp\n");

        while ((u = av_next_option(&class, u)))
            if (u->type == FF_OPT_TYPE_CONST && u->unit && !strcmp(u->unit, o->unit))
                printf("@item %s\n%s\n", u->name, u->help ? u->help : "");
        printf("@end table\n");
    }
}

static void show_opts(const AVClass *class)
{
    const AVOption *o = NULL;

    printf("@table @option\n");
    while ((o = av_next_option(&class, o)))
        if (o->type != FF_OPT_TYPE_CONST)
            print_option(class, o);
    printf("@end table\n");
}

static void show_format_opts(void)
{
    AVInputFormat *iformat = NULL;
    AVOutputFormat *oformat = NULL;

    printf("@section Generic format AVOptions\n");
    show_opts(avformat_get_class());

    printf("@section Format-specific AVOptions\n");
    while ((iformat = av_iformat_next(iformat))) {
        if (!iformat->priv_class)
            continue;
        printf("@subsection %s AVOptions\n", iformat->priv_class->class_name);
        show_opts(iformat->priv_class);
    }
    while ((oformat = av_oformat_next(oformat))) {
        if (!oformat->priv_class)
            continue;
        printf("@subsection %s AVOptions\n", oformat->priv_class->class_name);
        show_opts(oformat->priv_class);
    }
}

static void show_codec_opts(void)
{
    AVCodec *c = NULL;

    printf("@section Generic codec AVOptions\n");
    show_opts(avcodec_get_class());

    printf("@section Codec-specific AVOptions\n");
    while ((c = av_codec_next(c))) {
        if (!c->priv_class)
            continue;
        printf("@subsection %s AVOptions\n", c->priv_class->class_name);
        show_opts(c->priv_class);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
        print_usage();

    av_register_all();

    if (!strcmp(argv[1], "format"))
        show_format_opts();
    else if (!strcmp(argv[1], "codec"))
        show_codec_opts();
    else
        print_usage();

    return 0;
}
