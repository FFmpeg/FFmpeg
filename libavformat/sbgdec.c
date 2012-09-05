/*
 * SBG (SBaGen) file format decoder
 * Copyright (c) 2011 Nicolas George
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
#include <stdlib.h>
#include <time.h>
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "internal.h"

#define SBG_SCALE (1 << 16)
#define DAY (24 * 60 * 60)
#define DAY_TS ((int64_t)DAY * AV_TIME_BASE)

struct sbg_demuxer {
    AVClass *class;
    int sample_rate;
    int frame_size;
    int max_file_size;
};

struct sbg_string {
    char *s;
    char *e;
};

enum sbg_fade_type {
    SBG_FADE_SILENCE = 0,
    SBG_FADE_SAME    = 1,
    SBG_FADE_ADAPT   = 3,
};

struct sbg_fade {
    int8_t in, out, slide;
};

enum sbg_synth_type {
    SBG_TYPE_NONE,
    SBG_TYPE_SINE,
    SBG_TYPE_NOISE,
    SBG_TYPE_BELL,
    SBG_TYPE_MIX,
    SBG_TYPE_SPIN,
};

/* bell: freq constant, ampl decreases exponentially, can be approx lin */

struct sbg_timestamp {
    int64_t t;
    char type; /* 0 for relative, 'N' for now, 'T' for absolute */
};

struct sbg_script_definition {
    char *name;
    int name_len;
    int elements, nb_elements;
    char type; /* 'S' or 'B' */
};

struct sbg_script_synth {
    int carrier;
    int beat;
    int vol;
    enum sbg_synth_type type;
    struct {
        int l, r;
    } ref;
};

struct sbg_script_tseq {
    struct sbg_timestamp ts;
    char *name;
    int name_len;
    int lock;
    struct sbg_fade fade;
};

struct sbg_script_event {
    int64_t ts;
    int64_t ts_int, ts_trans, ts_next;
    int elements, nb_elements;
    struct sbg_fade fade;
};

struct sbg_script {
    struct sbg_script_definition *def;
    struct sbg_script_synth *synth;
    struct sbg_script_tseq *tseq;
    struct sbg_script_tseq *block_tseq;
    struct sbg_script_event *events;
    int nb_def;
    int nb_tseq;
    int nb_events;
    int nb_synth;
    int64_t start_ts;
    int64_t end_ts;
    int64_t opt_fade_time;
    int64_t opt_duration;
    char *opt_mix;
    int sample_rate;
    uint8_t opt_start_at_first;
    uint8_t opt_end_at_last;
};

struct sbg_parser {
    void *log;
    char *script, *end;
    char *cursor;
    struct sbg_script scs;
    struct sbg_timestamp current_time;
    int nb_block_tseq;
    int nb_def_max, nb_synth_max, nb_tseq_max, nb_block_tseq_max;
    int line_no;
    char err_msg[128];
};

enum ws_interval_type {
    WS_SINE  = MKTAG('S','I','N','E'),
    WS_NOISE = MKTAG('N','O','I','S'),
};

struct ws_interval {
    int64_t ts1, ts2;
    enum ws_interval_type type;
    uint32_t channels;
    int32_t f1, f2;
    int32_t a1, a2;
    uint32_t phi;
};

struct ws_intervals {
    struct ws_interval *inter;
    int nb_inter;
    int max_inter;
};

static void *alloc_array_elem(void **array, size_t elsize,
                              int *size, int *max_size)
{
    void *ret;

    if (*size == *max_size) {
        int m = FFMAX(32, FFMIN(*max_size, INT_MAX / 2) * 2);
        if (*size >= m)
            return NULL;
        *array = av_realloc_f(*array, m, elsize);
        if (!*array)
            return NULL;
        *max_size = m;
    }
    ret = (char *)*array + elsize * *size;
    memset(ret, 0, elsize);
    (*size)++;
    return ret;
}

static int str_to_time(const char *str, int64_t *rtime)
{
    const char *cur = str;
    char *end;
    int hours, minutes;
    double seconds = 0;

    if (*cur < '0' || *cur > '9')
        return 0;
    hours = strtol(cur, &end, 10);
    if (end == cur || *end != ':' || end[1] < '0' || end[1] > '9')
        return 0;
    cur = end + 1;
    minutes = strtol(cur, &end, 10);
    if (end == cur)
        return 0;
    cur = end;
    if (*end == ':'){
        seconds = strtod(cur + 1, &end);
        if (end > cur + 1)
            cur = end;
    }
    *rtime = (hours * 3600 + minutes * 60 + seconds) * AV_TIME_BASE;
    return cur - str;
}

static inline int is_space(char c)
{
    return c == ' '  || c == '\t' || c == '\r';
}

static inline int scale_double(void *log, double d, double m, int *r)
{
    m *= d * SBG_SCALE;
    if (m < INT_MIN || m >= INT_MAX) {
        if (log)
            av_log(log, AV_LOG_ERROR, "%g is too large\n", d);
        return AVERROR(EDOM);
    }
    *r = m;
    return 0;
}

static int lex_space(struct sbg_parser *p)
{
    char *c = p->cursor;

    while (p->cursor < p->end && is_space(*p->cursor))
        p->cursor++;
    return p->cursor > c;
}

static int lex_char(struct sbg_parser *p, char c)
{
    int r = p->cursor < p->end && *p->cursor == c;

    p->cursor += r;
    return r;
}

static int lex_double(struct sbg_parser *p, double *r)
{
    double d;
    char *end;

    if (p->cursor == p->end || is_space(*p->cursor) || *p->cursor == '\n')
        return 0;
    d = strtod(p->cursor, &end);
    if (end > p->cursor) {
        *r = d;
        p->cursor = end;
        return 1;
    }
    return 0;
}

static int lex_fixed(struct sbg_parser *p, const char *t, int l)
{
    if (p->end - p->cursor < l || memcmp(p->cursor, t, l))
        return 0;
    p->cursor += l;
    return 1;
}

