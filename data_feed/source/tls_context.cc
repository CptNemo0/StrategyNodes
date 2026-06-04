#include "tls_context.h"

#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>

#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/verify_mode.hpp>

#ifdef _WIN32
#include <windows.h>

#include <openssl/x509.h>
#include <wincrypt.h>

// CertOpenSystemStoreA and Win32 related functions.
#pragma comment(lib, "crypt32.lib")
#endif

namespace data_feed {

namespace {

#ifdef _WIN32
void LoadWindowsRootStore(boost::asio::ssl::context& ctx) {
  HCERTSTORE win_store = CertOpenSystemStoreA(0, "ROOT");
  if (win_store == nullptr) {
    return;
  }

  X509_STORE* ssl_store = SSL_CTX_get_cert_store(ctx.native_handle());

  for (PCCERT_CONTEXT cert = nullptr;
       (cert = CertEnumCertificatesInStore(win_store, cert)) != nullptr;) {
    // d2i_X509 advances the pointer it is given, so pass a copy.
    const unsigned char* der = cert->pbCertEncoded;
    X509* x509 = d2i_X509(nullptr, &der, cert->cbCertEncoded);
    if (x509 != nullptr) {
      X509_STORE_add_cert(ssl_store, x509);
      X509_free(x509);  // The store holds its own reference.
    }
  }

  CertCloseStore(win_store, 0);
}
#endif

}  // namespace

TlsContext::TlsContext() : ctx_(boost::asio::ssl::context::tls_client) {
#ifdef _WIN32
  LoadWindowsRootStore(ctx_);
#else
  ctx_.set_default_verify_paths();
#endif
  ctx_.set_verify_mode(boost::asio::ssl::verify_peer);
}

}  // namespace data_feed
