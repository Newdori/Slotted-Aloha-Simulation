#ifndef SLOTTED_ALOHA_SIMULATION_H
#define SLOTTED_ALOHA_SIMULATION_H

#include <stdbool.h>
#include <stdint.h>

#define GROUP_MAX_COUNT 100
#define GROUP_UNASSIGNED UINT8_C(0xFF)

typedef enum AreaType {
    AreaAlert,
    AreaLb,
    AreaCount
} AreaType;

typedef enum SlotState {
    SlotIdle,
    SlotSuccess,
    SlotCollision
} SlotState;

typedef struct SimulationConfig {
    uint32_t uiSymbolRate;
    uint32_t uiWaveId;
    uint32_t auiSlots[AreaCount];
    uint32_t uiTerminalCount;
    uint64_t ullSuperframeMilliseconds;
    uint64_t ullDurationMilliseconds;
    uint32_t uiGroupCount;
    uint32_t uiRepeatCount;
} SimulationConfig_t;

#define REPEAT_MAX_COUNT 10000

typedef struct SlotCounts {
    uint64_t ullIdle;
    uint64_t ullSuccess;
    uint64_t ullCollision;
    uint64_t ullCollidedAttempts;
} SlotCounts_t;

typedef struct RoundAreaCounts {
    uint32_t uiSuccess;
    uint32_t uiCollision;
    uint32_t uiCollidedAttempts;
} RoundAreaCounts_t;

typedef struct RoundResult {
    RoundAreaCounts_t aAreas[AreaCount];
} RoundResult_t;

/* How the slots of an area are divided among its groups. Equal and proportional follow the current parameters. */
typedef enum SlotMode {
    SlotModeEqual,
    SlotModeProportional,
    SlotModeManual
} SlotMode;

/* Runtime slot plan of one area. Terminals are numbered from 0 and assigned to groups in order. */
typedef struct AreaGroupPlan {
    uint32_t uiGroupCount;
    uint32_t auiTerminalEnd[GROUP_MAX_COUNT];
    uint32_t auiSlotStart[GROUP_MAX_COUNT];
    uint32_t auiSlotCount[GROUP_MAX_COUNT];
    uint32_t *pauiSlotList;
    uint8_t *paucSlotGroup;
    uint32_t uiUnassignedSlots;
    uint32_t uiFixedTerminalGroups;
    bool abFixedTerminals[GROUP_MAX_COUNT];
    SlotMode SlotAssignment;
} AreaGroupPlan_t;

typedef struct TerminalSuccess {
    uint8_t *paucSeen[AreaCount];
    uint32_t auiSucceeded[AreaCount];
    uint32_t uiSucceededAnywhere;
    uint32_t aauiGroupSucceeded[AreaCount][GROUP_MAX_COUNT];
} TerminalSuccess_t;

/* Final values of one repetition. Repetition 1 is the detailed run shown on screen; the others use derived seeds. */
typedef struct RepeatSample {
    uint64_t ullSeed;
    SlotCounts_t aTotals[AreaCount];
    SlotCounts_t aBaselineTotals[AreaCount];
    uint32_t auiUnsuccessful[AreaCount];
    uint32_t auiBaselineUnsuccessful[AreaCount];
} RepeatSample_t;

typedef enum RepeatMetric {
    RepeatFailure,
    RepeatSuccess,
    RepeatCollided,
    RepeatMetricCount
} RepeatMetric;

typedef enum RepeatSeries {
    RepeatGrouped,
    RepeatBaseline,
    RepeatDifference,
    RepeatSeriesCount
} RepeatSeries;

typedef struct RepeatStatistic {
    double dMean;
    double dDeviation;
    double dMinimum;
    double dMaximum;
} RepeatStatistic_t;

typedef struct SimulationResult {
    SimulationConfig_t Config;
    uint64_t ullRoundCount;
    RoundResult_t *paRounds;
    SlotCounts_t aTotals[AreaCount];
    uint32_t auiUnsuccessful[AreaCount];
    uint32_t uiUnsuccessfulAnywhere;
    uint32_t uiGroupCount;
    AreaGroupPlan_t aPlans[AreaCount];
    RoundAreaCounts_t *paGroupRounds;
    SlotCounts_t aaGroupTotals[AreaCount][GROUP_MAX_COUNT];
    uint32_t aauiGroupUnsuccessful[AreaCount][GROUP_MAX_COUNT];
    RoundResult_t *paBaselineRounds;
    SlotCounts_t aBaselineTotals[AreaCount];
    uint32_t auiBaselineUnsuccessful[AreaCount];
    RepeatSample_t *paRepeats;
    uint32_t uiRepeatCount;
} SimulationResult_t;

