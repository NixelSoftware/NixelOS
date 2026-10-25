#include "../engine/lib/nh-dom/Interfaces/DOM.h"
#include "../engine/lib/nh-dom/Interfaces/Event.h"
#include "../engine/lib/nh-dom/Interfaces/Attr.h"
#include "../engine/lib/nh-dom/Interfaces/NodeList.h"
#include "../engine/lib/nh-html/Engine/DocumentContext.h"
#include "../engine/lib/nh-api/nh-html.h"
#include "../engine/lib/nh-css/Parser/Parser.h"
#include "../engine/lib/nh-css/Properties/Match.h"
#include "../engine/platform/netzhaut/Runtime.h"
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#define OK(x) assert((x)==NH_API_SUCCESS)
#define NODE(x) NH_WEBIDL_GET_DOM_NODE(x)
static nh_encoding_UTF8String str(const char *value) {
    nh_encoding_UTF8String s=nh_encoding_initUTF8(32);
    OK(nh_core_appendToString(&s,(char *)value,strlen(value)));return s;
}
static nh_webidl_Object *text(nh_webidl_Object *doc,const char *value) {
    nh_encoding_UTF8String s=str(value);nh_webidl_Object *t=nh_dom_createText(s,doc);
    nh_core_freeString(&s);assert(t);return t;
}
static void content(nh_webidl_Object *node,const char *expected) {
    nh_encoding_UTF8String s;OK(nh_dom_getTextContent(node,&s));
    assert(s.length==strlen(expected)&&!memcmp(s.p,expected,s.length));nh_core_freeString(&s);
}
static bool selector_matches(nh_webidl_Object *element,const char *selector) {
    bool match=false;OK(nh_dom_matches(element,selector,&match));return match;
}
static unsigned trace[32],count;
static void listener(nh_dom_Event *event,void *user) {
    unsigned id=(unsigned)(uintptr_t)user;
    assert(event->currentTarget&&event->phase);
    trace[count++]=id;
    if(id==3||id==7)nh_dom_preventDefault(event);
}
void check_dom(void) {
    nh_webidl_Object *doc=nh_dom_createDocument(),*root=nh_dom_createElementFromName(doc,"main");
    assert(doc&&root);OK(nh_dom_appendToNode(NODE(doc),root));
    nh_webidl_Object *a=nh_dom_createElementFromName(doc,"p"),*b=nh_dom_createElementFromName(doc,"span");
    assert(a&&b);OK(nh_dom_appendToNode(NODE(root),a));OK(nh_dom_appendToNode(NODE(root),b));
    assert(nh_dom_getFirstChild(root)==a&&nh_dom_getNextSibling(a)==b&&nh_dom_getPreviousSibling(b)==a);
    assert(nh_dom_isConnected(b)&&nh_dom_contains(doc,b)&&nh_dom_getRootNode(b)==doc);
    assert(nh_dom_appendToNode(NODE(a),root)==NH_API_ERROR_BAD_STATE);
    assert(nh_dom_appendToNode(NODE(doc),b)==NH_API_ERROR_BAD_STATE);
    assert(nh_dom_getLastChild(root)==b); /* Rejections do not detach the node. */
    OK(nh_dom_insertBefore(root,b,a));assert(nh_dom_getFirstChild(root)==b&&nh_dom_getLastChild(root)==a);
    OK(nh_dom_removeChild(root,b));assert(!nh_dom_isConnected(b)&&!nh_dom_getParentNode(NODE(b)));
    assert(!nh_dom_getParentElement(NODE(b))&&!nh_dom_getPreviousSibling(b));
    OK(nh_dom_replaceChild(root,b,a));assert(nh_dom_getFirstChild(root)==b&&!nh_dom_isConnected(a));
    nh_webidl_Object *fragment=nh_dom_createDocumentFragment(doc),*one=text(doc,"one"),*two=text(doc,"two");
    assert(fragment&&nh_dom_getNodeType(fragment)==11);
    OK(nh_dom_appendToNode(NODE(fragment),one));OK(nh_dom_appendToNode(NODE(fragment),two));
    OK(nh_dom_insertBefore(root,fragment,b));assert(!nh_dom_getFirstChild(fragment)&&nh_dom_getFirstChild(root)==one);
    assert(nh_dom_getNextSibling(one)==two&&nh_dom_getNextSibling(two)==b);
    nh_encoding_UTF8String id=str("target"),classes=str("card  selected\twide");
    OK(nh_dom_setAttribute(b,"id",id));OK(nh_dom_setAttribute(b,"class",classes));
    assert(nh_dom_getElementById(doc,"target")==b&&nh_dom_classListContains(b,"selected"));
    OK(nh_dom_classListToggle(b,"selected",false));assert(!nh_dom_classListContains(b,"selected"));
    OK(nh_dom_classListToggle(b,"small",true));assert(nh_dom_classListContains(b,"small"));
    assert(nh_dom_classListToggle(b,"bad token",true)==NH_API_ERROR_BAD_STATE);
    nh_core_List matches=nh_core_initList(8);OK(nh_dom_getElementsByTagName(doc,"span",&matches));assert(matches.size==1&&matches.pp[0]==b);
    nh_core_freeList(&matches,false);matches=nh_core_initList(8);
    OK(nh_dom_getElementsByClassName(doc,"card",&matches));assert(matches.size==1);nh_core_freeList(&matches,false);
    nh_encoding_UTF8String changed=str("changed");OK(nh_dom_setAttribute(b,"id",changed));
    assert(!nh_dom_getElementById(doc,"target")&&nh_dom_getElementById(doc,"changed")==b);
    nh_webidl_Object *attr=nh_dom_getAttrByLocalName(NH_WEBIDL_GET_DOM_ELEMENT(b),"id");
    OK(nh_dom_removeAttribute(b,"id"));assert(!nh_dom_getAttribute(b,"id")&&!nh_dom_getAttrElement(attr));
    nh_core_freeString(&id);nh_core_freeString(&classes);nh_core_freeString(&changed);
    content(root,"onetwo");OK(nh_dom_normalize(root));content(root,"onetwo");
    assert(nh_dom_getNextSibling(nh_dom_getFirstChild(root))==b);
    nh_encoding_UTF8String value=str("A\xf0\x9f\x98\x80\xd8\xa8Z");
    OK(nh_dom_setTextContent(b,value));nh_core_freeString(&value);
    nh_webidl_Object *t=nh_dom_getFirstChild(b);assert(nh_dom_getCharacterDataLength(t)==5);
    nh_encoding_UTF8String sub;OK(nh_dom_substringCharacterData(t,1,2,&sub));
    assert(sub.length==4&&!memcmp(sub.p,"\xf0\x9f\x98\x80",4));nh_core_freeString(&sub);
    value=str("B");OK(nh_dom_replaceCharacterData(t,1,2,value));nh_core_freeString(&value);content(t,"AB\xd8\xa8Z");
    assert(nh_dom_getCharacterDataLength(t)==4);
    nh_webidl_Object *tail=nh_dom_splitText(t,2);assert(tail&&nh_dom_getNextSibling(t)==tail);content(t,"AB");content(tail,"\xd8\xa8Z");
    OK(nh_dom_normalize(b));content(b,"AB\xd8\xa8Z");assert(!nh_dom_getNextSibling(t));
    nh_webidl_Object *copy=nh_dom_cloneNode(root,doc,true);assert(copy&&copy!=root);content(copy,"onetwoAB\xd8\xa8Z");
    nh_webidl_Object *other=nh_dom_createDocument();assert(other);OK(nh_dom_adoptNode(other,b));
    assert(!nh_dom_isConnected(b)&&nh_dom_getNodeDocument(NODE(t))==other);
    value=str("CDATA");nh_webidl_Object *cdata=nh_dom_createCDATASection(other,value);nh_core_freeString(&value);
    assert(cdata&&nh_dom_getNodeType(cdata)==4);content(cdata,"CDATA");
    value=str("data");nh_webidl_Object *pi=nh_dom_createProcessingInstruction(other,"test",value);nh_core_freeString(&value);
    assert(pi&&nh_dom_getNodeType(pi)==7);content(pi,"data");
    OK(nh_dom_appendToNode(NODE(root),b));
    OK(nh_dom_addEventListener(doc,"test",listener,(void *)5,true,false,false));
    OK(nh_dom_addEventListener(root,"test",listener,(void *)1,true,false,false));
    OK(nh_dom_addEventListener(b,"test",listener,(void *)2,true,false,false));
    OK(nh_dom_addEventListener(b,"test",listener,(void *)3,false,true,false));
    OK(nh_dom_addEventListener(root,"test",listener,(void *)4,false,false,false));
    assert(selector_matches(b,"main span")&&selector_matches(b,"main > span"));
    assert(!selector_matches(root,"main span")&&selector_matches(b,".card.small"));
    assert(selector_matches(b,"[class]")&&selector_matches(b,"[class~=card]")&&selector_matches(b,"span:last-child"));
    nh_webidl_Object *em=nh_dom_createElementFromName(doc,"em");assert(em);OK(nh_dom_appendToNode(NODE(b),em));
    assert(selector_matches(em,"span > em")&&!selector_matches(em,"main > em")&&selector_matches(em,"main em"));
    assert(nh_dom_querySelector(doc,"main > span.card")==b);
    matches=nh_core_initList(8);OK(nh_dom_querySelectorAll(doc,"main span, em",&matches));assert(matches.size==2);nh_core_freeList(&matches,false);
    size_t query_heap=nh_nixel_heap_used();
    for(unsigned i=0;i<20;i++)assert(nh_dom_querySelector(doc,"main > span.card")==b);
    assert(nh_nixel_heap_used()==query_heap);
    nh_webidl_Object *event=nh_dom_createEvent("test",true,true);assert(event);bool accepted=true;count=0;
    OK(nh_dom_dispatchEvent(b,event,&accepted));unsigned expected[]={5,1,2,3,4};assert(count==5&&!memcmp(trace,expected,sizeof(expected))&&!accepted);
    count=0;OK(nh_dom_dispatchEvent(b,event,&accepted));assert(count==4&&trace[3]==4);
    nh_dom_removeEventListener(b,"test",listener,(void *)2,true);
    OK(nh_dom_addEventListener(b,"passive",listener,(void *)7,false,false,true));
    event=nh_dom_createEvent("passive",false,true);count=0;OK(nh_dom_dispatchEvent(b,event,&accepted));assert(count==1&&accepted);
    nh_api_DocumentContext *context=nh_api_createDocumentContext(false);assert(context);
    char html[]="<!doctype html><html><head><script>if (1 < 2) x=3;</script><template id='tpl'><p>Hidden</p></template></head><body><table id='table'><tr><td>A &amp; B<td>&#x628;</tr></table><select><optgroup label='x'><option>One<option>Two</optgroup></select><p id='after'>After &NotEqualTilde; &copy; &bogus; &</p></body></html>";
    OK(nh_api_loadBytes(context,html,sizeof(html)-1));
    nh_webidl_Object *page=((nh_html_DocumentContext *)context)->Document_p;
    nh_webidl_Object *table=nh_dom_getElementById(page,"table");assert(table);
    matches=nh_core_initList(8);OK(nh_dom_getElementsByTagName(table,"td",&matches));assert(matches.size==2);
    content(matches.pp[0],"A & B");content(matches.pp[1],"\xd8\xa8");nh_core_freeList(&matches,false);
    matches=nh_core_initList(8);OK(nh_dom_getElementsByTagName(page,"option",&matches));assert(matches.size==2);content(matches.pp[0],"One");content(matches.pp[1],"Two");nh_core_freeList(&matches,false);
    nh_webidl_Object *tpl=nh_dom_getElementById(page,"tpl");assert(tpl&&tpl->Attributes.pp[0]);content(tpl,"");content(tpl->Attributes.pp[0],"Hidden");
    content(nh_dom_getElementById(page,"after"),"After \xe2\x89\x82\xcc\xb8 \xc2\xa9 &bogus; &");
    char foreign[]="<!doctype html><html><head></head><body><!--Comment--><svg id='svg'><text><![CDATA[foreign text]]></text><circle/></svg><img><p>Last</p></body></html>";
    OK(nh_api_loadBytes(context,foreign,sizeof(foreign)-1));page=((nh_html_DocumentContext *)context)->Document_p;
    nh_webidl_Object *svg=nh_dom_getElementById(page,"svg");assert(svg);content(svg,"foreign text");
    matches=nh_core_initList(8);OK(nh_dom_getElementsByTagName(page,"body",&matches));assert(matches.size==1);
    nh_webidl_Object *comment=nh_dom_getFirstChild(matches.pp[0]);assert(nh_dom_getNodeType(comment)==8);content(comment,"Comment");nh_core_freeList(&matches,false);
    char eof[]="<p>&amp;&#128;&#0;&";OK(nh_api_loadBytes(context,eof,sizeof(eof)-1));page=((nh_html_DocumentContext *)context)->Document_p;
    matches=nh_core_initList(8);OK(nh_dom_getElementsByTagName(page,"p",&matches));assert(matches.size==1);content(matches.pp[0],"&\xe2\x82\xac\xef\xbf\xbd&");nh_core_freeList(&matches,false);
    char formatting[]="<p><b><i>A</b>B</i></p><b>before<div>inside</b>after</div>";
    OK(nh_api_loadBytes(context,formatting,sizeof(formatting)-1));page=((nh_html_DocumentContext *)context)->Document_p;
    matches=nh_core_initList(8);OK(nh_dom_getElementsByTagName(page,"p",&matches));assert(matches.size==1);content(matches.pp[0],"AB");nh_core_freeList(&matches,false);
    matches=nh_core_initList(8);OK(nh_dom_getElementsByTagName(page,"div",&matches));assert(matches.size==1);content(matches.pp[0],"insideafter");nh_core_freeList(&matches,false);
    puts("DOM mutation, fragments, Unicode data, attributes, queries, events and expanded HTML parsing passed.");
}
