# data_feed architecture

## Data flow across threads

```mermaid
flowchart LR
    subgraph Kraken["Kraken"]
        WS[("wss://ws.kraken.com/v2<br/>book channel")]
        REST[("api.kraken.com<br/>/0/public/AssetPairs")]
    end

    subgraph Main["Main thread"]
        MAIN["main()<br/>SIGINT → stop_requested"]
        ROUTER["venue::Router<br/>global ids<br/>builds FileWriter / Transmitter"]
        VM["kraken::VenueManager : venue::Manager<br/>life_cycle_mutex_<br/>subscriptions_: id → {symbol, depth, sink, start, receiver}<br/>at most one per symbol"]
        PREC["kraken::FetchPairPrecision<br/>on every Subscribe"]
    end

    SUP["Supervisor thread: venue::Router<br/>every 100 ms: Connect() each unhealthy venue<br/>backoff 1 s, doubling up to 30 s"]

    subgraph Net["Network thread: net::WebsocketFeed"]
        CONN["Connection<br/>TCP → TLS SNI → WS handshake<br/>Next(): async_read, 50 ms stop poll<br/>Send(): post → outbox_ → async_write"]
        STAMP["stamp capture_time_ns<br/>UnixNanosNow()"]
    end

    RAWLOG[/"kraken_l2_messages-&lt;ns&gt;.txt<br/>raw JSON log"/]
    RFQ[["RawFrameQueue<br/>SPSC, cap 256<br/>RawFrame{json, capture_time_ns}"]]

    subgraph Parse["Parser thread: kraken::L2MessageParser"]
        DISPATCH["Dispatch()<br/>rapidjson, numbers as strings<br/>filter channel == book"]
        ROUTES{"routes_<br/>symbol → Route<br/>{PairPrecision, Receiver*, awaiting_snapshot}<br/>routes_mutex_"}
        SCALE["ScaleDecimalStringToI64<br/>ParseChecksum / ParseVenueTime"]
    end

    subgraph W1["Writer thread: BTC/USD @ 100"]
        Q1[["MessageQueue<br/>SPSC, cap 256"]]
        FW1["l2::FileWriter<br/>Serialize + flush"]
    end

        TX2["frontend::Transmitter → Stream<br/>ETH/USD @ 100<br/>keeps the book, sends to watchers"]

    BIN1[/"BTCUSD-&lt;start&gt;[-&lt;end&gt;].bin<br/>l2::FileHeader 64B + l2::Message records"/]

    STATUS(("PipelineStatus<br/>atomic failed"))

    MAIN -- "Subscribe(venue, symbol, depth, sink)" --> ROUTER
    ROUTER -- "Subscribe(id, symbol, depth, sink, factory)" --> VM
    SUP -- "Connect()" --> VM
    VM --> PREC
    PREC -- "HTTPS GET" --> REST
    VM -- "Send(subscribe / unsubscribe JSON)" --> CONN
    CONN <-- "frames" --> WS
    CONN --> STAMP
    STAMP --> RAWLOG
    STAMP -- "PushBlocking" --> RFQ
    RFQ -- "Drain" --> DISPATCH
    DISPATCH --> ROUTES
    ROUTES --> SCALE
    SCALE -- "Receive(l2::Message)" --> Q1
    SCALE -- "Receive(l2::Message)" --> TX2
    Q1 -- "Drain" --> FW1 --> BIN1

    CONN -. "Fail() on socket error" .-> STATUS
    DISPATCH -. "Fail() on exception" .-> STATUS
    STATUS -. "healthy()" .-> SUP
```

## One subscription per pair

Kraken streams a pair at one depth per connection, so a pair has at most one
subscription: sharing it would force every receiver onto one depth, and a
receiver joining mid-stream would start without a snapshot. A second
subscription to a symbol fails with `kSymbolInUse`, with one exception: a
recording of a pair already recorded at the same depth returns the existing
recording's id, since a second file would only duplicate the first. Both are
checked before and after the precision fetch. An id already in use throws
`std::logic_error`, as only a Router bug can cause it.

A subscription is identified by an id the Router hands out, unique across
venues. The manager gets the receiver from a factory the Router builds, plus
the subscription's `Sink`, which it keeps for `List()`, for spotting a
duplicate recording, and for refusing an `Unsubscribe` that names another
sink. Both checks run under the manager's lock, so a `Start` that resolves to
an existing recording and a `Stop` of it cannot interleave.

