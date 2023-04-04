#!/usr/bin/env python3

import argparse
import logging
import shlex
import subprocess

HELP = '''
Normalize audio input.

The command uses ffprobe to analyze an input file with the ebur128
filter, and finally run ffmpeg to normalize the input depending on the
computed adjustment.

ffmpeg encoding arguments can be passed through the extra arguments
after options, for example as in:
normalize.py --input input.mp3 --output output.mp3 -- -loglevel debug -y
'''

logging.basicConfig(format='normalize|%(levelname)s> %(message)s', level=logging.INFO)
log = logging.getLogger()


class Formatter(
    argparse.ArgumentDefaultsHelpFormatter, argparse.RawDescriptionHelpFormatter
):
    pass


def normalize():
    parser = argparse.ArgumentParser(description=HELP, formatter_class=Formatter)
    parser.add_argument('--input', '-i', required=True, help='specify input file')
    parser.add_argument('--output', '-o', required=True, help='specify output file')
    parser.add_argument('--dry-run', '-n', help='simulate commands', action='store_true')
    parser.add_argument('encode_arguments', nargs='*', help='specify encode options used for the actual encoding')

    args = parser.parse_args()

    analysis_cmd = [
        'ffprobe', '-v', 'error', '-of', 'compact=p=0:nk=1',
        '-show_entries', 'frame_tags=lavfi.r128.I', '-f', 'lavfi',
        f"amovie='{args.input}',ebur128=metadata=1"
    ]

    def _run_command(cmd, dry_run=False):
        log.info(f"Running command:\n$ {shlex.join(cmd)}")
        if not dry_run:
            result = subprocess.run(cmd, check=True, stdout=subprocess.PIPE)
            return result

    result = _run_command(analysis_cmd)

    loudness = ref = -23
    for line in result.stdout.splitlines():
        sline = line.rstrip()
        if sline:
            loudness = sline

    adjust = ref - float(loudness)
    if abs(adjust) < 0.0001:
        logging.info(f"No normalization needed for '{args.input}'")
        return

    logging.info(f"Adjusting '{args.input}' by {adjust:.2f}dB...")
    normalize_cmd = [
        'ffmpeg', '-i', args.input, '-af', f'volume={adjust:.2f}dB'
    ] + args.encode_arguments + [args.output]

    _run_command(normalize_cmd, args.dry_run)


if __name__ == '__main__':
    normalize()
