#ifndef ANNOTATEVARIABLES_ASSERTIONS_H
#define ANNOTATEVARIABLES_ASSERTIONS_H

// Function to initialize all the assertions, created by instrumenter. Call
// this at the beginning of main() if using function return value assertions.
#ifndef __ASSERTIONS_ANALYSER__
#define InitializeAllAssertions()
#else
extern void InitializeAllAssertions();
#endif

#define __assert_monotonic \
  __attribute__((annotate("assertion,monotonic")))

#define __assert_ge(NR)  __attribute__(( annotate("assertion,ge("#NR")" )))

#define __assert_uniform(FROM, TO) \
  __attribute__((annotate("assertion,dist(uniform," #FROM "," #TO ")")))

// #define __default_state(...) 

#endif

