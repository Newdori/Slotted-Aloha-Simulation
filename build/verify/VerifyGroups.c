/* Verification program for the group slot assignment feature. Not part of the distributed sources. */
#include "Group.h"
#include "Report.h"
#include "VerifyCases.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static uint32_t guiFailures;
static uint32_t guiChecks;

#define CHECK(bCondition, ...) do { guiChecks++; if (!(bCondition)) { guiFailures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct VerifyRun {
    SimulationResult_t Result;
    TerminalSuccess_t Success;
    TerminalSuccess_t Baseline;
    uint8_t *apucStates[AreaCount];
    SlotCounts_t aRoundCounts[AreaCount];
} VerifyRun_t;

static SimulationConfig_t MakeConfig(uint32_t uiRate, uint32_t uiWave, uint32_t uiAlert, uint32_t uiLb, uint32_t uiTerminals, uint32_t uiRounds, uint32_t uiGroups)
{
    return (SimulationConfig_t){ uiRate, uiWave, { uiAlert, uiLb }, uiTerminals, 1000, (uint64_t)uiRounds * 1000, uiGroups, 1 };
}

static void FreeRun(VerifyRun_t *pRun)
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
    free(pRun->Result.paRepeats);
    memset(pRun, 0, sizeof(*pRun));
}

/* Mirrors CreateTask in Main.c. */
static bool PrepareRun(VerifyRun_t *pRun, const SimulationConfig_t *pConfig, const GroupConfig_t *pGroups)
{
    SimulationResult_t *pResult = &pRun->Result;
    uint64_t ullRounds = GetRoundCount(pConfig);
    bool bTrack = (0 != pConfig->uiTerminalCount) && ((0 != pConfig->auiSlots[AreaAlert]) || (0 != pConfig->auiSlots[AreaLb]));

    memset(pRun, 0, sizeof(*pRun));
    pResult->Config = *pConfig;
    pResult->ullRoundCount = ullRounds;
    if ((NULL != pGroups) && !BuildGroupPlans(pConfig, pGroups, pResult->aPlans)) {
        return false;
    }
    if ((0 != pResult->aPlans[AreaAlert].uiGroupCount) || (0 != pResult->aPlans[AreaLb].uiGroupCount)) {
        pResult->uiGroupCount = pConfig->uiGroupCount;
    }
    if ((0 != ullRounds) && bTrack) {
        pResult->paRounds = calloc(ullRounds, sizeof(*pResult->paRounds));
        if (IsGroupedResult(pResult)) {
            pResult->paBaselineRounds = calloc(ullRounds, sizeof(*pResult->paBaselineRounds));
            pResult->paGroupRounds = calloc(ullRounds * AreaCount * pResult->uiGroupCount, sizeof(*pResult->paGroupRounds));
        }
    }
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        pRun->apucStates[uiArea] = malloc(pConfig->auiSlots[uiArea] + 1);
        if ((0 != pConfig->auiSlots[uiArea]) && bTrack) {
            pRun->Success.paucSeen[uiArea] = calloc(pConfig->uiTerminalCount / 8 + 1, 1);
            pRun->Baseline.paucSeen[uiArea] = calloc(pConfig->uiTerminalCount / 8 + 1, 1);
        }
    }
    return true;
}

static bool ExecuteRun(VerifyRun_t *pRun, const SimulationConfig_t *pConfig, const GroupConfig_t *pGroups)
{
    return PrepareRun(pRun, pConfig, pGroups) &&
        SimulateAllRounds(&pRun->Result, VERIFY_SEED, pRun->apucStates, pRun->aRoundCounts, &pRun->Success, &pRun->Baseline, NULL, NULL, NULL);
}

/* Mirrors RunRepeats in Main.c: repetition 1 is the detailed run, the others keep their final values. */
static bool ExecuteRepeatedRun(VerifyRun_t *pRun, const SimulationConfig_t *pConfig, const GroupConfig_t *pGroups, uint32_t uiRepeats)
{
    if (!ExecuteRun(pRun, pConfig, pGroups)) {
        return false;
    }
    pRun->Result.paRepeats = calloc(uiRepeats, sizeof(*pRun->Result.paRepeats));
    if (NULL == pRun->Result.paRepeats) {
        return false;
    }
    CaptureRepeatSample(&pRun->Result, VERIFY_SEED, &pRun->Result.paRepeats[0]);
    pRun->Result.uiRepeatCount = 1;
    for (uint32_t uiRepeat = 1; uiRepeat < uiRepeats; uiRepeat++) {
        if (!SimulateRepeat(&pRun->Result, GetRepeatSeed(VERIFY_SEED, uiRepeat), pRun->apucStates, pRun->aRoundCounts, &pRun->Success, &pRun->Baseline,
            &pRun->Result.paRepeats[uiRepeat], NULL, NULL)) {
            return false;
        }
        pRun->Result.uiRepeatCount = uiRepeat + 1;
    }
    return true;
}

