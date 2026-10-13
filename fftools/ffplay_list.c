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

#include <limits.h>
#include <string.h>

#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/common.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/log.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/qsort.h"
#include "libavutil/time.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"

#include "ffplay_list.h"

#define LIST_FADE_TIME 250000
#define LIST_HOLD_TIME 500000
#define LIST_ROWS      7

enum {
    LIST_SORT_NUMBER,
    LIST_SORT_ARTIST,
    LIST_SORT_TITLE,
    LIST_SORT_LENGTH,
    LIST_SORT_SHUFFLE,
    LIST_SORT_NB
};

typedef struct ListSortKey {
    int key;
    int descending;
} ListSortKey;

/* what lies under a window position: the row and the sort button, -1 for none, and whether it is inside the list at all */
typedef struct ListHit {
    int inside;
    int row;
    int button;
    int detail;
} ListHit;

/* a strip rendered once, kept while it may be shown again */
typedef struct ListCacheEntry {
    char *script;
    const char *background;
    int x, w, h;
    uint8_t *pixels;
    unsigned last_used;
} ListCacheEntry;

/* a horizontal band of the list bitmap and the rendered strip shown there */
typedef struct ListStrip {
    SDL_Rect rect;
    ListCacheEntry *entry;
} ListStrip;

struct ListView {
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Rect dirty;
    uint8_t *bitmap;
    SDL_Rect rect;
    AVFilterGraph *graph;
    AVFilterContext *sink;
    ListStrip *strips;
    int nb_strips;
    ListCacheEntry **cache;
    int nb_cache;
    size_t cache_bytes;
    unsigned frame;
    ListEntry *entries;
    int nb_entries;
    /* the listed entries in display order, and the scratch space sorting them needs */
    int *rows, *sort_tmp;
    int nb_rows;
    char search[64];
    int selected;
    int playing;
    int mouse_x, mouse_y;
    ListHit hover;
    int render_pending;
    int changed;
    int pinned;
    int64_t fade_start;
    int64_t last_input;
    double drawn_alpha;
    int width, height;
    /* the columns the list is sorted by, the most recently clicked first */
    ListSortKey sort[LIST_SORT_NB];
    int shuffle_seed;
    int filter_missing_reported;
};

static double list_alpha(const ListView *lv, int64_t now)
{
    double alpha = (now - lv->fade_start) / (double)LIST_FADE_TIME;

    if (!lv->bitmap)
        return 0;
    if (!lv->pinned)
        alpha = FFMIN(alpha, 1 - (now - lv->last_input - LIST_HOLD_TIME) / (double)LIST_FADE_TIME);
    return av_clipd(alpha, 0, 1);
}

static const char *entry_tag(const char *tag)
{
    return tag ? tag : "";
}

/* case-insensitive, with missing tags after all others */
static int compare_tags(const char *a, const char *b)
{
    a = entry_tag(a);
    b = entry_tag(b);
    return !*a != !*b ? !*a - !*b : av_strcasecmp(a, b);
}

/* the rank of an entry in the shuffled order for the current seed */
static uint32_t shuffle_rank(const ListView *lv, int index)
{
    uint32_t rank = (index + 1) * 0x9E3779B1u ^ lv->shuffle_seed * 0x85EBCA77u;

    rank ^= rank >> 15;
    rank *= 0x2C1B3C6Du;
    return rank ^ rank >> 12;
}

static int compare_rows(const ListView *lv, const int *a, const int *b)
{
    const ListEntry *ea = &lv->entries[*a], *eb = &lv->entries[*b];

    for (int i = 0; i < LIST_SORT_NB; i++) {
        int cmp;

        switch (lv->sort[i].key) {
        case LIST_SORT_ARTIST:  cmp = compare_tags(ea->artist, eb->artist); break;
        case LIST_SORT_TITLE:   cmp = compare_tags(ea->title, eb->title);   break;
        case LIST_SORT_LENGTH:  cmp = FFDIFFSIGN(ea->length, eb->length);   break;
        case LIST_SORT_SHUFFLE: cmp = FFDIFFSIGN(shuffle_rank(lv, *a), shuffle_rank(lv, *b)); break;
        default:                cmp = *a - *b;                              break;
        }
        if (cmp)
            return lv->sort[i].descending ? -cmp : cmp;
    }
    return 0;
}

/* a column clicked again flips its order, any other becomes the first key ahead of the previous ones;
 * the shuffle button instead steps its seed by the side it was clicked on */
static void list_sort_by(ListView *lv, int key, int seed_step)
{
    int i = 0;

    while (lv->sort[i].key != key)
        i++;
    if (key == LIST_SORT_SHUFFLE)
        lv->shuffle_seed += seed_step;
    else if (i == 0)
        lv->sort[0].descending ^= 1;
    if (i == 0)
        return;
    memmove(&lv->sort[1], &lv->sort[0], i * sizeof(*lv->sort));
    lv->sort[0] = (ListSortKey){ key, 0 };
}

