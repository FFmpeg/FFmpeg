#!/bin/sh

svn_revision=`svn info | grep Revision | cut -d' ' -f2 || echo UNKNOWN`
NEW_REVISION="#define FFMPEG_VERSION \"SVN-r$svn_revision\""
OLD_REVISION=`cat version.h 2> /dev/null`

# Update version.h only on revision changes to avoid spurious rebuilds
if test "$NEW_REVISION" != "$OLD_REVISION"; then
    echo "$NEW_REVISION" > version.h
fi
