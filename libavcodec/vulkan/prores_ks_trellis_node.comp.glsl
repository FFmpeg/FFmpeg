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

#version 460
#pragma shader_stage(compute)

#extension GL_EXT_control_flow_attributes : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

layout (local_size_x_id = 253, local_size_y_id = 254, local_size_z_id = 255) in;

layout (constant_id = 0) const int slices_per_row = 1;
layout (constant_id = 2) const int num_planes = 0;
layout (constant_id = 3) const int force_quant = 0;
layout (constant_id = 4) const int min_quant = 0;
layout (constant_id = 5) const int max_quant = 0;
layout (constant_id = 6) const int mbs_per_slice = 0;
layout (constant_id = 7) const int bits_per_mb = 0;

struct SliceScore {
    ivec4 bits[16];
    ivec4 score[16];
    int total_bits[16];
    int total_score[16];
    int overquant;
    int quant;
};

layout (set = 0, binding = 0, scalar) buffer SliceScores {
    SliceScore scores[];
};

#define TRELLIS_WIDTH 16
#define SCORE_LIMIT   1073741823

struct TrellisNode {
    int prev_node;
    int quant;
    int bits;
    int score;
};

TrellisNode nodes[(slices_per_row + 1) * TRELLIS_WIDTH];

int find_slice_quant(int slice_x)
{
    int slice = int(gl_LocalInvocationID.x) * slices_per_row + slice_x;

    int trellis_node = int(slice_x + 1) * TRELLIS_WIDTH;
    [[unroll]] for (int q = min_quant; q < max_quant + 2; q++) {
        nodes[trellis_node + q].prev_node = -1;
        nodes[trellis_node + q].quant = q;
    }

    int mbs = int(slice_x + 1) * mbs_per_slice;
    nodes[trellis_node + max_quant + 1].quant = scores[slice].overquant;

    int bits_limit = mbs * bits_per_mb;
    for (int pq = min_quant; pq < max_quant + 2; pq++) {
        int prev = trellis_node - TRELLIS_WIDTH + pq;
        for (int q = min_quant; q < max_quant + 2; q++) {
            int cur = trellis_node + q;
            int bits = nodes[prev].bits + scores[slice].total_bits[q];
            int error = scores[slice].total_score[q];
            if (bits > bits_limit)
                error = SCORE_LIMIT;

            int new_score;
            if (nodes[prev].score < SCORE_LIMIT && error < SCORE_LIMIT)
                new_score = nodes[prev].score + error;
            else
                new_score = SCORE_LIMIT;
            if (nodes[cur].prev_node == -1 || nodes[cur].score >= new_score) {
                nodes[cur].bits      = bits;
                nodes[cur].score     = new_score;
                nodes[cur].prev_node = prev;
            }
        }
    }

    int error = nodes[trellis_node + min_quant].score;
    int pq = trellis_node + min_quant;
    for (int q = min_quant + 1; q < max_quant + 2; q++) {
        if (nodes[trellis_node + q].score <= error) {
            error = nodes[trellis_node + q].score;
            pq = trellis_node + q;
        }
    }

    return pq;
}

void find_slice_row_quants()
{
    for (int i = min_quant; i < max_quant + 2; i++) {
        nodes[i].prev_node = -1;
        nodes[i].bits = 0;
        nodes[i].score = 0;
    }

    int q = 0;
    for (int slice_x = 0; slice_x < slices_per_row; ++slice_x) {
        q = find_slice_quant(slice_x);
    }

    int y = int(gl_LocalInvocationID.x);
    for (int x = slices_per_row - 1; x >= 0; x--) {
        int slice = x + y * slices_per_row;
        scores[slice].quant = nodes[q].quant;
        q = nodes[q].prev_node;
    }
}

void main()
{
    if (force_quant == 0) {
        find_slice_row_quants();
    } else {
        int y = int(gl_LocalInvocationID.x);
        for (int x = 0; x < slices_per_row; x++)
            scores[x + y * slices_per_row].quant = force_quant;
    }
}
