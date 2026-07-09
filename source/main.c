#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <malloc.h>
#include <curl/curl.h>
#include <stdbool.h>
#include <stdint.h>

#include "pkce.h"
#include "auth.h"
#include "qrcodegen.h"
#include "qrscan.h"

#define CONFIG_ROOT "sdmc:/config"
#define CONFIG_DIR  "sdmc:/config/spotify"
#define TOKEN_PATH  "sdmc:/config/spotify/token.json"

#define SPOTIFY_REDIRECT_URI_ENCODED "https%3A%2F%2Flilcrespo.github.io%2Fspotify-auth-qr-callback%2F"
#define SPOTIFY_REDIRECT_URI         "https://lilcrespo.github.io/spotify-auth-qr-callback/"

#define SPOTIFY_SCOPE_ENCODED "user-modify-playback-state%20user-read-playback-state%20user-read-currently-playing"

#define SOC_ALIGN      0x1000
#define SOC_BUFFER_SIZE 0x100000

#define BOTTOM_SCREEN_WIDTH  320
#define BOTTOM_SCREEN_HEIGHT 240
#define QR_QUIET_ZONE        4

#define APP_EXIT_REQUESTED (-9001)

static u32 *socBuffer = NULL;

static void WaitForExit(void)
{
    printf("\nPress START to exit.\n");

    while(aptMainLoop())
    {
        hidScanInput();

        if(hidKeysDown() & KEY_START)
            break;

        gspWaitForVBlank();
    }
}

static int WaitForA(void)
{
    while(aptMainLoop())
    {
        hidScanInput();

        u32 keys = hidKeysDown();

        if(keys & KEY_START)
            return APP_EXIT_REQUESTED;

        if(keys & KEY_A)
            return 0;

        gspWaitForVBlank();
    }

    return APP_EXIT_REQUESTED;
}

static int InputText(const char *hint, char *out, size_t outSize)
{
    SwkbdState swkbd;
    SwkbdButton button;

    if(outSize == 0)
        return -1;

    out[0] = '\0';

    swkbdInit(&swkbd, SWKBD_TYPE_WESTERN, 2, outSize - 1);
    swkbdSetHintText(&swkbd, hint);
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);

    button = swkbdInputText(&swkbd, out, outSize);

    return button == SWKBD_BUTTON_CONFIRM ? 0 : -2;
}

static void DrawPixelBottom(u8 *framebuffer, int x, int y, u8 r, u8 g, u8 b)
{
    u32 offset;

    if(x < 0 || x >= BOTTOM_SCREEN_WIDTH || y < 0 || y >= BOTTOM_SCREEN_HEIGHT)
        return;

    offset = (u32)((x * BOTTOM_SCREEN_HEIGHT) + (BOTTOM_SCREEN_HEIGHT - 1 - y)) * 3;

    framebuffer[offset + 0] = b;
    framebuffer[offset + 1] = g;
    framebuffer[offset + 2] = r;
}

static void DrawFilledRectBottom(u8 *framebuffer, int x, int y, int width, int height, u8 r, u8 g, u8 b)
{
    for(int py = y; py < y + height; py++)
    {
        for(int px = x; px < x + width; px++)
            DrawPixelBottom(framebuffer, px, py, r, g, b);
    }
}

static void ClearBottomScreen(u8 r, u8 g, u8 b)
{
    u8 *framebuffer = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);

    DrawFilledRectBottom(
        framebuffer,
        0,
        0,
        BOTTOM_SCREEN_WIDTH,
        BOTTOM_SCREEN_HEIGHT,
        r,
        g,
        b
    );

    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

