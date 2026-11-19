/* Grouped vs ungrouped averages when there are fewer terminals than slots. Not part of the distributed sources. */
#include "Group.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct SweepRun {
    SimulationResult_t Result;
    TerminalSuccess_t Success;
    TerminalSuccess_t Baseline;
    uint8_t *apucStates[AreaCount];
    SlotCounts_t aRoundCounts[AreaCount];
} SweepRun_t;

static void FreeSweepRun(SweepRun_t *pRun)
{
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        free(pRun->apucStates[uiArea]);
        free(pRun->Success.paucSeen[uiArea]);
        free(pRun->Baseline.paucSeen[uiArea]);
    }
    FreeGroupPlans(pRun->Result.aPlans);
    free(pRun->Result.paRounds);
    free(pRun->Result.paGroupRounds);
    free(pRun->Result.paBaselineRounds);
    memset(pRun, 0, sizeof(*pRun));
}

static bool ExecuteSweepRun(SweepRun_t *pRun, const SimulationConfig_t *pConfig, uint64_t ullSeed)
{
    SimulationResult_t *pResult = &pRun->Result;
    GroupConfig_t Groups = { 0 };
    uint64_t ullRounds = GetRoundCount(pConfig);

    memset(pRun, 0, sizeof(*pRun));
    pResult->Config = *pConfig;
    pResult->ullRoundCount = ullRounds;
    if (!BuildGroupPlans(pConfig, &Groups, pResult->aPlans)) {
        return false;
    }
    pResult->uiGroupCount = pConfig->uiGroupCount;
    pResult->paRounds = calloc(ullRounds, sizeof(*pResult->paRounds));
    pResult->paBaselineRounds = calloc(ullRounds, sizeof(*pResult->paBaselineRounds));
    pResult->paGroupRounds = calloc(ullRounds * AreaCount * pResult->uiGroupCount, sizeof(*pResult->paGroupRounds));
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        pRun->apucStates[uiArea] = malloc(pConfig->auiSlots[uiArea] + 1);
        pRun->Success.paucSeen[uiArea] = calloc(pConfig->uiTerminalCount / 8 + 1, 1);
        pRun->Baseline.paucSeen[uiArea] = calloc(pConfig->uiTerminalCount / 8 + 1, 1);
    }
    return SimulateAllRounds(pResult, ullSeed, pRun->apucStates, pRun->aRoundCounts, &pRun->Success, &pRun->Baseline, NULL, NULL, NULL);
}

int main(void)
{
    const uint32_t auiTerminals[] = { 50, 100, 144, 200, 288, 300 };
    const uint32_t auiGroups[] = { 2, 10, 50, 100 };
    const uint32_t uiSeeds = 1000;

    printf("Alert 144 slots, 12 rounds, equal terminals and equal slots, %u seeds\n", uiSeeds);
    printf("terminals groups | success per round: grouped ungrouped (%% of terminals) | final failures: grouped ungrouped\n");
    for (uint32_t uiTerminal = 0; uiTerminal < sizeof(auiTerminals) / sizeof(auiTerminals[0]); uiTerminal++) {
        for (uint32_t uiGroup = 0; uiGroup < sizeof(auiGroups) / sizeof(auiGroups[0]); uiGroup++) {
            SimulationConfig_t Config = { 4000, 3, { 144, 0 }, auiTerminals[uiTerminal], 10000, 120000, auiGroups[uiGroup], 1 };
            double dGroupedSuccess = 0.0;
            double dBaselineSuccess = 0.0;
            double dGroupedFailures = 0.0;
            double dBaselineFailures = 0.0;

            for (uint32_t uiSeed = 0; uiSeed < uiSeeds; uiSeed++) {
                SweepRun_t Run;
                if (!ExecuteSweepRun(&Run, &Config, UINT64_C(0x9E3779B97F4A7C15) * (uiSeed + 1))) {
                    printf("run failed\n");
                    return 1;
                }
                dGroupedSuccess += (double)Run.Result.aTotals[AreaAlert].ullSuccess / Run.Result.ullRoundCount;
                dBaselineSuccess += (double)Run.Result.aBaselineTotals[AreaAlert].ullSuccess / Run.Result.ullRoundCount;
                dGroupedFailures += Run.Result.auiUnsuccessful[AreaAlert];
                dBaselineFailures += Run.Result.auiBaselineUnsuccessful[AreaAlert];
                FreeSweepRun(&Run);
            }
            dGroupedSuccess /= uiSeeds;
            dBaselineSuccess /= uiSeeds;
            printf("%5u %5u | %7.2f (%5.1f%%) %7.2f (%5.1f%%) | %8.3f %8.3f\n", auiTerminals[uiTerminal], auiGroups[uiGroup],
                dGroupedSuccess, 100.0 * dGroupedSuccess / auiTerminals[uiTerminal], dBaselineSuccess, 100.0 * dBaselineSuccess / auiTerminals[uiTerminal],
                dGroupedFailures / uiSeeds, dBaselineFailures / uiSeeds);
        }
    }
    return 0;
}
