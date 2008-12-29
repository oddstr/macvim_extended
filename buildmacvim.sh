#! /bin/sh

if which gnumake >/dev/null 2>&1; then
  make=gnumake
elif which gmake >/dev/null 2>&1; then
  make=gmake
elif which make >/dev/null 2>&1; then
  make=make
else
  echo "Sorry, couldn't find make in your PATH."
  exit 1
fi

topdir=$(dirname $0)
test -z "$topdir" && topdir=.
cd $topdir

exec $make -f buildmacvim.mk macvim

# vim: set sw=2 :