static int DrawQrCodeOnBottom(const uint8_t *qrcode)
{
    int qrSize = qrcodegen_getSize(qrcode);
    int totalSize = qrSize + QR_QUIET_ZONE * 2;
    int scaleX = BOTTOM_SCREEN_WIDTH / totalSize;
    int scaleY = BOTTOM_SCREEN_HEIGHT / totalSize;
    int scale = scaleX < scaleY ? scaleX : scaleY;
    int renderedSize;
    int xOffset;
    int yOffset;
    u8 *framebuffer;

    if(scale < 1)
        return -1;

    renderedSize = totalSize * scale;
    xOffset = (BOTTOM_SCREEN_WIDTH - renderedSize) / 2;
    yOffset = (BOTTOM_SCREEN_HEIGHT - renderedSize) / 2;

    framebuffer = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);

    DrawFilledRectBottom(
        framebuffer,
        0,
        0,
        BOTTOM_SCREEN_WIDTH,
        BOTTOM_SCREEN_HEIGHT,
        255,
        255,
        255
    );

    for(int y = 0; y < qrSize; y++)
    {
        for(int x = 0; x < qrSize; x++)
        {
            if(qrcodegen_getModule(qrcode, x, y))
            {
                int drawX = xOffset + (x + QR_QUIET_ZONE) * scale;
                int drawY = yOffset + (y + QR_QUIET_ZONE) * scale;

                DrawFilledRectBottom(framebuffer, drawX, drawY, scale, scale, 0, 0, 0);
            }
        }
    }

    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();

    return 0;
}

static int ShowAuthQrCode(const char *authUrl)
{
    static uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(qrcodegen_VERSION_MAX)];
    static uint8_t tempBuffer[qrcodegen_BUFFER_LEN_FOR_VERSION(qrcodegen_VERSION_MAX)];
    bool ok;

    ok = qrcodegen_encodeText(
        authUrl,
        tempBuffer,
        qrcode,
        qrcodegen_Ecc_LOW,
        1,
        qrcodegen_VERSION_MAX,
        qrcodegen_Mask_AUTO,
        false
    );

    if(!ok)
        return -1;

    consoleClear();

    printf("Spotify Setup\n\n");
    printf("Scan the QR code on the bottom screen.\n\n");
    printf("Log in on your phone or PC.\n");
    printf("The callback page will show another QR.\n\n");
    printf("Press A when the callback QR is visible.\n");

    if(DrawQrCodeOnBottom(qrcode) != 0)
        return -2;

    int waitRet = WaitForA();

    ClearBottomScreen(0, 0, 0);

    return waitRet;
}


#define CALLBACK_QR_MAX_PARTS 16
#define CALLBACK_QR_PART_MAX  96

