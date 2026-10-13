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

#include "config.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#if HAVE_LSTAT
#include <sys/stat.h>
#endif

#include "libavformat/avio.h"
#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/dynarray.h"
#include "libavutil/error.h"
#include "libavutil/mem.h"

#include "ffplay_dir.h"

enum EntryKind {
    ENTRY_FILE,
    ENTRY_MAYBE_DIR,
    ENTRY_DIR,
};

/* the directories first, then the entries that may be directories, then the files, each by name; the kind of an entry
 * is kept in its length until the entries are sorted */
static int compare_entries(const void *a, const void *b)
{
    const ListEntry *ea = a, *eb = b;
    int kind = (eb->length > ea->length) - (eb->length < ea->length);
    int cmp  = av_strcasecmp(ea->title, eb->title);

    return kind ? kind : cmp ? cmp : strcmp(ea->title, eb->title);
}

/* adds the entry, titled with a trailing slash when it is a directory and with its size when that is not negative */
static int add_entry(DirList *dl, const char *name, enum EntryKind kind, int64_t size)
{
    ListEntry *entry;

    FF_DYNARRAY_ADD(INT_MAX, sizeof(*entry), dl->entries, dl->nb_entries, {
        entry = &dl->entries[dl->nb_entries];
    }, {
        return AVERROR(ENOMEM);
    });
    *entry = (ListEntry){ av_asprintf("%s%s", name, kind == ENTRY_DIR ? "/" : ""), NULL, kind, NULL };
    if (!entry->title || (size >= 0 && av_dict_set_int(&entry->metadata, "size", size, 0) < 0))
        return AVERROR(ENOMEM);
    return 0;
}

/* whether the entry is listed, which hidden entries, special files and links to them are not; a link in a local
 * directory is listed as what it points to, with its size */
static int listed(const char *local_dir, AVIODirEntry *dirent)
{
    if (!dirent->name || !*dirent->name || dirent->name[0] == '.')
        return 0;
#if HAVE_LSTAT
    if (dirent->type == AVIO_ENTRY_SYMBOLIC_LINK && local_dir) {
        char *path = av_append_path_component(local_dir, dirent->name);
        struct stat st;
        int found;

        if (!path)
            return AVERROR(ENOMEM);
        found = !stat(path, &st);
        av_free(path);
        if (!found || !(S_ISDIR(st.st_mode) || S_ISREG(st.st_mode)))
            return 0;
        dirent->type = S_ISDIR(st.st_mode) ? AVIO_ENTRY_DIRECTORY : AVIO_ENTRY_FILE;
        dirent->size = st.st_size;
    }
#endif
    return dirent->type != AVIO_ENTRY_NAMED_PIPE && dirent->type != AVIO_ENTRY_CHARACTER_DEVICE &&
           dirent->type != AVIO_ENTRY_BLOCK_DEVICE && dirent->type != AVIO_ENTRY_SOCKET;
}

/* the local directory as an absolute path without links, NULL with errno set when it cannot be resolved */
static char *resolve_local(const char *path)
{
#if HAVE_REALPATH
    char *resolved = realpath(path, NULL), *copy = resolved ? av_strdup(resolved) : NULL;

    if (resolved && !copy)
        errno = ENOMEM;
    free(resolved);
    return copy;
#else
    errno = ENOSYS;
    return NULL;
#endif
}

int dir_list_read(DirList **pdl, const char *path, const AVIOInterruptCB *int_cb, AVDictionary **options)
{
    const char *protocol = avio_find_protocol_name(path);
    int local = !protocol || !strcmp(protocol, "file");
    AVIODirContext *ctx = NULL;
    AVIODirEntry *dirent = NULL;
    DirList *dl = av_mallocz(sizeof(*dl));
    int ret;

    if (!dl)
        return AVERROR(ENOMEM);
    if (local)
        av_strstart(path, "file:", &path);
    dl->path = local ? resolve_local(path) : av_strdup(path);
    if (!dl->path) {
        ret = AVERROR(local ? errno : ENOMEM);
        goto fail;
    }
    if ((ret = add_entry(dl, "..", ENTRY_DIR, -1)) < 0 || (ret = avio_open_dir2(&ctx, dl->path, int_cb, options)) < 0)
        goto fail;
    while ((ret = avio_read_dir(ctx, &dirent)) >= 0 && dirent) {
        if (int_cb && int_cb->callback && int_cb->callback(int_cb->opaque))
            ret = AVERROR_EXIT;
        else if ((ret = listed(local ? dl->path : NULL, dirent)) > 0)
            ret = add_entry(dl, dirent->name, dirent->type == AVIO_ENTRY_DIRECTORY ? ENTRY_DIR :
                            dirent->type == AVIO_ENTRY_FILE ? ENTRY_FILE : ENTRY_MAYBE_DIR, dirent->size);
        avio_free_directory_entry(&dirent);
        if (ret < 0)
            break;
    }
    avio_close_dir(&ctx);
    if (ret < 0)
        goto fail;
    qsort(dl->entries + 1, dl->nb_entries - 1, sizeof(*dl->entries), compare_entries);
    for (int i = 0; i < dl->nb_entries; i++) {
        dl->nb_dirs      += dl->entries[i].length == ENTRY_DIR;
        dl->nb_maybe_dirs += dl->entries[i].length == ENTRY_MAYBE_DIR;
        dl->entries[i].length = -1;
    }
    *pdl = dl;
    return 0;
fail:
    dir_list_free(&dl);
    return ret;
}

void dir_list_free(DirList **pdl)
{
    DirList *dl = *pdl;

    if (!dl)
        return;
    for (int i = 0; i < dl->nb_entries; i++) {
        av_freep(&dl->entries[i].title);
        av_dict_free(&dl->entries[i].metadata);
    }
    av_freep(&dl->entries);
    av_freep(&dl->path);
    av_freep(pdl);
}

/* the directory above, which is the root of a path or the host of a URL at most */
static char *parent_path(const char *path)
{
    const char *scheme = *path == '/' ? NULL : strstr(path, "://");
    const char *top    = scheme ? strchr(scheme + 3, '/') : path;
    const char *slash  = top ? strrchr(top, '/') : NULL;

    return av_strndup(path, slash ? FFMAX(slash, top + !scheme) - path : strlen(path));
}

char *dir_list_path(const DirList *dl, int entry)
{
    const char *title = dl->entries[entry].title;
    char *path;

    if (!entry)
        return parent_path(dl->path);
    path = av_append_path_component(dl->path, title);
    if (path && entry < dl->nb_dirs)
        path[strlen(path) - 1] = 0;
    return path;
}
