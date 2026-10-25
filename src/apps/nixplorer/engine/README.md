# Netzhaut on Nixel

This is an incomplete source port of Netzhaut. **Page loading and rendering are
unavailable.** The OS application currently verifies original core/encoding,
WebIDL parsing, interface-object creation, DOM lists, inherited DOM objects and
Unicode text storage. There is no native browser-engine fallback.

The supplied engine remains in `bin/` and `lib/`. Six missing original WebIDL
runtime files were recovered from upstream history; their commit and hashes are
recorded in `SOURCE_AUDIT.json`. Compatibility edits reconnect them to the
snapshot's existing interface registry. WebIDL objects remain distinct from
ECMAScript property objects; their data structures have not been merged.

Nixel's APP1 entry and window/event handling live in `../browser/`. Its bring-up
checks call the original engine implementations. The upstream browser executable
entry remains its original `TODO`; it is not a working browser launcher.

`NETZHAUT_C` lists the actual sources compiled for Nixel. Original HTML tokenizer,
tree constructor and parser files now compile, but their document-processing path
is not yet integrated. Linker garbage collection removes unreachable functions;
compiling a source does not mean every function in it is linked or operational.

Build and validation:

- `make os.img`: generate the complete Unicode resource, build the OS/browser,
  and include `UNICODE.DAT` in the FAT image.
- `make browser.bin`: build the diagnostic application. It requires
  `UNICODE.DAT` in the guest filesystem to run WebIDL checks.
- `make test-nixplorer`: sanitizer checks using the production sources and
  original generated IDL fragments, including inherited DOM objects and Unicode.
- `make check-netzhaut`: verify original-source preservation and compile the
  original engine entry paths. Currently eight of ten syntax probes pass;
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
