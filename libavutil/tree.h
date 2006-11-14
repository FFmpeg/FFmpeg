/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef TREE_H
#define TREE_H

struct AVTreeNode;
void *av_tree_find(const struct AVTreeNode *t, void *key, int (*cmp)(void *key, const void *b), void *next[2]);
void *av_tree_insert(struct AVTreeNode **tp, void *key, int (*cmp)(void *key, const void *b));
void av_tree_destroy(struct AVTreeNode *t);

#endif /* TREE_H */
