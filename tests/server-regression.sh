#!/bin/bash
# Even in the 21st century some diffs are not supporting -u.
diff -u $0 $0 > /dev/null 2>&1
if [ $? -eq 0 ]; then
  diff_cmd="diff -u"
else
  diff_cmd="diff"
fi

# Make sure that the data directory exists
mkdir -p tests/data

#perl -e 'chomp($wd = `pwd`); print map { s!tests/data/!!; "<Stream $_>\nFile $wd/tests/data/$_\n</Stream>\n\n" } @ARGV' tests/data/a* >> tests/data/test.conf
#perl -e 'chomp($wd = `pwd`); print map { s!tests/data/!!; "<Stream $_.asf>\nFile $wd/tests/data/$_\n</Stream>\n\n" } @ARGV' tests/data/a* >> tests/data/test.conf

FILES=`sed -n 's/^[^#]*<Stream \(.*\)>.*/\1/p' $2 | grep -v html`

rm -f tests/feed1.ffm
./ffserver -d -f tests/test.conf 2> /dev/null &
FFSERVER_PID=$!
sleep 2
echo "Waiting for feeds to startup..."
./ffmpeg -loop_input -flags +bitexact -dct fastint -idct simple -y -f pgmyuv -i tests/vsynth1/%02d.pgm http://localhost:9999/feed1.ffm 2> /dev/null &
FFMPEG_PID=$!
sleep 5
(
    cd tests/data || exit $?
    rm -f ff-*;
    WGET_OPTIONS="--user-agent=NSPlayer -q --proxy=off -e verbose=off -e server_response=off"
    for file in $FILES; do
        if [ `expr $file : "a-*"` != 0 ]; then
            wget $WGET_OPTIONS --output-document=- http://localhost:9999/$file > ff-$file
        else
            wget $WGET_OPTIONS --output-document=- http://localhost:9999/$file?date=19700101T000000Z | dd bs=1 count=20000 > ff-$file 2>/dev/null
        fi
        MDFILES="$MDFILES ff-$file"
    done
    wait
    # the status page is always different
    md5sum $MDFILES > ffserver.regression
)
kill $FFMPEG_PID
kill $FFSERVER_PID
wait > /dev/null 2>&1
rm -f tests/feed1.ffm
if $diff_cmd tests/data/ffserver.regression "$1" ; then
    echo
    echo Server regression test succeeded.
    exit 0
else
    echo
    echo Server regression test: Error.
    exit 1
fi
