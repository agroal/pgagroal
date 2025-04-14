\newpage

# Building pgagroal

## Overview

[**pgagroal**][pgagroal] can be built using CMake, where the build system will detect the compiler and apply appropriate flags for debugging and testing.

The main build system is defined in [CMakeLists.txt][cmake_txt]. The flags for Sanitizers are added in compile options in [src/CMakeLists.txt][src/cmake_txt]

## Dependencies

Before building pgagroal with sanitizer support, ensure you have the required packages installed:

* `libasan` - AddressSanitizer runtime library
* `libasan-static` - Static version of AddressSanitizer runtime library

On Red Hat/Fedora systems:
```
sudo dnf install libasan libasan-static
```

Package names and versions may vary depending on your distribution and compiler version.

## Debug Mode

When building in Debug mode, [**pgagroal**][pgagroal] automatically enables various compiler flags to help with debugging, including AddressSanitizer (ASAN) and UndefinedBehaviorSanitizer (UBSAN) support when available.

The Debug mode can be enabled by adding `-DCMAKE_BUILD_TYPE=Debug` to your CMake command.

## Sanitizer Flags

### AddressSanitizer (ASAN)

Address Sanitizer is a memory error detector that helps find use-after-free, heap/stack/global buffer overflow, use-after-return, initialization order bugs, and memory leaks.

### UndefinedBehaviorSanitizer (UBSAN)

UndefinedBehaviorSanitizer is a fast undefined behavior detector that can find various types of undefined behavior during program execution, such as integer overflow, null pointer dereference, and more.

### Common Flags

* `-fno-omit-frame-pointer` - Provides better stack traces in error reports
* `-Wall -Wextra` - Enables additional compiler warnings

### GCC Support

* `-fsanitize=address` - Enables the Address Sanitizer (GCC 4.8+)
* `-fsanitize=undefined` - Enables the Undefined Behavior Sanitizer (GCC 4.9+)
* `-fno-sanitize=alignment` - Disables alignment checking (GCC 5.1+)
* `-fno-sanitize-recover=all` - Makes all sanitizers halt on error (GCC 5.1+)
* `-fsanitize=float-divide-by-zero` - Detects floating-point division by zero (GCC 5.1+)
* `-fsanitize=float-cast-overflow` - Detects floating-point cast overflows (GCC 5.1+)
* `-fsanitize-recover=address` - Allows the program to continue execution after detecting an error (GCC 6.0+)
* `-fsanitize-address-use-after-scope` - Detects use-after-scope bugs (GCC 7.0+)

### Clang Support

* `-fsanitize=address` - Enables the Address Sanitizer (Clang 3.2+)
* `-fno-sanitize=null` - Disables null pointer dereference checking (Clang 3.2+)
* `-fno-sanitize=alignment` - Disables alignment checking (Clang 3.2+)
* `-fsanitize=undefined` - Enables the Undefined Behavior Sanitizer (Clang 3.3+)
* `-fsanitize=float-divide-by-zero` - Detects floating-point division by zero (Clang 3.3+)
* `-fsanitize=float-cast-overflow` - Detects floating-point cast overflows (Clang 3.3+)
* `-fno-sanitize-recover=all` - Makes all sanitizers halt on error (Clang 3.6+)
* `-fsanitize-recover=address` - Allows the program to continue execution after detecting an error (Clang 3.8+)
* `-fsanitize-address-use-after-scope` - Detects use-after-scope bugs (Clang 3.9+)

## Additional Sanitizer Options

Developers can add additional sanitizer flags via environment variables. Some useful options include:

### ASAN Options

* `ASAN_OPTIONS=detect_leaks=1` - Enables memory leak detection
* `ASAN_OPTIONS=halt_on_error=0` - Continues execution after errors
* `ASAN_OPTIONS=detect_stack_use_after_return=1` - Enables stack use-after-return detection
* `ASAN_OPTIONS=check_initialization_order=1` - Detects initialization order problems
* `ASAN_OPTIONS=strict_string_checks=1` - Enables strict string function checking
* `ASAN_OPTIONS=detect_invalid_pointer_pairs=2` - Enhanced pointer pair validation
* `ASAN_OPTIONS=print_stats=1` - Prints statistics about allocated memory
* `ASAN_OPTIONS=verbosity=1` - Increases logging verbosity

### UBSAN Options

* `UBSAN_OPTIONS=print_stacktrace=1` - Prints stack traces for errors
* `UBSAN_OPTIONS=halt_on_error=1` - Stops execution on the first error
* `UBSAN_OPTIONS=silence_unsigned_overflow=1` - Silences unsigned integer overflow reports

## Building with Sanitizers

To build [**pgagroal**][pgagroal] with sanitizer support:

```
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

The compiler can also be specified
```
cmake -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Debug ..
# or
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug .
```

The build system will automatically detect the compiler version and enable the appropriate sanitizer flags based on support.

## Running with Sanitizers

When running [**pgagroal**][pgagroal] built with sanitizers, any errors will be reported to stderr.

To get more detailed reports, you can set additional environment variables:

```
ASAN_OPTIONS=detect_leaks=1:halt_on_error=0:detect_stack_use_after_return=1 ./pgagroal
```

You can combine ASAN and UBSAN options:

```
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 ./pgagroal
```

## Advanced Sanitizer Options Not Included by Default

Developers may want to experiment with additional sanitizer flags not enabled by default:

* `-fsanitize=memory` - Enables MemorySanitizer (MSan) for detecting uninitialized reads (Note this can't be used with ASan)
* `-fsanitize=integer` - Only check integer operations (subset of UBSan)
* `-fsanitize=bounds` - Array bounds checking (subset of UBSan)
* `-fsanitize-memory-track-origins` - Tracks origins of uninitialized values (with MSan)
* `-fsanitize-memory-use-after-dtor` - Detects use-after-destroy bugs (with MSan)
* `-fno-common` - Prevents variables from being merged into common blocks, helping identify variable access issues

Note that some sanitizers are incompatible with each other. For example, you cannot use ASan and MSan together.