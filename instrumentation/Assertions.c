#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define STRUCT(ASSERTION)  ASSERTION##_state

#define INSTRUMENT_update(ASSERTION)                 \
   inline extern                                     \
   void __update_##ASSERTION(                        \
      const uint8_t *addr, STRUCT(ASSERTION) *state, \
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
    if (!(COND)) {                                 \
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

typedef struct {
  int prev;
} STRUCT(monotonic);

INSTRUMENT_init(monotonic) {
  // Initialise "prev" with the current value.
  state->prev = *(const int *)addr;
}

INSTRUMENT_update(monotonic) {
  int newVal = *(const int *) addr;
  EXPECT("monotonic", newVal >= state->prev, 
  {
    printf("While updating: old=%d, new=%d\n", state->prev, newVal);
  });
  state->prev = newVal;
}



// ge (greater or equal)
// ==============================================
typedef struct { int than; } STRUCT(ge);

INSTRUMENT_init(ge) {
  // *props has to be the number
  state->than = (int) *props;
  int val = *(const int *) addr;
}

INSTRUMENT_update(ge) {
  
}