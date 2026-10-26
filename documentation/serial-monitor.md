# Local serial and audio debugging

Build with `make`, boot the updated image and run `com0` in a terminal.
It shows retained kernel serial output and follows new output. Open the sound
or MIDI player from Files, or start playback from another terminal.

COM0 is the local command name for the first UART's transmitted debug output
(conventionally COM1, port 0x3F8). It reads a 256 KiB memory capture; it does
not receive bytes from an external serial device. A serial cable is unnecessary.
Each monitor has its own cursor; older data is overwritten with an overrun notice.
Use `com0 30` to monitor for 30 seconds and then return to the shell.
Without a duration it follows continuously. Terminal printing is not fed back into
serial capture.

`[AUDIO]` records show HAL client calls and returns, selected device, arguments
and result in hexadecimal. Init's `a` is the sample rate, write's `a` is the
byte count, and OPL's `a`/`b` are the register/value. `FFFFFFFF` means -1;
a write result of zero means queue full, and a positive result is bytes accepted.
Audio syscall failures also show the owner and caller address spaces.
`[HDA]` records explain rejected PCM rates or DMA errors/timeouts.
Periodic service polling and interrupts are not individually logged.

For a failure report, include the selected device, attempted rate and the last
`[AUDIO]`/`[HDA]` lines. The monitor does not require a healthy UART, and capture
continues even when the UART transmission queue drops bytes.
