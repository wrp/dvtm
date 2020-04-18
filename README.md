# mvtm - modal virtual terminal manager

mvtm is a fork of
[dvtm](http://www.brain-dump.org/projects/dvtm/)
bringing modes to the terminal manager.

## Quickstart

mvtm is a window manager.  When you start mvtm, you get a single window
with a pty running your default SHELL.  To send commands to mvtm,
enter the `MOD` keysequence, which by default is `CTRL+g`.  Hitting
`MOD` puts you in `command` mode, in which key sequences are interpreted
by mvtm to manipulate the windows.  To transition back to `keypress`
mode, you may press `RETURN`, `MOD`, or `ESC`.  Pressing `ESC` or
`RETURN` transitions mode without sending a key to the underlying pty,
while pressing `MOD` transitions and sends the keystroke.  To quit, use
`qq` from command mode.

### Windows

New windows are created in `command` mode with `c` and closed with `xx`.
To switch among the windows use `j`, `k`, and `1`, `2`, etc.
