import GraphCanvas from "./components/GraphCanvas.tsx";
import TickerPanel from "./components/TickerPanel.tsx";
import { useMarketFeed } from "./lib/market_feed.ts";

const kFeedUrl: string =
  import.meta.env.VITE_FEED_URL ?? "ws://localhost:8765";

export default function App() {
  const feed = useMarketFeed(kFeedUrl);

  return (
    <div className="app-layout">
      <main className="graph-pane">
        <GraphCanvas />
      </main>
      <aside className="side-pane">
        <TickerPanel feed={feed} />
      </aside>
    </div>
  );
}
