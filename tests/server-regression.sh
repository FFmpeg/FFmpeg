#!/bin/bash
# Even in the 21st century some diffs are not supporting -u.
diff -u $0 $0 > /dev/null 2>&1
if [ $? -eq 0 ]; then
  diff_cmd="diff -u"
else
  diff_cmd="diff"
fi
    
# Make sure that the data directory exists
mkdir -p data

cp "$2" data/test.conf
#perl -e 'chomp($wd = `pwd`); print map { s!data/!!; "<Stream $_>\nFile $wd/data/$_\n</Stream>\n\n" } @ARGV' data/a* >> data/test.conf
#perl -e 'chomp($wd = `pwd`); print map { s!data/!!; "<Stream $_.asf>\nFile $wd/data/$_\n</Stream>\n\n" } @ARGV' data/a* >> data/test.conf

FILES=`perl -n -e 'print \$1, "\n" if /<stream\\s+(\\S+)>/i' data/test.conf | sort`

rm -f /tmp/feed.ffm
../ffserver -d -f data/test.conf 2> /dev/null &
FFSERVER_PID=$!
echo "Waiting for feeds to startup..."
sleep 2
(
    cd data || exit $?
    rm -f ff-*;
    WGET_OPTIONS="--user-agent=NSPlayer -q --proxy=off -e verbose=off -e debug=off -e server_response=off"
    for file in $FILES; do
        if [ `expr match $file "a-*"` -ne 0 ]; then
            wget $WGET_OPTIONS --output-document=- http://localhost:9999/$file > ff-$file &
        else
            wget $WGET_OPTIONS --output-document=- http://localhost:9999/$file?date=19700101T000000Z | dd bs=1 count=100000 > ff-$file 2>/dev/null &
        fi
        MDFILES="$MDFILES ff-$file"
    done    
    wait
    # the status page is always different
    md5sum $MDFILES | grep -v html > ffserver.regression
)
kill $FFSERVER_PID
wait > /dev/null 2>&1
if $diff_cmd data/ffserver.regression $1 ; then
    echo 
    echo Server regression test succeeded.
    exit 0
else
    echo 
    echo Server regression test: Error.
    exit 1
fi
