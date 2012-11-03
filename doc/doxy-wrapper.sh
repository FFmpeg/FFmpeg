#!/bin/sh

SRC_PATH="${1}"
DOXYFILE="${2}"

shift 2

doxygen - <<EOF
@INCLUDE        = ${DOXYFILE}
INPUT           = $@
HTML_HEADER     = ${SRC_PATH}/doc/doxy/header.html
HTML_FOOTER     = ${SRC_PATH}/doc/doxy/footer.html
HTML_STYLESHEET = ${SRC_PATH}/doc/doxy/doxy_stylesheet.css
EOF
