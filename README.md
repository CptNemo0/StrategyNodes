# Trading System

A data acquisition pipeline for exchange orderbook data, written in C++23.

Right now it does one thing: it connects to Kraken's L2 `book` channel and
copies every message the venue sends, unchanged, into a text file.

## Layout

- `data_feed/` — the acquisition program (CMake project).
- `example_scripts/` — standalone helper scripts (e.g. fetching a Kraken
  websocket token from Python).
- `third_party/` — vendored headers (rapidjson) and the vcpkg install tree.
- `_unused/` — gitignored holding area for parked code (orderbook, websocket
  broadcast server, frontend, protobuf definitions). Not built.

## What it records

The values are hardcoded in `data_feed/source/main.cc`:

| Setting   | Value                    |
| --------- | ------------------------ |
| Venue     | `wss://ws.kraken.com/v2` |
| Channel   | `book`                   |
| Symbol    | `BTC/USD`                |
| Depth     | `10`                     |
| Output    | `kraken_l2_messages.txt` |

Each frame received after the subscription is acknowledged is written as one
line of raw JSON: the initial snapshot, updates and heartbeats. The file is
opened in append mode, so each run adds to it. The file is created in the
working directory, and each line is flushed as it is written.

The connection status message and the subscription acknowledgement are
consumed during `Connect()` and printed to the console. They are not written
to the file.

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
