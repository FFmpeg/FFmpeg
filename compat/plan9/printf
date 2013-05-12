#!/bin/sh
exec awk "BEGIN { for (i = 2; i < ARGC; i++) printf \"$1\", ARGV[i] }" "$@"
