#!/bin/sh

# check for git short hash
if ! test "$revision"; then
    revision=$(cd "$1" && git describe --tags --match N 2> /dev/null)
fi

# no revision number found
test "$revision" || revision=$(cd "$1" && cat RELEASE 2> /dev/null)

# releases extract the version number from the VERSION file
version=$(cd "$1" && cat VERSION 2> /dev/null)
test "$version" || version=$revision

test -n "$3" && version=$version-$3

if [ -z "$2" ]; then
    echo "$version"
    exit
fi

NEW_REVISION="#define FFMPEG_VERSION \"$version\""
OLD_REVISION=$(cat version.h 2> /dev/null)

# Update version.h only on revision changes to avoid spurious rebuilds
if test "$NEW_REVISION" != "$OLD_REVISION"; then
    echo "$NEW_REVISION" > "$2"
fi
