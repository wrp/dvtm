
bin_PROGRAMS = mvtm mvtm-editor
man1_MANS = mvtm.1 mvtm-editor.1
AM_CPPFLAGS = -D_DARWIN_C_SOURCE -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
	-D_XOPEN_SOURCE_EXTENDED
LIBS = -lncursesw -lutil

CLEANFILES = *.gcda *.gcno *.gcov version *.1

EXTRA_DIST = build-aux/package-version \
	mvtm.txt mvtm.1 \
	mvtm-editor.txt mvtm-editor.1 \
	version

.PHONY: version
version:
	{ cd $(top_srcdir) && build-aux/package-version; } | { read v; \
	echo "$$v" > version; \
	awk '/VERSION/{$$3 = sprintf("\"%s\"", v)}1' v=$$v config.h > config.h.new \
	&& mv config.h.new config.h; }


mvtm.1: mvtm.txt
	asciidoctor -a name=${PACKAGE} -a ver=${PACKAGE_VERSION} -b manpage $<

mvtm-editor.1: mvtm-editor.txt
	asciidoctor -a name=${PACKAGE} -a ver=${PACKAGE_VERSION} -b manpage $<

mvtm_SOURCES = mvtm.c vt.c keybindings.c command.c
