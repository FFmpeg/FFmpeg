#!/bin/sh

SRC_PATH="${1}"
DOXYFILE="${2}"

shift 2

doxygen - <<EOF
@INCLUDE        = ${DOXYFILE}
INPUT           = $@
EXAMPLE_PATH    = ${SRC_PATH}/doc/examples
HTML_HEADER     = ${SRC_PATH}/doc/doxy/header.html
HTML_FOOTER     = ${SRC_PATH}/doc/doxy/footer.html
HTML_STYLESHEET = ${SRC_PATH}/doc/doxy/doxy_stylesheet.css
EOF
