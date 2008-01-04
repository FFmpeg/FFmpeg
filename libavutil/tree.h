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
 * Insertion, Removial, Finding equal, largest which is smaller than and
 * smallest which is larger than all have O(log n) worst case time.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#ifndef FFMPEG_TREE_H
#define FFMPEG_TREE_H

struct AVTreeNode;
extern const int av_tree_node_size;

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
 * Inserts or removes an element.
 * If *next is NULL then the element supplied will be removed, if no such
 * element exists behavior is undefined.
 * If *next is not NULL then the element supplied will be inserted, unless
 * it already exists in the tree.
 * @param rootp A pointer to a pointer to the root node of the tree. Note that
 *              the root node can change during insertions, this is required
 *              to keep the tree balanced.
 * @param next Used to allocate and free AVTreeNodes. For insertion the user
 *             must set it to an allocated and zeroed object of at least
 *             av_tree_node_size bytes size. av_tree_insert() will set it to
 *             NULL if it has been consumed.
 *             For deleting elements *next is set to NULL by the user and
 *             av_tree_node_size() will set it to the AVTreeNode which was
 *             used for the removed element.
 *             This allows the use of flat arrays, which have
 *             lower overhead compared to many malloced elements.
 *             You might want to define a function like:
 *             void *tree_insert(struct AVTreeNode **rootp, void *key, int (*cmp)(void *key, const void *b), AVTreeNode **next){
 *                 if(!*next) *next= av_mallocz(av_tree_node_size);
 *                 return av_tree_insert(rootp, key, cmp, next);
 *             }
 *             void *tree_remove(struct AVTreeNode **rootp, void *key, int (*cmp)(void *key, const void *b, AVTreeNode **next)){
 *                 if(*next) av_freep(next);
 *                 return av_tree_insert(rootp, key, cmp, next);
 *             }
 *
 * @return If no insertion happened, the found element.
 *         If an insertion or removial happened, then either key or NULL will be returned.
 *         Which one it is depends on the tree state and the implementation. You
 *         should make no assumptions that it's one or the other in the code.
 */
void *av_tree_insert(struct AVTreeNode **rootp, void *key, int (*cmp)(void *key, const void *b), struct AVTreeNode **next);
void av_tree_destroy(struct AVTreeNode *t);

#endif /* FFMPEG_TREE_H */
