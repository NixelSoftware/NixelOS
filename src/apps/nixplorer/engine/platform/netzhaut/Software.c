/* Software drawing backend for the original CSS fragment tree. */
#include "Render.h"
#include "../../lib/nh-css/Main/Canvas.h"
#include "../../lib/nh-css/Layout/BoxModel.h"
static void rectangle(const nh_nixel_DrawTarget *t,int x,int y,int w,int h,nh_css_Color color) {
    y-=t->scroll;
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
        for(unsigned i=0;i<f->Text.length;i++) {
            int gx=x+(int)i*advance, gy=y-t->scroll;
            if(gx>=0&&gx+advance<=t->width&&gy>=0&&gy+size<=t->height)
                t->glyph(t->user,gx,gy,size,f->Text.p[i],f->Text.Values.Color);
        }
    }
    for(unsigned i=0;i<f->Children.size;i++) {
        NH_CORE_CHECK(fragment(t,f->Children.pp[i],depth+1))
    }
    return NH_API_SUCCESS;
}
NH_API_RESULT nh_nixel_drawFragmentTree(nh_css_LayoutEngine *engine,const nh_nixel_DrawTarget *target) {
    if(!engine||!target||!target->rectangle||!target->glyph) return NH_API_ERROR_NULL_POINTER;
    if(target->width<=0||target->height<=0) return NH_API_ERROR_BAD_STATE;
    bool idle=true;
    NH_CORE_CHECK(nh_css_updateLayout(engine,&idle))
    if(!engine->Layout_p)return NH_API_ERROR_BAD_STATE;
    for(unsigned i=0;i<engine->Layout_p->Canvases.size;i++) {
        nh_css_Canvas *c=engine->Layout_p->Canvases.pp[i];
        if(c->Type.Size.width==target->width&&c->Type.Size.height==target->height)
            return fragment(target,c->FragmentTree.Root_p,0);
    }
    return NH_API_ERROR_BAD_STATE;
}
