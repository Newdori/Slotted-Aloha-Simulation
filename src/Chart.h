#ifndef SLOTTED_ALOHA_CHART_H
#define SLOTTED_ALOHA_CHART_H

#include <windows.h>

#include "Simulation.h"

#define WM_SLOTCHART_SELECT (WM_APP + 1)

typedef enum TrendSource {
    TrendWholeArea,
    TrendSingleGroup,
    TrendGroupCompare
} TrendSource;

/* Cell range selected by a click or drag. Rows are frequency positions inside the area; row 0 is frequency 1, drawn at the bottom. */
typedef struct SlotSelection {
    AreaType Area;
    uint32_t uiStartTime;
    uint32_t uiStartRow;
    uint32_t uiEndTime;
    uint32_t uiEndRow;
} SlotSelection_t;

typedef struct ChartView {
    int32_t iDpi;
    HFONT Font;
    HFONT BoldFont;
    uint32_t uiTimeSlots;
    uint32_t auiSlots[AreaCount];
    const uint8_t *paucStates[AreaCount];
    const SimulationResult_t *pResult;
    uint32_t uiTopRow;
    uint64_t ullSelectedRound;
    AreaType TrendArea;
    bool bShowResults;
    bool bShowTrend;
    const uint8_t *paucGroups[AreaCount];
    uint32_t uiGroupCount;
    uint32_t uiSelectedGroup;
    bool bGroupSelected;
    bool bShowGroups;
    TrendSource Trend;
    uint32_t uiTrendGroup;
    uint32_t uiTrendTopRow;
    bool bEditable;
    AreaType EditArea;
    bool bDragging;
    SlotSelection_t Drag;
} ChartView_t;

bool RegisterSlotChart(HINSTANCE Instance);
HWND CreateSlotChart(HWND Parent, HINSTANCE Instance, ChartView_t *pView);
void RefreshSlotChart(HWND Window, ChartView_t *pView);
COLORREF GetGroupColor(uint32_t uiGroup);

#endif
