# Netzhaut on Nixel

This is an incomplete source port of Netzhaut. **HTTP retrieval works; page
rendering remains unavailable.** The OS application currently verifies original core/encoding,
WebIDL parsing, interface-object creation, DOM lists, inherited DOM objects,
Unicode text storage, and original HTML document/tree construction. There is no native browser-engine fallback.

The supplied engine remains in `bin/` and `lib/`. Six missing original WebIDL
runtime files were recovered from upstream history; their commit and hashes are
recorded in `SOURCE_AUDIT.json`. Compatibility edits reconnect them to the
snapshot's existing interface registry. WebIDL objects remain distinct from
ECMAScript property objects; their data structures have not been merged.

The desktop `nh-browser` entrypoint initializes the core runtime, runs its
event loop, and terminates it cleanly. Set `NETZHAUT_CORE` when the core
library is not discoverable through the platform's default loader path.

Nixel's APP1 entry and window/event handling live in `../browser/`. Its bring-up
checks call the original engine implementations.

`NETZHAUT_C` lists the actual sources compiled for Nixel. The original HTML tokenizer,
tree constructor and parser now execute a document integration check. Full page
processing and rendering are still not integrated. HTTP requests use the supplied
`nh-network/HTTPClient.c` through the original public API and a Nixel transport
adapter. Its request builder, response parser, chunked framing and streaming
callbacks remain the original implementations. Linker garbage collection removes unreachable functions;
compiling a source does not mean every function in it is linked or operational.

Build and validation:

- `make os.img`: generate the complete Unicode resource, build the OS/browser,
  and include `UNICODE.DAT` in the FAT image.
- `make browser.bin`: build the diagnostic application. It requires
  `UNICODE.DAT` in the guest filesystem to run WebIDL checks.
- `make test-nixplorer`: sanitizer checks using the production sources and
  original generated IDL fragments, inherited DOM objects, Unicode, HTML
  document/tree-construction output, and the original HTTP client over a mock
  kernel transport.
- `make check-netzhaut`: verify original-source preservation and compile the
  original engine entry paths. Currently ten of twelve syntax probes pass;
  layout and renderer probes still fail on graphics dependencies.
- `python3 src/apps/nixplorer/tests/check_original.py --sources-only`: check
  preservation of supplied and recovered original source independently.

The original 34,278-record Unicode table is generated into `UNICODE.DAT`, rather
than linked as an 8.7 MiB fixed-width array into a 2 MiB application slot. The
storage adapter reads complete records through the OS filesystem and caches them
without invalidating returned pointers. The original Unicode classification
routines still consume those descriptions. The source table remains present.

See [PORTING.md](PORTING.md) for source provenance, compatibility edits,
verification results and remaining dependencies. Original license notices remain
intact; see `LICENSE.LGPL`.

The browser chrome is restored: editable URL field, Go button, inset page area,
and the original `Ready` status. Enter and Go issue HTTP requests and show the
status code and received byte count. Response bodies stream through the original client without a homemade
page renderer. HTTPS/TLS, IPv6 transport, redirects and rendering are still
unavailable. Launch arguments populate the URL field.
The status window no longer replaces the browser interface.
