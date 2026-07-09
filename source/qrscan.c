#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "qrscan.h"
#include "quirc.h"

/*
 * QR scanner: stable no-preview loop.
 *
 * This version intentionally keeps the scanner simple:
 *   - no worker threads
 *   - one complete 400x240 RGB565 frame per loop
 *   - quirc decode structs allocated on heap to avoid 3DS stack crashes
 */

#define QRSCAN_WIDTH              400
#define QRSCAN_HEIGHT             240
#define QRSCAN_FRAME_SIZE         (QRSCAN_WIDTH * QRSCAN_HEIGHT * 2)
#define QRSCAN_CAPTURE_TIMEOUT_NS 300000000LL
#define QRSCAN_MAX_FRAME_ERRORS   30
#define QRSCAN_MAX_FRAMES         600

#ifndef QRSCAN_CANCELLED
#define QRSCAN_CANCELLED          (-1001)
#endif

#ifndef QRSCAN_TIMEOUT
#define QRSCAN_TIMEOUT            (-1002)
#endif

#ifndef QRSCAN_EXIT
#define QRSCAN_EXIT               (-1003)
#endif

static int DecodeQrPayload(struct quirc *qr, char *out, size_t outSize)
{
    if(!qr || !out || outSize == 0)
        return -1;

    out[0] = '\0';

    int count = quirc_count(qr);

    for(int i = 0; i < count; i++)
    {
        quirc_decode_error_t err;

        /*
         * IMPORTANT FOR 3DS:
         * struct quirc_code and struct quirc_data are large. Keeping them on
         * the stack caused crashes when a QR was detected. Allocate them on
         * the heap instead.
         */
        struct quirc_code *code = (struct quirc_code *)calloc(1, sizeof(struct quirc_code));
        struct quirc_data *data = (struct quirc_data *)calloc(1, sizeof(struct quirc_data));

        if(!code || !data)
        {
            free(code);
            free(data);
            return -2;
        }

        quirc_extract(qr, i, code);
        err = quirc_decode(code, data);

        if(err == QUIRC_ERROR_DATA_ECC)
        {
            quirc_flip(code);
            memset(data, 0, sizeof(struct quirc_data));
            err = quirc_decode(code, data);
        }

        if(err == QUIRC_SUCCESS && data->payload_len > 0)
        {
            size_t copyLen = (size_t)data->payload_len;

            if(copyLen >= outSize)
                copyLen = outSize - 1;

            memcpy(out, data->payload, copyLen);
            out[copyLen] = '\0';

            free(code);
            free(data);
            return 0;
        }

        free(code);
        free(data);
    }

    return -1;
}

static void Rgb565ToQuircGray(const u16 *src, uint8_t *gray)
{
    for(int i = 0; i < QRSCAN_WIDTH * QRSCAN_HEIGHT; i++)
    {
        u16 p = src[i];

        uint8_t b = (uint8_t)(((p >> 11) & 0x1F) << 3);
        uint8_t g = (uint8_t)(((p >> 5) & 0x3F) << 2);
        uint8_t r = (uint8_t)((p & 0x1F) << 3);

        gray[i] = (uint8_t)((r * 30 + g * 59 + b * 11) / 100);
    }
}

static Result ConfigureCamera(u32 *transferBytes)
{
    Result res;

    if(!transferBytes)
        return -1;

    res = CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A);
    if(R_FAILED(res)) return res;

    res = CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A);
    if(R_FAILED(res)) return res;

    /* Optional tuning. These return Results on some libctru versions, but do not
       make them fatal: older hardware or camera state can reject them. */
    CAMU_SetNoiseFilter(SELECT_OUT1, true);
    CAMU_SetAutoExposure(SELECT_OUT1, true);
    CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);
    CAMU_SetTrimming(PORT_CAM1, false);

    res = CAMU_GetMaxBytes(transferBytes, QRSCAN_WIDTH, QRSCAN_HEIGHT);
    if(R_FAILED(res)) return res;

    res = CAMU_SetTransferBytes(PORT_CAM1, *transferBytes, QRSCAN_WIDTH, QRSCAN_HEIGHT);
    if(R_FAILED(res)) return res;

    res = CAMU_Activate(SELECT_OUT1);
    if(R_FAILED(res)) return res;

    return 0;
}

static void StopCameraSafely(bool cameraConfigured)
{
    if(!cameraConfigured)
        return;

    CAMU_StopCapture(PORT_CAM1);
    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_Activate(SELECT_NONE);
    svcSleepThread(50000000LL);
}

