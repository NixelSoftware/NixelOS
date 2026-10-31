# IRC on Nixel

Build from the repository root with `make irc.bin`, or `make` to include
`/usr/bin/irc.bin` in the OS image. Run `irc` from the Nixel terminal or open
`irc.bin` in Files. The client opens its own window and releases the terminal.

Start with:

```
/nick yournick
/server irc.libera.chat 6667
/join #yourchannel
```

The client joins #nixel after server registration. Type a message and press
Enter to send to the target shown in the header. `/join` switches the target
when the server confirms the join. `/target #channel` switches between joined
channels; `/target nick` selects a private conversation. Incoming messages
show their destination. `/msg nick message` sends a private message without
switching targets. `/nick` also handles nickname collisions during registration.

Use `/help` for commands, `/disconnect` to disconnect, `/connect` to reconnect,
and `/quit` or the window close button to exit. Left/Right/Home/End, Backspace
and Delete edit input; Up recalls the previous input; Escape clears it.
Page Up/Down browse 256 rows of scrollback.

Networking uses Nixel's asynchronous DNS and TCP syscalls. Only plain IRC TCP
is supported; TLS (usually port 6697), SASL, and persistent settings are not
implemented. Set up networking in Nixel before connecting.

`make test-irc` runs host tests with address/undefined behavior sanitizers for
registration, stream framing, PING, channel changes, private messages,
oversized lines, input editing, scrollback and connection timeout.

The original POSIX ircy sources remain under `src/` and `include/`; the local
Makefile builds that host version. Nixel builds `main.c` and `entry.asm` using
the repository's root Makefile.