static void TestRepeats(void)
{
    SimulationConfig_t Config = MakeConfig(4000, 3, 144, 288, 300, 12, 10);
    GroupConfig_t Groups = { 0 };
    VerifyRun_t Run;
    const uint32_t auiChecked[] = { 1, 17, 49 };

    CHECK(ExecuteRepeatedRun(&Run, &Config, &Groups, 50), "repeated run");
    CHECK((50 == Run.Result.uiRepeatCount) && IsRepeatedResult(&Run.Result), "repeat count");
    CHECK(GetRepeatSeed(VERIFY_SEED, 0) == VERIFY_SEED, "repetition 1 keeps the seed");
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        const RepeatSample_t *pFirst = &Run.Result.paRepeats[0];
        CHECK((pFirst->auiUnsuccessful[uiArea] == Run.Result.auiUnsuccessful[uiArea]) && (pFirst->auiBaselineUnsuccessful[uiArea] == Run.Result.auiBaselineUnsuccessful[uiArea]) &&
            (pFirst->aTotals[uiArea].ullSuccess == Run.Result.aTotals[uiArea].ullSuccess), "repetition 1 is the detailed run");
    }
    /* Every other repetition equals a full run with its own seed. */
    for (uint32_t uiIndex = 0; uiIndex < sizeof(auiChecked) / sizeof(auiChecked[0]); uiIndex++) {
        uint32_t uiRepeat = auiChecked[uiIndex];
        const RepeatSample_t *pSample = &Run.Result.paRepeats[uiRepeat];
        VerifyRun_t Single;

        CHECK(PrepareRun(&Single, &Config, &Groups) && SimulateAllRounds(&Single.Result, GetRepeatSeed(VERIFY_SEED, uiRepeat), Single.apucStates, Single.aRoundCounts,
            &Single.Success, &Single.Baseline, NULL, NULL, NULL), "single run %u", uiRepeat);
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            CHECK((pSample->auiUnsuccessful[uiArea] == Single.Result.auiUnsuccessful[uiArea]) &&
                (pSample->auiBaselineUnsuccessful[uiArea] == Single.Result.auiBaselineUnsuccessful[uiArea]) &&
                (0 == memcmp(&pSample->aTotals[uiArea], &Single.Result.aTotals[uiArea], sizeof(SlotCounts_t))) &&
                (0 == memcmp(&pSample->aBaselineTotals[uiArea], &Single.Result.aBaselineTotals[uiArea], sizeof(SlotCounts_t))),
                "repetition %u equals a full run, area %u", uiRepeat + 1, uiArea);
        }
        FreeRun(&Single);
    }
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        for (uint32_t uiMetric = 0; uiMetric < RepeatMetricCount; uiMetric++) {
            for (uint32_t uiSeries = 0; uiSeries < RepeatSeriesCount; uiSeries++) {
                RepeatStatistic_t Statistic;
                double dSum = 0.0;
                double dSquares = 0.0;
                double dMinimum = 1e300;
                double dMaximum = -1e300;

                for (uint32_t uiRepeat = 0; uiRepeat < Run.Result.uiRepeatCount; uiRepeat++) {
                    double dValue = GetRepeatValue(&Run.Result.paRepeats[uiRepeat], (AreaType)uiArea, (RepeatMetric)uiMetric, (RepeatSeries)uiSeries);
                    dSum += dValue;
                    dMinimum = (dValue < dMinimum) ? dValue : dMinimum;
                    dMaximum = (dValue > dMaximum) ? dValue : dMaximum;
                }
                for (uint32_t uiRepeat = 0; uiRepeat < Run.Result.uiRepeatCount; uiRepeat++) {
                    double dDelta = GetRepeatValue(&Run.Result.paRepeats[uiRepeat], (AreaType)uiArea, (RepeatMetric)uiMetric, (RepeatSeries)uiSeries) - dSum / Run.Result.uiRepeatCount;
                    dSquares += dDelta * dDelta;
                }
                SummarizeRepeats(&Run.Result, (AreaType)uiArea, (RepeatMetric)uiMetric, (RepeatSeries)uiSeries, &Statistic);
                CHECK((fabs(Statistic.dMean - dSum / Run.Result.uiRepeatCount) < 1e-9) && (fabs(Statistic.dDeviation - sqrt(dSquares / (Run.Result.uiRepeatCount - 1))) < 1e-9) &&
                    (Statistic.dMinimum == dMinimum) && (Statistic.dMaximum == dMaximum), "statistics area %u metric %u series %u", uiArea, uiMetric, uiSeries);
            }
        }
    }
    FreeRun(&Run);

    /* Without groups the ungrouped series is the run itself. */
    Config.uiGroupCount = 0;
    CHECK(ExecuteRepeatedRun(&Run, &Config, NULL, 5), "ungrouped repeats");
    for (uint32_t uiRepeat = 0; uiRepeat < 5; uiRepeat++) {
        const RepeatSample_t *pSample = &Run.Result.paRepeats[uiRepeat];
        CHECK((pSample->auiBaselineUnsuccessful[AreaAlert] == pSample->auiUnsuccessful[AreaAlert]) &&
            (pSample->aBaselineTotals[AreaLb].ullSuccess == pSample->aTotals[AreaLb].ullSuccess), "ungrouped baseline equals run %u", uiRepeat + 1);
    }
    FreeRun(&Run);

    /* Many repetitions approach the closed form n * (1 - (1 - 1/s)^(n - 1))^R. */
    Config.uiGroupCount = 10;
    CHECK(ExecuteRepeatedRun(&Run, &Config, &Groups, 2000), "monte carlo repeats");
    {
        RepeatStatistic_t Grouped;
        RepeatStatistic_t Baseline;
        SummarizeRepeats(&Run.Result, AreaAlert, RepeatFailure, RepeatGrouped, &Grouped);
        SummarizeRepeats(&Run.Result, AreaAlert, RepeatFailure, RepeatBaseline, &Baseline);
        printf("monte carlo Alert failures: grouped %.2f (theory 61.66), ungrouped %.2f (theory 60.86)\n", Grouped.dMean, Baseline.dMean);
        CHECK((fabs(Grouped.dMean - 61.66) < 0.6) && (fabs(Baseline.dMean - 60.86) < 0.6), "monte carlo matches theory");
    }
    FreeRun(&Run);
}

