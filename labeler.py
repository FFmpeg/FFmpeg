#!/usr/bin/env python2
import intervaltree
import os
import json
import subprocess
from Tkinter import Tk
import tkFileDialog

SAVE_MSG = 'Save video labels as...'


def main():
    root = Tk()
    root.title('Oregon State University Video Labeler')
    root.focus_force()
    root.update()
    input_filename = tkFileDialog.askopenfilename(title="Select a video file to label")
    root.update()
    root.withdraw()

    print("subprocess ./ffplay {}".format(input_filename))

    cmd = ['./ffplay', input_filename]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=open('/dev/null'))
    csv_output, _ = proc.communicate()

    lines = csv_output.splitlines()
    print lines

    intervals = {}

    for line in lines:
        if line.startswith('#') or not line:
            continue
        tokens = line.split(',')
        class_name = tokens[0]
        start_time = float(tokens[1])
        duration = float(tokens[2])
        if class_name not in intervals:
            intervals[class_name] = intervaltree.IntervalTree()
        intervals[class_name].add(intervaltree.Interval(start_time, start_time + duration))

    output_json = {}
    for class_name, tree in intervals.items():
        tree.merge_overlaps()
        output_json[class_name] = [{'start': i[0], 'end': i[1]} for i in tree.items()]
    default_filename = os.path.splitext(os.path.basename(input_filename))[0] + '.txt'
    out_filename = tkFileDialog.asksaveasfilename(defaultextension=".txt", initialfile=default_filename,
        initialdir='.', message=SAVE_MSG, title=SAVE_MSG)
    open(out_filename, 'w').write(json.dumps(output_json, indent=4))


if __name__ == '__main__':
    main()
