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
import convert_header as header

__all__ = ['convert_from_tensorflow']

class Operand(object):
    IOTYPE_INPUT = 1
    IOTYPE_OUTPUT = 2
    IOTYPE_INTERMEDIATE = IOTYPE_INPUT | IOTYPE_OUTPUT
    DTYPE_FLOAT = 1
    DTYPE_UINT8 = 4
    index = 0
    def __init__(self, name, dtype, dims):
        self.name = name
        self.dtype = dtype
        self.dims = dims
        self.iotype = 0
        self.used_count = 0
        self.index = Operand.index
        Operand.index = Operand.index + 1
        self.iotype2str = {Operand.IOTYPE_INPUT: 'in', Operand.IOTYPE_OUTPUT: 'out', Operand.IOTYPE_INTERMEDIATE: 'inout'}
        self.dtype2str = {Operand.DTYPE_FLOAT: 'DT_FLOAT', Operand.DTYPE_UINT8: 'DT_UINT8'}

    def add_iotype(self, iotype):
        self.iotype = self.iotype | iotype
        if iotype == Operand.IOTYPE_INPUT:
            self.used_count = self.used_count + 1

    def __str__(self):
        return "{}: (name: {}, iotype: {}, dtype: {}, dims: ({},{},{},{}) used_count: {})".format(self.index,
                            self.name, self.iotype2str[self.iotype], self.dtype2str[self.dtype],
                            self.dims[0], self.dims[1], self.dims[2], self.dims[3], self.used_count)

    def __lt__(self, other):
        return self.index < other.index

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
        self.conv2d_scopename_inputname_dict = {}
        self.op2code = {'Conv2D':1, 'DepthToSpace':2, 'MirrorPad':3, 'Maximum':4, 'MathBinary':5, 'MathUnary':6}
        self.mathbin2code = {'Sub':0, 'Add':1, 'Mul':2, 'RealDiv':3, 'Minimum':4}
        self.mathun2code  = {'Abs':0, 'Sin':1, 'Cos':2, 'Tan':3}
        self.mirrorpad_mode = {'CONSTANT':0, 'REFLECT':1, 'SYMMETRIC':2}
        self.name_operand_dict = {}


    def add_operand(self, name, type):
        node = self.name_node_dict[name]
        if name not in self.name_operand_dict:
            dtype = node.attr['dtype'].type
            if dtype == 0:
                dtype = node.attr['T'].type
            dims = [-1,-1,-1,-1]
            if 'shape' in node.attr:
                dims[0] = node.attr['shape'].shape.dim[0].size
                dims[1] = node.attr['shape'].shape.dim[1].size
                dims[2] = node.attr['shape'].shape.dim[2].size
                dims[3] = node.attr['shape'].shape.dim[3].size
            operand = Operand(name, dtype, dims)
            self.name_operand_dict[name] = operand;
        self.name_operand_dict[name].add_iotype(type)
        return self.name_operand_dict[name].index


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
            anode = self.edges[conv2d_scope_name + '/BiasAdd'][0]
            if anode.op not in self.conv_activations:
                anode = None
        else:
            anode = None
        return knode, bnode, dnode, anode


    def dump_complex_conv2d_to_file(self, node, f):
        assert(node.op == 'Conv2D')
        self.layer_number = self.layer_number + 1
        self.converted_nodes.add(node.name)

        scope_name = TFConverter.get_scope_name(node.name)
        #knode for kernel, bnode for bias, dnode for dilation, anode for activation
        knode, bnode, dnode, anode = self.get_conv2d_params(scope_name)

        if dnode is not None:
            dilation = struct.unpack('i', dnode.attr['value'].tensor.tensor_content[0:4])[0]
        else:
            dilation = 1

        if anode is not None:
            activation = anode.op
        else:
            activation = 'None'

        padding = node.attr['padding'].s.decode("utf-8")
        # conv2d with dilation > 1 generates tens of nodes, not easy to parse them, so use this tricky method.
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

        has_bias = 1
        np.array([self.op2code[node.op], dilation, padding, self.conv_activations[activation], in_channels, out_channels, filter_height, has_bias], dtype=np.uint32).tofile(f)
        kernel.tofile(f)

        btensor = bnode.attr['value'].tensor
        if btensor.tensor_shape.dim[0].size == 1:
            bias = struct.pack("f", btensor.float_val[0])
        else:
            bias = btensor.tensor_content
        f.write(bias)

        input_name = self.conv2d_scopename_inputname_dict[scope_name]
        input_operand_index = self.add_operand(input_name, Operand.IOTYPE_INPUT)

        if anode is not None:
            output_operand_index = self.add_operand(anode.name, Operand.IOTYPE_OUTPUT)
        else:
            output_operand_index = self.add_operand(self.edges[bnode.name][0].name, Operand.IOTYPE_OUTPUT)
        np.array([input_operand_index, output_operand_index], dtype=np.uint32).tofile(f)


    def dump_simple_conv2d_to_file(self, node, f):
        assert(node.op == 'Conv2D')
        self.layer_number = self.layer_number + 1
        self.converted_nodes.add(node.name)

        node0 = self.name_node_dict[node.input[0]]
        node1 = self.name_node_dict[node.input[1]]
        if node0.op == 'Const':
            knode = node0
            input_name = node.input[1]
        else:
            knode = node1
            input_name = node.input[0]

        ktensor = knode.attr['value'].tensor
        filter_height = ktensor.tensor_shape.dim[0].size
        filter_width = ktensor.tensor_shape.dim[1].size
        in_channels = ktensor.tensor_shape.dim[2].size
        out_channels = ktensor.tensor_shape.dim[3].size
        if filter_height * filter_width * in_channels * out_channels == 1:
            kernel = np.float32(ktensor.float_val[0])
        else:
            kernel = np.frombuffer(ktensor.tensor_content, dtype=np.float32)
        kernel = kernel.reshape(filter_height, filter_width, in_channels, out_channels)
        kernel = np.transpose(kernel, [3, 0, 1, 2])

        has_bias = 0
        dilation = 1
        padding = node.attr['padding'].s.decode("utf-8")
        np.array([self.op2code[node.op], dilation, self.conv_paddings[padding], self.conv_activations['None'],
                  in_channels, out_channels, filter_height, has_bias], dtype=np.uint32).tofile(f)
        kernel.tofile(f)

        input_operand_index = self.add_operand(input_name, Operand.IOTYPE_INPUT)
        output_operand_index = self.add_operand(node.name, Operand.IOTYPE_OUTPUT)
        np.array([input_operand_index, output_operand_index], dtype=np.uint32).tofile(f)


    def dump_depth2space_to_file(self, node, f):
        assert(node.op == 'DepthToSpace')
        self.layer_number = self.layer_number + 1
        block_size = node.attr['block_size'].i
        np.array([self.op2code[node.op], block_size], dtype=np.uint32).tofile(f)
        self.converted_nodes.add(node.name)
        input_operand_index = self.add_operand(node.input[0], Operand.IOTYPE_INPUT)
        output_operand_index = self.add_operand(node.name, Operand.IOTYPE_OUTPUT)
        np.array([input_operand_index, output_operand_index], dtype=np.uint32).tofile(f)


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
        input_operand_index = self.add_operand(node.input[0], Operand.IOTYPE_INPUT)
        output_operand_index = self.add_operand(node.name, Operand.IOTYPE_OUTPUT)
        np.array([input_operand_index, output_operand_index], dtype=np.uint32).tofile(f)


    def dump_maximum_to_file(self, node, f):
        assert(node.op == 'Maximum')
        self.layer_number = self.layer_number + 1
        ynode = self.name_node_dict[node.input[1]]
        y = ynode.attr['value'].tensor.float_val[0]
        np.array([self.op2code[node.op]], dtype=np.uint32).tofile(f)
        np.array([y], dtype=np.float32).tofile(f)
        self.converted_nodes.add(node.name)
        input_operand_index = self.add_operand(node.input[0], Operand.IOTYPE_INPUT)
        output_operand_index = self.add_operand(node.name, Operand.IOTYPE_OUTPUT)
        np.array([input_operand_index, output_operand_index], dtype=np.uint32).tofile(f)


    def dump_mathbinary_to_file(self, node, f):
        self.layer_number = self.layer_number + 1
        self.converted_nodes.add(node.name)
        i0_node = self.name_node_dict[node.input[0]]
        i1_node = self.name_node_dict[node.input[1]]
        np.array([self.op2code['MathBinary'], self.mathbin2code[node.op]], dtype=np.uint32).tofile(f)
        if i0_node.op == 'Const':
            scalar = i0_node.attr['value'].tensor.float_val[0]
            np.array([1], dtype=np.uint32).tofile(f)            # broadcast: 1
            np.array([scalar], dtype=np.float32).tofile(f)
            np.array([0], dtype=np.uint32).tofile(f)            # broadcast: 0
            input_operand_index = self.add_operand(i1_node.name, Operand.IOTYPE_INPUT)
            np.array([input_operand_index], dtype=np.uint32).tofile(f)
        elif i1_node.op == 'Const':
            scalar = i1_node.attr['value'].tensor.float_val[0]
            np.array([0], dtype=np.uint32).tofile(f)
            input_operand_index = self.add_operand(i0_node.name, Operand.IOTYPE_INPUT)
            np.array([input_operand_index], dtype=np.uint32).tofile(f)
            np.array([1], dtype=np.uint32).tofile(f)
            np.array([scalar], dtype=np.float32).tofile(f)
        else:
            np.array([0], dtype=np.uint32).tofile(f)
            input_operand_index = self.add_operand(i0_node.name, Operand.IOTYPE_INPUT)
            np.array([input_operand_index], dtype=np.uint32).tofile(f)
            np.array([0], dtype=np.uint32).tofile(f)
            input_operand_index = self.add_operand(i1_node.name, Operand.IOTYPE_INPUT)
            np.array([input_operand_index], dtype=np.uint32).tofile(f)
        output_operand_index = self.add_operand(node.name, Operand.IOTYPE_OUTPUT)
        np.array([output_operand_index], dtype=np.uint32).tofile(f)


    def dump_mathunary_to_file(self, node, f):
        self.layer_number = self.layer_number + 1
        self.converted_nodes.add(node.name)
        i0_node = self.name_node_dict[node.input[0]]
        np.array([self.op2code['MathUnary'], self.mathun2code[node.op]], dtype=np.uint32).tofile(f)
        input_operand_index = self.add_operand(i0_node.name, Operand.IOTYPE_INPUT)
        np.array([input_operand_index], dtype=np.uint32).tofile(f)
        output_operand_index = self.add_operand(node.name, Operand.IOTYPE_OUTPUT)
        np.array([output_operand_index],dtype=np.uint32).tofile(f)


    def dump_layers_to_file(self, f):
        for node in self.nodes:
            if node.name in self.converted_nodes:
                continue

            # conv2d with dilation generates very complex nodes, so handle it in special
            if self.in_conv2d_scope(node.name):
                if node.op == 'Conv2D':
                    self.dump_complex_conv2d_to_file(node, f)
                continue

            if node.op == 'Conv2D':
                self.dump_simple_conv2d_to_file(node, f)
            elif node.op == 'DepthToSpace':
                self.dump_depth2space_to_file(node, f)
            elif node.op == 'MirrorPad':
                self.dump_mirrorpad_to_file(node, f)
            elif node.op == 'Maximum':
                self.dump_maximum_to_file(node, f)
            elif node.op in self.mathbin2code:
                self.dump_mathbinary_to_file(node, f)
            elif node.op in self.mathun2code:
                self.dump_mathunary_to_file(node, f)


    def dump_operands_to_file(self, f):
            operands = sorted(self.name_operand_dict.values())
            for operand in operands:
                #print('{}'.format(operand))
                np.array([operand.index, len(operand.name)], dtype=np.uint32).tofile(f)
                f.write(operand.name.encode('utf-8'))
                np.array([operand.iotype, operand.dtype], dtype=np.uint32).tofile(f)
                np.array([operand.dims[0], operand.dims[1], operand.dims[2], operand.dims[3]], dtype=np.uint32).tofile(f)


    def dump_to_file(self):
        with open(self.outfile, 'wb') as f:
            f.write(header.str.encode('utf-8'))
            np.array([header.major, header.minor], dtype=np.uint32).tofile(f)
            self.dump_layers_to_file(f)
            self.dump_operands_to_file(f)
            np.array([self.layer_number, len(self.name_operand_dict)], dtype=np.uint32).tofile(f)


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


    def in_conv2d_scope(self, name):
        inner_scope = TFConverter.get_scope_name(name)
        if inner_scope == "":
            return False;
        for scope in self.conv2d_scope_names:
            index = inner_scope.find(scope)
            if index == 0:
                return True
        return False


    def generate_conv2d_scope_info(self):
        # mostly, conv2d is a sub block in graph, get the scope name
        for node in self.nodes:
            if node.op == 'Conv2D':
                scope = TFConverter.get_scope_name(node.name)
                # for the case tf.nn.conv2d is called directly
                if scope == '':
                    continue
                # for the case tf.nn.conv2d is called within a scope
                if scope + '/kernel' not in self.name_node_dict:
                    continue
                self.conv2d_scope_names.add(scope)

        # get the input name to the conv2d sub block
        for node in self.nodes:
            scope = TFConverter.get_scope_name(node.name)
            if scope in self.conv2d_scope_names:
                if node.op == 'Conv2D' or node.op == 'Shape':
                    for inp in node.input:
                        if TFConverter.get_scope_name(inp) != scope:
                            self.conv2d_scopename_inputname_dict[scope] = inp


    def run(self):
        self.generate_name_node_dict()
        self.generate_output_names()
        self.remove_identity()
        self.generate_edges()
        self.generate_conv2d_scope_info()

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
