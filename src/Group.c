#include "Group.h"

#include <stdlib.h>
#include <string.h>

static const wchar_t *const gpacGroupAreaNames[AreaCount] = { L"Alert", L"LB" };

void FreeGroupConfig(GroupConfig_t *pGroups)
{
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        free(pGroups->aAreas[uiArea].paucOwner);
    }
    memset(pGroups, 0, sizeof(*pGroups));
}

bool CopyGroupConfig(GroupConfig_t *pTarget, const GroupConfig_t *pSource)
{
    GroupConfig_t Copy = *pSource;

    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        Copy.aAreas[uiArea].paucOwner = NULL;
    }
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        const AreaGroupConfig_t *pArea = &pSource->aAreas[uiArea];

        if ((NULL != pArea->paucOwner) && (0 != pArea->uiGridSlots)) {
            Copy.aAreas[uiArea].paucOwner = malloc(pArea->uiGridSlots);
            if (NULL == Copy.aAreas[uiArea].paucOwner) {
                FreeGroupConfig(&Copy);
                return false;
            }
            memcpy(Copy.aAreas[uiArea].paucOwner, pArea->paucOwner, pArea->uiGridSlots);
        }
    }
    FreeGroupConfig(pTarget);
    *pTarget = Copy;
    return true;
}

void DistributeTerminals(uint32_t uiTerminals, uint32_t uiGroups, uint32_t auiCounts[GROUP_MAX_COUNT])
{
    memset(auiCounts, 0, GROUP_MAX_COUNT * sizeof(*auiCounts));
    for (uint32_t uiGroup = 0; (0 != uiGroups) && (uiGroup < uiGroups) && (uiGroup < GROUP_MAX_COUNT); uiGroup++) {
        auiCounts[uiGroup] = uiTerminals / uiGroups + (uint32_t)(uiGroup < uiTerminals % uiGroups);
    }
}

void DistributeSlots(uint8_t *paucOwner, uint32_t uiSlots, uint32_t uiGroups)
{
    uint32_t uiSlot = 0;

    if (0 == uiGroups) {
        memset(paucOwner, GROUP_UNASSIGNED, uiSlots);
        return;
    }
    for (uint32_t uiGroup = 0; uiGroup < uiGroups; uiGroup++) {
        uint32_t uiSize = uiSlots / uiGroups + (uint32_t)(uiGroup < uiSlots % uiGroups);
        memset(&paucOwner[uiSlot], (int32_t)uiGroup, uiSize);
        uiSlot += uiSize;
    }
}

/* Slots follow the terminal share of each group, so every group has about the same terminals per slot.
   Shares are rounded down and the leftover slots go to the largest remainders (earlier group on ties). With equal
   terminal counts this is the same as the equal distribution. A group with terminals keeps at least one slot when
   another group can spare one. */
void DistributeProportionalSlots(uint8_t *paucOwner, uint32_t uiSlots, uint32_t uiGroups, const uint32_t auiTerminals[GROUP_MAX_COUNT])
{
    uint32_t auiSizes[GROUP_MAX_COUNT] = { 0 };
    uint64_t aullRemainders[GROUP_MAX_COUNT] = { 0 };
    uint64_t ullTotal = 0;
    uint32_t uiUsed = 0;
    uint32_t uiSlot = 0;

    uiGroups = (uiGroups > GROUP_MAX_COUNT) ? GROUP_MAX_COUNT : uiGroups;
    for (uint32_t uiGroup = 0; uiGroup < uiGroups; uiGroup++) {
        ullTotal += auiTerminals[uiGroup];
    }
    if ((0 == uiGroups) || (0 == ullTotal)) {
        DistributeSlots(paucOwner, uiSlots, uiGroups);
        return;
    }
    for (uint32_t uiGroup = 0; uiGroup < uiGroups; uiGroup++) {
        uint64_t ullShare = (uint64_t)uiSlots * auiTerminals[uiGroup];
        auiSizes[uiGroup] = (uint32_t)(ullShare / ullTotal);
        aullRemainders[uiGroup] = ullShare % ullTotal;
        uiUsed += auiSizes[uiGroup];
    }
    while (uiUsed < uiSlots) {
        uint32_t uiBest = 0;
        for (uint32_t uiGroup = 1; uiGroup < uiGroups; uiGroup++) {
            if (aullRemainders[uiGroup] > aullRemainders[uiBest]) {
                uiBest = uiGroup;
            }
        }
        auiSizes[uiBest]++;
        aullRemainders[uiBest] = 0;
        uiUsed++;
    }
    for (uint32_t uiGroup = 0; uiGroup < uiGroups; uiGroup++) {
        uint32_t uiDonor = 0;

        if ((0 == auiTerminals[uiGroup]) || (0 != auiSizes[uiGroup])) {
            continue;
        }
        for (uint32_t uiCandidate = 1; uiCandidate < uiGroups; uiCandidate++) {
            if (auiSizes[uiCandidate] > auiSizes[uiDonor]) {
                uiDonor = uiCandidate;
            }
        }
        if (auiSizes[uiDonor] > 1) {
            auiSizes[uiDonor]--;
            auiSizes[uiGroup] = 1;
        }
    }
    for (uint32_t uiGroup = 0; uiGroup < uiGroups; uiGroup++) {
        memset(&paucOwner[uiSlot], (int32_t)uiGroup, auiSizes[uiGroup]);
        uiSlot += auiSizes[uiGroup];
    }
}