Every subscription starts with a snapshot: the parser drops a newly attached
pair's updates until its first one, so updates still in flight from an
earlier subscription to the pair never reach the new receiver. Unsubscribing
detaches the receiver from the parser, then unsubscribes from the pair.

## Gaps: reconnects and checksum mismatches

A book cannot be carried across a gap in the stream: whatever changed in it
is lost. A gap opens in two ways, and both end with a fresh snapshot.

- **A dropped connection.** The parser waits for a fresh snapshot of every
  pair on each `Start()`, and when a connection that was up ends,
  `VenueManager::DisconnectLocked()` calls
  `Receiver::OnGap(core::Gap::kDisconnected)` on every subscription's
  receiver, once the parser has drained, so never alongside `Receive()`.
- **A checksum mismatch.** The parser keeps every pair's `l2::Book` at its
  subscribed depth and, after each frame, compares `kraken::BookChecksum()`
  (CRC32 over the top 10 asks then bids, each level's digits without the
  decimal point or leading zeros) with the checksum Kraken sent. Our
  fixed-point integers are exactly those digits, so no string formatting is
  needed. On a mismatch it drops the frame, calls
  `OnGap(core::Gap::kResubscribing)` on the pair's receiver, drops the pair's
  updates until its next snapshot, and tells the `VenueManager`, whose
  resubscriber thread sends Kraken an unsubscribe and then a subscribe for
  the pair on the live connection. Kraken answers in order: updates already
  in flight (dropped), both acknowledgements, then the new snapshot. The
  thread exists so the parser never waits on the manager's lock, which
  `Disconnect()` holds while joining the parser. A pair is resubscribed at
  most once per second, so a pair that keeps failing cannot flood the venue.
  A frame without a checksum is not checked.

What the receivers do with a gap:

- `l2::FileWriter` closes and renames the current `.bin`; the snapshot that
  ends the gap opens the next one, with its own header and start time. A
  file with no messages yet is kept, so failed reconnect attempts leave no
  empty files. A final name already taken (two files starting and ending in
  the same seconds) gets `-1`, `-2`, ... appended.
- `frontend::Stream` drops its book and sends its watchers a `FeedStatus`:
  `STATE_DISCONNECTED` for a dropped connection, `STATE_DEGRADED` for a
  resubscribe. A client that watches during the gap gets that in place of a
  snapshot. A `FeedStatus` `STATE_LIVE`, naming the stream in
  `healthy_stream_ids`, goes out just before the snapshot that ends the gap.

## Lifecycle: main → Connect → Subscribe → shutdown

```mermaid
sequenceDiagram
    autonumber
    participant M as main
    participant VM as kraken::VenueManager
    participant P as kraken::L2MessageParser
    participant F as net::WebsocketFeed
    participant K as Kraken REST/WS
    participant R as l2::FileWriter / frontend::Transmitter
    participant RT as venue::Router

    M->>VM: construct
    Note over VM: kraken::Credentials::FromEnvironment()<br/>build parser and feed sharing RawFrameQueue and PipelineStatus

    M->>RT: construct with {kKraken: VenueManager}
    Note over RT: starts the supervisor thread
    RT->>VM: Connect(), since healthy() is false
    VM->>VM: DisconnectLocked(), status_.Reset()
    VM->>P: Start() spawns parser thread
    VM->>F: Connect()
    F->>K: TCP + TLS + WS handshake
    F->>F: open raw log, spawn network thread
    VM->>F: Send(subscribe) for every subscription, none yet

    loop BTC/USD@100 kRecord, ETH/USD@100 kLive
        M->>RT: Subscribe(kKraken, symbol, 100, sink)
        RT->>VM: Subscribe(next id, symbol, 100, sink, factory)
        Note over VM: kSymbolInUse if the symbol is taken,<br/>the existing id for a duplicate recording
        VM->>K: kraken::FetchPairPrecision via AssetPairs
        K-->>VM: price_decimals, qty_decimals
        VM->>R: create (a writer opens its .bin and writes l2::FileHeader)
        VM->>P: Attach(symbol, precision, receiver)
        VM->>F: Send(subscribe)
        F->>K: async_write
    end

    loop until SIGINT; the supervisor reconnects whenever healthy() is false
        K-->>F: snapshot / update frames
        F->>P: RawFrame via RawFrameQueue
        P->>R: l2::Message via Receive(), to the pair's receiver
    end

    M->>RT: ~Router(): stop the supervisor, destroy the managers
    RT->>VM: ~VenueManager()
    VM->>F: Disconnect(): stop network thread, WS close, close raw log
    VM->>P: Stop(): drain queue, join
    Note over VM: subscriptions_ destroyed → each writer drains,<br/>closes and renames to SYMBOL-start-end.bin
```

