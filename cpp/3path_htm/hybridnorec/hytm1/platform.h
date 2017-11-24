/* =============================================================================
 *
 * platform.h
 *
 * Platform-specific bindings
 *
 * =============================================================================
 */


#ifndef PLATFORM_H
#define PLATFORM_H 1

#if defined(SPARC) || defined(__sparc__)
#  error SPARC is not supported
#elif defined __PPC__ || defined __POWERPC__  || defined powerpc || defined _POWER || defined __ppc__ || defined __powerpc__
#  include "platform_p8.h"
#else
#  include "platform_x86.h"
#endif

#define CAS(m,c,s)  cas((intptr_t)(s),(intptr_t)(c),(intptr_t*)(m))

typedef unsigned long long TL2_TIMER_T;


#endif /* PLATFORM_H */


/* =============================================================================
 *
 * End of platform.h
 *
 * =============================================================================
 */
