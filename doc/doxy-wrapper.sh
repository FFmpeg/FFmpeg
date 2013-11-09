#!/bin/sh

SRC_PATH="${1}"
DOXYFILE="${2}"

shift 2

doxygen - <<EOF
@INCLUDE        = ${DOXYFILE}
INPUT           = $@
EOF
