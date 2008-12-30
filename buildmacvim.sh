#! /bin/sh

# migemo が不要ならコメントアウト
# if you dont want migemo, commentout this.
#with_migemo=no

# すでにインストール済みの libmigemo を使うときはコメントアウト
# if you use already-installed libmigemo, commentout this.
#with_migemo=installed

# migemo が /usr/local 以外の prefix でインストールされているときは以下を設定
# if you have migemo installed with prefix *other* than /usr/local, set below.
#with_migemo_prefix=/opt/local

prepare_auto_files() {
  cp -f src/auto/config.mk.$1 src/auto/config.mk
  if ! diff src/auto/config.h.$1 src/auto/config.h &>/dev/null; then
    cp -f src/auto/config.h.$1 src/auto/config.h
  fi
}

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

set -e
set -x
case "$with_migemo" in
  no)
    prepare_auto_files no_migemo
    exec $make -f buildmacvim.mk macvim
    ;;
  installed)
    migemo_prefix=${with_migemo_prefix-/usr/local}
    test -f ${migemo_prefix}/include/migemo.h || \
      { echo "migemo.h not found in ${migemo_prefix}/include"; exit 1; }

    prepare_auto_files installed_migemo
    sed -i -e "s.@@@migemo_prefix@@@.${migemo_prefix}." \
      src/auto/config.mk.installed_migemo
    exec $make -f buildmacvim.mk macvim
    ;;
  *)
    prepare_auto_files with_migemo
    exec $make -f buildmacvim.mk macvim APP_WITH_MIGEMO=yes
    ;;
esac

# vim: set sw=2 :
