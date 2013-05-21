assertions
==========

This tool instruments (adds run-time code to enforce) assertions on variables and function return values in C programs, using C `annotate` attributes to guide the process.

Quick steps to run the whole suite on a particular C source file:

* First of all, run the [Clang annotator tool](https://github.com/dansanduleac/clang-annotator), which will be installed at `${CLANG_PREFIX}/bin/assertions`, on the desired input file and produce a LLVM file (either `.ll` or `.bc` will do). The tool accepts some arguments of its own, and after explicitly passing a `--`, takes the same flags as `clang -cc1`.
* Run this tool, found at `${PREFIX}/bin/assertions-instrumenter`, on the resulting file, which will produce another LLVM file (with assertions).
* Use any LLVM-enabled C compiler (e.g. an ordinary unmodified Clang, or `llvm-gcc` will do) to compile the resulting LLVM file into an object file, or executable, or whatever.
* The resulting executable will abort when an assertion fails, pointing out what assertion failed, and where.

# Using the assertions in your code

* Add this `include` directory to your project's include path.
* Include `Assertions.h` in files that use assertions.
* Use the macros provided in that include file (one for each assertion) to annotate variables, as well as function return values.
* You can add your own assertions to the `instrumentation/Assertions.c` file. You need to provide a `init` and `update` method (called when initializing a variable, and when updating the variable respectively) for each assertion, following the model of the previously defined assertions. More details in the following section.

# Adding new assertions

...