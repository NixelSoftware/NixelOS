// LICENSE NOTICE ==================================================================================

/**
 * Netzhaut - Web Browser Engine
 * Copyright (C) 2022  Dajo Frey
 * Published under GNU LGPL. See Netzhaut/LICENSE.LGPL file.
 */

// INCLUDES ========================================================================================

#include "Attr.h"
#include "Node.h"

#include "../../nh-core/Util/List.h"
#include "../../nh-core/System/Memory.h"

#include "../../nh-webidl/Runtime/Object.h"
#include "../../nh-webidl/Runtime/String.h"
#include "../../nh-webidl/Runtime/Runtime.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

// FUNCTIONS =======================================================================================

NH_API_RESULT nh_dom_initializeAttr(
    nh_webidl_Object *Attr_p)
{
    Attr_p->Attributes.pp[3] = nh_core_allocate(sizeof(nh_encoding_UTF8String));
    NH_CORE_CHECK_MEM(Attr_p->Attributes.pp[3])
    *((nh_encoding_UTF8String*)Attr_p->Attributes.pp[3]) = nh_encoding_initUTF8(16);

    Attr_p->Attributes.pp[4] = nh_core_allocate(sizeof(nh_encoding_UTF8String));
    NH_CORE_CHECK_MEM(Attr_p->Attributes.pp[4])
    *((nh_encoding_UTF8String*)Attr_p->Attributes.pp[4]) = nh_encoding_initUTF8(16);

    return NH_API_SUCCESS;
}

nh_webidl_Object *nh_dom_createAttr(
    nh_webidl_Object *Document_p, nh_encoding_UTF8String *LocalName_p, nh_encoding_UTF8String *Namespace_p, 
    nh_encoding_UTF8String *NamespacePrefix_p, nh_webidl_Object *OwnerElement_p, nh_encoding_UTF8String *Value_p)
{
    NH_CORE_CHECK_NULL_2(NULL, Document_p)
    NH_CORE_CHECK_NULL_2(NULL, LocalName_p)

    nh_webidl_Object *Attr_p = (nh_webidl_Object*)nh_webidl_createObject("DOM", "Attr");
    NH_CORE_CHECK_NULL_2(NULL, Attr_p)

    ((nh_webidl_Object*)Attr_p)->Attributes.pp[0] = Namespace_p;
    ((nh_webidl_Object*)Attr_p)->Attributes.pp[1] = NamespacePrefix_p;
    ((nh_webidl_Object*)Attr_p)->Attributes.pp[2] = LocalName_p;
    nh_encoding_UTF8String *QualifiedName_p = Attr_p->Attributes.pp[3];
    if (NamespacePrefix_p && NamespacePrefix_p->length) {
        nh_core_appendToString(QualifiedName_p, NamespacePrefix_p->p, NamespacePrefix_p->length);
        nh_core_appendToString(QualifiedName_p, ":", 1);
    }
    nh_core_appendToString(QualifiedName_p, LocalName_p->p, LocalName_p->length);
    ((nh_webidl_Object*)Attr_p)->Attributes.pp[5] = OwnerElement_p;
    ((nh_webidl_Object*)Attr_p)->Attributes.pp[6] = (void*)1;

    nh_core_appendToString(((nh_webidl_Object*)Attr_p)->Attributes.pp[4], Value_p->p, Value_p->length);

    NH_CORE_CHECK_2(NULL, nh_dom_setNodeDocument(NH_WEBIDL_GET_DOM_NODE((nh_webidl_Object*)Attr_p), Document_p))

    return Attr_p;
}

nh_encoding_UTF8String *nh_dom_getAttrNamespace(
    nh_webidl_Object *Attr_p)
{
    return ((nh_webidl_Object*)Attr_p)->Attributes.pp[0];
}

nh_encoding_UTF8String *nh_dom_getAttrLocalName(
    nh_webidl_Object *Attr_p)
{
    return ((nh_webidl_Object*)Attr_p)->Attributes.pp[2];
}

void nh_dom_setAttrElement(
    nh_webidl_Object *Attr_p, nh_webidl_Object *Element_p)
{
    ((nh_webidl_Object*)Attr_p)->Attributes.pp[5] = Element_p;
    return;
}

nh_webidl_Object *nh_dom_getAttrElement(
    nh_webidl_Object *Attr_p)
{
    return ((nh_webidl_Object*)Attr_p)->Attributes.pp[5];
}

nh_encoding_UTF8String *nh_dom_getAttrValue(
    nh_webidl_Object *Attr_p)
{
    return ((nh_webidl_Object*)Attr_p)->Attributes.pp[4];
}
