/* Exercise the supplied HTML parser and tree constructor, never a fallback. */
#include "Probe.h"
#include "Runtime.h"
#include "../../lib/nh-api/nh-html.h"
#include "../../lib/nh-html/Engine/DocumentContext.h"
#include "WebIDL.h"
#ifdef NH_NIXEL_HOST_TEST
#include "Render.h"
#include "../../lib/nh-css/Main/Canvas.h"
static unsigned drawn, styled, background, scaled;
static void probe_rect(void *u,int x,int y,int w,int h,nh_css_Color c) {
    (void)u;
    if(x<0||y<0||x+w>460||y+h>284)return;
    if(c.r>0.85f&&c.r<0.86f&&c.g==c.r&&c.b==c.r)background++;
}
static void probe_glyph(void *u,int x,int y,int size,NH_ENCODING_UTF32 cp,nh_css_Color c) {
    (void)u;
    static const NH_ENCODING_UTF32 expected[]={'N','e','t','z','h','a','u','t',' ',0x628};
    if(drawn<10&&cp==expected[drawn]&&x>=0&&y>=0&&x+size/2<=460&&y+size<=284&&
       c.r>0.95f&&c.r<0.96f&&c.g>0.002f&&c.g<0.003f&&c.b>0.41f&&c.b<0.42f)styled++;
    if(size>16)scaled++;
    drawn++;
}
#endif
#include "../../lib/nh-html/Common/IndexMap.h"
#include "../../lib/nh-html/Interfaces/Document.h"
#include "../../lib/nh-html/Parser/Parser.h"
#include "../../lib/nh-dom/Interfaces/Document.h"
#include "../../lib/nh-dom/Interfaces/Node.h"
#include "../../lib/nh-dom/Interfaces/Element.h"
#include "../../lib/nh-dom/Interfaces/Text.h"
#include "../../lib/nh-webidl/Runtime/String.h"
#include "../../lib/nh-encoding/Encodings/UTF8.h"
#include <string.h>

static nh_webidl_Object *child_element(nh_webidl_Object *parent, const char *name) {
    nh_core_List *children=nh_dom_getNodeChildren(NH_WEBIDL_GET_DOM_NODE(parent));
    for(unsigned i=0;children && i<children->size;++i) {
        nh_webidl_Object *element=NH_WEBIDL_GET_DOM_ELEMENT(children->pp[i]);
        if(element && !strcmp(nh_dom_getLocalName(element)->p,name))return children->pp[i];
    }
    return NULL;
}
int nh_nixel_initialize_html(void) {
    const char *dom[] = {"Document", "Element", "NamedNodeMap", "Attr", "DocumentType", "Comment"};
    const char *html[] = {"Document", "HTMLElement", "HTMLHtmlElement", "HTMLHeadElement", "HTMLBodyElement"};
    for (unsigned i=0;i<sizeof(dom)/sizeof(dom[0]);++i)
        if (!nh_nixel_load_fragment("DOM",dom[i])) return 0;
    for (unsigned i=0;i<sizeof(html)/sizeof(html[0]);++i)
        if (!nh_nixel_load_fragment("HTML",html[i])) return 0;
    if (nh_html_createIndexMap()!=NH_API_SUCCESS) return 0;
    if (nh_webidl_initNamespaces()!=NH_API_SUCCESS || nh_dom_initDocumentModes()!=NH_API_SUCCESS) return 0;
    return !nh_nixel_failed();
}
int nh_nixel_probe_html(void) {
    if (!nh_nixel_initialize_html()) return 0;
    nh_api_DocumentContext *context = nh_api_createDocumentContext(false);
    if (!context) return 0;
#ifdef NH_NIXEL_HOST_TEST
    nh_css_LayoutEngine *engine=nh_css_createLayoutEngine((nh_html_DocumentContext *)context);
    if(!engine || nh_css_addCanvasType(engine,nh_css_createCanvasType((nh_api_PixelSize){460,284}))!=NH_API_SUCCESS)return 0;
#endif
    unsigned char source[] = "<!doctype html><html><head><style>body { color: #fa10ab; background-color: #eeeeee; }</style></head><body>Netzhaut \xD8\xA8</body></html>";
    if (nh_api_loadBytes(context,(char *)source,sizeof(source)-1)!=NH_API_SUCCESS) return 0;
    nh_html_DocumentContext *internal = (nh_html_DocumentContext *)context;
    nh_webidl_Object *document = internal->Document_p;
    if (!document || internal->Documents.size != 1 || internal->Documents.pp[0] != document) return 0;
#ifdef NH_NIXEL_HOST_TEST
    nh_nixel_DrawTarget target={0}; target.width=460;target.height=284;
    target.rectangle=probe_rect;target.glyph=probe_glyph;
    drawn=styled=background=0;
    if(nh_nixel_load_document_styles(internal)!=NH_API_SUCCESS)return 0;
    NH_API_RESULT render_result=nh_renderer_drawDocument(engine,&target);
    if(render_result!=NH_API_SUCCESS || drawn!=10 || styled!=10 || !background)return 0;
#endif
    nh_webidl_Object *root=child_element(document,"html");
    if(!root || !child_element(root,"head"))return 0;
    nh_webidl_Object *body=child_element(root,"body");
    if(!body)return 0;
    nh_core_List *children=nh_dom_getNodeChildren(NH_WEBIDL_GET_DOM_NODE(body));
    if(!children || children->size!=1)return 0;
    nh_webidl_Object *text=NH_WEBIDL_GET_DOM_TEXT(children->pp[0]);
    if(!text)return 0;
    nh_encoding_UTF8String *content=nh_dom_getTextString(text);
    const char expected[]="Netzhaut \xD8\xA8";
    if(!content || content->length!=sizeof(expected)-1 || memcmp(content->p,expected,sizeof(expected)-1))return 0;
#ifdef NH_NIXEL_HOST_TEST
    /* A second navigation must publish a new document and use its own layout. */
    nh_css_Layout *previous_layout=engine->Layout_p;
    if(nh_api_loadBytes(context,(char *)source,sizeof(source)-1)!=NH_API_SUCCESS ||
       internal->Documents.size!=2 || internal->Document_p==document)return 0;
    if(nh_nixel_load_document_styles(internal)!=NH_API_SUCCESS)return 0;
    drawn=styled=background=0;
    if(nh_renderer_drawDocument(engine,&target)!=NH_API_SUCCESS || engine->Layout_p==previous_layout ||
       drawn!=10 || styled!=10 || !background)return 0;
#endif
#ifdef NH_NIXEL_HOST_TEST
    char headings[]="<!doctype html><html><head></head><body><h1>Heading</h1><p>Body</p></body></html>";
    if(nh_api_loadBytes(context,headings,sizeof(headings)-1)!=NH_API_SUCCESS)return 0;
    drawn=styled=background=scaled=0;
    if(nh_renderer_drawDocument(engine,&target)!=NH_API_SUCCESS || drawn!=11 || !scaled)return 0;
#endif
    return !nh_nixel_failed();
}
