/*
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

#include "libavutil/tree.c"

#include <stdint.h>

#include "libavutil/common.h"
#include "libavutil/lfg.h"
#include "libavutil/log.h"

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
