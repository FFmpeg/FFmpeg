#!/bin/sh

cd "$1"/..

git show > /dev/null 2> /dev/null || { cat tests/ref/fate/source ; exit 0; }

echo Files without standard license headers:
git grep -L -E "This file is part of FFmpeg|This file is part of libswresample|"\
"Permission to use, copy, modify, and/or distribute this software for any|"\
"Permission is hereby granted, free of charge, to any person|"\
"Permission is hereby granted to use, copy, modify, and distribute this|"\
"Permission is granted to anyone to use this software for any purpose|"\
"This work is licensed under the terms of the GNU GPL|"\
"Redistribution and use in source and binary forms, with or without modification|"\
"This library is free software; you can redistribute it and/or|"\
"This program is free software; you can redistribute it and/or modify|"\
"This file is placed in the public domain" | grep -E '\.c$|\.h$|\.S$|\.asm$'

echo Headers without standard inclusion guards:
for f in `git ls-files | grep '\.h$'` ; do
    macro="`echo $f | sed \
        -e 's/^lib//' \
        -e 's/[^A-Za-z0-9]\{1,\}/_/g' \
        -e 's/_af_/_/' \
        -e 's/_vf_/_/' \
        -e 's/_avf_/_/' \
        -e 's/_vaf_/_/' \
    | tr abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ`"

    grep -L "^#define $macro$" $f
done

exit 0
