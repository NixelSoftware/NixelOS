#ifndef NH_WEBIDL_COMMON_INCLUDES_H
#define NH_WEBIDL_COMMON_INCLUDES_H

#ifndef DOXYGEN_SHOULD_SKIP_THIS

/**
 * Netzhaut - Web Browser Engine
 * Copyright (C) 2022  Dajo Frey
 * Published under GNU LGPL. See Netzhaut/LICENSE.LGPL file.
 */

#include "../../nh-api/nh-webidl.h"

/* WebIDL interface objects are distinct from ECMAScript property objects. */
typedef struct nh_webidl_Object nh_webidl_Object;
#include "../../nh-core/Util/Debug.h"
#include "../../nh-core/Util/List.h"
#include "../../nh-encoding/Encodings/UTF8.h"
#include "../../nh-encoding/Encodings/UTF32.h"

#endif // DOXYGEN_SHOULD_SKIP_THIS

#endif // NH_WEBIDL_COMMON_INCLUDES_H
