# Netzhaut source port status

**Incomplete, with HTTP → HTML → CSS layout → software drawing now integrated.**
Downloaded HTML goes through the original document API, embedded styles through
its original CSS parser, and the original cascade/source/box/fragment tree and
block/inline layout feed a Nixel drawing backend. Text, backgrounds and rectangular
borders can be drawn in the browser viewport. This is a limited engine port;
arbitrary websites, script execution and full graphics support remain unfinished.

## Original source and recovered dependency

All 831 supplied `bin/` and `lib/` files are present. 721 are byte-for-byte
unchanged; 110 have recorded compatibility fixes. Of those, 72 only restore the
`nh_webidl_Object` type name at users of the original WebIDL interface/attribute
object model. They do not replace the implementations or their algorithms.

Six original runtime files (`Object.c/.h`, `Type.c/.h`, `String.c/.h`) were
recovered from [Netzhaut commit b5e19c6e2cbafa9671cc8ab572a24d94b84f9196](https://github.com/dajofrey/Netzhaut/tree/b5e19c6e2cbafa9671cc8ab572a24d94b84f9196/src/lib/nh-webidl/Runtime).
Their original hashes and the supplied-source hashes are in `SOURCE_AUDIT.json`.
The recovered object structure matches the supplied DOM's `Interface_p`,
`Parent_p`, `Child_p`, `Attributes`, `Parts` and `internal_p` fields. It is distinct
from the newer ECMAScript `Engine/Object` structure.

The recovered `Object.c/.h` includes point to the existing `Utils/Interface.h`,
and `Type.h` includes the existing `Utils/Parser.h`. The recovered algorithmic
bodies are unchanged. A compatibility `Runtime/Runtime.h` forwards to the
snapshot's existing `Utils/Global.h`; there is no second registry implementation.

## Compatibility changes to supplied files

In addition to the 72 mechanical object-type corrections, the other 38 files
contain these changes (some also correct object-type references):

| Sources | Changes |
| --- | --- |
| `nh-api/nh-html.c` | Restore original document-context/load wrappers using static symbol resolution on Nixel and the module loader on desktop. |
| `nh-html/Engine/DocumentContext.c` | Initialize the context list, preserve the browsing-context flag, reject unsupported browsing loads and invalid inputs, propagate list failures, and release decoded input after parsing. Full document teardown remains unfinished. |
| `nh-html/Common/Log.c` | Bound indentation by its actual buffer size, avoid a negative root indentation index, bound tag formatting, and propagate recursive logging errors. |
| `nh-api/nh-network.c` | Resolve the same original network API symbols through the static loader on Nixel. Public request/response APIs remain unchanged. |
| `nh-network/HTTPClient.c` | Adapt connection/read/write/close and platform dependencies; retain original HTTP request/response/framing/streaming implementations. Reject unsupported TLS explicitly, guard size overflow, validate lengths/chunks, omit URL fragments, and normalize query-only request targets. Desktop TLS and loopback implementations remain present. |
| `nh-api/nh-core.h` | Include `stddef.h` explicitly for API `size_t`. |
| `nh-core/Util/Debug.h` | Retain original checks and result handling; use compile-time file/line diagnostics on Nixel without formatted stdio. |
| `nh-core/Util/String.c` | Fix self-recursion in `nh_core_removeFromString` by calling the original array operation. |
| `nh-webidl/Common/Includes.h` | Declare the original WebIDL object type, distinct from ECMAScript objects. |
| `nh-webidl/Utils/{Parser,Tokenizer}.c/.h` | Add counted-input entry points around the original parser/lexer. Keep string entry points as wrappers. Generated IDL byte arrays are not terminated; using `strlen` read past their end under ASan. |
| `nh-webidl/Utils/Global.c/.h` | Use generated fragment lengths; expose the original fragment-loading operation and separate registry initialization from eager loading. Retain the existing full-registry initialization path. |
| `nh-html/Common/Includes.h`, `nh-css/Common/Includes.h` | Include the original object/string definitions required by consumers. |
| `nh-html/Interfaces/Document.h`, `nh-css/Main/LayoutEngine.h` | Correct stale `Main/DocumentContext.h` paths to the supplied `Engine/DocumentContext.h`. |
| `nh-html/Parser/{Parser,Tokenizer}.c/.h` | Restore UTF-32 input/temporary-buffer types to match the original USV string operations; fix the parser declaration and function-pointer result/object types to match its definition. |
| `nh-css/Parser/Tokenizer.h/.c` | Include original UTF-32 definitions, use the token fields' existing `double` type for an absent typedef, and add byte-array pointer casts. |
| `nh-css/Parser/Parser.h`, `SelectorParser.c` | Match the original stylesheet object types in declarations and add two typed allocation casts. No CSS algorithms replaced. |
| `nh-css/Parser/TokenParser.c/.h` | Add typed casts from the byte-array API and correct the stylesheet parser declaration to match its original object-returning definition. |
| `nh-encoding/Base/UnicodeData.gen.h` | Remove a literal `\\n` accidentally present in a preprocessor directive. |
| `nh-encoding/Base/UnicodeData.gen.c` | Keep the full source table; exclude only its embedded array on Nixel, retaining the original undefined-range data. The complete array is emitted as a filesystem resource. |
| `nh-encoding/Base/UnicodeDataHelper.c` | Adapt record storage access. Fix range indexing to use the original codepoint and inclusive generated gap boundaries; the old calculation returned wrong records after gaps. Classification routines are retained. |

## Platform-specific code

- `Runtime.c`: aligned 16 MiB process allocator with a free-block list and
  adjacent-block coalescing (avoids scanning all live objects on every free) and libc allocation/memory/string
  adapters, exit and logging. The guest obtains a private heap through
  `SYS_ALLOCATE_HEAP`; the APP1 image stays in its original 2 MiB slot. Original `nh-core/System/Memory.c` remains the
  engine-facing implementation. Logging is disabled by default and can be
  enabled with `nh_nixel_set_logging`.
- `StaticLoader.c`: resolve original DOM/HTML object initializers and document APIs from a static
  symbol table through the original loader API. No dynamic linker is needed for
  the currently compiled modules. Unregistered symbols return the original
  missing-symbol result; this does not provide the full desktop module loader.
- `limits.h`: compiler-target integer limits for freestanding builds without
  desktop libc headers.
- `Format.c`: integer/string formatted output used by original object creation
  and diagnostics. Floating formats/streams remain unsupported; unknown format
  conversions return `-1`.
- `pack_unicode.py` and `UnicodeData.c`: emit all 34,278 original fixed-width
  records into the 8,740,890-byte `UNICODE.DAT`; read records using
  `sys_fs_read_at`. Cached record pointers remain stable for the allocator
  lifetime. No ASCII-only substitute or reduced Unicode dataset is used.
  The existing generated range handling still has upstream limitations for
  ranges represented only by Unicode First/Last records.
- `Network.c` and `platform/tcp.c`: adapt IPv4/DNS/TCP transport to the original
  HTTP client. Preserve partial send/read behavior, distinguish EOF from pending
  input, close failed/completed handles, normalize the kernel send acceptance
  flag to bytes written, respect its 1,448-byte send/4,096-byte receive limits,
  use the canonical IPv4 integer ABI, and enforce per-operation timeouts using
  the actual 100 Hz `SYS_GET_TICKS` clock, including tick wraparound.
  Socket slots are process-local. The API is blocking; the UI waits during a
  request. DNS timeout/handle release remains owned by the OS resolver.
- `Strings.c`, `Scan.c`, and the 64-bit conversion additions in `Convert.c`:
  freestanding libc support used by original HTTP URL/header/status/chunk code;
  no replacement HTTP parser. Only integer scanf conversions are provided.
- `Convert.c` and `Math.c`: integer conversion and x87 `pow` compatibility for
  original CSS number parsing. Comparisons against host libc/libm cover consumed
  cases, saturation and edge values; errno/floating-environment conformance and
  the rest of libm remain outside this bring-up.
- `WebIDL.c`: named-fragment loading adapter around the existing original parser
  and registry; used by both DOM and HTML initialization checks.
- `Probe.c`, `DOMProbe.c` and `HTMLProbe.c`: integration checks using original functions and
  original generated IDL fragments, not engine implementations. The DOM check
  explicitly loads NodeList, EventTarget, Node, CharacterData and Text. It does
  not claim the whole interface registry is initialized. The HTML check loads
  bytes through the public document API and verifies the `html`/`head`/`body`
  tree and Unicode text produced by the original tokenizer/tree constructor.
- `../browser/main.c`: restored original browser chrome with URL editing,
  cursor, Go button, inset page/status areas, default address, launch arguments,
  and `Ready`/editing/error status. Window mouse events are already discrete
  clicks; repeated clicks remain actionable. Enter/Go call the original
  `nh_api_performHTTPRequest`, retain bounded body bytes, pass successful
  responses to the original document-context/load API, show HTTP status/byte
  counts and parse results or transport errors, and release responses. The
  fetched HTML is not handed to an independently implemented parser or
  renderer.
  `platform/tcp.*` now supplies the active OS transport integration.
  `paint/png.*` remains shared with the kernel.

Do not reset the arena while registry objects or cached descriptions are live.
Startup initializes the partial HTML registry without retaining a fixture document.
The full HTML initializer is not called: the upstream tokenizer still has
unfinished entity paths, and its dense character-reference trie adds substantial
memory beyond the currently tested registry/layout path.
Registry/object teardown and full navigation lifecycle are still unfinished.
The APP1 image mapping remains 2 MiB; an optional separate 16 MiB process heap
now holds the engine registry, styles and documents. Retaining successive documents
is still bounded by that heap; document/tree teardown is unfinished.

## Removed native implementation

The previous `dom`, `font`, `layout`, `net`, `parser` and `style` directories and
their relocated `nh-*/Nixel` copies remain deleted, together with alternate
parser/matcher/URL branches, `nh-api/nh-nixel.h`, replacement String/UTF32 files,
and the browser's independent layout, rendering, forms and page-processing code.
Their obsolete build references and tests remain removed. No new native engine
has been introduced in this pass.

## Validation and limits

- Original-source check: all 831 supplied files and six recovered runtime files
  are present, with compatibility changes recorded.
- `make test-nixplorer`: passes ASan/UBSan using production sources, real generated
  IDL fragments, original object creation and DOM initialization. Covers list
  operations, Text → CharacterData → Node → EventTarget inheritance, Unicode
  text, stable Arabic/Latin Unicode descriptions, gap/out-of-range lookups,
  allocation alignment/failure behavior, formatted-output comparisons, integer
  conversion/libm compatibility, and original HTML document output. The 64-bit
  host check uses 9,320,928 bytes after CSS initialization and three document loads.
  Public API checks also cover null inputs, excessive lengths, context publication
  and explicit rejection of unsupported browsing-context loads.
  Renderer checks verify positioned Unicode glyph callbacks, stylesheet colors,
  repeated navigation and scaled headings/paragraphs. Allocator stress checks
  interleave allocation, reallocation and freeing, verify live data and alignment,
  and require coalescing back into a large contiguous allocation.
  Registry allocations intentionally remain live until process exit; this is
  not a completed object-lifecycle test.
- `make browser.bin`: passes APP1 and loader memory checks; code/data/BSS occupy
  652,160 bytes. Original HTML parsing/tree-construction is now reached and
  linked, together with its original CSS parsing dependencies.
- `make os.img` and headless QEMU: HTTP fixture retrieved through real guest
  networking, parsed, styled and drawn in the browser window. Pixel checks verify
  the requested `#fa10ab` text and `#eeeeee` background. The guest reaches `Ready`
  before requesting the document. `make test-nixplorer-render` reproduces this
  check using a private disk copy and a controlled local HTTP fixture, and checks
  a second navigation with different response text.
- `make check-netzhaut`: all twelve original entry-path syntax probes pass.
  HTML parser, tree construction, DOM document, CSS tokenization/rule parsing,
  URL parser, WebIDL interface utility, ECMAScript evaluator, HTTP client and
  public HTTP API probes pass.
  Layout and renderer now compile through their Nixel paths. Desktop GPU
  dependencies remain unresolved. A syntax pass
  is not proof of full linkage or feature correctness.

## Renderer integration and remaining limits

- The original `nh_css_initialize` parses the complete supplied default stylesheet.
  CSS WebIDL initializers are registered with the static loader. Missing WebIDL
  interfaces load their original generated fragment on demand.
- The original layout engine runs synchronously when the browser loads/repaints a
  document. A corrected canvas loop checks its actual bounds. No alternate DOM,
  selector, cascade, box or line-layout implementation is used.
- Embedded `<style>` text is collected from the original DOM and passed to the
  original stylesheet parser/list. Inline styles use the original cascade path.
  External stylesheet/image fetching and resource scheduling remain unconnected.
- `nh-renderer/Main/Renderer.c` selects a Nixel software backend, preserving its
  desktop GPU branch. `Software.c` traverses positioned CSS fragments, clips to
  the viewport, and emits backgrounds, rectangular borders and glyph commands.
  The browser translates linear engine colors to display colors and sends window
  drawing syscalls. Rounded borders, compositing and images are unfinished;
  partial alpha is currently drawn as opaque.
- `Font.c` adapts the original text segmentation/measurement API to Nixel's
  scalable bitmap cells (8–64 px). Original CSS line breaking uses those metrics.
  FreeType/HarfBuzz are not ported: complex shaping and non-ASCII glyph rendering
  are unavailable; unsupported glyphs display `?`.
- The process heap syscall allocates zeroed private pages at `0x40000000`, up to
  16 MiB. Kernel syscall buffer validation checks this range and its actual user
  page mappings. Allocation failure rolls back pages/tables; process destruction
  releases the heap. Existing APP1 addresses, stack and argument pages stay fixed.
  Physical aliases of the heap window are reserved so kernel frame pointers stay
  identity mapped even on systems with more than 1 GiB RAM.
- Compatibility fixes also include a root/indentation overflow in HTML logging,
  a missing terminator byte in CSS named-color computation, stale CSS object
  declarations, and separating unused GPU includes from CPU layout sources.
  Selector grammar mismatches no longer emit engine error messages for every
  skipped separator in the default stylesheet. Static-loader entries explicitly
  cover passive interfaces whose original constructors/parents initialize state;
  unknown symbols still fail resolution.

The original parser, selectors and layout contain incomplete algorithms. HTML
entity/foster-parenting and CSS pseudo-selector branches can still call `exit`.
Full document teardown, scrolling, links/forms/events, HTTPS, external resources,
GPU rendering and ECMAScript execution remain unfinished. Do not advertise full
arbitrary-site compatibility.

## HTTP networking validation and remaining limits

`make test-nixplorer` runs the original public API and HTTP client against a mock
kernel transport, with ASan/UBSan. Coverage includes partial writes/fragmented
reads, interim 1xx responses, Content-Length and chunked framing/extensions/
trailers, close-delimited and binary bodies, buffered/streaming responses,
buffered/streamed POST bodies, query targets, omitted fragments, non-default
ports and case-insensitive header lookup. Failure tests cover malformed/truncated
bodies, overflows, rejected URLs, DNS/connect/send/read failures, timeouts/tick
wraparound, callback cancellation, socket release and allocator cleanup.
Compatibility tests separately exercise the integer scanner, string helpers and
64-bit number conversion used by the 32-bit guest. The desktop branch of the
original HTTP client also passes a host syntax check with `_GNU_SOURCE`.

The Nixel path supports HTTP/1.1 over IPv4 using OS DNS or dotted IPv4 literals.
HTTPS fails before opening a socket because OpenSSL/TLS and a trust store are
not ported. IPv6 transport and listening/loopback APIs are not implemented; the
existing API wrappers return unavailable for missing listener symbols. Redirect
responses are returned to the UI, not followed automatically. The client still
requests identity content encoding. No cookies/cache/resource scheduling or
complete arbitrary-page compatibility is claimed.

QEMU guest validation also passes against a controlled virtual-network HTTP
server: two requests to `http://10.0.1.100:8080/test` produce exact `GET /test`
and `Host: 10.0.1.100:8080` headers. The server sends an interim 100 response,
then a fragmented chunked 200 response with chunk extensions and trailers. The
browser displays `HTTP 200 - 78 bytes received` on both requests. This tests
real guest syscalls and the original parser beyond mocked transport. The guest
test exposed and fixed IPv4 byte-order, send-return and read-size ABI mismatches;
regression mocks now enforce those contracts, including multi-segment uploads.
