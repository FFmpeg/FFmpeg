#!/bin/sh

SRC_PATH="${1}"
DOXYFILE="${2}"

shift 2

doxygen - <<EOF
@INCLUDE        = ${DOXYFILE}
INPUT           = $@
EXAMPLE_PATH    = ${SRC_PATH}/doc/examples
EOF
