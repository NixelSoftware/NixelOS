/* Nixel bitmap-font transport for the original text segmentation/measurement
 * pipeline. The window service draws 8x16 cells; complex shaping is unavailable. */
#include "../../lib/nh-gfx/Fonts/HarfBuzz.h"
#include "../../lib/nh-core/System/Memory.h"
#include "../../lib/nh-core/Util/Array.h"
#include "../../lib/nh-core/Util/List.h"
#include <string.h>

nh_gfx_FontManager NH_GFX_FONT_MANAGER;
static nh_gfx_Font font;
static nh_gfx_FontInstance instances[65];
nh_gfx_FontInstance *nh_gfx_claimFontInstance(nh_gfx_Font *f, unsigned int size) {
    if(size<8)size=8;
    if(size>64)size=64;
    nh_gfx_FontInstance *instance=&instances[size];
    instance->Font_p=f; instance->fontSize=size; instance->ascender=size*3/4; instance->descender=-(int)size/4;
    return instance;
}
nh_core_List nh_gfx_getFonts(nh_gfx_FontFamily family, nh_gfx_FontStyle style, bool internal) {
    (void)family; (void)style; (void)internal;
    nh_core_List fonts=nh_core_initList(1);
    nh_core_appendToList(&fonts,&font);
    return fonts;
}
bool nh_gfx_hasGlyph(nh_gfx_FontInstance *f, NH_ENCODING_UTF32 codepoint) {
    (void)f; (void)codepoint; return true; /* Unsupported glyphs draw as '?'. */
}
NH_API_RESULT nh_gfx_loadGlyphs(nh_gfx_FontInstance *f, NH_ENCODING_UTF32 *text, unsigned int count) {
    (void)f; (void)text; (void)count; return NH_API_SUCCESS;
}
typedef struct { NH_ENCODING_UTF32 *text; unsigned count; int advance; } BitmapRun;
nh_gfx_HarfBuzzBuffer nh_gfx_createHarfBuzzBuffer(nh_gfx_FontInstance *f, NH_ENCODING_UTF32 *text, unsigned count) {
    BitmapRun *run=(BitmapRun *)nh_core_allocate(sizeof(*run));
    if(run) { run->text=text; run->count=count; run->advance=f->fontSize/2; }
    return (nh_gfx_HarfBuzzBuffer){run};
}
void nh_gfx_destroyHarfBuzzBuffer(nh_gfx_HarfBuzzBuffer buffer) { nh_core_free(buffer.external_p); }
nh_gfx_HarfBuzzGlyphInfo *nh_gfx_getHarfBuzzGlyphInfos(nh_gfx_HarfBuzzBuffer buffer, unsigned *count) {
    BitmapRun *run=buffer.external_p;
    *count=0;
    if(!run) return NULL;
    nh_gfx_HarfBuzzGlyphInfo *info=(nh_gfx_HarfBuzzGlyphInfo *)nh_core_allocate(run->count*sizeof(*info));
    if(!info) return NULL;
    memset(info,0,run->count*sizeof(*info));
    for(unsigned i=0;i<run->count;i++) { info[i].id=run->text[i]; info[i].xAdvance=run->advance; }
    *count=run->count;
    return info;
}
