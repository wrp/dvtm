/* Invoke $EDITOR as a filter.
 *
 * Copyright (c) 2016 Dmitry Bogatov <KAction@gnu.org>
 * Copyright (c) 2017 Marc André Tanner <mat@brain-dump.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int tmp_fd = -1;
char tempname[PATH_MAX];
void (*stop)(int) = exit;

static void
cleanup(void)
{
	if( tmp_fd != -1 && unlink(tempname) == -1) {
		perror(tempname);
	}
}

static void
error(int errnum, const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);
	if( errnum ) {
		fprintf(stderr, ": %s", strerror(errnum));
	}
	fputc('\n', stderr);
	cleanup();
	stop(EXIT_FAILURE);
}

const char *
get_default_editor(void)
{
	const char *editor = getenv("MVTM_EDITOR");
	if( editor == NULL ) {
		editor = getenv("DVTM_EDITOR");
	}
	if( editor == NULL ) {
		editor = getenv("VISUAL");
	}
	if( editor == NULL ) {
		editor = getenv("EDITOR");
	}
	if( editor == NULL ) {
		editor = "vi";
	}
	return editor;
}

static void
build_template(char *name, size_t s)
{
	char *template = "mvtm-editor.XXXXXX";
	char *tmpdir = getenv("TMPDIR");
	int length = 0;

	if( tmpdir ) {
		size_t cap = s - sizeof template - 2;
		length = snprintf(name, cap, "%s", tmpdir);
		if( length >= cap ) {
			error(0, "TMPDIR too long!  Unable to build tmpfile name.");
		}
		if( name[length - 1] != '/' ) {
			name[length++] = '/';
		}
	}
	name[length] = '\0';
	strncat(name + length, template, s - length);
}


int
main(int argc, char *argv[])
{
	char buffer[BUFSIZ];
	ssize_t bytes;
	struct stat stat_before;
	const char *editor = get_default_editor();

	build_template(tempname, sizeof tempname);
	if( (tmp_fd = mkstemp(tempname)) == -1 ) {
		error(errno, "failed mkstemp %s", tempname);
	}
	if( fchmod(tmp_fd, 0600) == -1 ) {
		error(errno, "failed chmod %s", tempname);
	}
	while( (bytes = read(STDIN_FILENO, buffer, sizeof buffer )) > 0 ) {
		if( bytes < 0 ) {
			error(errno, "failed to read from stdin");
		}
		do {
			ssize_t written = write(tmp_fd, buffer, bytes);
			if( written == -1 ) {
				error(errno, "failed to write to %s", tempname);
			}
			bytes -= written;
		} while (bytes > 0);
	}
	if( fsync(tmp_fd) == -1 ) {
		error(errno, "failed to fsync %s", tempname);
	}
	if( fstat(tmp_fd, &stat_before) == -1 ) {
		error(errno, "failed to stat %s", tempname);
	}
	if( close(tmp_fd) == -1 ) {
		error(errno, "failed to close %s", tempname);
	}
	pid_t pid = fork();
	if( pid == -1 ) {
		error(errno, "failed to fork editor process");
	} else if( pid == 0 ) {
		stop = _exit;
		tmp_fd = -1;
		int tty = open("/dev/tty", O_RDWR);
		if( tty == -1 ) {
			error(errno, "failed to open /dev/tty");
		}
		if( dup2(tty, STDIN_FILENO) == -1 ) {
			error(errno, "failed to set tty as stdin");
		}
		if( dup2(tty, STDOUT_FILENO) == -1 ) {
			error(errno, "failed to set tty as stdout");
		}
		if( dup2(tty, STDERR_FILENO) == -1 ) {
			error(errno, "failed to set tty as stderr");
		}
		if( close(tty) == -1 ) {
			error(errno, "failed to close tty");
		}
		const char *editor_argv[argc + 2];
		editor_argv[0] = editor;
		for( int i = 1; i < argc; i++ ) {
			editor_argv[i] = argv[i];
		}
		editor_argv[argc] = tempname;
		editor_argv[argc + 1] = NULL;

		execvp(editor, (char* const*)editor_argv);
		error(errno, "failed to exec %s", editor);
		_exit(EXIT_FAILURE);
	}
	int status;
	if( waitpid(pid, &status, 0 ) == -1 ) {
		error(errno, "waitpid failed");
	}
	if( WIFSIGNALED(status) ) {
		error(0, "editor %s (pid %ld) terminated by signal %d",
			editor, (long)pid, WTERMSIG(status));
	}
	if( !WIFEXITED(status) ) {
		error(0, "unexpedted editor invocation failure");
	}
	if( (status = WEXITSTATUS(status)) != 0 ) {
		error(0, "editor terminated with exit status: %d", status);
	}
	int tmp_read = open(tempname, O_RDONLY);
	if( tmp_read == -1 ) {
		error(errno, "failed to open for reading %s", tempname);
	}
	struct stat stat_after;
	if( fstat(tmp_read, &stat_after ) == -1) {
		error(errno, "failed to stat edited temporary file %s", tempname);
	}
	if( stat_before.st_mtime != stat_after.st_mtime ) {
		while( (bytes = read(tmp_read, buffer, sizeof(buffer))) > 0 ) {
			if( bytes < 0 ) {
				error(errno, "failed reading %s", tempname);
			}
			do {
				ssize_t written = write(STDOUT_FILENO, buffer, bytes);
				if( written == -1 ) {
					error(errno, "failed to write data to stdout");
				}
				bytes -= written;
			} while( bytes > 0 );
		}
	}
	cleanup();
	return EXIT_SUCCESS;
}
