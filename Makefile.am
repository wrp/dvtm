
bin_PROGRAMS = mvtm mvtm-editor
bin_SCRIPTS = dvtm-status
man1_MANS = mvtm.1 mvtm-editor.1
AM_CPPFLAGS = -D_DARWIN_C_SOURCE -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
	-D_XOPEN_SOURCE_EXTENDED
LIBS = -lncursesw -lutil

CLEANFILES = *.gcda *.gcno *.gcov version *.1

EXTRA_DIST = build-aux/package-version \
	mvtm.txt mvtm.1 \
	mvtm-editor.txt mvtm-editor.1 \
	version

version:
	git describe --tags --always --dirty > version

mvtm.1: mvtm.txt
	asciidoctor -a ver=${PACKAGE_VERSION} -b manpage $<

mvtm-editor.1: mvtm-editor.txt
	asciidoctor -a ver=${PACKAGE_VERSION} -b manpage $<

mvtm_SOURCES = mvtm.c vt.c wstack.c keybindings.c
