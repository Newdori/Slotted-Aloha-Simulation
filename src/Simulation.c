#include "Simulation.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

uint32_t GetTimeSlotCount(uint32_t uiSymbolRate, uint32_t uiWaveId)
{
    uint32_t uiSlots;

    switch (uiSymbolRate) {
    case 500:
        uiSlots = 9;
        break;

    case 2000:
        uiSlots = 36;
        break;

    case 4000:
        uiSlots = 72;
        break;

    default:
        return 0;
    }

    if (3 == uiWaveId) {
        return uiSlots;
    }
    else if (13 == uiWaveId) {
        return uiSlots / 3;
    }
    else {
        return 0;
    }
}

uint32_t GetFrequencyRowCount(uint32_t uiSlots, uint32_t uiTimeSlots)
{
    if (0 == uiTimeSlots) {
        return 0;
    }
    else {
        return uiSlots / uiTimeSlots + (uint32_t)(0 != uiSlots % uiTimeSlots);
    }
}

uint64_t GetRoundCount(const SimulationConfig_t *pConfig)
{
    if (0 == pConfig->ullSuperframeMilliseconds) {
        return 0;
    }
    else {
        return pConfig->ullDurationMilliseconds / pConfig->ullSuperframeMilliseconds;
    }
}

bool ValidateCounters(const SimulationConfig_t *pConfig)
{
    uint64_t ullRounds = GetRoundCount(pConfig);

    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        uint32_t uiSlots = pConfig->auiSlots[uiArea];
        if ((0 != uiSlots) && (ullRounds > UINT64_MAX / uiSlots)) {
            return false;
        }
        if ((0 != uiSlots) && (0 != pConfig->uiTerminalCount) && (ullRounds > UINT64_MAX / pConfig->uiTerminalCount)) {
            return false;
        }
    }
    return true;
}

