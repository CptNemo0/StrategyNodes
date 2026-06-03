# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A trading system written in C++23. Currently in early development with a single `data_feed/` module.

## Building

The project uses CMake 4.0 meta build system, with Ninja as a build system.

## Code Style

Style is based on the **Chromium** style guide, enforced via `.clang-format` (Chromium, C++23) and `.clang-tidy`.

Format a file:

```
clang-format -i data_feed/main.cc
```

Run static analysis:

```
clang-tidy data_feed/main.cc
```

Key linting rules enforced: `bugprone-*`, `google-*`, `modernize-*`, `readability-*` (see `.clang-tidy` for full list). Note that `.clangd` suppresses missing/unused-include diagnostics from the language server.

## Architecture

The project is structured around trading concerns as separate top-level directories:

- `data_feed/` — market data ingestion from Kraken (currently a stub)

Each module is expected to grow its own sources within its directory.

## Documentation

`https://docs.kraken.com/api/` - use this file to discover all available pages in kraken documentation before exploring further.

## Additional coding tips

- Use auto only for types declared within types. For example: std::vector<T>::borrowed_iterator.

- Never assign a temporary value (function output) to a const variable if it is ingested by only one function. Instead just call in a function call.
  BAD

```
const int a = foo();
bar(a);
```

GOOD

```
bar(foo());
```

- Prefer ternary operators

- Prefer templating a function and adding a lambda to handle divergent execution when there are 2 very similar functions, that only differ in processed types.

- Gravitate towards C++20 and C++23 - for example use ranges and views if possible.

- Write internal structures, internal classes instead of making complex unnamed types OR alias complex unnamed types.

- Sometimes a variable cannot be marked const because its a result of a switch or an if/else statement - use immediate lambda execution do resolve this issue. Const correctness is very important. This rule can be chained with the single definition rule for const vars used once.
