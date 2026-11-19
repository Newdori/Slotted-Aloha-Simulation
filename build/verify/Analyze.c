/* Monte Carlo check of final failure counts against the closed form. Not part of the distributed sources. */
#include "Group.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Run {
    SimulationResult_t Result;
    TerminalSuccess_t Success;
    TerminalSuccess_t Baseline;
    uint8_t *apucStates[AreaCount];
    SlotCounts_t aRoundCounts[AreaCount];
} Run_t;

static void FreeAnalysisRun(Run_t *pRun)
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

static bool ExecuteAnalysisRun(Run_t *pRun, const SimulationConfig_t *pConfig, const GroupConfig_t *pGroups, uint64_t ullSeed)
{
    SimulationResult_t *pResult = &pRun->Result;
    uint64_t ullRounds = GetRoundCount(pConfig);

    memset(pRun, 0, sizeof(*pRun));
    pResult->Config = *pConfig;
    pResult->ullRoundCount = ullRounds;
    if (!BuildGroupPlans(pConfig, pGroups, pResult->aPlans)) {
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

/* Expected number of terminals with no success in R rounds: n * (1 - (1 - 1/s)^(n - 1))^R per pool. */
static double CalculatePoolFailures(double dTerminals, double dSlots, double dRounds)
{
    double dSuccess;

    if (dTerminals <= 0.0) {
        return 0.0;
    }
    if (dSlots <= 0.0) {
        return dTerminals;
    }
    dSuccess = pow(1.0 - 1.0 / dSlots, dTerminals - 1.0);
    return dTerminals * pow(1.0 - dSuccess, dRounds);
}

static double CalculatePlanFailures(const AreaGroupPlan_t *pPlan, double dRounds)
{
    double dTotal = 0.0;

    for (uint32_t uiGroup = 0; uiGroup < pPlan->uiGroupCount; uiGroup++) {
        dTotal += CalculatePoolFailures(GetGroupTerminalCount(pPlan, uiGroup), pPlan->auiSlotCount[uiGroup], dRounds);
    }
    return dTotal;
}

static void AnalyzeCase(const char *pcName, const SimulationConfig_t *pConfig, const GroupConfig_t *pGroups, uint32_t uiSeeds)
{
    double adGrouped[AreaCount] = { 0 };
    double adGroupedSquares[AreaCount] = { 0 };
    double adBaseline[AreaCount] = { 0 };
    double adBaselineSquares[AreaCount] = { 0 };
    double adTheory[AreaCount] = { 0 };
    double dRounds = (double)GetRoundCount(pConfig);
    uint32_t uiWorse[AreaCount] = { 0 };

    for (uint32_t uiSeed = 0; uiSeed < uiSeeds; uiSeed++) {
        Run_t Run;
        if (!ExecuteAnalysisRun(&Run, pConfig, pGroups, UINT64_C(0x9E3779B97F4A7C15) * (uiSeed + 1))) {
            printf("%s: run failed\n", pcName);
            return;
        }
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            double dGrouped = Run.Result.auiUnsuccessful[uiArea];
            double dBaseline = Run.Result.auiBaselineUnsuccessful[uiArea];
            adGrouped[uiArea] += dGrouped;
            adGroupedSquares[uiArea] += dGrouped * dGrouped;
            adBaseline[uiArea] += dBaseline;
            adBaselineSquares[uiArea] += dBaseline * dBaseline;
            uiWorse[uiArea] += (uint32_t)(dGrouped > dBaseline);
            if (0 == uiSeed) {
                adTheory[uiArea] = CalculatePlanFailures(&Run.Result.aPlans[uiArea], dRounds);
            }
        }
        FreeAnalysisRun(&Run);
    }
    printf("== %s (%u seeds, %.0f rounds)\n", pcName, uiSeeds, dRounds);
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        double dMeanGrouped = adGrouped[uiArea] / uiSeeds;
        double dMeanBaseline = adBaseline[uiArea] / uiSeeds;
        double dStdGrouped = sqrt(adGroupedSquares[uiArea] / uiSeeds - dMeanGrouped * dMeanGrouped);
        double dStdBaseline = sqrt(adBaselineSquares[uiArea] / uiSeeds - dMeanBaseline * dMeanBaseline);
        double dBaselineTheory = CalculatePoolFailures(pConfig->uiTerminalCount, pConfig->auiSlots[uiArea], dRounds);
        printf("  %-5s grouped %7.2f (sd %5.2f, theory %7.2f) | ungrouped %7.2f (sd %5.2f, theory %7.2f) | grouped worse in %3u%% of runs\n",
            (AreaAlert == uiArea) ? "Alert" : "LB", dMeanGrouped, dStdGrouped, adTheory[uiArea], dMeanBaseline, dStdBaseline, dBaselineTheory,
            100 * uiWorse[uiArea] / uiSeeds);
    }
}

