#ifndef RTM_X86_H
#define RTM_X86_H 1

/*
 * Copyright (c) 2012,2013 Intel Corporation
 * Author: Andi Kleen
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#define _XBEGIN_STARTED		(~0u)
#define _XABORT_EXPLICIT	(1 << 0)
#define _XABORT_RETRY		(1 << 1)
#define _XABORT_CONFLICT	(1 << 2)
#define _XABORT_CAPACITY	(1 << 3)
#define _XABORT_DEBUG		(1 << 4)
#define _XABORT_NESTED		(1 << 5)
#define _XABORT_CODE(x)		(((x) >> 24) & 0xff)

#define __rtm_force_inline __attribute__((__always_inline__)) inline

#ifndef SOFTWARE_BARRIER
#define SOFTWARE_BARRIER asm volatile("": : :"memory")
#endif

#define HYTM_XBEGIN_ARG_T int
#define HYTM_XBEGIN(arg) (((arg) = ___HYTM_XBEGIN()) == _XBEGIN_STARTED)
#define HYTM_XEND() (___HYTM_XEND())
#define HYTM_XABORT(arg) (___HYTM_XABORT((arg)))
#define HYTM_XSUSPEND()
#define HYTM_XRESUME()
#define X_ABORT_GET_STATUS(arg) ((arg))
#define X_ABORT_COMPRESS_STATUS(status) (((status)&0x3f) | ((((status)>>24)&0xff)<<6))
#define X_ABORT_DECOMPRESS_STATUS(cstatus) (((cstatus)&0x3f) | (((cstatus)&(~0x3f))<<24))
#define X_ABORT_STATUS_USERCODE(status) (((status)>>24)&0xff)
#define X_ABORT_STATUS_IS_USER(status) ((status)&0x1)
#define X_ABORT_STATUS_IS_CAPACITY(status) (((status)>>3)&0x1)
#define X_ABORT_STATUS_IS_NESTING(status) (((status)>>5)&0x1)
#define X_ABORT_STATUS_IS_CONFLICT(status) (((status)>>2)&0x1)
#define X_ABORT_STATUS_IS_RETRY(status) (!(((status)>>1)&0x1))
//#define X_IS_ABORT_USER(arg, code) ({ (code) = _XABORT_CODE((arg)); (arg) & _XABORT_EXPLICIT; })
//#define X_IS_ABORT_CAPACITY(arg) ((arg) & _XABORT_CAPACITY)
//#define X_IS_ABORT_NESTING(arg) ((arg) & _XABORT_NESTED)
//#define X_IS_ABORT_CONFLICT(arg) ((arg) & _XABORT_CONFLICT)
//#define X_IS_ABORT_RETRY(arg) ((arg) & _XABORT_RETRY)
//#define X_IS_ABORT_ILLEGAL(arg) false

static __rtm_force_inline int ___HYTM_XBEGIN(void)
{
    int ret = _XBEGIN_STARTED;
    asm volatile(".byte 0xc7,0xf8 ; .long 0" : "+a" (ret) ::"memory");
    //__sync_synchronize();
    return ret;
}

static __rtm_force_inline void ___HYTM_XEND(void) {
    asm volatile(".byte 0x0f,0x01,0xd5" :: : "memory");
    //__sync_synchronize();
}

static __rtm_force_inline void ___HYTM_XABORT(const unsigned int status) {
    asm volatile(".byte 0xc6,0xf8,%P0" ::"i" (status) : "memory");
    //__sync_synchronize();
}

#endif
