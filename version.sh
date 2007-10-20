#!/bin/sh

# check for SVN revision number
revision=`cd "$1" && LC_ALL=C svn info 2> /dev/null | grep Revision | cut -d' ' -f2`
test $revision || revision=`cd "$1" && grep revision .svn/entries 2>/dev/null | cut -d '"' -f2`
test $revision || revision=`cd "$1" && sed -n -e '/^dir$/{n;p;q}' .svn/entries 2>/dev/null`
test $revision && revision=SVN-r$revision

# check for git short hash
if ! test $revision; then
    revision=`cd "$1" && git log -1 --pretty=format:%h`
    test $revision && revision=git-$revision
fi

# no version number found
test $revision || revision=UNKNOWN

NEW_REVISION="#define FFMPEG_VERSION \"$revision\""
OLD_REVISION=`cat version.h 2> /dev/null`

# Update version.h only on revision changes to avoid spurious rebuilds
if test "$NEW_REVISION" != "$OLD_REVISION"; then
    echo "$NEW_REVISION" > version.h
fi