const wchar_t *GetSlotModeName(SlotMode Mode)
{
    switch (Mode) {
    case SlotModeProportional:
        return L"단말 수 비례";

    case SlotModeManual:
        return L"직접 지정";

    default:
        return L"균등";
    }
}

bool IsAreaGroupActive(const SimulationConfig_t *pConfig, AreaType Area)
{
    return (0 != pConfig->uiGroupCount) && (0 != pConfig->auiSlots[Area]) && (0 != GetTimeSlotCount(pConfig->uiSymbolRate, pConfig->uiWaveId));
}

static void MapManualSlots(const AreaGroupConfig_t *pArea, AreaGroupLayout_t *pLayout)
{
    uint32_t uiWidth = pArea->uiGridTimeSlots;

    memset(pLayout->paucOwner, GROUP_UNASSIGNED, pLayout->uiSlots);
    for (uint32_t uiIndex = 0; (NULL != pArea->paucOwner) && (uiIndex < pArea->uiGridSlots); uiIndex++) {
        uint8_t ucGroup = pArea->paucOwner[uiIndex];
        uint32_t uiTime;
        uint64_t ullTarget;

        if (GROUP_UNASSIGNED == ucGroup) {
            continue;
        }
        /* Keep the time and frequency position when the grid width changes. */
        uiTime = (0 == uiWidth) ? UINT32_MAX : uiIndex % uiWidth;
        ullTarget = (0 == uiWidth) ? UINT64_MAX : (uint64_t)(uiIndex / uiWidth) * pLayout->uiTimeSlots + uiTime;
        if ((uiTime >= pLayout->uiTimeSlots) || (ullTarget >= pLayout->uiSlots)) {
            pLayout->uiOutside++;
        }
        else if (ucGroup >= pLayout->uiGroupCount) {
            pLayout->uiRemoved++;
        }
        else {
            pLayout->paucOwner[ullTarget] = ucGroup;
        }
    }
}

uint32_t CountFixedTerminalGroups(const AreaGroupConfig_t *pArea, uint32_t uiGroupCount)
{
    uint32_t uiFixed = 0;

    for (uint32_t uiGroup = 0; (uiGroup < uiGroupCount) && (uiGroup < GROUP_MAX_COUNT); uiGroup++) {
        uiFixed += (uint32_t)pArea->abFixedTerminals[uiGroup];
    }
    return uiFixed;
}