static uint64_t GenerateRandomValue(uint64_t *pullState)
{
    uint64_t ullValue;

    *pullState += UINT64_C(0x9e3779b97f4a7c15);
    ullValue = *pullState;
    ullValue = (ullValue ^ (ullValue >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    ullValue = (ullValue ^ (ullValue >> 27)) * UINT64_C(0x94d049bb133111eb);
    return ullValue ^ (ullValue >> 31);
}

static uint32_t SelectSlot(uint32_t uiSlots, uint64_t *pullState)
{
    uint64_t ullValue;
    uint64_t ullThreshold = (UINT64_C(0) - uiSlots) % uiSlots;

    do {
        ullValue = GenerateRandomValue(pullState);
    } while (ullValue < ullThreshold);

    return (uint32_t)(ullValue % uiSlots);
}

/* Terminals are visited in ascending order, so the group cursor only moves forward. */
static uint32_t SelectTerminalSlot(uint32_t uiSlots, const AreaGroupPlan_t *pPlan, uint32_t uiTerminal, uint32_t *puiGroup, uint64_t *pullState)
{
    uint32_t uiGroup = *puiGroup;

    if (NULL == pPlan) {
        return SelectSlot(uiSlots, pullState);
    }
    while ((uiGroup + 1 < pPlan->uiGroupCount) && (uiTerminal >= pPlan->auiTerminalEnd[uiGroup])) {
        uiGroup++;
    }
    *puiGroup = uiGroup;
    return pPlan->pauiSlotList[pPlan->auiSlotStart[uiGroup] + SelectSlot(pPlan->auiSlotCount[uiGroup], pullState)];
}

static bool SimulateArea(uint32_t uiSlots, uint32_t uiTerminals, const AreaGroupPlan_t *pPlan, uint64_t *pullRandomState, uint8_t *pucStates, SlotCounts_t *pCounts, RoundAreaCounts_t *paGroupCounts, CheckStopFunction CheckStop, void *pvContext)
{
    uint32_t uiGroup = 0;

    *pCounts = (SlotCounts_t){ .ullIdle = uiSlots };
    if (0 == uiSlots) {
        return true;
    }

    memset(pucStates, SlotIdle, uiSlots);
    for (uint32_t uiTerminal = 0; uiTerminal < uiTerminals; uiTerminal++) {
        uint32_t uiSlot;
        RoundAreaCounts_t *pGroup;

        if ((0 == (uiTerminal & UINT32_C(4095))) && (NULL != CheckStop) && CheckStop(pvContext)) {
            return false;
        }
        uiSlot = SelectTerminalSlot(uiSlots, pPlan, uiTerminal, &uiGroup, pullRandomState);
        pGroup = (NULL == paGroupCounts) ? NULL : &paGroupCounts[uiGroup];
        if (SlotIdle == pucStates[uiSlot]) {
            pucStates[uiSlot] = SlotSuccess;
            pCounts->ullIdle--;
            pCounts->ullSuccess++;
            if (NULL != pGroup) {
                pGroup->uiSuccess++;
            }
        }
        else if (SlotSuccess == pucStates[uiSlot]) {
            pucStates[uiSlot] = SlotCollision;
            pCounts->ullSuccess--;
            pCounts->ullCollision++;
            pCounts->ullCollidedAttempts += 2;
            if (NULL != pGroup) {
                pGroup->uiSuccess--;
                pGroup->uiCollision++;
                pGroup->uiCollidedAttempts += 2;
            }
        }
        else {
            pCounts->ullCollidedAttempts++;
            if (NULL != pGroup) {
                pGroup->uiCollidedAttempts++;
            }
        }
    }
    return true;
}

static bool RecordTerminalSuccess(uint32_t uiSlots, uint32_t uiTerminals, AreaType Area, const AreaGroupPlan_t *pPlan, uint64_t ullRandomState, const uint8_t *pucStates, TerminalSuccess_t *pSuccess, CheckStopFunction CheckStop, void *pvContext)
{
    AreaType Other = (AreaAlert == Area) ? AreaLb : AreaAlert;
    uint8_t *pucSeen = pSuccess->paucSeen[Area];
    const uint8_t *pucOther = pSuccess->paucSeen[Other];
    uint32_t uiGroup = 0;

    /* Replay the slot choices after collisions are known to identify the successful terminals. */
    for (uint32_t uiTerminal = 0; (uiTerminal < uiTerminals) && (pSuccess->auiSucceeded[Area] < uiTerminals); uiTerminal++) {
        uint32_t uiSlot;
        uint32_t uiByte = uiTerminal / 8;
        uint8_t ucMask = (uint8_t)(1U << (uiTerminal % 8));

        if ((0 == (uiTerminal & UINT32_C(4095))) && (NULL != CheckStop) && CheckStop(pvContext)) {
            return false;
        }
        uiSlot = SelectTerminalSlot(uiSlots, pPlan, uiTerminal, &uiGroup, &ullRandomState);
        if ((SlotSuccess == pucStates[uiSlot]) && (0 == (pucSeen[uiByte] & ucMask))) {
            pucSeen[uiByte] |= ucMask;
            pSuccess->auiSucceeded[Area]++;
            pSuccess->aauiGroupSucceeded[Area][uiGroup]++;
            if ((NULL == pucOther) || (0 == (pucOther[uiByte] & ucMask))) {
                pSuccess->uiSucceededAnywhere++;
            }
        }
    }
    return true;
}

static const AreaGroupPlan_t *GetActivePlan(const AreaGroupPlan_t *paPlans, uint32_t uiArea)
{
    if ((NULL == paPlans) || (0 == paPlans[uiArea].uiGroupCount)) {
        return NULL;
    }
    else {
        return &paPlans[uiArea];
    }
}

bool SimulateRound(const SimulationConfig_t *pConfig, const AreaGroupPlan_t *paPlans, uint64_t ullSeed, uint64_t ullRound, uint8_t *const paucStates[AreaCount], SlotCounts_t aCounts[AreaCount], RoundAreaCounts_t *paGroupCounts, TerminalSuccess_t *pSuccess, CheckStopFunction CheckStop, void *pvContext)
{
    uint64_t ullRandomState = ullSeed ^ (ullRound * UINT64_C(0xd1342543de82ef95));
    uint32_t uiGroups = pConfig->uiGroupCount;

    if ((NULL != paGroupCounts) && (0 != uiGroups)) {
        memset(paGroupCounts, 0, (size_t)AreaCount * uiGroups * sizeof(*paGroupCounts));
    }
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        uint64_t ullAreaState = ullRandomState;
        const AreaGroupPlan_t *pPlan = GetActivePlan(paPlans, uiArea);
        RoundAreaCounts_t *paAreaGroups = ((NULL == pPlan) || (NULL == paGroupCounts)) ? NULL : &paGroupCounts[(size_t)uiArea * uiGroups];

        if (!SimulateArea(pConfig->auiSlots[uiArea], pConfig->uiTerminalCount, pPlan, &ullRandomState, paucStates[uiArea], &aCounts[uiArea], paAreaGroups, CheckStop, pvContext)) {
            return false;
        }
        if ((NULL != pSuccess) && (NULL != pSuccess->paucSeen[uiArea]) && (0 != aCounts[uiArea].ullSuccess)) {
            if (!RecordTerminalSuccess(pConfig->auiSlots[uiArea], pConfig->uiTerminalCount, (AreaType)uiArea, pPlan, ullAreaState, paucStates[uiArea], pSuccess, CheckStop, pvContext)) {
                return false;
            }
        }
    }
    return true;
}

void AddSlotCounts(SlotCounts_t *pTotal, const SlotCounts_t *pRound)
{
    pTotal->ullIdle += pRound->ullIdle;
    pTotal->ullSuccess += pRound->ullSuccess;
    pTotal->ullCollision += pRound->ullCollision;
    pTotal->ullCollidedAttempts += pRound->ullCollidedAttempts;
}

static RoundAreaCounts_t ConvertRoundCounts(const SlotCounts_t *pCounts)
{
    return (RoundAreaCounts_t){
        .uiSuccess = (uint32_t)pCounts->ullSuccess,
        .uiCollision = (uint32_t)pCounts->ullCollision,
        .uiCollidedAttempts = (uint32_t)pCounts->ullCollidedAttempts
    };
}

static void FinishGroupTotals(SimulationResult_t *pResult)
{
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        const AreaGroupPlan_t *pPlan = &pResult->aPlans[uiArea];
        for (uint32_t uiGroup = 0; uiGroup < pPlan->uiGroupCount; uiGroup++) {
            SlotCounts_t *pTotal = &pResult->aaGroupTotals[uiArea][uiGroup];
            pTotal->ullIdle = pResult->ullRoundCount * pPlan->auiSlotCount[uiGroup] - pTotal->ullSuccess - pTotal->ullCollision;
        }
    }
}

