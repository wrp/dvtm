
bin_PROGRAMS = tm tm-editor
man1_MANS = tm.1 tm-editor.1
AM_CPPFLAGS = -D_DARWIN_C_SOURCE -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
	-D_XOPEN_SOURCE_EXTENDED
LIBS = -lncursesw -lutil

CLEANFILES = *.gcda *.gcno *.gcov version *.1

EXTRA_DIST = build-aux/package-version \
	tm.txt tm.1 \
	tm-editor.txt tm-editor.1 \
	version

.PHONY: version
version:
	{ cd $(top_srcdir) && build-aux/package-version; } | { read v; \
	echo "$$v" > version; \
	awk '/VERSION/{$$3 = sprintf("\"%s\"", v)}1' v=$$v config.h > config.h.new \
	&& mv config.h.new config.h; }


tm.1: tm.txt
	asciidoctor -a name=${PACKAGE} -a ver=${PACKAGE_VERSION} -b manpage $<

tm-editor.1: tm-editor.txt
	asciidoctor -a name=${PACKAGE} -a ver=${PACKAGE_VERSION} -b manpage $<

tm_SOURCES = tm.c vt.c keybindings.c command.c
