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

/**
 * @file tree.h
 * A tree container.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#ifndef TREE_H
#define TREE_H

struct AVTreeNode;

/**
 * Finds an element.
 * @param root a pointer to the root node of the tree
 * @param next If next is not NULL then next[0] will contain the previous
 *             element and next[1] the next element if either does not exist
 *             then the corresponding entry in next is unchanged.
 * @return An element with cmp(key, elem)==0 or NULL if no such element exists in
 *         the tree.
 */
void *av_tree_find(const struct AVTreeNode *root, void *key, int (*cmp)(void *key, const void *b), void *next[2]);

/**
 * Finds a element for which cmp(key, elem)==0, if no such element is found key
 * is inserted into the tree.
 * @param rootp A pointer to a pointer to the root node of the tree. Note that
 *              the root node can change during insertions, this is required
 *              to keep the tree balanced.
 *
 * @return If no insertion happened, the found element.
 *         If an insertion happened, then either key or NULL will be returned.
 *         Which one it is depends on the tree state and the implementation. You
 *         should make no assumptions that it's one or the other in the code.
 */
void *av_tree_insert(struct AVTreeNode **rootp, void *key, int (*cmp)(void *key, const void *b));
void av_tree_destroy(struct AVTreeNode *t);

#endif /* TREE_H */