/* rebuilds the rows matching the search in the current sort order, keeping the selected entry selected */
static void list_update_rows(ListView *lv)
{
    int selected = list_view_selected(lv);

    lv->nb_rows = 0;
    for (int i = 0; i < lv->nb_entries; i++)
        if (av_stristr(entry_tag(lv->entries[i].artist), lv->search) || av_stristr(entry_tag(lv->entries[i].title), lv->search))
            lv->rows[lv->nb_rows++] = i;
#define CMP(a, b) compare_rows(lv, a, b)
    AV_MSORT(lv->rows, lv->sort_tmp, lv->nb_rows, int, CMP);
#undef CMP
    lv->selected = 0;
    for (int row = 0; row < lv->nb_rows; row++)
        if (lv->rows[row] == selected)
            lv->selected = row;
}

static const struct {
    const char *label;
    int width;
} sort_buttons[LIST_SORT_NB] = {
    [LIST_SORT_NUMBER] = { "#",      5 },
    [LIST_SORT_ARTIST] = { "Artist", 8 },
    [LIST_SORT_TITLE]  = { "Title",  7 },
    [LIST_SORT_LENGTH] = { "Duration", 10 },
    [LIST_SORT_SHUFFLE] = { "Shuffle", 14 },
};

#define LIST_EVENT "Dialogue: 0:00:00.00,9999:00:00.00,"

static void bprint_box(AVBPrint *script, const char *style, int x, int y, int w, int h)
{
    av_bprintf(script, LIST_EVENT "%s,{\\pos(%d,%d)\\p1}m 0 0 l %d 0 %d %d 0 %d{\\p0}\n", style, x, y, w, w, h, h);
}

/* a text cell cut off at the right and bottom of its column, with the characters libass would read as tags or line breaks blanked */
static void bprint_cell(AVBPrint *script, const char *style, int x, int y, int right, int bottom, const char *text)
{
    av_bprintf(script, LIST_EVENT "%s,{\\pos(%d,%d)\\clip(0,0,%d,%d)}", style, x, y, right, bottom);
    for (text = entry_tag(text); *text; text++)
        av_bprint_chars(script, strchr("{}\\\r\n", *text) ? ' ' : *text, 1);
    av_bprint_chars(script, '\n', 1);
}

typedef struct ListLayout {
    int font, line, width, height, canvas_h, strip_h, nb_rows, first;
    int button_x[LIST_SORT_NB], button_y[LIST_SORT_NB];
    int search_x, search_y, rows_y, text_x, text_right, length_x, detail_x, canvas_w;
} ListLayout;

/* button widths are in half font sizes, a quarter apart, wrapping onto further lines when the panel is too narrow */
static ListLayout list_layout(const ListView *lv)
{
    ListLayout l;
    int x, y;

    l.font       = FFMAX(lv->height / 30, 12);
    l.line       = l.font * 9 / 4;
    l.width      = FFMIN(FFMAX(lv->width * 3 / 5, l.font * 26), lv->width - l.font * 2);
    x = y = l.font / 2;
    for (int b = 0; b < LIST_SORT_NB; b++) {
        int w = sort_buttons[b].width * l.font / 2;

        if (x > l.font / 2 && x + w > l.width - l.font / 2) {
            x  = l.font / 2;
            y += l.font * 3 / 2;
        }
        l.button_x[b] = x;
        l.button_y[b] = y;
        x += w + l.font / 4;
    }
    /* the search field follows the buttons, or takes its own line when they leave it less than 8 em */
    if (x + l.font * 8 > l.width - l.font / 2) {
        x  = l.font / 2;
        y += l.font * 3 / 2;
    }
    l.search_x   = x;
    l.search_y   = y;
    l.rows_y     = lv->pinned ? y + l.font * 3 / 2 : l.button_y[LIST_SORT_NB - 1] + l.font * 3 / 2;
    l.canvas_h   = FFMIN(y + l.font * 2 + LIST_ROWS * l.line, lv->height - l.font * 2);
    l.strip_h    = FFMAX(l.line, y + l.font * 3 / 2);
    l.nb_rows    = FFMIN(lv->nb_rows, FFMAX(1, (l.canvas_h - l.rows_y - l.font / 2) / l.line));
    l.first      = av_clip(lv->selected - l.nb_rows / 2, 0, lv->nb_rows - l.nb_rows);
    l.height     = l.rows_y + l.nb_rows * l.line + l.font / 2;
    l.text_x     = l.font * 3;
    l.text_right = l.width - l.font * 11 / 2;
    l.length_x   = l.width - l.font * 5 / 2;
    l.detail_x   = l.width - l.font * 5 / 4;
    l.canvas_w   = lv->width - l.font * 2;
    return l;
}

