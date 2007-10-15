/**
 * @file huffman.c
 * huffman tree builder and VLC generator
 * Copyright (c) 2006 Konstantin Shishkov
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

#include "avcodec.h"
#include "bitstream.h"
#include "huffman.h"

/* symbol for Huffman tree node */
#define HNODE -1


static void get_tree_codes(uint32_t *bits, int16_t *lens, uint8_t *xlat, Node *nodes, int node, uint32_t pfx, int pl, int *pos)
{
    int s;

    s = nodes[node].sym;
    if(s != HNODE || !nodes[node].count){
        bits[*pos] = pfx;
        lens[*pos] = pl;
        xlat[*pos] = s;
        (*pos)++;
    }else{
        pfx <<= 1;
        pl++;
        get_tree_codes(bits, lens, xlat, nodes, nodes[node].n0, pfx, pl, pos);
        pfx |= 1;
        get_tree_codes(bits, lens, xlat, nodes, nodes[node].n0+1, pfx, pl, pos);
    }
}

static int build_huff_tree(VLC *vlc, Node *nodes, int head)
{
    uint32_t bits[256];
    int16_t lens[256];
    uint8_t xlat[256];
    int pos = 0;

    get_tree_codes(bits, lens, xlat, nodes, head, 0, 0, &pos);
    return init_vlc_sparse(vlc, 9, pos, lens, 2, 2, bits, 4, 4, xlat, 1, 1, 0);
}


/**
 * nodes size must be 2*nb_codes
 * first nb_codes nodes.count must be set
 */
int ff_huff_build_tree(AVCodecContext *avctx, VLC *vlc, int nb_codes,
                       Node *nodes, huff_cmp_t cmp, int hnode_first)
{
    int i, j;
    int cur_node;
    int64_t sum = 0;

    for(i = 0; i < nb_codes; i++){
        nodes[i].sym = i;
        nodes[i].n0 = -2;
        sum += nodes[i].count;
    }

    if(sum >> 31) {
        av_log(avctx, AV_LOG_ERROR, "Too high symbol frequencies. Tree construction is not possible\n");
        return -1;
    }
    qsort(nodes, nb_codes, sizeof(Node), cmp);
    cur_node = nb_codes;
    for(i = 0; i < nb_codes*2-1; i += 2){
        nodes[cur_node].sym = HNODE;
        nodes[cur_node].count = nodes[i].count + nodes[i+1].count;
        nodes[cur_node].n0 = i;
        for(j = cur_node; j > 0; j--){
            if(nodes[j].count > nodes[j-1].count ||
               (nodes[j].count == nodes[j-1].count &&
                (!hnode_first || nodes[j].n0==j-1 || nodes[j].n0==j-2 ||
                 (nodes[j].sym!=HNODE && nodes[j-1].sym!=HNODE))))
                break;
            FFSWAP(Node, nodes[j], nodes[j-1]);
        }
        cur_node++;
    }
    if(build_huff_tree(vlc, nodes, nb_codes*2-2) < 0){
        av_log(avctx, AV_LOG_ERROR, "Error building tree\n");
        return -1;
    }
    return 0;
}
