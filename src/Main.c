#include "Chart.h"
#include "GroupDialog.h"
#include "Report.h"

#include <commctrl.h>
#include <commdlg.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef enum InputField {
    FieldSymbolRate,
    FieldWaveId,
    FieldAlertSlots,
    FieldLbSlots,
    FieldTerminals,
    FieldSuperframe,
    FieldDuration,
    FieldGroups,
    FieldRepeats,
    FieldCount
} InputField;

typedef enum CommandId {
    CommandFirstField = 100,
    CommandRun = 200,
    CommandPrevious,
    CommandRound,
    CommandJump,
    CommandNext,
    CommandExport,
    CommandTrend,
    CommandTrendArea,
    CommandTabs,
    CommandGroupSettings,
    CommandGroupSelect,
    CommandGroupView,
    CommandTableMode
} CommandId;

typedef enum PageType {
    PageSimulation,
    PageResult
} PageType;

typedef enum TaskOperation {
    TaskSimulation,
    TaskRound,
    TaskExport
} TaskOperation;

typedef enum TableMode {
    TableArea,
    TableGroup,
    TableCompare,
    TableRepeat
} TableMode;

typedef enum FailureColumn {
    FailureHidden,
    FailureType,
    FailureGrouped,
    FailureBaseline
} FailureColumn;

#define FAILURE_COLUMNS 4

/* Vertical positions of the lower result area. The splitter changes the group and repeat table height. */
typedef struct ResultLayout {
    int32_t iTableHeight;
    int32_t iMinimumTable;
    int32_t iMaximumTable;
    int32_t iStatistics;
    int32_t iNavigation;
    RECT Splitter;
    bool bResizable;
} ResultLayout_t;

typedef struct SimulationTask {
    SimulationResult_t Result;
    TerminalSuccess_t Success;
    TerminalSuccess_t BaselineSuccess;
    TaskOperation Operation;
    uint64_t ullSeed;
    uint64_t ullTargetRound;
    uint8_t *paucStates[AreaCount];
    SlotCounts_t aRoundCounts[AreaCount];
    SlotCounts_t aShownTotals[AreaCount];
    SlotCounts_t aaGroupShownTotals[AreaCount][GROUP_MAX_COUNT];
    SlotCounts_t aBaselineShownTotals[AreaCount];
    wchar_t acExportPath[MAX_PATH];
    volatile LONG64 llCompletedRounds;
    volatile LONG lCompletedRepeats;
    volatile LONG lStop;
} SimulationTask_t;

typedef struct Application {
    HINSTANCE Instance;
    ChartView_t View;
    ChartView_t PreviewView;
    GroupConfig_t Groups;
    AreaGroupLayout_t aPreviewLayouts[AreaCount];
    HWND Tabs;
    HWND SimulationPage;
    HWND ResultPage;
    HWND ResultContent;
    HWND EmptyResult;
    HWND PreviewChart;
    HWND ResultMetadata;
    HWND aFields[FieldCount];
    HWND aLabels[FieldCount];
    HWND SettingsPanel;
    HWND FailureTable;
    HWND StatisticsTable;
    HWND GroupTable;
    HWND CompareTable;
    HWND RepeatTable;
    HWND StatisticsTitle;
    HWND TableModeSelect;
    HWND FailureNote;
    HWND Chart;
    HWND RunButton;
    HWND GroupButton;
    HWND GroupSummary;
    HWND Status;
    HWND PreviousButton;
    HWND RoundEdit;
    HWND JumpButton;
    HWND NextButton;
    HWND Reception;
    HWND ExportButton;
    HWND TrendButton;
    HWND TrendAreaSelect;
    HWND GroupSelect;
    HWND GroupViewButton;
    HWND TerminalSummary;
    HFONT TableFont;
    HFONT TableBoldFont;
    HANDLE Worker;
    SimulationTask_t *pTask;
    uint64_t ullShownRound;
    TableMode Table;
    int32_t iGroupTableHeight;
    int32_t iSplitterGrab;
    wchar_t acGroupIssue[256];
    uint32_t uiGroupIssues;
    bool bGroupError;
    bool bSplitterDragging;
    bool bReady;
    bool bClosing;
} Application_t;

static const wchar_t gacTitle[] = L"Slotted Aloha 채널 경쟁 상황 시뮬레이션";
static const wchar_t gacWindowClass[] = L"SlottedAlohaSimulator";
static const wchar_t gacPageClass[] = L"SlottedAlohaPage";
static const wchar_t *const gpacAreaLabels[AreaCount] = { L"Alert", L"LB" };
static const wchar_t gacFailureTitle[] = L"최종 접속 실패 단말 갯수";
static const COLORREF guiLabelColor = RGB(96, 108, 122);
static const COLORREF guiValueColor = RGB(31, 45, 61);
static const uint32_t gauiSymbolRates[] = { 500, 2000, 4000 };
static const uint32_t gauiWaveIds[] = { 3, 13 };

static int32_t ScaleLayout(const Application_t *pApp, int32_t iValue)
{
    return MulDiv(iValue, pApp->View.iDpi, 96);
}

static void LayoutResultControls(const Application_t *pApp);
static bool HandleSplitterMessage(Application_t *pApp, HWND Window, UINT uiMessage, LPARAM MessageData);

static void SetFormattedText(HWND Control, const wchar_t *pcFormat, ...)
{
    wchar_t acText[512];
    va_list Arguments;
    int32_t iWritten;
    size_t uCapacity = sizeof(acText) / sizeof(acText[0]);

    va_start(Arguments, pcFormat);
    iWritten = vswprintf(acText, uCapacity, pcFormat, Arguments);
    va_end(Arguments);
    if ((iWritten >= 0) && ((size_t)iWritten < uCapacity)) {
        SetWindowTextW(Control, acText);
    }
}

static void SetTableText(HWND Table, int32_t iRow, int32_t iColumn, const wchar_t *pcFormat, ...)
{
    wchar_t acText[128];
    size_t uCapacity = sizeof(acText) / sizeof(acText[0]);
    va_list Arguments;
    int32_t iWritten;

    va_start(Arguments, pcFormat);
    iWritten = vswprintf(acText, uCapacity, pcFormat, Arguments);
    va_end(Arguments);
    if ((iWritten >= 0) && ((size_t)iWritten < uCapacity)) {
        ListView_SetItemText(Table, iRow, iColumn, acText);
    }
}

static bool ParseDigits(const wchar_t **ppcText, uint64_t ullLimit, uint64_t *pullValue)
{
    const wchar_t *pcDigit = *ppcText;
    uint64_t ullValue = 0;

    while ((*pcDigit >= L'0') && (*pcDigit <= L'9')) {
        uint32_t uiDigit = (uint32_t)(*pcDigit - L'0');
        if ((uiDigit > ullLimit) || (ullValue > (ullLimit - uiDigit) / 10)) {
            return false;
        }
        ullValue = ullValue * 10 + uiDigit;
        pcDigit++;
    }
    if (pcDigit == *ppcText) {
        return false;
    }
    *pullValue = ullValue;
    *ppcText = pcDigit;
    return true;
}

static bool ReadInteger(HWND Control, uint64_t ullMaximum, uint64_t *pullValue)
{
    wchar_t acText[32] = { 0 };
    const wchar_t *pcText = acText;

    GetWindowTextW(Control, acText, (int32_t)(sizeof(acText) / sizeof(acText[0])));
    return ParseDigits(&pcText, ullMaximum, pullValue) && (L'\0' == *pcText);
}

static bool ReadMilliseconds(HWND Control, uint64_t *pullMilliseconds)
{
    wchar_t acText[32] = { 0 };
    const wchar_t *pcText = acText;
    uint64_t ullSeconds;
    uint32_t uiFraction = 0;

    GetWindowTextW(Control, acText, (int32_t)(sizeof(acText) / sizeof(acText[0])));
    if (!ParseDigits(&pcText, UINT64_MAX / 1000, &ullSeconds)) {
        return false;
    }
    if (L'.' == *pcText) {
        uint32_t uiPlace = 100;
        pcText++;
        if ((*pcText < L'0') || (*pcText > L'9')) {
            return false;
        }
        while ((uiPlace > 0) && (*pcText >= L'0') && (*pcText <= L'9')) {
            uiFraction += (uint32_t)(*pcText - L'0') * uiPlace;
            uiPlace /= 10;
            pcText++;
        }
    }
    if ((L'\0' != *pcText) || (ullSeconds > (UINT64_MAX - uiFraction) / 1000)) {
        return false;
    }
    *pullMilliseconds = ullSeconds * 1000 + uiFraction;
    return true;
}

static bool ReadChoice(HWND Control, const uint32_t *pauiValues, size_t uValueCount, uint32_t *puiValue)
{
    LRESULT Selection = SendMessageW(Control, CB_GETCURSEL, 0, 0);

    if ((Selection < 0) || ((size_t)Selection >= uValueCount)) {
        return false;
    }
    *puiValue = pauiValues[Selection];
    return true;
}

static bool ReadWaveSettings(const Application_t *pApp, uint32_t *puiSymbolRate, uint32_t *puiWaveId)
{
    return ReadChoice(pApp->aFields[FieldSymbolRate], gauiSymbolRates, sizeof(gauiSymbolRates) / sizeof(gauiSymbolRates[0]), puiSymbolRate)
        && ReadChoice(pApp->aFields[FieldWaveId], gauiWaveIds, sizeof(gauiWaveIds) / sizeof(gauiWaveIds[0]), puiWaveId);
}

/* Reads only the inputs that decide the slot grid and the group layout. */
static bool ReadGridConfig(const Application_t *pApp, SimulationConfig_t *pConfig)
{
    uint64_t aullValues[4];
    const InputField aFields[] = { FieldAlertSlots, FieldLbSlots, FieldTerminals, FieldGroups };
    const uint64_t aullLimits[] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, GROUP_MAX_COUNT };

    *pConfig = (SimulationConfig_t){ 0 };
    if (!ReadWaveSettings(pApp, &pConfig->uiSymbolRate, &pConfig->uiWaveId)) {
        return false;
    }
    for (uint32_t uiIndex = 0; uiIndex < 4; uiIndex++) {
        if (!ReadInteger(pApp->aFields[aFields[uiIndex]], aullLimits[uiIndex], &aullValues[uiIndex])) {
            return false;
        }
    }
    pConfig->auiSlots[AreaAlert] = (uint32_t)aullValues[0];
    pConfig->auiSlots[AreaLb] = (uint32_t)aullValues[1];
    pConfig->uiTerminalCount = (uint32_t)aullValues[2];
    pConfig->uiGroupCount = (uint32_t)aullValues[3];
    return true;
}

static bool RejectInput(const Application_t *pApp, InputField Field, const wchar_t *pcReason)
{
    SetWindowTextW(pApp->Status, pcReason);
    SetFocus(pApp->aFields[Field]);
    return false;
}

static bool ReadConfig(const Application_t *pApp, SimulationConfig_t *pConfig)
{
    const InputField aCountFields[] = { FieldAlertSlots, FieldLbSlots, FieldTerminals };
    uint32_t *const pauiCounts[] = { &pConfig->auiSlots[AreaAlert], &pConfig->auiSlots[AreaLb], &pConfig->uiTerminalCount };
    uint64_t ullGroups;
    uint64_t ullRepeats;

    if (!ReadWaveSettings(pApp, &pConfig->uiSymbolRate, &pConfig->uiWaveId)) {
        return RejectInput(pApp, FieldSymbolRate, L"심볼레이트와 Wave ID를 선택하세요.");
    }
    for (uint32_t uiIndex = 0; uiIndex < sizeof(aCountFields) / sizeof(aCountFields[0]); uiIndex++) {
        uint64_t ullValue;
        if (!ReadInteger(pApp->aFields[aCountFields[uiIndex]], UINT32_MAX, &ullValue)) {
            return RejectInput(pApp, aCountFields[uiIndex], L"채널 수와 단말 수에는 0 이상 4,294,967,295 이하의 정수를 입력하세요.");
        }
        *pauiCounts[uiIndex] = (uint32_t)ullValue;
    }
    if (!ReadMilliseconds(pApp->aFields[FieldSuperframe], &pConfig->ullSuperframeMilliseconds) || (0 == pConfig->ullSuperframeMilliseconds)) {
        return RejectInput(pApp, FieldSuperframe, L"슈퍼프레임 주기에 0보다 큰 초 값을 입력하세요. 소수 셋째 자리까지 입력할 수 있습니다.");
    }
    if (!ReadMilliseconds(pApp->aFields[FieldDuration], &pConfig->ullDurationMilliseconds)) {
        return RejectInput(pApp, FieldDuration, L"시뮬레이션 시간에 0 이상의 초 값을 입력하세요. 소수 셋째 자리까지 입력할 수 있습니다.");
    }
    if (!ReadInteger(pApp->aFields[FieldGroups], GROUP_MAX_COUNT, &ullGroups)) {
        return RejectInput(pApp, FieldGroups, L"그룹 수에는 0부터 100 사이의 정수를 입력하세요. 0이면 그룹을 사용하지 않습니다.");
    }
    pConfig->uiGroupCount = (uint32_t)ullGroups;
    if (!ReadInteger(pApp->aFields[FieldRepeats], REPEAT_MAX_COUNT, &ullRepeats) || (0 == ullRepeats)) {
        return RejectInput(pApp, FieldRepeats, L"반복 횟수에는 1부터 10000 사이의 정수를 입력하세요. 1이면 한 번만 실행합니다.");
    }
    pConfig->uiRepeatCount = (uint32_t)ullRepeats;
    if (!ValidateCounters(pConfig)) {
        return RejectInput(pApp, FieldDuration, L"누적 집계 범위를 초과했습니다. 시뮬레이션 시간이나 채널·단말 수를 줄이세요.");
    }
    return true;
}

static void FreeTerminalSuccess(TerminalSuccess_t *pSuccess)
{
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        free(pSuccess->paucSeen[uiArea]);
        pSuccess->paucSeen[uiArea] = NULL;
    }
}

static void FreeTask(SimulationTask_t *pTask)
{
    if (NULL != pTask) {
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            free(pTask->paucStates[uiArea]);
        }
        FreeTerminalSuccess(&pTask->Success);
        FreeTerminalSuccess(&pTask->BaselineSuccess);
        FreeGroupPlans(pTask->Result.aPlans);
        free(pTask->Result.paRounds);
        free(pTask->Result.paGroupRounds);
        free(pTask->Result.paBaselineRounds);
        free(pTask->Result.paRepeats);
        free(pTask);
    }
}

static bool AllocateRoundTables(SimulationTask_t *pTask, uint64_t ullRounds)
{
    SimulationResult_t *pResult = &pTask->Result;
    uint64_t ullGroupEntries = (uint64_t)AreaCount * pResult->uiGroupCount;

    if (ullRounds > SIZE_MAX / sizeof(*pResult->paRounds)) {
        return false;
    }
    pResult->paRounds = malloc((size_t)ullRounds * sizeof(*pResult->paRounds));
    if (NULL == pResult->paRounds) {
        return false;
    }
    if (!IsGroupedResult(pResult)) {
        return true;
    }
    if (ullRounds > SIZE_MAX / sizeof(RoundAreaCounts_t) / ullGroupEntries) {
        return false;
    }
    pResult->paBaselineRounds = malloc((size_t)ullRounds * sizeof(*pResult->paBaselineRounds));
    pResult->paGroupRounds = malloc((size_t)(ullRounds * ullGroupEntries) * sizeof(*pResult->paGroupRounds));
    return (NULL != pResult->paBaselineRounds) && (NULL != pResult->paGroupRounds);
}

static SimulationTask_t *CreateTask(const SimulationConfig_t *pConfig, const GroupConfig_t *pGroups)
{
    SimulationTask_t *pTask = calloc(1, sizeof(*pTask));
    LARGE_INTEGER Counter = { 0 };
    FILETIME Time;
    uint64_t ullRounds = GetRoundCount(pConfig);
    bool bTrack = (0 != pConfig->uiTerminalCount) && ((0 != pConfig->auiSlots[AreaAlert]) || (0 != pConfig->auiSlots[AreaLb]));

    if (NULL == pTask) {
        return NULL;
    }
    pTask->Result.Config = *pConfig;
    pTask->Result.ullRoundCount = ullRounds;
    if (!BuildGroupPlans(pConfig, pGroups, pTask->Result.aPlans)) {
        FreeTask(pTask);
        return NULL;
    }
    if ((0 != pTask->Result.aPlans[AreaAlert].uiGroupCount) || (0 != pTask->Result.aPlans[AreaLb].uiGroupCount)) {
        pTask->Result.uiGroupCount = pConfig->uiGroupCount;
    }
    if (pConfig->uiRepeatCount > 1) {
        pTask->Result.paRepeats = calloc(pConfig->uiRepeatCount, sizeof(*pTask->Result.paRepeats));
        if (NULL == pTask->Result.paRepeats) {
            FreeTask(pTask);
            return NULL;
        }
    }
    QueryPerformanceCounter(&Counter);
    GetSystemTimeAsFileTime(&Time);
    pTask->ullSeed = (uint64_t)Counter.QuadPart ^ ((uint64_t)Time.dwHighDateTime << 32) ^ Time.dwLowDateTime;

    if (0 != ullRounds) {
        if (bTrack && !AllocateRoundTables(pTask, ullRounds)) {
            FreeTask(pTask);
            return NULL;
        }
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            uint32_t uiSlots = pConfig->auiSlots[uiArea];
            size_t uBytes = (size_t)(((uint64_t)pConfig->uiTerminalCount + 7) / 8);

            if (0 == uiSlots) {
                continue;
            }
            pTask->paucStates[uiArea] = malloc(uiSlots);
            if (NULL == pTask->paucStates[uiArea]) {
                FreeTask(pTask);
                return NULL;
            }
            if (bTrack) {
                pTask->Success.paucSeen[uiArea] = calloc(uBytes, 1);
                if (NULL == pTask->Success.paucSeen[uiArea]) {
                    FreeTask(pTask);
                    return NULL;
                }
            }
            if (bTrack && IsGroupedResult(&pTask->Result)) {
                pTask->BaselineSuccess.paucSeen[uiArea] = calloc(uBytes, 1);
                if (NULL == pTask->BaselineSuccess.paucSeen[uiArea]) {
                    FreeTask(pTask);
                    return NULL;
                }
            }
        }
    }
    return pTask;
}