static void AddGroupRound(SimulationResult_t *pResult, const RoundAreaCounts_t *paRound)
{
    uint32_t uiGroups = pResult->uiGroupCount;

    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        for (uint32_t uiGroup = 0; uiGroup < pResult->aPlans[uiArea].uiGroupCount; uiGroup++) {
            const RoundAreaCounts_t *pCounts = &paRound[uiArea * uiGroups + uiGroup];
            SlotCounts_t *pTotal = &pResult->aaGroupTotals[uiArea][uiGroup];
            pTotal->ullSuccess += pCounts->uiSuccess;
            pTotal->ullCollision += pCounts->uiCollision;
            pTotal->ullCollidedAttempts += pCounts->uiCollidedAttempts;
        }
    }
}

bool SimulateAllRounds(SimulationResult_t *pResult, uint64_t ullSeed, uint8_t *const paucStates[AreaCount], SlotCounts_t aRoundCounts[AreaCount], TerminalSuccess_t *pSuccess, TerminalSuccess_t *pBaselineSuccess, CheckStopFunction CheckStop, ReportProgressFunction ReportProgress, void *pvContext)
{
    bool bGrouped = IsGroupedResult(pResult);
    const AreaGroupPlan_t *paPlans = bGrouped ? pResult->aPlans : NULL;
    uint64_t ullRounds = pResult->ullRoundCount;

    if (0 == ullRounds) {
        SummarizeTerminalSuccess(pResult, pSuccess, bGrouped ? pBaselineSuccess : NULL);
        return true;
    }
    if (NULL == pResult->paRounds) {
        /* Without terminals or active areas every slot stays idle, so one round describes all of them. */
        if (!SimulateRound(&pResult->Config, paPlans, ullSeed, ullRounds, paucStates, aRoundCounts, NULL, NULL, CheckStop, pvContext)) {
            return false;
        }
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            pResult->aTotals[uiArea].ullIdle = aRoundCounts[uiArea].ullIdle * ullRounds;
            pResult->aBaselineTotals[uiArea].ullIdle = pResult->aTotals[uiArea].ullIdle;
        }
    }
    else {
        for (uint64_t ullIndex = 0; ullIndex < ullRounds; ullIndex++) {
            uint64_t ullRound = ullIndex + 1;
            RoundAreaCounts_t *paGroupRound = (NULL == pResult->paGroupRounds) ? NULL
                : &pResult->paGroupRounds[ullIndex * AreaCount * pResult->uiGroupCount];

            if (bGrouped) {
                /* The comparison run uses the same seed without groups, which is the original competition. */
                if (!SimulateRound(&pResult->Config, NULL, ullSeed, ullRound, paucStates, aRoundCounts, NULL, pBaselineSuccess, CheckStop, pvContext)) {
                    return false;
                }
                for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
                    pResult->paBaselineRounds[ullIndex].aAreas[uiArea] = ConvertRoundCounts(&aRoundCounts[uiArea]);
                    AddSlotCounts(&pResult->aBaselineTotals[uiArea], &aRoundCounts[uiArea]);
                }
            }
            if (!SimulateRound(&pResult->Config, paPlans, ullSeed, ullRound, paucStates, aRoundCounts, paGroupRound, pSuccess, CheckStop, pvContext)) {
                return false;
            }
            for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
                pResult->paRounds[ullIndex].aAreas[uiArea] = ConvertRoundCounts(&aRoundCounts[uiArea]);
                AddSlotCounts(&pResult->aTotals[uiArea], &aRoundCounts[uiArea]);
            }
            if (NULL != paGroupRound) {
                AddGroupRound(pResult, paGroupRound);
            }
            if (NULL != ReportProgress) {
                ReportProgress(pvContext, ullRound);
            }
        }
    }
    FinishGroupTotals(pResult);
    SummarizeTerminalSuccess(pResult, pSuccess, bGrouped ? pBaselineSuccess : NULL);
    return true;
}

