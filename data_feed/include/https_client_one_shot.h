#ifndef DATA_FEED_GENERIC_HTTPS_CLIENT_H_
#define DATA_FEED_GENERIC_HTTPS_CLIENT_H_

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <string_view>

namespace data_feed {

class HttpsClientOneShot {
 public:
  template <typename T>
  using response = boost::beast::http::response<T>;

  template <typename T>
  using request = boost::beast::http::request<T>;

  template <typename T>
  using ssl_stream = boost::beast::ssl_stream<T>;

  using tcp_stream = boost::beast::tcp_stream;
  using resolver = boost::asio::ip::tcp::resolver;
  using string_body = boost::beast::http::string_body;
  using io_context = boost::asio::io_context;
  using flat_buffer = boost::beast::flat_buffer;

  using Result = response<string_body>;

  Result MakeRequest(request<string_body>& body,
                     std::string_view host,
                     std::string_view port);
};

}  // namespace data_feed

#endif  // ! DATA_FEED_GENERIC_HTTPS_CLIENT_H_