static ListHit list_hit(const ListLayout *l, int x, int y)
{
    ListHit hit = { 0, -1, -1, 0 };
    int line;

    x -= l->font;
    y -= l->font;
    if (x < 0 || y < 0 || x >= l->width || y >= FFMIN(l->height, l->canvas_h))
        return hit;
    hit.inside = 1;
    line = (y - l->rows_y) / l->line;
    if (y >= l->rows_y && line < l->nb_rows) {
        hit.row    = l->first + line;
        hit.detail = x >= l->width - l->font * 2;
    }
    for (int b = 0; b < FF_ARRAY_ELEMS(sort_buttons); b++)
        if (x >= l->button_x[b] && x < l->button_x[b] + sort_buttons[b].width * l->font / 2 &&
            y >= l->button_y[b] && y < l->button_y[b] + l->font * 5 / 4)
            hit.button = b;
    return hit;
}

/* a circle outline drawn as four bezier arcs around a center */
static void bprint_ring(AVBPrint *script, int cx, int cy, int r)
{
    int k = r * 552 / 1000;

    av_bprintf(script, LIST_EVENT "Ring,{\\an5\\pos(%d,%d)\\p1}m 0 %d b 0 %d %d 0 %d 0 b %d 0 %d %d %d %d "
               "b %d %d %d %d %d %d b %d %d 0 %d 0 %d{\\p0}\n",
               cx, cy, r, r - k, r - k, r, r + k, 2 * r, r - k, 2 * r, r, 2 * r, r + k, r + k, 2 * r, r, 2 * r,
               r - k, 2 * r, r + k, r);
}

/* strip backgrounds, with premultiplied alpha as the ass filter composites */
static const char panel_color[] = "0x000000BF";
static const char hover_color[] = "0x373737CD";
static const char popup_color[] = "0x000000DF";

static void bprint_styles(AVBPrint *script, const ListLayout *l)
{
    av_bprintf(script,
               "[Script Info]\nScriptType: v4.00+\nPlayResX: %d\nPlayResY: %d\nWrapStyle: 2\nYCbCr Matrix: None\n\n"
               "[V4+ Styles]\n"
               "Format: Name, Fontname, Fontsize, PrimaryColour, OutlineColour, Bold, Outline, Alignment\n"
               "Style: Button,Sans,%d,&H80FFFFFF,&H80FFFFFF,0,0,7\n"
               "Style: Row,Sans,%d,&H00FFFFFF,&H00000000,0,%d,7\n"
               "Style: Selected,Sans,%d,&H0080D0FF,&H00000000,-1,%d,7\n"
               "Style: Artist,Sans,%d,&H00C0C0C0,&H00000000,0,%d,7\n"
               "Style: ArtistSelected,Sans,%d,&H0080D0FF,&H00000000,-1,%d,7\n"
               "Style: Playing,Sans,%d,&H00FFFFFF,&H00FFFFFF,0,0,7\n"
               "Style: Hint,Sans,%d,&H00A0A0A0,&H00000000,0,%d,7\n"
               "Style: ButtonHover,Sans,%d,&H50FFFFFF,&H50FFFFFF,0,0,7\n"
               "Style: Ring,Sans,%d,&HFF000000,&H00A0A0A0,0,%d,7\n"
               "Style: Detail,Sans,%d,&H00FFFFFF,&H00000000,0,%d,7\n\n"
               "[Events]\nFormat: Start, End, Style, Text\n",
               l->canvas_w, l->strip_h, l->font, l->font, l->font / 16 + 1, l->font, l->font / 16 + 1,
               l->font * 4 / 5, l->font / 16 + 1, l->font * 4 / 5, l->font / 16 + 1, l->font, l->font, l->font / 16 + 1,
               l->font, l->font, l->font / 16 + 1, l->font * 4 / 5, l->font / 16 + 1);
}

static void bprint_header_strip(const ListView *lv, AVBPrint *script, const ListLayout *l)
{
    if (lv->pinned)
        bprint_cell(script, *lv->search ? "Row" : "Hint", l->search_x, l->search_y, l->length_x,
                    l->search_y + l->font * 3 / 2, *lv->search ? lv->search : "type to search");
    for (int b = 0; b < FF_ARRAY_ELEMS(sort_buttons); b++) {
        int x = l->button_x[b], y = l->button_y[b], w = sort_buttons[b].width * l->font / 2;

        bprint_box(script, b == lv->hover.button ? "ButtonHover" : "Button", x, y, w, l->font * 5 / 4);
        av_bprintf(script, LIST_EVENT "%s,{\\an5\\pos(%d,%d)}", lv->sort[0].key == b ? "Selected" : "Row",
                   x + w / 2, y + l->font * 5 / 8);
        if (b == LIST_SORT_SHUFFLE)
            av_bprintf(script, "\xe2\x97\x82 %s %d \xe2\x96\xb8\n", sort_buttons[b].label, lv->shuffle_seed);
        else
            av_bprintf(script, "%s%s\n", sort_buttons[b].label,
                       lv->sort[0].key != b ? "" : lv->sort[0].descending ? " \xe2\x96\xbc" : " \xe2\x96\xb2");
    }
}

