#include "https_client_one_shot.h"

#include <openssl/tls1.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/basic_resolver_results.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/impl/read.hpp>
#include <boost/beast/http/impl/write.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <stdexcept>
#include <string_view>
//
#include "tls_context.h"

namespace data_feed {

HttpsClientOneShot::Result HttpsClientOneShot::MakeRequest(
    request<string_body>& body,
    std::string_view host,
    std::string_view port) {
  TlsContext tls;
  io_context ioc;
  ssl_stream<tcp_stream> stream{ioc, tls.native()};
  resolver resolver{ioc};
  flat_buffer buffer;
  response<string_body> res;

  namespace beast = boost::beast;
  namespace ssl = boost::asio::ssl;
  namespace http = boost::beast::http;

  if (SSL_set_tlsext_host_name(stream.native_handle(), host.data()) != 1) {
    throw std::runtime_error("Failed to set TLS SNI hostname");
  }

  beast::get_lowest_layer(stream).connect(resolver.resolve(host, port));
  stream.handshake(ssl::stream_base::client);

  http::write(stream, body);
  http::read(stream, buffer, res);

  beast::error_code ec;
  stream.shutdown(ec);

  return res;
}

}  // namespace data_feed