/* Gives each group a contiguous block whose size follows its terminal count (largest remainder). */
static bool SetProportionalSlots(GroupConfig_t *pGroups, const SimulationConfig_t *pConfig, AreaType Area)
{
    AreaGroupLayout_t Layout;
    AreaGroupConfig_t *pArea = &pGroups->aAreas[Area];
    uint32_t auiSizes[GROUP_MAX_COUNT] = { 0 };
    double adRemainders[GROUP_MAX_COUNT] = { 0 };
    uint32_t uiUsed = 0;
    uint32_t uiSlot = 0;

    if (!ResolveAreaGroups(pConfig, pGroups, Area, &Layout)) {
        return false;
    }
    for (uint32_t uiGroup = 0; uiGroup < Layout.uiGroupCount; uiGroup++) {
        double dShare = (double)Layout.uiSlots * Layout.auiTerminals[uiGroup] / (double)Layout.ullTerminalSum;
        auiSizes[uiGroup] = (uint32_t)dShare;
        adRemainders[uiGroup] = dShare - auiSizes[uiGroup];
        uiUsed += auiSizes[uiGroup];
    }
    while (uiUsed < Layout.uiSlots) {
        uint32_t uiBest = 0;
        for (uint32_t uiGroup = 1; uiGroup < Layout.uiGroupCount; uiGroup++) {
            uiBest = (adRemainders[uiGroup] > adRemainders[uiBest]) ? uiGroup : uiBest;
        }
        auiSizes[uiBest]++;
        adRemainders[uiBest] = -1.0;
        uiUsed++;
    }
    if (!ClearGroupSlots(pArea, Layout.uiTimeSlots, Layout.uiSlots)) {
        FreeAreaGroupLayout(&Layout);
        return false;
    }
    for (uint32_t uiGroup = 0; uiGroup < Layout.uiGroupCount; uiGroup++) {
        memset(&pArea->paucOwner[uiSlot], (int32_t)uiGroup, auiSizes[uiGroup]);
        uiSlot += auiSizes[uiGroup];
    }
    FreeAreaGroupLayout(&Layout);
    return true;
}

int main(void)
{
    const uint32_t uiSeeds = 2000;
    SimulationConfig_t Config = { 4000, 3, { 144, 288 }, 300, 10000, 120000, 10, 1 };
    GroupConfig_t Groups = { 0 };
    char acName[96];

    AnalyzeCase("300 terminals, 10 groups, equal terminals and slots", &Config, &Groups, uiSeeds);
    FixGroupTerminalCount(&Groups.aAreas[AreaAlert], 0, 50);
    AnalyzeCase("Alert group 1 = 50 terminals, slots equal (15)", &Config, &Groups, uiSeeds);
    if (SetProportionalSlots(&Groups, &Config, AreaAlert)) {
        AnalyzeCase("Alert group 1 = 50 terminals, slots proportional to terminals", &Config, &Groups, uiSeeds);
    }
    FreeGroupConfig(&Groups);
    {
        const uint32_t auiGroupCounts[] = { 1, 2, 5, 10, 20, 50 };
        for (uint32_t uiIndex = 0; uiIndex < sizeof(auiGroupCounts) / sizeof(auiGroupCounts[0]); uiIndex++) {
            Config.uiGroupCount = auiGroupCounts[uiIndex];
            if (snprintf(acName, sizeof(acName), "300 terminals, %u equal groups", auiGroupCounts[uiIndex]) > 0) {
                AnalyzeCase(acName, &Config, &Groups, 500);
            }
        }
    }
    Config = (SimulationConfig_t){ 4000, 3, { 144, 288 }, 3000, 10000, 120000, 10, 1 };
    AnalyzeCase("3000 terminals, 10 equal groups", &Config, &Groups, 300);
    return 0;
}
