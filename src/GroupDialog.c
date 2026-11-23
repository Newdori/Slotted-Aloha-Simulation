#include "GroupDialog.h"
#include "Chart.h"

#include <commctrl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef enum DialogCommand {
    DialogArea = 300,
    DialogTerminalApply,
    DialogEqualTerminals,
    DialogEqualSlots,
    DialogAssign,
    DialogRelease,
    DialogReleaseOutside,
    DialogApply,
    DialogCancel,
    DialogClearSlots,
    DialogHelp,
    DialogProportionalSlots
} DialogCommand;

typedef enum RangeField {
    RangeTimeFirst,
    RangeTimeLast,
    RangeRowFirst,
    RangeRowLast,
    RangeFieldCount
} RangeField;

typedef enum SummaryColumn {
    SummaryType,
    SummaryTerminals,
    SummaryTerminalMode,
    SummarySlots,
    SummaryUnassigned,
    SummarySlotMode,
    SummaryCheck,
    SummaryColumnCount
} SummaryColumn;

typedef enum AreaCheck {
    AreaCheckSkipped,
    AreaCheckPassed,
    AreaCheckFailed
} AreaCheck;

typedef struct GroupDialog {
    GroupDialogInput_t Input;
    GroupConfig_t Draft;
    AreaGroupLayout_t aLayouts[AreaCount];
    ChartView_t View;
    AreaCheck aChecks[AreaCount];
    HWND Window;
    HWND AreaLabel;
    HWND AreaSelect;
    HWND HelpButton;
    HWND SummaryLabel;
    HWND SummaryTable;
    HWND GroupStep;
    HWND GroupTable;
    HWND TerminalLabel;
    HWND TerminalEdit;
    HWND TerminalApply;
    HWND EqualTerminals;
    HWND TerminalHint;
    HWND SlotLabel;
    HWND SlotHint;
    HWND aRangeEdits[RangeFieldCount];
    HWND aRangeLabels[RangeFieldCount];
    HWND AssignButton;
    HWND ReleaseButton;
    HWND EqualSlots;
    HWND ProportionalSlots;
    HWND ClearSlots;
    HWND ReleaseOutside;
    HWND Chart;
    HWND Message;
    HWND Issues;
    HWND ApplyButton;
    HWND CancelButton;
    AreaType Area;
    uint32_t uiSelectedGroup;
    uint32_t uiIssueCount;
    int32_t iScrollTop;
    bool bApplied;
    bool bDone;
} GroupDialog_t;

static const wchar_t gacDialogClass[] = L"SlottedAlohaGroupDialog";
static const wchar_t *const gpacDialogAreaNames[AreaCount] = { L"Alert", L"LB" };
static const COLORREF guiHintColor = RGB(96, 108, 122);

static int32_t ScaleDialog(const GroupDialog_t *pDialog, int32_t iValue)
{
    return MulDiv(iValue, pDialog->Input.iDpi, 96);
}

static void SetDialogText(HWND Control, const wchar_t *pcFormat, ...)
{
    wchar_t acText[1024];
    size_t uCapacity = sizeof(acText) / sizeof(acText[0]);
    va_list Arguments;
    int32_t iWritten;

    va_start(Arguments, pcFormat);
    iWritten = vswprintf(acText, uCapacity, pcFormat, Arguments);
    va_end(Arguments);
    if ((iWritten >= 0) && ((size_t)iWritten < uCapacity)) {
        SetWindowTextW(Control, acText);
    }
}

