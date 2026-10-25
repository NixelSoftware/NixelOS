#ifndef NH_NIXEL_RENDER_H
#define NH_NIXEL_RENDER_H
#include "../../lib/nh-css/Main/LayoutEngine.h"
#include "../../lib/nh-css/Properties/Color.h"
/* The backend consumes positioned CSS fragments, never HTML or DOM nodes. */
typedef struct nh_nixel_DrawTarget {
    void *user;
    int width, height, scroll;
    void (*rectangle)(void *,int,int,int,int,nh_css_Color);
    void (*glyph)(void *,int,int,int,NH_ENCODING_UTF32,nh_css_Color);
} nh_nixel_DrawTarget;
NH_API_RESULT nh_nixel_load_document_styles(nh_html_DocumentContext *);
NH_API_RESULT nh_renderer_drawDocument(nh_css_LayoutEngine *, const nh_nixel_DrawTarget *);
#endif
