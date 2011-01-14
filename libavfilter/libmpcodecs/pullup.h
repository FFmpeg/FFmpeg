/*
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef MPLAYER_PULLUP_H
#define MPLAYER_PULLUP_H

#define PULLUP_CPU_MMX 1
#define PULLUP_CPU_MMX2 2
#define PULLUP_CPU_3DNOW 4
#define PULLUP_CPU_3DNOWEXT 8
#define PULLUP_CPU_SSE 16
#define PULLUP_CPU_SSE2 32

#define PULLUP_FMT_Y 1
#define PULLUP_FMT_YUY2 2
#define PULLUP_FMT_UYVY 3
#define PULLUP_FMT_RGB32 4

struct pullup_buffer
{
    int lock[2];
    unsigned char **planes;
};

struct pullup_field
{
    int parity;
    struct pullup_buffer *buffer;
    unsigned int flags;
    int breaks;
    int affinity;
    int *diffs;
    int *comb;
    int *var;
    struct pullup_field *prev, *next;
};

struct pullup_frame
{
    int lock;
    int length;
    int parity;
    struct pullup_buffer **ifields, *ofields[2];
    struct pullup_buffer *buffer;
};

struct pullup_context
{
    /* Public interface */
    int format;
    int nplanes;
    int *bpp, *w, *h, *stride, *background;
    unsigned int cpu;
    int junk_left, junk_right, junk_top, junk_bottom;
    int verbose;
    int metric_plane;
    int strict_breaks;
    int strict_pairs;
    /* Internal data */
    struct pullup_field *first, *last, *head;
    struct pullup_buffer *buffers;
    int nbuffers;
    int (*diff)(unsigned char *, unsigned char *, int);
    int (*comb)(unsigned char *, unsigned char *, int);
    int (*var)(unsigned char *, unsigned char *, int);
    int metric_w, metric_h, metric_len, metric_offset;
    struct pullup_frame *frame;
};


struct pullup_buffer *pullup_lock_buffer(struct pullup_buffer *b, int parity);
void pullup_release_buffer(struct pullup_buffer *b, int parity);
struct pullup_buffer *pullup_get_buffer(struct pullup_context *c, int parity);

void pullup_submit_field(struct pullup_context *c, struct pullup_buffer *b, int parity);
void pullup_flush_fields(struct pullup_context *c);

struct pullup_frame *pullup_get_frame(struct pullup_context *c);
void pullup_pack_frame(struct pullup_context *c, struct pullup_frame *fr);
void pullup_release_frame(struct pullup_frame *fr);

struct pullup_context *pullup_alloc_context(void);
void pullup_preinit_context(struct pullup_context *c);
void pullup_init_context(struct pullup_context *c);
void pullup_free_context(struct pullup_context *c);

#endif /* MPLAYER_PULLUP_H */