static void SetGroupTableText(HWND Table, int32_t iRow, int32_t iColumn, const wchar_t *pcFormat, ...)
{
    wchar_t acText[96];
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

/* Reads a non-negative integer. An empty field is reported separately so that it can mean "automatic". */
static bool ReadDialogInteger(HWND Control, uint64_t ullMaximum, uint64_t *pullValue, bool *pbEmpty)
{
    wchar_t acText[32] = { 0 };
    const wchar_t *pcDigit = acText;
    uint64_t ullValue = 0;

    GetWindowTextW(Control, acText, (int32_t)(sizeof(acText) / sizeof(acText[0])));
    if (NULL != pbEmpty) {
        *pbEmpty = L'\0' == acText[0];
    }
    while ((*pcDigit >= L'0') && (*pcDigit <= L'9')) {
        uint32_t uiDigit = (uint32_t)(*pcDigit - L'0');
        if ((uiDigit > ullMaximum) || (ullValue > (ullMaximum - uiDigit) / 10)) {
            return false;
        }
        ullValue = ullValue * 10 + uiDigit;
        pcDigit++;
    }
    if ((pcDigit == acText) || (L'\0' != *pcDigit)) {
        return false;
    }
    *pullValue = ullValue;
    return true;
}

static uint32_t GetAreaRows(const GroupDialog_t *pDialog)
{
    const AreaGroupLayout_t *pLayout = &pDialog->aLayouts[pDialog->Area];
    return GetFrequencyRowCount(pLayout->uiSlots, pLayout->uiTimeSlots);
}

static bool IsAreaEditable(const GroupDialog_t *pDialog, AreaType Area)
{
    return IsAreaGroupActive(&pDialog->Input.Config, Area) && (NULL != pDialog->aLayouts[Area].paucOwner);
}

static bool IsEditableArea(const GroupDialog_t *pDialog)
{
    return IsAreaEditable(pDialog, pDialog->Area);
}

static void UpdateGroupTable(GroupDialog_t *pDialog)
{
    const AreaGroupLayout_t *pLayout = &pDialog->aLayouts[pDialog->Area];
    bool bActive = IsEditableArea(pDialog);
    uint64_t ullFirst = 1;

    for (uint32_t uiGroup = 0; uiGroup < pDialog->Input.Config.uiGroupCount; uiGroup++) {
        uint32_t uiTerminals = bActive ? pLayout->auiTerminals[uiGroup] : 0;
        int32_t iRow = (int32_t)uiGroup;

        SetGroupTableText(pDialog->GroupTable, iRow, 0, L"그룹 %u", uiGroup + 1);
        if (!bActive) {
            for (int32_t iColumn = 1; iColumn < 5; iColumn++) {
                SetGroupTableText(pDialog->GroupTable, iRow, iColumn, L"-");
            }
            continue;
        }
        SetGroupTableText(pDialog->GroupTable, iRow, 1, L"%u", uiTerminals);
        SetGroupTableText(pDialog->GroupTable, iRow, 2, L"%ls", pLayout->abFixedTerminals[uiGroup] ? L"직접" : L"자동");
        if (0 == uiTerminals) {
            SetGroupTableText(pDialog->GroupTable, iRow, 3, L"없음");
        }
        else if (1 == uiTerminals) {
            SetGroupTableText(pDialog->GroupTable, iRow, 3, L"%llu", ullFirst);
        }
        else {
            SetGroupTableText(pDialog->GroupTable, iRow, 3, L"%llu–%llu", ullFirst, ullFirst + uiTerminals - 1);
        }
        SetGroupTableText(pDialog->GroupTable, iRow, 4, L"%u", pLayout->auiSlots[uiGroup]);
        ullFirst += uiTerminals;
    }
}

static const wchar_t *GetIssueShortText(const GroupIssue_t *pIssue)
{
    switch (pIssue->Type) {
    case GroupIssueMemory:
        return L"메모리 부족";

    case GroupIssueTerminalSum:
        return (pIssue->ullValue > pIssue->ullLimit) ? L"단말 합계 초과" : L"단말 합계 부족";

    case GroupIssueNoSlot:
        return L"칸 없는 그룹";

    case GroupIssueOutside:
        return L"범위 밖 칸";

    case GroupIssueRemoved:
        return L"없어진 그룹 칸";

    default:
        return L"확인 필요";
    }
}

/* One row per channel type, so both settings are visible while one of them is edited. */
static void UpdateSummaryTable(GroupDialog_t *pDialog)
{
    const SimulationConfig_t *pConfig = &pDialog->Input.Config;

    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        const AreaGroupLayout_t *pLayout = &pDialog->aLayouts[uiArea];
        const AreaGroupConfig_t *pArea = &pDialog->Draft.aAreas[uiArea];
        int32_t iRow = (int32_t)uiArea;
        GroupIssue_t Issue;
        uint32_t uiIssues;

        SetGroupTableText(pDialog->SummaryTable, iRow, SummaryType, L"%ls", gpacDialogAreaNames[uiArea]);
        if (!IsAreaEditable(pDialog, (AreaType)uiArea)) {
            pDialog->aChecks[uiArea] = AreaCheckSkipped;
            SetGroupTableText(pDialog->SummaryTable, iRow, SummaryTerminals, L"채널 0 · 경쟁 없음");
            for (int32_t iColumn = SummaryTerminalMode; iColumn < SummaryCheck; iColumn++) {
                SetGroupTableText(pDialog->SummaryTable, iRow, iColumn, L"-");
            }
            SetGroupTableText(pDialog->SummaryTable, iRow, SummaryCheck, L"검사 제외");
            continue;
        }
        SetGroupTableText(pDialog->SummaryTable, iRow, SummaryTerminals, L"%llu / %u 대", pLayout->ullTerminalSum, pConfig->uiTerminalCount);
        if (0 == pLayout->uiFixedGroups) {
            SetGroupTableText(pDialog->SummaryTable, iRow, SummaryTerminalMode, L"자동 균등");
        }
        else if (pLayout->uiFixedGroups == pLayout->uiGroupCount) {
            SetGroupTableText(pDialog->SummaryTable, iRow, SummaryTerminalMode, L"모두 직접 지정");
        }
        else {
            SetGroupTableText(pDialog->SummaryTable, iRow, SummaryTerminalMode, L"직접 %u개 · 자동 %u개", pLayout->uiFixedGroups, pLayout->uiGroupCount - pLayout->uiFixedGroups);
        }
        SetGroupTableText(pDialog->SummaryTable, iRow, SummarySlots, L"%u / %u 칸", pLayout->uiSlots - pLayout->uiUnassigned, pLayout->uiSlots);
        SetGroupTableText(pDialog->SummaryTable, iRow, SummaryUnassigned, L"%u 칸", pLayout->uiUnassigned);
        SetGroupTableText(pDialog->SummaryTable, iRow, SummarySlotMode, L"%ls", (SlotModeEqual == pArea->SlotAssignment) ? L"자동 균등" : GetSlotModeName(pArea->SlotAssignment));
        uiIssues = CheckAreaGroupLayout(pConfig, pLayout, (AreaType)uiArea, &Issue, 1);
        pDialog->aChecks[uiArea] = (0 == uiIssues) ? AreaCheckPassed : AreaCheckFailed;
        if (0 == uiIssues) {
            SetGroupTableText(pDialog->SummaryTable, iRow, SummaryCheck, L"통과");
        }
        else if (1 == uiIssues) {
            SetGroupTableText(pDialog->SummaryTable, iRow, SummaryCheck, L"⚠ %ls", GetIssueShortText(&Issue));
        }
        else {
            SetGroupTableText(pDialog->SummaryTable, iRow, SummaryCheck, L"⚠ %ls 외 %u건", GetIssueShortText(&Issue), uiIssues - 1);
        }
    }
    ListView_SetItemState(pDialog->SummaryTable, (int32_t)pDialog->Area, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    InvalidateRect(pDialog->SummaryTable, NULL, FALSE);
}

static void UpdateIssues(GroupDialog_t *pDialog)
{
    GroupIssue_t aIssues[4];
    wchar_t acText[1024] = L"";
    size_t uLength = 0;
    uint32_t uiCount = ValidateGroupConfig(&pDialog->Input.Config, &pDialog->Draft, aIssues, 4);

    pDialog->uiIssueCount = uiCount;
    if (0 == uiCount) {
        SetWindowTextW(pDialog->Issues, L"④ 검사 통과 · [적용]을 누르면 다음 실행부터 이 그룹 설정을 사용합니다.");
        return;
    }
    for (uint32_t uiIndex = 0; (uiIndex < uiCount) && (uiIndex < 4); uiIndex++) {
        wchar_t acLine[256];
        if (FormatGroupIssue(&aIssues[uiIndex], acLine, sizeof(acLine) / sizeof(acLine[0]))) {
            int32_t iWritten = swprintf(&acText[uLength], sizeof(acText) / sizeof(acText[0]) - uLength, L"%ls⚠ %ls", (0 == uLength) ? L"" : L"\r\n", acLine);
            if ((iWritten < 0) || ((size_t)iWritten >= sizeof(acText) / sizeof(acText[0]) - uLength)) {
                break;
            }
            uLength += (size_t)iWritten;
        }
    }
    if (uiCount > 4) {
        int32_t iWritten = swprintf(&acText[uLength], sizeof(acText) / sizeof(acText[0]) - uLength, L"  외 %u건", uiCount - 4);
        if ((iWritten < 0) || ((size_t)iWritten >= sizeof(acText) / sizeof(acText[0]) - uLength)) {
            acText[uLength] = L'\0';
        }
    }
    SetWindowTextW(pDialog->Issues, acText);
}

static void UpdateDialogControls(GroupDialog_t *pDialog)
{
    const AreaGroupLayout_t *pLayout = &pDialog->aLayouts[pDialog->Area];
    bool bActive = IsEditableArea(pDialog);
    bool bLost = bActive && ((0 != pLayout->uiOutside) || (0 != pLayout->uiRemoved));
    const HWND aControls[] = { pDialog->TerminalEdit, pDialog->TerminalApply, pDialog->EqualTerminals, pDialog->EqualSlots, pDialog->ProportionalSlots, pDialog->ClearSlots,
        pDialog->AssignButton, pDialog->ReleaseButton, pDialog->aRangeEdits[0], pDialog->aRangeEdits[1], pDialog->aRangeEdits[2], pDialog->aRangeEdits[3] };

    for (uint32_t uiIndex = 0; uiIndex < sizeof(aControls) / sizeof(aControls[0]); uiIndex++) {
        EnableWindow(aControls[uiIndex], bActive);
    }
    ShowWindow(pDialog->ReleaseOutside, bLost ? SW_SHOW : SW_HIDE);
    if (bLost) {
        SetDialogText(pDialog->ReleaseOutside, L"범위 밖 %u칸 해제", pLayout->uiOutside + pLayout->uiRemoved);
    }
    SetDialogText(pDialog->GroupStep, L"① 그룹 선택 (%ls)", gpacDialogAreaNames[pDialog->Area]);
    SetDialogText(pDialog->TerminalLabel, L"② 그룹 %u 단말 수", pDialog->uiSelectedGroup + 1);
    if (bActive) {
        SetDialogText(pDialog->SlotLabel, L"③ 그룹 %u 슬롯 배정  (시간 1~%u, 주파수 1~%u)", pDialog->uiSelectedGroup + 1, pLayout->uiTimeSlots, GetAreaRows(pDialog));
    }
    else {
        SetDialogText(pDialog->SlotLabel, L"③ 그룹 %u 슬롯 배정", pDialog->uiSelectedGroup + 1);
    }
}

static bool RefreshDialog(GroupDialog_t *pDialog)
{
    bool bResolved = true;

    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        FreeAreaGroupLayout(&pDialog->aLayouts[uiArea]);
        if (!ResolveAreaGroups(&pDialog->Input.Config, &pDialog->Draft, (AreaType)uiArea, &pDialog->aLayouts[uiArea])) {
            bResolved = false;
        }
        pDialog->View.paucGroups[uiArea] = pDialog->aLayouts[uiArea].paucOwner;
    }
    pDialog->View.EditArea = pDialog->Area;
    pDialog->View.uiSelectedGroup = pDialog->uiSelectedGroup;
    pDialog->View.bGroupSelected = true;
    UpdateSummaryTable(pDialog);
    UpdateGroupTable(pDialog);
    UpdateIssues(pDialog);
    UpdateDialogControls(pDialog);
    if (IsEditableArea(pDialog)) {
        SetDialogText(pDialog->TerminalEdit, L"%u", pDialog->aLayouts[pDialog->Area].auiTerminals[pDialog->uiSelectedGroup]);
    }
    else {
        SetWindowTextW(pDialog->TerminalEdit, L"");
    }
    RefreshSlotChart(pDialog->Chart, &pDialog->View);
    if (!bResolved) {
        SetWindowTextW(pDialog->Message, L"그룹 배정을 표시할 메모리가 부족합니다.");
    }
    return bResolved;
}

