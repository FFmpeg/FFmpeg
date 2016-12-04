#!/bin/sh

target_samples=$3
target_exec=$4
target_path=$5

#perl -e 'chomp($wd = `pwd`); print map { s!tests/data/!!; "<Stream $_>\nFile $wd/tests/data/$_\n</Stream>\n\n" } @ARGV' tests/data/a* >> tests/data/ffserver.conf
#perl -e 'chomp($wd = `pwd`); print map { s!tests/data/!!; "<Stream $_.asf>\nFile $wd/tests/data/$_\n</Stream>\n\n" } @ARGV' tests/data/a* >> tests/data/ffserver.conf

. $(dirname $0)/md5.sh

FILES=$(sed -n 's/^[^#]*<Stream \(.*\)>.*/\1/p' $2 | grep -v html)

rm -f tests/feed1.ffm
$target_exec ${target_path}/ffserver${PROGSUF} -f "$2" &
FFSERVER_PID=$!
echo "Waiting for feeds to startup..."
sleep 2
(
    cd tests/data || exit $?
    rm -f ff-* ffserver.regression
    WGET_OPTIONS="--user-agent=NSPlayer -q --proxy=off -e verbose=off -e server_response=off -T3 --tries=1"
    for file in $FILES; do
        if [ $(expr $file : "a-*") != 0 ]; then
            wget $WGET_OPTIONS -O - http://localhost:9999/$file > ff-$file
        else
            wget $WGET_OPTIONS -O - http://localhost:9999/$file?date=19700101T000000Z | dd bs=1 count=100000 > ff-$file 2>/dev/null
        fi
        do_md5sum ff-$file >>ffserver.regression
    done
    wget  $WGET_OPTIONS -O - 'http://localhost:9999/teststat.html?abc' > ff-stat 2>/dev/null
    do_md5sum ff-stat >>ffserver.regression
)
kill $FFSERVER_PID
wait > /dev/null 2>&1
rm -f tests/feed1.ffm
if diff -u "$1" tests/data/ffserver.regression; then
    echo
    echo Server regression test succeeded.
    exit 0
else
    echo
    echo Server regression test: Error.
    exit 1
fi
