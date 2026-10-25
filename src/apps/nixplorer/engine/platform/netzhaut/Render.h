#ifndef NH_NIXEL_RENDER_H
#define NH_NIXEL_RENDER_H
#include "../../lib/nh-css/Main/LayoutEngine.h"
#include "../../lib/nh-css/Properties/Color.h"
/* The backend consumes positioned CSS fragments, never HTML or DOM nodes. */
typedef struct nh_nixel_DrawTarget {
    void *user;
    int width, height, scroll;
    unsigned scale_percent; /* 0 means 100%; layout uses a larger logical canvas. */
    void (*rectangle)(void *,int,int,int,int,nh_css_Color);
    void (*glyph)(void *,int,int,int,NH_ENCODING_UTF32,nh_css_Color);
} nh_nixel_DrawTarget;
nh_api_PixelSize nh_nixel_canvas_size(const nh_nixel_DrawTarget *);
NH_API_RESULT nh_nixel_load_document_styles(nh_html_DocumentContext *);
int nh_nixel_scroll_limit(nh_css_LayoutEngine *,const nh_nixel_DrawTarget *);
NH_API_RESULT nh_renderer_drawDocument(nh_css_LayoutEngine *, const nh_nixel_DrawTarget *);
#endif