/* Fixed groups keep their counts. The other groups share the rest equally, earlier groups first. */
static void ResolveTerminals(const AreaGroupConfig_t *pArea, uint32_t uiTerminals, AreaGroupLayout_t *pLayout)
{
    uint64_t ullFixed = 0;
    uint64_t ullRest;
    uint32_t uiAuto = 0;
    uint32_t uiIndex = 0;

    for (uint32_t uiGroup = 0; uiGroup < pLayout->uiGroupCount; uiGroup++) {
        pLayout->abFixedTerminals[uiGroup] = pArea->abFixedTerminals[uiGroup];
        if (pArea->abFixedTerminals[uiGroup]) {
            pLayout->auiTerminals[uiGroup] = pArea->auiTerminals[uiGroup];
            ullFixed += pArea->auiTerminals[uiGroup];
            pLayout->uiFixedGroups++;
        }
        else {
            uiAuto++;
        }
    }
    ullRest = (ullFixed < uiTerminals) ? uiTerminals - ullFixed : 0;
    for (uint32_t uiGroup = 0; (0 != uiAuto) && (uiGroup < pLayout->uiGroupCount); uiGroup++) {
        if (!pArea->abFixedTerminals[uiGroup]) {
            pLayout->auiTerminals[uiGroup] = (uint32_t)(ullRest / uiAuto + (uint64_t)(uiIndex < ullRest % uiAuto));
            uiIndex++;
        }
    }
    for (uint32_t uiGroup = 0; uiGroup < pLayout->uiGroupCount; uiGroup++) {
        pLayout->ullTerminalSum += pLayout->auiTerminals[uiGroup];
    }
}

bool ResolveAreaGroups(const SimulationConfig_t *pConfig, const GroupConfig_t *pGroups, AreaType Area, AreaGroupLayout_t *pLayout)
{
    const AreaGroupConfig_t *pArea = &pGroups->aAreas[Area];

    *pLayout = (AreaGroupLayout_t){
        .uiGroupCount = pConfig->uiGroupCount,
        .uiTimeSlots = GetTimeSlotCount(pConfig->uiSymbolRate, pConfig->uiWaveId),
        .uiSlots = pConfig->auiSlots[Area]
    };
    if (!IsAreaGroupActive(pConfig, Area) || (pConfig->uiGroupCount > GROUP_MAX_COUNT)) {
        return true;
    }
    ResolveTerminals(pArea, pConfig->uiTerminalCount, pLayout);

    pLayout->paucOwner = malloc(pLayout->uiSlots);
    if (NULL == pLayout->paucOwner) {
        return false;
    }
    if (SlotModeManual == pArea->SlotAssignment) {
        MapManualSlots(pArea, pLayout);
    }
    else if (SlotModeProportional == pArea->SlotAssignment) {
        DistributeProportionalSlots(pLayout->paucOwner, pLayout->uiSlots, pLayout->uiGroupCount, pLayout->auiTerminals);
    }
    else {
        DistributeSlots(pLayout->paucOwner, pLayout->uiSlots, pLayout->uiGroupCount);
    }
    for (uint32_t uiSlot = 0; uiSlot < pLayout->uiSlots; uiSlot++) {
        uint8_t ucGroup = pLayout->paucOwner[uiSlot];
        if (GROUP_UNASSIGNED == ucGroup) {
            pLayout->uiUnassigned++;
        }
        else {
            pLayout->auiSlots[ucGroup]++;
        }
    }
    return true;
}

void FreeAreaGroupLayout(AreaGroupLayout_t *pLayout)
{
    free(pLayout->paucOwner);
    pLayout->paucOwner = NULL;
}

static void AddGroupIssue(GroupIssue_t *paIssues, uint32_t uiCapacity, uint32_t *puiCount, GroupIssue_t Issue)
{
    if (*puiCount < uiCapacity) {
        paIssues[*puiCount] = Issue;
    }
    (*puiCount)++;
}

static void CheckAreaLayout(const SimulationConfig_t *pConfig, const AreaGroupLayout_t *pLayout, AreaType Area, GroupIssue_t *paIssues, uint32_t uiCapacity, uint32_t *puiCount)
{
    uint32_t uiGroup = 0;

    if (pLayout->ullTerminalSum != pConfig->uiTerminalCount) {
        AddGroupIssue(paIssues, uiCapacity, puiCount, (GroupIssue_t){ .Area = Area, .Type = GroupIssueTerminalSum,
            .ullValue = pLayout->ullTerminalSum, .ullLimit = pConfig->uiTerminalCount });
    }
    if (0 != pLayout->uiOutside) {
        AddGroupIssue(paIssues, uiCapacity, puiCount, (GroupIssue_t){ .Area = Area, .Type = GroupIssueOutside, .ullValue = pLayout->uiOutside });
    }
    if (0 != pLayout->uiRemoved) {
        AddGroupIssue(paIssues, uiCapacity, puiCount, (GroupIssue_t){ .Area = Area, .Type = GroupIssueRemoved, .ullValue = pLayout->uiRemoved });
    }
    while (uiGroup < pLayout->uiGroupCount) {
        uint32_t uiLast = uiGroup;

        if ((0 == pLayout->auiTerminals[uiGroup]) || (0 != pLayout->auiSlots[uiGroup])) {
            uiGroup++;
            continue;
        }
        while ((uiLast + 1 < pLayout->uiGroupCount) && (0 != pLayout->auiTerminals[uiLast + 1]) && (0 == pLayout->auiSlots[uiLast + 1])) {
            uiLast++;
        }
        AddGroupIssue(paIssues, uiCapacity, puiCount, (GroupIssue_t){ .Area = Area, .Type = GroupIssueNoSlot,
            .uiFirstGroup = uiGroup, .uiLastGroup = uiLast });
        uiGroup = uiLast + 1;
    }
}

