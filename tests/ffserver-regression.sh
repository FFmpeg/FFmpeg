#!/bin/sh

#perl -e 'chomp($wd = `pwd`); print map { s!tests/data/!!; "<Stream $_>\nFile $wd/tests/data/$_\n</Stream>\n\n" } @ARGV' tests/data/a* >> tests/data/ffserver.conf
#perl -e 'chomp($wd = `pwd`); print map { s!tests/data/!!; "<Stream $_.asf>\nFile $wd/tests/data/$_\n</Stream>\n\n" } @ARGV' tests/data/a* >> tests/data/ffserver.conf

FILES=`sed -n 's/^[^#]*<Stream \(.*\)>.*/\1/p' $2 | grep -v html`

rm -f tests/feed1.ffm
./ffserver -d -f "$2" 2> /dev/null &
FFSERVER_PID=$!
echo "Waiting for feeds to startup..."
sleep 2
(
    cd tests/data || exit $?
    rm -f ff-*;
    WGET_OPTIONS="--user-agent=NSPlayer -q --proxy=off -e verbose=off -e server_response=off"
    for file in $FILES; do
        if [ `expr $file : "a-*"` != 0 ]; then
            wget $WGET_OPTIONS -O - http://localhost:9999/$file > ff-$file
        else
            wget $WGET_OPTIONS -O - http://localhost:9999/$file?date=19700101T000000Z | dd bs=1 count=20000 > ff-$file 2>/dev/null
        fi
        MDFILES="$MDFILES ff-$file"
    done
    wait
    # the status page is always different
    md5sum $MDFILES > ffserver.regression
)
kill $FFSERVER_PID
wait > /dev/null 2>&1
rm -f tests/feed1.ffm
if diff -u tests/data/ffserver.regression "$1" ; then
    echo
    echo Server regression test succeeded.
    exit 0
else
    echo
    echo Server regression test: Error.
    exit 1
fi
