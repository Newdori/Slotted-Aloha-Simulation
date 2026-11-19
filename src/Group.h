#ifndef SLOTTED_ALOHA_GROUP_H
#define SLOTTED_ALOHA_GROUP_H

#include <stddef.h>
#include <wchar.h>

#include "Simulation.h"

/* Editable group settings. A group whose terminal count is not fixed shares the remaining terminals equally,
   so changing one group keeps the total equal to the terminal count. Automatic slots follow the current grid. */
typedef struct AreaGroupConfig {
    SlotMode SlotAssignment;
    bool abFixedTerminals[GROUP_MAX_COUNT];
    uint32_t auiTerminals[GROUP_MAX_COUNT];
    uint32_t uiGridTimeSlots;
    uint32_t uiGridSlots;
    uint8_t *paucOwner;
} AreaGroupConfig_t;

typedef struct GroupConfig {
    AreaGroupConfig_t aAreas[AreaCount];
} GroupConfig_t;

/* Group settings resolved on the current slot grid. */
typedef struct AreaGroupLayout {
    uint32_t uiGroupCount;
    uint32_t uiTimeSlots;
    uint32_t uiSlots;
    uint32_t auiTerminals[GROUP_MAX_COUNT];
    uint32_t auiSlots[GROUP_MAX_COUNT];
    bool abFixedTerminals[GROUP_MAX_COUNT];
    uint32_t uiFixedGroups;
    uint64_t ullTerminalSum;
    uint32_t uiUnassigned;
    uint32_t uiOutside;
    uint32_t uiRemoved;
    uint8_t *paucOwner;
} AreaGroupLayout_t;

typedef enum GroupIssueType {
    GroupIssueMemory,
    GroupIssueTerminalSum,
    GroupIssueNoSlot,
    GroupIssueOutside,
    GroupIssueRemoved
} GroupIssueType;

typedef struct GroupIssue {
    AreaType Area;
    GroupIssueType Type;
    uint32_t uiFirstGroup;
    uint32_t uiLastGroup;
    uint64_t ullValue;
    uint64_t ullLimit;
} GroupIssue_t;

typedef struct SlotRect {
    uint32_t uiTimeFirst;
    uint32_t uiTimeLast;
    uint32_t uiRowFirst;
    uint32_t uiRowLast;
} SlotRect_t;

void FreeGroupConfig(GroupConfig_t *pGroups);
bool CopyGroupConfig(GroupConfig_t *pTarget, const GroupConfig_t *pSource);
void DistributeTerminals(uint32_t uiTerminals, uint32_t uiGroups, uint32_t auiCounts[GROUP_MAX_COUNT]);
void DistributeSlots(uint8_t *paucOwner, uint32_t uiSlots, uint32_t uiGroups);
void DistributeProportionalSlots(uint8_t *paucOwner, uint32_t uiSlots, uint32_t uiGroups, const uint32_t auiTerminals[GROUP_MAX_COUNT]);
const wchar_t *GetSlotModeName(SlotMode Mode);
bool IsAreaGroupActive(const SimulationConfig_t *pConfig, AreaType Area);
bool ResolveAreaGroups(const SimulationConfig_t *pConfig, const GroupConfig_t *pGroups, AreaType Area, AreaGroupLayout_t *pLayout);
void FreeAreaGroupLayout(AreaGroupLayout_t *pLayout);
uint32_t ValidateGroupConfig(const SimulationConfig_t *pConfig, const GroupConfig_t *pGroups, GroupIssue_t *paIssues, uint32_t uiCapacity);
uint32_t CheckAreaGroupLayout(const SimulationConfig_t *pConfig, const AreaGroupLayout_t *pLayout, AreaType Area, GroupIssue_t *paIssues, uint32_t uiCapacity);
uint32_t CountFixedTerminalGroups(const AreaGroupConfig_t *pArea, uint32_t uiGroupCount);
bool FormatGroupIssue(const GroupIssue_t *pIssue, wchar_t *pcText, size_t uCapacity);
bool BuildGroupPlans(const SimulationConfig_t *pConfig, const GroupConfig_t *pGroups, AreaGroupPlan_t aPlans[AreaCount]);
void FreeGroupPlans(AreaGroupPlan_t aPlans[AreaCount]);
bool ConvertManualSlots(AreaGroupConfig_t *pArea, const SimulationConfig_t *pConfig, const GroupConfig_t *pGroups, AreaType Area);
void EditSlotRect(AreaGroupConfig_t *pArea, uint32_t uiTimeSlots, const SlotRect_t *pRect, uint32_t uiGroup, bool bRelease, uint64_t *pullChanged, uint64_t *pullSkipped);
void FixGroupTerminalCount(AreaGroupConfig_t *pArea, uint32_t uiGroup, uint32_t uiCount);
void ReleaseGroupTerminalCount(AreaGroupConfig_t *pArea, uint32_t uiGroup);
void ResetGroupTerminals(AreaGroupConfig_t *pArea);
void ResetGroupSlots(AreaGroupConfig_t *pArea);
void SetProportionalSlots(AreaGroupConfig_t *pArea);
bool ClearGroupSlots(AreaGroupConfig_t *pArea, uint32_t uiTimeSlots, uint32_t uiSlots);

#endif
