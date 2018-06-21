#!/bin/sh

OUT_DIR="${1}"
DOXYFILE="${2}"
DOXYGEN="${3}"

shift 3

if [ -e "VERSION" ]; then
    VERSION=`cat "VERSION"`
else
    VERSION=`git describe`
fi

$DOXYGEN - <<EOF
@INCLUDE        = ${DOXYFILE}
INPUT           = $@
HTML_TIMESTAMP  = NO
PROJECT_NUMBER  = $VERSION
OUTPUT_DIRECTORY = $OUT_DIR
EOF
