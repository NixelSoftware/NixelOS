/**************************************************************************
 *
 * Copyright (C) 2014 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/* Minimal stable host-test ABI, from virglrenderer.h. Tests do not require
 * distribution-specific development headers. Production uses the wire ABI. */
#ifndef TEST_VIRGL_HOST_API_H
#define TEST_VIRGL_HOST_API_H
#include <stdint.h>
#include <stdbool.h>
#include <sys/uio.h>
typedef void *virgl_renderer_gl_context;
struct virgl_renderer_gl_ctx_param { int version; bool shared; int major_ver, minor_ver; };
struct virgl_renderer_callbacks {
    int version;
    void (*write_fence)(void *,uint32_t);
    void *(*create_gl_context)(void *,int,struct virgl_renderer_gl_ctx_param *);
    void (*destroy_gl_context)(void *,void *);
    int (*make_current)(void *,int,void *);
};
struct virgl_renderer_resource_create_args {
    uint32_t handle,target,format,bind,width,height,depth,array_size,last_level,nr_samples,flags;
};
struct virgl_box { uint32_t x,y,z,w,h,d; };
int virgl_renderer_init(void *,int,struct virgl_renderer_callbacks *);
void virgl_renderer_cleanup(void *);
int virgl_renderer_context_create(uint32_t,uint32_t,const char *);
void virgl_renderer_context_destroy(uint32_t);
int virgl_renderer_resource_create(struct virgl_renderer_resource_create_args *,struct iovec *,uint32_t);
void virgl_renderer_resource_unref(uint32_t);
void virgl_renderer_ctx_attach_resource(int,int);
int virgl_renderer_resource_attach_iov(int,struct iovec *,int);
int virgl_renderer_submit_cmd(void *,int,int);
int virgl_renderer_transfer_read_iov(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,
    struct virgl_box *,uint64_t,struct iovec *,int);
#endif
