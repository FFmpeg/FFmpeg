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
 * The content of a directory as list entries, leaving out hidden entries, special files and links to them.
 */

#ifndef FFTOOLS_FFPLAY_DIR_H
#define FFTOOLS_FFPLAY_DIR_H

#include "ffplay_list.h"

typedef struct DirList {
    char *path;          ///< the directory as an absolute path, or its URL
    ListEntry *entries;  ///< ".." first, then the subdirectories and then the files, each by name; directories end with a slash
    int nb_entries;
    int nb_dirs;         ///< the entries up to this one are directories
    int nb_maybe_dirs;   ///< the entries after the directories up to this one may list as one, without a slash
} DirList;

/**
 * Reads the directory into a new list.
 *
 * @param dl      set to the list
 * @param path    a local directory, or the URL of a directory its protocol can list
 * @param int_cb  interrupt callback, asked between entries and by the protocol, may be NULL
 * @param options options for avio_open_dir2(), which removes those it uses
 * @return 0 or a negative error code
 */
int dir_list_read(DirList **dl, const char *path, const AVIOInterruptCB *int_cb, AVDictionary **options);

void dir_list_free(DirList **dl);

/**
 * @return the absolute path or the URL of the entry, without a trailing slash, to be freed with av_free(), NULL on
 *         allocation failure
 */
char *dir_list_path(const DirList *dl, int entry);

#endif /* FFTOOLS_FFPLAY_DIR_H */
