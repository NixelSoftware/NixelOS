#ifndef QUARK_DIRLISTING_H
#define QUARK_DIRLISTING_H

#include "../../kernel/types.h"

int serve_directory_listing(uint32_t socket, const char* full_path, const char* req_path);

#endif