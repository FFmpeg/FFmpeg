#!/bin/sh -v

# args [where to put patches] [smtp server] [destination]

git format-patch -o "$1" --inline --subject-prefix=soc --thread origin
git send-email --no-chain-reply-to --smtp-server $2 --to $3 --dry-run "$1"
