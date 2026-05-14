// Standard library
#include <iostream> // std::cout — learn more: https://en.cppreference.com/w/cpp/io/cout
#include <string> // std::string — learn more: https://en.cppreference.com/w/cpp/string/basic_string

// OpenSSL X.509 API — needed to decode DER certificates from the Windows store.
// d2i_X509, X509_STORE_add_cert, X509_free, SSL_CTX_get_cert_store.
// Learn more: https://docs.openssl.org/master/man3/d2i_X509/
#include <openssl/x509.h>

// crypt32.lib provides CertOpenSystemStoreA and related Win32 functions.
// This pragma tells the linker to pull it in automatically (MSVC / Clang-cl).
// In CMake you can instead write: target_link_libraries(<target> crypt32)
#pragma comment(lib, "crypt32.lib")

// Boost.Asio: cross-platform async I/O (networking, timers).
// Reference: https://www.boost.org/doc/libs/release/doc/html/boost_asio.html
// Book: "Asio C++ Network Programming" by Torjo
#include <boost/asio/connect.hpp> // free function asio::connect() — iterates resolver results
#include <boost/asio/ssl.hpp> // TLS/SSL support layered on top of Asio sockets

// Boost.Beast: HTTP and WebSocket built on top of Boost.Asio.
// Reference:
// https://www.boost.org/doc/libs/release/libs/beast/doc/html/index.html
// Thorough examples: https://github.com/boostorg/beast/tree/develop/example
#include <boost/beast/core.hpp> // flat_buffer, buffers_to_string, get_lowest_layer
#include <boost/beast/websocket.hpp> // websocket::stream
#include <boost/beast/websocket/ssl.hpp> // websocket over ssl — teardown/handshake glue

// windows.h must come first — wincrypt.h is not self-contained and depends on
// base types like ULONG_PTR, HANDLE, DWORD that windows.h defines.
// Learn more:
// https://learn.microsoft.com/en-us/windows/win32/winprog/windows-data-types
#include <windows.h>

// Windows Certificate Store API — needed to load trusted root CAs into OpenSSL.
// OpenSSL does NOT use the Windows cert store automatically; we must bridge
// them. wincrypt.h: CertOpenSystemStore, CertEnumCertificatesInStore,
// CertCloseStore. Learn more:
// https://learn.microsoft.com/en-us/windows/win32/seccrypto/cryptography-functions
#include <wincrypt.h>

#include "data_feed.h"

// Namespace aliases — Beast/Asio have deep namespaces; aliases match official
// docs.
namespace beast =
    boost::beast; // https://www.boost.org/doc/libs/release/libs/beast
namespace websocket = beast::
    websocket; // https://www.boost.org/doc/libs/release/libs/beast/doc/html/beast/using_websocket.html
namespace asio = boost::
    asio; // https://www.boost.org/doc/libs/release/doc/html/boost_asio.html
namespace ssl = asio::
    ssl; // https://www.boost.org/doc/libs/release/doc/html/boost_asio/reference/ssl__context.html
using tcp =
    asio::ip::tcp; // shorthand for the TCP protocol tag used throughout Asio

