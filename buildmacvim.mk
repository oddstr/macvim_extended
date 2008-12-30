# Makefile to build MacVim.app
VIM           = src/Vim
MACVIM_BUNDLE = src/MacVim/build/Release/MacVim.app
MACVIM_BIN    = $(MACVIM_BUNDLE)/Contents/MacOS/MacVim
VIM_BIN       = $(MACVIM_BUNDLE)/Contents/MacOS/Vim

all: macvim

ifdef APP_WITH_MIGEMO
MIGEMO_TMP    = src/cmigemo/dist
MIGEMO_DEST   = $(MACVIM_BUNDLE)/Contents/Resources
GLOBAL_VIMRC  = $(MACVIM_BUNDLE)/Contents/Resources/vim/vimrc
LIBMIGEMO     = $(MIGEMO_TMP)/lib/libmigemo.1.dylib
$(LIBMIGEMO):
	cd src/cmigemo && ./configure --prefix=`pwd`/dist
	$(MAKE) -C src/cmigemo/dict utf-8
	$(MAKE) -C src/cmigemo osx-install
else
LIBMIGEMO     = 
endif

$(VIM): $(LIBMIGEMO)
	$(MAKE) -C src

macvim: $(MACVIM_BIN)

$(MACVIM_BIN): $(VIM)
	cd src/MacVim && xcodebuild
ifdef APP_WITH_MIGEMO
	if ! grep migemo $(GLOBAL_VIMRC) &>/dev/null; then \
	  echo; \
	  echo '" enable migemo by default'; \
	  echo 'set migemo'; \
	  echo 'let &migemodict = fnamemodify($$VIM, ":h") . "/share/migemo/utf-8/migemo-dict"'; \
	fi >> $(GLOBAL_VIMRC)
	install -d $(MIGEMO_DEST)/lib
	install -m 644 $(MIGEMO_TMP)/lib/* $(MIGEMO_DEST)/lib/
	install -d $(MIGEMO_DEST)/share/migemo/utf-8
	install -m 644 $(MIGEMO_TMP)/share/migemo/utf-8/* $(MIGEMO_DEST)/share/migemo/utf-8/
	install_name_tool -change \
	  "`otool -L $(VIM_BIN) | awk '/libmigemo/ {print $$1}'`" \
	  "@executable_path/../Resources/lib/libmigemo.1.dylib" \
	  $(VIM_BIN)
	install_name_tool -id \
	  "@executable_path/../Resources/lib/libmigemo.1.dylib" \
	  "$(MIGEMO_DEST)/lib/libmigemo.1.dylib"
endif
	touch $(MACVIM_BIN)

# vim: set sw=2:
