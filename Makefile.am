
bin_PROGRAMS = mvtm mvtm-editor
bin_SCRIPTS = dvtm-status dvtm-pager
man1_MANS = dvtm.1 dvtm-editor.1 dvtm-pager.1
AM_CPPFLAGS = -D_DARWIN_C_SOURCE -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
	-D_XOPEN_SOURCE_EXTENDED
LIBS = -lncursesw -lutil

CLEANFILES = *.gcda *.gcno *.gcov

EXTRA_DIST = version build-aux/package-version dvtm.txt dvtm.1
.PHONY: version
version:
	git describe --tags --always --dirty > version

dvtm.1: dvtm.txt
	asciidoctor -a ver=${PACKAGE_VERSION} -b manpage $<

mvtm_SOURCES = mvtm.c vt.c wstack.c keybindings.c