static void SelectDialogArea(GroupDialog_t *pDialog, AreaType Area)
{
    if (Area == pDialog->Area) {
        return;
    }
    pDialog->Area = Area;
    SendMessageW(pDialog->AreaSelect, CB_SETCURSEL, (WPARAM)Area, 0);
    pDialog->View.uiTopRow = (AreaLb == Area) ? GetFrequencyRowCount(pDialog->Input.Config.auiSlots[AreaAlert], pDialog->View.uiTimeSlots) : 0;
    RefreshDialog(pDialog);
    if (!IsEditableArea(pDialog)) {
        SetDialogText(pDialog->Message, L"%ls 채널 수가 0이라 경쟁하지 않습니다. 그룹 검사에서도 제외합니다.", gpacDialogAreaNames[Area]);
    }
    else {
        SetDialogText(pDialog->Message, L"%ls 채널 타입을 편집합니다. Alert와 LB는 따로 설정합니다.", gpacDialogAreaNames[Area]);
    }
}

static bool PrepareSlotEdit(GroupDialog_t *pDialog)
{
    AreaGroupConfig_t *pArea = &pDialog->Draft.aAreas[pDialog->Area];
    const AreaGroupLayout_t *pLayout = &pDialog->aLayouts[pDialog->Area];
    uint32_t uiLost = pLayout->uiOutside + pLayout->uiRemoved;

    if ((SlotModeManual == pArea->SlotAssignment) && (0 == uiLost) && (pArea->uiGridTimeSlots == pLayout->uiTimeSlots) && (pArea->uiGridSlots == pLayout->uiSlots)) {
        return true;
    }
    if (0 != uiLost) {
        wchar_t acText[256];
        int32_t iWritten = swprintf(acText, sizeof(acText) / sizeof(acText[0]),
            L"현재 격자 범위를 벗어난 칸 %u개와 없어진 그룹의 칸 %u개는 편집하려면 배정을 해제해야 합니다.\r\n해제하고 계속할까요?",
            pLayout->uiOutside, pLayout->uiRemoved);
        if ((iWritten < 0) || ((size_t)iWritten >= sizeof(acText) / sizeof(acText[0])) ||
            (IDYES != MessageBoxW(pDialog->Window, acText, L"그룹 설정", MB_YESNO | MB_ICONQUESTION))) {
            return false;
        }
    }
    if (!ConvertManualSlots(pArea, &pDialog->Input.Config, &pDialog->Draft, pDialog->Area)) {
        SetWindowTextW(pDialog->Message, L"슬롯 배정을 편집할 메모리가 부족합니다.");
        return false;
    }
    return true;
}

static void ApplySlotRect(GroupDialog_t *pDialog, const SlotRect_t *pRect, bool bRelease)
{
    AreaGroupConfig_t *pArea = &pDialog->Draft.aAreas[pDialog->Area];
    uint64_t ullChanged;
    uint64_t ullSkipped;

    if (!IsEditableArea(pDialog) || !PrepareSlotEdit(pDialog)) {
        return;
    }
    EditSlotRect(pArea, pDialog->aLayouts[pDialog->Area].uiTimeSlots, pRect, pDialog->uiSelectedGroup, bRelease, &ullChanged, &ullSkipped);
    RefreshDialog(pDialog);
    if (0 == ullSkipped) {
        SetDialogText(pDialog->Message, L"%ls 그룹 %u: %llu칸 %ls", gpacDialogAreaNames[pDialog->Area], pDialog->uiSelectedGroup + 1, ullChanged, bRelease ? L"해제" : L"배정");
    }
    else {
        SetDialogText(pDialog->Message, L"%ls 그룹 %u: %llu칸 %ls · 다른 그룹의 칸 %llu개는 건너뜀 (먼저 그 그룹에서 해제하거나 [전체 배정 해제])",
            gpacDialogAreaNames[pDialog->Area], pDialog->uiSelectedGroup + 1, ullChanged, bRelease ? L"해제" : L"배정", ullSkipped);
    }
}

static void ApplyRangeInput(GroupDialog_t *pDialog, bool bRelease)
{
    const AreaGroupLayout_t *pLayout = &pDialog->aLayouts[pDialog->Area];
    uint64_t aullValues[RangeFieldCount];
    uint32_t auiLimits[RangeFieldCount] = { pLayout->uiTimeSlots, pLayout->uiTimeSlots, GetAreaRows(pDialog), GetAreaRows(pDialog) };
    SlotRect_t Rect;

    for (uint32_t uiField = 0; uiField < RangeFieldCount; uiField++) {
        if (!ReadDialogInteger(pDialog->aRangeEdits[uiField], auiLimits[uiField], &aullValues[uiField], NULL) || (0 == aullValues[uiField])) {
            SetDialogText(pDialog->Message, L"시간은 1~%u, 주파수는 1~%u 사이의 위치를 입력하세요. 주파수 1은 격자의 맨 아래 줄입니다.", pLayout->uiTimeSlots, GetAreaRows(pDialog));
            SetFocus(pDialog->aRangeEdits[uiField]);
            return;
        }
    }
    if ((aullValues[RangeTimeFirst] > aullValues[RangeTimeLast]) || (aullValues[RangeRowFirst] > aullValues[RangeRowLast])) {
        SetWindowTextW(pDialog->Message, L"범위의 시작 위치가 끝 위치보다 클 수 없습니다.");
        return;
    }
    Rect = (SlotRect_t){ (uint32_t)aullValues[RangeTimeFirst] - 1, (uint32_t)aullValues[RangeTimeLast] - 1,
        (uint32_t)aullValues[RangeRowFirst] - 1, (uint32_t)aullValues[RangeRowLast] - 1 };
    ApplySlotRect(pDialog, &Rect, bRelease);
}

static void HandleChartSelection(GroupDialog_t *pDialog, const SlotSelection_t *pSelection)
{
    const AreaGroupLayout_t *pLayout = &pDialog->aLayouts[pDialog->Area];
    uint64_t ullStart = (uint64_t)pSelection->uiStartRow * pLayout->uiTimeSlots + pSelection->uiStartTime;
    bool bRelease = (ullStart < pLayout->uiSlots) && (NULL != pLayout->paucOwner) && (pDialog->uiSelectedGroup == pLayout->paucOwner[ullStart]);
    SlotRect_t Rect = {
        (pSelection->uiStartTime < pSelection->uiEndTime) ? pSelection->uiStartTime : pSelection->uiEndTime,
        (pSelection->uiStartTime < pSelection->uiEndTime) ? pSelection->uiEndTime : pSelection->uiStartTime,
        (pSelection->uiStartRow < pSelection->uiEndRow) ? pSelection->uiStartRow : pSelection->uiEndRow,
        (pSelection->uiStartRow < pSelection->uiEndRow) ? pSelection->uiEndRow : pSelection->uiStartRow
    };

    if (pSelection->Area != pDialog->Area) {
        return;
    }
    SetDialogText(pDialog->aRangeEdits[RangeTimeFirst], L"%u", Rect.uiTimeFirst + 1);
    SetDialogText(pDialog->aRangeEdits[RangeTimeLast], L"%u", Rect.uiTimeLast + 1);
    SetDialogText(pDialog->aRangeEdits[RangeRowFirst], L"%u", Rect.uiRowFirst + 1);
    SetDialogText(pDialog->aRangeEdits[RangeRowLast], L"%u", Rect.uiRowLast + 1);
    ApplySlotRect(pDialog, &Rect, bRelease);
}