uint32_t GetGroupFirstTerminal(const AreaGroupPlan_t *pPlan, uint32_t uiGroup)
{
    if ((0 == uiGroup) || (uiGroup > pPlan->uiGroupCount)) {
        return 0;
    }
    else {
        return pPlan->auiTerminalEnd[uiGroup - 1];
    }
}

uint32_t GetGroupTerminalCount(const AreaGroupPlan_t *pPlan, uint32_t uiGroup)
{
    if (uiGroup >= pPlan->uiGroupCount) {
        return 0;
    }
    else {
        return pPlan->auiTerminalEnd[uiGroup] - GetGroupFirstTerminal(pPlan, uiGroup);
    }
}

void SummarizeTerminalSuccess(SimulationResult_t *pResult, const TerminalSuccess_t *pSuccess, const TerminalSuccess_t *pBaselineSuccess)
{
    bool bAnyActive = false;

    memset(pResult->aauiGroupUnsuccessful, 0, sizeof(pResult->aauiGroupUnsuccessful));
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        const AreaGroupPlan_t *pPlan = &pResult->aPlans[uiArea];
        bool bActive = (0 != pResult->ullRoundCount) && (0 != pResult->Config.auiSlots[uiArea]);

        pResult->auiUnsuccessful[uiArea] = bActive ? pResult->Config.uiTerminalCount - pSuccess->auiSucceeded[uiArea] : 0;
        bAnyActive = bAnyActive || bActive;
        for (uint32_t uiGroup = 0; bActive && (uiGroup < pPlan->uiGroupCount); uiGroup++) {
            pResult->aauiGroupUnsuccessful[uiArea][uiGroup] = GetGroupTerminalCount(pPlan, uiGroup) - pSuccess->aauiGroupSucceeded[uiArea][uiGroup];
        }
        if (NULL != pBaselineSuccess) {
            pResult->auiBaselineUnsuccessful[uiArea] = bActive ? pResult->Config.uiTerminalCount - pBaselineSuccess->auiSucceeded[uiArea] : 0;
        }
    }
    pResult->uiUnsuccessfulAnywhere = bAnyActive ? pResult->Config.uiTerminalCount - pSuccess->uiSucceededAnywhere : 0;
}