static void bprint_row_strip(const ListView *lv, AVBPrint *script, const ListLayout *l, int r)
{
    int index = lv->rows[r];
    const ListEntry *entry = &lv->entries[index];
    const char *style = r == lv->selected ? "Selected" : "Row";
    int64_t seconds;

    if (index == lv->playing)
        bprint_box(script, "Playing", 0, 0, l->font / 6, l->line);
    av_bprintf(script, LIST_EVENT "%s,{\\an6\\pos(%d,%d)}%d\n", style, l->font * 5 / 2, l->line / 2, index + 1);
    bprint_cell(script, style, l->text_x, l->font / 8, l->text_right, l->line, entry->title);
    bprint_cell(script, r == lv->selected ? "ArtistSelected" : "Artist", l->text_x, l->font * 5 / 4,
                l->text_right, l->line, entry->artist);
    bprint_ring(script, l->detail_x, l->line / 2, l->font * 9 / 20);
    av_bprintf(script, LIST_EVENT "Hint,{\\an5\\pos(%d,%d)}i\n", l->detail_x, l->line / 2);
    if (entry->length < 0)
        return;
    seconds = av_rescale_rnd(entry->length, 1, AV_TIME_BASE, AV_ROUND_NEAR_INF);
    av_bprintf(script, LIST_EVENT "%s,{\\an6\\pos(%d,%d)}", style, l->length_x, l->line / 2);
    if (seconds >= 3600)
        av_bprintf(script, "%"PRId64":%02d:%02d\n", seconds / 3600, (int)(seconds / 60 % 60), (int)(seconds % 60));
    else
        av_bprintf(script, "%d:%02d\n", (int)(seconds / 60), (int)(seconds % 60));
}

static void cache_entry_free(ListCacheEntry **entry)
{
    if (*entry) {
        av_freep(&(*entry)->script);
        av_freep(&(*entry)->pixels);
    }
    av_freep(entry);
}

/* drops the least recently used strips not shown in this frame until the new one fits into twice the bitmap size */
static void cache_make_room(ListView *lv, size_t bytes)
{
    while (lv->cache_bytes + bytes > 2 * (size_t)lv->rect.w * lv->rect.h * 4) {
        int oldest = -1;

        for (int i = 0; i < lv->nb_cache; i++)
            if (lv->cache[i]->last_used != lv->frame && (oldest < 0 || lv->cache[i]->last_used < lv->cache[oldest]->last_used))
                oldest = i;
        if (oldest < 0)
            return;
        lv->cache_bytes -= (size_t)lv->cache[oldest]->w * lv->cache[oldest]->h * 4;
        cache_entry_free(&lv->cache[oldest]);
        lv->cache[oldest] = lv->cache[--lv->nb_cache];
    }
}

static int strip_render(ListView *lv, ListCacheEntry *entry);

/* the cached strip for the script, background and place, rendered when there is none */
static int cache_get(ListView *lv, const SDL_Rect *rect, const char *background, const AVBPrint *script, ListCacheEntry **entry)
{
    int ret;

    for (int i = 0; i < lv->nb_cache; i++) {
        ListCacheEntry *e = lv->cache[i];

        if (e->background == background && e->x == rect->x && e->w == rect->w && e->h == rect->h &&
            !strcmp(e->script, script->str)) {
            e->last_used = lv->frame;
            *entry = e;
            return 0;
        }
    }
    cache_make_room(lv, (size_t)rect->w * rect->h * 4);
    if (!(lv->nb_cache & (lv->nb_cache - 1))) {
        ListCacheEntry **grown = av_realloc_array(lv->cache, lv->nb_cache ? 2 * lv->nb_cache : 1, sizeof(*grown));

        if (!grown)
            return AVERROR(ENOMEM);
        lv->cache = grown;
    }
    if (!(*entry = av_mallocz(sizeof(**entry))))
        return AVERROR(ENOMEM);
    **entry = (ListCacheEntry){ av_strdup(script->str), background, rect->x, rect->w, rect->h,
                                av_malloc_array(rect->w * rect->h, 4), lv->frame };
    ret = (*entry)->script && (*entry)->pixels ? strip_render(lv, *entry) : AVERROR(ENOMEM);
    if (ret < 0) {
        cache_entry_free(entry);
        return ret;
    }
    lv->cache[lv->nb_cache++] = *entry;
    lv->cache_bytes += (size_t)rect->w * rect->h * 4;
    return 0;
}