static void TestProportional(void)
{
    SimulationConfig_t Config = MakeConfig(4000, 3, 144, 361, 300, 12, 10);
    GroupConfig_t Groups = { 0 };
    AreaGroupLayout_t Equal;
    AreaGroupLayout_t Proportional;
    const uint32_t auiExpected[10] = { 24, 14, 14, 14, 13, 13, 13, 13, 13, 13 };

    /* Equal terminal counts: proportional is the same map as equal. */
    for (uint32_t uiGroups = 1; uiGroups <= 100; uiGroups = uiGroups * 3 + 1) {
        Config.uiGroupCount = uiGroups;
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            ResetGroupSlots(&Groups.aAreas[uiArea]);
        }
        CHECK(ResolveAreaGroups(&Config, &Groups, AreaLb, &Equal), "resolve equal %u", uiGroups);
        SetProportionalSlots(&Groups.aAreas[AreaLb]);
        CHECK(ResolveAreaGroups(&Config, &Groups, AreaLb, &Proportional), "resolve proportional %u", uiGroups);
        if ((0 == (300 % uiGroups)) && (NULL != Equal.paucOwner) && (NULL != Proportional.paucOwner)) {
            CHECK(0 == memcmp(Equal.paucOwner, Proportional.paucOwner, Equal.uiSlots), "proportional equals equal for %u groups", uiGroups);
        }
        FreeAreaGroupLayout(&Equal);
        FreeAreaGroupLayout(&Proportional);
    }

    /* Group 1 with 50 of 300 terminals gets 24 of 144 slots; the others about 13.4. */
    Config = MakeConfig(4000, 3, 144, 288, 300, 12, 10);
    FreeGroupConfig(&Groups);
    FixGroupTerminalCount(&Groups.aAreas[AreaAlert], 0, 50);
    SetProportionalSlots(&Groups.aAreas[AreaAlert]);
    CHECK(0 == ValidateGroupConfig(&Config, &Groups, NULL, 0), "proportional valid");
    CHECK(ResolveAreaGroups(&Config, &Groups, AreaAlert, &Proportional), "resolve 50");
    CHECK(0 == memcmp(Proportional.auiSlots, auiExpected, sizeof(auiExpected)), "proportional sizes %u %u %u %u %u", Proportional.auiSlots[0], Proportional.auiSlots[1],
        Proportional.auiSlots[4], Proportional.auiSlots[8], Proportional.auiSlots[9]);
    CHECK((0 == Proportional.paucOwner[0]) && (0 == Proportional.paucOwner[23]) && (1 == Proportional.paucOwner[24]) && (0 == Proportional.uiUnassigned), "contiguous proportional blocks");
    FreeAreaGroupLayout(&Proportional);

    /* Changing a terminal count re-divides the slots automatically. */
    FixGroupTerminalCount(&Groups.aAreas[AreaAlert], 0, 30);
    CHECK(ResolveAreaGroups(&Config, &Groups, AreaAlert, &Proportional), "resolve after change");
    CHECK((15 == Proportional.auiSlots[0]) && (14 == Proportional.auiSlots[9]), "proportional follows terminal counts");
    FreeAreaGroupLayout(&Proportional);

    /* A tiny group keeps one slot; an empty group gets none. */
    Config = MakeConfig(4000, 13, 10, 0, 1000, 12, 3);
    FreeGroupConfig(&Groups);
    FixGroupTerminalCount(&Groups.aAreas[AreaAlert], 0, 1);
    FixGroupTerminalCount(&Groups.aAreas[AreaAlert], 1, 0);
    SetProportionalSlots(&Groups.aAreas[AreaAlert]);
    CHECK(ResolveAreaGroups(&Config, &Groups, AreaAlert, &Proportional), "resolve tiny");
    CHECK((1 == Proportional.auiSlots[0]) && (0 == Proportional.auiSlots[1]) && (9 == Proportional.auiSlots[2]), "minimum slot %u %u %u",
        Proportional.auiSlots[0], Proportional.auiSlots[1], Proportional.auiSlots[2]);
    CHECK(0 == ValidateGroupConfig(&Config, &Groups, NULL, 0), "tiny group valid");
    FreeAreaGroupLayout(&Proportional);

    /* Averaged over many runs, proportional slots remove the loss caused by the 50-terminal group. */
    Config = MakeConfig(4000, 3, 144, 288, 300, 12, 10);
    FreeGroupConfig(&Groups);
    FixGroupTerminalCount(&Groups.aAreas[AreaAlert], 0, 50);
    {
        VerifyRun_t Run;
        RepeatStatistic_t EqualSlots;
        RepeatStatistic_t ProportionalSlots;

        CHECK(ExecuteRepeatedRun(&Run, &Config, &Groups, 2000), "equal slots repeats");
        SummarizeRepeats(&Run.Result, AreaAlert, RepeatFailure, RepeatGrouped, &EqualSlots);
        FreeRun(&Run);
        SetProportionalSlots(&Groups.aAreas[AreaAlert]);
        CHECK(ExecuteRepeatedRun(&Run, &Config, &Groups, 2000), "proportional repeats");
        CHECK(SlotModeProportional == Run.Result.aPlans[AreaAlert].SlotAssignment, "plan keeps the slot mode");
        SummarizeRepeats(&Run.Result, AreaAlert, RepeatFailure, RepeatGrouped, &ProportionalSlots);
        FreeRun(&Run);
        printf("group 1 = 50 terminals, Alert failures: equal slots %.2f, proportional slots %.2f (ungrouped theory 60.86)\n", EqualSlots.dMean, ProportionalSlots.dMean);
        CHECK((EqualSlots.dMean > 70.0) && (fabs(ProportionalSlots.dMean - 61.6) < 0.7), "proportional slots fix the uneven group");
    }
    FreeGroupConfig(&Groups);
}

