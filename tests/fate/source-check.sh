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
"Redistribution and use in source and binary forms, with or without|"\
"This library is free software; you can redistribute it and/or|"\
"This program is free software; you can redistribute it and/or modify|"\
"Licensed under the Apache License|"\
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

    git grep -L "^#define $macro$" $f
done

echo "Use of av_clip() where av_clip_uintp2() could be used:"
git grep -E 'av_clip *\(.*, *0 *, *(3|7|15|31|63|127|255|511|1023|2047|4095|8191|'\
'16383|32767|65535|131071|262143|524287|1048575|2097151|4194303|8388607|16777215|'\
'33554431|67108863|134217727|268435455|536870911|1073741823) *\)' | grep -v fate/source

echo "Use of av_clip() where av_clip_intp2() could be used:"
git grep -E 'av_clip *\(.*, *(-2 *, *1|-4 *, *3|-8 *, *7|-16 *, *15|-32 *, *31|-64'\
' *, *63|-128 *, *127|-256 *, *255|-512 *, *511|-1024 *, *1023|-2048 *, *2047|-4096'\
' *, *4095|-8192 *, *8191|-16384 *, *16383|-32768 *, *32767|-65536 *, *65535|-131072'\
' *, *131071|-262144 *, *262143|-524288 *, *524287|-1048576 *, *1048575|-2097152 *,'\
' *2097151|-4194304 *, *4194303|-8388608 *, *8388607|-16777216 *, *16777215|-33554432'\
' *, *33554431|-67108864 *, *67108863|-134217728 *, *134217727|-268435456 *, *'\
'268435455|-536870912 *, *536870911|-1073741824 *, *1073741823) *\)'| grep -v fate/source

exit 0
