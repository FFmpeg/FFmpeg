#!/bin/sh

SRC_PATH="${1}"
DOXYFILE="${2}"
DOXYGEN="${3}"

shift 3

$DOXYGEN - <<EOF
@INCLUDE        = ${DOXYFILE}
INPUT           = $@
EXAMPLE_PATH    = ${SRC_PATH}/doc/examples
EOF
