# Copyright (c) 2019 Guo Yejun
#
# This file is part of FFmpeg.
#
# FFmpeg is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# FFmpeg is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with FFmpeg; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
# ==============================================================================

import tensorflow as tf
import numpy as np
import sys, struct

__all__ = ['convert_from_tensorflow']

# as the first step to be compatible with vf_sr, it is not general.
# it will be refined step by step.

class TFConverter:
    def __init__(self, graph_def, nodes, outfile):
        self.graph_def = graph_def
        self.nodes = nodes
        self.outfile = outfile
        self.layer_number = 0
        self.output_names = []
        self.name_node_dict = {}
        self.edges = {}
        self.conv_activations = {'Relu':0, 'Tanh':1, 'Sigmoid':2, 'LeakyRelu':4}
        self.conv_paddings = {'VALID':2, 'SAME':1}
        self.converted_nodes = set()
        self.op2code = {'Conv2D':1, 'DepthToSpace':2}


    def dump_for_tensorboard(self):
        graph = tf.get_default_graph()
        tf.import_graph_def(self.graph_def, name="")
        # tensorboard --logdir=/tmp/graph
        tf.summary.FileWriter('/tmp/graph', graph)


    def get_conv2d_params(self, node):
        knode = self.name_node_dict[node.input[1]]
        bnode = None
        activation = 'None'
        next = self.edges[node.name][0]
        if next.op == 'BiasAdd':
            self.converted_nodes.add(next.name)
            bnode = self.name_node_dict[next.input[1]]
            next = self.edges[next.name][0]
        if next.op in self.conv_activations:
            self.converted_nodes.add(next.name)
            activation = next.op
        return knode, bnode, activation


    def dump_conv2d_to_file(self, node, f):
        assert(node.op == 'Conv2D')
        self.layer_number = self.layer_number + 1
        self.converted_nodes.add(node.name)
        knode, bnode, activation = self.get_conv2d_params(node)

        dilation = node.attr['dilations'].list.i[0]
        padding = node.attr['padding'].s
        padding = self.conv_paddings[padding.decode("utf-8")]

        ktensor = knode.attr['value'].tensor
        filter_height = ktensor.tensor_shape.dim[0].size
        filter_width = ktensor.tensor_shape.dim[1].size
        in_channels = ktensor.tensor_shape.dim[2].size
        out_channels = ktensor.tensor_shape.dim[3].size
        kernel = np.frombuffer(ktensor.tensor_content, dtype=np.float32)
        kernel = kernel.reshape(filter_height, filter_width, in_channels, out_channels)
        kernel = np.transpose(kernel, [3, 0, 1, 2])

        np.array([self.op2code[node.op], dilation, padding, self.conv_activations[activation], in_channels, out_channels, filter_height], dtype=np.uint32).tofile(f)
        kernel.tofile(f)

        btensor = bnode.attr['value'].tensor
        if btensor.tensor_shape.dim[0].size == 1:
            bias = struct.pack("f", btensor.float_val[0])
        else:
            bias = btensor.tensor_content
        f.write(bias)


    def dump_depth2space_to_file(self, node, f):
        assert(node.op == 'DepthToSpace')
        self.layer_number = self.layer_number + 1
        block_size = node.attr['block_size'].i
        np.array([self.op2code[node.op], block_size], dtype=np.uint32).tofile(f)
        self.converted_nodes.add(node.name)


    def generate_layer_number(self):
        # in current hard code implementation, the layer number is the first data written to the native model file
        # it is not easy to know it at the beginning time in the general converter, so first do a dry run for compatibility
        # will be refined later.
        with open('/tmp/tmp.model', 'wb') as f:
            self.dump_layers_to_file(f)
        self.converted_nodes.clear()


    def dump_layers_to_file(self, f):
        for node in self.nodes:
            if node.name in self.converted_nodes:
                continue
            if node.op == 'Conv2D':
                self.dump_conv2d_to_file(node, f)
            elif node.op == 'DepthToSpace':
                self.dump_depth2space_to_file(node, f)


    def dump_to_file(self):
        self.generate_layer_number()
        with open(self.outfile, 'wb') as f:
            np.array([self.layer_number], dtype=np.uint32).tofile(f)
            self.dump_layers_to_file(f)


    def generate_name_node_dict(self):
        for node in self.nodes:
            self.name_node_dict[node.name] = node


    def generate_output_names(self):
        used_names = []
        for node in self.nodes:
            for input in node.input:
                used_names.append(input)

        for node in self.nodes:
            if node.name not in used_names:
                self.output_names.append(node.name)


    def remove_identity(self):
        id_nodes = []
        id_dict = {}
        for node in self.nodes:
            if node.op == 'Identity':
                name = node.name
                input = node.input[0]
                id_nodes.append(node)
                # do not change the output name
                if name in self.output_names:
                    self.name_node_dict[input].name = name
                    self.name_node_dict[name] = self.name_node_dict[input]
                    del self.name_node_dict[input]
                else:
                    id_dict[name] = input

        for idnode in id_nodes:
            self.nodes.remove(idnode)

        for node in self.nodes:
            for i in range(len(node.input)):
                input = node.input[i]
                if input in id_dict:
                    node.input[i] = id_dict[input]


    def generate_edges(self):
        for node in self.nodes:
            for input in node.input:
                if input in self.edges:
                    self.edges[input].append(node)
                else:
                    self.edges[input] = [node]


    def run(self):
        self.generate_name_node_dict()
        self.generate_output_names()
        self.remove_identity()
        self.generate_edges()

        #check the graph with tensorboard with human eyes
        #self.dump_for_tensorboard()

        self.dump_to_file()


def convert_from_tensorflow(infile, outfile):
    with open(infile, 'rb') as f:
        # read the file in .proto format
        graph_def = tf.GraphDef()
        graph_def.ParseFromString(f.read())
        nodes = graph_def.node

    converter = TFConverter(graph_def, nodes, outfile)
    converter.run()
