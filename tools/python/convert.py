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

# verified with Python 3.5.2 on Ubuntu 16.04
import argparse
import os
from convert_from_tensorflow import *

def get_arguments():
    parser = argparse.ArgumentParser(description='generate native mode model with weights from deep learning model')
    parser.add_argument('--outdir', type=str, default='./', help='where to put generated files')
    parser.add_argument('--infmt', type=str, default='tensorflow', help='format of the deep learning model')
    parser.add_argument('infile', help='path to the deep learning model with weights')

    return parser.parse_args()

def main():
    args = get_arguments()

    if not os.path.isfile(args.infile):
        print('the specified input file %s does not exist' % args.infile)
        exit(1)

    if not os.path.exists(args.outdir):
        print('create output directory %s' % args.outdir)
        os.mkdir(args.outdir)

    basefile = os.path.split(args.infile)[1]
    basefile = os.path.splitext(basefile)[0]
    outfile = os.path.join(args.outdir, basefile) + '.model'

    if args.infmt == 'tensorflow':
        convert_from_tensorflow(args.infile, outfile)

if __name__ == '__main__':
    main()
