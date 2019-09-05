#!/bin/sh

set -e

if test "bisect-create" = "`basename $0`" ; then
    echo tools/ffbisect created
    git show master:tools/bisect-create > tools/ffbisect
    chmod u+x tools/ffbisect
    exit 1
fi

if ! git show master:tools/bisect-create | diff - tools/ffbisect > /dev/null ; then
    echo updating tools/ffbisect script to HEAD.
    git show master:tools/bisect-create > tools/ffbisect
    chmod u+x tools/ffbisect
    tools/ffbisect $*
    exit 0
fi

case "$1" in
    need)
        case $2 in
            ffmpeg|ffplay|ffprobe)
                echo $2.c >> tools/bisect.need
            ;;
        esac
    ;;
    start|reset)
        echo . > tools/bisect.need
        git bisect $*
    ;;
    skip)
        git bisect $*
    ;;
    good|bad)
        git bisect $*

        until ls `cat tools/bisect.need` > /dev/null 2> /dev/null; do
            git bisect skip || break
        done
    ;;
    run)
       shift # remove "run" from arguments
       git bisect run sh -c "ls \`cat tools/bisect.need\` > /dev/null 2> /dev/null || exit 125; \"\$@\"" sh "$@"
    ;;
esac
