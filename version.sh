#!/bin/sh

revision=0.5

test -n "$3" && revision=$revision-$3

NEW_REVISION="#define FFMPEG_VERSION \"$revision\""
OLD_REVISION=$(cat version.h 2> /dev/null)

# Update version.h only on revision changes to avoid spurious rebuilds
if test "$NEW_REVISION" != "$OLD_REVISION"; then
    echo "$NEW_REVISION" > "$2"
fi
