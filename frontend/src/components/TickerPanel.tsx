import type { MarketFeed } from "../lib/market_feed.ts";

const kPriceFormat = new Intl.NumberFormat("en-US", {
  minimumFractionDigits: 2,
  maximumFractionDigits: 2,
});

export default function TickerPanel({ feed }: { feed: MarketFeed }) {
  const { status, ticker } = feed;

  return (
    <section className="ticker-panel">
      <header className="ticker-header">
        <h2>Book Ticker</h2>
        <span className={`feed-status feed-status--${status}`}>{status}</span>
      </header>
      {ticker === null ? (
        <p className="ticker-waiting">Waiting for data…</p>
      ) : (
        <dl className="ticker-fields">
          <dt>Symbol</dt>
          <dd>{ticker.symbol}</dd>
          <dt>Bid</dt>
          <dd className="ticker-bid">
            {kPriceFormat.format(ticker.bidPrice)} × {ticker.bidQty}
          </dd>
          <dt>Ask</dt>
          <dd className="ticker-ask">
            {kPriceFormat.format(ticker.askPrice)} × {ticker.askQty}
          </dd>
          <dt>Spread</dt>
          <dd>{kPriceFormat.format(ticker.askPrice - ticker.bidPrice)}</dd>
        </dl>
      )}
    </section>
  );
}
