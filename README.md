# Trading System

A data acquisition pipeline for exchange orderbook data, written in C++23.

It connects to Kraken's L2 `book` channel and, on command from a frontend
client, records pairs into per-pair binary files or streams their books live
to the client over a websocket.

## Layout

- `data_feed/` — the acquisition program (CMake project).
- `example_scripts/` — standalone helper scripts (e.g. fetching a Kraken
  websocket token from Python).
- `third_party/` — vendored headers (rapidjson) and the vcpkg install tree.

## What it records

Nothing is subscribed at start. A client connects to the websocket on
`127.0.0.1:8765` and sends protobuf `ClientCommand`s
(`proto/market_data.proto`): `Start` / `Stop` / `List` for recordings and
live feeds, `Watch` / `Unwatch` to stream a live feed's books. See
`data_feed/ARCHITECTURE.md`.

Each subscription is set up as follows:

| Setting   | Value                        |
| --------- | ---------------------------- |
| Venue     | `wss://ws.kraken.com/v2`     |
| Channel   | `book`                       |
| Symbols   | any Kraken pair, one subscription per pair |
| Depth     | one of Kraken's book depths (10, 25, 100, 500, 1000) |
| Recording | one binary file per pair per venue connection, named by its capture window (e.g. `BTCUSD-<start>-<end>.bin`) |
| Live feed | protobuf `Book`s to every client watching it |

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
