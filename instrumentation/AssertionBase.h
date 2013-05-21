#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef NDEBUG

// if (DebugFlag)
#define DEBUG(X)      \
  do { X; } while (0)

#else
#define DEBUG(X) do { } while (0)
#endif


#define STRUCT(ASSERTION)  ASSERTION##_state

#define STRUCT_DEFAULT(ASSERTION) \
  const STRUCT(ASSERTION) ASSERTION##_state_default

// CTYPE should take the form of /u?int\d+_t/, e.g. uint8_t
// These types are defined in stdint.h
#define INSTRUMENT_update(ASSERTION, CTYPE)          \
   inline extern                                     \
   void __update_##ASSERTION(                        \
      const CTYPE newVal, STRUCT(ASSERTION) *state,  \
      const char *file, int line)

// State is being allocated automatically, and passed to the function to avoid
// Clang ABI lowering. (for instance, returning an { i32 } would be lowered to
// i32 directly)
#define INSTRUMENT_init(ASSERTION)                 \
   inline extern                                   \
   void __init_##ASSERTION(                        \
      STRUCT(ASSERTION) *state,                    \
      const uint8_t *addr, const char **props,     \
      const char *file, int line)

#define INSTRUMENT_alloc(ASSERTION)                \
   inline extern                                   \
    STRUCT(ASSERTION) __alloc_##ASSERTION(         \
      const uint8_t *addr, const char **props)



#define EXPECT(ASSERTION, COND, FAIL_BLOCK)        \
  do {                                             \
    if (__builtin_expect(!COND, 0)) {              \
      fprintf(stderr, "%s:%d: failed "ASSERTION" assertion `%s' (%s:%d).\n", file, line, #COND, __FILE__, __LINE__); \
      do { FAIL_BLOCK } while(0);                  \
      abort();                                     \
    }                                              \
  } while (0)
  // __assert(#COND, file, line); \

// TODO in the general case, at least have an idea on how large this struct
// can be. For instance, for monotonic, a pointer size should be the largest,
// when we're dealing with an indirect larger type, otherwise it's an ordinary
// type that is not larger than a pointer type.