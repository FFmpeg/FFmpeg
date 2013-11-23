/*
 * Copyright (c) 2006 Konstantin Shishkov
 * Copyright (c) 2007 Loren Merritt
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

/**
 * @file
 * huffman tree builder and VLC generator
 */

#include <stdint.h>

#include "avcodec.h"
#include "get_bits.h"
#include "huffman.h"

/* symbol for Huffman tree node */
#define HNODE -1

typedef struct {
    uint64_t val;
    int name;
} HeapElem;

static void heap_sift(HeapElem *h, int root, int size)
{
    while (root * 2 + 1 < size) {
        int child = root * 2 + 1;
        if (child < size - 1 && h[child].val > h[child+1].val)
            child++;
        if (h[root].val > h[child].val) {
            FFSWAP(HeapElem, h[root], h[child]);
            root = child;
        } else
            break;
    }
}

void ff_huff_gen_len_table(uint8_t *dst, const uint64_t *stats)
{
    HeapElem h[256];
    int up[2*256];
    int len[2*256];
    int offset, i, next;
    int size = 256;

    for (offset = 1; ; offset <<= 1) {
        for (i=0; i < size; i++) {
            h[i].name = i;
            h[i].val = (stats[i] << 8) + offset;
        }
        for (i = size / 2 - 1; i >= 0; i--)
            heap_sift(h, i, size);

        for (next = size; next < size * 2 - 1; next++) {
            // merge the two smallest entries, and put it back in the heap
            uint64_t min1v = h[0].val;
            up[h[0].name] = next;
            h[0].val = INT64_MAX;
            heap_sift(h, 0, size);
            up[h[0].name] = next;
            h[0].name = next;
            h[0].val += min1v;
            heap_sift(h, 0, size);
        }

        len[2 * size - 2] = 0;
        for (i = 2 * size - 3; i >= size; i--)
            len[i] = len[up[i]] + 1;
        for (i = 0; i < size; i++) {
            dst[i] = len[up[i]] + 1;
            if (dst[i] >= 32) break;
        }
        if (i==size) break;
    }
}

static void get_tree_codes(uint32_t *bits, int16_t *lens, uint8_t *xlat,
                           Node *nodes, int node,
                           uint32_t pfx, int pl, int *pos, int no_zero_count)
{
    int s;

    s = nodes[node].sym;
    if (s != HNODE || (no_zero_count && !nodes[node].count)) {
        bits[*pos] = pfx;
        lens[*pos] = pl;
        xlat[*pos] = s;
        (*pos)++;
    } else {
        pfx <<= 1;
        pl++;
        get_tree_codes(bits, lens, xlat, nodes, nodes[node].n0, pfx, pl,
                       pos, no_zero_count);
        pfx |= 1;
        get_tree_codes(bits, lens, xlat, nodes, nodes[node].n0 + 1, pfx, pl,
                       pos, no_zero_count);
    }
}

static int build_huff_tree(VLC *vlc, Node *nodes, int head, int flags)
{
    int no_zero_count = !(flags & FF_HUFFMAN_FLAG_ZERO_COUNT);
    uint32_t bits[256];
    int16_t lens[256];
    uint8_t xlat[256];
    int pos = 0;

    get_tree_codes(bits, lens, xlat, nodes, head, 0, 0,
                   &pos, no_zero_count);
    return ff_init_vlc_sparse(vlc, 9, pos, lens, 2, 2, bits, 4, 4, xlat, 1, 1, 0);
}


/**
 * nodes size must be 2*nb_codes
 * first nb_codes nodes.count must be set
 */
int ff_huff_build_tree(AVCodecContext *avctx, VLC *vlc, int nb_codes,
                       Node *nodes, HuffCmp cmp, int flags)
{
    int i, j;
    int cur_node;
    int64_t sum = 0;

    for (i = 0; i < nb_codes; i++) {
        nodes[i].sym = i;
        nodes[i].n0 = -2;
        sum += nodes[i].count;
    }

    if (sum >> 31) {
        av_log(avctx, AV_LOG_ERROR,
               "Too high symbol frequencies. "
               "Tree construction is not possible\n");
        return -1;
    }
    qsort(nodes, nb_codes, sizeof(Node), cmp);
    cur_node = nb_codes;
    nodes[nb_codes*2-1].count = 0;
    for (i = 0; i < nb_codes * 2 - 1; i += 2) {
        nodes[cur_node].sym = HNODE;
        nodes[cur_node].count = nodes[i].count + nodes[i + 1].count;
        nodes[cur_node].n0 = i;
        for (j = cur_node; j > 0; j--) {
            if (nodes[j].count > nodes[j - 1].count ||
                (nodes[j].count == nodes[j - 1].count &&
                 (!(flags & FF_HUFFMAN_FLAG_HNODE_FIRST) ||
                  nodes[j].n0 == j - 1 || nodes[j].n0 == j - 2 ||
                  (nodes[j].sym!=HNODE && nodes[j-1].sym!=HNODE))))
                break;
            FFSWAP(Node, nodes[j], nodes[j - 1]);
        }
        cur_node++;
    }
    if (build_huff_tree(vlc, nodes, nb_codes * 2 - 2, flags) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error building tree\n");
        return -1;
    }
    return 0;
}
