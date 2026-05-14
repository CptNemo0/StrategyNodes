# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A Coinbase trading system written in C++23. Currently in early development with a single `data_feed/` module.

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

- `data_feed/` — market data ingestion from Coinbase (currently a stub)

Each module is expected to grow its own sources within its directory.

## Documentation

`https://docs.cdp.coinbase.com/llms.txt` - use this file to discover all available pages in coinbase documentation before exploring further.