static int strip_add(ListView *lv, ListStrip **strips, int *nb, SDL_Rect rect, const char *background, const AVBPrint *script)
{
    int ret;

    if (rect.w <= 0 || rect.h <= 0)
        return 0;
    if (!av_bprint_is_complete(script))
        return AVERROR(ENOMEM);
    if (!(*nb & (*nb - 1))) {
        ListStrip *grown = av_realloc_array(*strips, *nb ? 2 * *nb : 1, sizeof(**strips));

        if (!grown)
            return AVERROR(ENOMEM);
        *strips = grown;
    }
    (*strips)[*nb].rect = rect;
    if ((ret = cache_get(lv, &rect, background, script, &(*strips)[*nb].entry)) < 0)
        return ret;
    (*nb)++;
    return 0;
}

/* the strips the list consists of right now: the header, each visible row, the bottom padding and the lines of the metadata
 * popup, beside the list or over its rows when the window leaves no room there */
static int list_strips(ListView *lv, const ListLayout *l, ListStrip **strips, int *nb)
{
    AVBPrint script;
    int ret = 0;

    av_bprint_init(&script, 0, AV_BPRINT_SIZE_UNLIMITED);
    for (int i = 0; ret >= 0 && i < l->nb_rows + 2; i++) {
        int row = i && i <= l->nb_rows ? l->first + i - 1 : -1;
        SDL_Rect rect = { 0, i ? l->rows_y + (i - 1) * l->line : 0, l->width, i ? l->line : l->rows_y };

        if (i > l->nb_rows)
            rect.h = l->font / 2;
        rect.h = FFMIN(rect.h, l->canvas_h - rect.y);
        av_bprint_clear(&script);
        bprint_styles(&script, l);
        if (!i)
            bprint_header_strip(lv, &script, l);
        else if (row >= 0)
            bprint_row_strip(lv, &script, l, row);
        ret = strip_add(lv, strips, nb, rect, row >= 0 && row == lv->hover.row ? hover_color : panel_color, &script);
    }
    if (ret >= 0 && lv->hover.detail) {
        const AVDictionary *metadata = lv->entries[lv->rows[lv->hover.row]].metadata;
        const AVDictionaryEntry *above = NULL, *tag = av_dict_iterate(metadata, NULL);
        int beside = l->canvas_w - l->width >= l->font * 12;
        int height = FFMIN(av_dict_count(metadata) * l->font + l->font / 2, l->canvas_h);
        int x      = beside ? l->width + l->font / 2 : l->text_x;
        int right  = beside ? l->canvas_w : l->length_x - l->font / 2;
        int y      = av_clip(l->rows_y + (lv->hover.row - l->first) * l->line, 0, l->canvas_h - height);

        /* the descenders of a line reach into the line below, so each line's strip also draws the line above it */
        for (int top = 0; ret >= 0 && top < height && (above || tag); top += l->font) {
            const AVDictionaryEntry *lines[2] = { above, tag };
            SDL_Rect rect = { x, y + top, right - x, FFMIN(l->font, height - top) };

            av_bprint_clear(&script);
            bprint_styles(&script, l);
            for (int i = 0; i < 2; i++) {
                if (!lines[i])
                    continue;
                bprint_cell(&script, "Detail", x + l->font / 2, l->font / 4 + (i - 1) * l->font, x + l->font * 7, rect.h,
                            lines[i]->key);
                bprint_cell(&script, "Detail", x + l->font * 15 / 2, l->font / 4 + (i - 1) * l->font, right - l->font / 4,
                            rect.h, lines[i]->value);
            }
            ret   = strip_add(lv, strips, nb, rect, popup_color, &script);
            above = tag;
            tag   = tag ? av_dict_iterate(metadata, tag) : NULL;
        }
    }
    av_bprint_finalize(&script, NULL);
    return ret;
}

static void list_discard(ListView *lv)
{
    avfilter_graph_free(&lv->graph);
    av_freep(&lv->bitmap);
    av_freep(&lv->strips);
    lv->nb_strips = 0;
    for (int i = 0; i < lv->nb_cache; i++)
        cache_entry_free(&lv->cache[i]);
    av_freep(&lv->cache);
    lv->nb_cache    = 0;
    lv->cache_bytes = 0;
    if (lv->texture)
        SDL_DestroyTexture(lv->texture);
    lv->texture = NULL;
}

