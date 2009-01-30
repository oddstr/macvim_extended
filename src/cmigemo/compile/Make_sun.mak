# vim:set ts=8 sts=8 sw=8 tw=0:
#
# Sun's Solaris/gcc $BMQ(B Makefile
#
# Last Change:	19-Oct-2003.
# Base Idea:	AIDA Shinra
# Modified By:	Hiroshi Fujishima <pooh@nature.tsukuba.ac.jp>
# Maintainer:	MURAOKA Taro <koron@tka.att.ne.jp>

##############################################################################
# $B4D6-$K1~$8$F$3$NJQ?t$rJQ99$9$k(B
#
CC		= gcc
libmigemo_LIB	= libmigemo.so.1.1.0
libmigemo_DSO	= libmigemo.so.1
libmigemo	= libmigemo.so
EXEEXT		=
CFLAGS_MIGEMO	= -fPIC
LDFLAGS_MIGEMO	= -R/usr/local/lib

include config.mk
include compile/unix.mak
include src/depend.mak
include compile/clean_unix.mak
include compile/clean.mak

##############################################################################
# $B4D6-$K1~$8$F%i%$%V%i%j9=C[K!$rJQ99$9$k(B
#
$(libmigemo_LIB): $(libmigemo_DSO)
$(libmigemo_DSO): $(libmigemo_OBJ)
	/usr/ccs/bin/ld -G -o $(libmigemo_LIB) -h $@ $(libmigemo_OBJ)
	$(RM) $@ $(libmigemo)
	ln -s $(libmigemo_LIB) $@
	ln -s $(libmigemo_LIB) $(libmigemo)

install-lib: $(libmigemo_DSO)
	$(INSTALL_PROGRAM) $(libmigemo_LIB) $(libdir)
	$(RM) $(libdir)/$(libmigemo_DSO) $(libdir)/$(libmigemo)
	ln -s $(libmigemo_LIB) $(libdir)/$(libmigemo_DSO)
	ln -s $(libmigemo_LIB) $(libdir)/$(libmigemo)

uninstall-lib:
	$(RM) $(libdir)/$(libmigemo_DSO)
	$(RM) $(libdir)/$(libmigemo_LIB)
	$(RM) $(libdir)/$(libmigemo)

dictionary:
	cd dict && $(MAKE) gcc
