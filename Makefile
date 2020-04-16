include config.mk

SRC = dvtm.c vt.c
BIN = dvtm dvtm-status dvtm-editor dvtm-pager
MANUALS = dvtm.1 dvtm-editor.1 dvtm-pager.1

VERSION = $(shell GIT_CEILING_DIRECTORIES=$$(cd ..; pwd -P) git describe --always --dirty 2>/dev/null || \
	echo "0.15-g$Format:%h$ " )
CFLAGS += -DVERSION=\"${VERSION}\"
DEBUG_CFLAGS = ${CFLAGS} -UNDEBUG -O0 -g -ggdb -Wall -Wextra -Wno-unused-parameter

all: dvtm dvtm-editor

config.h: config.def.h
	cp config.def.h config.h

dvtm: config.h config.mk *.c *.h wstack.o
	${CC} ${CFLAGS} ${SRC} ${LDFLAGS} ${LIBS} wstack.o -o $@

dvtm-editor: dvtm-editor.c
	${CC} ${CFLAGS} $^ ${LDFLAGS} -o $@

man:
	@for m in ${MANUALS}; do \
		echo "Generating $$m"; \
		sed -e "s/VERSION/${VERSION}/" "$$m" | mandoc -W warning -T utf8 -T xhtml -O man=%N.%S.html -O style=mandoc.css 1> "$$m.html" || true; \
	done

dvtm.1: dvtm.txt
	asciidoctor -a version=${VERSION} -b manpage $<

debug: clean
	@$(MAKE) CFLAGS='${DEBUG_CFLAGS}'

clean:
	rm -f dvtm
	rm -f dvtm-editor
	rm -f *.o

dist: clean
	git archive --prefix=dvtm-${VERSION}/ -o dvtm-${VERSION}.tar.gz HEAD

install: all
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@for b in ${BIN}; do \
		echo "installing ${DESTDIR}${PREFIX}/bin/$$b"; \
		cp -f "$$b" "${DESTDIR}${PREFIX}/bin" && \
		chmod 755 "${DESTDIR}${PREFIX}/bin/$$b"; \
	done
	@echo installing manual page to ${DESTDIR}${MANPREFIX}/man1
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@for m in ${MANUALS}; do \
		sed -e "s/VERSION/${VERSION}/" < "$$m" >  "${DESTDIR}${MANPREFIX}/man1/$$m" && \
		chmod 644 "${DESTDIR}${MANPREFIX}/man1/$$m"; \
	done
	@echo installing terminfo description
	@TERMINFO=${TERMINFO} tic -s dvtm.info

uninstall:
	@for b in ${BIN}; do \
		echo "removing ${DESTDIR}${PREFIX}/bin/$$b"; \
		rm -f "${DESTDIR}${PREFIX}/bin/$$b"; \
	done
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/dvtm.1

.PHONY: all clean dist install uninstall debug
