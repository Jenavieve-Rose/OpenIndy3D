#ifndef J3DCORE_TYPES_H
#define J3DCORE_TYPES_H

/*
 * Port: the decomp's version of this header redefined int32_t, uintptr_t (as unsigned int) and friends to reproduce
 * the 1999 Visual C++ 5.0 ABI, and carried the count-fit (CF_*) placeholder macros that only padded that compiler's
 * symbol tables. Both are replaced by the standard headers; the CF_* call sites were removed with them.
 */
#include <stddef.h>
#include <stdint.h>

#endif