static void DumpCases(const char *pcPath, uint32_t uiGroups){
    FILE *pFile = fopen(pcPath, "w");
    GroupConfig_t Groups = { 0 };

    for (size_t uCase = 0; uCase < sizeof(gaVerifyCases) / sizeof(gaVerifyCases[0]); uCase++) {
        const VerifyCase_t *pCase = &gaVerifyCases[uCase];
        SimulationConfig_t Config = MakeConfig(pCase->uiSymbolRate, pCase->uiWaveId, pCase->uiAlertSlots, pCase->uiLbSlots, pCase->uiTerminals, pCase->uiRounds, uiGroups);
        AreaGroupPlan_t aPlans[AreaCount];
        TerminalSuccess_t Success = { 0 };
        SimulationResult_t Result = { .Config = Config, .ullRoundCount = pCase->uiRounds };
        uint8_t *apucStates[AreaCount];
        SlotCounts_t aCounts[AreaCount];

        CHECK(BuildGroupPlans(&Config, &Groups, aPlans), "plan build case %zu", uCase);
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            apucStates[uiArea] = malloc(Config.auiSlots[uiArea] + 1);
            Success.paucSeen[uiArea] = (0 == Config.auiSlots[uiArea]) ? NULL : calloc(pCase->uiTerminals / 8 + 1, 1);
        }
        fprintf(pFile, "case %zu\n", uCase);
        for (uint64_t ullRound = 1; ullRound <= Result.ullRoundCount; ullRound++) {
            SimulateRound(&Config, (0 == uiGroups) ? NULL : aPlans, VERIFY_SEED, ullRound, apucStates, aCounts, NULL, &Success, NULL, NULL);
            for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
                fprintf(pFile, " r%llu a%u %llu %llu %llu %llu\n", ullRound, uiArea, aCounts[uiArea].ullSuccess, aCounts[uiArea].ullCollision,
                    aCounts[uiArea].ullCollidedAttempts, aCounts[uiArea].ullIdle);
            }
        }
        SummarizeTerminalSuccess(&Result, &Success, NULL);
        fprintf(pFile, " fail %u %u\n", Result.auiUnsuccessful[AreaAlert], Result.auiUnsuccessful[AreaLb]);
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            free(apucStates[uiArea]);
            free(Success.paucSeen[uiArea]);
        }
        FreeGroupPlans(aPlans);
    }
    fclose(pFile);
}

static void CheckGroupConsistency(const VerifyRun_t *pRun, const char *pcName)
{
    const SimulationResult_t *pResult = &pRun->Result;

    for (uint64_t ullIndex = 0; ullIndex < pResult->ullRoundCount; ullIndex++) {
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            const AreaGroupPlan_t *pPlan = &pResult->aPlans[uiArea];
            RoundAreaCounts_t Area = GetRoundAreaCounts(pResult, ullIndex, (AreaType)uiArea);
            uint64_t ullSuccess = 0;
            uint64_t ullCollision = 0;
            uint64_t ullCollided = 0;
            uint64_t ullIdle = pPlan->uiUnassignedSlots;
            uint64_t ullAreaIdle = pResult->Config.auiSlots[uiArea] - Area.uiSuccess - Area.uiCollision;

            if (0 == pPlan->uiGroupCount) {
                continue;
            }
            for (uint32_t uiGroup = 0; uiGroup < pPlan->uiGroupCount; uiGroup++) {
                RoundAreaCounts_t Group = GetRoundGroupCounts(pResult, ullIndex, (AreaType)uiArea, uiGroup);
                ullSuccess += Group.uiSuccess;
                ullCollision += Group.uiCollision;
                ullCollided += Group.uiCollidedAttempts;
                ullIdle += pPlan->auiSlotCount[uiGroup] - Group.uiSuccess - Group.uiCollision;
                CHECK(Group.uiSuccess + Group.uiCollidedAttempts == ((NULL == pResult->paRounds) ? 0 : GetGroupTerminalCount(pPlan, uiGroup)),
                    "%s round %llu area %u group %u transmissions", pcName, ullIndex + 1, uiArea, uiGroup);
            }
            CHECK(ullSuccess == Area.uiSuccess, "%s round %llu area %u success %llu != %u", pcName, ullIndex + 1, uiArea, ullSuccess, Area.uiSuccess);
            CHECK(ullCollision == Area.uiCollision, "%s round %llu area %u collision", pcName, ullIndex + 1, uiArea);
            CHECK(ullCollided == Area.uiCollidedAttempts, "%s round %llu area %u collided", pcName, ullIndex + 1, uiArea);
            CHECK(ullIdle == ullAreaIdle, "%s round %llu area %u idle %llu != %llu", pcName, ullIndex + 1, uiArea, ullIdle, ullAreaIdle);
        }
    }
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        const AreaGroupPlan_t *pPlan = &pResult->aPlans[uiArea];
        uint64_t ullFailures = 0;
        SlotCounts_t Sum = { .ullIdle = pResult->ullRoundCount * pPlan->uiUnassignedSlots };

        if (0 == pPlan->uiGroupCount) {
            continue;
        }
        for (uint32_t uiGroup = 0; uiGroup < pPlan->uiGroupCount; uiGroup++) {
            ullFailures += pResult->aauiGroupUnsuccessful[uiArea][uiGroup];
            AddSlotCounts(&Sum, &pResult->aaGroupTotals[uiArea][uiGroup]);
        }
        CHECK(ullFailures == pResult->auiUnsuccessful[uiArea], "%s area %u failures %llu != %u", pcName, uiArea, ullFailures, pResult->auiUnsuccessful[uiArea]);
        CHECK(0 == memcmp(&Sum, &pResult->aTotals[uiArea], sizeof(Sum)), "%s area %u totals", pcName, uiArea);
    }
    /* Cumulative values must match direct sums for every selectable round. */
    for (uint64_t ullRound = 0; ullRound <= pResult->ullRoundCount; ullRound++) {
        SlotCounts_t aaGroups[AreaCount][GROUP_MAX_COUNT];
        SlotCounts_t aAreas[AreaCount];
        SlotCounts_t aBaseline[AreaCount];

        CHECK(SumGroupRoundsThrough(pResult, ullRound, aaGroups, NULL, NULL), "%s sum groups", pcName);
        CHECK(SumRoundsThrough(pResult, ullRound, aAreas, NULL, NULL), "%s sum areas", pcName);
        CHECK(SumBaselineRoundsThrough(pResult, ullRound, aBaseline, NULL, NULL), "%s sum baseline", pcName);
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            const AreaGroupPlan_t *pPlan = &pResult->aPlans[uiArea];
            SlotCounts_t Sum = { .ullIdle = ullRound * pPlan->uiUnassignedSlots };

            if (0 == pPlan->uiGroupCount) {
                continue;
            }
            for (uint32_t uiGroup = 0; uiGroup < pPlan->uiGroupCount; uiGroup++) {
                AddSlotCounts(&Sum, &aaGroups[uiArea][uiGroup]);
            }
            CHECK(0 == memcmp(&Sum, &aAreas[uiArea], sizeof(Sum)), "%s cumulative round %llu area %u", pcName, ullRound, uiArea);
        }
    }
}

