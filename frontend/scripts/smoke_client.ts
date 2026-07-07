// One-shot check that the feed emits decodable MarketDataEvent frames.

import { fromBinary } from "@bufbuild/protobuf";
import { WebSocket } from "ws";

import { MarketDataEventSchema } from "../src/gen/market_data_pb.ts";

const socket = new WebSocket("ws://localhost:8765");
socket.binaryType = "arraybuffer";

socket.on("message", (data: Buffer) => {
  const event = fromBinary(MarketDataEventSchema, new Uint8Array(data));
  console.log(JSON.stringify(event, (_, v) => (typeof v === "bigint" ? v.toString() : v)));
  socket.close();
  process.exit(0);
});

setTimeout(() => {
  console.error("no frame received within 5s");
  process.exit(1);
}, 5000);