## Frontend control API

Implemented, except playback. The frontend is a CLI-like client that talks
to `data_feed` over one plain websocket on `frontend::kStreamPort` (8765),
bound to loopback and meant to be exposed publicly through a Cloudflare
Tunnel, which terminates TLS. There is no authentication yet, so nothing
should expose the port before there is.

### Commands

One venue for now, so it defaults to kraken.

| CLI | Request | Reply |
|---|---|---|
| `live start BTC/USD@100` | `Start{KRAKEN, "BTC/USD", 100, LIVE}` | `CommandResult.started_id` |
| `live ls` | `List{}`, client keeps `sink == LIVE` | `SubscriptionList` |
| `live stop 3` | `Stop{3, LIVE}` | `Ok` / `CommandError` |
| `live show 3`, Esc to leave | `Watch{3}`, then `Unwatch{}` | `StreamInfo`, then `Book`s |
| `rec start / ls / stop` | as above with `sink = RECORD` | as above |
| `play ls` | `ListRecordings{}` | recordings with ids; the client renders unix times |
| `play 7` | `Play{7}`, then `PlaybackNext{...}` | batches of `Book`s, see below |

`Stop` carries the sink the client expects, so `rec stop 3` cannot stop a
live feed by mistake (`CODE_SINK_MISMATCH`). `Book.stream_id` is the
subscription id, so `Watch{3}` yields `StreamInfo{stream_id: 3}` and
`Book{stream_id: 3}`. Live feeds, recordings and playbacks run concurrently.

### Wire protocol

`proto/market_data.proto`, compiled with `protoc` for C++ and `protoc-gen-es`
for TypeScript. On the C++ side it is the `market_data_proto` CMake target,
included as `"market_data.pb.h"` (namespace `trading::market_data::v1`). Its
headers are `SYSTEM` and its sources stay out of `PROJECT_SOURCES`, so
clang-tidy never sees them. protoc runs from a custom command rather than
`protobuf_generate`, so that the compiler's directory can be put first on
PATH: protoc needs GCC's runtime DLLs, and Git for Windows ships an
incompatible `libstdc++-6.dll`.

- Client to server: `ClientCommand{request_id, oneof command}` with `Start`,
  `Stop`, `List`, `Watch`, `Unwatch`; fields 7 to 15 reserved for playback
  (`ListRecordings`, `Play`, `PlaybackNext`, `StopPlayback`).
- Server to client: the existing `MarketDataEvent` envelope (`sequence`,
  `send_time_ns`) gains a `CommandResult{request_id, oneof result}` case:
  `started_id`, `Ok`, `SubscriptionList`, `CommandError{code, detail}`.
- `Subscription{id, venue, symbol, depth, sink, start_time_ns}` describes one
  entry of a `SubscriptionList`.
- Not `Request` / `Response` / `Error`: those would shadow JavaScript globals
  in the generated TypeScript.

Generated types stay inside `frontend/`: `frontend::Server` converts a
`ClientCommand` into plain arguments for the Router, and `l2::Message` /
subscription data into `Book` / `CommandResult`. Nothing below `frontend/`
depends on protobuf.

### Router layout

```mermaid
flowchart LR
    CLI["CLI client"] <-- "wss: ClientCommand / MarketDataEvent" --> SRV
    subgraph frontend["frontend/"]
        SRV["frontend::Server<br/>parses ClientCommand, calls Router"]
        OUT["frontend::Outbox<br/>id → Stream registry"]
        TX["frontend::Transmitter<br/>one per live subscription"]
        ST["frontend::Stream<br/>the book and its watchers"]
    end
    subgraph venue["venue/"]
        ROUTER["venue::Router<br/>allocates global ids"]
        MGR["venue::Manager<br/>Subscribe / Unsubscribe / List"]
    end
    KVM["kraken::VenueManager"]

    SRV --> ROUTER
    ROUTER -- "factory: Transmitter(outbox, id) or FileWriter" --> MGR
    MGR -. "implemented by" .- KVM
    SRV -- "Watch(id, session)" --> OUT --> ST
    TX -- "Receive / OnGap" --> ST
    ST -- "Send(Payload)" --> SRV
```

