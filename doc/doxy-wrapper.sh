#!/bin/sh

SRC_PATH="${1}"
DOXYFILE="${2}"
DOXYGEN="${3}"

shift 3

if [ -e "$SRC_PATH/VERSION" ]; then
    VERSION=`cat "$SRC_PATH/VERSION"`
else
    VERSION=`cd "$SRC_PATH"; git describe`
fi

$DOXYGEN - <<EOF
@INCLUDE        = ${DOXYFILE}
INPUT           = $@
EXAMPLE_PATH    = ${SRC_PATH}/doc/examples
HTML_TIMESTAMP  = NO
PROJECT_NUMBER  = $VERSION
EOF
