/* Software drawing backend for the original CSS fragment tree. */
#include "Render.h"
#include "../../lib/nh-css/Main/Canvas.h"
#include "../../lib/nh-css/Layout/BoxModel.h"
#include <limits.h>
static unsigned scale(const nh_nixel_DrawTarget *t) { return t->scale_percent?t->scale_percent:100; }
static int scaled(const nh_nixel_DrawTarget *t,int value) {
    unsigned percent=scale(t);
    unsigned magnitude=value<0?0u-(unsigned)value:(unsigned)value;
    unsigned quotient=magnitude/100;
    unsigned remainder=magnitude%100;
    unsigned limit=value<0?0x80000000u:(unsigned)INT_MAX;
    unsigned result;

    if(quotient>limit/percent)return value<0?INT_MIN:INT_MAX;
    result=quotient*percent+(remainder*percent)/100;
    if(result>limit)return value<0?INT_MIN:INT_MAX;
    if(value<0) {
        if(result==0x80000000u)return INT_MIN;
        return -(int)result;
    }
    return (int)result;
}
nh_api_PixelSize nh_nixel_canvas_size(const nh_nixel_DrawTarget *t) {
    unsigned percent=scale(t);
    if(percent<50||percent>200||t->width<=0||t->height<=0||t->width>32768||t->height>32768)
        return (nh_api_PixelSize){0,0};
    return (nh_api_PixelSize){t->width*100/(int)percent,t->height*100/(int)percent};
}
static void rectangle(const nh_nixel_DrawTarget *t,int x,int y,int w,int h,nh_css_Color color) {
    int right=scaled(t,x+w), bottom=scaled(t,y+h)-t->scroll;
    x=scaled(t,x);y=scaled(t,y)-t->scroll;
    w=right-x;h=bottom-y;
    if(color.a<=0 || x>=t->width || y>=t->height || w<=0 || h<=0) return;
    if(x<0) { w+=x; x=0; }
    if(y<0) { h+=y; y=0; }
    if(w>t->width-x) w=t->width-x;
    if(h>t->height-y) h=t->height-y;
    if(w>0&&h>0)t->rectangle(t->user,x,y,w,h,color);
}
static NH_API_RESULT fragment(const nh_nixel_DrawTarget *t, nh_css_Fragment *f,unsigned depth) {
    if(!f || depth>256) return NH_API_ERROR_BAD_STATE;
    int x=f->ContentBox.Position.x, y=f->ContentBox.Position.y;
    if(f->type==NH_CSS_FRAGMENT_BOX) {
        nh_css_BoxValues *v=&f->Box.Values;
        int px=x-v->paddingLeft, py=y-v->paddingTop;
        int w=f->ContentBox.Size.width+v->paddingLeft+v->paddingRight;
        int h=f->ContentBox.Size.height+v->paddingTop+v->paddingBottom;
        rectangle(t,px,py,w,h,v->BackgroundColor);
        rectangle(t,px-v->borderLeft,py-v->borderTop,w+v->borderLeft+v->borderRight,v->borderTop,v->BorderTopColor);
        rectangle(t,px-v->borderLeft,py+h,w+v->borderLeft+v->borderRight,v->borderBottom,v->BorderBottomColor);
        rectangle(t,px-v->borderLeft,py,v->borderLeft,h,v->BorderLeftColor);
        rectangle(t,px+w,py,v->borderRight,h,v->BorderRightColor);
    } else {
        int size=f->Text.Values.Text.Segments.length ?
            ((nh_gfx_TextSegment *)f->Text.Values.Text.Segments.p)->FontInstance_p->fontSize : 16;
        int advance=size/2;
        int display_size=scaled(t,size);
        if(display_size<8)display_size=8;
        if(display_size>64)display_size=64;
        for(unsigned i=0;i<f->Text.length;i++) {
            int gx=scaled(t,x+(int)i*advance), gy=scaled(t,y)-t->scroll;
            if(gx>=0&&gx+display_size/2<=t->width&&gy>=0&&gy+display_size<=t->height)
                t->glyph(t->user,gx,gy,display_size,f->Text.p[i],f->Text.Values.Color);
        }
    }
    for(unsigned i=0;i<f->Children.size;i++) {
        NH_CORE_CHECK(fragment(t,f->Children.pp[i],depth+1))
    }
    return NH_API_SUCCESS;
}
NH_API_RESULT nh_nixel_drawFragmentTree(nh_css_LayoutEngine *engine,const nh_nixel_DrawTarget *target) {
    if(!engine||!target||!target->rectangle||!target->glyph) return NH_API_ERROR_NULL_POINTER;
    if(target->width<=0||target->height<=0||scale(target)<50||scale(target)>200) return NH_API_ERROR_BAD_STATE;
    nh_api_PixelSize logical=nh_nixel_canvas_size(target);
    bool idle=true;
    NH_CORE_CHECK(nh_css_updateLayout(engine,&idle))
    if(!engine->Layout_p)return NH_API_ERROR_BAD_STATE;
    for(unsigned i=0;i<engine->Layout_p->Canvases.size;i++) {
        nh_css_Canvas *c=engine->Layout_p->Canvases.pp[i];
        if(c->Type.Size.width==logical.width&&c->Type.Size.height==logical.height)
            return fragment(target,c->FragmentTree.Root_p,0);
    }
    return NH_API_ERROR_BAD_STATE;
}

static int content_bottom(const nh_nixel_DrawTarget *target,nh_css_Fragment *fragment,unsigned depth) {
    if(!fragment||depth>256)return 0;
    int bottom=scaled(target,fragment->ContentBox.Position.y+fragment->ContentBox.Size.height);
    for(unsigned long i=0;i<fragment->Children.size;i++) {
        int child=content_bottom(target,fragment->Children.pp[i],depth+1);
        if(child>bottom)bottom=child;
    }
    return bottom;
}
int nh_nixel_scroll_limit(nh_css_LayoutEngine *engine,const nh_nixel_DrawTarget *target) {
    if(!engine||!engine->Layout_p||!target)return 0;
    nh_api_PixelSize logical=nh_nixel_canvas_size(target);
    for(unsigned long i=0;i<engine->Layout_p->Canvases.size;i++) {
        nh_css_Canvas *canvas=engine->Layout_p->Canvases.pp[i];
        if(canvas->Type.Size.width!=logical.width||canvas->Type.Size.height!=logical.height)continue;
        int bottom=content_bottom(target,canvas->FragmentTree.Root_p,0);
        return bottom>target->height?bottom-target->height:0;
    }
    return 0;
}