RoundAreaCounts_t GetRoundAreaCounts(const SimulationResult_t *pResult, uint64_t ullIndex, AreaType Area)
{
    if (NULL == pResult->paRounds) {
        return (RoundAreaCounts_t){ 0 };
    }
    else {
        return pResult->paRounds[ullIndex].aAreas[Area];
    }
}

RoundAreaCounts_t GetBaselineRoundCounts(const SimulationResult_t *pResult, uint64_t ullIndex, AreaType Area)
{
    if (NULL == pResult->paBaselineRounds) {
        return (RoundAreaCounts_t){ 0 };
    }
    else {
        return pResult->paBaselineRounds[ullIndex].aAreas[Area];
    }
}

RoundAreaCounts_t GetRoundGroupCounts(const SimulationResult_t *pResult, uint64_t ullIndex, AreaType Area, uint32_t uiGroup)
{
    uint32_t uiGroups = pResult->uiGroupCount;

    if ((NULL == pResult->paGroupRounds) || (uiGroup >= pResult->aPlans[Area].uiGroupCount)) {
        return (RoundAreaCounts_t){ 0 };
    }
    else {
        return pResult->paGroupRounds[(ullIndex * AreaCount + (uint64_t)Area) * uiGroups + uiGroup];
    }
}

bool IsGroupedResult(const SimulationResult_t *pResult)
{
    return 0 != pResult->uiGroupCount;
}

static bool SumRoundTable(const SimulationResult_t *pResult, const RoundResult_t *paRounds, uint64_t ullRound, SlotCounts_t aCounts[AreaCount], CheckStopFunction CheckStop, void *pvContext)
{
    if (ullRound > pResult->ullRoundCount) {
        return false;
    }
    memset(aCounts, 0, AreaCount * sizeof(*aCounts));
    if (NULL == paRounds) {
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            aCounts[uiArea].ullIdle = ullRound * pResult->Config.auiSlots[uiArea];
        }
        return true;
    }
    for (uint64_t ullIndex = 0; ullIndex < ullRound; ullIndex++) {
        if ((0 == (ullIndex & UINT64_C(4095))) && (NULL != CheckStop) && CheckStop(pvContext)) {
            return false;
        }
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            const RoundAreaCounts_t *pCounts = &paRounds[ullIndex].aAreas[uiArea];
            aCounts[uiArea].ullSuccess += pCounts->uiSuccess;
            aCounts[uiArea].ullCollision += pCounts->uiCollision;
            aCounts[uiArea].ullCollidedAttempts += pCounts->uiCollidedAttempts;
        }
    }
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        aCounts[uiArea].ullIdle = ullRound * pResult->Config.auiSlots[uiArea] - aCounts[uiArea].ullSuccess - aCounts[uiArea].ullCollision;
    }
    return true;
}

bool SumRoundsThrough(const SimulationResult_t *pResult, uint64_t ullRound, SlotCounts_t aCounts[AreaCount], CheckStopFunction CheckStop, void *pvContext)
{
    return SumRoundTable(pResult, pResult->paRounds, ullRound, aCounts, CheckStop, pvContext);
}

bool SumBaselineRoundsThrough(const SimulationResult_t *pResult, uint64_t ullRound, SlotCounts_t aCounts[AreaCount], CheckStopFunction CheckStop, void *pvContext)
{
    return SumRoundTable(pResult, pResult->paBaselineRounds, ullRound, aCounts, CheckStop, pvContext);
}

