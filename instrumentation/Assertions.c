#include "AssertionBase.h"
#include <limits.h>

// monotonic
// ==============================================

typedef struct {
  int prev;
} STRUCT(monotonic);

STRUCT_DEFAULT(monotonic) = { .prev = 0 }; // INT_MIN?

INSTRUMENT_init(monotonic) {
  // Initialise "prev" with the current value.
  state->prev = *(const int *)addr;
}

INSTRUMENT_update(monotonic, int32_t) {
  // DEBUG(printf("MONOTONIC: old=%d, new=%d\n", state->prev, newVal));
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
  // TODO finish
}

INSTRUMENT_update(ge, int32_t) {
  EXPECT("ge", newVal >= state->than, 
  {
    printf("New value %d is not >= %d\n", newVal, state->than);
  });
}