/* a graph rendering one strip of the bitmap at a time, the bitmap itself and the texture it is uploaded to */
static int list_configure(ListView *lv, const ListLayout *l, const SDL_Rect *canvas)
{
    const AVFilter *ass_filter = avfilter_get_by_name("ass");
    AVFilterContext *source, *ass;
    char source_args[64];
    int ret;

    if (!ass_filter) {
        if (!lv->filter_missing_reported++)
            av_log(NULL, AV_LOG_ERROR, "The chapter list needs the ass filter, which this build lacks\n");
        return AVERROR_FILTER_NOT_FOUND;
    }
    list_discard(lv);
    lv->rect = *canvas;
    if (l->canvas_w <= 0 || l->canvas_h <= 0 || l->canvas_w > INT_MAX / 4 / l->canvas_h)
        return AVERROR(EINVAL);
    if (!(lv->graph = avfilter_graph_alloc()) ||
        !(lv->bitmap = av_mallocz(l->canvas_w * l->canvas_h * 4)))
        return AVERROR(ENOMEM);
    if (!(lv->texture = SDL_CreateTexture(lv->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                          l->canvas_w, l->canvas_h)) ||
        SDL_SetTextureBlendMode(lv->texture, SDL_BLENDMODE_BLEND) < 0)
        return AVERROR_EXTERNAL;
    lv->dirty = (SDL_Rect){ 0, 0, l->canvas_w, l->canvas_h };
    snprintf(source_args, sizeof(source_args), "color=black@0:rate=1:size=%dx%d", l->canvas_w, l->strip_h);
    ass      = avfilter_graph_alloc_filter(lv->graph, ass_filter, "ass");
    lv->sink = avfilter_graph_alloc_filter(lv->graph, avfilter_get_by_name("buffersink"), "sink");
    if (!ass || !lv->sink)
        return AVERROR(ENOMEM);
    if ((ret = avfilter_graph_create_filter(&source, avfilter_get_by_name("color"), "source", source_args, NULL, lv->graph)) < 0 ||
        (ret = av_opt_set(ass, "script", "[Script Info]\nScriptType: v4.00+\n", AV_OPT_SEARCH_CHILDREN)) < 0 ||
        (ret = av_opt_set_int(ass, "alpha", 1, AV_OPT_SEARCH_CHILDREN)) < 0 ||
        (ret = avfilter_init_str(ass, NULL)) < 0 ||
        (ret = av_opt_set_array(lv->sink, "pixel_formats", AV_OPT_SEARCH_CHILDREN, 0, 1, AV_OPT_TYPE_PIXEL_FMT,
                                &(enum AVPixelFormat){ AV_PIX_FMT_RGB32 })) < 0 ||
        (ret = avfilter_init_str(lv->sink, NULL)) < 0 ||
        (ret = avfilter_link(source, 0, ass, 0)) < 0 ||
        (ret = avfilter_link(ass, 0, lv->sink, 0)) < 0)
        return ret;
    return avfilter_graph_config(lv->graph, NULL);
}

/* renders the strip, the ass filter composing it over its background */
static int strip_render(ListView *lv, ListCacheEntry *entry)
{
    AVFrame *frame = av_frame_alloc();
    int alpha = av_pix_fmt_desc_get(AV_PIX_FMT_RGB32)->comp[3].offset;
    uint32_t scale[256] = { 1 << 16 };
    int ret;

    if (!frame)
        return AVERROR(ENOMEM);
    for (int a = 1; a < 256; a++)
        scale[a] = ((255 << 16) + a - 1) / a;
    if ((ret = avfilter_graph_send_command(lv->graph, "source", "color", entry->background, NULL, 0, 0)) >= 0 &&
        (ret = avfilter_graph_send_command(lv->graph, "ass", "script", entry->script, NULL, 0, 0)) >= 0)
        ret = av_buffersink_get_frame(lv->sink, frame);
    /* the ass filter composites with premultiplied alpha, SDL blends straight alpha */
    for (int y = 0; ret >= 0 && y < entry->h; y++) {
        const uint8_t *src = frame->data[0] + y * frame->linesize[0] + entry->x * 4;
        uint8_t *dst = entry->pixels + y * entry->w * 4;

        for (int x = 0; x < entry->w * 4; x += 4)
            for (int c = 0; c < 4; c++)
                dst[x + c] = c == alpha ? src[x + c] : FFMIN(src[x + c] * scale[src[x + alpha]] >> 16, 255);
    }
    av_frame_free(&frame);
    return ret;
}

static void bitmap_touched(ListView *lv, const SDL_Rect *rect)
{
    SDL_UnionRect(&lv->dirty, rect, &lv->dirty);
    lv->changed = 1;
}

/* brings the bitmap up to date: what differs from the strips shown before is cleared and composed again from the strips
 * covering it, rendering only strips that are not cached */
