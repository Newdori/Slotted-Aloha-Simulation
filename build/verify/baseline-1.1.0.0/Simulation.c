#include "Simulation.h"

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

static bool SimulateArea(uint32_t uiSlots, uint32_t uiTerminals, uint64_t *pullRandomState, uint8_t *pucStates, SlotCounts_t *pCounts, CheckStopFunction CheckStop, void *pvContext)
{
    *pCounts = (SlotCounts_t){ .ullIdle = uiSlots };
    if (0 == uiSlots) {
        return true;
    }

    memset(pucStates, SlotIdle, uiSlots);
    for (uint32_t uiTerminal = 0; uiTerminal < uiTerminals; uiTerminal++) {
        uint32_t uiSlot;

        if ((0 == (uiTerminal & UINT32_C(4095))) && (NULL != CheckStop) && CheckStop(pvContext)) {
            return false;
        }
        uiSlot = SelectSlot(uiSlots, pullRandomState);
        if (SlotIdle == pucStates[uiSlot]) {
            pucStates[uiSlot] = SlotSuccess;
            pCounts->ullIdle--;
            pCounts->ullSuccess++;
        }
        else if (SlotSuccess == pucStates[uiSlot]) {
            pucStates[uiSlot] = SlotCollision;
            pCounts->ullSuccess--;
            pCounts->ullCollision++;
            pCounts->ullCollidedAttempts += 2;
        }
        else {
            pCounts->ullCollidedAttempts++;
        }
    }
    return true;
}

static bool RecordTerminalSuccess(uint32_t uiSlots, uint32_t uiTerminals, AreaType Area, uint64_t ullRandomState, const uint8_t *pucStates, TerminalSuccess_t *pSuccess, CheckStopFunction CheckStop, void *pvContext)
{
    AreaType Other = (AreaAlert == Area) ? AreaLb : AreaAlert;
    uint8_t *pucSeen = pSuccess->paucSeen[Area];
    const uint8_t *pucOther = pSuccess->paucSeen[Other];

    /* Replay the slot choices after collisions are known to identify the successful terminals. */
    for (uint32_t uiTerminal = 0; (uiTerminal < uiTerminals) && (pSuccess->auiSucceeded[Area] < uiTerminals); uiTerminal++) {
        uint32_t uiSlot;
        uint32_t uiByte = uiTerminal / 8;
        uint8_t ucMask = (uint8_t)(1U << (uiTerminal % 8));

        if ((0 == (uiTerminal & UINT32_C(4095))) && (NULL != CheckStop) && CheckStop(pvContext)) {
            return false;
        }
        uiSlot = SelectSlot(uiSlots, &ullRandomState);
        if ((SlotSuccess == pucStates[uiSlot]) && (0 == (pucSeen[uiByte] & ucMask))) {
            pucSeen[uiByte] |= ucMask;
            pSuccess->auiSucceeded[Area]++;
            if ((NULL == pucOther) || (0 == (pucOther[uiByte] & ucMask))) {
                pSuccess->uiSucceededAnywhere++;
            }
        }
    }
    return true;
}

bool SimulateRound(const SimulationConfig_t *pConfig, uint64_t ullSeed, uint64_t ullRound, uint8_t *const paucStates[AreaCount], SlotCounts_t aCounts[AreaCount], TerminalSuccess_t *pSuccess, CheckStopFunction CheckStop, void *pvContext)
{
    uint64_t ullRandomState = ullSeed ^ (ullRound * UINT64_C(0xd1342543de82ef95));

    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        uint64_t ullAreaState = ullRandomState;

        if (!SimulateArea(pConfig->auiSlots[uiArea], pConfig->uiTerminalCount, &ullRandomState, paucStates[uiArea], &aCounts[uiArea], CheckStop, pvContext)) {
            return false;
        }
        if ((NULL != pSuccess) && (NULL != pSuccess->paucSeen[uiArea]) && (0 != aCounts[uiArea].ullSuccess)) {
            if (!RecordTerminalSuccess(pConfig->auiSlots[uiArea], pConfig->uiTerminalCount, (AreaType)uiArea, ullAreaState, paucStates[uiArea], pSuccess, CheckStop, pvContext)) {
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

void SummarizeTerminalSuccess(SimulationResult_t *pResult, const TerminalSuccess_t *pSuccess)
{
    bool bAnyActive = false;

    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        bool bActive = (0 != pResult->ullRoundCount) && (0 != pResult->Config.auiSlots[uiArea]);
        pResult->auiUnsuccessful[uiArea] = bActive ? pResult->Config.uiTerminalCount - pSuccess->auiSucceeded[uiArea] : 0;
        bAnyActive = bAnyActive || bActive;
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

bool SumRoundsThrough(const SimulationResult_t *pResult, uint64_t ullRound, SlotCounts_t aCounts[AreaCount], CheckStopFunction CheckStop, void *pvContext)
{
    if (ullRound > pResult->ullRoundCount) {
        return false;
    }
    memset(aCounts, 0, AreaCount * sizeof(*aCounts));
    if (NULL == pResult->paRounds) {
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
            RoundAreaCounts_t Counts = GetRoundAreaCounts(pResult, ullIndex, (AreaType)uiArea);
            aCounts[uiArea].ullSuccess += Counts.uiSuccess;
            aCounts[uiArea].ullCollision += Counts.uiCollision;
            aCounts[uiArea].ullCollidedAttempts += Counts.uiCollidedAttempts;
        }
    }
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        aCounts[uiArea].ullIdle = ullRound * pResult->Config.auiSlots[uiArea] - aCounts[uiArea].ullSuccess - aCounts[uiArea].ullCollision;
    }
    return true;
}
