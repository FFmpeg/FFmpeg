#!/bin/sh

LINK_EXE_PATH=$(dirname "$(command -v cl)")/link
if [ -x "$LINK_EXE_PATH" ]; then
    "$LINK_EXE_PATH" $@
else
    link $@
fi
exit $?
