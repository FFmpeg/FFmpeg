#! /bin/sh

base=$(dirname $0)
. "${base}/md5.sh"

test="${1#fate-}"
SAMPLES_PATH=$2
target_exec=$3
BUILD_PATH=$4
command=$5

ref="${base}/ref/fate/${test}"
outdir="tests/data/fate"
outfile="${outdir}/${test}"

mkdir -p "$outdir"

eval $target_exec $command > "$outfile" 2>/dev/null
diff -u -w "$ref" "$outfile"
