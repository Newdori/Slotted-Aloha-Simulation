/* Dumps round results of the original 1.1.0.0 simulation core for regression comparison. */
#include "Simulation.h"
#include "VerifyCases.h"

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    for (size_t uCase = 0; uCase < sizeof(gaVerifyCases) / sizeof(gaVerifyCases[0]); uCase++) {
        const VerifyCase_t *pCase = &gaVerifyCases[uCase];
        SimulationResult_t Result = { 0 };
        TerminalSuccess_t Success = { 0 };
        uint8_t *apucStates[AreaCount] = { 0 };
        SlotCounts_t aCounts[AreaCount];

        Result.Config = (SimulationConfig_t){ pCase->uiSymbolRate, pCase->uiWaveId, { pCase->uiAlertSlots, pCase->uiLbSlots },
            pCase->uiTerminals, 1000, (uint64_t)pCase->uiRounds * 1000 };
        Result.ullRoundCount = pCase->uiRounds;
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            apucStates[uiArea] = malloc(Result.Config.auiSlots[uiArea] + 1);
            Success.paucSeen[uiArea] = (0 == Result.Config.auiSlots[uiArea]) ? NULL : calloc(pCase->uiTerminals / 8 + 1, 1);
        }
        printf("case %zu\n", uCase);
        for (uint64_t ullRound = 1; ullRound <= Result.ullRoundCount; ullRound++) {
            SimulateRound(&Result.Config, VERIFY_SEED, ullRound, apucStates, aCounts, &Success, NULL, NULL);
            for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
                printf(" r%llu a%u %llu %llu %llu %llu\n", ullRound, uiArea, aCounts[uiArea].ullSuccess, aCounts[uiArea].ullCollision,
                    aCounts[uiArea].ullCollidedAttempts, aCounts[uiArea].ullIdle);
            }
        }
        SummarizeTerminalSuccess(&Result, &Success);
        printf(" fail %u %u\n", Result.auiUnsuccessful[AreaAlert], Result.auiUnsuccessful[AreaLb]);
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            free(apucStates[uiArea]);
            free(Success.paucSeen[uiArea]);
        }
    }
    return 0;
}
