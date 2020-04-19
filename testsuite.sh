#!/bin/sh

MOD="" # CTRL+g
ESC="" # \e
MVTM="./mvtm"
export MVTM_EDITOR="vis"
LOG="mvtm.log"
TEST_LOG="$0.log"
UTF8_TEST_URL="http://www.cl.cam.ac.uk/~mgk25/ucs/examples/UTF-8-demo.txt"

test -n "$1" && MVTM=${1}
test -x "$MVTM" || { echo "usage: $0 [path-to-mvtm]" && exit 1; } >&2

mvtm_input() {
	printf '%s' "$1"
}

mvtm_cmd() {
	printf '%s' "${MOD}$1"
	sleep 1
}

sh_cmd() {
	printf '%s\n' "$1"
	sleep 1
}

test_copymode() {
	local FILENAME="UTF-8-demo.txt"
	local COPY="$FILENAME.copy"
	[ ! -e "$FILENAME" ] && (wget "$UTF8_TEST_URL" -O "$FILENAME" > /dev/null 2>&1 || return 1)
	sleep 1
	sh_cmd "cat $FILENAME"
	mvtm_cmd 'e'
	mvtm_input "?UTF-8 encoded\n"
	mvtm_input '^kvG1k$'
	mvtm_input ":wq!\n"
	sleep 1
	sh_cmd "cat <<'EOF' > $COPY"
	mvtm_cmd 'p'
	sh_cmd 'EOF'
	while [ ! -r "$COPY" ]; do sleep 1; done;
	mvtm_input "exit\n"
	diff -u "$FILENAME" "$COPY" 1>&2
	local RESULT=$?
	rm -f "$COPY"
	return $RESULT
}

if ! which vis > /dev/null 2>&1 ; then
	echo "vis not found, skiping copymode test"
	exit 0
fi

{
	echo "Testing $MVTM" 1>&2
	$MVTM -v 1>&2
	test_copymode && echo "copymode: OK" 1>&2 || echo "copymode: FAIL" 1>&2;
} 2> "$TEST_LOG" | $MVTM -m ^g 2> $LOG

cat "$TEST_LOG" && rm "$TEST_LOG" $LOG
