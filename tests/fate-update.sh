#! /bin/sh

set -e

base=$(dirname $0)
ref="${base}/ref/fate"

FATE_DB_URL="http://fate.multimedia.cx/fate-tests.sqlite.bz2"
FATE_DB=$(mktemp fate-db.XXXXXX)
SQL_TESTS='SELECT id,short_name,command FROM test_spec WHERE active=1 ORDER BY short_name'

do_sql(){
    sqlite3 -noheader -separator ' ' "$FATE_DB" "$@"
}

wget -q -O - "$FATE_DB_URL" | bunzip2 > "$FATE_DB"
rm -rf "$ref"
mkdir -p "$ref"
exec 3>"$base/fate.mak"

do_sql "$SQL_TESTS" | while read id name command; do
    case "$name" in
        00-full-regression|ffmpeg-help|binsize-*) continue ;;
    esac
    case "$command" in
        {MD5}*) command="${command#\{MD5\}} | do_md5sum | cut -c-32" ;;
        {*}*)   continue ;;
    esac
    command=$(echo "$command" | sed 's/\$/$$/g')
    do_sql "SELECT expected_stdout FROM test_spec WHERE id=$id" | awk '/./{print}' > "$ref/$name"
    printf "FATE_TESTS += fate-${name}\n" >&3
    printf "fate-${name}: CMD = %s\n" "$command" >&3
done

exec 3<&-
rm -f "$FATE_DB"