static int lex_line_end(struct sbg_parser *p)
{
    if (p->cursor < p->end && *p->cursor == '#') {
        p->cursor++;
        while (p->cursor < p->end && *p->cursor != '\n')
            p->cursor++;
    }
    if (p->cursor == p->end)
        /* simulate final LF for files lacking it */
        return 1;
    if (*p->cursor != '\n')
        return 0;
    p->cursor++;
    p->line_no++;
    lex_space(p);
    return 1;
}

static int lex_wsword(struct sbg_parser *p, struct sbg_string *rs)
{
    char *s = p->cursor, *c = s;

    if (s == p->end || *s == '\n')
        return 0;
    while (c < p->end && *c != '\n' && !is_space(*c))
        c++;
    rs->s = s;
    rs->e = p->cursor = c;
    lex_space(p);
    return 1;
}

static int lex_name(struct sbg_parser *p, struct sbg_string *rs)
{
    char *s = p->cursor, *c = s;

    while (c < p->end && ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')
           || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-'))
        c++;
    if (c == s)
        return 0;
    rs->s = s;
    rs->e = p->cursor = c;
    return 1;
}

static int lex_time(struct sbg_parser *p, int64_t *rt)
{
    int r = str_to_time(p->cursor, rt);
    p->cursor += r;
    return r > 0;
}

#define FORWARD_ERROR(c) \
    do { \
        int errcode = c; \
        if (errcode <= 0) \
            return errcode ? errcode : AVERROR_INVALIDDATA; \
    } while(0);

static int parse_immediate(struct sbg_parser *p)
{
    snprintf(p->err_msg, sizeof(p->err_msg),
             "immediate sequences not yet implemented");
    return AVERROR_PATCHWELCOME;
}

static int parse_preprogrammed(struct sbg_parser *p)
{
    snprintf(p->err_msg, sizeof(p->err_msg),
             "preprogrammed sequences not yet implemented");
    return AVERROR_PATCHWELCOME;
}

static int parse_optarg(struct sbg_parser *p, char o, struct sbg_string *r)
{
    if (!lex_wsword(p, r)) {
        snprintf(p->err_msg, sizeof(p->err_msg),
                 "option '%c' requires an argument", o);
        return AVERROR_INVALIDDATA;
    }
    return 1;
}

static int parse_options(struct sbg_parser *p)
{
    struct sbg_string ostr, oarg;
    char mode = 0;
    int r;
    char *tptr;
    double v;

    if (p->cursor == p->end || *p->cursor != '-')
        return 0;
    while (lex_char(p, '-') && lex_wsword(p, &ostr)) {
        for (; ostr.s < ostr.e; ostr.s++) {
            char opt = *ostr.s;
            switch (opt) {
                case 'S':
                    p->scs.opt_start_at_first = 1;
                    break;
                case 'E':
                    p->scs.opt_end_at_last = 1;
                    break;
                case 'i':
                    mode = 'i';
                    break;
                case 'p':
                    mode = 'p';
                    break;
                case 'F':
                    FORWARD_ERROR(parse_optarg(p, opt, &oarg));
                    v = strtod(oarg.s, &tptr);
                    if (oarg.e != tptr) {
                        snprintf(p->err_msg, sizeof(p->err_msg),
                                 "syntax error for option -F");
                        return AVERROR_INVALIDDATA;
                    }
                    p->scs.opt_fade_time = v * AV_TIME_BASE / 1000;
                    break;
                case 'L':
                    FORWARD_ERROR(parse_optarg(p, opt, &oarg));
                    r = str_to_time(oarg.s, &p->scs.opt_duration);
                    if (oarg.e != oarg.s + r) {
                        snprintf(p->err_msg, sizeof(p->err_msg),
                                 "syntax error for option -L");
                        return AVERROR_INVALIDDATA;
                    }
                    break;
                case 'T':
                    FORWARD_ERROR(parse_optarg(p, opt, &oarg));
                    r = str_to_time(oarg.s, &p->scs.start_ts);
                    if (oarg.e != oarg.s + r) {
                        snprintf(p->err_msg, sizeof(p->err_msg),
                                 "syntax error for option -T");
                        return AVERROR_INVALIDDATA;
                    }
                    break;
                case 'm':
                    FORWARD_ERROR(parse_optarg(p, opt, &oarg));
                    tptr = av_malloc(oarg.e - oarg.s + 1);
                    if (!tptr)
                        return AVERROR(ENOMEM);
                    memcpy(tptr, oarg.s, oarg.e - oarg.s);
                    tptr[oarg.e - oarg.s] = 0;
                    av_free(p->scs.opt_mix);
                    p->scs.opt_mix = tptr;
                    break;
                case 'q':
                    FORWARD_ERROR(parse_optarg(p, opt, &oarg));
                    v = strtod(oarg.s, &tptr);
                    if (oarg.e != tptr) {
                        snprintf(p->err_msg, sizeof(p->err_msg),
                                 "syntax error for option -q");
                        return AVERROR_INVALIDDATA;
                    }
                    if (v != 1) {
                        snprintf(p->err_msg, sizeof(p->err_msg),
                                 "speed factor other than 1 not supported");
                        return AVERROR_PATCHWELCOME;
                    }
                    break;
                case 'r':
                    FORWARD_ERROR(parse_optarg(p, opt, &oarg));
                    r = strtol(oarg.s, &tptr, 10);
                    if (oarg.e != tptr) {
                        snprintf(p->err_msg, sizeof(p->err_msg),
                                 "syntax error for option -r");
                        return AVERROR_INVALIDDATA;
                    }
                    if (r < 40) {
                        snprintf(p->err_msg, sizeof(p->err_msg),
                                 "invalid sample rate");
                        return AVERROR_PATCHWELCOME;
                    }
                    p->scs.sample_rate = r;
                    break;
                default:
                    snprintf(p->err_msg, sizeof(p->err_msg),
                             "unknown option: '%c'", *ostr.s);
                    return AVERROR_INVALIDDATA;
            }
        }
    }
    switch (mode) {
        case 'i':
            return parse_immediate(p);
        case 'p':
            return parse_preprogrammed(p);
        case 0:
            if (!lex_line_end(p))
                return AVERROR_INVALIDDATA;
            return 1;
    }
    return AVERROR_BUG;
}

static int parse_timestamp(struct sbg_parser *p,
                               struct sbg_timestamp *rts, int64_t *rrel)
{
    int64_t abs = 0, rel = 0, dt;
    char type = 0;
    int r;

    if (lex_fixed(p, "NOW", 3)) {
        type = 'N';
        r = 1;
    } else {
        r = lex_time(p, &abs);
        if (r)
            type = 'T';
    }
    while (lex_char(p, '+')) {
        if (!lex_time(p, &dt))
            return AVERROR_INVALIDDATA;
        rel += dt;
        r = 1;
    }
    if (r) {
        if (!lex_space(p))
            return AVERROR_INVALIDDATA;
        rts->type = type;
        rts->t    = abs;
        *rrel     = rel;
    }
    return r;
}

static int parse_fade(struct sbg_parser *p, struct sbg_fade *fr)
{
    struct sbg_fade f;

    if (lex_char(p, '<'))
        f.in = SBG_FADE_SILENCE;
    else if (lex_char(p, '-'))
        f.in = SBG_FADE_SAME;
    else if (lex_char(p, '='))
        f.in = SBG_FADE_ADAPT;
    else
        return 0;
    if (lex_char(p, '>'))
        f.out = SBG_FADE_SILENCE;
    else if (lex_char(p, '-'))
        f.out = SBG_FADE_SAME;
    else if (lex_char(p, '='))
        f.out = SBG_FADE_ADAPT;
    else
        return AVERROR_INVALIDDATA;
    *fr = f;
    return 1;
}

static int parse_time_sequence(struct sbg_parser *p, int inblock)
{
    struct sbg_timestamp ts;
    int64_t rel_ts;
    int r;
    struct sbg_fade fade = { SBG_FADE_SAME, SBG_FADE_SAME, 0 };
    struct sbg_string name;
    struct sbg_script_tseq *tseq;

    r = parse_timestamp(p, &ts, &rel_ts);
    if (!r)
        return 0;
    if (r < 0)
        return r;
    if (ts.type) {
        if (inblock)
            return AVERROR_INVALIDDATA;
        p->current_time.type = ts.type;
        p->current_time.t    = ts.t;
    } else if(!inblock && !p->current_time.type) {
        snprintf(p->err_msg, sizeof(p->err_msg),
                 "relative time without previous absolute time");
        return AVERROR_INVALIDDATA;
    }
    ts.type = p->current_time.type;
    ts.t    = p->current_time.t + rel_ts;
    r = parse_fade(p, &fade);
    if (r < 0)
        return r;
    lex_space(p);
    if (!lex_name(p, &name))
        return AVERROR_INVALIDDATA;
    lex_space(p);
    if (lex_fixed(p, "->", 2)) {
        fade.slide = SBG_FADE_ADAPT;
        lex_space(p);
    }
    if (!lex_line_end(p))
        return AVERROR_INVALIDDATA;
    tseq = inblock ?
           alloc_array_elem((void **)&p->scs.block_tseq, sizeof(*tseq),
                            &p->nb_block_tseq, &p->nb_block_tseq_max) :
           alloc_array_elem((void **)&p->scs.tseq, sizeof(*tseq),
                            &p->scs.nb_tseq, &p->nb_tseq_max);
    if (!tseq)
        return AVERROR(ENOMEM);
    tseq->ts       = ts;
    tseq->name     = name.s;
    tseq->name_len = name.e - name.s;
    tseq->fade     = fade;
    return 1;
}

static int parse_wave_def(struct sbg_parser *p, int wavenum)
{
    snprintf(p->err_msg, sizeof(p->err_msg),
             "waveform definitions not yet implemented");
    return AVERROR_PATCHWELCOME;
}

static int parse_block_def(struct sbg_parser *p,
                           struct sbg_script_definition *def)
{
    int r, tseq;

    lex_space(p);
    if (!lex_line_end(p))
        return AVERROR_INVALIDDATA;
    tseq = p->nb_block_tseq;
    while (1) {
        r = parse_time_sequence(p, 1);
        if (r < 0)
            return r;
        if (!r)
            break;
    }
    if (!lex_char(p, '}'))
        return AVERROR_INVALIDDATA;
    lex_space(p);
    if (!lex_line_end(p))
        return AVERROR_INVALIDDATA;
    def->type        = 'B';
    def->elements    = tseq;
    def->nb_elements = p->nb_block_tseq - tseq;
    if (!def->nb_elements)
        return AVERROR_INVALIDDATA;
    return 1;
}

static int parse_volume(struct sbg_parser *p, int *vol)
{
    double v;

    if (!lex_char(p, '/'))
        return 0;
    if (!lex_double(p, &v))
        return AVERROR_INVALIDDATA;
    if (scale_double(p->log, v, 0.01, vol))
        return AVERROR(ERANGE);
    return 1;
}

static int parse_synth_channel_sine(struct sbg_parser *p,
                                    struct sbg_script_synth *synth)
{
    double carrierf, beatf;
    int carrier, beat, vol;

    if (!lex_double(p, &carrierf))
        return 0;
    if (!lex_double(p, &beatf))
        beatf = 0;
    FORWARD_ERROR(parse_volume(p, &vol));
    if (scale_double(p->log, carrierf, 1, &carrier) < 0 ||
        scale_double(p->log, beatf, 1, &beat) < 0)
        return AVERROR(EDOM);
    synth->type    = SBG_TYPE_SINE;
    synth->carrier = carrier;
    synth->beat    = beat;
    synth->vol     = vol;
    return 1;
}

static int parse_synth_channel_pink(struct sbg_parser *p,
                                    struct sbg_script_synth *synth)
{
    int vol;

    if (!lex_fixed(p, "pink", 4))
        return 0;
    FORWARD_ERROR(parse_volume(p, &vol));
    synth->type    = SBG_TYPE_NOISE;
    synth->vol     = vol;
    return 1;
}

static int parse_synth_channel_bell(struct sbg_parser *p,
                                    struct sbg_script_synth *synth)
{
    double carrierf;
    int carrier, vol;

    if (!lex_fixed(p, "bell", 4))
        return 0;
    if (!lex_double(p, &carrierf))
        return AVERROR_INVALIDDATA;
    FORWARD_ERROR(parse_volume(p, &vol));
    if (scale_double(p->log, carrierf, 1, &carrier) < 0)
        return AVERROR(EDOM);
    synth->type    = SBG_TYPE_BELL;
    synth->carrier = carrier;
    synth->vol     = vol;
    return 1;
}

static int parse_synth_channel_mix(struct sbg_parser *p,
                                   struct sbg_script_synth *synth)
{
    int vol;

    if (!lex_fixed(p, "mix", 3))
        return 0;
    FORWARD_ERROR(parse_volume(p, &vol));
    synth->type    = SBG_TYPE_MIX;
    synth->vol     = vol;
    return 1;
}

static int parse_synth_channel_spin(struct sbg_parser *p,
                                    struct sbg_script_synth *synth)
{
    double carrierf, beatf;
    int carrier, beat, vol;

    if (!lex_fixed(p, "spin:", 5))
        return 0;
    if (!lex_double(p, &carrierf))
        return AVERROR_INVALIDDATA;
    if (!lex_double(p, &beatf))
        return AVERROR_INVALIDDATA;
    FORWARD_ERROR(parse_volume(p, &vol));
    if (scale_double(p->log, carrierf, 1, &carrier) < 0 ||
        scale_double(p->log, beatf, 1, &beat) < 0)
        return AVERROR(EDOM);
    synth->type    = SBG_TYPE_SPIN;
    synth->carrier = carrier;
    synth->beat    = beat;
    synth->vol     = vol;
    return 1;
}

static int parse_synth_channel(struct sbg_parser *p)
{
    int r;
    struct sbg_script_synth *synth;

    synth = alloc_array_elem((void **)&p->scs.synth, sizeof(*synth),
                             &p->scs.nb_synth, &p->nb_synth_max);
    if (!synth)
        return AVERROR(ENOMEM);
    r = lex_char(p, '-');
    if (!r)
        r = parse_synth_channel_pink(p, synth);
    if (!r)
        r = parse_synth_channel_bell(p, synth);
    if (!r)
        r = parse_synth_channel_mix(p, synth);
    if (!r)
        r = parse_synth_channel_spin(p, synth);
    /* Unimplemented: wave%d:%f%f/vol (carrier, beat) */
    if (!r)
        r = parse_synth_channel_sine(p, synth);
    if (r <= 0)
        p->scs.nb_synth--;
    return r;
}

static int parse_synth_def(struct sbg_parser *p,
                           struct sbg_script_definition *def)
{
    int r, synth;

    synth = p->scs.nb_synth;
    while (1) {
        r = parse_synth_channel(p);
        if (r < 0)
            return r;
        if (!r || !lex_space(p))
            break;
    }
    lex_space(p);
    if (synth == p->scs.nb_synth)
        return AVERROR_INVALIDDATA;
    if (!lex_line_end(p))
        return AVERROR_INVALIDDATA;
    def->type        = 'S';
    def->elements    = synth;
    def->nb_elements = p->scs.nb_synth - synth;
    return 1;
}

static int parse_named_def(struct sbg_parser *p)
{
    char *cursor_save = p->cursor;
    struct sbg_string name;
    struct sbg_script_definition *def;

    if (!lex_name(p, &name) || !lex_char(p, ':') || !lex_space(p)) {
        p->cursor = cursor_save;
        return 0;
    }
    if (name.e - name.s == 6 && !memcmp(name.s, "wave", 4) &&
        name.s[4] >= '0' && name.s[4] <= '9' &&
        name.s[5] >= '0' && name.s[5] <= '9') {
        int wavenum = (name.s[4] - '0') * 10 + (name.s[5] - '0');
        return parse_wave_def(p, wavenum);
    }
    def = alloc_array_elem((void **)&p->scs.def, sizeof(*def),
                           &p->scs.nb_def, &p->nb_def_max);
    if (!def)
        return AVERROR(ENOMEM);
    def->name     = name.s;
    def->name_len = name.e - name.s;
    if (lex_char(p, '{'))
        return parse_block_def(p, def);
    return parse_synth_def(p, def);
}

static void free_script(struct sbg_script *s)
{
    av_freep(&s->def);
    av_freep(&s->synth);
    av_freep(&s->tseq);
    av_freep(&s->block_tseq);
    av_freep(&s->events);
    av_freep(&s->opt_mix);
}

static int parse_script(void *log, char *script, int script_len,
                            struct sbg_script *rscript)
{
    struct sbg_parser sp = {
        .log     = log,
        .script  = script,
        .end     = script + script_len,
        .cursor  = script,
        .line_no = 1,
        .err_msg = "",
        .scs = {
            /* default values */
            .start_ts      = AV_NOPTS_VALUE,
            .sample_rate   = 44100,
            .opt_fade_time = 60 * AV_TIME_BASE,
        },
    };
    int r;

    lex_space(&sp);
    while (sp.cursor < sp.end) {
        r = parse_options(&sp);
        if (r < 0)
            goto fail;
        if (!r && !lex_line_end(&sp))
            break;
    }
    while (sp.cursor < sp.end) {
        r = parse_named_def(&sp);
        if (!r)
            r = parse_time_sequence(&sp, 0);
        if (!r)
            r = lex_line_end(&sp) ? 1 : AVERROR_INVALIDDATA;
        if (r < 0)
            goto fail;
    }
    *rscript = sp.scs;
    return 1;
fail:
    free_script(&sp.scs);
    if (!*sp.err_msg)
        if (r == AVERROR_INVALIDDATA)
            snprintf(sp.err_msg, sizeof(sp.err_msg), "syntax error");
    if (log && *sp.err_msg) {
        const char *ctx = sp.cursor;
        const char *ectx = av_x_if_null(memchr(ctx, '\n', sp.end - sp.cursor),
                                        sp.end);
        int lctx = ectx - ctx;
        const char *quote = "\"";
        if (lctx > 0 && ctx[lctx - 1] == '\r')
            lctx--;
        if (lctx == 0) {
            ctx = "the end of line";
            lctx = strlen(ctx);
            quote = "";
        }
        av_log(log, AV_LOG_ERROR, "Error line %d: %s near %s%.*s%s.\n",
               sp.line_no, sp.err_msg, quote, lctx, ctx, quote);
    }
    return r;
}

static int read_whole_file(AVIOContext *io, int max_size, char **rbuf)
{
    char *buf = NULL;
    int size = 0, bufsize = 0, r;

    while (1) {
        if (bufsize - size < 1024) {
            bufsize = FFMIN(FFMAX(2 * bufsize, 8192), max_size);
            if (bufsize - size < 2) {
                size = AVERROR(EFBIG);
                goto fail;
            }
            buf = av_realloc_f(buf, bufsize, 1);
            if (!buf) {
                size = AVERROR(ENOMEM);
                goto fail;
            }
        }
        r = avio_read(io, buf, bufsize - size - 1);
        if (r == AVERROR_EOF)
            break;
        if (r < 0)
            goto fail;
        size += r;
    }
    buf[size] = 0;
    *rbuf = buf;
    return size;
fail:
    av_free(buf);
    return size;
}

static void expand_timestamps(void *log, struct sbg_script *s)
{
    int i, nb_rel = 0;
    int64_t now, cur_ts, delta = 0;

    for (i = 0; i < s->nb_tseq; i++)
        nb_rel += s->tseq[i].ts.type == 'N';
    if (nb_rel == s->nb_tseq) {
        /* All ts are relative to NOW: consider NOW = 0 */
        now = 0;
        if (s->start_ts != AV_NOPTS_VALUE)
            av_log(log, AV_LOG_WARNING,
                   "Start time ignored in a purely relative script.\n");
    } else if (nb_rel == 0 && s->start_ts != AV_NOPTS_VALUE ||
               s->opt_start_at_first) {
        /* All ts are absolute and start time is specified */
        if (s->start_ts == AV_NOPTS_VALUE)
            s->start_ts = s->tseq[0].ts.t;
        now = s->start_ts;
    } else {
        /* Mixed relative/absolute ts: expand */
        time_t now0;
        struct tm *tm;

        av_log(log, AV_LOG_WARNING,
               "Scripts with mixed absolute and relative timestamps can give "
               "unexpected results (pause, seeking, time zone change).\n");
#undef time
        time(&now0);
        tm = localtime(&now0);
        now = tm ? tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec :
                   now0 % DAY;
        av_log(log, AV_LOG_INFO, "Using %02d:%02d:%02d as NOW.\n",
               (int)(now / 3600), (int)(now / 60) % 60, (int)now % 60);
        now *= AV_TIME_BASE;
        for (i = 0; i < s->nb_tseq; i++) {
            if (s->tseq[i].ts.type == 'N') {
                s->tseq[i].ts.t += now;
                s->tseq[i].ts.type = 'T'; /* not necessary */
            }
        }
    }
    if (s->start_ts == AV_NOPTS_VALUE)
        s->start_ts = s->opt_start_at_first ? s->tseq[0].ts.t : now;
    s->end_ts = s->opt_duration ? s->start_ts + s->opt_duration :
                AV_NOPTS_VALUE; /* may be overridden later by -E option */
    cur_ts = now;
    for (i = 0; i < s->nb_tseq; i++) {
        if (s->tseq[i].ts.t + delta < cur_ts)
            delta += DAY_TS;
        cur_ts = s->tseq[i].ts.t += delta;
    }
}

static int expand_tseq(void *log, struct sbg_script *s, int *nb_ev_max,
                       int64_t t0, struct sbg_script_tseq *tseq)
{
    int i, r;
    struct sbg_script_definition *def;
    struct sbg_script_tseq *be;
    struct sbg_script_event *ev;

    if (tseq->lock++) {
        av_log(log, 16, "Recursion loop on \"%.*s\"\n",
               tseq->name_len, tseq->name);
        return AVERROR(EINVAL);
    }
    t0 += tseq->ts.t;
    for (i = 0; i < s->nb_def; i++) {
        if (s->def[i].name_len == tseq->name_len &&
            !memcmp(s->def[i].name, tseq->name, tseq->name_len))
            break;
    }
    if (i >= s->nb_def) {
        av_log(log, 16, "Tone-set \"%.*s\" not defined\n",
               tseq->name_len, tseq->name);
        return AVERROR(EINVAL);
    }
    def = &s->def[i];
    if (def->type == 'B') {
        be = s->block_tseq + def->elements;
        for (i = 0; i < def->nb_elements; i++) {
            r = expand_tseq(log, s, nb_ev_max, t0, &be[i]);
            if (r < 0)
                return r;
        }
    } else {
        ev = alloc_array_elem((void **)&s->events, sizeof(*ev),
                              &s->nb_events, nb_ev_max);
        ev->ts          = tseq->ts.t;
        ev->elements    = def->elements;
        ev->nb_elements = def->nb_elements;
        ev->fade        = tseq->fade;
    }
    tseq->lock--;
    return 0;
}

static int expand_script(void *log, struct sbg_script *s)
{
    int i, r, nb_events_max = 0;

    expand_timestamps(log, s);
    for (i = 0; i < s->nb_tseq; i++) {
        r = expand_tseq(log, s, &nb_events_max, 0, &s->tseq[i]);
        if (r < 0)
            return r;
    }
    if (!s->nb_events) {
        av_log(log, AV_LOG_ERROR, "No events in script\n");
        return AVERROR_INVALIDDATA;
    }
    if (s->opt_end_at_last)
        s->end_ts = s->events[s->nb_events - 1].ts;
    return 0;
}

static int add_interval(struct ws_intervals *inter,
                        enum ws_interval_type type, uint32_t channels, int ref,
                        int64_t ts1, int32_t f1, int32_t a1,
                        int64_t ts2, int32_t f2, int32_t a2)
{
    struct ws_interval *i, *ri;

    if (ref >= 0) {
        ri = &inter->inter[ref];
        /* ref and new intervals are constant, identical and adjacent */
        if (ri->type == type && ri->channels == channels &&
            ri->f1 == ri->f2 && ri->f2 == f1 && f1 == f2 &&
            ri->a1 == ri->a2 && ri->a2 == a1 && a1 == a2 &&
            ri->ts2 == ts1) {
            ri->ts2 = ts2;
            return ref;
        }
    }
    i = alloc_array_elem((void **)&inter->inter, sizeof(*i),
                         &inter->nb_inter, &inter->max_inter);
    if (!i)
        return AVERROR(ENOMEM);
    i->ts1      = ts1;
    i->ts2      = ts2;
    i->type     = type;
    i->channels = channels;
    i->f1       = f1;
    i->f2       = f2;
    i->a1       = a1;
    i->a2       = a2;
    i->phi      = ref >= 0 ? ref | 0x80000000 : 0;
    return i - inter->inter;
}

static int add_bell(struct ws_intervals *inter, struct sbg_script *s,
                    int64_t ts1, int64_t ts2, int32_t f, int32_t a)
{
    /* SBaGen uses an exponential decrease every 50ms.
       We approximate it with piecewise affine segments. */
    int32_t cpoints[][2] = {
        {  2, a },
        {  4, a - a / 4 },
        {  8, a / 2 },
        { 16, a / 4 },
        { 25, a / 10 },
        { 50, a / 80 },
        { 75, 0 },
    };
    int i, r;
    int64_t dt = s->sample_rate / 20, ts3 = ts1, ts4;
    for (i = 0; i < FF_ARRAY_ELEMS(cpoints); i++) {
        ts4 = FFMIN(ts2, ts1 + cpoints[i][0] * dt);
        r = add_interval(inter, WS_SINE, 3, -1,
                         ts3, f, a, ts4, f, cpoints[i][1]);
        if (r < 0)
            return r;
        ts3 = ts4;
        a = cpoints[i][1];
    }
    return 0;
}

static int generate_interval(void *log, struct sbg_script *s,
                             struct ws_intervals *inter,
                             int64_t ts1, int64_t ts2,
                             struct sbg_script_synth *s1,
                             struct sbg_script_synth *s2,
                             int transition)
{
    int r;

    if (ts2 <= ts1 || (s1->vol == 0 && s2->vol == 0))
        return 0;
    switch (s1->type) {
        case SBG_TYPE_NONE:
            break;
        case SBG_TYPE_SINE:
            if (s1->beat == 0 && s2->beat == 0) {
                r = add_interval(inter, WS_SINE, 3, s1->ref.l,
                                 ts1, s1->carrier, s1->vol,
                                 ts2, s2->carrier, s2->vol);
                if (r < 0)
                    return r;
                s2->ref.l = s2->ref.r = r;
            } else {
                r = add_interval(inter, WS_SINE, 1, s1->ref.l,
                                 ts1, s1->carrier + s1->beat / 2, s1->vol,
                                 ts2, s2->carrier + s2->beat / 2, s2->vol);
                if (r < 0)
                    return r;
                s2->ref.l = r;
                r = add_interval(inter, WS_SINE, 2, s1->ref.r,
                                 ts1, s1->carrier - s1->beat / 2, s1->vol,
                                 ts2, s2->carrier - s2->beat / 2, s2->vol);
                if (r < 0)
                    return r;
                s2->ref.r = r;
            }
            break;

        case SBG_TYPE_BELL:
            if (transition == 2) {
                r = add_bell(inter, s, ts1, ts2, s1->carrier, s2->vol);
                if (r < 0)
                    return r;
            }
            break;

        case SBG_TYPE_SPIN:
            av_log(log, AV_LOG_WARNING, "Spinning noise not implemented, "
                                        "using pink noise instead.\n");
            /* fall through */
        case SBG_TYPE_NOISE:
            /* SBaGen's pink noise generator uses:
               - 1 band of white noise, mean square: 1/3;
               - 9 bands of subsampled white noise with linear
                 interpolation, mean square: 2/3 each;
               with 1/10 weight each: the total mean square is 7/300.
               Our pink noise generator uses 8 bands of white noise with
               rectangular subsampling: the total mean square is 1/24.
               Therefore, to match SBaGen's volume, we must multiply vol by
               sqrt((7/300) / (1/24)) = sqrt(14/25) =~ 0.748
             */
            r = add_interval(inter, WS_NOISE, 3, s1->ref.l,
                             ts1, 0, s1->vol - s1->vol / 4,
                             ts2, 0, s2->vol - s2->vol / 4);
            if (r < 0)
                return r;
            s2->ref.l = s2->ref.r = r;
            break;

        case SBG_TYPE_MIX:
            /* Unimplemented: silence; warning present elsewhere */
        default:
            av_log(log, AV_LOG_ERROR,
                   "Type %d is not implemented\n", s1->type);
            return AVERROR_PATCHWELCOME;
    }
    return 0;
}

static int generate_plateau(void *log, struct sbg_script *s,
                            struct ws_intervals *inter,
                            struct sbg_script_event *ev1)
{
    int64_t ts1 = ev1->ts_int, ts2 = ev1->ts_trans;
    int i, r;
    struct sbg_script_synth *s1;

    for (i = 0; i < ev1->nb_elements; i++) {
        s1 = &s->synth[ev1->elements + i];
        r = generate_interval(log, s, inter, ts1, ts2, s1, s1, 0);
        if (r < 0)
            return r;
    }
    return 0;
}

/*

   ts1             ts2         ts1    tsmid    ts2
    |               |           |       |       |
    v               v           v       |       v
____                        ____        v       ____
    ''''....                    ''..        ..''
            ''''....____            ''....''

  compatible transition      incompatible transition
 */

static int generate_transition(void *log, struct sbg_script *s,
                               struct ws_intervals *inter,
                               struct sbg_script_event *ev1,
                               struct sbg_script_event *ev2)
{
    int64_t ts1 = ev1->ts_trans, ts2 = ev1->ts_next;
    /* (ts1 + ts2) / 2 without overflow */
    int64_t tsmid = (ts1 >> 1) + (ts2 >> 1) + (ts1 & ts2 & 1);
    enum sbg_fade_type type = ev1->fade.slide | (ev1->fade.out & ev2->fade.in);
    int nb_elements = FFMAX(ev1->nb_elements, ev2->nb_elements);
    struct sbg_script_synth *s1, *s2, s1mod, s2mod, smid;
    int pass, i, r;

    for (pass = 0; pass < 2; pass++) {
        /* pass = 0 -> compatible and first half of incompatible
           pass = 1 -> second half of incompatible
           Using two passes like that ensures that the intervals are generated
           in increasing order according to their start timestamp.
           Otherwise it would be necessary to sort them
           while keeping the mutual references.
         */
        for (i = 0; i < nb_elements; i++) {
            s1 = i < ev1->nb_elements ? &s->synth[ev1->elements + i] : &s1mod;
            s2 = i < ev2->nb_elements ? &s->synth[ev2->elements + i] : &s2mod;
            s1mod = s1 != &s1mod ? *s1 : (struct sbg_script_synth){ 0 };
            s2mod = s2 != &s2mod ? *s2 : (struct sbg_script_synth){ 0 };
            if (ev1->fade.slide) {
                /* for slides, and only for slides, silence ("-") is equivalent
                   to anything with volume 0 */
                if (s1mod.type == SBG_TYPE_NONE) {
                    s1mod = s2mod;
                    s1mod.vol = 0;
                } else if (s2mod.type == SBG_TYPE_NONE) {
                    s2mod = s1mod;
                    s2mod.vol = 0;
                }
            }
            if (s1mod.type == s2mod.type &&
                s1mod.type != SBG_TYPE_BELL &&
                (type == SBG_FADE_ADAPT ||
                 (s1mod.carrier == s2mod.carrier &&
                  s1mod.beat == s2mod.beat))) {
                /* compatible: single transition */
                if (!pass) {
                    r = generate_interval(log, s, inter,
                                          ts1, ts2, &s1mod, &s2mod, 3);
                    if (r < 0)
                        return r;
                    s2->ref = s2mod.ref;
                }
            } else {
                /* incompatible: silence at midpoint */
                if (!pass) {
                    smid = s1mod;
                    smid.vol = 0;
                    r = generate_interval(log, s, inter,
                                          ts1, tsmid, &s1mod, &smid, 1);
                    if (r < 0)
                        return r;
                } else {
                    smid = s2mod;
                    smid.vol = 0;
                    r = generate_interval(log, s, inter,
                                          tsmid, ts2, &smid, &s2mod, 2);
                    if (r < 0)
                        return r;
                    s2->ref = s2mod.ref;
                }
            }
        }
    }
    return 0;
}

/*
    ev1                  trats ev2  intts           endts ev3
     |                     |    |     |               |    |
     v                     v    v     v               v    v
                                      ________________
....                              ....                ....
    '''....________________....'''                        '''...._______________

\_________/\______________/\_________/\______________/\_________/\_____________/
  tr x->1        int1        tr 1->2        int2        tr 2->3        int3
 */

static int generate_intervals(void *log, struct sbg_script *s, int sample_rate,
                              struct ws_intervals *inter)
{
    int64_t trans_time = s->opt_fade_time / 2;
    struct sbg_script_event ev0, *ev1, *ev2;
    int64_t period;
    int i, r;

    /* SBaGen handles the time before and after the extremal events,
       and the corresponding transitions, as if the sequence were cyclic
       with a 24-hours period. */
    period = s->events[s->nb_events - 1].ts - s->events[0].ts;
    period = (period + (DAY_TS - 1)) / DAY_TS * DAY_TS;
    period = FFMAX(period, DAY_TS);

    /* Prepare timestamps for transitions */
    for (i = 0; i < s->nb_events; i++) {
        ev1 = &s->events[i];
        ev2 = &s->events[(i + 1) % s->nb_events];
        ev1->ts_int   = ev1->ts;
        ev1->ts_trans = ev1->fade.slide ? ev1->ts
                                        : ev2->ts + (ev1 < ev2 ? 0 : period);
    }
    for (i = 0; i < s->nb_events; i++) {
        ev1 = &s->events[i];
        ev2 = &s->events[(i + 1) % s->nb_events];
        if (!ev1->fade.slide) {
            ev1->ts_trans = FFMAX(ev1->ts_int,   ev1->ts_trans - trans_time);
            ev2->ts_int   = FFMIN(ev2->ts_trans, ev2->ts_int   + trans_time);
        }
        ev1->ts_next  = ev2->ts_int + (ev1 < ev2 ? 0 : period);
    }

    /* Pseudo event before the first one */
    ev0 = s->events[s->nb_events - 1];
    ev0.ts_int   -= period;
    ev0.ts_trans -= period;
    ev0.ts_next  -= period;

    /* Convert timestamps */
    for (i = -1; i < s->nb_events; i++) {
        ev1 = i < 0 ? &ev0 : &s->events[i];
        ev1->ts_int   = av_rescale(ev1->ts_int,   sample_rate, AV_TIME_BASE);
        ev1->ts_trans = av_rescale(ev1->ts_trans, sample_rate, AV_TIME_BASE);
        ev1->ts_next  = av_rescale(ev1->ts_next,  sample_rate, AV_TIME_BASE);
    }

    /* Generate intervals */
    for (i = 0; i < s->nb_synth; i++)
        s->synth[i].ref.l = s->synth[i].ref.r = -1;
    for (i = -1; i < s->nb_events; i++) {
        ev1 = i < 0 ? &ev0 : &s->events[i];
        ev2 = &s->events[(i + 1) % s->nb_events];
        r = generate_plateau(log, s, inter, ev1);
        if (r < 0)
            return r;
        r = generate_transition(log, s, inter, ev1, ev2);
        if (r < 0)
            return r;
    }
    if (!inter->nb_inter)
        av_log(log, AV_LOG_WARNING, "Completely silent script.\n");
    return 0;
}

static int encode_intervals(struct sbg_script *s, AVCodecContext *avc,
                            struct ws_intervals *inter)
{
    int i, edata_size = 4;
    uint8_t *edata;

    for (i = 0; i < inter->nb_inter; i++) {
        edata_size += inter->inter[i].type == WS_SINE  ? 44 :
                      inter->inter[i].type == WS_NOISE ? 32 : 0;
        if (edata_size < 0)
            return AVERROR(ENOMEM);
    }
    edata = av_malloc(edata_size);
    if (!edata)
        return AVERROR(ENOMEM);
    avc->extradata = edata;
    avc->extradata_size = edata_size;

#define ADD_EDATA32(v) do { AV_WL32(edata, (v)); edata += 4; } while(0)
#define ADD_EDATA64(v) do { AV_WL64(edata, (v)); edata += 8; } while(0)
    ADD_EDATA32(inter->nb_inter);
    for (i = 0; i < inter->nb_inter; i++) {
        ADD_EDATA64(inter->inter[i].ts1);
        ADD_EDATA64(inter->inter[i].ts2);
        ADD_EDATA32(inter->inter[i].type);
        ADD_EDATA32(inter->inter[i].channels);
        switch (inter->inter[i].type) {
            case WS_SINE:
                ADD_EDATA32(inter->inter[i].f1);
                ADD_EDATA32(inter->inter[i].f2);
                ADD_EDATA32(inter->inter[i].a1);
                ADD_EDATA32(inter->inter[i].a2);
                ADD_EDATA32(inter->inter[i].phi);
                break;
            case WS_NOISE:
                ADD_EDATA32(inter->inter[i].a1);
                ADD_EDATA32(inter->inter[i].a2);
                break;
        }
    }
    if (edata != avc->extradata + edata_size)
        return AVERROR_BUG;
    return 0;
}

static av_cold int sbg_read_probe(AVProbeData *p)
{
    int r, score;
    struct sbg_script script = { 0 };

    r = parse_script(NULL, p->buf, p->buf_size, &script);
    score = r < 0 || !script.nb_def || !script.nb_tseq ? 0 :
            AVPROBE_SCORE_MAX / 3;
    free_script(&script);
    return score;
}

static av_cold int sbg_read_header(AVFormatContext *avf)
{
    struct sbg_demuxer *sbg = avf->priv_data;
    int r;
    char *buf = NULL;
    struct sbg_script script = { 0 };
    AVStream *st;
    struct ws_intervals inter = { 0 };

    r = read_whole_file(avf->pb, sbg->max_file_size, &buf);
    if (r < 0)
        goto fail;
    r = parse_script(avf, buf, r, &script);
    if (r < 0)
        goto fail;
    if (!sbg->sample_rate)
        sbg->sample_rate = script.sample_rate;
    else
        script.sample_rate = sbg->sample_rate;
    if (!sbg->frame_size)
        sbg->frame_size = FFMAX(1, sbg->sample_rate / 10);
    if (script.opt_mix)
        av_log(avf, AV_LOG_WARNING, "Mix feature not implemented: "
               "-m is ignored and mix channels will be silent.\n");
    r = expand_script(avf, &script);
    if (r < 0)
        goto fail;
    av_freep(&buf);
    r = generate_intervals(avf, &script, sbg->sample_rate, &inter);
    if (r < 0)
        goto fail;

    st = avformat_new_stream(avf, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type     = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id       = AV_CODEC_ID_FFWAVESYNTH;
    st->codec->channels       = 2;
    st->codec->channel_layout = AV_CH_LAYOUT_STEREO;
    st->codec->sample_rate    = sbg->sample_rate;
    st->codec->frame_size     = sbg->frame_size;
    avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);
    st->probe_packets = 0;
    st->start_time    = av_rescale(script.start_ts,
                                   sbg->sample_rate, AV_TIME_BASE);
    st->duration      = script.end_ts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE :
                        av_rescale(script.end_ts - script.start_ts,
                                   sbg->sample_rate, AV_TIME_BASE);
    st->cur_dts       = st->start_time;
    r = encode_intervals(&script, st->codec, &inter);
    if (r < 0)
        goto fail;

    av_free(inter.inter);
    free_script(&script);
    return 0;

fail:
    av_free(inter.inter);
    free_script(&script);
    av_free(buf);
    return r;
}

static int sbg_read_packet(AVFormatContext *avf, AVPacket *packet)
{
    int64_t ts, end_ts;

    ts = avf->streams[0]->cur_dts;
    end_ts = ts + avf->streams[0]->codec->frame_size;
    if (avf->streams[0]->duration != AV_NOPTS_VALUE)
        end_ts = FFMIN(avf->streams[0]->start_time + avf->streams[0]->duration,
                       end_ts);
    if (end_ts <= ts)
        return AVERROR_EOF;
    if (av_new_packet(packet, 12) < 0)
        return AVERROR(ENOMEM);
    packet->dts = packet->pts = ts;
    packet->duration = end_ts - ts;
    AV_WL64(packet->data + 0, ts);
    AV_WL32(packet->data + 8, packet->duration);
    return packet->size;
}

static int sbg_read_seek2(AVFormatContext *avf, int stream_index,
                          int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    if (flags || stream_index > 0)
        return AVERROR(EINVAL);
    if (stream_index < 0)
        ts = av_rescale_q(ts, AV_TIME_BASE_Q, avf->streams[0]->time_base);
    avf->streams[0]->cur_dts = ts;
    return 0;
}

static int sbg_read_seek(AVFormatContext *avf, int stream_index,
                         int64_t ts, int flags)
{
    return sbg_read_seek2(avf, stream_index, ts, ts, ts, 0);
}

static const AVOption sbg_options[] = {
    { "sample_rate", "", offsetof(struct sbg_demuxer, sample_rate),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX,
      AV_OPT_FLAG_DECODING_PARAM },
    { "frame_size", "", offsetof(struct sbg_demuxer, frame_size),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX,
      AV_OPT_FLAG_DECODING_PARAM },
    { "max_file_size", "", offsetof(struct sbg_demuxer, max_file_size),
      AV_OPT_TYPE_INT, { .i64 = 5000000 }, 0, INT_MAX,
      AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass sbg_demuxer_class = {
    .class_name = "sbg_demuxer",
    .item_name  = av_default_item_name,
    .option     = sbg_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_sbg_demuxer = {
    .name           = "sbg",
    .long_name      = NULL_IF_CONFIG_SMALL("SBaGen binaural beats script"),
    .priv_data_size = sizeof(struct sbg_demuxer),
    .read_probe     = sbg_read_probe,
    .read_header    = sbg_read_header,
    .read_packet    = sbg_read_packet,
    .read_seek      = sbg_read_seek,
    .read_seek2     = sbg_read_seek2,
    .extensions     = "sbg",
    .priv_class     = &sbg_demuxer_class,
};
