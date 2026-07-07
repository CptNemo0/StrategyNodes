// Development stand-in for the C++ backend: pushes protobuf-encoded
// MarketDataEvent frames over a WebSocket. Run with `npm run mock:feed`.

import { create, toBinary } from "@bufbuild/protobuf";
import { WebSocketServer } from "ws";

import { MarketDataEventSchema } from "../src/gen/market_data_pb.ts";

const kPort = 8765;
const kIntervalMs = 250;

let mid = 60000;

function nextEvent(): Uint8Array {
  mid += (Math.random() - 0.5) * 20;
  const spread = 0.5 + Math.random() * 2;
  return toBinary(
    MarketDataEventSchema,
    create(MarketDataEventSchema, {
      payload: {
        case: "bookTicker",
        value: {
          symbol: "BTC/USD",
          bidPrice: mid - spread / 2,
          bidQty: Math.round(Math.random() * 500) / 100,
          askPrice: mid + spread / 2,
          askQty: Math.round(Math.random() * 500) / 100,
          timestampNs: BigInt(Date.now()) * 1000000n,
        },
      },
    }),
  );
}

const server = new WebSocketServer({ port: kPort });
console.log(`mock feed listening on ws://localhost:${kPort}`);

setInterval(() => {
  const frame = nextEvent();
  for (const client of server.clients) {
    client.send(frame);
  }
}, kIntervalMs);
