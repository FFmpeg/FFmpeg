#!/bin/sh
toupper(){
    echo "$@" | tr abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ
}

name=lib$1
ucname=$(toupper ${name})
file=$2
file2=$3

eval $(awk "/#define ${ucname}_VERSION_M/ { print \$2 \"=\" \$3 }" "$file")
if [ -f "$file2" ]; then
  eval $(awk "/#define ${ucname}_VERSION_M/ { print \$2 \"=\" \$3 }" "$file2")
fi
eval ${ucname}_VERSION=\$${ucname}_VERSION_MAJOR.\$${ucname}_VERSION_MINOR.\$${ucname}_VERSION_MICRO
eval echo "${name}_VERSION=\$${ucname}_VERSION"
eval echo "${name}_VERSION_MAJOR=\$${ucname}_VERSION_MAJOR"
eval echo "${name}_VERSION_MINOR=\$${ucname}_VERSION_MINOR"
