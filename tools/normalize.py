#!/usr/bin/env python2

import sys, subprocess

if len(sys.argv) > 2:
    ifile  = sys.argv[1]
    encopt = sys.argv[2:-1]
    ofile  = sys.argv[-1]
else:
    print 'usage: %s <input> [encode_options] <output>' % sys.argv[0]
    sys.exit(1)

analysis_cmd  = 'ffprobe -v error -of compact=p=0:nk=1 '
analysis_cmd += '-show_entries frame_tags=lavfi.r128.I -f lavfi '
analysis_cmd += "amovie='%s',ebur128=metadata=1" % ifile
try:
    probe_out = subprocess.check_output(analysis_cmd, shell=True)
except subprocess.CalledProcessError, e:
    sys.exit(e.returncode)
loudness = ref = -23
for line in probe_out.splitlines():
    sline = line.rstrip()
    if sline:
        loudness = sline
adjust = ref - float(loudness)
if abs(adjust) < 0.0001:
    print 'No normalization needed for ' + ifile
else:
    print "Adjust %s by %.1fdB" % (ifile, adjust)
    norm_cmd  = ['ffmpeg', '-i', ifile, '-af', 'volume=%fdB' % adjust]
    norm_cmd += encopt + [ofile]
    print ' => %s' % ' '.join(norm_cmd)
    subprocess.call(norm_cmd)