static void TestEquivalence(void)
{
    for (size_t uCase = 0; uCase < sizeof(gaVerifyCases) / sizeof(gaVerifyCases[0]); uCase++) {
        const VerifyCase_t *pCase = &gaVerifyCases[uCase];
        SimulationConfig_t Plain = MakeConfig(pCase->uiSymbolRate, pCase->uiWaveId, pCase->uiAlertSlots, pCase->uiLbSlots, pCase->uiTerminals, pCase->uiRounds, 0);
        SimulationConfig_t Single = Plain;
        GroupConfig_t Groups = { 0 };
        VerifyRun_t PlainRun;
        VerifyRun_t SingleRun;

        Single.uiGroupCount = 1;
        CHECK(ExecuteRun(&PlainRun, &Plain, NULL), "plain run %zu", uCase);
        CHECK(ExecuteRun(&SingleRun, &Single, &Groups), "single run %zu", uCase);
        CHECK((0 == SingleRun.Result.ullRoundCount) || (NULL == SingleRun.Result.paRounds) || (NULL == PlainRun.Result.paRounds) ||
            (0 == memcmp(PlainRun.Result.paRounds, SingleRun.Result.paRounds, SingleRun.Result.ullRoundCount * sizeof(RoundResult_t))), "single group rounds %zu", uCase);
        CHECK(0 == memcmp(PlainRun.Result.aTotals, SingleRun.Result.aTotals, sizeof(PlainRun.Result.aTotals)), "single group totals %zu", uCase);
        CHECK(0 == memcmp(PlainRun.Result.auiUnsuccessful, SingleRun.Result.auiUnsuccessful, sizeof(PlainRun.Result.auiUnsuccessful)), "single group failures %zu", uCase);
        CHECK(0 == memcmp(PlainRun.Result.aTotals, SingleRun.Result.aBaselineTotals, sizeof(PlainRun.Result.aTotals)) || !IsGroupedResult(&SingleRun.Result),
            "baseline totals %zu", uCase);
        CHECK(0 == memcmp(PlainRun.Result.auiUnsuccessful, SingleRun.Result.auiBaselineUnsuccessful, sizeof(PlainRun.Result.auiUnsuccessful)) || !IsGroupedResult(&SingleRun.Result),
            "baseline failures %zu", uCase);
        CheckGroupConsistency(&SingleRun, "single");
        FreeRun(&PlainRun);
        FreeRun(&SingleRun);
    }
}

static void TestRandomGroups(void)
{
    for (uint32_t uiGroups = 2; uiGroups <= 13; uiGroups += 1) {
        for (size_t uCase = 0; uCase < sizeof(gaVerifyCases) / sizeof(gaVerifyCases[0]); uCase++) {
            const VerifyCase_t *pCase = &gaVerifyCases[uCase];
            SimulationConfig_t Config = MakeConfig(pCase->uiSymbolRate, pCase->uiWaveId, pCase->uiAlertSlots, pCase->uiLbSlots, pCase->uiTerminals, pCase->uiRounds, uiGroups);
            GroupConfig_t Groups = { 0 };
            GroupIssue_t aIssues[8];
            uint32_t uiIssues = ValidateGroupConfig(&Config, &Groups, aIssues, 8);
            VerifyRun_t Run;
            char acName[64];

            snprintf(acName, sizeof(acName), "groups %u case %zu", uiGroups, uCase);
            if (0 != uiIssues) {
                CHECK(GroupIssueNoSlot == aIssues[0].Type, "%s unexpected issue %u", acName, aIssues[0].Type);
                CHECK(!BuildGroupPlans(&Config, &Groups, Run.Result.aPlans), "%s invalid plan built", acName);
                continue;
            }
            CHECK(ExecuteRun(&Run, &Config, &Groups), "%s run", acName);
            CheckGroupConsistency(&Run, acName);
            FreeRun(&Run);
        }
    }
}

static void SetManualOwners(GroupConfig_t *pGroups, AreaType Area, const SimulationConfig_t *pConfig, const uint8_t *paucOwners)
{
    AreaGroupConfig_t *pArea = &pGroups->aAreas[Area];

    ResetGroupSlots(pArea);
    pArea->paucOwner = malloc(pConfig->auiSlots[Area]);
    memcpy(pArea->paucOwner, paucOwners, pConfig->auiSlots[Area]);
    pArea->uiGridSlots = pConfig->auiSlots[Area];
    pArea->uiGridTimeSlots = GetTimeSlotCount(pConfig->uiSymbolRate, pConfig->uiWaveId);
    pArea->SlotAssignment = SlotModeManual;
}

