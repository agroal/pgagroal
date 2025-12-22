#!/bin/bash

function format_files()
{
    for f in $1
    do
        if [ -f "$f" ]; then
            clang-format -i "$f"
        fi
    done
}

format_files "src/*.c"
format_files "src/include/*.h"
format_files "src/libpgagroal/*.c"

format_files "test/*.c"
format_files "test/include/*.h"
format_files "test/testcases/*.c"
format_files "test/testcases/*.h"
