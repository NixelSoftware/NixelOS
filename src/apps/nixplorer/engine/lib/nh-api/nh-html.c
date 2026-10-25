// LICENSE NOTICE ==================================================================================

/**
 * Netzhaut - Web Browser Engine
 * Copyright (C) 2022  Dajo Frey
 * Published under GNU LGPL. See Netzhaut/LICENSE.LGPL file.
 */

// INCLUDES ========================================================================================

#include "nh-html.h"
#include "nh-core.h"

#include "../nh-core/Loader/Loader.h"



#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// FUNCTIONS =======================================================================================

static void *nh_api_loadHTMLSymbol(const char *name_p)
{
#ifdef NH_NIXEL
    return nh_core_loadExistingSymbol(NH_MODULE_HTML, 0, name_p);
#else
    nh_core_Loader *Loader_p = nh_api_getLoader();
    return !Loader_p ? NULL : Loader_p->loadSymbol_f(NH_MODULE_HTML, 0, (char *)name_p);
#endif
}

nh_api_DocumentContext *nh_api_createDocumentContext(bool browsingContext)
{
    typedef nh_api_DocumentContext *(*create_f)(bool);
    create_f create = nh_api_loadHTMLSymbol("nh_html_createDocumentContext");
    return create ? create(browsingContext) : NULL;
}

NH_API_RESULT nh_api_loadBytes(
    nh_api_DocumentContext *Context_p, char *bytes_p, unsigned long long size)
{
    typedef NH_API_RESULT (*load_f)(nh_api_DocumentContext *, char *, unsigned long long);
    load_f load = nh_api_loadHTMLSymbol("nh_html_loadBytes");
    return load ? load(Context_p, bytes_p, size) : NH_API_ERROR_BAD_STATE;
}
