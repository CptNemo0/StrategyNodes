import { fromBinary } from "@bufbuild/protobuf";
import { useEffect, useState } from "react";

import {
  type BookTicker,
  MarketDataEventSchema,
} from "../gen/market_data_pb.ts";

export type FeedStatus = "connecting" | "open" | "closed";

export interface MarketFeed {
  status: FeedStatus;
  ticker: BookTicker | null;
}

const kReconnectDelayMs = 1000;

// Subscribes to the backend WebSocket and decodes protobuf-framed
// MarketDataEvent messages. Reconnects until the component unmounts.
export function useMarketFeed(url: string): MarketFeed {
  const [status, setStatus] = useState<FeedStatus>("connecting");
  const [ticker, setTicker] = useState<BookTicker | null>(null);

  useEffect(() => {
    let socket: WebSocket | null = null;
    let retryTimer: number | undefined;
    let disposed = false;

    const connect = () => {
      setStatus("connecting");
      socket = new WebSocket(url);
      socket.binaryType = "arraybuffer";
      socket.onopen = () => setStatus("open");
      socket.onmessage = (message: MessageEvent) => {
        if (!(message.data instanceof ArrayBuffer)) {
          return;
        }
        const event = fromBinary(
          MarketDataEventSchema,
          new Uint8Array(message.data),
        );
        if (event.payload.case === "bookTicker") {
          console.log(event.payload.value);
          setTicker(event.payload.value);
        }
      };
      socket.onclose = () => {
        if (disposed) {
          return;
        }
        setStatus("closed");
        retryTimer = window.setTimeout(connect, kReconnectDelayMs);
      };
    };

    connect();
    return () => {
      disposed = true;
      window.clearTimeout(retryTimer);
      socket?.close();
    };
  }, [url]);

  return { status, ticker };
}
