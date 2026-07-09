#ifndef PKCE_H
#define PKCE_H

int Pkce_GenerateVerifier(char *out, int outSize);
int Pkce_GenerateChallengeS256(const char *verifier, char *out, int outSize);
int Pkce_GenerateState(char *out, int outSize);

#endif