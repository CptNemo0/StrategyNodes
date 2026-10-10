#include "net/tls_context.h"

#include <gtest/gtest.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>

namespace net {
namespace {

// Connections made with the context verify the venue's certificate.
TEST(TlsContextTest, VerifiesThePeer) {
  TlsContext tls;

  EXPECT_NE(
      SSL_CTX_get_verify_mode(tls.native().native_handle()) & SSL_VERIFY_PEER,
      0);
}

// The platform's root certificates are trusted, so verification can
// succeed.
TEST(TlsContextTest, TrustsThePlatformRootCertificates) {
  TlsContext tls;

#ifdef _WIN32
  // Loaded from the Windows ROOT store, which is never empty.
  EXPECT_GT(sk_X509_OBJECT_num(X509_STORE_get0_objects(
                SSL_CTX_get_cert_store(tls.native().native_handle()))),
            0);
#else
  GTEST_SKIP() << "Default verify paths are resolved lazily by OpenSSL";
#endif
}

// Each TlsContext owns its own OpenSSL context.
TEST(TlsContextTest, ContextsAreIndependent) {
  TlsContext first;
  TlsContext second;

  EXPECT_NE(first.native().native_handle(), second.native().native_handle());
}

}  // namespace
}  // namespace net
