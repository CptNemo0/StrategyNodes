# Trading System

A data acquisition pipeline for exchange orderbook data, written in C++23.

Right now it connects to Kraken's L2 `book` channel for a fixed set of pairs
and records every message the venue sends into a per-pair binary file.

## Layout

- `data_feed/` — the acquisition program (CMake project).
- `example_scripts/` — standalone helper scripts (e.g. fetching a Kraken
  websocket token from Python).
- `third_party/` — vendored headers (rapidjson) and the vcpkg install tree.

## What it records

The pairs are hardcoded in `data_feed/source/main.cc`:

| Setting   | Value                        |
| --------- | ---------------------------- |
| Venue     | `wss://ws.kraken.com/v2`     |
| Channel   | `book`                       |
| Symbols   | `BTC/USD`, `ETH/USD`         |
| Depth     | `100`                        |
| Output    | one binary file per pair, windowed by capture time (e.g. `BTCUSD-<start>-<end>.bin`) |

Each pair gets its own writer lane on its own thread; a write failure only
takes down that pair's lane (and triggers an unsubscribe for it) — the rest
of the feed keeps running. The connection status message and subscription
acknowledgements are consumed during `Connect()` and printed to the console,
not written to the files.

## Requirements

- CMake 4.0+, Ninja, MinGW-w64 GCC (see `data_feed/CMakePresets.json`)
- vcpkg with `VCPKG_ROOT` set; dependencies are pinned in `vcpkg.json`
- Kraken API credentials in the environment:
  - `KRAKEN_API_KEY`
  - `KRAKEN_PRIVATE_KEY`

## Build and run

From `data_feed/`:

```sh
cmake --preset mingw
cmake --build --preset debug      # or: --preset release
./build/Debug/data_feed.exe
```

Stop with Ctrl+C.
