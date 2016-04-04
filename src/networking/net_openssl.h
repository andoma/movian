#pragma once

int openssl_verify_connection(SSL *ssl, const char *hostname,
                              char *errbuf, size_t errlen);

