#ifndef DATA_FEED_TLS_CONTEXT_H_
#define DATA_FEED_TLS_CONTEXT_H_

#include <boost/asio/ssl/context.hpp>

namespace data_feed {

// RAII wrapper around a boost::asio::ssl::context configured for TLS client
// use. On construction it loads the platform trust store and enables peer
// verification, so a single immutable context can be shared (read-only) across
// every product pipeline — sharing it does not couple pipelines.
//
// Note: OpenSSL >= 1.1 self-initializes, so no explicit library-init call is
// needed here. native() returns a mutable reference only because Beast's
// websocket/ssl streams must be constructed from a non-const ssl::context&;
// the context itself is not mutated after construction.
class TlsContext {
 public:
  TlsContext();

  TlsContext(const TlsContext&) = delete;
  TlsContext& operator=(const TlsContext&) = delete;

  boost::asio::ssl::context& native() { return ctx_; }

 private:
  boost::asio::ssl::context ctx_;
};

}  // namespace data_feed

#endif  // !DATA_FEED_TLS_CONTEXT_H_
