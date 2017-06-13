#!/bin/sh

echo "Inside autogen 1"
srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.
echo "Inside autogen 22"
mkdir -p m4

autoreconf --verbose --force --install || exit 1