static bool CheckWorkerStop(void *pvContext)
{
    SimulationTask_t *pTask = pvContext;
    return 0 != InterlockedCompareExchange(&pTask->lStop, 0, 0);
}

static void ReportWorkerProgress(void *pvContext, uint64_t ullCompletedRounds)
{
    SimulationTask_t *pTask = pvContext;
    InterlockedExchange64(&pTask->llCompletedRounds, (LONG64)ullCompletedRounds);
}

static bool SimulateTaskRound(SimulationTask_t *pTask, uint64_t ullRound)
{
    const AreaGroupPlan_t *paPlans = IsGroupedResult(&pTask->Result) ? pTask->Result.aPlans : NULL;
    return !CheckWorkerStop(pTask) && SimulateRound(&pTask->Result.Config, paPlans, pTask->ullSeed, ullRound, pTask->paucStates, pTask->aRoundCounts, NULL, NULL, CheckWorkerStop, pTask);
}

/* Repetition 1 is the detailed run. The other repetitions keep only their final values; afterwards the last round
   of repetition 1 is simulated again so that the slot grid and the round table show the detailed run. */
static bool RunRepeats(SimulationTask_t *pTask)
{
    SimulationResult_t *pResult = &pTask->Result;
    uint32_t uiRepeats = pResult->Config.uiRepeatCount;

    if ((uiRepeats <= 1) || (NULL == pResult->paRepeats)) {
        return true;
    }
    CaptureRepeatSample(pResult, pTask->ullSeed, &pResult->paRepeats[0]);
    pResult->uiRepeatCount = 1;
    InterlockedExchange(&pTask->lCompletedRepeats, 1);
    for (uint32_t uiRepeat = 1; uiRepeat < uiRepeats; uiRepeat++) {
        if (CheckWorkerStop(pTask) || !SimulateRepeat(pResult, GetRepeatSeed(pTask->ullSeed, uiRepeat), pTask->paucStates, pTask->aRoundCounts,
            &pTask->Success, &pTask->BaselineSuccess, &pResult->paRepeats[uiRepeat], CheckWorkerStop, pTask)) {
            return false;
        }
        pResult->uiRepeatCount = uiRepeat + 1;
        InterlockedExchange(&pTask->lCompletedRepeats, (LONG)(uiRepeat + 1));
    }
    return (0 == pResult->ullRoundCount) || SimulateTaskRound(pTask, pResult->ullRoundCount);
}

static DWORD WINAPI RunWorker(void *pvContext)
{
    SimulationTask_t *pTask = pvContext;
    SimulationResult_t *pResult = &pTask->Result;

    if (TaskRound == pTask->Operation) {
        return (SimulateTaskRound(pTask, pTask->ullTargetRound) &&
            SumRoundsThrough(pResult, pTask->ullTargetRound, pTask->aShownTotals, CheckWorkerStop, pTask) &&
            SumGroupRoundsThrough(pResult, pTask->ullTargetRound, pTask->aaGroupShownTotals, CheckWorkerStop, pTask) &&
            SumBaselineRoundsThrough(pResult, pTask->ullTargetRound, pTask->aBaselineShownTotals, CheckWorkerStop, pTask)) ? 0 : 1;
    }
    if (TaskExport == pTask->Operation) {
        if (ExportResults(pTask->acExportPath, pResult, CheckWorkerStop, pTask)) {
            return 0;
        }
        return CheckWorkerStop(pTask) ? 1 : 2;
    }
    if (CheckWorkerStop(pTask) ||
        !SimulateAllRounds(pResult, pTask->ullSeed, pTask->paucStates, pTask->aRoundCounts, &pTask->Success, &pTask->BaselineSuccess,
            CheckWorkerStop, ReportWorkerProgress, pTask)) {
        return 1;
    }
    memcpy(pTask->aShownTotals, pResult->aTotals, sizeof(pTask->aShownTotals));
    memcpy(pTask->aaGroupShownTotals, pResult->aaGroupTotals, sizeof(pTask->aaGroupShownTotals));
    memcpy(pTask->aBaselineShownTotals, pResult->aBaselineTotals, sizeof(pTask->aBaselineShownTotals));
    return RunRepeats(pTask) ? 0 : 1;
}

static void ShowActivePage(const Application_t *pApp)
{
    bool bResult = PageResult == TabCtrl_GetCurSel(pApp->Tabs);
    HWND Showing = bResult ? pApp->ResultPage : pApp->SimulationPage;
    HWND Hiding = bResult ? pApp->SimulationPage : pApp->ResultPage;

    if (FALSE != IsChild(Hiding, GetFocus())) {
        SetFocus(pApp->Tabs);
    }
    ShowWindow(Hiding, SW_HIDE);
    SetWindowPos(Showing, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void SelectPage(const Application_t *pApp, PageType Page)
{
    TabCtrl_SetCurSel(pApp->Tabs, Page);
    ShowActivePage(pApp);
}

static bool IsGroupedView(const Application_t *pApp)
{
    return (NULL != pApp->View.pResult) && IsGroupedResult(pApp->View.pResult);
}

static bool IsRepeatedView(const Application_t *pApp)
{
    return (NULL != pApp->View.pResult) && IsRepeatedResult(pApp->View.pResult);
}

/* The table selector appears when there is more than one table to choose from. */
static bool HasTableModes(const Application_t *pApp)
{
    return IsGroupedView(pApp) || IsRepeatedView(pApp);
}

static void UpdateControls(const Application_t *pApp)
{
    bool bBusy = NULL != pApp->Worker;
    bool bSimulating = bBusy && (TaskSimulation == pApp->pTask->Operation);
    bool bExporting = bBusy && (TaskExport == pApp->pTask->Operation);
    bool bCanStop = bBusy && !pApp->bClosing && !CheckWorkerStop(pApp->pTask);
    bool bHasResults = NULL != pApp->View.pResult;
    bool bGrouped = IsGroupedView(pApp);
    bool bRounds = 0 != pApp->ullShownRound;
    bool bRoundControls = bRounds;
    bool bNavigate = bRoundControls && !bBusy;
    const HWND aRoundControls[] = { pApp->PreviousButton, pApp->RoundEdit, pApp->JumpButton, pApp->NextButton, pApp->Reception };

    for (uint32_t uiField = 0; uiField < FieldCount; uiField++) {
        EnableWindow(pApp->aFields[uiField], !bBusy);
    }
    EnableWindow(pApp->GroupButton, !bBusy);
    SetWindowTextW(pApp->RunButton, bSimulating ? L"중지" : L"시뮬레이션 실행");
    EnableWindow(pApp->RunButton, !bBusy || (bSimulating && bCanStop));
    SetWindowTextW(pApp->ExportButton, bExporting ? L"저장 중지" : L"결과 파일 생성");
    EnableWindow(pApp->ExportButton, (!bBusy && bHasResults) || (bExporting && bCanStop));
    EnableWindow(pApp->PreviousButton, bNavigate && (pApp->ullShownRound > 1));
    EnableWindow(pApp->NextButton, bNavigate && (pApp->ullShownRound < pApp->pTask->Result.ullRoundCount));
    EnableWindow(pApp->RoundEdit, bNavigate);
    EnableWindow(pApp->JumpButton, bNavigate);
    EnableWindow(pApp->TrendButton, bRounds && !bBusy);
    EnableWindow(pApp->TrendAreaSelect, bRounds && pApp->View.bShowTrend && !bBusy);
    ShowWindow(pApp->TrendAreaSelect, pApp->View.bShowTrend ? SW_SHOW : SW_HIDE);
    SetWindowTextW(pApp->TrendButton, pApp->View.bShowTrend ? L"슬롯 보기" : L"전송 추이");
    ShowWindow(pApp->GroupSelect, (bGrouped && bRounds) ? SW_SHOW : SW_HIDE);
    EnableWindow(pApp->GroupSelect, !bBusy);
    ShowWindow(pApp->GroupViewButton, (bGrouped && bRounds) ? SW_SHOW : SW_HIDE);
    EnableWindow(pApp->GroupViewButton, !bBusy && !pApp->View.bShowTrend);
    SetWindowTextW(pApp->GroupViewButton, pApp->View.bShowGroups ? L"전송 결과 보기" : L"그룹 배정 보기");
    ShowWindow(pApp->TableModeSelect, HasTableModes(pApp) ? SW_SHOW : SW_HIDE);
    EnableWindow(pApp->TableModeSelect, !bBusy);
    ShowWindow(pApp->StatisticsTable, (TableArea == pApp->Table) ? SW_SHOW : SW_HIDE);
    ShowWindow(pApp->GroupTable, (bGrouped && (TableGroup == pApp->Table)) ? SW_SHOW : SW_HIDE);
    ShowWindow(pApp->CompareTable, (bGrouped && (TableCompare == pApp->Table)) ? SW_SHOW : SW_HIDE);
    ShowWindow(pApp->RepeatTable, (IsRepeatedView(pApp) && (TableRepeat == pApp->Table)) ? SW_SHOW : SW_HIDE);

    for (uint32_t uiIndex = 0; uiIndex < sizeof(aRoundControls) / sizeof(aRoundControls[0]); uiIndex++) {
        ShowWindow(aRoundControls[uiIndex], bRoundControls ? SW_SHOW : SW_HIDE);
    }
    ShowWindow(pApp->Chart, bRounds ? SW_SHOW : SW_HIDE);
    SetWindowTextW(pApp->EmptyResult, bSimulating ? L"시뮬레이션 결과를 집계하고 있습니다." : L"시뮬레이션을 실행하면 여기에 결과가 표시됩니다.");
    ShowWindow(pApp->EmptyResult, bHasResults ? SW_HIDE : SW_SHOW);
    ShowWindow(pApp->ResultContent, bHasResults ? SW_SHOW : SW_HIDE);
    LayoutResultControls(pApp);
}

static void ClearResults(Application_t *pApp)
{
    pApp->View.bShowResults = false;
    pApp->View.bShowTrend = false;
    pApp->View.bShowGroups = false;
    pApp->View.bGroupSelected = false;
    pApp->View.Trend = TrendWholeArea;
    pApp->View.uiGroupCount = 0;
    pApp->View.uiTrendTopRow = 0;
    pApp->View.pResult = NULL;
    pApp->ullShownRound = 0;
    pApp->Table = TableArea;
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        pApp->View.paucStates[uiArea] = NULL;
        pApp->View.paucGroups[uiArea] = NULL;
    }
    FreeTask(pApp->pTask);
    pApp->pTask = NULL;
    ListView_DeleteAllItems(pApp->GroupTable);
    SendMessageW(pApp->GroupSelect, CB_RESETCONTENT, 0, 0);
    SetWindowTextW(pApp->RoundEdit, L"");
    SetWindowTextW(pApp->Reception, L"");
    SetWindowTextW(pApp->Chart, L"주파수 및 시간 슬롯");
}

static size_t AppendText(wchar_t *pcText, size_t uCapacity, size_t uLength, const wchar_t *pcFormat, ...)
{
    va_list Arguments;
    int32_t iWritten;

    if (uLength >= uCapacity) {
        return uLength;
    }
    va_start(Arguments, pcFormat);
    iWritten = vswprintf(&pcText[uLength], uCapacity - uLength, pcFormat, Arguments);
    va_end(Arguments);
    if ((iWritten < 0) || ((size_t)iWritten >= uCapacity - uLength)) {
        pcText[uLength] = L'\0';
        return uLength;
    }
    return uLength + (size_t)iWritten;
}

static void UpdateGroupSummary(Application_t *pApp)
{
    SimulationConfig_t Config;
    GroupIssue_t Issue;
    wchar_t acText[512] = L"";
    size_t uLength = 0;
    uint32_t uiIssues;

    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        FreeAreaGroupLayout(&pApp->aPreviewLayouts[uiArea]);
        pApp->PreviewView.paucGroups[uiArea] = NULL;
    }
    pApp->bGroupError = false;
    pApp->uiGroupIssues = 0;
    pApp->acGroupIssue[0] = L'\0';
    pApp->PreviewView.uiGroupCount = 0;
    if (!ReadGridConfig(pApp, &Config)) {
        SetWindowTextW(pApp->GroupSummary, L"채널 수·단말 수·그룹 수(0~100)를 확인하면 그룹 배정 요약이 표시됩니다.");
        InvalidateRect(pApp->GroupSummary, NULL, TRUE);
        return;
    }
    if (0 == Config.uiGroupCount) {
        SetWindowTextW(pApp->GroupSummary, L"그룹 미적용 · 모든 단말이 채널 타입의 모든 칸에서 경쟁합니다.\r\n그룹 수(1~100)를 입력하면 단말과 슬롯을 균등 배정합니다.");
        InvalidateRect(pApp->GroupSummary, NULL, TRUE);
        return;
    }
    uiIssues = ValidateGroupConfig(&Config, &pApp->Groups, &Issue, 1);
    pApp->bGroupError = 0 != uiIssues;
    pApp->PreviewView.uiGroupCount = Config.uiGroupCount;
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        const AreaGroupConfig_t *pArea = &pApp->Groups.aAreas[uiArea];
        AreaGroupLayout_t *pLayout = &pApp->aPreviewLayouts[uiArea];

        if (0 != uLength) {
            uLength = AppendText(acText, sizeof(acText) / sizeof(acText[0]), uLength, L"\r\n");
        }
        if (!IsAreaGroupActive(&Config, (AreaType)uiArea) || !ResolveAreaGroups(&Config, &pApp->Groups, (AreaType)uiArea, pLayout) || (NULL == pLayout->paucOwner)) {
            uLength = AppendText(acText, sizeof(acText) / sizeof(acText[0]), uLength, L"%ls: 채널 0 · 경쟁 없음", gpacAreaLabels[uiArea]);
            continue;
        }
        pApp->PreviewView.paucGroups[uiArea] = pLayout->paucOwner;
        if (0 == pLayout->uiFixedGroups) {
            uLength = AppendText(acText, sizeof(acText) / sizeof(acText[0]), uLength, L"%ls: 단말 균등", gpacAreaLabels[uiArea]);
        }
        else {
            uLength = AppendText(acText, sizeof(acText) / sizeof(acText[0]), uLength, L"%ls: 단말 직접 %u개 그룹", gpacAreaLabels[uiArea], pLayout->uiFixedGroups);
        }
        uLength = AppendText(acText, sizeof(acText) / sizeof(acText[0]), uLength, L" · 슬롯 %ls · 배정 %u칸 · 미배정 %u칸",
            GetSlotModeName(pArea->SlotAssignment), pLayout->uiSlots - pLayout->uiUnassigned, pLayout->uiUnassigned);
    }
    if (pApp->bGroupError && FormatGroupIssue(&Issue, pApp->acGroupIssue, sizeof(pApp->acGroupIssue) / sizeof(pApp->acGroupIssue[0]))) {
        pApp->uiGroupIssues = uiIssues;
    }
    SetWindowTextW(pApp->GroupSummary, acText);
    InvalidateRect(pApp->GroupSummary, NULL, TRUE);
}

/* The status line shows the first group issue at full width so that the summary keeps two lines. */
static void ShowGridStatus(const Application_t *pApp)
{
    if (!pApp->bGroupError) {
        SetFormattedText(pApp->Status, L"시간 슬롯 %u개", pApp->PreviewView.uiTimeSlots);
    }
    else if (pApp->uiGroupIssues > 1) {
        SetFormattedText(pApp->Status, L"⚠ %ls  (외 %u건)", pApp->acGroupIssue, pApp->uiGroupIssues - 1);
    }
    else {
        SetFormattedText(pApp->Status, L"⚠ %ls", pApp->acGroupIssue);
    }
}

