#!/bin/sh

OUT_DIR="${1}"
SRC_DIR="${2}"
DOXYFILE="${3}"
DOXYGEN="${4}"

shift 4

cd ${SRC_DIR}

VERSION=$(./ffbuild/version.sh .)

$DOXYGEN - <<EOF
@INCLUDE        = ${DOXYFILE}
INPUT           = $@
HTML_TIMESTAMP  = NO
PROJECT_NUMBER  = $VERSION
OUTPUT_DIRECTORY = $OUT_DIR
EOF
