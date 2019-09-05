#!/bin/sh

grep -A99999 '^<<<<<<<' | grep -B99999 '^>>>>>>>' >murge.X
grep -A99999 '^====' murge.X | egrep -v '^(=======|<<<<<<<|>>>>>>>|\|\|\|\|\|\|\|)' >murge.theirs
grep -B99999 '^||||' murge.X | egrep -v '^(=======|<<<<<<<|>>>>>>>|\|\|\|\|\|\|\|)' >murge.ours
grep -B99999 '^====' murge.X | grep -A99999 '^||||' | egrep -v '^(=======|<<<<<<<|>>>>>>>|\|\|\|\|\|\|\|)'  >murge.common

colordiff -du $* murge.ours murge.theirs
grep . murge.common > /dev/null && colordiff -du $* murge.common murge.theirs
grep . murge.common > /dev/null && colordiff -du $* murge.common murge.ours
rm murge.theirs murge.common murge.ours murge.X