static void UpdatePreview(Application_t *pApp)
{
    uint32_t uiRate;
    uint32_t uiWave;
    uint64_t ullSlots;

    if (ReadWaveSettings(pApp, &uiRate, &uiWave)) {
        pApp->PreviewView.uiTimeSlots = GetTimeSlotCount(uiRate, uiWave);
    }
    if (ReadInteger(pApp->aFields[FieldAlertSlots], UINT32_MAX, &ullSlots)) {
        pApp->PreviewView.auiSlots[AreaAlert] = (uint32_t)ullSlots;
    }
    if (ReadInteger(pApp->aFields[FieldLbSlots], UINT32_MAX, &ullSlots)) {
        pApp->PreviewView.auiSlots[AreaLb] = (uint32_t)ullSlots;
    }
    UpdateGroupSummary(pApp);
    pApp->PreviewView.uiTopRow = 0;
    ShowGridStatus(pApp);
    RefreshSlotChart(pApp->PreviewChart, &pApp->PreviewView);
}

static void WriteDifference(HWND Table, int32_t iRow, int32_t iColumn, uint64_t ullGrouped, uint64_t ullBaseline)
{
    if (ullGrouped >= ullBaseline) {
        SetTableText(Table, iRow, iColumn, (ullGrouped == ullBaseline) ? L"0" : L"+%llu", ullGrouped - ullBaseline);
    }
    else {
        SetTableText(Table, iRow, iColumn, L"-%llu", ullBaseline - ullGrouped);
    }
}

static void ShowGroupStatistics(const Application_t *pApp)
{
    const SimulationTask_t *pTask = pApp->pTask;
    const SimulationResult_t *pResult = &pTask->Result;
    int32_t iRow = 0;

    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        const AreaGroupPlan_t *pPlan = &pResult->aPlans[uiArea];
        uint64_t aullRound[4] = { 0 };
        uint64_t aullTotal[2] = { 0 };
        uint64_t ullUnsuccessful = 0;

        if (0 == pPlan->uiGroupCount) {
            continue;
        }
        for (uint32_t uiGroup = 0; uiGroup < pPlan->uiGroupCount; uiGroup++) {
            RoundAreaCounts_t Round = (0 == pApp->ullShownRound) ? (RoundAreaCounts_t){ 0 }
                : GetRoundGroupCounts(pResult, pApp->ullShownRound - 1, (AreaType)uiArea, uiGroup);
            const SlotCounts_t *pTotal = &pTask->aaGroupShownTotals[uiArea][uiGroup];
            uint32_t uiIdle = pPlan->auiSlotCount[uiGroup] - Round.uiSuccess - Round.uiCollision;

            SetTableText(pApp->GroupTable, iRow, 4, L"%u", Round.uiSuccess);
            SetTableText(pApp->GroupTable, iRow, 5, L"%u", Round.uiCollidedAttempts);
            SetTableText(pApp->GroupTable, iRow, 6, L"%u", Round.uiCollision);
            SetTableText(pApp->GroupTable, iRow, 7, L"%u", uiIdle);
            SetTableText(pApp->GroupTable, iRow, 8, L"%llu", pTotal->ullSuccess);
            SetTableText(pApp->GroupTable, iRow, 9, L"%llu", pTotal->ullCollidedAttempts);
            SetTableText(pApp->GroupTable, iRow, 10, L"%u 대", pResult->aauiGroupUnsuccessful[uiArea][uiGroup]);
            aullRound[0] += Round.uiSuccess;
            aullRound[1] += Round.uiCollidedAttempts;
            aullRound[2] += Round.uiCollision;
            aullRound[3] += uiIdle;
            aullTotal[0] += pTotal->ullSuccess;
            aullTotal[1] += pTotal->ullCollidedAttempts;
            ullUnsuccessful += pResult->aauiGroupUnsuccessful[uiArea][uiGroup];
            iRow++;
        }
        /* The total row adds only the groups; unassigned cells are always idle and are left out. */
        for (int32_t iColumn = 0; iColumn < 4; iColumn++) {
            SetTableText(pApp->GroupTable, iRow, 4 + iColumn, L"%llu", aullRound[iColumn]);
        }
        SetTableText(pApp->GroupTable, iRow, 8, L"%llu", aullTotal[0]);
        SetTableText(pApp->GroupTable, iRow, 9, L"%llu", aullTotal[1]);
        SetTableText(pApp->GroupTable, iRow, 10, L"%llu 대", ullUnsuccessful);
        iRow++;
    }
}

static void ShowCompareStatistics(const Application_t *pApp)
{
    const SimulationTask_t *pTask = pApp->pTask;
    const SimulationResult_t *pResult = &pTask->Result;

    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        uint64_t aaullValues[3][2] = {
            { pTask->aShownTotals[uiArea].ullSuccess, pTask->aBaselineShownTotals[uiArea].ullSuccess },
            { pTask->aShownTotals[uiArea].ullCollidedAttempts, pTask->aBaselineShownTotals[uiArea].ullCollidedAttempts },
            { pResult->auiUnsuccessful[uiArea], pResult->auiBaselineUnsuccessful[uiArea] }
        };
        int32_t iColumn = 1 + 3 * (int32_t)uiArea;

        for (int32_t iRow = 0; iRow < 3; iRow++) {
            const wchar_t *pcFormat = (2 == iRow) ? L"%llu 대" : L"%llu";
            SetTableText(pApp->CompareTable, iRow, iColumn, pcFormat, aaullValues[iRow][0]);
            SetTableText(pApp->CompareTable, iRow, iColumn + 1, pcFormat, aaullValues[iRow][1]);
            WriteDifference(pApp->CompareTable, iRow, iColumn + 2, aaullValues[iRow][0], aaullValues[iRow][1]);
        }
    }
}

static void ShowAreaStatistics(const Application_t *pApp)
{
    const SimulationTask_t *pTask = pApp->pTask;

    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        const SlotCounts_t *pRound = &pTask->aRoundCounts[uiArea];
        const SlotCounts_t *pTotal = &pTask->aShownTotals[uiArea];

        SetTableText(pApp->StatisticsTable, (int32_t)uiArea, 0, L"%ls", gpacAreaLabels[uiArea]);
        SetTableText(pApp->StatisticsTable, (int32_t)uiArea, 1, L"%llu", pRound->ullSuccess);
        SetTableText(pApp->StatisticsTable, (int32_t)uiArea, 2, L"%llu", pRound->ullCollision);
        SetTableText(pApp->StatisticsTable, (int32_t)uiArea, 3, L"%llu", pRound->ullIdle);
        SetTableText(pApp->StatisticsTable, (int32_t)uiArea, 4, L"%llu", pTotal->ullSuccess);
        SetTableText(pApp->StatisticsTable, (int32_t)uiArea, 5, L"%llu", pTotal->ullCollidedAttempts);
    }
    if (IsGroupedResult(&pTask->Result)) {
        ShowGroupStatistics(pApp);
        ShowCompareStatistics(pApp);
    }
    /* In a repeated run the round tables belong to repetition 1. */
    const wchar_t *pcRun = IsRepeatedResult(&pTask->Result) ? L"  ·  1번째 실행" : L"";

    if (IsRepeatedResult(&pTask->Result) && (TableRepeat == pApp->Table)) {
        SetFormattedText(pApp->StatisticsTitle, L"%u회 반복 통계  ·  실행마다 다른 시드  ·  누적은 마지막 회차까지, 접속 실패는 종료 시점", pTask->Result.uiRepeatCount);
    }
    else if (IsGroupedResult(&pTask->Result) && (TableCompare == pApp->Table)) {
        SetFormattedText(pApp->StatisticsTitle, L"%llu회차까지 누적  ·  그룹 적용과 미적용(같은 시드) 비교  ·  %ls는 종료 시점 기준%ls", pApp->ullShownRound, gacFailureTitle, pcRun);
    }
    else if (IsGroupedResult(&pTask->Result) && (TableGroup == pApp->Table)) {
        SetFormattedText(pApp->StatisticsTitle, L"%llu회차 그룹 통계  ·  누적 값은 1회차부터 선택 회차까지  ·  합계는 그룹들의 합%ls", pApp->ullShownRound, pcRun);
    }
    else {
        SetFormattedText(pApp->StatisticsTitle, L"%llu회차 통계  ·  누적 값은 1회차부터 선택 회차까지%ls", pApp->ullShownRound, pcRun);
    }
}

static void ShowResult(Application_t *pApp)
{
    const SimulationTask_t *pTask = pApp->pTask;
    const SimulationResult_t *pResult = &pTask->Result;
    uint64_t ullTime = pApp->ullShownRound * pResult->Config.ullSuperframeMilliseconds;
    uint64_t ullSeconds = ullTime / 1000;
    uint32_t uiFraction = (uint32_t)(ullTime % 1000);

    pApp->View.bShowResults = true;
    pApp->View.ullSelectedRound = pApp->ullShownRound;
    SetFormattedText(pApp->RoundEdit, L"%llu", pApp->ullShownRound);
    if (0 == uiFraction) {
        SetFormattedText(pApp->Reception, L"/ %llu회   ·   수신 %llu초", pResult->ullRoundCount, ullSeconds);
    }
    else {
        SetFormattedText(pApp->Reception, L"/ %llu회   ·   수신 %llu.%03u초", pResult->ullRoundCount, ullSeconds, uiFraction);
    }
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        pApp->View.paucStates[uiArea] = pTask->paucStates[uiArea];
    }
    ShowAreaStatistics(pApp);
    RefreshSlotChart(pApp->Chart, &pApp->View);
}

static bool FormatBuffer(wchar_t *pcText, size_t uCapacity, const wchar_t *pcFormat, ...)
{
    va_list Arguments;
    int32_t iWritten;

    va_start(Arguments, pcFormat);
    iWritten = vswprintf(pcText, uCapacity, pcFormat, Arguments);
    va_end(Arguments);
    if ((iWritten < 0) || ((size_t)iWritten >= uCapacity)) {
        pcText[0] = L'\0';
        return false;
    }
    return true;
}

static int32_t MeasureWidestText(HDC Target, const wchar_t *const pacTexts[], uint32_t uiCount)
{
    int32_t iWidth = 0;

    for (uint32_t uiIndex = 0; uiIndex < uiCount; uiIndex++) {
        SIZE Size;
        if ((FALSE != GetTextExtentPoint32W(Target, pacTexts[uiIndex], (int32_t)wcslen(pacTexts[uiIndex]), &Size)) && (Size.cx > iWidth)) {
            iWidth = Size.cx;
        }
    }
    return iWidth;
}

