/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#include "error.h"
#include "log.h"
#include "mem.h"
#include "tree.h"

typedef struct AVTreeNode {
    struct AVTreeNode *child[2];
    void *elem;
    int state;
} AVTreeNode;

#if FF_API_CONTEXT_SIZE
const int av_tree_node_size = sizeof(AVTreeNode);
#endif

struct AVTreeNode *av_tree_node_alloc(void)
{
    return av_mallocz(sizeof(struct AVTreeNode));
}

void *av_tree_find(const AVTreeNode *t, void *key,
                   int (*cmp)(void *key, const void *b), void *next[2])
{
    if (t) {
        unsigned int v = cmp(key, t->elem);
        if (v) {
            if (next)
                next[v >> 31] = t->elem;
            return av_tree_find(t->child[(v >> 31) ^ 1], key, cmp, next);
        } else {
            if (next) {
                av_tree_find(t->child[0], key, cmp, next);
                av_tree_find(t->child[1], key, cmp, next);
            }
            return t->elem;
        }
    }
    return NULL;
}

void *av_tree_insert(AVTreeNode **tp, void *key,
                     int (*cmp)(void *key, const void *b), AVTreeNode **next)
{
    AVTreeNode *t = *tp;
    if (t) {
        unsigned int v = cmp(t->elem, key);
        void *ret;
        if (!v) {
            if (*next)
                return t->elem;
            else if (t->child[0] || t->child[1]) {
                int i = !t->child[0];
                void *next_elem[2];
                av_tree_find(t->child[i], key, cmp, next_elem);
                key = t->elem = next_elem[i];
                v   = -i;
            } else {
                *next = t;
                *tp   = NULL;
                return NULL;
            }
        }
        ret = av_tree_insert(&t->child[v >> 31], key, cmp, next);
        if (!ret) {
            int i              = (v >> 31) ^ !!*next;
            AVTreeNode **child = &t->child[i];
            t->state += 2 * i - 1;

            if (!(t->state & 1)) {
                if (t->state) {
                    /* The following code is equivalent to
                     * if ((*child)->state * 2 == -t->state)
                     *     rotate(child, i ^ 1);
                     * rotate(tp, i);
                     *
                     * with rotate():
                     * static void rotate(AVTreeNode **tp, int i)
                     * {
                     *     AVTreeNode *t= *tp;
                     *
                     *     *tp = t->child[i];
                     *     t->child[i] = t->child[i]->child[i ^ 1];
                     *     (*tp)->child[i ^ 1] = t;
                     *     i = 4 * t->state + 2 * (*tp)->state + 12;
                     *     t->state     = ((0x614586 >> i) & 3) - 1;
                     *     (*tp)->state = ((0x400EEA >> i) & 3) - 1 +
                     *                    ((*tp)->state >> 1);
                     * }
                     * but such a rotate function is both bigger and slower
                     */
                    if ((*child)->state * 2 == -t->state) {
                        *tp                    = (*child)->child[i ^ 1];
                        (*child)->child[i ^ 1] = (*tp)->child[i];
                        (*tp)->child[i]        = *child;
                        *child                 = (*tp)->child[i ^ 1];
                        (*tp)->child[i ^ 1]    = t;

                        (*tp)->child[0]->state = -((*tp)->state > 0);
                        (*tp)->child[1]->state = (*tp)->state < 0;
                        (*tp)->state           = 0;
                    } else {
                        *tp                 = *child;
                        *child              = (*child)->child[i ^ 1];
                        (*tp)->child[i ^ 1] = t;
                        if ((*tp)->state)
                            t->state = 0;
                        else
                            t->state >>= 1;
                        (*tp)->state = -t->state;
                    }
                }
            }
            if (!(*tp)->state ^ !!*next)
                return key;
        }
        return ret;
    } else {
        *tp   = *next;
        *next = NULL;
        if (*tp) {
            (*tp)->elem = key;
            return NULL;
        } else
            return key;
    }
}

void av_tree_destroy(AVTreeNode *t)
{
    if (t) {
        av_tree_destroy(t->child[0]);
        av_tree_destroy(t->child[1]);
        av_free(t);
    }
}

void av_tree_enumerate(AVTreeNode *t, void *opaque,
                       int (*cmp)(void *opaque, void *elem),
                       int (*enu)(void *opaque, void *elem))
{
    if (t) {
        int v = cmp ? cmp(opaque, t->elem) : 0;
        if (v >= 0)
            av_tree_enumerate(t->child[0], opaque, cmp, enu);
        if (v == 0)
            enu(opaque, t->elem);
        if (v <= 0)
            av_tree_enumerate(t->child[1], opaque, cmp, enu);
    }
}

#ifdef TEST

#include "common.h"
#include "lfg.h"

static int check(AVTreeNode *t)
{
    if (t) {
        int left  = check(t->child[0]);
        int right = check(t->child[1]);

        if (left > 999 || right > 999)
            return 1000;
        if (right - left != t->state)
            return 1000;
        if (t->state > 1 || t->state < -1)
            return 1000;
        return FFMAX(left, right) + 1;
    }
    return 0;
}

static void print(AVTreeNode *t, int depth)
{
    int i;
    for (i = 0; i < depth * 4; i++)
        av_log(NULL, AV_LOG_ERROR, " ");
    if (t) {
        av_log(NULL, AV_LOG_ERROR, "Node %p %2d %p\n", t, t->state, t->elem);
        print(t->child[0], depth + 1);
        print(t->child[1], depth + 1);
    } else
        av_log(NULL, AV_LOG_ERROR, "NULL\n");
}

static int cmp(void *a, const void *b)
{
    return (uint8_t *) a - (const uint8_t *) b;
}

int main(void)
{
    int i;
    AVTreeNode *root = NULL, *node = NULL;
    AVLFG prng;

    av_lfg_init(&prng, 1);

    for (i = 0; i < 10000; i++) {
        AVTreeNode *node2 = NULL;
        intptr_t j = av_lfg_get(&prng) % 86294;
        void *ret, *jj = (void *)(j + 1);

        while (ret = av_tree_find(root, jj, cmp, NULL)) {
            j  = av_lfg_get(&prng) % 86294;
            jj = (void *)(j + 1);
        }

        if (check(root) > 999) {
            av_log(NULL, AV_LOG_ERROR, "FATAL error %d\n", i);
            print(root, 0);
            return 1;
        }

        if (!node)
            node = av_tree_node_alloc();
        if (!node) {
            av_log(NULL, AV_LOG_ERROR, "Memory allocation failure.\n");
            return 1;
        }
        av_tree_insert(&root, jj, cmp, &node);

        while (ret = av_tree_find(root, jj, cmp, NULL)) {
            j  = av_lfg_get(&prng) % 86294;
            jj = (void *)(j + 1);
        }

        ret = av_tree_insert(&root, jj, cmp, &node2);
        if (ret != jj)
            av_tree_destroy(node2);
        ret = av_tree_find(root, jj, cmp, NULL);
        if (ret)
            av_log(NULL, AV_LOG_ERROR, "removal failure %d\n", i);
    }

    av_tree_destroy(root);

    return 0;
}
#endif