static void TestCollisionRules(void)
{
    SimulationConfig_t Config = MakeConfig(4000, 3, 10, 0, 2, 7, 1);
    GroupConfig_t Groups = { 0 };
    uint8_t aucOwners[10];
    VerifyRun_t Run;

    memset(aucOwners, GROUP_UNASSIGNED, sizeof(aucOwners));
    aucOwners[3] = 0;
    SetManualOwners(&Groups, AreaAlert, &Config, aucOwners);
    CHECK(0 == ValidateGroupConfig(&Config, &Groups, NULL, 0), "one slot group valid");
    CHECK(ExecuteRun(&Run, &Config, &Groups), "one slot run");
    for (uint64_t ullIndex = 0; ullIndex < Run.Result.ullRoundCount; ullIndex++) {
        RoundAreaCounts_t Counts = GetRoundAreaCounts(&Run.Result, ullIndex, AreaAlert);
        CHECK((0 == Counts.uiSuccess) && (1 == Counts.uiCollision) && (2 == Counts.uiCollidedAttempts), "same group one slot collides round %llu", ullIndex + 1);
    }
    CHECK(2 == Run.Result.auiUnsuccessful[AreaAlert], "both terminals fail");
    CHECK(2 == Run.Result.aauiGroupUnsuccessful[AreaAlert][0], "group failures");
    CheckGroupConsistency(&Run, "one slot");
    FreeRun(&Run);

    Config.uiGroupCount = 2;
    aucOwners[7] = 1;
    SetManualOwners(&Groups, AreaAlert, &Config, aucOwners);
    CHECK(0 == ValidateGroupConfig(&Config, &Groups, NULL, 0), "two groups valid");
    CHECK(ExecuteRun(&Run, &Config, &Groups), "two groups run");
    for (uint64_t ullIndex = 0; ullIndex < Run.Result.ullRoundCount; ullIndex++) {
        RoundAreaCounts_t Counts = GetRoundAreaCounts(&Run.Result, ullIndex, AreaAlert);
        CHECK((2 == Counts.uiSuccess) && (0 == Counts.uiCollision), "different groups never collide round %llu", ullIndex + 1);
    }
    CHECK(0 == Run.Result.auiUnsuccessful[AreaAlert], "no failures");
    CHECK(8 * Run.Result.ullRoundCount == Run.Result.aTotals[AreaAlert].ullIdle, "unassigned slots stay idle");
    CHECK(8 == Run.Result.aPlans[AreaAlert].uiUnassignedSlots, "unassigned count");
    CheckGroupConsistency(&Run, "two groups");
    FreeRun(&Run);

    /* A group with no terminals keeps its slots idle. */
    Config = MakeConfig(4000, 3, 30, 30, 10, 9, 3);
    ResetGroupSlots(&Groups.aAreas[AreaAlert]);
    FixGroupTerminalCount(&Groups.aAreas[AreaAlert], 0, 5);
    FixGroupTerminalCount(&Groups.aAreas[AreaAlert], 1, 0);
    FixGroupTerminalCount(&Groups.aAreas[AreaAlert], 2, 5);
    CHECK(0 == ValidateGroupConfig(&Config, &Groups, NULL, 0), "empty group valid");
    CHECK(ExecuteRun(&Run, &Config, &Groups), "empty group run");
    CHECK(0 == Run.Result.aaGroupTotals[AreaAlert][1].ullSuccess, "empty group no success");
    CHECK(10 * Run.Result.ullRoundCount == Run.Result.aaGroupTotals[AreaAlert][1].ullIdle, "empty group idle");
    CHECK(0 == Run.Result.aauiGroupUnsuccessful[AreaAlert][1], "empty group failures");
    CHECK((5 == GetGroupFirstTerminal(&Run.Result.aPlans[AreaAlert], 2)) && (5 == GetGroupTerminalCount(&Run.Result.aPlans[AreaAlert], 2)), "terminal ids");
    CheckGroupConsistency(&Run, "empty group");
    FreeRun(&Run);
    FreeGroupConfig(&Groups);
}