static void DrawSettingPair(HDC Target, RECT Cell, int32_t iLabelWidth, const wchar_t *pcLabel, const wchar_t *pcValue)
{
    RECT Label = { Cell.left, Cell.top, Cell.left + iLabelWidth, Cell.bottom };
    RECT Value = { Cell.left + iLabelWidth, Cell.top, Cell.right, Cell.bottom };

    SetTextColor(Target, guiLabelColor);
    DrawTextW(Target, pcLabel, -1, &Label, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SetTextColor(Target, guiValueColor);
    DrawTextW(Target, pcValue, -1, &Value, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

static void FormatTerminalMode(const AreaGroupPlan_t *pPlan, wchar_t *pcText, size_t uCapacity)
{
    if (0 == pPlan->uiFixedTerminalGroups) {
        FormatBuffer(pcText, uCapacity, L"균등");
    }
    else if (pPlan->uiFixedTerminalGroups == pPlan->uiGroupCount) {
        FormatBuffer(pcText, uCapacity, L"직접 지정");
    }
    else {
        FormatBuffer(pcText, uCapacity, L"직접 %u개", pPlan->uiFixedTerminalGroups);
    }
}

static void DrawPanelText(HDC Target, const wchar_t *pcText, RECT Cell, COLORREF uiColor)
{
    SetTextColor(Target, uiColor);
    DrawTextW(Target, pcText, -1, &Cell, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

/* Channel-type table: one header line and one line per channel type. Grouped runs add the assignment modes. */
static void DrawAreaTable(const Application_t *pApp, HDC Target, RECT Table, int32_t iRow)
{
    const SimulationResult_t *pResult = &pApp->pTask->Result;
    const SimulationConfig_t *pConfig = &pResult->Config;
    const wchar_t *const pacHeaders[] = { L"채널 수", L"주파수 위치", L"단말 배정", L"슬롯 배정" };
    uint32_t uiColumns = IsGroupedResult(pResult) ? 4 : 2;
    int32_t iName = ScaleLayout(pApp, 70);
    int32_t iColumn = (Table.right - Table.left - iName) / 4;
    uint32_t uiTimeSlots = GetTimeSlotCount(pConfig->uiSymbolRate, pConfig->uiWaveId);

    for (uint32_t uiHeader = 0; uiHeader < uiColumns; uiHeader++) {
        RECT Cell = { Table.left + iName + (int32_t)uiHeader * iColumn, Table.top, Table.left + iName + (int32_t)(uiHeader + 1) * iColumn, Table.top + iRow };
        DrawPanelText(Target, pacHeaders[uiHeader], Cell, guiLabelColor);
    }
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        const AreaGroupPlan_t *pPlan = &pResult->aPlans[uiArea];
        int32_t iTop = Table.top + (int32_t)(uiArea + 1) * iRow;
        RECT Name = { Table.left, iTop, Table.left + iName, iTop + iRow };
        wchar_t aacValues[4][32] = { { 0 } };
        HGDIOBJ PreviousFont = SelectObject(Target, pApp->View.BoldFont);

        DrawPanelText(Target, gpacAreaLabels[uiArea], Name, guiValueColor);
        SelectObject(Target, PreviousFont);
        FormatBuffer(aacValues[0], 32, L"%u 칸", pConfig->auiSlots[uiArea]);
        if (0 == pConfig->auiSlots[uiArea]) {
            FormatBuffer(aacValues[1], 32, L"경쟁 없음");
        }
        else {
            FormatBuffer(aacValues[1], 32, L"%u 개", GetFrequencyRowCount(pConfig->auiSlots[uiArea], uiTimeSlots));
        }
        if (0 == pPlan->uiGroupCount) {
            FormatBuffer(aacValues[2], 32, L"-");
            FormatBuffer(aacValues[3], 32, L"-");
        }
        else {
            FormatTerminalMode(pPlan, aacValues[2], 32);
            FormatBuffer(aacValues[3], 32, L"%ls", GetSlotModeName(pPlan->SlotAssignment));
        }
        for (uint32_t uiValue = 0; uiValue < uiColumns; uiValue++) {
            RECT Cell = { Table.left + iName + (int32_t)uiValue * iColumn, iTop, Table.left + iName + (int32_t)(uiValue + 1) * iColumn - ScaleLayout(pApp, 6), iTop + iRow };
            DrawPanelText(Target, aacValues[uiValue], Cell, guiValueColor);
        }
    }
}

static void DrawSettingsPanel(const Application_t *pApp, const DRAWITEMSTRUCT *pDraw)
{
    const wchar_t *const aapcLabels[3][3] = {
        { L"심볼레이트", L"Wave ID", L"시간 슬롯" }, { L"슈퍼프레임 주기", L"시뮬레이션 시간", L"수신 회차" }, { L"단말 수", L"그룹 수", L"반복 횟수" }
    };
    const int32_t aiWeights[3] = { 30, 40, 30 };
    wchar_t aaacValues[3][3][40] = { { { 0 } } };
    HDC Target = pDraw->hDC;
    RECT Bounds = pDraw->rcItem;
    int32_t iPad = ScaleLayout(pApp, 12);
    int32_t iRow = ScaleLayout(pApp, 22);
    int32_t iTop = Bounds.top + ScaleLayout(pApp, 7);
    int32_t iInner = Bounds.right - Bounds.left - 2 * iPad;
    int32_t iDivider = iTop + 3 * iRow + ScaleLayout(pApp, 6);
    int32_t iLeft = Bounds.left + iPad;
    HGDIOBJ PreviousFont;
    HBRUSH Brush = (HBRUSH)GetStockObject(DC_BRUSH);
    const SimulationResult_t *pResult;
    const SimulationConfig_t *pConfig;

    SetDCBrushColor(Target, RGB(248, 250, 252));
    FillRect(Target, &Bounds, Brush);
    SetDCBrushColor(Target, RGB(214, 220, 228));
    FrameRect(Target, &Bounds, Brush);
    if (NULL == pApp->pTask) {
        return;
    }
    pResult = &pApp->pTask->Result;
    pConfig = &pResult->Config;
    FormatBuffer(aaacValues[0][0], 40, L"%u sps", pConfig->uiSymbolRate);
    FormatBuffer(aaacValues[0][1], 40, L"%u", pConfig->uiWaveId);
    FormatBuffer(aaacValues[0][2], 40, L"%u 개", GetTimeSlotCount(pConfig->uiSymbolRate, pConfig->uiWaveId));
    FormatBuffer(aaacValues[1][0], 40, L"%llu.%03u 초", pConfig->ullSuperframeMilliseconds / 1000, (uint32_t)(pConfig->ullSuperframeMilliseconds % 1000));
    FormatBuffer(aaacValues[1][1], 40, L"%llu.%03u 초", pConfig->ullDurationMilliseconds / 1000, (uint32_t)(pConfig->ullDurationMilliseconds % 1000));
    FormatBuffer(aaacValues[1][2], 40, L"%llu 회", pResult->ullRoundCount);
    FormatBuffer(aaacValues[2][0], 40, L"%u 대", pConfig->uiTerminalCount);
    if (IsRepeatedResult(pResult)) {
        FormatBuffer(aaacValues[2][2], 40, L"%u 회", pResult->uiRepeatCount);
    }
    else {
        FormatBuffer(aaacValues[2][2], 40, L"1 회 (반복 없음)");
    }
    if (IsGroupedResult(pResult)) {
        FormatBuffer(aaacValues[2][1], 40, L"%u 개", pConfig->uiGroupCount);
    }
    else if (0 != pConfig->uiGroupCount) {
        FormatBuffer(aaacValues[2][1], 40, L"%u 개 (활성 채널 타입 없음)", pConfig->uiGroupCount);
    }
    else {
        FormatBuffer(aaacValues[2][1], 40, L"0 (미적용)");
    }

    SetBkMode(Target, TRANSPARENT);
    PreviousFont = SelectObject(Target, pApp->View.Font);
    for (int32_t iSection = 0; iSection < 3; iSection++) {
        int32_t iWidth = iInner * aiWeights[iSection] / 100;
        int32_t iLabelWidth = MeasureWidestText(Target, aapcLabels[iSection], 3) + ScaleLayout(pApp, 12);

        for (int32_t iItem = 0; iItem < 3; iItem++) {
            RECT Cell = { iLeft, iTop + iItem * iRow, iLeft + iWidth - ScaleLayout(pApp, 8), iTop + (iItem + 1) * iRow };
            if (L'\0' != aapcLabels[iSection][iItem][0]) {
                DrawSettingPair(Target, Cell, iLabelWidth, aapcLabels[iSection][iItem], aaacValues[iSection][iItem]);
            }
        }
        iLeft += iWidth;
    }
    SetDCBrushColor(Target, RGB(214, 220, 228));
    FillRect(Target, &(RECT){ Bounds.left + iPad, iDivider, Bounds.right - iPad, iDivider + 1 }, Brush);
    DrawAreaTable(pApp, Target, (RECT){ Bounds.left + iPad, iDivider + ScaleLayout(pApp, 5), Bounds.right - iPad, Bounds.bottom - ScaleLayout(pApp, 4) }, iRow);
    SelectObject(Target, PreviousFont);
}

static void PrepareGroupResults(Application_t *pApp)
{
    const SimulationResult_t *pResult = &pApp->pTask->Result;
    int32_t iRow = 0;
    wchar_t acText[32];

    ListView_DeleteAllItems(pApp->GroupTable);
    SendMessageW(pApp->GroupSelect, CB_RESETCONTENT, 0, 0);
    if (!IsGroupedResult(pResult)) {
        return;
    }
    SendMessageW(pApp->GroupSelect, CB_ADDSTRING, 0, (LPARAM)L"채널 타입 전체");
    for (uint32_t uiGroup = 0; uiGroup < pResult->uiGroupCount; uiGroup++) {
        if (swprintf(acText, sizeof(acText) / sizeof(acText[0]), L"그룹 %u", uiGroup + 1) > 0) {
            SendMessageW(pApp->GroupSelect, CB_ADDSTRING, 0, (LPARAM)acText);
        }
    }
    SendMessageW(pApp->GroupSelect, CB_ADDSTRING, 0, (LPARAM)L"그룹별 비교");
    SendMessageW(pApp->GroupSelect, CB_SETCURSEL, 0, 0);
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        const AreaGroupPlan_t *pPlan = &pResult->aPlans[uiArea];
        uint32_t uiAssigned = pResult->Config.auiSlots[uiArea] - pPlan->uiUnassignedSlots;

        for (uint32_t uiGroup = 0; (0 != pPlan->uiGroupCount) && (uiGroup <= pPlan->uiGroupCount); uiGroup++) {
            bool bTotal = uiGroup == pPlan->uiGroupCount;
            LVITEMW Item = { .mask = LVIF_TEXT | LVIF_PARAM, .iItem = iRow, .pszText = L"",
                .lParam = (LPARAM)(uiArea * (GROUP_MAX_COUNT + 1) + (bTotal ? GROUP_MAX_COUNT : uiGroup)) };
            uint32_t uiTerminals = bTotal ? GetGroupFirstTerminal(pPlan, uiGroup) : GetGroupTerminalCount(pPlan, uiGroup);
            uint32_t uiFirst = bTotal ? 1 : GetGroupFirstTerminal(pPlan, uiGroup) + 1;

            if (-1 == ListView_InsertItem(pApp->GroupTable, &Item)) {
                break;
            }
            SetTableText(pApp->GroupTable, iRow, 0, L"%ls", gpacAreaLabels[uiArea]);
            if (bTotal) {
                SetTableText(pApp->GroupTable, iRow, 1, L"합계");
                if (uiTerminals > 1) {
                    SetTableText(pApp->GroupTable, iRow, 2, L"%u (1–%u)", uiTerminals, uiTerminals);
                }
                else {
                    SetTableText(pApp->GroupTable, iRow, 2, L"%u", uiTerminals);
                }
                SetTableText(pApp->GroupTable, iRow, 3, L"%u", uiAssigned);
            }
            else {
                SetTableText(pApp->GroupTable, iRow, 1, L"그룹 %u", uiGroup + 1);
                if (0 == uiTerminals) {
                    SetTableText(pApp->GroupTable, iRow, 2, L"0");
                }
                else if (1 == uiTerminals) {
                    SetTableText(pApp->GroupTable, iRow, 2, L"1 (%u)", uiFirst);
                }
                else {
                    SetTableText(pApp->GroupTable, iRow, 2, L"%u (%u–%u)", uiTerminals, uiFirst, uiFirst + uiTerminals - 1);
                }
                SetTableText(pApp->GroupTable, iRow, 3, L"%u", pPlan->auiSlotCount[uiGroup]);
            }
            iRow++;
        }
    }
}

static void SetTableHeader(HWND Table, int32_t iColumn, const wchar_t *pcText)
{
    LVCOLUMNW Column = { .mask = LVCF_TEXT, .pszText = (wchar_t *)pcText };
    ListView_SetColumn(Table, iColumn, &Column);
}

static void CenterTableHeaders(HWND Table, int32_t iColumns)
{
    HWND Header = ListView_GetHeader(Table);

    for (int32_t iColumn = 0; iColumn < iColumns; iColumn++) {
        HDITEMW Item = { .mask = HDI_FORMAT };
        if (FALSE != Header_GetItem(Header, iColumn, &Item)) {
            Item.fmt = (Item.fmt & ~HDF_JUSTIFYMASK) | HDF_CENTER;
            Header_SetItem(Header, iColumn, &Item);
        }
    }
}

/* A list view cannot center its first column, so the failure table keeps a hidden zero-width column 0 and shows it last.
   Visible columns: channel type (centered), counts (right-aligned); every header is centered. */
static void FormatFailureTable(HWND Table)
{
    const int32_t aiFormats[FAILURE_COLUMNS] = { LVCFMT_LEFT, LVCFMT_CENTER, LVCFMT_RIGHT, LVCFMT_RIGHT };
    int32_t aiOrder[FAILURE_COLUMNS] = { FailureType, FailureGrouped, FailureBaseline, FailureHidden };

    for (int32_t iColumn = FailureType; iColumn < FAILURE_COLUMNS; iColumn++) {
        LVCOLUMNW Column = { .mask = LVCF_FMT, .fmt = aiFormats[iColumn] };
        ListView_SetColumn(Table, iColumn, &Column);
    }
    ListView_SetColumnOrderArray(Table, FAILURE_COLUMNS, aiOrder);
    CenterTableHeaders(Table, FAILURE_COLUMNS);
}

/* Repeated runs show mean ± sample standard deviation; a single run shows its own count. */
static void SetFailureCell(HWND Table, int32_t iRow, int32_t iColumn, const SimulationResult_t *pResult, AreaType Area, RepeatSeries Series, uint32_t uiSingle)
{
    if (IsRepeatedResult(pResult)) {
        RepeatStatistic_t Statistic;
        SummarizeRepeats(pResult, Area, RepeatFailure, Series, &Statistic);
        SetTableText(Table, iRow, iColumn, L"%.1f ± %.1f 대", Statistic.dMean, Statistic.dDeviation);
    }
    else {
        SetTableText(Table, iRow, iColumn, L"%u 대", uiSingle);
    }
}

/* Grouped runs show the grouped and the same-seed ungrouped counts side by side. */
static void ShowFailureCounts(const Application_t *pApp)
{
    const SimulationResult_t *pResult = &pApp->pTask->Result;
    bool bGrouped = IsGroupedResult(pResult);

    bool bRepeated = IsRepeatedResult(pResult);

    SetTableHeader(pApp->FailureTable, FailureGrouped, bGrouped ? L"그룹 적용" : L"단말 갯수");
    SetTableHeader(pApp->FailureTable, FailureBaseline, bGrouped ? L"그룹 미적용" : L"");
    CenterTableHeaders(pApp->FailureTable, FAILURE_COLUMNS);
    if (bRepeated) {
        SetFormattedText(pApp->TerminalSummary, L"%ls (%u회 평균)", gacFailureTitle, pResult->uiRepeatCount);
    }
    else {
        SetWindowTextW(pApp->TerminalSummary, gacFailureTitle);
    }
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        SetFailureCell(pApp->FailureTable, (int32_t)uiArea, FailureGrouped, pResult, (AreaType)uiArea, RepeatGrouped, pResult->auiUnsuccessful[uiArea]);
        if (bGrouped) {
            SetFailureCell(pApp->FailureTable, (int32_t)uiArea, FailureBaseline, pResult, (AreaType)uiArea, RepeatBaseline, pResult->auiBaselineUnsuccessful[uiArea]);
        }
        else {
            SetTableText(pApp->FailureTable, (int32_t)uiArea, FailureBaseline, L"");
        }
    }
    if (0 == pResult->ullRoundCount) {
        SetWindowTextW(pApp->FailureNote, L"완료된 수신 회차가 없습니다.");
    }
    else if (bRepeated && bGrouped) {
        SetFormattedText(pApp->FailureNote, L"%u회 반복 평균 ± 표준편차\r\n그룹 미적용: 같은 시드로 그룹 없이 계산한 값\r\n아래 그래프·표는 1번째 실행 결과", pResult->uiRepeatCount);
    }
    else if (bRepeated) {
        SetFormattedText(pApp->FailureNote, L"%u회 반복 평균 ± 표준편차\r\n아래 그래프·표는 1번째 실행 결과", pResult->uiRepeatCount);
    }
    else if (bGrouped) {
        SetWindowTextW(pApp->FailureNote, L"종료 시점까지 성공 0회인 단말 수\r\n그룹 미적용: 같은 시드로 그룹 없이 계산한 값");
    }
    else {
        SetWindowTextW(pApp->FailureNote, L"종료 시점까지 성공 0회인 단말 수");
    }
}

static void AddRepeatRow(HWND Table, int32_t *piRow, AreaType Area, const SimulationResult_t *pResult, const wchar_t *pcLabel, RepeatMetric Metric, RepeatSeries Series)
{
    LVITEMW Item = { .mask = LVIF_TEXT, .iItem = *piRow, .pszText = L"" };
    RepeatStatistic_t Statistic;

    if (-1 == ListView_InsertItem(Table, &Item)) {
        return;
    }
    SummarizeRepeats(pResult, Area, Metric, Series, &Statistic);
    SetTableText(Table, *piRow, 0, L"%ls", gpacAreaLabels[Area]);
    SetTableText(Table, *piRow, 1, L"%ls", pcLabel);
    SetTableText(Table, *piRow, 2, L"%.2f", Statistic.dMean);
    SetTableText(Table, *piRow, 3, L"%.2f", Statistic.dDeviation);
    SetTableText(Table, *piRow, 4, L"%.0f", Statistic.dMinimum);
    SetTableText(Table, *piRow, 5, L"%.0f", Statistic.dMaximum);
    (*piRow)++;
}

/* One row per channel type and value. Grouped runs compare grouped, ungrouped and their per-run difference. */
static void ShowRepeatStatistics(const Application_t *pApp)
{
    const SimulationResult_t *pResult = &pApp->pTask->Result;
    bool bGrouped = IsGroupedResult(pResult);
    int32_t iRow = 0;

    ListView_DeleteAllItems(pApp->RepeatTable);
    if (!IsRepeatedResult(pResult)) {
        return;
    }
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        AreaType Area = (AreaType)uiArea;

        if (0 == pResult->Config.auiSlots[uiArea]) {
            continue;
        }
        if (bGrouped) {
            AddRepeatRow(pApp->RepeatTable, &iRow, Area, pResult, L"최종 접속 실패 단말 갯수 · 그룹 적용", RepeatFailure, RepeatGrouped);
            AddRepeatRow(pApp->RepeatTable, &iRow, Area, pResult, L"최종 접속 실패 단말 갯수 · 그룹 미적용", RepeatFailure, RepeatBaseline);
            AddRepeatRow(pApp->RepeatTable, &iRow, Area, pResult, L"최종 접속 실패 단말 갯수 · 차이 (적용 − 미적용)", RepeatFailure, RepeatDifference);
            AddRepeatRow(pApp->RepeatTable, &iRow, Area, pResult, L"누적 성공 건수 · 그룹 적용", RepeatSuccess, RepeatGrouped);
            AddRepeatRow(pApp->RepeatTable, &iRow, Area, pResult, L"누적 성공 건수 · 그룹 미적용", RepeatSuccess, RepeatBaseline);
            AddRepeatRow(pApp->RepeatTable, &iRow, Area, pResult, L"누적 충돌 전송 건수 · 그룹 적용", RepeatCollided, RepeatGrouped);
            AddRepeatRow(pApp->RepeatTable, &iRow, Area, pResult, L"누적 충돌 전송 건수 · 그룹 미적용", RepeatCollided, RepeatBaseline);
        }
        else {
            AddRepeatRow(pApp->RepeatTable, &iRow, Area, pResult, gacFailureTitle, RepeatFailure, RepeatGrouped);
            AddRepeatRow(pApp->RepeatTable, &iRow, Area, pResult, L"누적 성공 건수", RepeatSuccess, RepeatGrouped);
            AddRepeatRow(pApp->RepeatTable, &iRow, Area, pResult, L"누적 충돌 전송 건수", RepeatCollided, RepeatGrouped);
        }
    }
}

static void AddTableMode(HWND Select, const wchar_t *pcText, TableMode Mode)
{
    LRESULT Index = SendMessageW(Select, CB_ADDSTRING, 0, (LPARAM)pcText);

    if (Index >= 0) {
        SendMessageW(Select, CB_SETITEMDATA, (WPARAM)Index, (LPARAM)Mode);
    }
}

static void SelectTableMode(HWND Select, TableMode Mode)
{
    LRESULT Count = SendMessageW(Select, CB_GETCOUNT, 0, 0);

    for (LRESULT Index = 0; Index < Count; Index++) {
        if ((LRESULT)Mode == SendMessageW(Select, CB_GETITEMDATA, (WPARAM)Index, 0)) {
            SendMessageW(Select, CB_SETCURSEL, (WPARAM)Index, 0);
        }
    }
}

/* The table list depends on the run: group tables for grouped runs, the repeat table for repeated runs. */
static void PrepareTableModes(Application_t *pApp)
{
    const SimulationResult_t *pResult = &pApp->pTask->Result;

    SendMessageW(pApp->TableModeSelect, CB_RESETCONTENT, 0, 0);
    AddTableMode(pApp->TableModeSelect, L"채널 타입 통계", TableArea);
    if (IsGroupedResult(pResult)) {
        AddTableMode(pApp->TableModeSelect, L"그룹 통계", TableGroup);
        AddTableMode(pApp->TableModeSelect, L"미적용 비교", TableCompare);
    }
    if (IsRepeatedResult(pResult)) {
        AddTableMode(pApp->TableModeSelect, L"반복 통계", TableRepeat);
    }
    pApp->Table = IsRepeatedResult(pResult) ? TableRepeat : TableArea;
    SelectTableMode(pApp->TableModeSelect, pApp->Table);
}

static void ShowFinalSummary(Application_t *pApp)
{
    InvalidateRect(pApp->SettingsPanel, NULL, FALSE);
    ShowFailureCounts(pApp);
    PrepareGroupResults(pApp);
    PrepareTableModes(pApp);
    ShowRepeatStatistics(pApp);
    ShowAreaStatistics(pApp);
}

static void ShowGroupIssues(Application_t *pApp)
{
    UpdateGroupSummary(pApp);
    ShowGridStatus(pApp);
    SetFocus(pApp->GroupButton);
}

static void StartSimulation(Application_t *pApp)
{
    SimulationConfig_t Config = { 0 };
    SimulationTask_t *pTask;
    HANDLE Worker;

    if (!ReadConfig(pApp, &Config)) {
        return;
    }
    if (0 != ValidateGroupConfig(&Config, &pApp->Groups, NULL, 0)) {
        ShowGroupIssues(pApp);
        return;
    }
    pTask = CreateTask(&Config, &pApp->Groups);
    if (NULL == pTask) {
        SetWindowTextW(pApp->Status, L"결과 메모리가 부족합니다. 채널 수, 단말 수, 그룹 수, 시뮬레이션 시간을 확인하세요.");
        return;
    }
    Worker = CreateThread(NULL, 0, RunWorker, pTask, 0, NULL);
    if (NULL == Worker) {
        FreeTask(pTask);
        SetWindowTextW(pApp->Status, L"시뮬레이션을 시작할 수 없습니다. 다시 시도하세요.");
        return;
    }
    ClearResults(pApp);
    pApp->pTask = pTask;
    pApp->Worker = Worker;
    UpdatePreview(pApp);
    SetWindowTextW(pApp->Status, L"시뮬레이션 집계 중…");
    UpdateControls(pApp);
}

static void OpenGroupSettings(HWND Window, Application_t *pApp)
{
    GroupDialogInput_t Input = { .Instance = pApp->Instance, .Font = pApp->View.Font, .BoldFont = pApp->View.BoldFont, .iDpi = pApp->View.iDpi };

    if (NULL != pApp->Worker) {
        return;
    }
    if (!ReadGridConfig(pApp, &Input.Config)) {
        SetWindowTextW(pApp->Status, L"그룹 설정 전에 채널 수, 단말 수, 그룹 수(0~100)를 올바르게 입력하세요.");
        return;
    }
    if (0 == Input.Config.uiGroupCount) {
        SetWindowTextW(pApp->Status, L"그룹 수를 1 이상 입력하면 그룹 설정을 열 수 있습니다. 0이면 그룹을 사용하지 않습니다.");
        SetFocus(pApp->aFields[FieldGroups]);
        return;
    }
    if (!IsAreaGroupActive(&Input.Config, AreaAlert) && !IsAreaGroupActive(&Input.Config, AreaLb)) {
        SetWindowTextW(pApp->Status, L"채널 수가 1 이상인 채널 타입이 있어야 그룹을 설정할 수 있습니다.");
        return;
    }
    if (ShowGroupDialog(Window, &Input, &pApp->Groups)) {
        UpdatePreview(pApp);
        SetWindowTextW(pApp->Status, L"그룹 설정을 적용했습니다. 다음 실행부터 사용합니다.");
    }
    else {
        UpdatePreview(pApp);
    }
}

static void SelectRound(Application_t *pApp, uint64_t ullRound)
{
    SimulationTask_t *pTask = pApp->pTask;
    HANDLE Worker;

    if ((NULL != pApp->Worker) || (NULL == pTask) || (0 == ullRound) || (ullRound > pTask->Result.ullRoundCount) || (ullRound == pApp->ullShownRound)) {
        return;
    }
    pTask->Operation = TaskRound;
    pTask->ullTargetRound = ullRound;
    Worker = CreateThread(NULL, 0, RunWorker, pTask, 0, NULL);
    if (NULL == Worker) {
        pTask->ullTargetRound = 0;
        SetWindowTextW(pApp->Reception, L"선택한 회차를 표시할 수 없습니다.");
        return;
    }
    pApp->Worker = Worker;
    pApp->View.bShowResults = false;
    RefreshSlotChart(pApp->Chart, &pApp->View);
    SetWindowTextW(pApp->Reception, L"선택 회차 불러오는 중…");
    UpdateControls(pApp);
}

static void StartExport(HWND Window, Application_t *pApp)
{
    SimulationTask_t *pTask = pApp->pTask;
    wchar_t acPath[MAX_PATH] = L"SlottedAloha-results.xlsx";
    OPENFILENAMEW File = {
        .lStructSize = sizeof(File),
        .hwndOwner = Window,
        .lpstrFilter = L"Excel 통합 문서 (*.xlsx)\0*.xlsx\0",
        .nFilterIndex = 1,
        .lpstrFile = acPath,
        .nMaxFile = MAX_PATH,
        .lpstrTitle = L"결과 파일 생성",
        .Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR,
        .lpstrDefExt = L"xlsx"
    };
    HANDLE Worker;
    ReportCheck Check;

    if ((NULL == pApp->View.pResult) || (NULL != pApp->Worker)) {
        return;
    }
    Check = CheckExportLimits(&pTask->Result);
    if (ReportTooManyRounds == Check) {
        MessageBoxW(Window, L"Excel 행 제한으로 최대 1,048,573회차까지 저장할 수 있습니다. 시뮬레이션 시간이나 주기를 조정하세요.", L"결과 파일 생성", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (ReportTooLarge == Check) {
        MessageBoxW(Window, L"예상 파일 크기가 Excel 결과 파일 한도(4GB)를 넘습니다. 시뮬레이션 시간, 그룹 수 또는 채널 수를 줄여 다시 실행하세요.", L"결과 파일 생성", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (FALSE == GetSaveFileNameW(&File)) {
        if (0 != CommDlgExtendedError()) {
            MessageBoxW(Window, L"저장 대화상자를 열 수 없습니다.", L"결과 파일 생성", MB_OK | MB_ICONERROR);
        }
        return;
    }
    wmemcpy(pTask->acExportPath, acPath, MAX_PATH);
    pTask->Operation = TaskExport;
    Worker = CreateThread(NULL, 0, RunWorker, pTask, 0, NULL);
    if (NULL == Worker) {
        MessageBoxW(Window, L"결과 파일 생성을 시작할 수 없습니다.", L"결과 파일 생성", MB_OK | MB_ICONERROR);
        return;
    }
    pApp->Worker = Worker;
    UpdateControls(pApp);
}

static void ToggleTrend(Application_t *pApp)
{
    if ((0 == pApp->ullShownRound) || (NULL != pApp->Worker)) {
        return;
    }
    pApp->View.bShowTrend = !pApp->View.bShowTrend;
    pApp->View.uiTrendTopRow = 0;
    SetWindowTextW(pApp->Chart, pApp->View.bShowTrend ? L"전송 주기별 성공 및 충돌 단말 수" : L"주파수 및 시간 슬롯");
    ShowAreaStatistics(pApp);
    RefreshSlotChart(pApp->Chart, &pApp->View);
    UpdateControls(pApp);
}

static void ApplyGroupSelection(Application_t *pApp)
{
    LRESULT Selection = SendMessageW(pApp->GroupSelect, CB_GETCURSEL, 0, 0);
    uint32_t uiGroups = (NULL == pApp->pTask) ? 0 : pApp->pTask->Result.uiGroupCount;

    pApp->View.bGroupSelected = false;
    pApp->View.Trend = TrendWholeArea;
    pApp->View.uiTrendTopRow = 0;
    if ((Selection >= 1) && ((uint32_t)Selection <= uiGroups)) {
        pApp->View.bGroupSelected = true;
        pApp->View.uiSelectedGroup = (uint32_t)Selection - 1;
        pApp->View.Trend = TrendSingleGroup;
        pApp->View.uiTrendGroup = (uint32_t)Selection - 1;
    }
    else if ((uint32_t)Selection == uiGroups + 1) {
        pApp->View.Trend = TrendGroupCompare;
    }
    RefreshSlotChart(pApp->Chart, &pApp->View);
}

static void SelectGroupRow(Application_t *pApp, int32_t iRow)
{
    LVITEMW Item = { .mask = LVIF_PARAM, .iItem = iRow };
    uint32_t uiArea;
    uint32_t uiGroup;

    if ((NULL == pApp->pTask) || (FALSE == ListView_GetItem(pApp->GroupTable, &Item))) {
        return;
    }
    uiArea = (uint32_t)Item.lParam / (GROUP_MAX_COUNT + 1);
    uiGroup = (uint32_t)Item.lParam % (GROUP_MAX_COUNT + 1);
    if ((uiArea >= AreaCount) || ((GROUP_MAX_COUNT != uiGroup) && (uiGroup >= pApp->pTask->Result.aPlans[uiArea].uiGroupCount))) {
        return;
    }
    pApp->View.TrendArea = (AreaType)uiArea;
    SendMessageW(pApp->TrendAreaSelect, CB_SETCURSEL, (WPARAM)uiArea, 0);
    /* The total row shows the whole channel type. */
    SendMessageW(pApp->GroupSelect, CB_SETCURSEL, (GROUP_MAX_COUNT == uiGroup) ? 0 : (WPARAM)uiGroup + 1, 0);
    ApplyGroupSelection(pApp);
}

static const wchar_t *GetExportFileName(const wchar_t *pcPath)
{
    const wchar_t *pcBackslash = wcsrchr(pcPath, L'\\');
    const wchar_t *pcSlash = wcsrchr(pcPath, L'/');
    const wchar_t *pcFileName = (NULL == pcBackslash) ? pcPath : pcBackslash + 1;

    if ((NULL != pcSlash) && (pcSlash >= pcFileName)) {
        return pcSlash + 1;
    }
    else {
        return pcFileName;
    }
}

static void ShowExportComplete(HWND Window, const SimulationTask_t *pTask)
{
    wchar_t acMessage[2 * MAX_PATH + 64];
    size_t uCapacity = sizeof(acMessage) / sizeof(acMessage[0]);
    int32_t iLength = swprintf(acMessage, uCapacity, L"[%ls] 파일 생성 완료", GetExportFileName(pTask->acExportPath));

    if ((iLength >= 0) && ((size_t)iLength < uCapacity)) {
        MessageBoxW(Window, acMessage, L"결과 파일 생성", MB_OK | MB_ICONINFORMATION);
    }
}

static void UpdateWorker(HWND Window, Application_t *pApp)
{
    SimulationTask_t *pTask = pApp->pTask;
    DWORD uiExitCode = 1;
    bool bCompletedSimulation = false;

    if (NULL == pApp->Worker) {
        return;
    }
    if (WAIT_OBJECT_0 != WaitForSingleObject(pApp->Worker, 0)) {
        if ((TaskSimulation == pTask->Operation) && !CheckWorkerStop(pTask)) {
            uint64_t ullCompleted = (uint64_t)InterlockedCompareExchange64(&pTask->llCompletedRounds, 0, 0);
            uint32_t uiRepeats = (uint32_t)InterlockedCompareExchange(&pTask->lCompletedRepeats, 0, 0);
            const wchar_t *pcMode = IsGroupedResult(&pTask->Result) ? L"집계 중 (그룹 적용·미적용 비교)" : L"집계 중";

            if ((pTask->Result.Config.uiRepeatCount > 1) && (0 != uiRepeats)) {
                SetFormattedText(pApp->Status, L"%ls   ·   반복 %u / %u번째 실행 완료", pcMode, uiRepeats, pTask->Result.Config.uiRepeatCount);
            }
            else if (pTask->Result.Config.uiRepeatCount > 1) {
                SetFormattedText(pApp->Status, L"%ls   ·   1번째 실행 %llu / %llu회 (반복 %u번)", pcMode, ullCompleted, pTask->Result.ullRoundCount, pTask->Result.Config.uiRepeatCount);
            }
            else {
                SetFormattedText(pApp->Status, L"%ls   ·   %llu / %llu회", pcMode, ullCompleted, pTask->Result.ullRoundCount);
            }
        }
        return;
    }
    GetExitCodeThread(pApp->Worker, &uiExitCode);
    CloseHandle(pApp->Worker);
    pApp->Worker = NULL;
    if (pApp->bClosing) {
        DestroyWindow(Window);
        return;
    }

    if (TaskExport == pTask->Operation) {
        InterlockedExchange(&pTask->lStop, 0);
        UpdateControls(pApp);
        if (0 == uiExitCode) {
            ShowExportComplete(Window, pTask);
        }
        else if (1 != uiExitCode) {
            MessageBoxW(Window, L"결과 파일 저장에 실패했습니다. 저장 위치를 확인하세요.", L"결과 파일 생성", MB_OK | MB_ICONERROR);
        }
        return;
    }
    else if (0 == uiExitCode) {
        if (TaskSimulation == pTask->Operation) {
            const SimulationConfig_t *pConfig = &pTask->Result.Config;

            FreeTerminalSuccess(&pTask->Success);
            FreeTerminalSuccess(&pTask->BaselineSuccess);
            pApp->View.pResult = &pTask->Result;
            pApp->View.uiTimeSlots = GetTimeSlotCount(pConfig->uiSymbolRate, pConfig->uiWaveId);
            pApp->View.uiTopRow = 0;
            pApp->View.uiGroupCount = pTask->Result.uiGroupCount;
            for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
                pApp->View.auiSlots[uiArea] = pConfig->auiSlots[uiArea];
                pApp->View.paucGroups[uiArea] = pTask->Result.aPlans[uiArea].paucSlotGroup;
            }
            pApp->ullShownRound = pTask->Result.ullRoundCount;
            pApp->View.TrendArea = (0 == pConfig->auiSlots[AreaAlert]) ? AreaLb : AreaAlert;
            SendMessageW(pApp->TrendAreaSelect, CB_SETCURSEL, (WPARAM)pApp->View.TrendArea, 0);
            ShowFinalSummary(pApp);
            SetFormattedText(pApp->Status, L"시간 슬롯 %u개", pApp->PreviewView.uiTimeSlots);
            bCompletedSimulation = true;
        }
        else {
            pApp->ullShownRound = pTask->ullTargetRound;
        }
        if (0 != pApp->ullShownRound) {
            ShowResult(pApp);
        }
    }
    else {
        ClearResults(pApp);
        UpdatePreview(pApp);
        SetWindowTextW(pApp->Status, L"시뮬레이션을 중지했습니다.");
    }
    UpdateControls(pApp);
    if (bCompletedSimulation) {
        SelectPage(pApp, PageResult);
    }
}

static HWND CreateControl(HWND Parent, const Application_t *pApp, const wchar_t *pcClass, const wchar_t *pcText, DWORD uiStyle, int32_t iId)
{
    HWND Control = CreateWindowExW(0, pcClass, pcText, WS_CHILD | WS_VISIBLE | uiStyle, 0, 0, 1, 1, Parent, (HMENU)(INT_PTR)iId, pApp->Instance, NULL);

    if (NULL != Control) {
        SendMessageW(Control, WM_SETFONT, (WPARAM)pApp->View.Font, FALSE);
    }
    return Control;
}

static LRESULT CALLBACK HandlePageMessage(HWND Window, UINT uiMessage, WPARAM MessageParameter, LPARAM MessageData)
{
    Application_t *pApp = (Application_t *)GetWindowLongPtrW(Window, GWLP_USERDATA);

    if ((WM_COMMAND == uiMessage) || (WM_CTLCOLORSTATIC == uiMessage) || (WM_NOTIFY == uiMessage) || (WM_DRAWITEM == uiMessage)) {
        return SendMessageW(GetParent(Window), uiMessage, MessageParameter, MessageData);
    }
    /* Only the result content page has the application pointer; it owns the table splitter. */
    if ((NULL != pApp) && HandleSplitterMessage(pApp, Window, uiMessage, MessageData)) {
        return (WM_SETCURSOR == uiMessage) ? TRUE : 0;
    }
    return DefWindowProcW(Window, uiMessage, MessageParameter, MessageData);
}

static bool RegisterPageClass(HINSTANCE Instance)
{
    INITCOMMONCONTROLSEX Controls = { .dwSize = sizeof(Controls), .dwICC = ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES };
    WNDCLASSW Class = {
        .lpfnWndProc = HandlePageMessage,
        .hInstance = Instance,
        .hCursor = LoadCursorW(NULL, IDC_ARROW),
        .hbrBackground = GetSysColorBrush(COLOR_WINDOW),
        .lpszClassName = gacPageClass
    };

    return (FALSE != InitCommonControlsEx(&Controls)) && (0 != RegisterClassW(&Class));
}

static HWND CreatePage(HWND Parent, const Application_t *pApp)
{
    return CreateWindowExW(WS_EX_CONTROLPARENT, gacPageClass, L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 1, 1, Parent, NULL, pApp->Instance, NULL);
}

static bool CreateSimulationControls(Application_t *pApp)
{
    const wchar_t *const pacLabels[FieldCount] = {
        L"심볼레이트", L"Wave ID", L"Alert 채널 수", L"LB 채널 수", L"단말 수", L"슈퍼프레임 주기 (초)", L"시뮬레이션 시간 (초)", L"그룹 수 (0 = 미적용)", L"반복 횟수 (1 = 한 번)"
    };
    const wchar_t *const pacDefaults[FieldCount] = { L"", L"", L"144", L"288", L"3", L"1", L"5", L"0", L"1" };
    uint32_t uiStaticStyle = SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS;
    HWND Page = pApp->SimulationPage;

    for (uint32_t uiField = 0; uiField < FieldCount; uiField++) {
        bool bChoice = uiField <= FieldWaveId;
        DWORD uiStyle = bChoice ? (CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP) : (WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP);
        pApp->aLabels[uiField] = CreateControl(Page, pApp, L"STATIC", pacLabels[uiField], uiStaticStyle, 0);
        pApp->aFields[uiField] = CreateControl(Page, pApp, bChoice ? L"COMBOBOX" : L"EDIT", pacDefaults[uiField], uiStyle, CommandFirstField + (int32_t)uiField);
        if ((NULL == pApp->aLabels[uiField]) || (NULL == pApp->aFields[uiField])) {
            return false;
        }
        if (!bChoice) {
            SendMessageW(pApp->aFields[uiField], EM_SETLIMITTEXT, 24, 0);
        }
    }

    SendMessageW(pApp->aFields[FieldSymbolRate], CB_ADDSTRING, 0, (LPARAM)L"500 sps");
    SendMessageW(pApp->aFields[FieldSymbolRate], CB_ADDSTRING, 0, (LPARAM)L"2000 sps");
    SendMessageW(pApp->aFields[FieldSymbolRate], CB_ADDSTRING, 0, (LPARAM)L"4000 sps");
    SendMessageW(pApp->aFields[FieldSymbolRate], CB_SETCURSEL, 2, 0);
    SendMessageW(pApp->aFields[FieldWaveId], CB_ADDSTRING, 0, (LPARAM)L"3");
    SendMessageW(pApp->aFields[FieldWaveId], CB_ADDSTRING, 0, (LPARAM)L"13");
    SendMessageW(pApp->aFields[FieldWaveId], CB_SETCURSEL, 0, 0);
    SendMessageW(pApp->aFields[FieldSymbolRate], CB_SETITEMHEIGHT, (WPARAM)-1, ScaleLayout(pApp, 25));
    SendMessageW(pApp->aFields[FieldWaveId], CB_SETITEMHEIGHT, (WPARAM)-1, ScaleLayout(pApp, 25));

    pApp->RunButton = CreateControl(Page, pApp, L"BUTTON", L"시뮬레이션 실행", BS_DEFPUSHBUTTON | WS_TABSTOP, CommandRun);
    pApp->GroupButton = CreateControl(Page, pApp, L"BUTTON", L"그룹 설정", BS_PUSHBUTTON | WS_TABSTOP, CommandGroupSettings);
    pApp->GroupSummary = CreateControl(Page, pApp, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0);
    pApp->Status = CreateControl(Page, pApp, L"STATIC", L"", uiStaticStyle, 0);
    pApp->PreviewChart = CreateSlotChart(Page, pApp->Instance, &pApp->PreviewView);
    return (NULL != pApp->RunButton) && (NULL != pApp->GroupButton) && (NULL != pApp->GroupSummary) && (NULL != pApp->Status) && (NULL != pApp->PreviewChart);
}

static HWND CreateResultTable(HWND Parent, const Application_t *pApp, const wchar_t *const pacHeaders[], uint32_t uiColumns, uint32_t uiRows)
{
    HWND Table = CreateControl(Parent, pApp, WC_LISTVIEWW, L"", WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_NOSORTHEADER | WS_TABSTOP, 0);

    if (NULL == Table) {
        return NULL;
    }
    ListView_SetExtendedListViewStyle(Table, LVS_EX_GRIDLINES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    ListView_SetTextColor(Table, RGB(31, 45, 61));
    SendMessageW(ListView_GetHeader(Table), WM_SETFONT, (WPARAM)pApp->View.BoldFont, FALSE);
    for (uint32_t uiColumn = 0; uiColumn < uiColumns; uiColumn++) {
        LVCOLUMNW Column = { .mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT, .fmt = LVCFMT_LEFT,
            .cx = ScaleLayout(pApp, 120), .pszText = (wchar_t *)pacHeaders[uiColumn] };
        if (-1 == ListView_InsertColumn(Table, (int32_t)uiColumn, &Column)) {
            DestroyWindow(Table);
            return NULL;
        }
    }
    for (uint32_t uiRow = 0; uiRow < uiRows; uiRow++) {
        LVITEMW Item = { .mask = LVIF_TEXT, .iItem = (int32_t)uiRow, .pszText = L"" };
        if (-1 == ListView_InsertItem(Table, &Item)) {
            DestroyWindow(Table);
            return NULL;
        }
    }
    return Table;
}

static bool CreateResultControls(Application_t *pApp)
{
    uint32_t uiStaticStyle = SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS;
    const wchar_t *const pacFailures[FAILURE_COLUMNS] = { L"", L"채널 타입", L"단말 갯수", L"" };
    const wchar_t *const pacStatistics[] = { L"채널 타입", L"성공 건수", L"충돌 칸 수", L"유휴 칸 수", L"누적 성공 건수", L"누적 충돌 전송 건수" };
    const wchar_t *const pacGroups[] = { L"채널 타입", L"그룹", L"단말 수 (ID)", L"배정 칸", L"성공", L"충돌 전송", L"충돌 칸", L"유휴 칸",
        L"누적 성공", L"누적 충돌 전송", gacFailureTitle };
    const wchar_t *const pacCompare[] = { L"항목", L"Alert 그룹 적용", L"Alert 미적용", L"Alert 차이", L"LB 그룹 적용", L"LB 미적용", L"LB 차이" };
    const wchar_t *const pacCompareRows[] = { L"누적 성공 건수", L"누적 충돌 전송 건수", gacFailureTitle };
    const wchar_t *const pacRepeat[] = { L"채널 타입", L"항목", L"평균", L"표준편차", L"최소", L"최대" };
    HWND Page = pApp->ResultContent;

    pApp->EmptyResult = CreateControl(pApp->ResultPage, pApp, L"STATIC", L"", SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX, 0);
    pApp->ResultMetadata = CreateControl(Page, pApp, L"STATIC", L"설정 값", uiStaticStyle, 0);
    pApp->TerminalSummary = CreateControl(Page, pApp, L"STATIC", gacFailureTitle, uiStaticStyle, 0);
    pApp->SettingsPanel = CreateControl(Page, pApp, L"STATIC", L"", SS_OWNERDRAW, 0);
    pApp->FailureTable = CreateResultTable(Page, pApp, pacFailures, FAILURE_COLUMNS, AreaCount);
    pApp->StatisticsTable = CreateResultTable(Page, pApp, pacStatistics, 6, AreaCount);
    pApp->GroupTable = CreateResultTable(Page, pApp, pacGroups, 11, 0);
    if (NULL != pApp->GroupTable) {
        SendMessageW(pApp->GroupTable, WM_SETFONT, (WPARAM)pApp->TableFont, FALSE);
        SendMessageW(ListView_GetHeader(pApp->GroupTable), WM_SETFONT, (WPARAM)pApp->TableFont, FALSE);
    }
    pApp->CompareTable = CreateResultTable(Page, pApp, pacCompare, 7, 3);
    pApp->RepeatTable = CreateResultTable(Page, pApp, pacRepeat, 6, 0);
    pApp->StatisticsTitle = CreateControl(Page, pApp, L"STATIC", L"", uiStaticStyle, 0);
    pApp->TableModeSelect = CreateControl(Page, pApp, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, CommandTableMode);
    pApp->FailureNote = CreateControl(Page, pApp, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0);
    pApp->TrendButton = CreateControl(Page, pApp, L"BUTTON", L"전송 추이", BS_PUSHBUTTON | WS_TABSTOP, CommandTrend);
    pApp->TrendAreaSelect = CreateControl(Page, pApp, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, CommandTrendArea);
    pApp->GroupSelect = CreateControl(Page, pApp, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, CommandGroupSelect);
    pApp->GroupViewButton = CreateControl(Page, pApp, L"BUTTON", L"그룹 배정 보기", BS_PUSHBUTTON | WS_TABSTOP, CommandGroupView);
    pApp->ExportButton = CreateControl(Page, pApp, L"BUTTON", L"결과 파일 생성", BS_PUSHBUTTON | WS_TABSTOP, CommandExport);
    pApp->Chart = CreateSlotChart(Page, pApp->Instance, &pApp->View);
    pApp->PreviousButton = CreateControl(Page, pApp, L"BUTTON", L"이전", BS_PUSHBUTTON | WS_TABSTOP, CommandPrevious);
    pApp->RoundEdit = CreateControl(Page, pApp, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, CommandRound);
    pApp->JumpButton = CreateControl(Page, pApp, L"BUTTON", L"회차 이동", BS_PUSHBUTTON | WS_TABSTOP, CommandJump);
    pApp->NextButton = CreateControl(Page, pApp, L"BUTTON", L"다음", BS_PUSHBUTTON | WS_TABSTOP, CommandNext);
    pApp->Reception = CreateControl(Page, pApp, L"STATIC", L"", uiStaticStyle, 0);

    if ((NULL == pApp->EmptyResult) || (NULL == pApp->ResultMetadata) || (NULL == pApp->Chart) ||
        (NULL == pApp->PreviousButton) || (NULL == pApp->RoundEdit) || (NULL == pApp->JumpButton) ||
        (NULL == pApp->NextButton) || (NULL == pApp->Reception) ||
        (NULL == pApp->SettingsPanel) || (NULL == pApp->FailureTable) || (NULL == pApp->StatisticsTable) ||
        (NULL == pApp->GroupTable) || (NULL == pApp->CompareTable) || (NULL == pApp->RepeatTable) || (NULL == pApp->TableModeSelect) ||
        (NULL == pApp->StatisticsTitle) || (NULL == pApp->FailureNote) ||
        (NULL == pApp->TrendAreaSelect) || (NULL == pApp->TrendButton) || (NULL == pApp->GroupSelect) || (NULL == pApp->GroupViewButton) ||
        (NULL == pApp->ExportButton) || (NULL == pApp->TerminalSummary)) {
        return false;
    }
    FormatFailureTable(pApp->FailureTable);
    SetTableText(pApp->FailureTable, AreaAlert, FailureType, L"Alert");
    SetTableText(pApp->FailureTable, AreaLb, FailureType, L"LB");
    for (int32_t iRow = 0; iRow < 3; iRow++) {
        SetTableText(pApp->CompareTable, iRow, 0, L"%ls", pacCompareRows[iRow]);
    }
    SendMessageW(pApp->TrendAreaSelect, CB_ADDSTRING, 0, (LPARAM)L"Alert");
    SendMessageW(pApp->TrendAreaSelect, CB_ADDSTRING, 0, (LPARAM)L"LB");
    SendMessageW(pApp->TrendAreaSelect, CB_SETCURSEL, 0, 0);
    SendMessageW(pApp->TrendAreaSelect, CB_SETITEMHEIGHT, (WPARAM)-1, ScaleLayout(pApp, 25));
    SendMessageW(pApp->GroupSelect, CB_SETITEMHEIGHT, (WPARAM)-1, ScaleLayout(pApp, 25));
    SendMessageW(pApp->TableModeSelect, CB_SETITEMHEIGHT, (WPARAM)-1, ScaleLayout(pApp, 22));
    SendMessageW(pApp->TerminalSummary, WM_SETFONT, (WPARAM)pApp->View.BoldFont, FALSE);
    SendMessageW(pApp->ResultMetadata, WM_SETFONT, (WPARAM)pApp->View.BoldFont, FALSE);
    SendMessageW(pApp->StatisticsTitle, WM_SETFONT, (WPARAM)pApp->View.BoldFont, FALSE);
    SendMessageW(pApp->RoundEdit, EM_SETLIMITTEXT, 20, 0);
    return true;
}

static bool CreateControls(HWND Window, Application_t *pApp)
{
    TCITEMW Tab = { .mask = TCIF_TEXT, .pszText = L"시뮬레이션" };

    pApp->Tabs = CreateControl(Window, pApp, WC_TABCONTROLW, L"", WS_TABSTOP | WS_CLIPSIBLINGS | TCS_FIXEDWIDTH, CommandTabs);
    if ((NULL == pApp->Tabs) || (-1 == TabCtrl_InsertItem(pApp->Tabs, PageSimulation, &Tab))) {
        return false;
    }
    Tab.pszText = L"결과";
    if (-1 == TabCtrl_InsertItem(pApp->Tabs, PageResult, &Tab)) {
        return false;
    }
    TabCtrl_SetItemSize(pApp->Tabs, ScaleLayout(pApp, 120), ScaleLayout(pApp, 34));
    pApp->SimulationPage = CreatePage(Window, pApp);
    pApp->ResultPage = CreatePage(Window, pApp);
    if ((NULL == pApp->SimulationPage) || (NULL == pApp->ResultPage)) {
        return false;
    }
    pApp->ResultContent = CreatePage(pApp->ResultPage, pApp);
    if (NULL == pApp->ResultContent) {
        return false;
    }
    SetWindowLongPtrW(pApp->ResultContent, GWLP_USERDATA, (LONG_PTR)pApp);
    return CreateSimulationControls(pApp) && CreateResultControls(pApp) && (0 != SetTimer(Window, 1, 100, NULL));
}

static void LayoutSimulationControls(const Application_t *pApp)
{
    RECT Client;
    int32_t iMargin = ScaleLayout(pApp, 20);
    int32_t iGap = ScaleLayout(pApp, 15);
    int32_t iFieldHeight = ScaleLayout(pApp, 32);
    int32_t iLabelHeight = ScaleLayout(pApp, 23);
    int32_t iTop = ScaleLayout(pApp, 17);
    int32_t iGroupTop = iTop + 2 * ScaleLayout(pApp, 65);
    int32_t iChartTop = ScaleLayout(pApp, 234);
    int32_t iWidth;
    int32_t iFieldWidth;

    GetClientRect(pApp->SimulationPage, &Client);
    iWidth = Client.right - 2 * iMargin;
    /* Five columns: nine inputs in two rows, then the group button, summary and run button. */
    iFieldWidth = (iWidth - 4 * iGap) / 5;
    for (uint32_t uiField = 0; uiField < FieldCount; uiField++) {
        int32_t iX = iMargin + (int32_t)(uiField % 5) * (iFieldWidth + iGap);
        int32_t iY = iTop + (int32_t)(uiField / 5) * ScaleLayout(pApp, 65);
        int32_t iHeight = (uiField <= FieldWaveId) ? ScaleLayout(pApp, 148) : iFieldHeight;
        MoveWindow(pApp->aLabels[uiField], iX, iY, iFieldWidth, iLabelHeight, TRUE);
        MoveWindow(pApp->aFields[uiField], iX, iY + iLabelHeight, iFieldWidth, iHeight, TRUE);
    }

    MoveWindow(pApp->GroupButton, iMargin, iGroupTop + ScaleLayout(pApp, 4), iFieldWidth, iFieldHeight, TRUE);
    MoveWindow(pApp->GroupSummary, iMargin + iFieldWidth + iGap, iGroupTop, 3 * iFieldWidth + 2 * iGap, ScaleLayout(pApp, 48), TRUE);
    MoveWindow(pApp->RunButton, iMargin + 4 * (iFieldWidth + iGap), iGroupTop + ScaleLayout(pApp, 4), iFieldWidth, iFieldHeight, TRUE);
    MoveWindow(pApp->Status, iMargin, iGroupTop + ScaleLayout(pApp, 56), iWidth, iLabelHeight, TRUE);
    MoveWindow(pApp->PreviewChart, iMargin, iChartTop, iWidth, Client.bottom - iChartTop - iMargin, TRUE);
}

/* Height that shows every row of a table, including its header and border. */
static int32_t GetTableFullHeight(const Application_t *pApp, HWND Table)
{
    RECT Header = { 0 };
    RECT Row = { 0 };
    int32_t iRows = (int32_t)ListView_GetItemCount(Table);
    int32_t iRowHeight = ScaleLayout(pApp, 24);
    int32_t iHeaderHeight;

    GetWindowRect(ListView_GetHeader(Table), &Header);
    iHeaderHeight = (Header.bottom > Header.top) ? Header.bottom - Header.top : ScaleLayout(pApp, 26);
    if ((iRows > 0) && (FALSE != ListView_GetItemRect(Table, 0, &Row, LVIR_BOUNDS)) && (Row.bottom > Row.top)) {
        iRowHeight = Row.bottom - Row.top;
    }
    return iHeaderHeight + iRows * iRowHeight + ScaleLayout(pApp, 6);
}

static int32_t MeasureText(HDC Context, HFONT Font, const wchar_t *pcText)
{
    SIZE Size = { 0 };
    HGDIOBJ PreviousFont = SelectObject(Context, Font);

    GetTextExtentPoint32W(Context, pcText, (int32_t)wcslen(pcText), &Size);
    SelectObject(Context, PreviousFont);
    return Size.cx;
}

/* Sizes the group table columns to their header and cell text and spreads any spare width evenly.
   Nothing is cut: if the columns are wider than the table, the table scrolls sideways. */
static void FitGroupTableColumns(const Application_t *pApp, int32_t iAvailable)
{
    enum { GroupColumns = 11 };
    HWND Table = pApp->GroupTable;
    HWND Header = ListView_GetHeader(Table);
    HFONT HeaderFont = (HFONT)SendMessageW(Header, WM_GETFONT, 0, 0);
    int32_t aiWidths[GroupColumns] = { 0 };
    int32_t iRows = (int32_t)ListView_GetItemCount(Table);
    int32_t iCellPad = ScaleLayout(pApp, 12);
    int32_t iHeaderPad = ScaleLayout(pApp, 16);
    int32_t iTotal = 0;
    HDC Context = GetDC(Table);

    if (NULL == Context) {
        return;
    }
    for (int32_t iColumn = 0; iColumn < GroupColumns; iColumn++) {
        wchar_t acText[128] = L"";
        LVCOLUMNW Column = { .mask = LVCF_TEXT, .pszText = acText, .cchTextMax = (int32_t)(sizeof(acText) / sizeof(acText[0])) };

        for (int32_t iRow = 0; iRow < iRows; iRow++) {
            LVITEMW Item = { .mask = LVIF_PARAM, .iItem = iRow };
            bool bTotal = (FALSE != ListView_GetItem(Table, &Item)) && (GROUP_MAX_COUNT == (uint32_t)Item.lParam % (GROUP_MAX_COUNT + 1));
            int32_t iWidth;

            ListView_GetItemText(Table, iRow, iColumn, acText, (int32_t)(sizeof(acText) / sizeof(acText[0])));
            iWidth = MeasureText(Context, bTotal ? pApp->TableBoldFont : pApp->TableFont, acText) + iCellPad;
            aiWidths[iColumn] = (iWidth > aiWidths[iColumn]) ? iWidth : aiWidths[iColumn];
        }
        if (FALSE != ListView_GetColumn(Table, iColumn, &Column)) {
            int32_t iHeader = MeasureText(Context, (NULL != HeaderFont) ? HeaderFont : pApp->TableFont, Column.pszText) + iHeaderPad;
            aiWidths[iColumn] = (iHeader > aiWidths[iColumn]) ? iHeader : aiWidths[iColumn];
        }
        iTotal += aiWidths[iColumn];
    }
    ReleaseDC(Table, Context);
    if (iTotal < iAvailable) {
        int32_t iExtra = iAvailable - iTotal;
        for (int32_t iColumn = 0; iColumn < GroupColumns; iColumn++) {
            aiWidths[iColumn] += iExtra / GroupColumns + (int32_t)(iColumn < iExtra % GroupColumns);
        }
    }
    for (int32_t iColumn = 0; iColumn < GroupColumns; iColumn++) {
        ListView_SetColumnWidth(Table, iColumn, aiWidths[iColumn]);
    }
}

static void CalculateResultLayout(const Application_t *pApp, ResultLayout_t *pLayout)
{
    RECT Client;
    bool bGrouped = IsGroupedView(pApp);
    int32_t iMargin = ScaleLayout(pApp, 20);
    int32_t iChartTop = ScaleLayout(pApp, 259);
    int32_t iFixed = ScaleLayout(pApp, 120);

    GetClientRect(pApp->ResultPage, &Client);
    *pLayout = (ResultLayout_t){ .iTableHeight = ScaleLayout(pApp, 90) };
    if (bGrouped && (TableCompare == pApp->Table)) {
        pLayout->iTableHeight = ScaleLayout(pApp, 112);
    }
    else if ((bGrouped && (TableGroup == pApp->Table)) || (IsRepeatedView(pApp) && (TableRepeat == pApp->Table))) {
        int32_t iFull = GetTableFullHeight(pApp, (TableRepeat == pApp->Table) ? pApp->RepeatTable : pApp->GroupTable);
        int32_t iDefault = (Client.bottom - iChartTop) * 42 / 100;

        /* The chart keeps at least 200 px; the table shows at least four rows or all of them. */
        pLayout->iMinimumTable = (iFull < ScaleLayout(pApp, 130)) ? iFull : ScaleLayout(pApp, 130);
        pLayout->iMaximumTable = Client.bottom - iFixed - iChartTop - ScaleLayout(pApp, 200);
        if (pLayout->iMaximumTable > iFull) {
            pLayout->iMaximumTable = iFull;
        }
        if (pLayout->iMaximumTable < pLayout->iMinimumTable) {
            pLayout->iMaximumTable = pLayout->iMinimumTable;
        }
        pLayout->iTableHeight = (0 < pApp->iGroupTableHeight) ? pApp->iGroupTableHeight : iDefault;
        pLayout->iTableHeight = (pLayout->iTableHeight < pLayout->iMinimumTable) ? pLayout->iMinimumTable : pLayout->iTableHeight;
        pLayout->iTableHeight = (pLayout->iTableHeight > pLayout->iMaximumTable) ? pLayout->iMaximumTable : pLayout->iTableHeight;
        pLayout->bResizable = pLayout->iMaximumTable > pLayout->iMinimumTable;
    }
    pLayout->iStatistics = Client.bottom - pLayout->iTableHeight - ScaleLayout(pApp, 50);
    pLayout->iNavigation = pLayout->iStatistics - ScaleLayout(pApp, 50);
    pLayout->Splitter = (RECT){ iMargin, pLayout->iNavigation + ScaleLayout(pApp, 34), Client.right - iMargin, pLayout->iStatistics - ScaleLayout(pApp, 2) };
}

static void LayoutResultControls(const Application_t *pApp)
{
    RECT Client;
    ResultLayout_t Layout;
    bool bGrouped = IsGroupedView(pApp);
    int32_t iMargin = ScaleLayout(pApp, 20);
    int32_t iGap = ScaleLayout(pApp, 12);
    int32_t iFieldHeight = ScaleLayout(pApp, 32);
    int32_t iLabelHeight = ScaleLayout(pApp, 23);
    int32_t iToolbarTop = ScaleLayout(pApp, 217);
    int32_t iChartTop = ScaleLayout(pApp, 259);
    int32_t iExportWidth = ScaleLayout(pApp, 150);
    int32_t iTrendWidth = ScaleLayout(pApp, 104);
    int32_t iButtonWidth = ScaleLayout(pApp, 62);
    int32_t iRoundWidth = ScaleLayout(pApp, 106);
    int32_t iJumpWidth = ScaleLayout(pApp, 96);
    int32_t iModeWidth = ScaleLayout(pApp, 130);
    int32_t iTableTop;
    int32_t iWidth;
    int32_t iColumnWidth;
    int32_t iRightWidth;
    int32_t iLeft;

    GetClientRect(pApp->ResultPage, &Client);
    CalculateResultLayout(pApp, &Layout);
    iWidth = Client.right - 2 * iMargin;
    iColumnWidth = (iWidth - iGap) * 65 / 100;
    iRightWidth = iWidth - iColumnWidth - iGap;
    iTableTop = Layout.iStatistics + ScaleLayout(pApp, 30);

    /* Controls are moved without copying old pixels; one repaint at the end avoids stale fragments. */
    MoveWindow(pApp->EmptyResult, iMargin, Client.bottom / 2 - iLabelHeight / 2, iWidth, iLabelHeight, FALSE);
    MoveWindow(pApp->ResultContent, 0, 0, Client.right, Client.bottom, FALSE);
    MoveWindow(pApp->ResultMetadata, iMargin, ScaleLayout(pApp, 15), iColumnWidth, iLabelHeight, FALSE);
    MoveWindow(pApp->SettingsPanel, iMargin, ScaleLayout(pApp, 43), iColumnWidth, ScaleLayout(pApp, 160), FALSE);
    iLeft = iMargin + iColumnWidth + iGap;
    MoveWindow(pApp->TerminalSummary, iLeft, ScaleLayout(pApp, 15), iRightWidth, iLabelHeight, FALSE);
    MoveWindow(pApp->FailureTable, iLeft, ScaleLayout(pApp, 43), iRightWidth, ScaleLayout(pApp, 90), FALSE);
    ListView_SetColumnWidth(pApp->FailureTable, FailureHidden, 0);
    ListView_SetColumnWidth(pApp->FailureTable, FailureType, ScaleLayout(pApp, 96));
    if (bGrouped) {
        ListView_SetColumnWidth(pApp->FailureTable, FailureGrouped, (iRightWidth - ScaleLayout(pApp, 101)) / 2);
        ListView_SetColumnWidth(pApp->FailureTable, FailureBaseline, (iRightWidth - ScaleLayout(pApp, 101)) / 2);
    }
    else {
        ListView_SetColumnWidth(pApp->FailureTable, FailureGrouped, iRightWidth - ScaleLayout(pApp, 101));
        ListView_SetColumnWidth(pApp->FailureTable, FailureBaseline, 0);
    }
    MoveWindow(pApp->FailureNote, iLeft, ScaleLayout(pApp, 139), iRightWidth, ScaleLayout(pApp, 64), FALSE);

    iLeft = iMargin;
    MoveWindow(pApp->TrendButton, iLeft, iToolbarTop, iTrendWidth, iFieldHeight, FALSE);
    iLeft += iTrendWidth + iGap;
    MoveWindow(pApp->TrendAreaSelect, iLeft, iToolbarTop, ScaleLayout(pApp, 95), ScaleLayout(pApp, 104), FALSE);
    if (pApp->View.bShowTrend) {
        iLeft += ScaleLayout(pApp, 95) + iGap;
    }
    MoveWindow(pApp->GroupSelect, iLeft, iToolbarTop, ScaleLayout(pApp, 140), ScaleLayout(pApp, 300), FALSE);
    iLeft += ScaleLayout(pApp, 140) + iGap;
    MoveWindow(pApp->GroupViewButton, iLeft, iToolbarTop, ScaleLayout(pApp, 140), iFieldHeight, FALSE);
    MoveWindow(pApp->ExportButton, Client.right - iMargin - iExportWidth, iToolbarTop, iExportWidth, iFieldHeight, FALSE);
    MoveWindow(pApp->Chart, iMargin, iChartTop, iWidth, Layout.iNavigation - iChartTop - ScaleLayout(pApp, 10), FALSE);
    iLeft = iMargin;
    MoveWindow(pApp->PreviousButton, iLeft, Layout.iNavigation, iButtonWidth, iFieldHeight, FALSE);
    iLeft += iButtonWidth + iGap;
    MoveWindow(pApp->RoundEdit, iLeft, Layout.iNavigation, iRoundWidth, iFieldHeight, FALSE);
    iLeft += iRoundWidth + iGap;
    MoveWindow(pApp->JumpButton, iLeft, Layout.iNavigation, iJumpWidth, iFieldHeight, FALSE);
    iLeft += iJumpWidth + iGap;
    MoveWindow(pApp->NextButton, iLeft, Layout.iNavigation, iButtonWidth, iFieldHeight, FALSE);
    iLeft += iButtonWidth + iGap;
    MoveWindow(pApp->Reception, iLeft, Layout.iNavigation + ScaleLayout(pApp, 6), Client.right - iMargin - iLeft, iLabelHeight, FALSE);
    MoveWindow(pApp->StatisticsTitle, iMargin, Layout.iStatistics, iWidth - (HasTableModes(pApp) ? iModeWidth + iGap : 0), iLabelHeight, FALSE);
    MoveWindow(pApp->TableModeSelect, Client.right - iMargin - iModeWidth, Layout.iStatistics - ScaleLayout(pApp, 4), iModeWidth, ScaleLayout(pApp, 120), FALSE);
    MoveWindow(pApp->StatisticsTable, iMargin, iTableTop, iWidth, ScaleLayout(pApp, 90), FALSE);
    MoveWindow(pApp->GroupTable, iMargin, iTableTop, iWidth, Layout.iTableHeight, FALSE);
    MoveWindow(pApp->CompareTable, iMargin, iTableTop, iWidth, Layout.iTableHeight, FALSE);
    MoveWindow(pApp->RepeatTable, iMargin, iTableTop, iWidth, Layout.iTableHeight, FALSE);
    for (int32_t iColumn = 0; iColumn < 6; iColumn++) {
        const int32_t aiWeights[] = { 12, 40, 12, 12, 12, 12 };
        ListView_SetColumnWidth(pApp->RepeatTable, iColumn, (iWidth - ScaleLayout(pApp, 6) - GetSystemMetrics(SM_CXVSCROLL)) * aiWeights[iColumn] / 100);
    }
    for (int32_t iColumn = 0; iColumn < 6; iColumn++) {
        const int32_t aiWeights[] = { 11, 13, 13, 13, 23, 27 };
        ListView_SetColumnWidth(pApp->StatisticsTable, iColumn, (iWidth - ScaleLayout(pApp, 5)) * aiWeights[iColumn] / 100);
    }
    FitGroupTableColumns(pApp, iWidth - ScaleLayout(pApp, 6) - GetSystemMetrics(SM_CXVSCROLL));
    for (int32_t iColumn = 0; iColumn < 7; iColumn++) {
        const int32_t aiWeights[] = { 22, 13, 13, 13, 13, 13, 13 };
        ListView_SetColumnWidth(pApp->CompareTable, iColumn, (iWidth - ScaleLayout(pApp, 5)) * aiWeights[iColumn] / 100);
    }
    RedrawWindow(pApp->ResultPage, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

static void DrawSplitter(const Application_t *pApp, HWND Window)
{
    PAINTSTRUCT Paint;
    HDC Target = BeginPaint(Window, &Paint);
    ResultLayout_t Layout;
    int32_t iMiddle;
    int32_t iCenter;
    int32_t iGrip = ScaleLayout(pApp, 28);

    CalculateResultLayout(pApp, &Layout);
    if (Layout.bResizable && (IsWindowVisible(pApp->GroupTable) || IsWindowVisible(pApp->RepeatTable))) {
        HBRUSH Brush = (HBRUSH)GetStockObject(DC_BRUSH);
        iMiddle = (Layout.Splitter.top + Layout.Splitter.bottom) / 2;
        iCenter = (Layout.Splitter.left + Layout.Splitter.right) / 2;
        SetDCBrushColor(Target, RGB(222, 227, 234));
        FillRect(Target, &(RECT){ Layout.Splitter.left, iMiddle, Layout.Splitter.right, iMiddle + 1 }, Brush);
        SetDCBrushColor(Target, pApp->bSplitterDragging ? RGB(52, 96, 140) : RGB(160, 172, 186));
        FillRect(Target, &(RECT){ iCenter - iGrip, iMiddle - ScaleLayout(pApp, 3), iCenter + iGrip, iMiddle - ScaleLayout(pApp, 2) }, Brush);
        FillRect(Target, &(RECT){ iCenter - iGrip, iMiddle + ScaleLayout(pApp, 2), iCenter + iGrip, iMiddle + ScaleLayout(pApp, 3) }, Brush);
    }
    EndPaint(Window, &Paint);
}

/* Dragging the bar between the round controls and the table resizes the group table. */
static bool HandleSplitterMessage(Application_t *pApp, HWND Window, UINT uiMessage, LPARAM MessageData)
{
    ResultLayout_t Layout;
    POINT Point = { (int16_t)LOWORD(MessageData), (int16_t)HIWORD(MessageData) };

    if (WM_PAINT == uiMessage) {
        DrawSplitter(pApp, Window);
        return true;
    }
    if (WM_CAPTURECHANGED == uiMessage) {
        if (pApp->bSplitterDragging) {
            pApp->bSplitterDragging = false;
            InvalidateRect(Window, NULL, TRUE);
        }
        return true;
    }
    if ((WM_SETCURSOR != uiMessage) && (WM_LBUTTONDOWN != uiMessage) && !pApp->bSplitterDragging) {
        return false;
    }
    CalculateResultLayout(pApp, &Layout);
    if (WM_SETCURSOR == uiMessage) {
        GetCursorPos(&Point);
        ScreenToClient(Window, &Point);
        if ((HTCLIENT == LOWORD(MessageData)) && Layout.bResizable && (FALSE != PtInRect(&Layout.Splitter, Point))) {
            SetCursor(LoadCursorW(NULL, IDC_SIZENS));
            return true;
        }
        return false;
    }
    if ((WM_LBUTTONDOWN == uiMessage) && Layout.bResizable && (FALSE != PtInRect(&Layout.Splitter, Point)) && (NULL == pApp->Worker)) {
        pApp->bSplitterDragging = true;
        pApp->iSplitterGrab = Point.y - Layout.Splitter.top;
        SetCapture(Window);
        InvalidateRect(Window, NULL, TRUE);
        return true;
    }
    if ((WM_MOUSEMOVE == uiMessage) && pApp->bSplitterDragging) {
        RECT Client;
        int32_t iHeight;

        GetClientRect(pApp->ResultPage, &Client);
        /* Splitter.top = Client.bottom - table height - 50 - 16 in layout units. */
        iHeight = Client.bottom - ScaleLayout(pApp, 66) - (Point.y - pApp->iSplitterGrab);
        iHeight = (iHeight < Layout.iMinimumTable) ? Layout.iMinimumTable : ((iHeight > Layout.iMaximumTable) ? Layout.iMaximumTable : iHeight);
        if (iHeight != Layout.iTableHeight) {
            pApp->iGroupTableHeight = iHeight;
            LayoutResultControls(pApp);
        }
        return true;
    }
    if ((WM_LBUTTONUP == uiMessage) && pApp->bSplitterDragging) {
        ReleaseCapture();
        return true;
    }
    return false;
}

/* The list view aligns cells like their headers, so the centered count headers would center the counts too.
   Count cells are drawn here, right-aligned; the table is read-only, so no selection colors are needed. */
static LRESULT DrawFailureCell(const Application_t *pApp, NMLVCUSTOMDRAW *pDraw)
{
    int32_t iRow = (int32_t)pDraw->nmcd.dwItemSpec;
    RECT Cell;
    wchar_t acText[64] = L"";

    if (CDDS_PREPAINT == pDraw->nmcd.dwDrawStage) {
        return CDRF_NOTIFYITEMDRAW;
    }
    if (CDDS_ITEMPREPAINT == pDraw->nmcd.dwDrawStage) {
        return CDRF_NOTIFYSUBITEMDRAW;
    }
    if (((CDDS_ITEMPREPAINT | CDDS_SUBITEM) != pDraw->nmcd.dwDrawStage) ||
        ((FailureGrouped != pDraw->iSubItem) && (FailureBaseline != pDraw->iSubItem)) ||
        (FALSE == ListView_GetSubItemRect(pApp->FailureTable, iRow, pDraw->iSubItem, LVIR_BOUNDS, &Cell))) {
        return CDRF_DODEFAULT;
    }
    ListView_GetItemText(pApp->FailureTable, iRow, pDraw->iSubItem, acText, (int32_t)(sizeof(acText) / sizeof(acText[0])));
    FillRect(pDraw->nmcd.hdc, &(RECT){ Cell.left, Cell.top, Cell.right - 1, Cell.bottom - 1 }, GetSysColorBrush(COLOR_WINDOW));
    Cell.right -= ScaleLayout(pApp, 10);
    SetBkMode(pDraw->nmcd.hdc, TRANSPARENT);
    SetTextColor(pDraw->nmcd.hdc, guiValueColor);
    DrawTextW(pDraw->nmcd.hdc, acText, -1, &Cell, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    return CDRF_SKIPDEFAULT;
}

/* The total row of each channel type is drawn in bold on a light background. */
static LRESULT DrawGroupTableRow(const Application_t *pApp, NMLVCUSTOMDRAW *pDraw)
{
    if (CDDS_PREPAINT == pDraw->nmcd.dwDrawStage) {
        return CDRF_NOTIFYITEMDRAW;
    }
    if ((CDDS_ITEMPREPAINT == pDraw->nmcd.dwDrawStage) && (GROUP_MAX_COUNT == (uint32_t)pDraw->nmcd.lItemlParam % (GROUP_MAX_COUNT + 1))) {
        SelectObject(pDraw->nmcd.hdc, pApp->TableBoldFont);
        pDraw->clrTextBk = RGB(236, 241, 247);
        return CDRF_NEWFONT;
    }
    return CDRF_DODEFAULT;
}

static void LayoutControls(HWND Window, const Application_t *pApp)
{
    RECT Client;
    RECT Pane;
    int32_t iMargin = ScaleLayout(pApp, 14);
    int32_t iWidth;
    int32_t iHeight;

    GetClientRect(Window, &Client);
    iWidth = Client.right - 2 * iMargin;
    iHeight = Client.bottom - 2 * iMargin;
    MoveWindow(pApp->Tabs, iMargin, iMargin, iWidth, iHeight, TRUE);
    Pane = (RECT){ 0, 0, iWidth, iHeight };
    TabCtrl_AdjustRect(pApp->Tabs, FALSE, &Pane);
    MoveWindow(pApp->SimulationPage, iMargin + Pane.left, iMargin + Pane.top, Pane.right - Pane.left, Pane.bottom - Pane.top, TRUE);
    MoveWindow(pApp->ResultPage, iMargin + Pane.left, iMargin + Pane.top, Pane.right - Pane.left, Pane.bottom - Pane.top, TRUE);
    LayoutSimulationControls(pApp);
    LayoutResultControls(pApp);
}

static bool HandleSelectionCommand(Application_t *pApp, uint32_t uiCommand, uint32_t uiNotification)
{
    LRESULT Selection;

    if (CBN_SELCHANGE != uiNotification) {
        return false;
    }
    if (CommandTrendArea == uiCommand) {
        Selection = SendMessageW(pApp->TrendAreaSelect, CB_GETCURSEL, 0, 0);
        if ((Selection >= AreaAlert) && (Selection < AreaCount)) {
            pApp->View.TrendArea = (AreaType)Selection;
            pApp->View.uiTrendTopRow = 0;
            RefreshSlotChart(pApp->Chart, &pApp->View);
        }
        return true;
    }
    if (CommandGroupSelect == uiCommand) {
        ApplyGroupSelection(pApp);
        return true;
    }
    if ((CommandTableMode == uiCommand) && (NULL != pApp->pTask)) {
        Selection = SendMessageW(pApp->TableModeSelect, CB_GETCURSEL, 0, 0);
        Selection = (Selection < 0) ? CB_ERR : SendMessageW(pApp->TableModeSelect, CB_GETITEMDATA, (WPARAM)Selection, 0);
        if ((Selection >= TableArea) && (Selection <= TableRepeat)) {
            pApp->Table = (TableMode)Selection;
            ShowAreaStatistics(pApp);
            UpdateControls(pApp);
        }
        return true;
    }
    return false;
}

static void HandleCommand(HWND Window, Application_t *pApp, uint32_t uiCommand, uint32_t uiNotification)
{
    if ((uiCommand >= CommandFirstField) && (uiCommand < CommandFirstField + FieldCount)) {
        if ((NULL == pApp->Worker) && ((EN_CHANGE == uiNotification) || (CBN_SELCHANGE == uiNotification))) {
            UpdatePreview(pApp);
        }
        return;
    }
    if (HandleSelectionCommand(pApp, uiCommand, uiNotification)) {
        return;
    }
    if (BN_CLICKED != uiNotification) {
        return;
    }
    if (IDOK == uiCommand) {
        if (PageSimulation != TabCtrl_GetCurSel(pApp->Tabs)) {
            return;
        }
        uiCommand = CommandRun;
    }

    switch (uiCommand) {
    case CommandRun:
        if (NULL == pApp->Worker) {
            StartSimulation(pApp);
        }
        else if (TaskSimulation == pApp->pTask->Operation) {
            InterlockedExchange(&pApp->pTask->lStop, 1);
            SetWindowTextW(pApp->Status, L"중지 중…");
            UpdateControls(pApp);
        }
        break;

    case CommandGroupSettings:
        OpenGroupSettings(Window, pApp);
        break;

    case CommandPrevious:
        if (pApp->ullShownRound > 1) {
            SelectRound(pApp, pApp->ullShownRound - 1);
        }
        break;

    case CommandNext:
        if ((NULL != pApp->pTask) && (pApp->ullShownRound < pApp->pTask->Result.ullRoundCount)) {
            SelectRound(pApp, pApp->ullShownRound + 1);
        }
        break;

    case CommandJump:
        if ((NULL != pApp->pTask) && (NULL == pApp->Worker)) {
            uint64_t ullRound;
            if (ReadInteger(pApp->RoundEdit, pApp->pTask->Result.ullRoundCount, &ullRound) && (0 != ullRound)) {
                SelectRound(pApp, ullRound);
            }
            else {
                SetFormattedText(pApp->Reception, L"1부터 %llu 사이의 회차를 입력하세요.", pApp->pTask->Result.ullRoundCount);
            }
        }
        break;

    case CommandExport:
        if (NULL == pApp->Worker) {
            StartExport(Window, pApp);
        }
        else if (TaskExport == pApp->pTask->Operation) {
            InterlockedExchange(&pApp->pTask->lStop, 1);
            UpdateControls(pApp);
        }
        break;

    case CommandTrend:
        ToggleTrend(pApp);
        break;

    case CommandGroupView:
        if ((NULL == pApp->Worker) && !pApp->View.bShowTrend) {
            pApp->View.bShowGroups = !pApp->View.bShowGroups;
            RefreshSlotChart(pApp->Chart, &pApp->View);
            UpdateControls(pApp);
        }
        break;

    default:
        break;
    }
}

static LRESULT CALLBACK HandleWindowMessage(HWND Window, UINT uiMessage, WPARAM MessageParameter, LPARAM MessageData)
{
    Application_t *pApp = (Application_t *)GetWindowLongPtrW(Window, GWLP_USERDATA);

    if (WM_NCCREATE == uiMessage) {
        const CREATESTRUCTW *pCreate = (const CREATESTRUCTW *)MessageData;
        SetWindowLongPtrW(Window, GWLP_USERDATA, (LONG_PTR)pCreate->lpCreateParams);
        return DefWindowProcW(Window, uiMessage, MessageParameter, MessageData);
    }
    if (NULL == pApp) {
        return DefWindowProcW(Window, uiMessage, MessageParameter, MessageData);
    }

    switch (uiMessage) {
    case WM_CREATE:
        if (!CreateControls(Window, pApp)) {
            return -1;
        }
        pApp->bReady = true;
        LayoutControls(Window, pApp);
        UpdatePreview(pApp);
        UpdateControls(pApp);
        SelectPage(pApp, PageSimulation);
        return 0;

    case WM_GETMINMAXINFO:
        {
            MINMAXINFO *pLimits = (MINMAXINFO *)MessageData;
            pLimits->ptMinTrackSize.x = ScaleLayout(pApp, 980);
            pLimits->ptMinTrackSize.y = ScaleLayout(pApp, 800);
        }
        return 0;

    case WM_SIZE:
        if (pApp->bReady && (SIZE_MINIMIZED != MessageParameter)) {
            LayoutControls(Window, pApp);
        }
        return 0;

    case WM_COMMAND:
        if (pApp->bReady) {
            HandleCommand(Window, pApp, LOWORD(MessageParameter), HIWORD(MessageParameter));
        }
        return 0;

    case WM_NOTIFY:
        {
            const NMHDR *pHeader = (const NMHDR *)MessageData;
            if (pApp->bReady && (NULL != pHeader) && (pHeader->hwndFrom == pApp->Tabs) && (TCN_SELCHANGE == pHeader->code)) {
                ShowActivePage(pApp);
            }
            else if (pApp->bReady && (NULL != pHeader) && (pHeader->hwndFrom == pApp->GroupTable) && (LVN_ITEMCHANGED == pHeader->code)) {
                const NMLISTVIEW *pList = (const NMLISTVIEW *)MessageData;
                if ((0 != (pList->uNewState & LVIS_SELECTED)) && (0 == (pList->uOldState & LVIS_SELECTED)) && (NULL == pApp->Worker)) {
                    SelectGroupRow(pApp, pList->iItem);
                }
            }
            else if (pApp->bReady && (NULL != pHeader) && (pHeader->hwndFrom == pApp->GroupTable) && (NM_CUSTOMDRAW == pHeader->code)) {
                return DrawGroupTableRow(pApp, (NMLVCUSTOMDRAW *)MessageData);
            }
            else if (pApp->bReady && (NULL != pHeader) && (pHeader->hwndFrom == pApp->FailureTable) && (NM_CUSTOMDRAW == pHeader->code)) {
                return DrawFailureCell(pApp, (NMLVCUSTOMDRAW *)MessageData);
            }
            else if (pApp->bReady && (NULL != pHeader) && (pHeader->hwndFrom == pApp->FailureTable) && (LVN_ITEMCHANGING == pHeader->code)) {
                const NMLISTVIEW *pList = (const NMLISTVIEW *)MessageData;
                /* Read-only table: refuse selection and focus. */
                return ((0 != (pList->uChanged & LVIF_STATE)) && (0 != (pList->uNewState & (LVIS_SELECTED | LVIS_FOCUSED)))) ? TRUE : FALSE;
            }
            else if (pApp->bReady && (NULL != pHeader) && (pHeader->hwndFrom == ListView_GetHeader(pApp->FailureTable)) &&
                ((HDN_BEGINTRACKW == pHeader->code) || (HDN_BEGINTRACKA == pHeader->code) || (HDN_DIVIDERDBLCLICKW == pHeader->code) || (HDN_DIVIDERDBLCLICKA == pHeader->code))) {
                /* Keep the hidden alignment column at zero width. */
                return (FailureHidden == ((const NMHEADERW *)MessageData)->iItem) ? TRUE : FALSE;
            }
        }
        return 0;

    case WM_DRAWITEM:
        {
            const DRAWITEMSTRUCT *pDraw = (const DRAWITEMSTRUCT *)MessageData;
            if (pApp->bReady && (NULL != pDraw) && (pDraw->hwndItem == pApp->SettingsPanel)) {
                DrawSettingsPanel(pApp, pDraw);
                return TRUE;
            }
        }
        return FALSE;

    case WM_TIMER:
        UpdateWorker(Window, pApp);
        return 0;

    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)MessageParameter, GetSysColor(COLOR_WINDOW));
        if (((HWND)MessageData == pApp->GroupSummary) && pApp->bGroupError) {
            SetTextColor((HDC)MessageParameter, RGB(176, 48, 36));
        }
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);

    case WM_CLOSE:
        if (NULL != pApp->Worker) {
            pApp->bClosing = true;
            InterlockedExchange(&pApp->pTask->lStop, 1);
            UpdateControls(pApp);
        }
        else {
            DestroyWindow(Window);
        }
        return 0;

    case WM_DESTROY:
        KillTimer(Window, 1);
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(Window, uiMessage, MessageParameter, MessageData);
    }
}

int32_t WINAPI wWinMain(HINSTANCE Instance, HINSTANCE PreviousInstance, PWSTR pcArguments, int32_t iShowCommand)
{
    Application_t App = {
        .Instance = Instance,
        .View = { .iDpi = 96, .uiTimeSlots = 72, .auiSlots = { 144, 288 } }
    };
    WNDCLASSW Class = {
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = HandleWindowMessage,
        .hInstance = Instance,
        .hIcon = LoadIconW(NULL, IDI_APPLICATION),
        .hCursor = LoadCursorW(NULL, IDC_ARROW),
        .hbrBackground = GetSysColorBrush(COLOR_3DFACE),
        .lpszClassName = gacWindowClass
    };
    HWND Window = NULL;
    HDC Screen = GetDC(NULL);
    MSG Message = { 0 };
    int32_t iResult = 1;

    (void)PreviousInstance;
    (void)pcArguments;
    if (NULL != Screen) {
        int32_t iDpi = GetDeviceCaps(Screen, LOGPIXELSX);
        if (iDpi > 0) {
            App.View.iDpi = iDpi;
        }
        ReleaseDC(NULL, Screen);
    }
    App.View.Font = CreateFontW(-ScaleLayout(&App, 16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Malgun Gothic");
    App.View.BoldFont = CreateFontW(-ScaleLayout(&App, 16), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Malgun Gothic");
    /* The eleven-column group table uses a slightly smaller font so that every header fits at 1080 px. */
    App.TableFont = CreateFontW(-ScaleLayout(&App, 15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Malgun Gothic");
    App.TableBoldFont = CreateFontW(-ScaleLayout(&App, 15), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Malgun Gothic");
    App.PreviewView = App.View;
    if ((NULL != App.View.Font) && (NULL != App.View.BoldFont) && (NULL != App.TableFont) && (NULL != App.TableBoldFont) &&
        (0 != RegisterClassW(&Class)) && RegisterPageClass(Instance) && RegisterSlotChart(Instance) && RegisterGroupDialog(Instance)) {
        Window = CreateWindowExW(WS_EX_CONTROLPARENT, gacWindowClass, gacTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
            ScaleLayout(&App, 1160), ScaleLayout(&App, 880), NULL, NULL, Instance, &App);
    }
    if (NULL == Window) {
        MessageBoxW(NULL, L"프로그램 창을 만들 수 없습니다.", gacTitle, MB_OK | MB_ICONERROR);
    }
    else {
        int32_t iMessageResult;
        ShowWindow(Window, iShowCommand);
        UpdateWindow(Window);
        while ((iMessageResult = GetMessageW(&Message, NULL, 0, 0)) > 0) {
            if ((WM_KEYDOWN == Message.message) && (VK_RETURN == Message.wParam) && (Message.hwnd == App.RoundEdit)) {
                HandleCommand(Window, &App, CommandJump, BN_CLICKED);
            }
            else if (FALSE == IsDialogMessageW(Window, &Message)) {
                TranslateMessage(&Message);
                DispatchMessageW(&Message);
            }
        }
        iResult = (0 == iMessageResult) ? (int32_t)Message.wParam : 1;
    }

    if (NULL != App.Worker) {
        InterlockedExchange(&App.pTask->lStop, 1);
        WaitForSingleObject(App.Worker, INFINITE);
        CloseHandle(App.Worker);
    }
    FreeTask(App.pTask);
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        FreeAreaGroupLayout(&App.aPreviewLayouts[uiArea]);
    }
    FreeGroupConfig(&App.Groups);
    if (NULL != App.View.BoldFont) {
        DeleteObject(App.View.BoldFont);
    }
    if (NULL != App.TableFont) {
        DeleteObject(App.TableFont);
    }
    if (NULL != App.TableBoldFont) {
        DeleteObject(App.TableBoldFont);
    }
    if (NULL != App.View.Font) {
        DeleteObject(App.View.Font);
    }
    return iResult;
}