static int list_render(ListView *lv)
{
    ListLayout l = list_layout(lv);
    SDL_Rect canvas = { l.font, l.font, l.canvas_w, l.canvas_h }, dirty = { 0, 0, 0, 0 };
    ListStrip *strips = NULL;
    int nb = 0, ret = 0;

    lv->render_pending = 0;
    lv->hover = list_hit(&l, lv->mouse_x, lv->mouse_y);
    /* the strips shown stay cached until compared with the new ones */
    lv->frame++;
    for (int i = 0; i < lv->nb_strips; i++)
        lv->strips[i].entry->last_used = lv->frame;
    if (!lv->graph || !SDL_RectEquals(&lv->rect, &canvas))
        ret = list_configure(lv, &l, &canvas);
    if (ret >= 0)
        ret = list_strips(lv, &l, &strips, &nb);
    for (int i = 0; ret >= 0 && i < FFMAX(nb, lv->nb_strips); i++) {
        const ListStrip *old = i < lv->nb_strips ? &lv->strips[i] : NULL, *new = i < nb ? &strips[i] : NULL;

        if (old && new && old->entry == new->entry && SDL_RectEquals(&old->rect, &new->rect))
            continue;
        if (old)
            SDL_UnionRect(&dirty, &old->rect, &dirty);
        if (new)
            SDL_UnionRect(&dirty, &new->rect, &dirty);
    }
    if (ret >= 0 && dirty.w) {
        for (int y = dirty.y; y < dirty.y + dirty.h; y++)
            memset(lv->bitmap + (y * lv->rect.w + dirty.x) * 4, 0, dirty.w * 4);
        for (int i = 0; i < nb; i++) {
            const ListCacheEntry *entry = strips[i].entry;
            SDL_Rect part;

            if (!SDL_IntersectRect(&strips[i].rect, &dirty, &part))
                continue;
            for (int y = part.y; y < part.y + part.h; y++)
                memcpy(lv->bitmap + (y * lv->rect.w + part.x) * 4,
                       entry->pixels + ((y - strips[i].rect.y) * entry->w + part.x - strips[i].rect.x) * 4, part.w * 4);
        }
        bitmap_touched(lv, &dirty);
    }
    if (ret < 0) {
        if (ret != AVERROR_FILTER_NOT_FOUND)
            av_log(NULL, AV_LOG_ERROR, "Could not render the chapter list: %s\n", av_err2str(ret));
        list_discard(lv);
        av_freep(&strips);
        nb = 0;
        if (lv->pinned)
            SDL_StopTextInput();
        lv->pinned = 0;
    }
    av_freep(&lv->strips);
    lv->strips    = strips;
    lv->nb_strips = nb;
    return ret;
}

static void list_select(ListView *lv, int row)
{
    int64_t now = av_gettime_relative();

    lv->selected       = row;
    lv->fade_start     = now - list_alpha(lv, now) * LIST_FADE_TIME;
    lv->last_input     = now;
    lv->render_pending = 1;
}

/* appends the typed text to the search, or erases its last character when nothing was typed */
static void list_search(ListView *lv, const char *typed)
{
    int len = strlen(lv->search);

    if (typed) {
        if (len + strlen(typed) < sizeof(lv->search))
            av_strlcat(lv->search, typed, sizeof(lv->search));
    } else if (len) {
        while (len > 0 && (lv->search[--len] & 0xC0) == 0x80)
            ;
        lv->search[len] = 0;
    }
    list_update_rows(lv);
    lv->render_pending = 1;
}

ListView *list_view_alloc(SDL_Renderer *renderer)
{
    ListView *lv = av_mallocz(sizeof(*lv));

    if (!lv)
        return NULL;
    lv->renderer = renderer;
    lv->playing  = -1;
    lv->mouse_x  = lv->mouse_y = -1;
    lv->hover    = (ListHit){ 0, -1, -1, 0 };
    for (int i = 0; i < LIST_SORT_NB; i++)
        lv->sort[i].key = i;
    return lv;
}

void list_view_free(ListView **plv)
{
    ListView *lv = *plv;

    if (!lv)
        return;
    list_discard(lv);
    av_freep(&lv->entries);
    av_freep(&lv->rows);
    av_freep(&lv->sort_tmp);
    av_freep(plv);
}

