#!/usr/bin/env python3
"""Add GCOV linker flags for profiling build."""

Import("env")

# Add GCOV linker flags
env.Append(LINKFLAGS=[
    "-fprofile-arcs",
    "-ftest-coverage",
    "-lgcov"
])

print("Added GCOV linker flags: -fprofile-arcs -ftest-coverage -lgcov")
