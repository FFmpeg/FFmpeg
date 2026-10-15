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

/**
 * @file
 * A list overlay for ffplay: entries with title, artist, length and metadata that the user can sort, filter by typing,
 * browse with keys, wheel and mouse, and pick one of.
 *
 * A ListView is used from the thread handling the SDL events and rendering; separate ListViews are independent.
 */

#ifndef FFTOOLS_FFPLAY_LIST_H
#define FFTOOLS_FFPLAY_LIST_H

#include <stdint.h>

#include <SDL.h>

#include "libavutil/dict.h"

typedef struct ListEntry {
    const char *title;
    const char *artist;
    int64_t length;                 ///< in AV_TIME_BASE units, negative when unknown
    AVDictionary *metadata;         ///< shown when the mouse is over the entry's symbol, may be NULL
} ListEntry;

typedef struct ListView ListView;

/**
 * @param renderer NULL when there is none, which leaves the list unable to show
 * @return the list, hidden, or NULL on allocation failure
 */
ListView *list_view_alloc(SDL_Renderer *renderer);

void list_view_free(ListView **lv);

/**
 * Replaces the entries and shows the list, fading it in.
 *
 * @param entries  numbered from 1 in the list; their strings and dictionaries must stay valid until the next call or
 *                 list_view_free()
 * @param selected entry to select, the selection is kept when it is -1 or not listed
 * @return 0 or a negative error code
 */
int list_view_show(ListView *lv, const ListEntry *entries, int nb_entries, int selected);

/**
 * Removes the entries, which may be freed after this, and resets the sort and the search.
 */
void list_view_clear(ListView *lv);

/**
 * Keeps the list on screen and lets it take the keyboard and text input, or lets it fade out again.
 */
void list_view_pin(ListView *lv, int pinned);

int list_view_pinned(const ListView *lv);

/**
 * @return whether the list is on screen or about to be
 */
int list_view_visible(const ListView *lv);

/**
 * @return the selected entry, -1 when none is listed
 */
int list_view_selected(const ListView *lv);

/**
 * Moves the selection by delta rows.
 */
void list_view_move(ListView *lv, int delta);

/**
 * Handles a key while the list is pinned: escape unpins it, up/down move the selection, backspace erases from the search.
 *
 * @return the entry chosen by enter, -1 otherwise
 */
int list_view_key(ListView *lv, SDL_Keycode key);

/**
 * Appends text typed while the list is pinned to the search, or erases its last character when text is NULL.
 */
void list_view_text(ListView *lv, const char *text);

/**
 * Clears the search, listing all entries.
 */
void list_view_clear_search(ListView *lv);

/**
 * Keeps the mouse position, in window coordinates, -1 when the mouse left the window.
 */
void list_view_hover(ListView *lv, int x, int y);

/**
 * Handles a left click at window coordinates: a click on an entry chooses it, a click on a column button sorts by it.
 *
 * @param chosen set to the entry clicked, -1 when none was
 * @return whether the click hit the list
 */
int list_view_click(ListView *lv, int x, int y, int *chosen);

/**
 * Sets the size of the window the list is drawn into.
 */
void list_view_set_size(ListView *lv, int width, int height);

/**
 * Marks the entry as playing, -1 for none.
 */
void list_view_set_playing(ListView *lv, int entry);

/**
 * Brings the list up to date for the next frame, once the pending input is handled.
 *
 * @return whether the window needs to be drawn again
 */
int list_view_update(ListView *lv);

/**
 * Draws the list onto the window, between its other content and the present.
 */
void list_view_draw(ListView *lv);

#endif /* FFTOOLS_FFPLAY_LIST_H */
