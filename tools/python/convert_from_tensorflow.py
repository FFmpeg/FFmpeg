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

class TFConverter:
    def __init__(self, graph_def, nodes, outfile, dump4tb):
        self.graph_def = graph_def
        self.nodes = nodes
        self.outfile = outfile
        self.dump4tb = dump4tb
        self.layer_number = 0
        self.output_names = []
        self.name_node_dict = {}
        self.edges = {}
        self.conv_activations = {'Relu':0, 'Tanh':1, 'Sigmoid':2, 'None':3, 'LeakyRelu':4}
        self.conv_paddings = {'VALID':0, 'SAME':1}
        self.converted_nodes = set()
        self.conv2d_scope_names = set()
        self.op2code = {'Conv2D':1, 'DepthToSpace':2, 'MirrorPad':3}
        self.mirrorpad_mode = {'CONSTANT':0, 'REFLECT':1, 'SYMMETRIC':2}


    def dump_for_tensorboard(self):
        graph = tf.get_default_graph()
        tf.import_graph_def(self.graph_def, name="")
        tf.summary.FileWriter('/tmp/graph', graph)
        print('graph saved, run "tensorboard --logdir=/tmp/graph" to see it')


    def get_conv2d_params(self, conv2d_scope_name):
        knode = self.name_node_dict[conv2d_scope_name + '/kernel']
        bnode = self.name_node_dict[conv2d_scope_name + '/bias']

        if conv2d_scope_name + '/dilation_rate' in self.name_node_dict:
            dnode = self.name_node_dict[conv2d_scope_name + '/dilation_rate']
        else:
            dnode = None

        # the BiasAdd name is possible be changed into the output name,
        # if activation is None, and BiasAdd.next is the last op which is Identity
        if conv2d_scope_name + '/BiasAdd' in self.edges:
            activation = self.edges[conv2d_scope_name + '/BiasAdd'][0]
            activation = activation.op
        else:
            activation = 'None'
        return knode, bnode, dnode, activation


    def dump_conv2d_to_file(self, node, f):
        assert(node.op == 'Conv2D')
        self.layer_number = self.layer_number + 1
        self.converted_nodes.add(node.name)

        scope_name = TFConverter.get_scope_name(node.name)
        #knode for kernel, bnode for bias, dnode for dilation
        knode, bnode, dnode, activation = self.get_conv2d_params(scope_name)

        if dnode is not None:
            dilation = struct.unpack('i', dnode.attr['value'].tensor.tensor_content[0:4])[0]
        else:
            dilation = 1

        padding = node.attr['padding'].s.decode("utf-8")
        # conv2d with dilation > 1 generates tens of nodes, not easy to parse them, so use tricky.
        if dilation > 1 and scope_name + '/stack' in self.name_node_dict:
            if self.name_node_dict[scope_name + '/stack'].op == "Const":
                padding = 'SAME'
        padding = self.conv_paddings[padding]

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


    def dump_mirrorpad_to_file(self, node, f):
        assert(node.op == 'MirrorPad')
        self.layer_number = self.layer_number + 1
        mode = node.attr['mode'].s
        mode = self.mirrorpad_mode[mode.decode("utf-8")]
        np.array([self.op2code[node.op], mode], dtype=np.uint32).tofile(f)
        pnode = self.name_node_dict[node.input[1]]
        self.converted_nodes.add(pnode.name)
        paddings = pnode.attr['value'].tensor.tensor_content
        f.write(paddings)
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

            # conv2d with dilation generates very complex nodes, so handle it in special
            scope_name = TFConverter.get_scope_name(node.name)
            if scope_name in self.conv2d_scope_names:
                if node.op == 'Conv2D':
                    self.dump_conv2d_to_file(node, f)
                continue

            if node.op == 'DepthToSpace':
                self.dump_depth2space_to_file(node, f)
            elif node.op == 'MirrorPad':
                self.dump_mirrorpad_to_file(node, f)


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


    @staticmethod
    def get_scope_name(name):
        index = name.rfind('/')
        if index == -1:
            return ""
        return name[0:index]


    def generate_conv2d_scope_names(self):
        for node in self.nodes:
            if node.op == 'Conv2D':
                scope = TFConverter.get_scope_name(node.name)
                self.conv2d_scope_names.add(scope)


    def run(self):
        self.generate_name_node_dict()
        self.generate_output_names()
        self.remove_identity()
        self.generate_edges()
        self.generate_conv2d_scope_names()

        if self.dump4tb:
            self.dump_for_tensorboard()

        self.dump_to_file()


def convert_from_tensorflow(infile, outfile, dump4tb):
    with open(infile, 'rb') as f:
        # read the file in .proto format
        graph_def = tf.GraphDef()
        graph_def.ParseFromString(f.read())
        nodes = graph_def.node

    converter = TFConverter(graph_def, nodes, outfile, dump4tb)
    converter.run()