static Result CaptureOneFrame(u16 *frame, u32 transferBytes)
{
    Handle receiveEvent = 0;
    Result res;

    if(!frame)
        return -1;

    res = CAMU_ClearBuffer(PORT_CAM1);
    if(R_FAILED(res))
        return res;

    res = CAMU_StartCapture(PORT_CAM1);
    if(R_FAILED(res))
        return res;

    res = CAMU_SetReceiving(
        &receiveEvent,
        frame,
        PORT_CAM1,
        QRSCAN_FRAME_SIZE,
        (s16)transferBytes
    );

    if(R_SUCCEEDED(res))
        res = svcWaitSynchronization(receiveEvent, QRSCAN_CAPTURE_TIMEOUT_NS);

    if(receiveEvent)
        svcCloseHandle(receiveEvent);

    CAMU_StopCapture(PORT_CAM1);

    return res;
}

int QrScan_ReadPayload(char *out, size_t outSize)
{
    Result res;
    u16 *frame = NULL;
    struct quirc *qr = NULL;
    int qrW = 0;
    int qrH = 0;
    int ret = -1;
    u32 transferBytes = 0;
    u32 frameErrors = 0;
    u32 frames = 0;
    bool camInited = false;
    bool cameraConfigured = false;
    char foundPayload[2048];

    if(!out || outSize == 0)
        return -1;

    out[0] = '\0';
    foundPayload[0] = '\0';

    consoleClear();
    printf("Spotify Setup - QR scanner\n\n");
    printf("Point the OUTER camera at the phone QR.\n\n");
    printf("B: cancel scan\n\n");

    frame = (u16 *)linearAlloc(QRSCAN_FRAME_SIZE);

    qr = quirc_new();

    if(!frame || !qr)
    {
        ret = -2;
        goto cleanup;
    }

    memset(frame, 0, QRSCAN_FRAME_SIZE);

    if(quirc_resize(qr, QRSCAN_WIDTH, QRSCAN_HEIGHT) < 0)
    {
        ret = -3;
        goto cleanup;
    }

    res = camInit();
    if(R_FAILED(res))
    {
        printf("camInit failed: 0x%08lX\n", (unsigned long)res);
        ret = (int)res;
        goto cleanup;
    }
    camInited = true;

    res = ConfigureCamera(&transferBytes);
    if(R_FAILED(res))
    {
        printf("Camera setup failed: 0x%08lX\n", (unsigned long)res);
        ret = (int)res;
        goto cleanup;
    }
    cameraConfigured = true;

    printf("Camera opened. Scanning...\n");

    while(aptMainLoop())
    {
        hidScanInput();
        u32 keys = hidKeysDown();

        if(keys & KEY_START)
        {
            ret = QRSCAN_EXIT;
            goto cleanup;
        }

        if(keys & KEY_B)
        {
            ret = QRSCAN_CANCELLED;
            goto cleanup;
        }

        res = CaptureOneFrame(frame, transferBytes);
        if(R_FAILED(res))
        {
            frameErrors++;
            if(frameErrors >= QRSCAN_MAX_FRAME_ERRORS)
            {
                ret = (int)res;
                goto cleanup;
            }

            svcSleepThread(50000000LL);
            continue;
        }

        frameErrors = 0;
        frames++;

        if((frames % 30) == 0)
            printf("\x1b[7;0HScanning... keep the QR centered.   ");

        uint8_t *gray = quirc_begin(qr, &qrW, &qrH);
        if(gray && qrW == QRSCAN_WIDTH && qrH == QRSCAN_HEIGHT)
        {
            Rgb565ToQuircGray(frame, gray);
            quirc_end(qr);

            if(DecodeQrPayload(qr, foundPayload, sizeof(foundPayload)) == 0)
            {
                size_t len = strlen(foundPayload);

                if(len + 1 > outSize)
                {
                    ret = -7;
                    goto cleanup;
                }

                memcpy(out, foundPayload, len + 1);
                ret = 0;
                goto cleanup;
            }
        }
        else
        {
            quirc_end(qr);
        }

        if(frames >= QRSCAN_MAX_FRAMES)
        {
            ret = QRSCAN_TIMEOUT;
            goto cleanup;
        }

        svcSleepThread(10000000LL);
    }

    ret = QRSCAN_CANCELLED;

cleanup:

    StopCameraSafely(cameraConfigured);

    if(camInited)
    {
        camExit();
    }

    if(qr)
    {
        quirc_destroy(qr);
        qr = NULL;
    }

    if(frame)
    {
        linearFree(frame);
        frame = NULL;
    }

    consoleClear();
    return ret;
}