static void ApplyTerminalCount(GroupDialog_t *pDialog)
{
    AreaGroupConfig_t *pArea = &pDialog->Draft.aAreas[pDialog->Area];
    const AreaGroupLayout_t *pLayout;
    uint32_t uiGroup = pDialog->uiSelectedGroup;
    uint64_t ullCount = 0;
    bool bEmpty = false;

    if (!IsEditableArea(pDialog)) {
        return;
    }
    if (!ReadDialogInteger(pDialog->TerminalEdit, UINT32_MAX, &ullCount, &bEmpty) && !bEmpty) {
        SetWindowTextW(pDialog->Message, L"단말 수에는 0 이상의 정수를 입력하세요. 비워 두면 자동으로 돌아갑니다.");
        SetFocus(pDialog->TerminalEdit);
        return;
    }
    if (bEmpty) {
        ReleaseGroupTerminalCount(pArea, uiGroup);
    }
    else {
        FixGroupTerminalCount(pArea, uiGroup, (uint32_t)ullCount);
    }
    RefreshDialog(pDialog);
    pLayout = &pDialog->aLayouts[pDialog->Area];
    if (pLayout->ullTerminalSum != pDialog->Input.Config.uiTerminalCount) {
        SetDialogText(pDialog->Message, L"%ls 그룹 %u 단말 수를 바꿨지만 합계 %llu대가 단말 수 %u대와 다릅니다. 직접 지정한 값을 줄이거나 다른 그룹을 자동으로 두세요.",
            gpacDialogAreaNames[pDialog->Area], uiGroup + 1, pLayout->ullTerminalSum, pDialog->Input.Config.uiTerminalCount);
    }
    else if (bEmpty) {
        SetDialogText(pDialog->Message, L"%ls 그룹 %u을 자동으로 되돌렸습니다. 자동 그룹이 남은 단말을 나눠 갖습니다.", gpacDialogAreaNames[pDialog->Area], uiGroup + 1);
    }
    else {
        SetDialogText(pDialog->Message, L"%ls 그룹 %u을 %llu대로 고정했습니다. 나머지 자동 그룹이 남은 단말을 나누고, 단말 ID는 그룹 순서대로 다시 이어집니다.",
            gpacDialogAreaNames[pDialog->Area], uiGroup + 1, ullCount);
    }
}

static void ResetSlots(GroupDialog_t *pDialog)
{
    AreaGroupConfig_t *pArea = &pDialog->Draft.aAreas[pDialog->Area];

    if ((SlotModeManual == pArea->SlotAssignment) &&
        (IDYES != MessageBoxW(pDialog->Window, L"직접 지정한 슬롯 배정을 모두 지우고 그룹 수만큼 균등 배정할까요?", L"그룹 설정", MB_YESNO | MB_ICONQUESTION))) {
        return;
    }
    ResetGroupSlots(pArea);
    RefreshDialog(pDialog);
    SetDialogText(pDialog->Message, L"%ls 슬롯을 그룹 순서대로 균등 배정했습니다. 주파수 1(맨 아래 줄)의 시간 1부터 채웁니다.", gpacDialogAreaNames[pDialog->Area]);
}

static void ApplyProportionalSlots(GroupDialog_t *pDialog)
{
    AreaGroupConfig_t *pArea = &pDialog->Draft.aAreas[pDialog->Area];

    if (!IsEditableArea(pDialog)) {
        return;
    }
    if ((SlotModeManual == pArea->SlotAssignment) &&
        (IDYES != MessageBoxW(pDialog->Window, L"직접 지정한 슬롯 배정을 모두 지우고 그룹별 단말 수에 비례해 다시 나눌까요?", L"그룹 설정", MB_YESNO | MB_ICONQUESTION))) {
        return;
    }
    SetProportionalSlots(pArea);
    RefreshDialog(pDialog);
    SetDialogText(pDialog->Message, L"%ls 슬롯을 그룹별 단말 수에 비례해 나눴습니다. 단말 수를 바꾸면 자동으로 다시 나눕니다.", gpacDialogAreaNames[pDialog->Area]);
}

static void ClearAllSlots(GroupDialog_t *pDialog)
{
    const AreaGroupLayout_t *pLayout = &pDialog->aLayouts[pDialog->Area];
    wchar_t acText[256];
    int32_t iWritten;

    if (!IsEditableArea(pDialog)) {
        return;
    }
    iWritten = swprintf(acText, sizeof(acText) / sizeof(acText[0]),
        L"%ls의 모든 그룹 슬롯 배정(%u칸)을 해제할까요?\r\n해제한 뒤 그룹을 하나씩 선택해 격자에 다시 배정합니다.",
        gpacDialogAreaNames[pDialog->Area], pLayout->uiSlots - pLayout->uiUnassigned);
    if ((iWritten < 0) || ((size_t)iWritten >= sizeof(acText) / sizeof(acText[0])) ||
        (IDYES != MessageBoxW(pDialog->Window, acText, L"그룹 설정", MB_YESNO | MB_ICONQUESTION))) {
        return;
    }
    if (!ClearGroupSlots(&pDialog->Draft.aAreas[pDialog->Area], pLayout->uiTimeSlots, pLayout->uiSlots)) {
        SetWindowTextW(pDialog->Message, L"슬롯 배정을 편집할 메모리가 부족합니다.");
        return;
    }
    RefreshDialog(pDialog);
    SetDialogText(pDialog->Message, L"%ls의 모든 슬롯 배정을 해제했습니다. 그룹을 선택하고 격자를 드래그해 배정하세요.", gpacDialogAreaNames[pDialog->Area]);
}

static void ReleaseLostSlots(GroupDialog_t *pDialog)
{
    const AreaGroupLayout_t *pLayout = &pDialog->aLayouts[pDialog->Area];
    uint32_t uiLost = pLayout->uiOutside + pLayout->uiRemoved;

    if ((0 == uiLost) || (IDYES != MessageBoxW(pDialog->Window, L"현재 격자 범위를 벗어났거나 없어진 그룹에 배정된 칸을 해제할까요?", L"그룹 설정", MB_YESNO | MB_ICONQUESTION))) {
        return;
    }
    if (ConvertManualSlots(&pDialog->Draft.aAreas[pDialog->Area], &pDialog->Input.Config, &pDialog->Draft, pDialog->Area)) {
        RefreshDialog(pDialog);
        SetDialogText(pDialog->Message, L"%ls: 배정 %u칸을 해제했습니다.", gpacDialogAreaNames[pDialog->Area], uiLost);
    }
    else {
        SetWindowTextW(pDialog->Message, L"슬롯 배정을 편집할 메모리가 부족합니다.");
    }
}

static void ShowDialogHelp(const GroupDialog_t *pDialog)
{
    MessageBoxW(pDialog->Window,
        L"먼저 위쪽 [채널 타입]에서 Alert 또는 LB를 고릅니다.\r\n"
        L"두 채널 타입은 따로 설정합니다.\r\n\r\n"
        L"① 그룹 선택\r\n"
        L"  · 왼쪽 표에서 바꿀 그룹을 클릭합니다.\r\n\r\n"
        L"② 단말 수\r\n"
        L"  · 숫자를 입력하고 [단말 수 변경] 또는 Enter.\r\n"
        L"  · 바꾼 그룹은 그 값으로 고정(직접)됩니다.\r\n"
        L"  · 나머지(자동) 그룹이 남은 단말을 나눕니다.\r\n"
        L"  · 값을 지우고 변경하면 자동으로 돌아갑니다.\r\n"
        L"  · [단말 균등 분배]: 모든 그룹을 자동으로.\r\n\r\n"
        L"③ 슬롯 배정\r\n"
        L"  · 격자를 클릭·드래그하면 선택 그룹에 배정.\r\n"
        L"  · 선택 그룹 칸에서 시작하면 해제.\r\n"
        L"  · 다른 그룹 칸은 바뀌지 않습니다.\r\n"
        L"  · 새로 나누려면 [전체 배정 해제] 후 배정.\r\n"
        L"  · [슬롯 균등 배정]: 그룹 순서대로 똑같이.\r\n"
        L"  · [단말 수 비례 배정]: 단말 수 비율대로 나눔.\r\n"
        L"    그룹마다 칸당 단말 수가 같아집니다.\r\n"
        L"  · 시간·주파수 범위를 입력해도 됩니다.\r\n\r\n"
        L"④ 적용\r\n"
        L"  · '검사 통과'가 보이면 [적용]을 누릅니다.\r\n\r\n"
        L"격자의 맨 아래 줄 = 주파수 1, 왼쪽 끝 = 시간 1",
        L"그룹 설정 방법", MB_OK | MB_ICONINFORMATION);
}

static void ApplyDialog(GroupDialog_t *pDialog)
{
    GroupIssue_t aIssues[8];
    wchar_t acText[2048] = L"다음 문제를 해결한 뒤 적용하세요.\r\n";
    size_t uLength = wcslen(acText);
    uint32_t uiCount = ValidateGroupConfig(&pDialog->Input.Config, &pDialog->Draft, aIssues, 8);

    if (0 == uiCount) {
        pDialog->bApplied = true;
        pDialog->bDone = true;
        return;
    }
    for (uint32_t uiIndex = 0; (uiIndex < uiCount) && (uiIndex < 8); uiIndex++) {
        wchar_t acLine[256];
        if (FormatGroupIssue(&aIssues[uiIndex], acLine, sizeof(acLine) / sizeof(acLine[0]))) {
            int32_t iWritten = swprintf(&acText[uLength], sizeof(acText) / sizeof(acText[0]) - uLength, L"\r\n• %ls", acLine);
            if ((iWritten < 0) || ((size_t)iWritten >= sizeof(acText) / sizeof(acText[0]) - uLength)) {
                break;
            }
            uLength += (size_t)iWritten;
        }
    }
    MessageBoxW(pDialog->Window, acText, L"그룹 설정", MB_OK | MB_ICONWARNING);
}

