#ifndef SLOTTED_ALOHA_SIMULATION_H
#define SLOTTED_ALOHA_SIMULATION_H

#include <stdbool.h>
#include <stdint.h>

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
} SimulationConfig_t;

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

typedef struct TerminalSuccess {
    uint8_t *paucSeen[AreaCount];
    uint32_t auiSucceeded[AreaCount];
    uint32_t uiSucceededAnywhere;
} TerminalSuccess_t;

typedef struct SimulationResult {
    SimulationConfig_t Config;
    uint64_t ullRoundCount;
    RoundResult_t *paRounds;
    SlotCounts_t aTotals[AreaCount];
    uint32_t auiUnsuccessful[AreaCount];
    uint32_t uiUnsuccessfulAnywhere;
} SimulationResult_t;

typedef bool (*CheckStopFunction)(void *pvContext);

uint32_t GetTimeSlotCount(uint32_t uiSymbolRate, uint32_t uiWaveId);
uint32_t GetFrequencyRowCount(uint32_t uiSlots, uint32_t uiTimeSlots);
uint64_t GetRoundCount(const SimulationConfig_t *pConfig);
bool ValidateCounters(const SimulationConfig_t *pConfig);
bool SimulateRound(const SimulationConfig_t *pConfig, uint64_t ullSeed, uint64_t ullRound, uint8_t *const paucStates[AreaCount], SlotCounts_t aCounts[AreaCount], TerminalSuccess_t *pSuccess, CheckStopFunction CheckStop, void *pvContext);
void AddSlotCounts(SlotCounts_t *pTotal, const SlotCounts_t *pRound);
void SummarizeTerminalSuccess(SimulationResult_t *pResult, const TerminalSuccess_t *pSuccess);
RoundAreaCounts_t GetRoundAreaCounts(const SimulationResult_t *pResult, uint64_t ullIndex, AreaType Area);
bool SumRoundsThrough(const SimulationResult_t *pResult, uint64_t ullRound, SlotCounts_t aCounts[AreaCount], CheckStopFunction CheckStop, void *pvContext);

#endif