bool SumGroupRoundsThrough(const SimulationResult_t *pResult, uint64_t ullRound, SlotCounts_t aaCounts[AreaCount][GROUP_MAX_COUNT], CheckStopFunction CheckStop, void *pvContext)
{
    uint32_t uiGroups = pResult->uiGroupCount;

    if (ullRound > pResult->ullRoundCount) {
        return false;
    }
    memset(aaCounts, 0, (size_t)AreaCount * GROUP_MAX_COUNT * sizeof(aaCounts[0][0]));
    for (uint64_t ullIndex = 0; (NULL != pResult->paGroupRounds) && (ullIndex < ullRound); ullIndex++) {
        const RoundAreaCounts_t *paRound = &pResult->paGroupRounds[ullIndex * AreaCount * uiGroups];

        if ((0 == (ullIndex & UINT64_C(4095))) && (NULL != CheckStop) && CheckStop(pvContext)) {
            return false;
        }
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            for (uint32_t uiGroup = 0; uiGroup < pResult->aPlans[uiArea].uiGroupCount; uiGroup++) {
                const RoundAreaCounts_t *pCounts = &paRound[uiArea * uiGroups + uiGroup];
                aaCounts[uiArea][uiGroup].ullSuccess += pCounts->uiSuccess;
                aaCounts[uiArea][uiGroup].ullCollision += pCounts->uiCollision;
                aaCounts[uiArea][uiGroup].ullCollidedAttempts += pCounts->uiCollidedAttempts;
            }
        }
    }
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        const AreaGroupPlan_t *pPlan = &pResult->aPlans[uiArea];
        for (uint32_t uiGroup = 0; uiGroup < pPlan->uiGroupCount; uiGroup++) {
            SlotCounts_t *pCounts = &aaCounts[uiArea][uiGroup];
            pCounts->ullIdle = ullRound * pPlan->auiSlotCount[uiGroup] - pCounts->ullSuccess - pCounts->ullCollision;
        }
    }
    return true;
}

uint64_t GetRepeatSeed(uint64_t ullSeed, uint32_t uiRepeat)
{
    uint64_t ullState = ullSeed + (uint64_t)uiRepeat * UINT64_C(0x6a09e667f3bcc909);

    /* Repetition 0 keeps the run seed so that the detailed run stays reproducible. */
    return (0 == uiRepeat) ? ullSeed : GenerateRandomValue(&ullState);
}

void CaptureRepeatSample(const SimulationResult_t *pResult, uint64_t ullSeed, RepeatSample_t *pSample)
{
    bool bGrouped = IsGroupedResult(pResult);

    *pSample = (RepeatSample_t){ .ullSeed = ullSeed };
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        pSample->aTotals[uiArea] = pResult->aTotals[uiArea];
        pSample->auiUnsuccessful[uiArea] = pResult->auiUnsuccessful[uiArea];
        pSample->aBaselineTotals[uiArea] = bGrouped ? pResult->aBaselineTotals[uiArea] : pResult->aTotals[uiArea];
        pSample->auiBaselineUnsuccessful[uiArea] = bGrouped ? pResult->auiBaselineUnsuccessful[uiArea] : pResult->auiUnsuccessful[uiArea];
    }
}

static void ResetTerminalSuccess(TerminalSuccess_t *pSuccess, uint32_t uiTerminals)
{
    size_t uBytes = (size_t)(((uint64_t)uiTerminals + 7) / 8);

    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        if (NULL != pSuccess->paucSeen[uiArea]) {
            memset(pSuccess->paucSeen[uiArea], 0, uBytes);
        }
    }
    memset(pSuccess->auiSucceeded, 0, sizeof(pSuccess->auiSucceeded));
    memset(pSuccess->aauiGroupSucceeded, 0, sizeof(pSuccess->aauiGroupSucceeded));
    pSuccess->uiSucceededAnywhere = 0;
}

