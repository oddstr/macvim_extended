# vim:set ts=8 sts=8 sw=8 tw=0:
#
# �A�[�L�e�N�`���ˑ� (DOS/Windows)
#
# Last Change:	29-Nov-2003.
# Written By:	MURAOKA Taro <koron@tka.att.ne.jp>
# Maintainer:	MURAOKA Taro <koron@tka.att.ne.jp>

srcdir = .\src\				#
objdir = .\build\object\		#
outdir = .\build\			#
# Borland��make�ł͌��ɃR�����g��t���邱�Ƃōs����\���܂߂邱�Ƃ��ł���

CP = copy
MKDIR = mkdir
RM = del /F /Q
RMDIR = rd /S /Q

O		= obj
EXE		= .exe
CONFIG_DEFAULT	= compile\config_default.mk
CONFIG_IN	= compile\config.mk.in
