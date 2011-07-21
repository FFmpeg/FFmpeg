#!/bin/sh

# check for git short hash
if ! test "$revision"; then
    revision=$(cd "$1" && git describe --tags --match N 2> /dev/null)
fi

# Shallow Git clones (--depth) do not have the N tag:
# use 'git-YYYY-MM-DD-hhhhhhh'.
test "$revision" || revision=$(cd "$1" &&
  git log -1 --pretty=format:"git-%cd-%h" --date=short 2> /dev/null)

# Snapshots from gitweb are in a directory called ffmpeg-hhhhhhh or
# ffmpeg-HEAD-hhhhhhh.
if [ -z "$revision" ]; then
  srcdir=$(cd "$1" && pwd)
  case "$srcdir" in
    */ffmpeg-[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f])
      git_hash="${srcdir##*-}";;
    */ffmpeg-HEAD-[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f])
      git_hash="${srcdir##*-}";;
  esac
fi

# no revision number found
test "$revision" || revision=$(cd "$1" && cat RELEASE 2> /dev/null)

# Append the Git hash if we have one
test "$revision" && test "$git_hash" && revision="$revision-$git_hash"

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