uint32_t CheckAreaGroupLayout(const SimulationConfig_t *pConfig, const AreaGroupLayout_t *pLayout, AreaType Area, GroupIssue_t *paIssues, uint32_t uiCapacity)
{
    uint32_t uiCount = 0;

    CheckAreaLayout(pConfig, pLayout, Area, paIssues, uiCapacity, &uiCount);
    return uiCount;
}

uint32_t ValidateGroupConfig(const SimulationConfig_t *pConfig, const GroupConfig_t *pGroups, GroupIssue_t *paIssues, uint32_t uiCapacity)
{
    uint32_t uiCount = 0;

    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        AreaGroupLayout_t Layout;

        if (!IsAreaGroupActive(pConfig, (AreaType)uiArea)) {
            continue;
        }
        if (!ResolveAreaGroups(pConfig, pGroups, (AreaType)uiArea, &Layout)) {
            AddGroupIssue(paIssues, uiCapacity, &uiCount, (GroupIssue_t){ .Area = (AreaType)uiArea, .Type = GroupIssueMemory });
            continue;
        }
        CheckAreaLayout(pConfig, &Layout, (AreaType)uiArea, paIssues, uiCapacity, &uiCount);
        FreeAreaGroupLayout(&Layout);
    }
    return uiCount;
}

bool FormatGroupIssue(const GroupIssue_t *pIssue, wchar_t *pcText, size_t uCapacity)
{
    const wchar_t *pcArea = gpacGroupAreaNames[pIssue->Area];
    int32_t iWritten = -1;

    switch (pIssue->Type) {
    case GroupIssueMemory:
        iWritten = swprintf(pcText, uCapacity, L"%ls: 그룹 배정을 검사할 메모리가 부족합니다.", pcArea);
        break;

    case GroupIssueTerminalSum:
        if (pIssue->ullValue > pIssue->ullLimit) {
            iWritten = swprintf(pcText, uCapacity, L"%ls: 직접 지정한 그룹 단말 수 합계 %llu대가 단말 수 %llu대보다 많습니다.", pcArea, pIssue->ullValue, pIssue->ullLimit);
        }
        else {
            iWritten = swprintf(pcText, uCapacity, L"%ls: 모든 그룹을 직접 지정했지만 합계 %llu대가 단말 수 %llu대보다 적습니다. 한 그룹을 자동으로 되돌리세요.",
                pcArea, pIssue->ullValue, pIssue->ullLimit);
        }
        break;

    case GroupIssueNoSlot:
        if (pIssue->uiFirstGroup == pIssue->uiLastGroup) {
            iWritten = swprintf(pcText, uCapacity, L"%ls 그룹 %u: 단말이 있지만 배정 칸이 없습니다.", pcArea, pIssue->uiFirstGroup + 1);
        }
        else {
            iWritten = swprintf(pcText, uCapacity, L"%ls 그룹 %u~%u: 단말이 있지만 배정 칸이 없습니다.", pcArea, pIssue->uiFirstGroup + 1, pIssue->uiLastGroup + 1);
        }
        break;

    case GroupIssueOutside:
        iWritten = swprintf(pcText, uCapacity, L"%ls: 현재 격자 범위를 벗어난 배정 칸 %llu개가 있습니다. 그룹 설정에서 해제하세요.", pcArea, pIssue->ullValue);
        break;

    case GroupIssueRemoved:
        iWritten = swprintf(pcText, uCapacity, L"%ls: 그룹 수가 줄어 없어진 그룹의 배정 칸 %llu개가 있습니다. 그룹 설정에서 해제하세요.", pcArea, pIssue->ullValue);
        break;

    default:
        break;
    }
    return (iWritten >= 0) && ((size_t)iWritten < uCapacity);
}

