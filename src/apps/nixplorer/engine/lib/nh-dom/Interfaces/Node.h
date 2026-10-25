#ifndef NH_DOM_INTERFACES_NODE_H
#define NH_DOM_INTERFACES_NODE_H

#ifndef DOXYGEN_SHOULD_SKIP_THIS

/**
 * Netzhaut - Web Browser Engine
 * Copyright (C) 2022  Dajo Frey
 * Published under GNU LGPL. See Netzhaut/LICENSE.LGPL file.
 */

#include "../Common/Includes.h"

#endif

/** @addtogroup lib_nh-dom_functions
 *  @{
 */

    NH_API_RESULT nh_dom_appendToNode(
        nh_webidl_Object *Node_p, nh_webidl_Object *Child_p
    );

    NH_API_RESULT nh_dom_insertIntoNode(
        nh_webidl_Object *Node_p, nh_webidl_Object *Child_p, NH_WEBIDL_UNSIGNED_LONG index
    );

    nh_core_List *nh_dom_getNodeChildren(
        nh_webidl_Object *Node_p
    );

    NH_API_RESULT nh_dom_setNodeDocument(
        nh_webidl_Object *Node_p, nh_webidl_Object *NodeDocument_p
    );

    nh_webidl_Object *nh_dom_getNodeDocument(
        nh_webidl_Object *Node_p
    );

    NH_API_RESULT nh_dom_setSpecifiedPropertyValues(
        nh_webidl_Object *Node_p, nh_core_Array SpecifiedPropertyValues
    );

    NH_API_RESULT nh_dom_setComputedPropertyValues(
        nh_webidl_Object *Node_p, nh_core_List ComputedPropertyValues
    );

    nh_core_Array *nh_dom_getSpecifiedPropertyValues(
        nh_webidl_Object *Node_p
    );

    nh_core_List *nh_dom_getComputedPropertyValues(
        nh_webidl_Object *Node_p
    );

    bool nh_dom_getUpdateAnnotationsFlag(
        nh_webidl_Object *Node_p
    );

    NH_API_RESULT nh_dom_setUpdateAnnotationsFlag(
        nh_webidl_Object *Node_p, bool update
    );

    void nh_dom_setParentNode(
        nh_webidl_Object *Node_p, nh_webidl_Object *Parent_p
    );

    nh_webidl_Object *nh_dom_getParentNode(
        nh_webidl_Object *Node_p
    );

    nh_webidl_Object *nh_dom_getParentElement(
        nh_webidl_Object *Node_p
    );

unsigned nh_dom_getNodeType(nh_webidl_Object *);
nh_webidl_Object *nh_dom_getFirstChild(nh_webidl_Object *);
nh_webidl_Object *nh_dom_getLastChild(nh_webidl_Object *);
nh_webidl_Object *nh_dom_getPreviousSibling(nh_webidl_Object *);
nh_webidl_Object *nh_dom_getNextSibling(nh_webidl_Object *);
nh_webidl_Object *nh_dom_getRootNode(nh_webidl_Object *);
bool nh_dom_contains(nh_webidl_Object *,nh_webidl_Object *);
bool nh_dom_isConnected(nh_webidl_Object *);
NH_API_RESULT nh_dom_insertBefore(nh_webidl_Object *,nh_webidl_Object *,nh_webidl_Object *);
NH_API_RESULT nh_dom_removeChild(nh_webidl_Object *,nh_webidl_Object *);
NH_API_RESULT nh_dom_replaceChild(nh_webidl_Object *,nh_webidl_Object *,nh_webidl_Object *);
nh_webidl_Object *nh_dom_createDocumentFragment(nh_webidl_Object *);

/** @} */

#endif
