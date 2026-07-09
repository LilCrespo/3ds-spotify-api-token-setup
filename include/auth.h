#ifndef AUTH_H
#define AUTH_H

#include <stddef.h>

int Auth_ExtractCode(const char *input, char *out, size_t outSize);
int Auth_ExtractState(const char *input, char *out, size_t outSize);

int Auth_ExchangeCode(
    const char *clientId,
    const char *code,
    const char *codeVerifier,
    char *response,
    size_t responseSize,
    long *httpCode
);

int Json_GetString(const char *json, const char *key, char *out, size_t outSize);

#endif