#ifndef QRSCAN_H
#define QRSCAN_H

#include <stddef.h>

#define QRSCAN_CANCELLED -1001
#define QRSCAN_TIMEOUT   -1002
#define QRSCAN_EXIT      -1003

int QrScan_ReadPayload(char *out, size_t outSize);

#endif
