#!/bin/sh

# check for SVN revision number
revision=$(cat snapshot_version 2> /dev/null)
test $revision || revision=$(cd "$1" && LC_ALL=C svn info 2> /dev/null | grep Revision | cut -d' ' -f2)
test $revision || revision=$(cd "$1" && grep revision .svn/entries 2>/dev/null | cut -d '"' -f2)
test $revision || revision=$(cd "$1" && sed -n -e '/^dir$/{n;p;q}' .svn/entries 2>/dev/null)
test $revision && revision=SVN-r$revision

# check for git short hash
if ! test $revision; then
    revision=$(cd "$1" && git log -1 --pretty=format:%h)
    test $revision && revision=git-$revision
fi

# no revision number found
test $revision || revision=UNKNOWN

# releases extract the version number from the VERSION file
version=$(cat VERSION 2> /dev/null)
test $version || version=$revision

test -n "$3" && version=$version-$3

NEW_REVISION="#define FFMPEG_VERSION \"$version\""
OLD_REVISION=$(cat version.h 2> /dev/null)

# Update version.h only on revision changes to avoid spurious rebuilds
if test "$NEW_REVISION" != "$OLD_REVISION"; then
    echo "$NEW_REVISION" > "$2"
fi
