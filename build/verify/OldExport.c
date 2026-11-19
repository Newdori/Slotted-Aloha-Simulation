/* Exports a workbook with the original 1.1.0.0 core and report writer for byte comparison. */
#include "Report.h"
#include "VerifyCases.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    SimulationResult_t Result = { 0 };
    TerminalSuccess_t Success = { 0 };
    uint8_t *apucStates[AreaCount];
    SlotCounts_t aCounts[AreaCount];

    Result.Config = (SimulationConfig_t){ 4000, 3, { 144, 288 }, 30, 1000, 12000 };
    Result.ullRoundCount = 12;
    Result.paRounds = calloc(Result.ullRoundCount, sizeof(*Result.paRounds));
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        apucStates[uiArea] = malloc(Result.Config.auiSlots[uiArea]);
        Success.paucSeen[uiArea] = calloc(Result.Config.uiTerminalCount / 8 + 1, 1);
    }
    for (uint64_t ullIndex = 0; ullIndex < Result.ullRoundCount; ullIndex++) {
        SimulateRound(&Result.Config, VERIFY_SEED, ullIndex + 1, apucStates, aCounts, &Success, NULL, NULL);
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            Result.paRounds[ullIndex].aAreas[uiArea] = (RoundAreaCounts_t){ (uint32_t)aCounts[uiArea].ullSuccess,
                (uint32_t)aCounts[uiArea].ullCollision, (uint32_t)aCounts[uiArea].ullCollidedAttempts };
            AddSlotCounts(&Result.aTotals[uiArea], &aCounts[uiArea]);
        }
    }
    SummarizeTerminalSuccess(&Result, &Success);
    return ExportResults(L"out-plain-old.xlsx", &Result, NULL, NULL) ? 0 : 1;
}
