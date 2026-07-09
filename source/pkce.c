#include <3ds.h>
#include <string.h>
#include <mbedtls/sha256.h>

#include "pkce.h"

static const char pkceChars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789"
    "-._~";

static const char base64UrlChars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789"
    "-_";

static u32 pkceRngState = 0x13572468;

static u32 Pkce_Rand32(void)
{
    u64 tick = svcGetSystemTick();

    pkceRngState ^= (u32)(tick ^ (tick >> 32));
    pkceRngState = pkceRngState * 1664525u + 1013904223u;

    return pkceRngState;
}

static int Base64Url_EncodeNoPadding(const unsigned char *in, int inLen, char *out, int outSize)
{
    int outLen = 0;

    for(int i = 0; i < inLen; i += 3)
    {
        unsigned int v = ((unsigned int)in[i]) << 16;
        int remaining = inLen - i;

        if(remaining > 1)
            v |= ((unsigned int)in[i + 1]) << 8;

        if(remaining > 2)
            v |= in[i + 2];

        if(outLen + 4 >= outSize)
            return -1;

        out[outLen++] = base64UrlChars[(v >> 18) & 0x3F];
        out[outLen++] = base64UrlChars[(v >> 12) & 0x3F];

        if(remaining > 1)
            out[outLen++] = base64UrlChars[(v >> 6) & 0x3F];

        if(remaining > 2)
            out[outLen++] = base64UrlChars[v & 0x3F];
    }

    out[outLen] = '\0';
    return 0;
}

int Pkce_GenerateVerifier(char *out, int outSize)
{
    int len = 64;

    if(outSize < len + 1)
        return -1;

    for(int i = 0; i < len; i++)
        out[i] = pkceChars[Pkce_Rand32() % (sizeof(pkceChars) - 1)];

    out[len] = '\0';
    return 0;
}

int Pkce_GenerateState(char *out, int outSize)
{
    int len = 32;

    if(outSize < len + 1)
        return -1;

    for(int i = 0; i < len; i++)
        out[i] = pkceChars[Pkce_Rand32() % (sizeof(pkceChars) - 1)];

    out[len] = '\0';
    return 0;
}

int Pkce_GenerateChallengeS256(const char *verifier, char *out, int outSize)
{
    unsigned char digest[32];

    mbedtls_sha256((const unsigned char *)verifier, strlen(verifier), digest, 0);

    return Base64Url_EncodeNoPadding(digest, sizeof(digest), out, outSize);
}