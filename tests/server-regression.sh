#!/bin/bash

FILES=`perl -n -e 'print \$1, " " if /<stream\\s+(\\S+)>/i' test.conf`

rm -f /tmp/feed.ffm
../ffserver -d -f test.conf 2> /dev/null &
FFSERVER_PID=$!
sleep 1
(
    cd data
    for file in $FILES; do
        wget --user-agent=NSPlayer -q --output-document=- http://localhost:9999/$file?date=19700101T000000Z | head --bytes=100000 > $file &
    done    
    wait
    # the status page is always different
    md5sum $FILES | grep -v html > ffserver.regression
)
kill $FFSERVER_PID
wait > /dev/null 2>&1
if diff -u data/ffserver.regression $1 ; then
    echo 
    echo Server regression test succeeded.
    exit 0
else
    echo 
    echo Server regression test: Error.
    exit 1
fi
