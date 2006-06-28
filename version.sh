#!/bin/sh

svn_revision=`cd "$1" && svn info 2> /dev/null | grep Revision | cut -d' ' -f2`
test $svn_revision || svn_revision=UNKNOWN

NEW_REVISION="#define FFMPEG_VERSION \"SVN-r$svn_revision\""
OLD_REVISION=`cat version.h 2> /dev/null`

# Update version.h only on revision changes to avoid spurious rebuilds
if test "$NEW_REVISION" != "$OLD_REVISION"; then
    echo "$NEW_REVISION" > version.h
fi