void FreeGroupPlans(AreaGroupPlan_t aPlans[AreaCount])
{
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        free(aPlans[uiArea].pauiSlotList);
        free(aPlans[uiArea].paucSlotGroup);
    }
    memset(aPlans, 0, AreaCount * sizeof(*aPlans));
}

static bool BuildAreaPlan(const AreaGroupLayout_t *pLayout, const AreaGroupConfig_t *pArea, uint32_t uiTerminals, AreaGroupPlan_t *pPlan)
{
    uint32_t auiCursor[GROUP_MAX_COUNT];
    uint32_t uiAssigned = pLayout->uiSlots - pLayout->uiUnassigned;
    uint32_t uiTerminalEnd = 0;
    uint32_t uiSlotStart = 0;

    if ((pLayout->ullTerminalSum != uiTerminals) || (0 != pLayout->uiOutside) || (0 != pLayout->uiRemoved)) {
        return false;
    }
    pPlan->uiGroupCount = pLayout->uiGroupCount;
    pPlan->uiUnassignedSlots = pLayout->uiUnassigned;
    pPlan->uiFixedTerminalGroups = pLayout->uiFixedGroups;
    memcpy(pPlan->abFixedTerminals, pLayout->abFixedTerminals, sizeof(pPlan->abFixedTerminals));
    pPlan->SlotAssignment = pArea->SlotAssignment;
    for (uint32_t uiGroup = 0; uiGroup < pLayout->uiGroupCount; uiGroup++) {
        if ((0 != pLayout->auiTerminals[uiGroup]) && (0 == pLayout->auiSlots[uiGroup])) {
            return false;
        }
        uiTerminalEnd += pLayout->auiTerminals[uiGroup];
        pPlan->auiTerminalEnd[uiGroup] = uiTerminalEnd;
        pPlan->auiSlotStart[uiGroup] = uiSlotStart;
        pPlan->auiSlotCount[uiGroup] = pLayout->auiSlots[uiGroup];
        auiCursor[uiGroup] = uiSlotStart;
        uiSlotStart += pLayout->auiSlots[uiGroup];
    }
    if (0 != uiAssigned) {
        pPlan->pauiSlotList = malloc((size_t)uiAssigned * sizeof(*pPlan->pauiSlotList));
        if (NULL == pPlan->pauiSlotList) {
            return false;
        }
    }
    for (uint32_t uiSlot = 0; uiSlot < pLayout->uiSlots; uiSlot++) {
        uint8_t ucGroup = pLayout->paucOwner[uiSlot];
        if (GROUP_UNASSIGNED != ucGroup) {
            pPlan->pauiSlotList[auiCursor[ucGroup]++] = uiSlot;
        }
    }
    return true;
}

bool BuildGroupPlans(const SimulationConfig_t *pConfig, const GroupConfig_t *pGroups, AreaGroupPlan_t aPlans[AreaCount])
{
    memset(aPlans, 0, AreaCount * sizeof(*aPlans));
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        AreaGroupLayout_t Layout;
        bool bBuilt;

        if (!IsAreaGroupActive(pConfig, (AreaType)uiArea)) {
            continue;
        }
        if (!ResolveAreaGroups(pConfig, pGroups, (AreaType)uiArea, &Layout)) {
            FreeGroupPlans(aPlans);
            return false;
        }
        bBuilt = BuildAreaPlan(&Layout, &pGroups->aAreas[uiArea], pConfig->uiTerminalCount, &aPlans[uiArea]);
        if (bBuilt) {
            aPlans[uiArea].paucSlotGroup = Layout.paucOwner;
            Layout.paucOwner = NULL;
        }
        FreeAreaGroupLayout(&Layout);
        if (!bBuilt) {
            FreeGroupPlans(aPlans);
            return false;
        }
    }
    return true;
}