static void TestValidation(void)
{
    SimulationConfig_t Config = MakeConfig(4000, 3, 144, 361, 100, 3, 5);
    GroupConfig_t Groups = { 0 };
    GroupIssue_t aIssues[8];
    uint32_t uiIssues;
    wchar_t acText[256];
    AreaGroupLayout_t Layout;

    CHECK(0 == ValidateGroupConfig(&Config, &Groups, aIssues, 8), "auto valid");
    CHECK(ResolveAreaGroups(&Config, &Groups, AreaLb, &Layout), "resolve");
    CHECK((73 == Layout.auiSlots[0]) && (72 == Layout.auiSlots[4]) && (0 == Layout.uiUnassigned), "equal slots 361/5");
    CHECK((20 == Layout.auiTerminals[0]) && (100 == Layout.ullTerminalSum), "equal terminals");
    CHECK((0 == Layout.paucOwner[72]) && (1 == Layout.paucOwner[73]) && (4 == Layout.paucOwner[360]), "contiguous blocks");
    FreeAreaGroupLayout(&Layout);

    /* Fixing one group keeps the total: the automatic groups share the rest. */
    FixGroupTerminalCount(&Groups.aAreas[AreaAlert], 0, 25);
    CHECK(0 == ValidateGroupConfig(&Config, &Groups, aIssues, 8), "one fixed group still valid");
    CHECK(ResolveAreaGroups(&Config, &Groups, AreaAlert, &Layout), "resolve fixed");
    CHECK((25 == Layout.auiTerminals[0]) && (19 == Layout.auiTerminals[1]) && (19 == Layout.auiTerminals[3]) && (18 == Layout.auiTerminals[4]) &&
        (100 == Layout.ullTerminalSum) && (1 == Layout.uiFixedGroups) && Layout.abFixedTerminals[0] && !Layout.abFixedTerminals[1], "auto groups share 75");
    FreeAreaGroupLayout(&Layout);
    FixGroupTerminalCount(&Groups.aAreas[AreaAlert], 2, 0);
    CHECK(ResolveAreaGroups(&Config, &Groups, AreaAlert, &Layout), "resolve two fixed");
    CHECK((0 == Layout.auiTerminals[2]) && (25 == Layout.auiTerminals[1]) && (25 == Layout.auiTerminals[4]) && (100 == Layout.ullTerminalSum), "zero fixed group");
    FreeAreaGroupLayout(&Layout);
    FixGroupTerminalCount(&Groups.aAreas[AreaAlert], 1, 90);
    uiIssues = ValidateGroupConfig(&Config, &Groups, aIssues, 8);
    CHECK((1 == uiIssues) && (GroupIssueTerminalSum == aIssues[0].Type) && (115 == aIssues[0].ullValue) && (100 == aIssues[0].ullLimit), "fixed sum over total %llu", aIssues[0].ullValue);
    CHECK(FormatGroupIssue(&aIssues[0], acText, 256) && (NULL != wcsstr(acText, L"많습니다")), "format over");
    for (uint32_t uiGroup = 0; uiGroup < 5; uiGroup++) {
        FixGroupTerminalCount(&Groups.aAreas[AreaAlert], uiGroup, 10);
    }
    uiIssues = ValidateGroupConfig(&Config, &Groups, aIssues, 8);
    CHECK((1 == uiIssues) && (GroupIssueTerminalSum == aIssues[0].Type) && (50 == aIssues[0].ullValue), "all fixed sum under total");
    CHECK(FormatGroupIssue(&aIssues[0], acText, 256) && (NULL != wcsstr(acText, L"적습니다")), "format under");
    ReleaseGroupTerminalCount(&Groups.aAreas[AreaAlert], 4);
    CHECK(0 == ValidateGroupConfig(&Config, &Groups, aIssues, 8), "released group takes the rest");
    CHECK(ResolveAreaGroups(&Config, &Groups, AreaAlert, &Layout), "resolve released");
    CHECK((60 == Layout.auiTerminals[4]) && (4 == Layout.uiFixedGroups), "released group count");
    FreeAreaGroupLayout(&Layout);
    ResetGroupTerminals(&Groups.aAreas[AreaAlert]);
    CHECK(0 == CountFixedTerminalGroups(&Groups.aAreas[AreaAlert], 5), "reset clears fixed groups");

    /* Clearing all slots leaves every group with terminals but no slot. */
    CHECK(ClearGroupSlots(&Groups.aAreas[AreaAlert], 72, 144), "clear slots");
    uiIssues = ValidateGroupConfig(&Config, &Groups, aIssues, 8);
    CHECK((1 == uiIssues) && (GroupIssueNoSlot == aIssues[0].Type) && (0 == aIssues[0].uiFirstGroup) && (4 == aIssues[0].uiLastGroup), "cleared slots issue");
    CHECK(ResolveAreaGroups(&Config, &Groups, AreaAlert, &Layout), "resolve cleared");
    CHECK(144 == Layout.uiUnassigned, "all unassigned after clear");
    FreeAreaGroupLayout(&Layout);
    ResetGroupSlots(&Groups.aAreas[AreaAlert]);

    Config.uiGroupCount = 200 > GROUP_MAX_COUNT ? GROUP_MAX_COUNT : 200;
    Config.auiSlots[AreaAlert] = 7;
    uiIssues = ValidateGroupConfig(&Config, &Groups, aIssues, 8);
    CHECK((1 <= uiIssues) && (GroupIssueNoSlot == aIssues[0].Type) && (7 == aIssues[0].uiFirstGroup) && (99 == aIssues[0].uiLastGroup), "no slot range issue");
    CHECK(FormatGroupIssue(&aIssues[0], acText, 256), "format no slot");

    /* Manual slots keep coordinates; a narrower grid reports outside cells. */
    Config = MakeConfig(4000, 3, 144, 288, 100, 3, 5);
    CHECK(ConvertManualSlots(&Groups.aAreas[AreaLb], &Config, &Groups, AreaLb), "convert manual");
    Config.uiWaveId = 13;
    uiIssues = ValidateGroupConfig(&Config, &Groups, aIssues, 8);
    CHECK((1 <= uiIssues) && (GroupIssueOutside == aIssues[0].Type), "outside issue %u", uiIssues);
    CHECK(ResolveAreaGroups(&Config, &Groups, AreaLb, &Layout), "resolve narrow");
    CHECK((0 == Layout.paucOwner[0]) && (1 == Layout.paucOwner[24]), "coordinates kept");
    CHECK((GROUP_UNASSIGNED == Layout.paucOwner[24 * 4]) && (192 == Layout.uiOutside), "rows beyond source unassigned");
    FreeAreaGroupLayout(&Layout);
    Config.uiWaveId = 3;
    Config.auiSlots[AreaLb] = 100;
    uiIssues = ValidateGroupConfig(&Config, &Groups, aIssues, 8);
    CHECK((1 <= uiIssues) && (GroupIssueOutside == aIssues[0].Type) && (188 == aIssues[0].ullValue), "shrunk channels outside %llu", aIssues[0].ullValue);
    Config.auiSlots[AreaLb] = 288;
    Config.uiGroupCount = 3;
    uiIssues = ValidateGroupConfig(&Config, &Groups, aIssues, 8);
    CHECK((1 <= uiIssues) && (GroupIssueRemoved == aIssues[0].Type) && (114 == aIssues[0].ullValue), "removed group issue %llu", aIssues[0].ullValue);
    CHECK(ConvertManualSlots(&Groups.aAreas[AreaLb], &Config, &Groups, AreaLb), "release removed");
    CHECK(0 == ValidateGroupConfig(&Config, &Groups, aIssues, 8), "valid after release");

    /* Editing respects ownership. */
    {
        uint64_t ullChanged;
        uint64_t ullSkipped;
        SlotRect_t Rect = { 0, 57, 0, 0 };
        EditSlotRect(&Groups.aAreas[AreaLb], 72, &Rect, 2, false, &ullChanged, &ullSkipped);
        CHECK((0 == ullChanged) && (58 == ullSkipped), "assign over other group skipped");
        EditSlotRect(&Groups.aAreas[AreaLb], 72, &Rect, 0, true, &ullChanged, &ullSkipped);
        CHECK((58 == ullChanged) && (0 == ullSkipped), "release own");
        EditSlotRect(&Groups.aAreas[AreaLb], 72, &Rect, 2, false, &ullChanged, &ullSkipped);
        CHECK((58 == ullChanged) && (0 == ullSkipped), "assign released");
        Rect = (SlotRect_t){ 0, 71, 0, 0 };
        EditSlotRect(&Groups.aAreas[AreaLb], 72, &Rect, 2, true, &ullChanged, &ullSkipped);
        CHECK((58 == ullChanged) && (14 == ullSkipped), "release keeps other groups %llu %llu", ullChanged, ullSkipped);
        Rect = (SlotRect_t){ 0, 71, 3, 10 };
        EditSlotRect(&Groups.aAreas[AreaLb], 72, &Rect, 2, false, &ullChanged, &ullSkipped);
        CHECK((72 == ullChanged) && (0 == ullSkipped), "nonexistent rows ignored %llu %llu", ullChanged, ullSkipped);
    }

    /* Inactive areas, zero terminals and zero rounds. */
    Config = MakeConfig(4000, 3, 0, 50, 0, 0, 4);
    FreeGroupConfig(&Groups);
    CHECK(0 == ValidateGroupConfig(&Config, &Groups, aIssues, 8), "inactive area and zero terminals valid");
    {
        VerifyRun_t Run;
        CHECK(ExecuteRun(&Run, &Config, &Groups), "zero run");
        CHECK((0 == Run.Result.aPlans[AreaAlert].uiGroupCount) && (4 == Run.Result.aPlans[AreaLb].uiGroupCount), "plans for active area only");
        FreeRun(&Run);
        Config = MakeConfig(4000, 3, 0, 50, 0, 6, 4);
        CHECK(ExecuteRun(&Run, &Config, &Groups), "zero terminal run");
        CHECK(50 * 6 == Run.Result.aTotals[AreaLb].ullIdle, "zero terminal idle");
        CHECK(13 * 6 == Run.Result.aaGroupTotals[AreaLb][0].ullIdle, "zero terminal group idle");
        CheckGroupConsistency(&Run, "zero terminals");
        FreeRun(&Run);
    }
    FreeGroupConfig(&Groups);
}