int list_view_show(ListView *lv, const ListEntry *entries, int nb_entries, int selected)
{
    ListEntry *copy;
    int *rows, *sort_tmp, previous = list_view_selected(lv);

    if (!lv->renderer)
        return AVERROR(EINVAL);
    copy     = av_malloc_array(nb_entries, sizeof(*copy));
    rows     = av_malloc_array(nb_entries, sizeof(*rows));
    sort_tmp = av_malloc_array(nb_entries, sizeof(*sort_tmp));
    if (!copy || !rows || !sort_tmp) {
        av_free(copy);
        av_free(rows);
        av_free(sort_tmp);
        return AVERROR(ENOMEM);
    }
    if (nb_entries)
        memcpy(copy, entries, nb_entries * sizeof(*entries));
    av_free(lv->entries);
    av_free(lv->rows);
    av_free(lv->sort_tmp);
    lv->entries    = copy;
    lv->rows       = rows;
    lv->sort_tmp   = sort_tmp;
    lv->nb_entries = nb_entries;
    lv->nb_rows    = 0;
    list_update_rows(lv);
    for (int row = 0; row < lv->nb_rows; row++)
        if (lv->rows[row] == previous)
            lv->selected = row;
    for (int row = 0; row < lv->nb_rows; row++)
        if (lv->rows[row] == selected)
            lv->selected = row;
    list_select(lv, lv->selected);
    return 0;
}

void list_view_pin(ListView *lv, int pinned)
{
    lv->pinned = pinned;
    if (pinned) {
        SDL_StartTextInput();
    } else {
        SDL_StopTextInput();
        lv->last_input = av_gettime_relative() - LIST_HOLD_TIME;
        lv->search[0] = 0;
        list_update_rows(lv);
    }
    lv->render_pending = 1;
}

int list_view_pinned(const ListView *lv)
{
    return lv->pinned;
}

int list_view_visible(const ListView *lv)
{
    return lv->pinned || list_alpha(lv, av_gettime_relative()) > 0;
}

int list_view_selected(const ListView *lv)
{
    return lv->nb_rows ? lv->rows[lv->selected] : -1;
}

void list_view_move(ListView *lv, int delta)
{
    if (lv->nb_rows)
        list_select(lv, av_clip(lv->selected + delta, 0, lv->nb_rows - 1));
}

int list_view_key(ListView *lv, SDL_Keycode key)
{
    switch (key) {
    case SDLK_ESCAPE:    list_view_pin(lv, 0); break;
    case SDLK_UP:        list_view_move(lv, -1); break;
    case SDLK_DOWN:      list_view_move(lv, 1); break;
    case SDLK_BACKSPACE: list_search(lv, NULL); break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:  return list_view_selected(lv);
    }
    return -1;
}

void list_view_text(ListView *lv, const char *text)
{
    list_search(lv, text);
}

/* asks for a render when what a visible list points at changed; the render waits until the pending events are handled,
 * so a sweep across the list renders once */
void list_view_hover(ListView *lv, int x, int y)
{
    ListLayout l;
    ListHit hit;

    lv->mouse_x = x;
    lv->mouse_y = y;
    if (!list_view_visible(lv))
        return;
    l   = list_layout(lv);
    hit = list_hit(&l, x, y);
    if (hit.row != lv->hover.row || hit.button != lv->hover.button || hit.detail != lv->hover.detail)
        lv->render_pending = 1;
}

int list_view_click(ListView *lv, int x, int y, int *chosen)
{
    ListLayout l = list_layout(lv);
    ListHit hit = list_hit(&l, x, y);

    *chosen = hit.row >= 0 && !hit.detail ? lv->rows[hit.row] : -1;
    if (hit.button >= 0) {
        int button_w = sort_buttons[hit.button].width * l.font / 2;

        list_sort_by(lv, hit.button, (x - l.font - l.button_x[hit.button]) * 2 < button_w ? -1 : 1);
        list_update_rows(lv);
        list_select(lv, lv->selected);
    }
    return hit.inside;
}

void list_view_set_size(ListView *lv, int width, int height)
{
    if (lv->width == width && lv->height == height)
        return;
    lv->width  = width;
    lv->height = height;
    if (lv->bitmap)
        lv->render_pending = 1;
}

void list_view_set_playing(ListView *lv, int entry)
{
    if (lv->playing == entry)
        return;
    lv->playing        = entry;
    lv->render_pending = 1;
}

int list_view_update(ListView *lv)
{
    int changed;

    if (lv->render_pending)
        list_render(lv);
    changed = lv->changed || list_alpha(lv, av_gettime_relative()) != lv->drawn_alpha;
    lv->changed = 0;
    return changed;
}

/* uploads what the texture lacks of the bitmap, at most once per refresh */
void list_view_draw(ListView *lv)
{
    double alpha = list_alpha(lv, av_gettime_relative());
    SDL_Rect *dirty = &lv->dirty;

    lv->drawn_alpha = alpha;
    if (alpha <= 0)
        return;
    if (dirty->w && !SDL_UpdateTexture(lv->texture, dirty, lv->bitmap + (dirty->y * lv->rect.w + dirty->x) * 4, lv->rect.w * 4))
        *dirty = (SDL_Rect){ 0, 0, 0, 0 };
    SDL_SetTextureAlphaMod(lv->texture, alpha * 255);
    SDL_RenderCopy(lv->renderer, lv->texture, NULL, &lv->rect);
}