static HWND CreateDialogControl(GroupDialog_t *pDialog, const wchar_t *pcClass, const wchar_t *pcText, DWORD uiStyle, int32_t iId)
{
    HWND Control = CreateWindowExW(0, pcClass, pcText, WS_CHILD | WS_VISIBLE | uiStyle, 0, 0, 1, 1, pDialog->Window,
        (HMENU)(INT_PTR)iId, pDialog->Input.Instance, NULL);

    if (NULL != Control) {
        SendMessageW(Control, WM_SETFONT, (WPARAM)pDialog->Input.Font, FALSE);
    }
    return Control;
}

static HWND CreateDialogTable(GroupDialog_t *pDialog, const wchar_t *const pacHeaders[], uint32_t uiColumns, uint32_t uiRows, DWORD uiStyle)
{
    HWND Table = CreateDialogControl(pDialog, WC_LISTVIEWW, L"", WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER | uiStyle, 0);

    if (NULL == Table) {
        return NULL;
    }
    ListView_SetExtendedListViewStyle(Table, LVS_EX_GRIDLINES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetTextColor(Table, RGB(31, 45, 61));
    SendMessageW(ListView_GetHeader(Table), WM_SETFONT, (WPARAM)pDialog->Input.BoldFont, FALSE);
    for (uint32_t uiColumn = 0; uiColumn < uiColumns; uiColumn++) {
        LVCOLUMNW Column = { .mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT, .fmt = LVCFMT_LEFT, .cx = ScaleDialog(pDialog, 100),
            .pszText = (wchar_t *)pacHeaders[uiColumn] };
        if (-1 == ListView_InsertColumn(Table, (int32_t)uiColumn, &Column)) {
            return NULL;
        }
    }
    for (uint32_t uiRow = 0; uiRow < uiRows; uiRow++) {
        LVITEMW Item = { .mask = LVIF_TEXT, .iItem = (int32_t)uiRow, .pszText = L"" };
        if (-1 == ListView_InsertItem(Table, &Item)) {
            return NULL;
        }
    }
    return Table;
}

static bool CreateDialogControls(GroupDialog_t *pDialog)
{
    const wchar_t *const pacGroupHeaders[] = { L"그룹", L"단말 수", L"지정", L"단말 ID", L"배정 칸 수" };
    const wchar_t *const pacSummaryHeaders[SummaryColumnCount] = { L"채널 타입", L"단말 수 (합계 / 전체)", L"단말 배정", L"배정 칸 / 전체 칸", L"미배정", L"슬롯 배정", L"검사" };
    const wchar_t *const pacRangeLabels[RangeFieldCount] = { L"시간", L"~", L"주파수", L"~" };
    DWORD uiStatic = SS_LEFT | SS_NOPREFIX;
    DWORD uiEdit = WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP;
    DWORD uiButton = BS_PUSHBUTTON | WS_TABSTOP;

    pDialog->AreaLabel = CreateDialogControl(pDialog, L"STATIC", L"채널 타입", uiStatic, 0);
    pDialog->AreaSelect = CreateDialogControl(pDialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, DialogArea);
    pDialog->HelpButton = CreateDialogControl(pDialog, L"BUTTON", L"사용 방법", uiButton, DialogHelp);
    pDialog->SummaryLabel = CreateDialogControl(pDialog, L"STATIC", L"그룹 설정 결과", uiStatic, 0);
    pDialog->SummaryTable = CreateDialogTable(pDialog, pacSummaryHeaders, SummaryColumnCount, AreaCount, 0);
    pDialog->GroupStep = CreateDialogControl(pDialog, L"STATIC", L"", uiStatic, 0);
    pDialog->GroupTable = CreateDialogTable(pDialog, pacGroupHeaders, 5, pDialog->Input.Config.uiGroupCount, WS_TABSTOP);
    pDialog->TerminalLabel = CreateDialogControl(pDialog, L"STATIC", L"", uiStatic, 0);
    pDialog->TerminalEdit = CreateDialogControl(pDialog, L"EDIT", L"", uiEdit, 0);
    pDialog->TerminalApply = CreateDialogControl(pDialog, L"BUTTON", L"단말 수 변경", uiButton, DialogTerminalApply);
    pDialog->EqualTerminals = CreateDialogControl(pDialog, L"BUTTON", L"단말 균등 분배", uiButton, DialogEqualTerminals);
    pDialog->TerminalHint = CreateDialogControl(pDialog, L"STATIC",
        L"바꾼 그룹만 고정되고 나머지 자동 그룹이 남은 단말을 나눕니다. 값을 지우고 변경하면 자동으로 돌아갑니다.", uiStatic, 0);
    pDialog->SlotLabel = CreateDialogControl(pDialog, L"STATIC", L"", uiStatic | SS_ENDELLIPSIS, 0);
    pDialog->SlotHint = CreateDialogControl(pDialog, L"STATIC",
        L"격자를 클릭·드래그하면 배정, 선택 그룹 칸에서 시작하면 해제합니다. 맨 아래 줄이 주파수 1입니다.", uiStatic, 0);
    for (uint32_t uiField = 0; uiField < RangeFieldCount; uiField++) {
        pDialog->aRangeLabels[uiField] = CreateDialogControl(pDialog, L"STATIC", pacRangeLabels[uiField], SS_CENTER | SS_NOPREFIX, 0);
        pDialog->aRangeEdits[uiField] = CreateDialogControl(pDialog, L"EDIT", L"1", uiEdit, 0);
        if ((NULL == pDialog->aRangeLabels[uiField]) || (NULL == pDialog->aRangeEdits[uiField])) {
            return false;
        }
        SendMessageW(pDialog->aRangeEdits[uiField], EM_SETLIMITTEXT, 10, 0);
    }
    pDialog->AssignButton = CreateDialogControl(pDialog, L"BUTTON", L"선택 그룹에 배정", uiButton, DialogAssign);
    pDialog->ReleaseButton = CreateDialogControl(pDialog, L"BUTTON", L"선택 그룹 배정 해제", uiButton, DialogRelease);
    pDialog->EqualSlots = CreateDialogControl(pDialog, L"BUTTON", L"슬롯 균등 배정", uiButton, DialogEqualSlots);
    pDialog->ProportionalSlots = CreateDialogControl(pDialog, L"BUTTON", L"단말 수 비례 배정", uiButton, DialogProportionalSlots);
    pDialog->ClearSlots = CreateDialogControl(pDialog, L"BUTTON", L"전체 배정 해제", uiButton, DialogClearSlots);
    pDialog->ReleaseOutside = CreateDialogControl(pDialog, L"BUTTON", L"", uiButton, DialogReleaseOutside);
    pDialog->Chart = CreateSlotChart(pDialog->Window, pDialog->Input.Instance, &pDialog->View);
    pDialog->Message = CreateDialogControl(pDialog, L"STATIC",
        L"① 그룹을 고른 뒤 ② 단말 수를 바꾸거나 ③ 격자에서 칸을 배정하세요. 자세한 순서는 [사용 방법]에 있습니다.", uiStatic | SS_ENDELLIPSIS, 0);
    pDialog->Issues = CreateDialogControl(pDialog, L"STATIC", L"", uiStatic, 0);
    pDialog->ApplyButton = CreateDialogControl(pDialog, L"BUTTON", L"적용", BS_PUSHBUTTON | WS_TABSTOP, DialogApply);
    pDialog->CancelButton = CreateDialogControl(pDialog, L"BUTTON", L"취소", BS_PUSHBUTTON | WS_TABSTOP, DialogCancel);
    if ((NULL == pDialog->AreaLabel) || (NULL == pDialog->AreaSelect) || (NULL == pDialog->HelpButton) || (NULL == pDialog->SummaryLabel) ||
        (NULL == pDialog->SummaryTable) || (NULL == pDialog->GroupStep) || (NULL == pDialog->GroupTable) ||
        (NULL == pDialog->TerminalLabel) || (NULL == pDialog->TerminalEdit) || (NULL == pDialog->TerminalApply) ||
        (NULL == pDialog->EqualTerminals) || (NULL == pDialog->TerminalHint) || (NULL == pDialog->SlotLabel) || (NULL == pDialog->SlotHint) ||
        (NULL == pDialog->AssignButton) || (NULL == pDialog->ReleaseButton) || (NULL == pDialog->EqualSlots) || (NULL == pDialog->ProportionalSlots) || (NULL == pDialog->ClearSlots) ||
        (NULL == pDialog->ReleaseOutside) || (NULL == pDialog->Chart) || (NULL == pDialog->Message) || (NULL == pDialog->Issues) ||
        (NULL == pDialog->ApplyButton) || (NULL == pDialog->CancelButton)) {
        return false;
    }

    SendMessageW(pDialog->AreaSelect, CB_ADDSTRING, 0, (LPARAM)L"Alert");
    SendMessageW(pDialog->AreaSelect, CB_ADDSTRING, 0, (LPARAM)L"LB");
    SendMessageW(pDialog->AreaSelect, CB_SETCURSEL, (WPARAM)pDialog->Area, 0);
    SendMessageW(pDialog->AreaSelect, CB_SETITEMHEIGHT, (WPARAM)-1, ScaleDialog(pDialog, 25));
    SendMessageW(pDialog->TerminalEdit, EM_SETLIMITTEXT, 10, 0);
    {
        const HWND aBold[] = { pDialog->AreaLabel, pDialog->SummaryLabel, pDialog->GroupStep, pDialog->TerminalLabel, pDialog->SlotLabel };
        for (uint32_t uiIndex = 0; uiIndex < sizeof(aBold) / sizeof(aBold[0]); uiIndex++) {
            SendMessageW(aBold[uiIndex], WM_SETFONT, (WPARAM)pDialog->Input.BoldFont, FALSE);
        }
    }
    ListView_SetItemState(pDialog->GroupTable, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    return true;
}

/* The fixed layout needs this client height; a shorter dialog scrolls instead of overlapping its controls. */
#define DIALOG_LAYOUT_HEIGHT 760

/* Controls are placed in layout coordinates and shifted by the scroll position. */
static void PlaceDialogControl(const GroupDialog_t *pDialog, HWND Control, int32_t iX, int32_t iY, int32_t iWidth, int32_t iHeight)
{
    MoveWindow(Control, iX, iY - pDialog->iScrollTop, iWidth, iHeight, FALSE);
}

static void UpdateDialogScroll(const GroupDialog_t *pDialog, int32_t iVisible)
{
    int32_t iLayout = ScaleDialog(pDialog, DIALOG_LAYOUT_HEIGHT);
    SCROLLINFO Scroll = { .cbSize = sizeof(Scroll), .fMask = SIF_RANGE | SIF_PAGE, .nMin = 0, .nMax = ((iVisible < iLayout) ? iLayout : iVisible) - 1, .nPage = (uint32_t)iVisible };

    SetScrollInfo(pDialog->Window, SB_VERT, &Scroll, TRUE);
}
static void LayoutDialog(GroupDialog_t *pDialog)
{
    RECT Client;
    int32_t iMargin = ScaleDialog(pDialog, 16);
    int32_t iGap = ScaleDialog(pDialog, 10);
    int32_t iField = ScaleDialog(pDialog, 30);
    int32_t iLabel = ScaleDialog(pDialog, 23);
    int32_t iHint = ScaleDialog(pDialog, 44);
    int32_t iEditWidth = ScaleDialog(pDialog, 58);
    int32_t iSmallLabel = ScaleDialog(pDialog, 22);
    int32_t iSummaryTop = ScaleDialog(pDialog, 82);
    int32_t iSummaryHeight = ScaleDialog(pDialog, 84);
    int32_t iStepTop = ScaleDialog(pDialog, 178);
    int32_t iControlTop = iStepTop + ScaleDialog(pDialog, 26);
    int32_t iChartTop = ScaleDialog(pDialog, 478);
    int32_t iWidth;
    int32_t iTableWidth;
    int32_t iRight;
    int32_t iRightWidth;
    int32_t iBottom;
    int32_t iX;
    int32_t iY;

    GetClientRect(pDialog->Window, &Client);
    UpdateDialogScroll(pDialog, Client.bottom);
    GetClientRect(pDialog->Window, &Client);
    pDialog->iScrollTop = GetScrollPos(pDialog->Window, SB_VERT);
    Client.bottom = (Client.bottom < ScaleDialog(pDialog, DIALOG_LAYOUT_HEIGHT)) ? ScaleDialog(pDialog, DIALOG_LAYOUT_HEIGHT) : Client.bottom;
    iWidth = Client.right - 2 * iMargin;
    iBottom = Client.bottom - iMargin;
    iTableWidth = iWidth * 44 / 100;
    iRight = iMargin + iTableWidth + 2 * iGap;
    iRightWidth = Client.right - iMargin - iRight;

    PlaceDialogControl(pDialog, pDialog->AreaLabel, iMargin, iMargin + ScaleDialog(pDialog, 4), ScaleDialog(pDialog, 84), iLabel);
    PlaceDialogControl(pDialog, pDialog->AreaSelect, iMargin + ScaleDialog(pDialog, 88), iMargin, ScaleDialog(pDialog, 110), ScaleDialog(pDialog, 120));
    PlaceDialogControl(pDialog, pDialog->HelpButton, Client.right - iMargin - ScaleDialog(pDialog, 110), iMargin, ScaleDialog(pDialog, 110), iField);
    PlaceDialogControl(pDialog, pDialog->SummaryLabel, iMargin, ScaleDialog(pDialog, 58), iWidth, iLabel);
    PlaceDialogControl(pDialog, pDialog->SummaryTable, iMargin, iSummaryTop, iWidth, iSummaryHeight);
    {
        const int32_t aiWeights[SummaryColumnCount] = { 10, 18, 16, 15, 9, 11, 21 };
        for (int32_t iColumn = 0; iColumn < SummaryColumnCount; iColumn++) {
            ListView_SetColumnWidth(pDialog->SummaryTable, iColumn, (iWidth - ScaleDialog(pDialog, 6)) * aiWeights[iColumn] / 100);
        }
    }

    PlaceDialogControl(pDialog, pDialog->GroupStep, iMargin, iStepTop, iTableWidth, iLabel);
    PlaceDialogControl(pDialog, pDialog->GroupTable, iMargin, iControlTop, iTableWidth, iChartTop - iControlTop - ScaleDialog(pDialog, 14));
    {
        const int32_t aiWeights[] = { 16, 18, 12, 27, 27 };
        for (int32_t iColumn = 0; iColumn < 5; iColumn++) {
            ListView_SetColumnWidth(pDialog->GroupTable, iColumn, (iTableWidth - ScaleDialog(pDialog, 24)) * aiWeights[iColumn] / 100);
        }
    }

    iY = iStepTop;
    PlaceDialogControl(pDialog, pDialog->TerminalLabel, iRight, iY, iRightWidth, iLabel);
    iY = iControlTop;
    PlaceDialogControl(pDialog, pDialog->TerminalEdit, iRight, iY, ScaleDialog(pDialog, 90), iField);
    PlaceDialogControl(pDialog, pDialog->TerminalApply, iRight + ScaleDialog(pDialog, 90) + iGap, iY, ScaleDialog(pDialog, 120), iField);
    PlaceDialogControl(pDialog, pDialog->EqualTerminals, iRight + ScaleDialog(pDialog, 210) + 2 * iGap, iY, ScaleDialog(pDialog, 130), iField);
    iY += iField + ScaleDialog(pDialog, 4);
    PlaceDialogControl(pDialog, pDialog->TerminalHint, iRight, iY, iRightWidth, iHint);
    iY += iHint + ScaleDialog(pDialog, 6);
    PlaceDialogControl(pDialog, pDialog->SlotLabel, iRight, iY, iRightWidth, iLabel);
    iY += iLabel + ScaleDialog(pDialog, 1);
    PlaceDialogControl(pDialog, pDialog->SlotHint, iRight, iY, iRightWidth, iHint);
    iY += iHint + ScaleDialog(pDialog, 4);
    iX = iRight;
    for (uint32_t uiField = 0; uiField < RangeFieldCount; uiField++) {
        int32_t iLabelWidth = (0 == uiField % 2) ? ScaleDialog(pDialog, 60) : iSmallLabel;
        PlaceDialogControl(pDialog, pDialog->aRangeLabels[uiField], iX, iY + ScaleDialog(pDialog, 4), iLabelWidth, iLabel);
        iX += iLabelWidth;
        PlaceDialogControl(pDialog, pDialog->aRangeEdits[uiField], iX, iY, iEditWidth, iField);
        iX += iEditWidth + ScaleDialog(pDialog, 4);
    }
    iY += iField + ScaleDialog(pDialog, 8);
    PlaceDialogControl(pDialog, pDialog->AssignButton, iRight, iY, ScaleDialog(pDialog, 150), iField);
    PlaceDialogControl(pDialog, pDialog->ReleaseButton, iRight + ScaleDialog(pDialog, 150) + iGap, iY, ScaleDialog(pDialog, 170), iField);
    PlaceDialogControl(pDialog, pDialog->ReleaseOutside, iRight + ScaleDialog(pDialog, 320) + 2 * iGap, iY, iRightWidth - ScaleDialog(pDialog, 320) - 2 * iGap, iField);
    iY += iField + ScaleDialog(pDialog, 8);
    PlaceDialogControl(pDialog, pDialog->EqualSlots, iRight, iY, ScaleDialog(pDialog, 130), iField);
    PlaceDialogControl(pDialog, pDialog->ProportionalSlots, iRight + ScaleDialog(pDialog, 130) + iGap, iY, ScaleDialog(pDialog, 150), iField);
    PlaceDialogControl(pDialog, pDialog->ClearSlots, iRight + ScaleDialog(pDialog, 280) + 2 * iGap, iY, ScaleDialog(pDialog, 130), iField);

    PlaceDialogControl(pDialog, pDialog->Chart, iMargin, iChartTop, iWidth, iBottom - ScaleDialog(pDialog, 112) - iChartTop);
    PlaceDialogControl(pDialog, pDialog->Message, iMargin, iBottom - ScaleDialog(pDialog, 104), iWidth, iLabel);
    PlaceDialogControl(pDialog, pDialog->Issues, iMargin, iBottom - ScaleDialog(pDialog, 76), iWidth - iMargin - ScaleDialog(pDialog, 220), ScaleDialog(pDialog, 76));
    PlaceDialogControl(pDialog, pDialog->ApplyButton, Client.right - iMargin - ScaleDialog(pDialog, 210), iBottom - iField, ScaleDialog(pDialog, 100), iField);
    PlaceDialogControl(pDialog, pDialog->CancelButton, Client.right - iMargin - ScaleDialog(pDialog, 100), iBottom - iField, ScaleDialog(pDialog, 100), iField);
    RedrawWindow(pDialog->Window, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

static void HandleDialogCommand(GroupDialog_t *pDialog, uint32_t uiCommand, uint32_t uiNotification)
{
    if (DialogArea == uiCommand) {
        LRESULT Selection = SendMessageW(pDialog->AreaSelect, CB_GETCURSEL, 0, 0);
        if ((CBN_SELCHANGE == uiNotification) && (Selection >= AreaAlert) && (Selection < AreaCount)) {
            SelectDialogArea(pDialog, (AreaType)Selection);
        }
        return;
    }
    if (BN_CLICKED != uiNotification) {
        return;
    }
    switch (uiCommand) {
    case IDOK:
        if (GetFocus() == pDialog->TerminalEdit) {
            ApplyTerminalCount(pDialog);
        }
        break;

    case IDCANCEL:
    case DialogCancel:
        pDialog->bDone = true;
        break;

    case DialogTerminalApply:
        ApplyTerminalCount(pDialog);
        break;

    case DialogEqualTerminals:
        ResetGroupTerminals(&pDialog->Draft.aAreas[pDialog->Area]);
        RefreshDialog(pDialog);
        SetDialogText(pDialog->Message, L"%ls의 모든 그룹을 자동으로 되돌려 단말을 그룹 순서대로 균등 분배했습니다.", gpacDialogAreaNames[pDialog->Area]);
        break;

    case DialogEqualSlots:
        ResetSlots(pDialog);
        break;

    case DialogClearSlots:
        ClearAllSlots(pDialog);
        break;

    case DialogProportionalSlots:
        ApplyProportionalSlots(pDialog);
        break;

    case DialogAssign:
        ApplyRangeInput(pDialog, false);
        break;

    case DialogRelease:
        ApplyRangeInput(pDialog, true);
        break;

    case DialogReleaseOutside:
        ReleaseLostSlots(pDialog);
        break;

    case DialogHelp:
        ShowDialogHelp(pDialog);
        break;

    case DialogApply:
        ApplyDialog(pDialog);
        break;

    default:
        break;
    }
}

static LRESULT DrawSummaryRow(const GroupDialog_t *pDialog, NMLVCUSTOMDRAW *pDraw)
{
    switch (pDraw->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;

    case CDDS_ITEMPREPAINT:
        return CDRF_NOTIFYSUBITEMDRAW;

    case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
        pDraw->clrText = RGB(31, 45, 61);
        if ((SummaryCheck == pDraw->iSubItem) && (pDraw->nmcd.dwItemSpec < (DWORD_PTR)AreaCount)) {
            AreaCheck Check = pDialog->aChecks[pDraw->nmcd.dwItemSpec];
            pDraw->clrText = (AreaCheckFailed == Check) ? RGB(176, 48, 36) : ((AreaCheckPassed == Check) ? RGB(25, 114, 78) : guiHintColor);
        }
        return CDRF_DODEFAULT;

    default:
        return CDRF_DODEFAULT;
    }
}

static LRESULT HandleDialogNotify(GroupDialog_t *pDialog, NMHDR *pHeader)
{
    if ((pHeader->hwndFrom == pDialog->SummaryTable) && (NM_CUSTOMDRAW == pHeader->code)) {
        return DrawSummaryRow(pDialog, (NMLVCUSTOMDRAW *)pHeader);
    }
    if (LVN_ITEMCHANGED != pHeader->code) {
        return 0;
    }
    {
        const NMLISTVIEW *pList = (const NMLISTVIEW *)pHeader;
        bool bSelected = (0 != (pList->uNewState & LVIS_SELECTED)) && (0 == (pList->uOldState & LVIS_SELECTED)) && (pList->iItem >= 0);

        if (!bSelected || (NULL == pDialog->Chart)) {
            return 0;
        }
        if ((pHeader->hwndFrom == pDialog->GroupTable) && ((uint32_t)pList->iItem < pDialog->Input.Config.uiGroupCount)) {
            pDialog->uiSelectedGroup = (uint32_t)pList->iItem;
            RefreshDialog(pDialog);
        }
        else if ((pHeader->hwndFrom == pDialog->SummaryTable) && (pList->iItem < AreaCount)) {
            SelectDialogArea(pDialog, (AreaType)pList->iItem);
        }
    }
    return 0;
}

static void ScrollDialog(GroupDialog_t *pDialog, UINT uiMessage, WPARAM MessageParameter)
{
    SCROLLINFO Scroll = { .cbSize = sizeof(Scroll), .fMask = SIF_ALL };
    int32_t iLine = ScaleDialog(pDialog, 40);
    int32_t iPosition;

    if (FALSE == GetScrollInfo(pDialog->Window, SB_VERT, &Scroll)) {
        return;
    }
    iPosition = Scroll.nPos;
    if (WM_MOUSEWHEEL == uiMessage) {
        iPosition -= GET_WHEEL_DELTA_WPARAM(MessageParameter) * 3 * iLine / WHEEL_DELTA;
    }
    else if ((SB_LINEUP == LOWORD(MessageParameter)) || (SB_LINEDOWN == LOWORD(MessageParameter))) {
        iPosition += (SB_LINEUP == LOWORD(MessageParameter)) ? -iLine : iLine;
    }
    else if ((SB_PAGEUP == LOWORD(MessageParameter)) || (SB_PAGEDOWN == LOWORD(MessageParameter))) {
        iPosition += (SB_PAGEUP == LOWORD(MessageParameter)) ? -(int32_t)Scroll.nPage : (int32_t)Scroll.nPage;
    }
    else if ((SB_THUMBTRACK == LOWORD(MessageParameter)) || (SB_THUMBPOSITION == LOWORD(MessageParameter))) {
        iPosition = Scroll.nTrackPos;
    }
    if (iPosition != Scroll.nPos) {
        Scroll.fMask = SIF_POS;
        Scroll.nPos = iPosition;
        SetScrollInfo(pDialog->Window, SB_VERT, &Scroll, TRUE);
        LayoutDialog(pDialog);
    }
}

static LRESULT CALLBACK HandleDialogMessage(HWND Window, UINT uiMessage, WPARAM MessageParameter, LPARAM MessageData)
{
    GroupDialog_t *pDialog = (GroupDialog_t *)GetWindowLongPtrW(Window, GWLP_USERDATA);

    if (WM_NCCREATE == uiMessage) {
        const CREATESTRUCTW *pCreate = (const CREATESTRUCTW *)MessageData;
        pDialog = pCreate->lpCreateParams;
        pDialog->Window = Window;
        SetWindowLongPtrW(Window, GWLP_USERDATA, (LONG_PTR)pDialog);
        return DefWindowProcW(Window, uiMessage, MessageParameter, MessageData);
    }
    if (NULL == pDialog) {
        return DefWindowProcW(Window, uiMessage, MessageParameter, MessageData);
    }

    switch (uiMessage) {
    case WM_CREATE:
        if (!CreateDialogControls(pDialog)) {
            return -1;
        }
        LayoutDialog(pDialog);
        RefreshDialog(pDialog);
        return 0;

    case WM_SIZE:
        LayoutDialog(pDialog);
        return 0;

    case WM_GETMINMAXINFO:
        {
            MINMAXINFO *pLimits = (MINMAXINFO *)MessageData;
            pLimits->ptMinTrackSize.x = ScaleDialog(pDialog, 940);
            pLimits->ptMinTrackSize.y = ScaleDialog(pDialog, 520);
        }
        return 0;

    case WM_COMMAND:
        HandleDialogCommand(pDialog, LOWORD(MessageParameter), HIWORD(MessageParameter));
        return 0;

    case WM_NOTIFY:
        return HandleDialogNotify(pDialog, (NMHDR *)MessageData);

    case WM_VSCROLL:
    case WM_MOUSEWHEEL:
        ScrollDialog(pDialog, uiMessage, MessageParameter);
        return 0;

    case WM_SLOTCHART_SELECT:
        HandleChartSelection(pDialog, (const SlotSelection_t *)MessageData);
        return 0;

    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)MessageParameter, GetSysColor(COLOR_WINDOW));
        if (((HWND)MessageData == pDialog->Issues) && (0 != pDialog->uiIssueCount)) {
            SetTextColor((HDC)MessageParameter, RGB(176, 48, 36));
        }
        else if ((HWND)MessageData == pDialog->Issues) {
            SetTextColor((HDC)MessageParameter, RGB(25, 114, 78));
        }
        else if (((HWND)MessageData == pDialog->TerminalHint) || ((HWND)MessageData == pDialog->SlotHint)) {
            SetTextColor((HDC)MessageParameter, guiHintColor);
        }
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);

    case WM_CLOSE:
        pDialog->bDone = true;
        return 0;

    default:
        return DefWindowProcW(Window, uiMessage, MessageParameter, MessageData);
    }
}

bool RegisterGroupDialog(HINSTANCE Instance)
{
    WNDCLASSW Class = {
        .lpfnWndProc = HandleDialogMessage,
        .hInstance = Instance,
        .hIcon = LoadIconW(NULL, IDI_APPLICATION),
        .hCursor = LoadCursorW(NULL, IDC_ARROW),
        .hbrBackground = GetSysColorBrush(COLOR_WINDOW),
        .lpszClassName = gacDialogClass
    };
    return 0 != RegisterClassW(&Class);
}

bool ShowGroupDialog(HWND Owner, const GroupDialogInput_t *pInput, GroupConfig_t *pGroups)
{
    GroupDialog_t *pDialog = calloc(1, sizeof(*pDialog));
    RECT OwnerRect = { 0 };
    int32_t iWidth;
    int32_t iHeight;
    int32_t iX;
    int32_t iY;
    HWND Window;
    MSG Message;
    bool bApplied = false;

    if (NULL == pDialog) {
        return false;
    }
    pDialog->Input = *pInput;
    if ((0 == pInput->Config.uiGroupCount) || !CopyGroupConfig(&pDialog->Draft, pGroups)) {
        free(pDialog);
        return false;
    }
    pDialog->Area = IsAreaGroupActive(&pInput->Config, AreaAlert) ? AreaAlert : AreaLb;
    pDialog->View = (ChartView_t){
        .iDpi = pInput->iDpi,
        .Font = pInput->Font,
        .BoldFont = pInput->BoldFont,
        .uiTimeSlots = GetTimeSlotCount(pInput->Config.uiSymbolRate, pInput->Config.uiWaveId),
        .auiSlots = { pInput->Config.auiSlots[AreaAlert], pInput->Config.auiSlots[AreaLb] },
        .uiGroupCount = pInput->Config.uiGroupCount,
        .bEditable = true,
        .bGroupSelected = true,
        .EditArea = pDialog->Area
    };
    if (AreaLb == pDialog->Area) {
        pDialog->View.uiTopRow = GetFrequencyRowCount(pInput->Config.auiSlots[AreaAlert], pDialog->View.uiTimeSlots);
    }
    GetWindowRect(Owner, &OwnerRect);
    iWidth = MulDiv(1040, pInput->iDpi, 96);
    iHeight = MulDiv(900, pInput->iDpi, 96);
    {
        MONITORINFO Monitor = { .cbSize = sizeof(Monitor) };
        RECT Work = { OwnerRect.left, OwnerRect.top, OwnerRect.left + iWidth, OwnerRect.top + iHeight };

        /* Keep the whole dialog on the owner's monitor, including portrait screens. */
        if (FALSE != GetMonitorInfoW(MonitorFromWindow(Owner, MONITOR_DEFAULTTONEAREST), &Monitor)) {
            Work = Monitor.rcWork;
        }
        iWidth = (iWidth > Work.right - Work.left) ? Work.right - Work.left : iWidth;
        iHeight = (iHeight > Work.bottom - Work.top) ? Work.bottom - Work.top : iHeight;
        iX = OwnerRect.left + (OwnerRect.right - OwnerRect.left - iWidth) / 2;
        iY = OwnerRect.top + (OwnerRect.bottom - OwnerRect.top - iHeight) / 2;
        iX = (iX < Work.left) ? Work.left : ((iX + iWidth > Work.right) ? Work.right - iWidth : iX);
        iY = (iY < Work.top) ? Work.top : ((iY + iHeight > Work.bottom) ? Work.bottom - iHeight : iY);
    }
    EnableWindow(Owner, FALSE);
    Window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, gacDialogClass, L"그룹 설정",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN | WS_VSCROLL,
        iX, iY, iWidth, iHeight, Owner, NULL, pInput->Instance, pDialog);
    if (NULL == Window) {
        EnableWindow(Owner, TRUE);
        MessageBoxW(Owner, L"그룹 설정 창을 열 수 없습니다.", L"그룹 설정", MB_OK | MB_ICONERROR);
    }
    else {
        ShowWindow(Window, SW_SHOW);
        while (!pDialog->bDone) {
            int32_t iResult = GetMessageW(&Message, NULL, 0, 0);
            if (iResult <= 0) {
                if (0 == iResult) {
                    PostQuitMessage((int32_t)Message.wParam);
                }
                break;
            }
            if (FALSE == IsDialogMessageW(Window, &Message)) {
                TranslateMessage(&Message);
                DispatchMessageW(&Message);
            }
        }
        EnableWindow(Owner, TRUE);
        SetActiveWindow(Owner);
        DestroyWindow(Window);
        bApplied = pDialog->bApplied;
    }
    if (bApplied) {
        FreeGroupConfig(pGroups);
        *pGroups = pDialog->Draft;
        memset(&pDialog->Draft, 0, sizeof(pDialog->Draft));
    }
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        FreeAreaGroupLayout(&pDialog->aLayouts[uiArea]);
    }
    FreeGroupConfig(&pDialog->Draft);
    free(pDialog);
    return bApplied;
}
