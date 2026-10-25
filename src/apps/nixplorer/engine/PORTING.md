# Netzhaut source port status

**Incomplete. Network page loading, styling, layout, rendering and script
execution are not available in Nixel.** The restored browser chrome runs original
engine bring-up checks, including a real HTML document parse. No native parser,
DOM, style, layout or renderer is retained as a fallback.

## Original source and recovered dependency

All 831 supplied `bin/` and `lib/` files are present. 732 are byte-for-byte
unchanged; 99 have recorded compatibility fixes. Of those, 72 only restore the
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

In addition to the 72 mechanical object-type corrections, the other 27 files
contain these changes (some also correct object-type references):

| Sources | Changes |
| --- | --- |
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

- `Runtime.c`: aligned 1,280 KiB guest process allocator (1,536 KiB in 64-bit host tests) and libc allocation/memory/string
  adapters, exit and logging. Original `nh-core/System/Memory.c` remains the
  engine-facing implementation. Logging is disabled by default and can be
  enabled with `nh_nixel_set_logging`.
- `StaticLoader.c`: resolve original DOM/HTML object initializers from a static
  symbol table through the original loader API. No dynamic linker is needed for
  the currently compiled modules. Unregistered symbols return the original
  missing-symbol result; this does not provide the full desktop module loader.
- `Format.c`: integer/string formatted output used by original object creation
  and diagnostics. Floating formats/streams remain unsupported; unknown format
  conversions return `-1`.
- `pack_unicode.py` and `UnicodeData.c`: emit all 34,278 original fixed-width
  records into the 8,740,890-byte `UNICODE.DAT`; read records using
  `sys_fs_read_at`. Cached record pointers remain stable for the allocator
  lifetime. No ASCII-only substitute or reduced Unicode dataset is used.
  The existing generated range handling still has upstream limitations for
  ranges represented only by Unicode First/Last records.
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
  the original document/element interfaces and verifies the `html`/`head`/`body`
  tree and Unicode text produced by the original tokenizer/tree constructor.
- `../browser/main.c`: restored original browser chrome with URL editing,
  cursor, Go button, inset page/status areas, default address, launch arguments,
  and `Ready`/editing/error status. Window mouse events are already discrete
  clicks; repeated clicks remain actionable. Enter/Go currently report that
  loading is unavailable rather than invoking the deleted native engine.
  `platform/tcp.*` remains reusable, currently unlinked OS transport integration.
  `paint/png.*` remains shared with the kernel.

Do not reset the arena while registry objects or cached descriptions are live.
Registry/object teardown and full navigation lifecycle are still unfinished.
The complete runtime needs a larger heap design before all interfaces/documents
can be retained. The current application mapping remains 2 MiB.

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
  host check uses 1,511,168 bytes of arena storage for retained interfaces/trees.
  Registry allocations intentionally remain live until process exit; this is
  not a completed object-lifecycle test.
- `make browser.bin`: passes APP1 and loader memory checks; code/data/BSS occupy
  1,788,416 bytes. Original HTML parsing/tree-construction is now reached and
  linked, together with its original CSS parsing dependencies.
- `make os.img` and QEMU: guest loads `UNICODE.DAT`; the window displays
  `Ready` after original core, DOM and HTML document checks complete. The
  restored URL/status interface is tested through keyboard and mouse input.
- `make check-netzhaut`: eight of ten original entry-path syntax probes pass.
  HTML parser, tree construction, DOM document, CSS tokenization/rule parsing,
  URL parser, WebIDL interface utility and ECMAScript evaluator probes pass.
  Layout and renderer still fail on graphics types/dependencies. A syntax pass
  is not proof of full linkage or feature correctness.

## Remaining integration

1. Extend original document/interface initialization beyond the tested document,
   including registry coverage, memory growth, teardown and resource/navigation
   lifecycle. The original HTML parsing path is now executing for the fixture.
2. Port the original CSS interface/cascade/layout pipeline and its math,
   scheduling and font dependencies. Do not replace it with native layout.
3. Adapt original graphics/rendering interfaces. OpenGL types, GLX/loaders,
   Vulkan/volk, FreeType/font libraries and stb dependencies remain unresolved.
4. Complete original resource loading/networking, original URL integration,
   ECMAScript bindings/execution, events and navigation lifecycle.

Other stale/missing includes and unfinished upstream algorithms remain visible
in the audit. The full registry, arbitrary-page HTML processing, CSS/layout, rendering and
ECMAScript bindings must not be advertised as completed features yet. The
original HTML parser still contains unfinished branches (including character
reference/foster-parenting paths that call `exit`); these have not been replaced
with a simplified parser.