int main() {
  // ── Connection parameters ────────────────────────────────────────────────
  // Coinbase Exchange sandbox WebSocket feed (public / unauthenticated).
  // Sandbox lets you test without real money; data is simulated.
  // Sandbox docs: https://docs.cdp.coinbase.com/exchange/docs/sandbox
  const std::string kHost = "ws-feed-public.sandbox.exchange.coinbase.com";
  const std::string kPort = "443"; // WSS always uses port 443 (same as HTTPS)
  const std::string kPath = "/";   // URL path; Coinbase uses the root path

  // Subscribe message for the Coinbase Exchange WebSocket Feed API.
  // NOTE: Exchange API uses "channels": [...] (plural, array) — different from
  //   the Advanced Trade API which uses "channel": "..." (singular string).
  //   "ticker" channel → level-1 best bid/ask + last price per match.
  //   "product_ids": ["BTC-USD"] → which trading pair to subscribe to.
  // Exchange channel reference:
  // https://docs.cdp.coinbase.com/exchange/docs/websocket-channels
  // Raw string literal R"(...)" avoids escaping every quote inside the JSON.
  // Learn more: https://en.cppreference.com/w/cpp/language/string_literal
  const std::string kSubscribe = R"({
    "type": "subscribe",
    "product_ids": ["BTC-USD"],
    "channels": ["ticker"]
  })";

  try {
    // ── I/O context ─────────────────────────────────────────────────────────
    // io_context is Asio's event loop / I/O scheduler.
    // Every socket, resolver, and timer must be bound to one.
    // Here we drive it synchronously (blocking calls); for async use ioc.run().
    // Learn more:
    // https://www.boost.org/doc/libs/release/doc/html/boost_asio/reference/io_context.html
    asio::io_context ioc;

    // ── TLS context ──────────────────────────────────────────────────────────
    // ssl::context carries TLS settings: protocol, certificates, verify mode.
    // tls_client = negotiate the highest mutually-supported version (TLS 1.2+).
    // Learn more:
    // https://www.boost.org/doc/libs/release/doc/html/boost_asio/reference/ssl__context.html
    ssl::context ssl_ctx{ssl::context::tls_client};

    // ── Load Windows trusted root CAs into OpenSSL ───────────────────────────
    // On Windows, set_default_verify_paths() does nothing useful — OpenSSL has
    // no knowledge of the Windows Certificate Store. We must walk the Win32
    // CertStore ourselves and inject each certificate into OpenSSL's
    // X509_STORE.
    //
    // CertOpenSystemStoreA: opens the named logical certificate store.
    //   "ROOT" = Trusted Root Certification Authorities (the anchors).
    //   First arg (0) means: use the current user's store (HKEY_CURRENT_USER).
    // Learn more:
    // https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-certopensystemstorea
    HCERTSTORE win_store = CertOpenSystemStoreA(0, "ROOT");

    // SSL_CTX_get_cert_store: returns the X509_STORE OpenSSL uses to verify
    // the server's certificate chain. We will add our Windows certs here.
    X509_STORE *ssl_store = SSL_CTX_get_cert_store(ssl_ctx.native_handle());

    // Walk every certificate in the Windows Trusted Root store.
    // CertEnumCertificatesInStore returns nullptr when there are no more certs.
    // Learn more:
    // https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-certenumcertificatesinstore
    for (PCCERT_CONTEXT cert = nullptr;
         (cert = CertEnumCertificatesInStore(win_store, cert)) != nullptr;) {
      // cert->pbCertEncoded: raw DER-encoded certificate bytes.
      // cert->cbCertEncoded: byte count.
      // d2i_X509 decodes DER bytes → an OpenSSL X509 object.
      // The pointer `p` must be a copy: d2i_X509 advances it past the decoded
      // data. Learn more: https://docs.openssl.org/master/man3/d2i_X509/
      const unsigned char *p = cert->pbCertEncoded;
      X509 *x509 = d2i_X509(nullptr, &p, cert->cbCertEncoded);
      if (x509) {
        // Add the decoded certificate to OpenSSL's trust store.
        // Returns 1 on success; 0 if already present (not a fatal error).
        X509_STORE_add_cert(ssl_store, x509);
        X509_free(
            x509); // Release this reference; the store holds its own copy.
      }
    }

    // CertCloseStore: release the handle to the Windows cert store.
    // Flag 0 = close gracefully (don't force-close if other code holds a ref).
    // Learn more:
    // https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-certclosestore
    CertCloseStore(win_store, 0);

    // Require the server to present a certificate signed by a trusted CA.
    // verify_peer = actually verify; skipping this would silently allow MITM.
    // Learn more:
    // https://www.boost.org/doc/libs/release/doc/html/boost_asio/reference/ssl__context/set_verify_mode.html
    ssl_ctx.set_verify_mode(ssl::verify_peer);

    // ── WebSocket stream
    // ────────────────────────────────────────────────────── Beast uses a
    // "layered stream" model — each layer wraps the one below:
    //   websocket::stream  ← WebSocket framing, masking, ping/pong, close
    //     ssl::stream      ← TLS encryption / decryption
    //       tcp::socket    ← raw TCP bytes over the network
    // Learn more:
    // https://www.boost.org/doc/libs/release/libs/beast/doc/html/beast/using_websocket.html
    websocket::stream<ssl::stream<tcp::socket>> ws{ioc, ssl_ctx};

    // ── Step 1: DNS resolution ───────────────────────────────────────────────
    // resolve() is synchronous here; returns a range of endpoint candidates.
    // Each endpoint is an (IP, port) pair; connect() will try them in order.
    //
    // ── Resolver ─────────────────────────────────────────────────────────────
    // Resolver turns a hostname + port string into a list of TCP endpoints.
    // Internally calls getaddrinfo(); returns both IPv4 and IPv6 candidates.
    // Learn more:
    // https://www.boost.org/doc/libs/release/doc/html/boost_asio/reference/ip__basic_resolver.html

    // ── Step 2: TCP connect
    // ─────────────────────────────────────────────────── asio::connect() (free
    // function, NOT tcp::socket::connect()) iterates the resolver results and
    // tries each endpoint until one succeeds. tcp::socket::connect() only takes
    // a *single* endpoint — passing a range to it is the bug this fixes.
    // get_lowest_layer() pierces stream wrappers to reach the raw tcp::socket.
    // Learn more:
    // https://www.boost.org/doc/libs/release/doc/html/boost_asio/reference/connect.html
    // Beast layer model:
    // https://www.boost.org/doc/libs/release/libs/beast/doc/html/beast/ref/boost__beast__get_lowest_layer.html
    asio::connect(beast::get_lowest_layer(ws),
                  tcp::resolver{ioc}.resolve(kHost, kPort));

    // ── Step 3: SNI (Server Name Indication) ─────────────────────────────────
    // SNI tells the server which hostname we're connecting to.
    // Needed because many servers share one IP but serve different TLS certs.
    // Must be set BEFORE the TLS handshake (sent inside the ClientHello
    // message). SSL_set_tlsext_host_name is an OpenSSL macro wrapping
    // SSL_ctrl(). RFC 6066 §3:
    // https://datatracker.ietf.org/doc/html/rfc6066#section-3
    SSL_set_tlsext_host_name(ws.next_layer().native_handle(), kHost.c_str());

    // ── Step 4: TLS handshake
    // ───────────────────────────────────────────────── Negotiates cipher
    // suite, exchanges certificates, derives session keys. stream_base::client
    // = we initiate the handshake (not the server). After this returns, all
    // bytes flowing over TCP are encrypted. Learn more:
    // https://www.boost.org/doc/libs/release/doc/html/boost_asio/reference/ssl__stream/handshake.html
    ws.next_layer().handshake(ssl::stream_base::client);

    // ── Step 5: WebSocket handshake (HTTP Upgrade) ───────────────────────────
    // Beast sends:  GET / HTTP/1.1 \r\n Upgrade: websocket \r\n ...
    // Server replies: 101 Switching Protocols
    // After this, the connection speaks the WebSocket framing protocol (RFC
    // 6455). RFC 6455 §4:
    // https://datatracker.ietf.org/doc/html/rfc6455#section-4 Beast handshake
    // reference:
    // https://www.boost.org/doc/libs/release/libs/beast/doc/html/beast/ref/boost__beast__websocket__stream/handshake.html
    ws.handshake(kHost, kPath);

    // ── Step 6: Subscribe
    // ───────────────────────────────────────────────────── Send the JSON
    // subscription message as a single WebSocket text frame. asio::buffer()
    // wraps the string in a ConstBuffer (zero-copy view). Learn more:
    // https://www.boost.org/doc/libs/release/libs/beast/doc/html/beast/ref/boost__beast__websocket__stream/write.html
    ws.write(asio::buffer(kSubscribe));

    // ── Step 7: Read loop
    // ───────────────────────────────────────────────────── flat_buffer:
    // Beast's contiguous growable byte buffer. Declared outside the loop so the
    // heap allocation is reused each iteration. Learn more:
    // https://www.boost.org/doc/libs/release/libs/beast/doc/html/beast/ref/boost__beast__flat_buffer.html
    beast::flat_buffer buf;

    for (;;) {
      buf.clear(); // Reset read position without freeing the underlying memory.

      // Blocking read: waits until a complete WebSocket message arrives.
      // Beast reassembles multi-frame messages transparently.
      // Learn more:
      // https://www.boost.org/doc/libs/release/libs/beast/doc/html/beast/ref/boost__beast__websocket__stream/read.html
      ws.read(buf);

      // buffers_to_string() copies the buffer contents into a std::string.
      // Streaming alternative (avoids the copy):
      //   std::cout << beast::make_printable(buf.data()) << "\n";
      // Learn more:
      // https://www.boost.org/doc/libs/release/libs/beast/doc/html/beast/ref/boost__beast__buffers_to_string.html
      std::cout << beast::buffers_to_string(buf.data()) << "\n";
    }

  } catch (const std::exception &e) {
    // beast/asio throw boost::system::system_error (a subclass of
    // std::exception) on any I/O failure (connection refused, TLS cert
    // rejected, etc.). Learn more:
    // https://www.boost.org/doc/libs/release/libs/system/doc/html/system.html
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
