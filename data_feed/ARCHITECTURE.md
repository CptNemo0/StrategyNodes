# data_feed architecture

## Data flow across threads

```mermaid
flowchart LR
    subgraph Kraken["Kraken"]
        WS[("wss://ws.kraken.com/v2<br/>book channel")]
        REST[("api.kraken.com<br/>/0/public/AssetPairs")]
    end

    subgraph Main["Main thread"]
        MAIN["main()<br/>SIGINT → stop_requested<br/>polls kraken_manager.failed() every 100 ms"]
        VM["kraken::VenueManager<br/>life_cycle_mutex_"]
        PREC["kraken::FetchPairPrecision"]
    end

    subgraph Net["Network thread: net::WebsocketFeed"]
        CONN["Connection<br/>TCP → TLS SNI → WS handshake<br/>Next(): async_read, 50 ms stop poll<br/>Send(): post → outbox_ → async_write"]
        STAMP["stamp capture_time_ns<br/>UnixNanosNow()"]
    end

    RAWLOG[/"kraken_l2_messages-&lt;ns&gt;.txt<br/>raw JSON log"/]
    RFQ[["RawFrameQueue<br/>SPSC, cap 256<br/>RawFrame{json, capture_time_ns}"]]

    subgraph Parse["Parser thread: kraken::L2MessageParser"]
        DISPATCH["Dispatch()<br/>rapidjson, numbers as strings<br/>filter channel == book"]
        ROUTES{"routes_<br/>symbol → Route<br/>{PairPrecision, Receiver*}<br/>routes_mutex_"}
        SCALE["ScaleDecimalStringToI64<br/>ParseChecksum / ParseVenueTime"]
    end

    subgraph W1["Writer thread: BTC/USD"]
        Q1[["MessageQueue<br/>SPSC, cap 256"]]
        FW1["l2::FileWriter<br/>Serialize + flush"]
    end

    subgraph W2["Writer thread: ETH/USD"]
        Q2[["MessageQueue<br/>SPSC, cap 256"]]
        FW2["l2::FileWriter<br/>Serialize + flush"]
    end

    BIN1[/"BTCUSD-&lt;start&gt;[-&lt;end&gt;].bin<br/>l2::FileHeader 64B + l2::Message records"/]
    BIN2[/"ETHUSD-&lt;start&gt;[-&lt;end&gt;].bin"/]

    STATUS(("PipelineStatus<br/>atomic failed"))

    MAIN --> VM
    VM -- "Subscribe(symbol, depth)" --> PREC
    PREC -- "HTTPS GET" --> REST
    VM -- "Send(subscribe JSON)" --> CONN
    CONN <-- "frames" --> WS
    CONN --> STAMP
    STAMP --> RAWLOG
    STAMP -- "PushBlocking" --> RFQ
    RFQ -- "Drain" --> DISPATCH
    DISPATCH --> ROUTES
    ROUTES --> SCALE
    SCALE -- "Receive(l2::Message)" --> Q1
    SCALE -- "Receive(l2::Message)" --> Q2
    Q1 -- "Drain" --> FW1 --> BIN1
    Q2 -- "Drain" --> FW2 --> BIN2

    CONN -. "Fail() on socket error" .-> STATUS
    DISPATCH -. "Fail() on exception" .-> STATUS
    STATUS -. "failed()" .-> MAIN
```

## Lifecycle: main → Connect → Subscribe → shutdown

```mermaid
sequenceDiagram
    autonumber
    participant M as main
    participant VM as kraken::VenueManager
    participant P as kraken::L2MessageParser
    participant F as net::WebsocketFeed
    participant K as Kraken REST/WS
    participant W as l2::FileWriter

    M->>VM: construct
    Note over VM: kraken::Credentials::FromEnvironment()<br/>build parser and feed sharing RawFrameQueue and PipelineStatus

    M->>VM: Connect()
    VM->>VM: DisconnectLocked(), status_.Reset()
    VM->>P: Start() spawns parser thread
    VM->>F: Connect()
    F->>K: TCP + TLS + WS handshake
    F->>F: open raw log, spawn network thread
    VM->>F: Send(subscribe) for existing subscriptions, none yet

    loop BTC/USD, ETH/USD at depth 100
        M->>VM: Subscribe(symbol, 100)
        VM->>K: kraken::FetchPairPrecision via AssetPairs
        K-->>VM: price_decimals, qty_decimals
        VM->>W: create: open .bin, write l2::FileHeader, spawn writer thread
        VM->>P: Attach(symbol, precision, writer)
        VM->>F: Send(subscribe book JSON)
        F->>K: async_write
    end

    loop until SIGINT or kraken_manager.failed()
        K-->>F: snapshot / update frames
        F->>P: RawFrame via RawFrameQueue
        P->>W: l2::Message via Receive()
        W->>W: append record to .bin
    end

    M->>VM: ~VenueManager()
    VM->>F: Disconnect(): stop network thread, WS close, close raw log
    VM->>P: Stop(): drain queue, join
    Note over VM: subscriptions_ destroyed → each writer drains,<br/>closes and renames to SYMBOL-start-end.bin
```