/* Runs one more repetition with its own seed and keeps only the final values. The run tables are not touched. */
bool SimulateRepeat(const SimulationResult_t *pResult, uint64_t ullSeed, uint8_t *const paucStates[AreaCount], SlotCounts_t aRoundCounts[AreaCount], TerminalSuccess_t *pSuccess, TerminalSuccess_t *pBaselineSuccess, RepeatSample_t *pSample, CheckStopFunction CheckStop, void *pvContext)
{
    const SimulationConfig_t *pConfig = &pResult->Config;
    bool bGrouped = IsGroupedResult(pResult);
    const AreaGroupPlan_t *paPlans = bGrouped ? pResult->aPlans : NULL;

    if (NULL == pResult->paRounds) {
        /* Without terminals or active areas every repetition is the same. */
        CaptureRepeatSample(pResult, ullSeed, pSample);
        return true;
    }
    *pSample = (RepeatSample_t){ .ullSeed = ullSeed };
    ResetTerminalSuccess(pSuccess, pConfig->uiTerminalCount);
    if (bGrouped) {
        ResetTerminalSuccess(pBaselineSuccess, pConfig->uiTerminalCount);
    }
    for (uint64_t ullRound = 1; ullRound <= pResult->ullRoundCount; ullRound++) {
        if (bGrouped) {
            if (!SimulateRound(pConfig, NULL, ullSeed, ullRound, paucStates, aRoundCounts, NULL, pBaselineSuccess, CheckStop, pvContext)) {
                return false;
            }
            for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
                AddSlotCounts(&pSample->aBaselineTotals[uiArea], &aRoundCounts[uiArea]);
            }
        }
        if (!SimulateRound(pConfig, paPlans, ullSeed, ullRound, paucStates, aRoundCounts, NULL, pSuccess, CheckStop, pvContext)) {
            return false;
        }
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            AddSlotCounts(&pSample->aTotals[uiArea], &aRoundCounts[uiArea]);
        }
    }
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        bool bActive = 0 != pConfig->auiSlots[uiArea];

        pSample->auiUnsuccessful[uiArea] = bActive ? pConfig->uiTerminalCount - pSuccess->auiSucceeded[uiArea] : 0;
        if (bGrouped) {
            pSample->auiBaselineUnsuccessful[uiArea] = bActive ? pConfig->uiTerminalCount - pBaselineSuccess->auiSucceeded[uiArea] : 0;
        }
        else {
            pSample->auiBaselineUnsuccessful[uiArea] = pSample->auiUnsuccessful[uiArea];
            pSample->aBaselineTotals[uiArea] = pSample->aTotals[uiArea];
        }
    }
    return true;
}

bool IsRepeatedResult(const SimulationResult_t *pResult)
{
    return (NULL != pResult->paRepeats) && (pResult->uiRepeatCount > 1);
}

double GetRepeatValue(const RepeatSample_t *pSample, AreaType Area, RepeatMetric Metric, RepeatSeries Series)
{
    double dGrouped;
    double dBaseline;

    switch (Metric) {
    case RepeatFailure:
        dGrouped = pSample->auiUnsuccessful[Area];
        dBaseline = pSample->auiBaselineUnsuccessful[Area];
        break;

    case RepeatSuccess:
        dGrouped = (double)pSample->aTotals[Area].ullSuccess;
        dBaseline = (double)pSample->aBaselineTotals[Area].ullSuccess;
        break;

    default:
        dGrouped = (double)pSample->aTotals[Area].ullCollidedAttempts;
        dBaseline = (double)pSample->aBaselineTotals[Area].ullCollidedAttempts;
        break;
    }
    if (RepeatBaseline == Series) {
        return dBaseline;
    }
    else {
        return (RepeatDifference == Series) ? dGrouped - dBaseline : dGrouped;
    }
}

/* Mean, sample standard deviation, minimum and maximum over the finished repetitions. */
void SummarizeRepeats(const SimulationResult_t *pResult, AreaType Area, RepeatMetric Metric, RepeatSeries Series, RepeatStatistic_t *pStatistic)
{
    double dMean = 0.0;
    double dSquares = 0.0;

    *pStatistic = (RepeatStatistic_t){ 0 };
    for (uint32_t uiRepeat = 0; (NULL != pResult->paRepeats) && (uiRepeat < pResult->uiRepeatCount); uiRepeat++) {
        double dValue = GetRepeatValue(&pResult->paRepeats[uiRepeat], Area, Metric, Series);
        double dDelta = dValue - dMean;

        dMean += dDelta / (uiRepeat + 1);
        dSquares += dDelta * (dValue - dMean);
        if ((0 == uiRepeat) || (dValue < pStatistic->dMinimum)) {
            pStatistic->dMinimum = dValue;
        }
        if ((0 == uiRepeat) || (dValue > pStatistic->dMaximum)) {
            pStatistic->dMaximum = dValue;
        }
    }
    pStatistic->dMean = dMean;
    pStatistic->dDeviation = (pResult->uiRepeatCount > 1) ? sqrt(dSquares / (pResult->uiRepeatCount - 1)) : 0.0;
}