static int CompactHexValue(char c)
{
    if(c >= '0' && c <= '9')
        return c - '0';

    if(c >= 'a' && c <= 'f')
        return c - 'a' + 10;

    if(c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return -1;
}

static int CompactUrlDecode(const char *in, char *out, size_t outSize)
{
    size_t outLen = 0;

    if(!in || !out || outSize == 0)
        return -1;

    while(*in)
    {
        if(outLen + 1 >= outSize)
            return -2;

        if(*in == '%' && in[1] && in[2])
        {
            int hi = CompactHexValue(in[1]);
            int lo = CompactHexValue(in[2]);

            if(hi >= 0 && lo >= 0)
            {
                out[outLen++] = (char)((hi << 4) | lo);
                in += 3;
                continue;
            }
        }

        out[outLen++] = (*in == '+') ? ' ' : *in;
        in++;
    }

    out[outLen] = '\0';
    return 0;
}

static int CompactExtractParam(const char *input, const char *param, char *out, size_t outSize)
{
    char needle[16];
    const char *start;
    char encoded[512];
    size_t len = 0;

    if(!input || !param || !out || outSize == 0)
        return -1;

    snprintf(needle, sizeof(needle), "%s=", param);
    start = strstr(input, needle);

    if(!start)
        return -2;

    start += strlen(needle);

    while(start[len] &&
          start[len] != '&' &&
          start[len] != '#' &&
          start[len] != ' ' &&
          start[len] != '\t' &&
          start[len] != '\r' &&
          start[len] != '\n')
    {
        if(len + 1 >= sizeof(encoded))
            return -3;

        encoded[len] = start[len];
        len++;
    }

    encoded[len] = '\0';

    if(len == 0)
        return -4;

    return CompactUrlDecode(encoded, out, outSize);
}

static int CompactExtractIntParam(const char *input, const char *param, int *out)
{
    char value[16];
    char *end = NULL;
    long n;

    if(!out)
        return -1;

    if(CompactExtractParam(input, param, value, sizeof(value)) != 0)
        return -2;

    n = strtol(value, &end, 10);

    if(!end || *end != '\0')
        return -3;

    if(n < 0 || n > 1000)
        return -4;

    *out = (int)n;
    return 0;
}

static int ScanSingleCallbackQr(char *out, size_t outSize)
{
    return QrScan_ReadPayload(out, outSize);
}

static int ScanCallbackPayload(char *authCode, size_t authCodeSize, char *returnedState, size_t returnedStateSize)
{
    char payload[512];
    char chunk[CALLBACK_QR_PART_MAX + 1];
    int totalParts = 0;
    size_t codeLen = 0;

    if(!authCode || authCodeSize == 0 || !returnedState || returnedStateSize == 0)
        return -1;

    authCode[0] = '\0';
    returnedState[0] = '\0';

    consoleClear();
    printf("Spotify Setup\n\n");
    printf("Scan QR 1 from the callback page.\n");
    printf("This can be a single QR or the meta QR.\n\n");
    printf("Press A when ready.\n");

    if(WaitForA() == APP_EXIT_REQUESTED)
        return APP_EXIT_REQUESTED;

    int ret = ScanSingleCallbackQr(payload, sizeof(payload));

    if(ret != 0)
        return ret;

    /*
     * Multipart mode must be detected before the single-QR fallback.
     * The first multipart QR is metadata only: v=2&n=<parts>&s=<state>.
     * Older Auth_ExtractCode() versions may treat any non-empty payload as a
     * manual authorization code, which would send "v=2" to Spotify.
     */
    if(CompactExtractIntParam(payload, "n", &totalParts) == 0)
    {
        if(totalParts < 1 || totalParts > CALLBACK_QR_MAX_PARTS)
        {
            consoleClear();
            printf("Spotify Setup\n\n");
            printf("Invalid callback QR part count.\n");
            printf("Parts: %d\n", totalParts);
            return -20;
        }

        if(CompactExtractParam(payload, "s", returnedState, returnedStateSize) != 0)
            returnedState[0] = '\0';
    }
    else
    {
        int partIndex = 0;

        /* If the user scanned a chunk QR first, do not treat it as a full code. */
        if(CompactExtractIntParam(payload, "i", &partIndex) == 0)
        {
            consoleClear();
            printf("Spotify Setup\n\n");
            printf("This is a callback code part, not QR 1.\n");
            printf("Go back to the callback page start QR.\n");
            return -25;
        }

        /* Backwards compatible: old page with one QR containing c=...&s=... or JSON. */
        if(Auth_ExtractCode(payload, authCode, authCodeSize) == 0)
        {
            Auth_ExtractState(payload, returnedState, returnedStateSize);
            return 0;
        }

        consoleClear();
        printf("Spotify Setup\n\n");
        printf("Invalid first callback QR.\n");
        return -20;
    }

    for(int expected = 1; expected <= totalParts; expected++)
    {
        int partIndex = 0;

        consoleClear();
        printf("Spotify Setup\n\n");
        printf("Callback QR part %d/%d\n\n", expected, totalParts);
        printf("On the phone, tap Next until it shows\n");
        printf("part %d/%d, then scan it.\n\n", expected, totalParts);
        printf("Press A when ready.\n");

        if(WaitForA() == APP_EXIT_REQUESTED)
            return APP_EXIT_REQUESTED;

        ret = ScanSingleCallbackQr(payload, sizeof(payload));

        if(ret != 0)
            return ret;

        if(CompactExtractIntParam(payload, "i", &partIndex) != 0 || partIndex != expected)
        {
            consoleClear();
            printf("Spotify Setup\n\n");
            printf("Wrong QR part.\n");
            printf("Expected: %d/%d\n", expected, totalParts);
            return -21;
        }

        if(CompactExtractParam(payload, "c", chunk, sizeof(chunk)) != 0)
        {
            consoleClear();
            printf("Spotify Setup\n\n");
            printf("Could not read QR part %d.\n", expected);
            return -22;
        }

        size_t chunkLen = strlen(chunk);

        if(codeLen + chunkLen + 1 > authCodeSize)
            return -23;

        memcpy(authCode + codeLen, chunk, chunkLen);
        codeLen += chunkLen;
        authCode[codeLen] = '\0';
    }

    return codeLen > 0 ? 0 : -24;
}

static int WriteTokenFile(const char *clientId, const char *accessToken, const char *refreshToken, const char *scope)
{
    FILE *f;

    mkdir(CONFIG_ROOT, 0777);
    mkdir(CONFIG_DIR, 0777);

    f = fopen(TOKEN_PATH, "wb");

    if(!f)
        return -1;

    fprintf(
        f,
        "{\n"
        "  \"client_id\": \"%s\",\n"
        "  \"access_token\": \"%s\",\n"
        "  \"refresh_token\": \"%s\",\n"
        "  \"expires_at\": 0,\n"
        "  \"scope\": \"%s\"\n"
        "}\n",
        clientId,
        accessToken,
        refreshToken,
        scope
    );

    fclose(f);
    return 0;
}

static int BuildSpotifyAuthUrl(
    const char *clientId,
    const char *codeChallenge,
    const char *state,
    char *out,
    size_t outSize
)
{
    int written = snprintf(
        out,
        outSize,
        "https://accounts.spotify.com/authorize"
        "?response_type=code"
        "&client_id=%s"
        "&scope=%s"
        "&redirect_uri=%s"
        "&code_challenge_method=S256"
        "&code_challenge=%s"
        "&state=%s",
        clientId,
        SPOTIFY_SCOPE_ENCODED,
        SPOTIFY_REDIRECT_URI_ENCODED,
        codeChallenge,
        state
    );

    if(written < 0 || (size_t)written >= outSize)
        return -1;

    return 0;
}

static int InitNetwork(void)
{
    Result res;

    socBuffer = (u32 *)memalign(SOC_ALIGN, SOC_BUFFER_SIZE);

    if(!socBuffer)
        return -1;

    res = socInit(socBuffer, SOC_BUFFER_SIZE);

    if(R_FAILED(res))
    {
        free(socBuffer);
        socBuffer = NULL;
        return (int)res;
    }

    if(curl_global_init(CURL_GLOBAL_DEFAULT) != 0)
    {
        socExit();

        free(socBuffer);
        socBuffer = NULL;

        return -2;
    }

    return 0;
}

static void ExitNetwork(void)
{
    curl_global_cleanup();

    socExit();

    if(socBuffer)
    {
        free(socBuffer);
        socBuffer = NULL;
    }
}

int main(int argc, char **argv)
{
    char clientId[128];
    char codeVerifier[128];
    char codeChallenge[128];
    char state[64];
    char authUrl[2048];

    char authCode[2048];
    char tokenResponse[8192];

    char accessToken[2048];
    char refreshToken[2048];
    char grantedScope[512];

    long httpCode = 0;

    int networkRet;
    int authRet;

    int inputRet;
    int pkceRet;
    int urlRet;
    int qrRet;
    int writeRet;

    (void)argc;
    (void)argv;

    gfxInitDefault();
    osSetSpeedupEnable(true);
    consoleInit(GFX_TOP, NULL);

    ClearBottomScreen(0, 0, 0);

    printf("Spotify Setup\n\n");
    printf("This app will create:\n");
    printf("%s\n\n", TOKEN_PATH);

    printf("Before continuing, make sure your Spotify app has this Redirect URI:\n\n");
    printf("%s\n\n", SPOTIFY_REDIRECT_URI);

    printf("Next step: enter your Spotify Client ID.\n\n");
    printf("Press A to continue.\n");
    printf("Press START to exit.\n");

    if(WaitForA() == APP_EXIT_REQUESTED)
    {
        gfxExit();
        return 0;
    }

    inputRet = InputText("Spotify Client ID", clientId, sizeof(clientId));

    consoleClear();

    printf("Spotify Setup\n\n");

    if(inputRet != 0)
    {
        printf("Input cancelled.\n");
        WaitForExit();
        gfxExit();
        return 0;
    }

    printf("Client ID entered:\n%s\n\n", clientId);

    pkceRet = Pkce_GenerateVerifier(codeVerifier, sizeof(codeVerifier));

    if(pkceRet == 0)
        pkceRet = Pkce_GenerateChallengeS256(codeVerifier, codeChallenge, sizeof(codeChallenge));

    if(pkceRet == 0)
        pkceRet = Pkce_GenerateState(state, sizeof(state));

    if(pkceRet != 0)
    {
        printf("Failed to generate PKCE data.\n");
        printf("Error: %d\n", pkceRet);
        WaitForExit();
        gfxExit();
        return 0;
    }

    urlRet = BuildSpotifyAuthUrl(
        clientId,
        codeChallenge,
        state,
        authUrl,
        sizeof(authUrl)
    );

    if(urlRet != 0)
    {
        printf("Failed to build authorization URL.\n");
        printf("Error: %d\n", urlRet);
        WaitForExit();
        gfxExit();
        return 0;
    }

    qrRet = ShowAuthQrCode(authUrl);

    if(qrRet == APP_EXIT_REQUESTED)
    {
        gfxExit();
        return 0;
    }

    if(qrRet != 0)
    {
        consoleClear();

        printf("Spotify Setup\n\n");
        printf("Failed to generate QR code.\n");
        printf("Error: %d\n\n", qrRet);
        printf("The URL may be too long for the QR buffer.\n");

        WaitForExit();

        gfxExit();
        return 0;
    }

    consoleClear();

    printf("Spotify Setup\n\n");
    printf("Now scan the callback QR from the web page.\n");
    printf("If the page shows multiple QR codes, scan\n");
    printf("them in the order requested here.\n\n");

    char returnedState[128];
    inputRet = ScanCallbackPayload(authCode, sizeof(authCode), returnedState, sizeof(returnedState));

    consoleClear();

    printf("Spotify Setup\n\n");

    if(inputRet == APP_EXIT_REQUESTED || inputRet == QRSCAN_EXIT)
    {
        gfxExit();
        return 0;
    }

    if(inputRet != 0)
    {
        if(inputRet == QRSCAN_CANCELLED)
            printf("QR scan cancelled.\n");
        else
        {
            printf("QR scan failed.\n");
            printf("Error: %d\n", inputRet);
        }

        WaitForExit();
        gfxExit();
        return 0;
    }

    if(returnedState[0] != '\0')
    {
        if(strcmp(returnedState, state) != 0)
        {
            printf("State mismatch. Authorization cancelled.\n");
            WaitForExit();
            gfxExit();
            return 0;
        }
    }

    printf("Authorization code received.\n");
    printf("Connecting to Spotify...\n\n");

    networkRet = InitNetwork();

    if(networkRet != 0)
    {
        printf("Network init failed: %d\n", networkRet);
        WaitForExit();
        gfxExit();
        return 0;
    }

    authRet = Auth_ExchangeCode(
        clientId,
        authCode,
        codeVerifier,
        tokenResponse,
        sizeof(tokenResponse),
        &httpCode
    );

    ExitNetwork();

    if(authRet != 0)
    {
        printf("Token exchange failed.\n");
        printf("Error: %d\n", authRet);
        printf("HTTP: %ld\n\n", httpCode);

        WaitForExit();
        gfxExit();
        return 0;
    }

    if(Json_GetString(tokenResponse, "\"access_token\"", accessToken, sizeof(accessToken)) != 0)
    {
        printf("Could not read access_token.\n");
        WaitForExit();
        gfxExit();
        return 0;
    }

    if(Json_GetString(tokenResponse, "\"refresh_token\"", refreshToken, sizeof(refreshToken)) != 0)
    {
        printf("Could not read refresh_token.\n");
        WaitForExit();
        gfxExit();
        return 0;
    }

    if(Json_GetString(tokenResponse, "\"scope\"", grantedScope, sizeof(grantedScope)) != 0)
    {
        strcpy(
            grantedScope,
            "user-modify-playback-state user-read-playback-state user-read-currently-playing"
        );
    }

    printf("Token exchange successful.\n");
    printf("Writing token file...\n\n");

    writeRet = WriteTokenFile(
        clientId,
        accessToken,
        refreshToken,
        grantedScope
    );

    if(writeRet == 0)
    {
        printf("Token file created:\n");
        printf("%s\n", TOKEN_PATH);
    }
    else
    {
        printf("Failed to create token file.\n");
        printf("Error: %d\n", writeRet);
    }

    WaitForExit();

    gfxExit();

    return 0;
}