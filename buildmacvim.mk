# Makefile to build MacVim.app
VIM        = src/Vim
MACVIM_BIN = src/MacVim/build/Release/MacVim.app/Contents/MacOS/MacVim

all: macvim

macvim: $(MACVIM_BIN)

$(MACVIM_BIN): vim
	cd src/MacVim && xcodebuild
	touch $(MACVIM_BIN)

vim:
	$(MAKE) -C src

# vim: set sw=2:
