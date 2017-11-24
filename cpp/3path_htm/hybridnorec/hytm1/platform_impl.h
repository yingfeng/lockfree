/* 
 * File:   platform_impl.h
 * Author: trbot
 *
 * Created on July 20, 2016, 11:25 PM
 */

#ifndef PLATFORM_IMPL_H
#define	PLATFORM_IMPL_H

#include "platform.h"

#if defined(SPARC) || defined(__sparc__)
#  error SPARC is not supported
#elif defined __PPC__ || defined __POWERPC__  || defined powerpc || defined _POWER || defined __ppc__ || defined __powerpc__
#  include "platform_impl_p8.h"
#else
#  include "platform_impl_x86.h"
#endif

#endif	/* PLATFORM_IMPL_H */