bool ConvertManualSlots(AreaGroupConfig_t *pArea, const SimulationConfig_t *pConfig, const GroupConfig_t *pGroups, AreaType Area)
{
    AreaGroupLayout_t Layout;

    if (!ResolveAreaGroups(pConfig, pGroups, Area, &Layout) || (NULL == Layout.paucOwner)) {
        FreeAreaGroupLayout(&Layout);
        return false;
    }
    /* The layout drops cells outside the grid and cells of removed groups; callers confirm this first. */
    free(pArea->paucOwner);
    pArea->paucOwner = Layout.paucOwner;
    pArea->uiGridTimeSlots = Layout.uiTimeSlots;
    pArea->uiGridSlots = Layout.uiSlots;
    pArea->SlotAssignment = SlotModeManual;
    return true;
}

void EditSlotRect(AreaGroupConfig_t *pArea, uint32_t uiTimeSlots, const SlotRect_t *pRect, uint32_t uiGroup, bool bRelease, uint64_t *pullChanged, uint64_t *pullSkipped)
{
    *pullChanged = 0;
    *pullSkipped = 0;
    if ((NULL == pArea->paucOwner) || (0 == uiTimeSlots) || (pRect->uiTimeLast >= uiTimeSlots)) {
        return;
    }
    for (uint64_t ullRow = pRect->uiRowFirst; (ullRow <= pRect->uiRowLast) && (ullRow * uiTimeSlots < pArea->uiGridSlots); ullRow++) {
        for (uint32_t uiTime = pRect->uiTimeFirst; uiTime <= pRect->uiTimeLast; uiTime++) {
            uint64_t ullSlot = ullRow * uiTimeSlots + uiTime;
            uint8_t *pucOwner;

            if (ullSlot >= pArea->uiGridSlots) {
                break;
            }
            pucOwner = &pArea->paucOwner[ullSlot];
            if (bRelease) {
                if (uiGroup == *pucOwner) {
                    *pucOwner = GROUP_UNASSIGNED;
                    (*pullChanged)++;
                }
                else if (GROUP_UNASSIGNED != *pucOwner) {
                    (*pullSkipped)++;
                }
            }
            else if (GROUP_UNASSIGNED == *pucOwner) {
                *pucOwner = (uint8_t)uiGroup;
                (*pullChanged)++;
            }
            else if (uiGroup != *pucOwner) {
                (*pullSkipped)++;
            }
        }
    }
}

void FixGroupTerminalCount(AreaGroupConfig_t *pArea, uint32_t uiGroup, uint32_t uiCount)
{
    if (uiGroup < GROUP_MAX_COUNT) {
        pArea->abFixedTerminals[uiGroup] = true;
        pArea->auiTerminals[uiGroup] = uiCount;
    }
}

void ReleaseGroupTerminalCount(AreaGroupConfig_t *pArea, uint32_t uiGroup)
{
    if (uiGroup < GROUP_MAX_COUNT) {
        pArea->abFixedTerminals[uiGroup] = false;
        pArea->auiTerminals[uiGroup] = 0;
    }
}

void ResetGroupTerminals(AreaGroupConfig_t *pArea)
{
    memset(pArea->abFixedTerminals, 0, sizeof(pArea->abFixedTerminals));
    memset(pArea->auiTerminals, 0, sizeof(pArea->auiTerminals));
}

void ResetGroupSlots(AreaGroupConfig_t *pArea)
{
    free(pArea->paucOwner);
    pArea->paucOwner = NULL;
    pArea->uiGridTimeSlots = 0;
    pArea->uiGridSlots = 0;
    pArea->SlotAssignment = SlotModeEqual;
}

void SetProportionalSlots(AreaGroupConfig_t *pArea)
{
    ResetGroupSlots(pArea);
    pArea->SlotAssignment = SlotModeProportional;
}

bool ClearGroupSlots(AreaGroupConfig_t *pArea, uint32_t uiTimeSlots, uint32_t uiSlots)
{
    uint8_t *paucOwner = malloc((0 == uiSlots) ? 1 : uiSlots);

    if (NULL == paucOwner) {
        return false;
    }
    memset(paucOwner, GROUP_UNASSIGNED, uiSlots);
    free(pArea->paucOwner);
    pArea->paucOwner = paucOwner;
    pArea->uiGridTimeSlots = uiTimeSlots;
    pArea->uiGridSlots = uiSlots;
    pArea->SlotAssignment = SlotModeManual;
    return true;
}
