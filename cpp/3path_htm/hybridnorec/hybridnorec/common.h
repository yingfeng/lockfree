/* =============================================================================
 *
 * common.h
 *
 * Some common defines
 *
 * =============================================================================
 */


#ifndef COMMON_H
#define COMMON_H 1

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE           64
#endif

#define __INLINE__                      /*static*/ __inline__
#define __ATTR__                        __attribute__((always_inline))

enum {
    NEVER  = 0,
    ALWAYS = 1,
};


#endif /* COMMON_H */


/* =============================================================================
 *
 * End of common.h
 *
 * =============================================================================
 */
