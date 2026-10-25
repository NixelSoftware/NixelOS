// LICENSE NOTICE ==================================================================================

/**
 * Netzhaut - Web Browser Engine
 * Copyright (C) 2022  Dajo Frey
 * Published under GNU LGPL. See Netzhaut/LICENSE.LGPL file.
 */

// INCLUDES ========================================================================================

#include "DocumentContext.h"

#include "../Parser/Parser.h"
#include "../Interfaces/Document.h"

#include "../../nh-core/Util/List.h"
#include "../../nh-core/System/Memory.h"

#include "../../nh-css/Main/LayoutEngine.h"
#include "../../nh-webidl/Runtime/Object.h"
#include "../../nh-encoding/Encodings/UTF8.h"

#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

// DATA ============================================================================================

nh_core_List NH_HTML_DOCUMENT_CONTEXTS;

// CREATE ==========================================================================================

static nh_html_DocumentContext nh_html_initDocumentContext()
{
    nh_html_DocumentContext DocumentContext;
    memset(&DocumentContext, 0, sizeof(nh_html_DocumentContext));
    DocumentContext.Documents = nh_core_initList(64);
    return DocumentContext;
}

nh_html_DocumentContext *nh_html_createDocumentContext(
    bool browsingContext)
{
    if (NH_HTML_DOCUMENT_CONTEXTS.chunkSize == 0) {
        NH_HTML_DOCUMENT_CONTEXTS = nh_core_initList(64);
    }
    nh_html_DocumentContext *DocumentContext_p = (nh_html_DocumentContext *)nh_core_allocate(sizeof(nh_html_DocumentContext));
    NH_CORE_CHECK_MEM_2(NULL, DocumentContext_p)

    *DocumentContext_p = nh_html_initDocumentContext();
    DocumentContext_p->browsingContext = browsingContext;

//    if (browsingContext) {
//        DocumentContext_p->BrowsingContextGroup_p = nh_html_createBrowsingContextGroup();
//        NH_CORE_CHECK_MEM(NULL, DocumentContext_p->BrowsingContextGroup_p)
//    }

    if (nh_core_appendToList(&NH_HTML_DOCUMENT_CONTEXTS, DocumentContext_p) != NH_API_SUCCESS) {
        nh_core_free(DocumentContext_p);
        return NULL;
    }

    return DocumentContext_p;
}

// LOAD ============================================================================================

NH_API_RESULT nh_html_loadBytes(
    nh_html_DocumentContext *Context_p, char *bytes_p, unsigned long long size)
{

    NH_CORE_CHECK_NULL(Context_p)

    if (!bytes_p && size) {return NH_API_ERROR_NULL_POINTER;}
    /* UTF-8 decoding and arrays use native unsigned-long lengths. */
    if (size > (unsigned long)-1 / sizeof(NH_ENCODING_UTF32)) {return NH_API_ERROR_BAD_STATE;}
    if (Context_p->browsingContext) {return NH_API_ERROR_BAD_STATE;}

    if (!Context_p->browsingContext) 
    {
        nh_encoding_UTF32String String = nh_encoding_decodeUTF8((unsigned char *)bytes_p, size, NULL);

        nh_webidl_Object *Document_p = nh_html_createDocument(Context_p);
        if (!Document_p) {
            nh_encoding_freeUTF32(&String);
            return NH_API_ERROR_MEMORY_ALLOCATION;
        }

        if (Context_p->LayoutEngine_p) 
        {
            nh_webidl_Object *Object_p = nh_webidl_getObject(Document_p, "CSS", "Document");
            NH_CORE_CHECK_NULL(Object_p)
            Object_p->internal_p = nh_core_allocate(sizeof(nh_css_Layout));
            NH_CORE_CHECK_MEM(Object_p->internal_p)
            memset(Object_p->internal_p, 0, sizeof(nh_css_Layout));
        }

        char logId_p[127] = {0};
        sprintf(logId_p, "%p", Document_p);

        NH_API_RESULT result = nh_html_parseDocument(logId_p, String, Document_p);
        nh_encoding_freeUTF32(&String);
        NH_CORE_CHECK(result)

        NH_CORE_CHECK(nh_core_appendToList(&Context_p->Documents, Document_p))
        Context_p->Document_p = Document_p;
    }

    return NH_API_SUCCESS;
}