#ifdef VERIFY_REPORT
static void ExportCase(const wchar_t *pcPath, const SimulationConfig_t *pConfig, const GroupConfig_t *pGroups)
{
    VerifyRun_t Run;

    CHECK(ExecuteRun(&Run, pConfig, pGroups), "export run");
    CHECK(ReportOk == CheckExportLimits(&Run.Result), "export limits");
    CHECK(ExportResults(pcPath, &Run.Result, NULL, NULL), "export %ls", pcPath);
    FreeRun(&Run);
}
#endif

int main(int iArgumentCount, char **ppcArguments)
{
    if ((iArgumentCount > 2) && (0 == strcmp(ppcArguments[1], "dump"))) {
        DumpCases(ppcArguments[2], (iArgumentCount > 3) ? (uint32_t)atoi(ppcArguments[3]) : 0);
        return 0;
    }
#ifdef VERIFY_REPORT
    if ((iArgumentCount > 1) && (0 == strcmp(ppcArguments[1], "split"))) {
        SimulationConfig_t Config = MakeConfig(500, 3, 9, 0, 3, 262144, 4);
        GroupConfig_t Groups = { 0 };
        VerifyRun_t Run;
        wchar_t acPath[MAX_PATH];

        if ((iArgumentCount < 3) || (0 == MultiByteToWideChar(CP_ACP, 0, ppcArguments[2], -1, acPath, MAX_PATH))) {
            return 2;
        }
        CHECK(ExecuteRun(&Run, &Config, &Groups), "split run");
        CHECK(ReportOk == CheckExportLimits(&Run.Result), "split limits");
        CHECK(ExportResults(acPath, &Run.Result, NULL, NULL), "split export");
        FreeRun(&Run);
        printf("%u checks, %u failures\n", guiChecks, guiFailures);
        return (0 == guiFailures) ? 0 : 1;
    }
#endif
    TestEquivalence();
    TestRandomGroups();
    TestCollisionRules();
    TestValidation();
    TestRepeats();
    TestProportional();
#ifdef VERIFY_REPORT
    {
        SimulationConfig_t Config = MakeConfig(4000, 3, 144, 288, 30, 12, 0);
        GroupConfig_t Groups = { 0 };
        ExportCase(L"out-plain.xlsx", &Config, NULL);
        Config.uiGroupCount = 3;
        FixGroupTerminalCount(&Groups.aAreas[AreaLb], 0, 12);
        FixGroupTerminalCount(&Groups.aAreas[AreaLb], 1, 18);
        FixGroupTerminalCount(&Groups.aAreas[AreaLb], 2, 0);
        SetProportionalSlots(&Groups.aAreas[AreaLb]);
        CHECK(ConvertManualSlots(&Groups.aAreas[AreaAlert], &Config, &Groups, AreaAlert), "manual alert");
        Groups.aAreas[AreaAlert].paucOwner[5] = GROUP_UNASSIGNED;
        Groups.aAreas[AreaAlert].paucOwner[80] = GROUP_UNASSIGNED;
        ExportCase(L"out-groups.xlsx", &Config, &Groups);
        FreeGroupConfig(&Groups);
        Config = MakeConfig(4000, 3, 144, 0, 30, 0, 2);
        ExportCase(L"out-zero-rounds.xlsx", &Config, &Groups);
        {
            VerifyRun_t Run;
            Config = MakeConfig(4000, 3, 144, 288, 300, 12, 10);
            CHECK(ExecuteRepeatedRun(&Run, &Config, &Groups, 30), "repeat export run");
            CHECK(ReportOk == CheckExportLimits(&Run.Result), "repeat export limits");
            CHECK(ExportResults(L"out-repeat.xlsx", &Run.Result, NULL, NULL), "repeat export");
            FreeRun(&Run);
            Config.uiGroupCount = 0;
            CHECK(ExecuteRepeatedRun(&Run, &Config, NULL, 12), "plain repeat export run");
            CHECK(ExportResults(L"out-repeat-plain.xlsx", &Run.Result, NULL, NULL), "plain repeat export");
            FreeRun(&Run);
        }
    }
#endif
    printf("%u checks, %u failures\n", guiChecks, guiFailures);
    return (0 == guiFailures) ? 0 : 1;
}