- `venue::Router` allocates the id first, then builds the receiver
  factory: a `frontend::Transmitter` bound to the Outbox and the id for
  `kLive`, an `l2::FileWriter` for `kRecord`. That is the only place the
  two differ. It keeps no copy of the subscriptions: `Unsubscribe` asks each
  venue in turn (ids are unique across venues), and the venue checks the
  sink, so nothing can go stale between the Router and a venue.
- `venue::Manager` is the interface each venue implements:
  `Connect`, `healthy`, `Subscribe`, `Unsubscribe`, `List`. It never sees
  receiver types, only the factory and the sink. Failures come back as
  `std::expected<…, venue::Error>`, whose codes mirror `CommandError::Code`
  (plus `kUnknownVenue`, which maps to `CODE_INVALID_COMMAND`). `List()`
  returns data; the CLI formats and filters it.
- The Router's supervisor thread reconnects every venue that is not
  `healthy()`, waiting `Backoff::initial` (1 s) after a failed attempt and
  doubling up to `Backoff::max` (30 s); a success resets the wait.
- The Outbox breaks the cycle between the Server (which needs the Router)
  and the Transmitters (which need to send). Each Transmitter registers its
  `Stream` in it under its id for its lifetime. The Stream is shared, so the
  Outbox holds its lock only to look one up, never while a `Watch` builds a
  snapshot; no two locks are ever nested.
- `frontend::Stream` keeps the pair's book, truncated to the subscribed
  depth, so a client can `Watch` mid-stream: it gets a `StreamInfo` and a
  snapshot built from the book, then every message after it. Its lock orders
  the snapshot strictly between two deltas. It sends to watchers
  (`frontend::Watcher`, implemented by the Server's sessions) on the parser
  thread; sending only queues.
- Each event is serialized once, into a `frontend::Payload` (the bytes in a
  `shared_ptr<const std::string>`), however many clients get it. Each
  session only writes its own envelope (`sequence`, `send_time_ns`, the
  payload's tag and length; `frontend::FrameHeader`) as the frame goes out,
  and writes the shared bytes after it uncopied, which is valid protobuf
  because a message is the concatenation of its fields.
- `frontend::Server` runs every socket on one network thread (Boost.Beast)
  and every command on a 4 thread pool, one strand per connection, so a
  `Start` blocking on the venue's REST round trip stalls neither the sockets
  nor other clients, and a connection's commands run in order. Each frame
  gets the connection's next `sequence`. A client that stops reading has
  `Book`s dropped once 4096 frames are queued; the dropped frames still take
  a sequence number, so the gap tells the client to `Watch` again. Past 8192
  queued frames (replies to commands it keeps sending) it is dropped, as it
  is with 64 commands read but not yet answered. An idle
  connection gets a `Heartbeat` every 5 s, and websocket pings detect a dead
  one after 30 s. `FeedStatus` goes out per stream, around reconnects (see
  Gaps); `STATE_CONNECTING` is not used yet.
- Shutdown runs in stages, so no session outlives the contexts its socket
  and strand belong to: the network thread closes every connection and
  exits, every queued command (and each closed session's unwatch) runs, then
  replies sent meanwhile are dropped. `main` declares the Outbox, Router and
  Server in that order.

### Playback flow control

The server reads a recording and sends it in batches of 10 messages as fast
as the client asks; the client, not the server, replays them at their
recorded pace (`capture_time_ns`). The client paces its requests so it never
runs dry and never holds more than it needs:

- It measures the round trip of each `PlaybackNext` to the batch it brings.
- It keeps the replay time it already holds: the recorded time between the
  message on screen and the last message buffered.
- When that held time drops below the time a request takes (about 2 × RTT,
  for headroom), it sends `PlaybackNext{playback_id, batches}`, asking for as
  many batches as it estimates it will play in that time, at least one. In a
  busy market 10 messages may cover less than one round trip, so several
  batches are in flight at once; in a quiet one a single batch lasts many.

The server never sends more than has been asked for, so a large `.bin` does
not pile up in the client's memory or on the Cloudflare connection.