typedef bool (*CheckStopFunction)(void *pvContext);
typedef void (*ReportProgressFunction)(void *pvContext, uint64_t ullCompletedRounds);

uint32_t GetTimeSlotCount(uint32_t uiSymbolRate, uint32_t uiWaveId);
uint32_t GetFrequencyRowCount(uint32_t uiSlots, uint32_t uiTimeSlots);
uint64_t GetRoundCount(const SimulationConfig_t *pConfig);
bool ValidateCounters(const SimulationConfig_t *pConfig);
bool SimulateRound(const SimulationConfig_t *pConfig, const AreaGroupPlan_t *paPlans, uint64_t ullSeed, uint64_t ullRound, uint8_t *const paucStates[AreaCount], SlotCounts_t aCounts[AreaCount], RoundAreaCounts_t *paGroupCounts, TerminalSuccess_t *pSuccess, CheckStopFunction CheckStop, void *pvContext);
bool SimulateAllRounds(SimulationResult_t *pResult, uint64_t ullSeed, uint8_t *const paucStates[AreaCount], SlotCounts_t aRoundCounts[AreaCount], TerminalSuccess_t *pSuccess, TerminalSuccess_t *pBaselineSuccess, CheckStopFunction CheckStop, ReportProgressFunction ReportProgress, void *pvContext);
void AddSlotCounts(SlotCounts_t *pTotal, const SlotCounts_t *pRound);
void SummarizeTerminalSuccess(SimulationResult_t *pResult, const TerminalSuccess_t *pSuccess, const TerminalSuccess_t *pBaselineSuccess);
RoundAreaCounts_t GetRoundAreaCounts(const SimulationResult_t *pResult, uint64_t ullIndex, AreaType Area);
RoundAreaCounts_t GetRoundGroupCounts(const SimulationResult_t *pResult, uint64_t ullIndex, AreaType Area, uint32_t uiGroup);
RoundAreaCounts_t GetBaselineRoundCounts(const SimulationResult_t *pResult, uint64_t ullIndex, AreaType Area);
bool IsGroupedResult(const SimulationResult_t *pResult);
uint32_t GetGroupTerminalCount(const AreaGroupPlan_t *pPlan, uint32_t uiGroup);
uint32_t GetGroupFirstTerminal(const AreaGroupPlan_t *pPlan, uint32_t uiGroup);
bool SumRoundsThrough(const SimulationResult_t *pResult, uint64_t ullRound, SlotCounts_t aCounts[AreaCount], CheckStopFunction CheckStop, void *pvContext);
bool SumBaselineRoundsThrough(const SimulationResult_t *pResult, uint64_t ullRound, SlotCounts_t aCounts[AreaCount], CheckStopFunction CheckStop, void *pvContext);
bool SumGroupRoundsThrough(const SimulationResult_t *pResult, uint64_t ullRound, SlotCounts_t aaCounts[AreaCount][GROUP_MAX_COUNT], CheckStopFunction CheckStop, void *pvContext);
uint64_t GetRepeatSeed(uint64_t ullSeed, uint32_t uiRepeat);
void CaptureRepeatSample(const SimulationResult_t *pResult, uint64_t ullSeed, RepeatSample_t *pSample);
bool SimulateRepeat(const SimulationResult_t *pResult, uint64_t ullSeed, uint8_t *const paucStates[AreaCount], SlotCounts_t aRoundCounts[AreaCount], TerminalSuccess_t *pSuccess, TerminalSuccess_t *pBaselineSuccess, RepeatSample_t *pSample, CheckStopFunction CheckStop, void *pvContext);
bool IsRepeatedResult(const SimulationResult_t *pResult);
double GetRepeatValue(const RepeatSample_t *pSample, AreaType Area, RepeatMetric Metric, RepeatSeries Series);
void SummarizeRepeats(const SimulationResult_t *pResult, AreaType Area, RepeatMetric Metric, RepeatSeries Series, RepeatStatistic_t *pStatistic);

#endif
