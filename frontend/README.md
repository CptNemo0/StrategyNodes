# frontend

Graph-based strategy creator (in the spirit of Bitwig Grid / Shader Graph).
The graph is edited here; all evaluation happens in the C++ backend, which
streams results to the browser as protobuf frames over a WebSocket.

## Stack

- **Vite + React + TypeScript** — app shell. Vite is pinned to v6 because the
  installed Node (22.11) predates v7's minimum (22.12).
- **@xyflow/react** (React Flow) — the node-graph editor canvas.
- **@bufbuild/protobuf** — decodes messages defined in `../proto/`, generated
  via `buf` + `protoc-gen-es` into `src/gen/` (checked in).
- **uplot / lightweight-charts** — installed for upcoming indicator plots and
  price charts; not wired up yet.

## Commands

```sh
npm run dev        # dev server on http://localhost:5173
npm run mock:feed  # protobuf mock of the C++ feed on ws://localhost:8765
npm run build      # typecheck + production bundle
npm run proto:gen  # regenerate src/gen/ after editing ../proto/*.proto
```

The UI connects to `ws://localhost:8765` by default; override with
`VITE_FEED_URL`. Until the C++ side serves protobuf, run `npm run mock:feed`
alongside `npm run dev`.

## Layout

- `src/lib/market_feed.ts` — WebSocket hook decoding `MarketDataEvent` frames.
- `src/components/GraphCanvas.tsx` — React Flow canvas (placeholder nodes).
- `src/components/TickerPanel.tsx` — plain-text bid/ask/spread readout.
- `src/gen/` — generated protobuf code, do not edit by hand.
- `scripts/mock_feed.ts` — dev stand-in for the backend feed.
- `scripts/smoke_client.ts` — one-shot decode check against a running feed.
