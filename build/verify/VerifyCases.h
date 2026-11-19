#ifndef VERIFY_CASES_H
#define VERIFY_CASES_H

#include <stdint.h>

typedef struct VerifyCase {
    uint32_t uiSymbolRate;
    uint32_t uiWaveId;
    uint32_t uiAlertSlots;
    uint32_t uiLbSlots;
    uint32_t uiTerminals;
    uint32_t uiRounds;
} VerifyCase_t;

static const VerifyCase_t gaVerifyCases[] = {
    { 4000, 3, 144, 288, 3, 5 },
    { 4000, 3, 361, 72, 500, 20 },
    { 500, 13, 7, 0, 10, 10 },
    { 2000, 3, 0, 50, 40, 8 },
    { 4000, 13, 100, 100, 0, 3 },
    { 4000, 3, 1000, 2000, 5000, 4 },
    { 2000, 13, 25, 37, 60, 12 },
    { 500, 3, 1, 1, 7, 6 },
    { 4000, 3, 144, 288, 3, 0 }
};

#define VERIFY_SEED UINT64_C(0x0123456789abcdef)

#endif
