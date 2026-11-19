#include "Chart.h"

#include <wchar.h>

typedef struct ChartLayout {
    int32_t iOrigin;
    int32_t iLeft;
    int32_t iRight;
    int32_t iTop;
    int32_t iBottom;
    int32_t iRowHeight;
    uint32_t auiFirstRows[AreaCount];
    uint32_t auiAreaRows[AreaCount];
    uint32_t uiPageRows;
    uint32_t uiTopLimit;
    uint32_t uiTrackLimit;
    uint32_t uiTopRow;
    uint32_t uiEndRow;
} ChartLayout_t;

typedef struct TrendLine {
    AreaType Area;
    bool bGroup;
    uint32_t uiGroup;
} TrendLine_t;

typedef struct TrendGrid {
    uint32_t uiGroups;
    uint32_t uiColumns;
    uint32_t uiRows;
    uint32_t uiVisibleRows;
} TrendGrid_t;

#define GROUP_COLOR_COUNT 20

static const wchar_t gacChartClass[] = L"SlottedAlohaSlotChart";
static const wchar_t *const gpacAreaNames[AreaCount] = { L"Alert", L"LB" };
static const COLORREF gauiPreviewColors[AreaCount] = { RGB(253, 227, 159), RGB(173, 195, 230) };
static const COLORREF gauiResultColors[] = { RGB(224, 230, 238), RGB(52, 161, 117), RGB(216, 99, 84) };
static const COLORREF gauiGroupColors[GROUP_COLOR_COUNT] = {
    RGB(78, 121, 167), RGB(242, 142, 43), RGB(225, 87, 89), RGB(118, 183, 178), RGB(89, 161, 79),
    RGB(237, 201, 72), RGB(176, 122, 161), RGB(255, 157, 167), RGB(156, 117, 95), RGB(186, 176, 172),
    RGB(160, 203, 232), RGB(255, 190, 125), RGB(255, 157, 154), RGB(134, 188, 182), RGB(140, 209, 125),
    RGB(241, 206, 99), RGB(212, 166, 200), RGB(250, 191, 210), RGB(215, 181, 166), RGB(121, 112, 110)
};
static const COLORREF guiUnassignedColor = RGB(248, 249, 251);
static const COLORREF guiUnassignedFrame = RGB(196, 204, 214);

static int32_t ScaleChart(const ChartView_t *pView, int32_t iValue)
{
    return MulDiv(iValue, pView->iDpi, 96);
}

COLORREF GetGroupColor(uint32_t uiGroup)
{
    return gauiGroupColors[uiGroup % GROUP_COLOR_COUNT];
}

static COLORREF BlendColor(COLORREF uiColor, uint32_t uiPercent)
{
    uint32_t uiRed = GetRValue(uiColor) + (255U - GetRValue(uiColor)) * uiPercent / 100U;
    uint32_t uiGreen = GetGValue(uiColor) + (255U - GetGValue(uiColor)) * uiPercent / 100U;
    uint32_t uiBlue = GetBValue(uiColor) + (255U - GetBValue(uiColor)) * uiPercent / 100U;

    return RGB(uiRed, uiGreen, uiBlue);
}

static bool HasGroupMaps(const ChartView_t *pView)
{
    return (NULL != pView->paucGroups[AreaAlert]) || (NULL != pView->paucGroups[AreaLb]);
}

static bool UsesGroupColors(const ChartView_t *pView)
{
    return HasGroupMaps(pView) && (pView->bShowGroups || !pView->bShowResults);
}

static ChartLayout_t CalculateChartLayout(HWND Window, const ChartView_t *pView)
{
    RECT Client;
    ChartLayout_t Layout = { 0 };
    uint32_t uiTotalRows;
    uint32_t uiVisibleRows;
    int32_t iAvailable;
    int32_t iMinimumHeight = ScaleChart(pView, 19);
    int32_t iMaximumHeight = ScaleChart(pView, 30);

    GetClientRect(Window, &Client);
    Layout.iOrigin = ScaleChart(pView, 120);
    Layout.iLeft = Layout.iOrigin + ScaleChart(pView, 4);
    Layout.iRight = Client.right - ScaleChart(pView, 16);
    Layout.iTop = ScaleChart(pView, 44);
    iAvailable = Client.bottom - Layout.iTop - ScaleChart(pView, 47);
    if (iAvailable < iMinimumHeight) {
        iAvailable = iMinimumHeight;
    }

    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        uint32_t uiRows = GetFrequencyRowCount(pView->auiSlots[uiArea], pView->uiTimeSlots);
        Layout.auiAreaRows[uiArea] = (0 == uiRows) ? 1 : uiRows;
    }
    Layout.auiFirstRows[AreaLb] = Layout.auiAreaRows[AreaAlert] + 1;
    uiTotalRows = Layout.auiFirstRows[AreaLb] + Layout.auiAreaRows[AreaLb];
    Layout.uiPageRows = (uint32_t)(iAvailable / iMinimumHeight);
    uiVisibleRows = (uiTotalRows < Layout.uiPageRows) ? uiTotalRows : Layout.uiPageRows;
    Layout.iRowHeight = iAvailable / (int32_t)uiVisibleRows;
    if (Layout.iRowHeight > iMaximumHeight) {
        Layout.iRowHeight = iMaximumHeight;
    }
    Layout.uiTopLimit = uiTotalRows - uiVisibleRows;
    Layout.uiTrackLimit = (uint32_t)INT32_MAX - Layout.uiPageRows;
    if (Layout.uiTrackLimit > Layout.uiTopLimit) {
        Layout.uiTrackLimit = Layout.uiTopLimit;
    }
    Layout.uiTopRow = (pView->uiTopRow < Layout.uiTopLimit) ? pView->uiTopRow : Layout.uiTopLimit;
    Layout.uiEndRow = Layout.uiTopRow + uiVisibleRows;
    Layout.iBottom = Layout.iTop + (int32_t)uiVisibleRows * Layout.iRowHeight + ScaleChart(pView, 5);
    return Layout;
}

static bool IsGroupCompareShown(const ChartView_t *pView)
{
    return pView->bShowTrend && (TrendGroupCompare == pView->Trend) && (NULL != pView->pResult) && IsGroupedResult(pView->pResult);
}

static TrendGrid_t CalculateTrendGrid(HWND Window, const ChartView_t *pView)
{
    RECT Client;
    TrendGrid_t Grid = { .uiColumns = 1, .uiRows = 1, .uiVisibleRows = 1 };
    int32_t iWidth;
    int32_t iHeight;

    GetClientRect(Window, &Client);
    iWidth = Client.right - ScaleChart(pView, 32);
    iHeight = Client.bottom - ScaleChart(pView, 84);
    Grid.uiGroups = (NULL == pView->pResult) ? 0 : pView->pResult->aPlans[pView->TrendArea].uiGroupCount;
    if ((iWidth > 0) && (iWidth / ScaleChart(pView, 300) > 1)) {
        Grid.uiColumns = (uint32_t)(iWidth / ScaleChart(pView, 300));
    }
    if ((0 != Grid.uiGroups) && (Grid.uiColumns > Grid.uiGroups)) {
        Grid.uiColumns = Grid.uiGroups;
    }
    Grid.uiRows = (0 == Grid.uiGroups) ? 1 : (Grid.uiGroups + Grid.uiColumns - 1) / Grid.uiColumns;
    if ((iHeight > 0) && (iHeight / ScaleChart(pView, 200) > 1)) {
        Grid.uiVisibleRows = (uint32_t)(iHeight / ScaleChart(pView, 200));
    }
    if (Grid.uiVisibleRows > Grid.uiRows) {
        Grid.uiVisibleRows = Grid.uiRows;
    }
    return Grid;
}

void RefreshSlotChart(HWND Window, ChartView_t *pView)
{
    if (pView->bShowTrend) {
        if (IsGroupCompareShown(pView)) {
            TrendGrid_t Grid = CalculateTrendGrid(Window, pView);
            SCROLLINFO Scroll = { .cbSize = sizeof(Scroll), .fMask = SIF_RANGE | SIF_PAGE | SIF_POS, .nMin = 0,
                .nMax = (int32_t)Grid.uiRows - 1, .nPage = Grid.uiVisibleRows };

            if (pView->uiTrendTopRow > Grid.uiRows - Grid.uiVisibleRows) {
                pView->uiTrendTopRow = Grid.uiRows - Grid.uiVisibleRows;
            }
            Scroll.nPos = (int32_t)pView->uiTrendTopRow;
            SetScrollInfo(Window, SB_VERT, &Scroll, TRUE);
        }
        else {
            ShowScrollBar(Window, SB_VERT, FALSE);
        }
        InvalidateRect(Window, NULL, FALSE);
        return;
    }

    ChartLayout_t Layout = CalculateChartLayout(Window, pView);
    SCROLLINFO Scroll = {
        .cbSize = sizeof(Scroll),
        .fMask = SIF_RANGE | SIF_PAGE | SIF_POS,
        .nMin = 0,
        .nMax = (int32_t)(Layout.uiTrackLimit + Layout.uiPageRows - 1),
        .nPage = Layout.uiPageRows
    };

    pView->uiTopRow = Layout.uiTopRow;
    if (0 != Layout.uiTopLimit) {
        Scroll.nPos = (int32_t)((uint64_t)Layout.uiTopRow * Layout.uiTrackLimit / Layout.uiTopLimit);
    }
    SetScrollInfo(Window, SB_VERT, &Scroll, TRUE);
    InvalidateRect(Window, NULL, FALSE);
}

static uint32_t MoveScrollRow(uint32_t uiRow, uint32_t uiPage, uint32_t uiLimit, uint32_t uiAction, uint32_t uiTrack)
{
    switch (uiAction) {
    case SB_LINEUP:
        return uiRow - (uint32_t)(0 != uiRow);

    case SB_LINEDOWN:
        return uiRow + 1;

    case SB_PAGEUP:
        return (uiRow > uiPage) ? uiRow - uiPage : 0;

    case SB_PAGEDOWN:
        return uiRow + uiPage;

    case SB_TOP:
        return 0;

    case SB_BOTTOM:
        return uiLimit;

    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
        return uiTrack;

    default:
        return uiRow;
    }
}

static void ScrollSlotChart(HWND Window, ChartView_t *pView, uint32_t uiAction)
{
    SCROLLINFO Scroll = { .cbSize = sizeof(Scroll), .fMask = SIF_TRACKPOS };
    uint32_t uiTrack = 0;

    if (FALSE != GetScrollInfo(Window, SB_VERT, &Scroll)) {
        uiTrack = (uint32_t)Scroll.nTrackPos;
    }
    if (IsGroupCompareShown(pView)) {
        TrendGrid_t Grid = CalculateTrendGrid(Window, pView);
        pView->uiTrendTopRow = MoveScrollRow(pView->uiTrendTopRow, Grid.uiVisibleRows, Grid.uiRows - Grid.uiVisibleRows, uiAction, uiTrack);
        RefreshSlotChart(Window, pView);
        return;
    }

    ChartLayout_t Layout = CalculateChartLayout(Window, pView);
    if (((SB_THUMBTRACK == uiAction) || (SB_THUMBPOSITION == uiAction)) && (0 != Layout.uiTrackLimit)) {
        uiTrack = (uint32_t)(((uint64_t)uiTrack * Layout.uiTopLimit + Layout.uiTrackLimit / 2) / Layout.uiTrackLimit);
    }
    else if ((SB_THUMBTRACK == uiAction) || (SB_THUMBPOSITION == uiAction)) {
        uiTrack = Layout.uiTopRow;
    }
    pView->uiTopRow = MoveScrollRow(Layout.uiTopRow, Layout.uiPageRows, Layout.uiTopLimit, uiAction, uiTrack);
    RefreshSlotChart(Window, pView);
}

static void DrawChartText(HDC Target, const wchar_t *pcText, RECT Bounds, uint32_t uiAlignment)
{
    DrawTextW(Target, pcText, -1, &Bounds, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS | uiAlignment);
}

static void DrawAxes(HDC Target, const ChartView_t *pView, const ChartLayout_t *pLayout)
{
    COLORREF uiColor = GetSysColor(COLOR_WINDOWTEXT);
    int32_t iArrow = ScaleChart(pView, 8);
    int32_t iWing = ScaleChart(pView, 4);
    int32_t iUpper = pLayout->iTop - ScaleChart(pView, 10);
    int32_t iEnd = pLayout->iRight + ScaleChart(pView, 5);
    int32_t iMiddle = pLayout->iOrigin;
    int32_t iBottom = pLayout->iBottom;
    POINT aUp[3] = { { iMiddle, iUpper - iArrow }, { iMiddle - iWing, iUpper }, { iMiddle + iWing, iUpper } };
    POINT aRight[3] = { { iEnd + iArrow, iBottom }, { iEnd, iBottom - iWing }, { iEnd, iBottom + iWing } };
    RECT Frequency = { iMiddle - ScaleChart(pView, 42), 0, iMiddle + ScaleChart(pView, 42), iUpper - iArrow };
    RECT Time = { iEnd - ScaleChart(pView, 86), iBottom + iWing, iEnd + iArrow, iBottom + ScaleChart(pView, 28) };
    HPEN Pen = CreatePen(PS_SOLID, ScaleChart(pView, 2), uiColor);
    HGDIOBJ PreviousPen = SelectObject(Target, (NULL != Pen) ? Pen : GetStockObject(DC_PEN));
    HGDIOBJ PreviousBrush = SelectObject(Target, GetStockObject(DC_BRUSH));

    SetDCPenColor(Target, uiColor);
    SetDCBrushColor(Target, uiColor);
    MoveToEx(Target, iMiddle, iUpper, NULL);
    LineTo(Target, iMiddle, iBottom);
    LineTo(Target, iEnd, iBottom);
    Polygon(Target, aUp, 3);
    Polygon(Target, aRight, 3);
    DrawChartText(Target, L"주파수", Frequency, DT_CENTER);
    DrawChartText(Target, L"시간", Time, DT_RIGHT);

    SelectObject(Target, PreviousBrush);
    SelectObject(Target, PreviousPen);
    if (NULL != Pen) {
        DeleteObject(Pen);
    }
}

static COLORREF GetCellColor(const ChartView_t *pView, AreaType Area, uint64_t ullSlot, uint8_t ucGroup, bool bGroupColors)
{
    const uint8_t *paucGroups = pView->paucGroups[Area];
    COLORREF uiColor;
    uint32_t uiBlend = 0;

    if (bGroupColors && (NULL != paucGroups)) {
        uiColor = (GROUP_UNASSIGNED == ucGroup) ? guiUnassignedColor : GetGroupColor(ucGroup);
    }
    else if (pView->bShowResults) {
        uiColor = gauiResultColors[pView->paucStates[Area][ullSlot]];
        if ((NULL != paucGroups) && (GROUP_UNASSIGNED == ucGroup)) {
            uiBlend = 55;
        }
    }
    else {
        uiColor = gauiPreviewColors[Area];
    }
    if ((NULL != paucGroups) && pView->bGroupSelected && !pView->bEditable && (ucGroup != pView->uiSelectedGroup)) {
        uiBlend = 68;
    }
    if (pView->bEditable && (Area != pView->EditArea)) {
        uiBlend = 72;
    }
    return (0 == uiBlend) ? uiColor : BlendColor(uiColor, uiBlend);
}

/* Rows are drawn with the highest frequency at the top, so frequency 1 is the bottom row of each area. */
static uint32_t GetRowFrequency(const ChartLayout_t *pLayout, AreaType Area, uint32_t uiRow)
{
    return pLayout->auiAreaRows[Area] - 1 - (uiRow - pLayout->auiFirstRows[Area]);
}

static void DrawFrequencyLabel(HDC Target, const ChartView_t *pView, const ChartLayout_t *pLayout, uint32_t uiFrequency, int32_t iTop)
{
    uint32_t uiNumber = uiFrequency + 1;
    bool bDense = pLayout->iRowHeight < ScaleChart(pView, 16);
    RECT Label = { pLayout->iOrigin - ScaleChart(pView, 34), iTop, pLayout->iOrigin - ScaleChart(pView, 6), iTop + pLayout->iRowHeight };
    wchar_t acNumber[16];
    int32_t iWritten;
    COLORREF uiPrevious;

    if (bDense && (1 != uiNumber) && (0 != uiNumber % 5)) {
        return;
    }
    iWritten = swprintf(acNumber, sizeof(acNumber) / sizeof(acNumber[0]), L"%u", uiNumber);
    if ((iWritten > 0) && ((size_t)iWritten < sizeof(acNumber) / sizeof(acNumber[0]))) {
        uiPrevious = SetTextColor(Target, RGB(120, 130, 142));
        DrawChartText(Target, acNumber, Label, DT_RIGHT);
        SetTextColor(Target, uiPrevious);
    }
}

static void DrawArea(HDC Target, const ChartView_t *pView, const ChartLayout_t *pLayout, AreaType Area)
{
    uint32_t uiStart = pLayout->auiFirstRows[Area];
    uint32_t uiEnd = uiStart + pLayout->auiAreaRows[Area];
    uint32_t uiFirst = (uiStart > pLayout->uiTopRow) ? uiStart : pLayout->uiTopRow;
    uint32_t uiLast = (uiEnd < pLayout->uiEndRow) ? uiEnd : pLayout->uiEndRow;
    uint32_t uiSlots = pView->auiSlots[Area];
    const uint8_t *paucGroups = pView->paucGroups[Area];
    bool bGroupColors = UsesGroupColors(pView);
    int32_t iWidth = pLayout->iRight - pLayout->iLeft;
    int32_t iTextHeight = ScaleChart(pView, 18);
    int32_t iLabelMiddle;
    RECT Name;
    RECT Capacity;
    wchar_t acCapacity[32];
    int32_t iWritten;
    HBRUSH Brush = (HBRUSH)GetStockObject(DC_BRUSH);
    HBRUSH FrameBrush = CreateSolidBrush(guiUnassignedFrame);
    HGDIOBJ PreviousFont;

    if ((uiFirst >= uiLast) || (iWidth <= 0)) {
        if (NULL != FrameBrush) {
            DeleteObject(FrameBrush);
        }
        return;
    }

    for (uint32_t uiRow = uiFirst; uiRow < uiLast; uiRow++) {
        uint32_t uiFrequency = GetRowFrequency(pLayout, Area, uiRow);
        uint64_t ullFirstSlot = (uint64_t)uiFrequency * pView->uiTimeSlots;
        int32_t iTop = pLayout->iTop + (int32_t)(uiRow - pLayout->uiTopRow) * pLayout->iRowHeight;

        if (0 != uiSlots) {
            DrawFrequencyLabel(Target, pView, pLayout, uiFrequency, iTop);
        }
        for (uint32_t uiColumn = 0; uiColumn < pView->uiTimeSlots; uiColumn++) {
            uint64_t ullSlot = ullFirstSlot + uiColumn;
            uint8_t ucGroup;
            RECT Cell;

            if (ullSlot >= uiSlots) {
                break;
            }
            ucGroup = (NULL == paucGroups) ? GROUP_UNASSIGNED : paucGroups[ullSlot];
            Cell.left = pLayout->iLeft + (int32_t)uiColumn * iWidth / (int32_t)pView->uiTimeSlots;
            Cell.right = pLayout->iLeft + (int32_t)(uiColumn + 1) * iWidth / (int32_t)pView->uiTimeSlots - 1;
            Cell.top = iTop;
            Cell.bottom = iTop + pLayout->iRowHeight - 1;
            SetDCBrushColor(Target, GetCellColor(pView, Area, ullSlot, ucGroup, bGroupColors));
            FillRect(Target, &Cell, Brush);
            if (bGroupColors && (NULL != paucGroups) && (GROUP_UNASSIGNED == ucGroup) && (NULL != FrameBrush)) {
                FrameRect(Target, &Cell, FrameBrush);
            }
            else if (pView->bEditable && pView->bGroupSelected && (Area == pView->EditArea) && (ucGroup == pView->uiSelectedGroup)) {
                SetDCBrushColor(Target, RGB(23, 54, 93));
                FrameRect(Target, &Cell, Brush);
            }
        }
    }
    if (NULL != FrameBrush) {
        DeleteObject(FrameBrush);
    }

    iLabelMiddle = pLayout->iTop + (int32_t)((uiFirst - pLayout->uiTopRow) + (uiLast - pLayout->uiTopRow)) * pLayout->iRowHeight / 2;
    Name = (RECT){ ScaleChart(pView, 6), iLabelMiddle - iTextHeight, pLayout->iOrigin - ScaleChart(pView, 38), iLabelMiddle };
    Capacity = (RECT){ Name.left, Name.bottom, Name.right, Name.bottom + iTextHeight };
    PreviousFont = SelectObject(Target, pView->BoldFont);
    DrawChartText(Target, gpacAreaNames[Area], Name, DT_LEFT);
    SelectObject(Target, PreviousFont);
    iWritten = swprintf(acCapacity, sizeof(acCapacity) / sizeof(acCapacity[0]), L"%u칸", uiSlots);
    if ((iWritten >= 0) && ((size_t)iWritten < sizeof(acCapacity) / sizeof(acCapacity[0]))) {
        DrawChartText(Target, acCapacity, Capacity, DT_LEFT);
    }
}

static void DrawLegendItem(HDC Target, const ChartView_t *pView, int32_t iLeft, int32_t iTop, int32_t iWidth, COLORREF uiColor, bool bFrame, const wchar_t *pcLabel)
{
    int32_t iMarkSize = ScaleChart(pView, 13);
    RECT Mark = { iLeft, iTop, iLeft + iMarkSize, iTop + iMarkSize };
    RECT Label = { Mark.right + ScaleChart(pView, 6), iTop - ScaleChart(pView, 3), iLeft + iWidth - 1, iTop + iMarkSize + ScaleChart(pView, 3) };

    SetDCBrushColor(Target, uiColor);
    FillRect(Target, &Mark, (HBRUSH)GetStockObject(DC_BRUSH));
    if (bFrame) {
        SetDCBrushColor(Target, guiUnassignedFrame);
        FrameRect(Target, &Mark, (HBRUSH)GetStockObject(DC_BRUSH));
    }
    DrawChartText(Target, pcLabel, Label, DT_LEFT);
}

static void DrawLegend(HDC Target, const ChartView_t *pView, const ChartLayout_t *pLayout)
{
    const wchar_t *const pacLabels[] = { L"유휴", L"성공", L"충돌" };
    int32_t iTop = pLayout->iBottom + ScaleChart(pView, 11);

    for (uint32_t uiState = SlotIdle; uiState <= SlotCollision; uiState++) {
        int32_t iLeft = pLayout->iOrigin + (int32_t)uiState * ScaleChart(pView, 77);
        DrawLegendItem(Target, pView, iLeft, iTop, ScaleChart(pView, 77), gauiResultColors[uiState], false, pacLabels[uiState]);
    }
    if (HasGroupMaps(pView)) {
        DrawLegendItem(Target, pView, pLayout->iOrigin + 3 * ScaleChart(pView, 77), iTop, ScaleChart(pView, 90),
            BlendColor(gauiResultColors[SlotIdle], 55), false, L"미배정");
    }
}

static void DrawGroupLegend(HDC Target, const ChartView_t *pView, const ChartLayout_t *pLayout)
{
    int32_t iTop = pLayout->iBottom + ScaleChart(pView, 11);
    int32_t iItem = ScaleChart(pView, 88);
    int32_t iCapacity = (pLayout->iRight - pLayout->iOrigin) / iItem;
    uint32_t uiShown = pView->uiGroupCount;
    int32_t iLeft = pLayout->iOrigin;
    wchar_t acLabel[32];

    if ((iCapacity > 0) && ((int64_t)uiShown + 1 > iCapacity)) {
        uiShown = (iCapacity > 2) ? (uint32_t)iCapacity - 2 : 0;
    }
    for (uint32_t uiGroup = 0; uiGroup < uiShown; uiGroup++) {
        int32_t iWritten = swprintf(acLabel, sizeof(acLabel) / sizeof(acLabel[0]), L"그룹 %u", uiGroup + 1);
        if ((iWritten >= 0) && ((size_t)iWritten < sizeof(acLabel) / sizeof(acLabel[0]))) {
            DrawLegendItem(Target, pView, iLeft, iTop, iItem, GetGroupColor(uiGroup), false, acLabel);
        }
        iLeft += iItem;
    }
    if (uiShown < pView->uiGroupCount) {
        RECT Rest = { iLeft, iTop - ScaleChart(pView, 3), iLeft + iItem - 1, iTop + ScaleChart(pView, 16) };
        int32_t iWritten = swprintf(acLabel, sizeof(acLabel) / sizeof(acLabel[0]), L"외 %u개", pView->uiGroupCount - uiShown);
        if ((iWritten >= 0) && ((size_t)iWritten < sizeof(acLabel) / sizeof(acLabel[0]))) {
            DrawChartText(Target, acLabel, Rest, DT_LEFT);
        }
        iLeft += iItem;
    }
    DrawLegendItem(Target, pView, iLeft, iTop, iItem, guiUnassignedColor, true, L"미배정");
}

static void DrawSelection(HDC Target, const ChartView_t *pView, const ChartLayout_t *pLayout)
{
    const SlotSelection_t *pDrag = &pView->Drag;
    uint32_t uiFirstRow = (pDrag->uiStartRow < pDrag->uiEndRow) ? pDrag->uiStartRow : pDrag->uiEndRow;
    uint32_t uiLastRow = (pDrag->uiStartRow < pDrag->uiEndRow) ? pDrag->uiEndRow : pDrag->uiStartRow;
    uint32_t uiFirstTime = (pDrag->uiStartTime < pDrag->uiEndTime) ? pDrag->uiStartTime : pDrag->uiEndTime;
    uint32_t uiLastTime = (pDrag->uiStartTime < pDrag->uiEndTime) ? pDrag->uiEndTime : pDrag->uiStartTime;
    uint64_t ullTop = (uint64_t)pLayout->auiFirstRows[pDrag->Area] + (pLayout->auiAreaRows[pDrag->Area] - 1 - uiLastRow);
    uint64_t ullBottom = (uint64_t)pLayout->auiFirstRows[pDrag->Area] + (pLayout->auiAreaRows[pDrag->Area] - 1 - uiFirstRow) + 1;
    int32_t iWidth = pLayout->iRight - pLayout->iLeft;
    HPEN Pen;
    HGDIOBJ PreviousPen;
    HGDIOBJ PreviousBrush;
    RECT Frame;

    if (!pView->bDragging || (0 == pView->uiTimeSlots) || (iWidth <= 0)) {
        return;
    }
    if (ullTop < pLayout->uiTopRow) {
        ullTop = pLayout->uiTopRow;
    }
    if (ullBottom > pLayout->uiEndRow) {
        ullBottom = pLayout->uiEndRow;
    }
    if (ullTop >= ullBottom) {
        return;
    }
    Frame.left = pLayout->iLeft + (int32_t)uiFirstTime * iWidth / (int32_t)pView->uiTimeSlots - 1;
    Frame.right = pLayout->iLeft + (int32_t)(uiLastTime + 1) * iWidth / (int32_t)pView->uiTimeSlots + 1;
    Frame.top = pLayout->iTop + (int32_t)(ullTop - pLayout->uiTopRow) * pLayout->iRowHeight - 1;
    Frame.bottom = pLayout->iTop + (int32_t)(ullBottom - pLayout->uiTopRow) * pLayout->iRowHeight + 1;
    Pen = CreatePen(PS_SOLID, ScaleChart(pView, 2), RGB(23, 54, 93));
    PreviousPen = SelectObject(Target, (NULL != Pen) ? Pen : GetStockObject(BLACK_PEN));
    PreviousBrush = SelectObject(Target, GetStockObject(HOLLOW_BRUSH));
    Rectangle(Target, Frame.left, Frame.top, Frame.right, Frame.bottom);
    SelectObject(Target, PreviousBrush);
    SelectObject(Target, PreviousPen);
    if (NULL != Pen) {
        DeleteObject(Pen);
    }
}

static uint64_t GetTrendIndex(uint64_t ullSpan, uint32_t uiPoint, uint32_t uiSegments)
{
    if (0 == uiSegments) {
        return 0;
    }
    else {
        return (ullSpan / uiSegments) * uiPoint + (ullSpan % uiSegments) * uiPoint / uiSegments;
    }
}

static bool FormatTrendTime(wchar_t *pcText, size_t uCapacity, uint64_t ullMilliseconds)
{
    uint64_t ullSeconds = ullMilliseconds / 1000;
    uint32_t uiFraction = (uint32_t)(ullMilliseconds % 1000);
    int32_t iWritten = (0 == uiFraction)
        ? swprintf(pcText, uCapacity, L"%llu", ullSeconds)
        : swprintf(pcText, uCapacity, L"%llu.%03u", ullSeconds, uiFraction);

    return (iWritten >= 0) && ((size_t)iWritten < uCapacity);
}

static RoundAreaCounts_t GetTrendCounts(const SimulationResult_t *pResult, const TrendLine_t *pLine, uint64_t ullIndex)
{
    if (pLine->bGroup) {
        return GetRoundGroupCounts(pResult, ullIndex, pLine->Area, pLine->uiGroup);
    }
    else {
        return GetRoundAreaCounts(pResult, ullIndex, pLine->Area);
    }
}

static void DrawTrendAxes(HDC Target, const ChartView_t *pView, const RECT *pPlot, uint32_t uiMaximum, uint64_t ullFirst, uint32_t uiPoints)
{
    const SimulationResult_t *pResult = pView->pResult;
    uint64_t ullSpan = uiPoints - 1;
    uint64_t ullPeriod = pResult->Config.ullSuperframeMilliseconds;
    uint32_t uiYTicks = (uiMaximum < 4) ? uiMaximum : 4;
    int32_t iWidth = pPlot->right - pPlot->left;
    int32_t iHeight = pPlot->bottom - pPlot->top;
    int32_t iTickWidth = ScaleChart(pView, 76);
    int32_t iLabelHeight = ScaleChart(pView, 23);
    uint32_t uiXTicks;
    wchar_t acText[32];
    SIZE TextSize;
    RECT Title = { 0, 0, pPlot->left, pPlot->top };
    HGDIOBJ PreviousPen = SelectObject(Target, GetStockObject(DC_PEN));

    if (FormatTrendTime(acText, sizeof(acText) / sizeof(acText[0]), pResult->ullRoundCount * ullPeriod) &&
        (FALSE != GetTextExtentPoint32W(Target, acText, (int32_t)wcslen(acText), &TextSize))) {
        int32_t iNeeded = TextSize.cx + ScaleChart(pView, 16);
        if (iNeeded > iTickWidth) {
            iTickWidth = iNeeded;
        }
    }
    uiXTicks = (uint32_t)(iWidth / iTickWidth);
    if (uiXTicks < 1) {
        uiXTicks = 1;
    }
    if (uiXTicks > 5) {
        uiXTicks = 5;
    }
    if (ullSpan < uiXTicks) {
        uiXTicks = (uint32_t)ullSpan;
    }

    DrawChartText(Target, L"단말 수", Title, DT_LEFT);
    for (uint32_t uiTick = 0; uiTick <= uiYTicks; uiTick++) {
        uint32_t uiValue = (uint32_t)((uint64_t)uiMaximum * uiTick / uiYTicks);
        int32_t iY = pPlot->bottom - (int32_t)((uint64_t)uiValue * (uint32_t)iHeight / uiMaximum);
        RECT Label = { 0, iY - iLabelHeight / 2, pPlot->left - ScaleChart(pView, 9), iY + iLabelHeight / 2 };
        int32_t iLength = swprintf(acText, sizeof(acText) / sizeof(acText[0]), L"%u", uiValue);

        SetDCPenColor(Target, GetSysColor(COLOR_3DFACE));
        MoveToEx(Target, pPlot->left, iY, NULL);
        LineTo(Target, pPlot->right, iY);
        if ((iLength >= 0) && ((size_t)iLength < sizeof(acText) / sizeof(acText[0]))) {
            DrawChartText(Target, acText, Label, DT_RIGHT);
        }
    }
    SetDCPenColor(Target, GetSysColor(COLOR_WINDOWTEXT));
    MoveToEx(Target, pPlot->left, pPlot->top, NULL);
    LineTo(Target, pPlot->left, pPlot->bottom);
    LineTo(Target, pPlot->right + 1, pPlot->bottom);
    for (uint32_t uiTick = 0; uiTick <= uiXTicks; uiTick++) {
        uint64_t ullIndex = GetTrendIndex(ullSpan, uiTick, uiXTicks);
        int32_t iX = pPlot->left + iWidth / 2;
        int32_t iLeft;
        RECT Label;

        if (0 != ullSpan) {
            iX = pPlot->left + (int32_t)((long double)ullIndex * iWidth / ullSpan);
        }
        iLeft = iX - iTickWidth / 2;
        if (iLeft > pPlot->right - iTickWidth) {
            iLeft = pPlot->right - iTickWidth;
        }
        Label = (RECT){ iLeft, pPlot->bottom + ScaleChart(pView, 28), iLeft + iTickWidth, pPlot->bottom + ScaleChart(pView, 51) };
        MoveToEx(Target, iX, pPlot->bottom, NULL);
        LineTo(Target, iX, pPlot->bottom + ScaleChart(pView, 4));
        if (FormatTrendTime(acText, sizeof(acText) / sizeof(acText[0]), (ullFirst + ullIndex + 1) * ullPeriod)) {
            DrawChartText(Target, acText, Label, (iX == pPlot->right) ? DT_RIGHT : DT_CENTER);
        }
    }
    Title = (RECT){ pPlot->left, pPlot->bottom + ScaleChart(pView, 54), pPlot->right, pPlot->bottom + ScaleChart(pView, 79) };
    DrawChartText(Target, L"전송 주기 (수신 시각, 초)", Title, DT_RIGHT);
    SelectObject(Target, PreviousPen);
}

static void DrawTrendLegend(HDC Target, const ChartView_t *pView, int32_t iLeft, bool bCollision)
{
    int32_t iLegendMiddle = ScaleChart(pView, 19);
    RECT Legend = { iLeft + ScaleChart(pView, 30), 0, iLeft + ScaleChart(pView, 135), ScaleChart(pView, 38) };

    MoveToEx(Target, iLeft, iLegendMiddle, NULL);
    LineTo(Target, iLeft + ScaleChart(pView, 23), iLegendMiddle);
    DrawChartText(Target, bCollision ? L"충돌 전송" : L"성공 전송", Legend, DT_LEFT);
}

static void DrawTrendSeries(HDC Target, const ChartView_t *pView, const TrendLine_t *pLine, const RECT *pPlot, uint32_t uiMaximum, uint64_t ullFirst, uint32_t uiPoints, bool bCollision, bool bLabels, bool bLegend)
{
    const SimulationResult_t *pResult = pView->pResult;
    COLORREF uiColor = gauiResultColors[bCollision ? SlotCollision : SlotSuccess];
    LOGBRUSH Brush = { .lbStyle = BS_SOLID, .lbColor = uiColor };
    HPEN Pen = ExtCreatePen(PS_GEOMETRIC | PS_ENDCAP_FLAT | (bCollision ? PS_DASH : PS_SOLID),
        (uint32_t)ScaleChart(pView, 2), &Brush, 0, NULL);
    HGDIOBJ PreviousPen = SelectObject(Target, (NULL != Pen) ? Pen : GetStockObject(DC_PEN));
    HGDIOBJ PreviousBrush = SelectObject(Target, GetStockObject(bCollision ? HOLLOW_BRUSH : DC_BRUSH));
    uint32_t uiSegments = uiPoints - 1;
    int32_t iWidth = pPlot->right - pPlot->left;
    int32_t iHeight = pPlot->bottom - pPlot->top;
    int32_t iRadius = ScaleChart(pView, bCollision ? 4 : 3);

    SetDCPenColor(Target, uiColor);
    SetDCBrushColor(Target, uiColor);
    if (bLegend && !bLabels) {
        DrawTrendLegend(Target, pView, pPlot->left + (bCollision ? ScaleChart(pView, 139) : 0), bCollision);
    }

    for (uint32_t uiPoint = 0; uiPoint < uiPoints; uiPoint++) {
        uint64_t ullIndex = ullFirst + uiPoint;
        RoundAreaCounts_t Counts = GetTrendCounts(pResult, pLine, ullIndex);
        uint32_t uiValue = bCollision ? Counts.uiCollidedAttempts : Counts.uiSuccess;
        int32_t iX = pPlot->left + ((0 == uiSegments)
            ? iWidth / 2 : (int32_t)((uint64_t)uiPoint * (uint32_t)iWidth / uiSegments));
        int32_t iY = pPlot->bottom - (int32_t)((uint64_t)uiValue * (uint32_t)iHeight / uiMaximum);

        if (bLabels) {
            wchar_t acValue[24];
            SIZE Size;
            int32_t iLength = swprintf(acValue, sizeof(acValue) / sizeof(acValue[0]), L"%u", uiValue);
            if ((iLength > 0) && ((size_t)iLength < sizeof(acValue) / sizeof(acValue[0])) &&
                (FALSE != GetTextExtentPoint32W(Target, acValue, iLength, &Size))) {
                bool bBelow = bCollision ? (Counts.uiCollidedAttempts <= Counts.uiSuccess) : (Counts.uiSuccess < Counts.uiCollidedAttempts);
                int32_t iTop = bBelow ? iY + ScaleChart(pView, 6) : iY - Size.cy - ScaleChart(pView, 6);
                if ((iTop + Size.cy > pPlot->bottom - ScaleChart(pView, 4)) &&
                    (!bCollision || (0 != Counts.uiSuccess) || (0 != Counts.uiCollidedAttempts))) {
                    iTop = iY - Size.cy - ScaleChart(pView, 6);
                }
                RECT Label = { iX - Size.cx / 2 - 3, iTop, iX + (Size.cx + 1) / 2 + 3, iTop + Size.cy };
                COLORREF uiPrevious = SetTextColor(Target, bCollision ? RGB(167, 58, 43) : RGB(25, 114, 78));
                if (Label.left < pPlot->left + ScaleChart(pView, 5)) {
                    OffsetRect(&Label, pPlot->left + ScaleChart(pView, 5) - Label.left, 0);
                }
                if (Label.right > pPlot->right) {
                    OffsetRect(&Label, pPlot->right - Label.right, 0);
                }
                FillRect(Target, &Label, GetSysColorBrush(COLOR_WINDOW));
                DrawChartText(Target, acValue, Label, DT_CENTER);
                SetTextColor(Target, uiPrevious);
            }
            continue;
        }
        if (0 == uiPoint) {
            MoveToEx(Target, iX, iY, NULL);
        }
        else {
            LineTo(Target, iX, iY);
        }
        if (uiPoints <= 60) {
            if (bCollision) {
                Rectangle(Target, iX - iRadius, iY - iRadius, iX + iRadius + 1, iY + iRadius + 1);
            }
            else {
                Ellipse(Target, iX - iRadius, iY - iRadius, iX + iRadius + 1, iY + iRadius + 1);
            }
        }
    }
    SelectObject(Target, PreviousBrush);
    SelectObject(Target, PreviousPen);
    if (NULL != Pen) {
        DeleteObject(Pen);
    }
}

static uint32_t CalculateTrendPoints(HDC Target, const ChartView_t *pView, int32_t iPlotWidth, uint32_t uiMaximum, int32_t iGap)
{
    wchar_t acMaximum[24];
    SIZE Size = { .cx = ScaleChart(pView, 44) };
    int32_t iLength = swprintf(acMaximum, sizeof(acMaximum) / sizeof(acMaximum[0]), L"%u", uiMaximum);
    uint32_t uiPoints;

    if ((iLength > 0) && ((size_t)iLength < sizeof(acMaximum) / sizeof(acMaximum[0]))) {
        GetTextExtentPoint32W(Target, acMaximum, iLength, &Size);
    }
    uiPoints = (uint32_t)(iPlotWidth / (Size.cx + iGap)) + 1;
    if (uiPoints < 2) {
        uiPoints = 2;
    }
    if (pView->pResult->ullRoundCount < uiPoints) {
        uiPoints = (uint32_t)pView->pResult->ullRoundCount;
    }
    return uiPoints;
}

static uint64_t GetTrendFirstRound(const ChartView_t *pView, uint32_t uiPoints)
{
    uint64_t ullSelected = (0 == pView->ullSelectedRound) ? pView->pResult->ullRoundCount : pView->ullSelectedRound;
    return (ullSelected > uiPoints) ? ullSelected - uiPoints : 0;
}

static void DrawRangeNote(HDC Target, const ChartView_t *pView, RECT Note, uint64_t ullFirst, uint32_t uiPoints)
{
    wchar_t acNote[96];
    int32_t iLength;

    if (pView->pResult->ullRoundCount <= uiPoints) {
        return;
    }
    iLength = swprintf(acNote, sizeof(acNote) / sizeof(acNote[0]), L"%llu–%llu회 표시 · 회차 이동으로 범위 변경", ullFirst + 1, ullFirst + uiPoints);
    if ((iLength > 0) && ((size_t)iLength < sizeof(acNote) / sizeof(acNote[0]))) {
        DrawChartText(Target, acNote, Note, DT_LEFT);
    }
}

static void DrawTrendSource(HDC Target, const ChartView_t *pView, const TrendLine_t *pLine, RECT Bounds)
{
    const AreaGroupPlan_t *pPlan = &pView->pResult->aPlans[pLine->Area];
    wchar_t acText[96];
    int32_t iLength = pLine->bGroup
        ? swprintf(acText, sizeof(acText) / sizeof(acText[0]), L"%ls 그룹 %u · 단말 %u대", gpacAreaNames[pLine->Area], pLine->uiGroup + 1, GetGroupTerminalCount(pPlan, pLine->uiGroup))
        : swprintf(acText, sizeof(acText) / sizeof(acText[0]), L"%ls 전체", gpacAreaNames[pLine->Area]);
    HGDIOBJ PreviousFont = SelectObject(Target, pView->BoldFont);

    if ((iLength > 0) && ((size_t)iLength < sizeof(acText) / sizeof(acText[0]))) {
        DrawChartText(Target, acText, Bounds, DT_RIGHT);
    }
    SelectObject(Target, PreviousFont);
}

static void DrawCompactTrend(HDC Target, const ChartView_t *pView, const TrendLine_t *pLine, RECT Cell, uint64_t ullFirst, uint32_t uiPoints)
{
    const SimulationResult_t *pResult = pView->pResult;
    const AreaGroupPlan_t *pPlan = &pResult->aPlans[pLine->Area];
    uint32_t uiTerminals = GetGroupTerminalCount(pPlan, pLine->uiGroup);
    uint32_t uiMaximum = (0 == uiTerminals) ? 1 : uiTerminals;
    RECT Plot = { Cell.left + ScaleChart(pView, 48), Cell.top + ScaleChart(pView, 54), Cell.right - ScaleChart(pView, 16), Cell.bottom - ScaleChart(pView, 30) };
    RECT Title = { Cell.left + ScaleChart(pView, 8), Cell.top + ScaleChart(pView, 4), Cell.right - ScaleChart(pView, 8), Cell.top + ScaleChart(pView, 28) };
    wchar_t acText[64];
    int32_t iLength;
    HGDIOBJ PreviousPen;
    HGDIOBJ PreviousFont;

    SetDCBrushColor(Target, GetSysColor(COLOR_3DFACE));
    FrameRect(Target, &Cell, (HBRUSH)GetStockObject(DC_BRUSH));
    PreviousFont = SelectObject(Target, pView->BoldFont);
    iLength = swprintf(acText, sizeof(acText) / sizeof(acText[0]), L"그룹 %u · 단말 %u대 · 배정 %u칸", pLine->uiGroup + 1, uiTerminals, pPlan->auiSlotCount[pLine->uiGroup]);
    if ((iLength > 0) && ((size_t)iLength < sizeof(acText) / sizeof(acText[0]))) {
        DrawChartText(Target, acText, Title, DT_LEFT);
    }
    SelectObject(Target, PreviousFont);
    if ((Plot.right - Plot.left < ScaleChart(pView, 40)) || (Plot.bottom - Plot.top < ScaleChart(pView, 30))) {
        return;
    }

    PreviousPen = SelectObject(Target, GetStockObject(DC_PEN));
    SetDCPenColor(Target, GetSysColor(COLOR_3DFACE));
    MoveToEx(Target, Plot.left, Plot.top, NULL);
    LineTo(Target, Plot.right, Plot.top);
    SetDCPenColor(Target, GetSysColor(COLOR_WINDOWTEXT));
    MoveToEx(Target, Plot.left, Plot.top, NULL);
    LineTo(Target, Plot.left, Plot.bottom);
    LineTo(Target, Plot.right + 1, Plot.bottom);
    SelectObject(Target, PreviousPen);
    for (uint32_t uiTick = 0; uiTick < 2; uiTick++) {
        int32_t iY = (0 == uiTick) ? Plot.bottom : Plot.top;
        RECT Label = { Cell.left + ScaleChart(pView, 2), iY - ScaleChart(pView, 10), Plot.left - ScaleChart(pView, 6), iY + ScaleChart(pView, 10) };
        iLength = swprintf(acText, sizeof(acText) / sizeof(acText[0]), L"%u", (0 == uiTick) ? 0U : uiMaximum);
        if ((iLength > 0) && ((size_t)iLength < sizeof(acText) / sizeof(acText[0]))) {
            DrawChartText(Target, acText, Label, DT_RIGHT);
        }
    }
    for (uint32_t uiTick = 0; uiTick < 2; uiTick++) {
        uint64_t ullRound = ullFirst + 1 + ((0 == uiTick) ? 0 : uiPoints - 1);
        RECT Label = { Plot.left - ScaleChart(pView, 30), Plot.bottom + ScaleChart(pView, 4), Plot.right + ScaleChart(pView, 12), Plot.bottom + ScaleChart(pView, 26) };
        if (((1 == uiTick) && (uiPoints < 2)) || !FormatTrendTime(acText, sizeof(acText) / sizeof(acText[0]), ullRound * pResult->Config.ullSuperframeMilliseconds)) {
            continue;
        }
        DrawChartText(Target, acText, Label, (0 == uiTick) ? DT_LEFT : DT_RIGHT);
    }
    DrawTrendSeries(Target, pView, pLine, &Plot, uiMaximum, ullFirst, uiPoints, false, false, false);
    DrawTrendSeries(Target, pView, pLine, &Plot, uiMaximum, ullFirst, uiPoints, true, false, false);
    DrawTrendSeries(Target, pView, pLine, &Plot, uiMaximum, ullFirst, uiPoints, false, true, false);
    DrawTrendSeries(Target, pView, pLine, &Plot, uiMaximum, ullFirst, uiPoints, true, true, false);
}

static void DrawGroupCompare(HWND Window, HDC Target, const ChartView_t *pView, RECT Bounds)
{
    const SimulationResult_t *pResult = pView->pResult;
    TrendGrid_t Grid = CalculateTrendGrid(Window, pView);
    int32_t iLeft = ScaleChart(pView, 16);
    int32_t iTop = ScaleChart(pView, 44);
    int32_t iWidth = Bounds.right - 2 * iLeft;
    int32_t iHeight = Bounds.bottom - iTop - ScaleChart(pView, 40);
    int32_t iCellWidth;
    int32_t iCellHeight;
    uint32_t uiPoints;
    uint32_t uiMaximum = 1;
    uint64_t ullFirst;
    TrendLine_t Line = { .Area = pView->TrendArea, .bGroup = true };
    RECT Header = { iLeft, 0, Bounds.right - iLeft, ScaleChart(pView, 38) };
    RECT Footer = { iLeft, Bounds.bottom - ScaleChart(pView, 34), Bounds.right - iLeft, Bounds.bottom - ScaleChart(pView, 8) };
    COLORREF auiColors[2] = { gauiResultColors[SlotSuccess], gauiResultColors[SlotCollision] };

    if ((0 == Grid.uiGroups) || (0 == pResult->ullRoundCount) || (iWidth <= 0) || (iHeight <= 0)) {
        DrawChartText(Target, L"이 채널 타입에는 그룹 결과가 없습니다.", Bounds, DT_CENTER);
        return;
    }
    for (uint32_t uiSeries = 0; uiSeries < 2; uiSeries++) {
        LOGBRUSH Brush = { .lbStyle = BS_SOLID, .lbColor = auiColors[uiSeries] };
        HPEN Pen = ExtCreatePen(PS_GEOMETRIC | PS_ENDCAP_FLAT | ((0 == uiSeries) ? PS_SOLID : PS_DASH), (uint32_t)ScaleChart(pView, 2), &Brush, 0, NULL);
        HGDIOBJ PreviousPen = SelectObject(Target, (NULL != Pen) ? Pen : GetStockObject(DC_PEN));
        DrawTrendLegend(Target, pView, iLeft + (int32_t)uiSeries * ScaleChart(pView, 139), 1 == uiSeries);
        SelectObject(Target, PreviousPen);
        if (NULL != Pen) {
            DeleteObject(Pen);
        }
    }
    {
        wchar_t acTitle[64];
        int32_t iLength = swprintf(acTitle, sizeof(acTitle) / sizeof(acTitle[0]), L"%ls 그룹별 전송 추이", gpacAreaNames[pView->TrendArea]);
        HGDIOBJ PreviousFont = SelectObject(Target, pView->BoldFont);
        if ((iLength > 0) && ((size_t)iLength < sizeof(acTitle) / sizeof(acTitle[0]))) {
            DrawChartText(Target, acTitle, Header, DT_RIGHT);
        }
        SelectObject(Target, PreviousFont);
    }

    iCellWidth = iWidth / (int32_t)Grid.uiColumns;
    iCellHeight = iHeight / (int32_t)Grid.uiVisibleRows;
    for (uint32_t uiGroup = 0; uiGroup < Grid.uiGroups; uiGroup++) {
        uint32_t uiTerminals = GetGroupTerminalCount(&pResult->aPlans[pView->TrendArea], uiGroup);
        if (uiTerminals > uiMaximum) {
            uiMaximum = uiTerminals;
        }
    }
    uiPoints = CalculateTrendPoints(Target, pView, iCellWidth - ScaleChart(pView, 64), uiMaximum, ScaleChart(pView, 26));
    ullFirst = GetTrendFirstRound(pView, uiPoints);
    for (uint32_t uiRow = pView->uiTrendTopRow; (uiRow < Grid.uiRows) && (uiRow < pView->uiTrendTopRow + Grid.uiVisibleRows); uiRow++) {
        for (uint32_t uiColumn = 0; uiColumn < Grid.uiColumns; uiColumn++) {
            uint32_t uiGroup = uiRow * Grid.uiColumns + uiColumn;
            RECT Cell;

            if (uiGroup >= Grid.uiGroups) {
                break;
            }
            Cell.left = iLeft + (int32_t)uiColumn * iCellWidth + ScaleChart(pView, 3);
            Cell.right = iLeft + (int32_t)(uiColumn + 1) * iCellWidth - ScaleChart(pView, 3);
            Cell.top = iTop + (int32_t)(uiRow - pView->uiTrendTopRow) * iCellHeight + ScaleChart(pView, 3);
            Cell.bottom = iTop + (int32_t)(uiRow - pView->uiTrendTopRow + 1) * iCellHeight - ScaleChart(pView, 3);
            Line.uiGroup = uiGroup;
            DrawCompactTrend(Target, pView, &Line, Cell, ullFirst, uiPoints);
        }
    }
    DrawChartText(Target, L"전송 주기 (수신 시각, 초)", Footer, DT_RIGHT);
    DrawRangeNote(Target, pView, (RECT){ Footer.left, Footer.top, Footer.right - ScaleChart(pView, 230), Footer.bottom }, ullFirst, uiPoints);
}

static void DrawTransmissionTrend(HWND Window, HDC Target, const ChartView_t *pView, RECT Bounds)
{
    const SimulationResult_t *pResult = pView->pResult;
    RECT Plot = { ScaleChart(pView, 105), ScaleChart(pView, 65),
        Bounds.right - ScaleChart(pView, 65), Bounds.bottom - ScaleChart(pView, 86) };
    TrendLine_t Line = { .Area = pView->TrendArea };
    uint32_t uiPoints;
    uint32_t uiMaximum;
    uint64_t ullFirst;

    if ((NULL == pResult) || (0 == pResult->ullRoundCount) || (Plot.right <= Plot.left) || (Plot.bottom <= Plot.top)) {
        return;
    }
    if (IsGroupCompareShown(pView)) {
        DrawGroupCompare(Window, Target, pView, Bounds);
        return;
    }
    Line.bGroup = IsGroupedResult(pResult) && (TrendSingleGroup == pView->Trend) && (pView->uiTrendGroup < pResult->aPlans[Line.Area].uiGroupCount);
    Line.uiGroup = pView->uiTrendGroup;
    uiMaximum = Line.bGroup ? GetGroupTerminalCount(&pResult->aPlans[Line.Area], Line.uiGroup) : pResult->Config.uiTerminalCount;
    if (0 == uiMaximum) {
        uiMaximum = 1;
    }
    uiPoints = CalculateTrendPoints(Target, pView, Plot.right - Plot.left, uiMaximum, ScaleChart(pView, 40));
    ullFirst = GetTrendFirstRound(pView, uiPoints);
    DrawTrendAxes(Target, pView, &Plot, uiMaximum, ullFirst, uiPoints);
    DrawTrendSeries(Target, pView, &Line, &Plot, uiMaximum, ullFirst, uiPoints, false, false, true);
    DrawTrendSeries(Target, pView, &Line, &Plot, uiMaximum, ullFirst, uiPoints, true, false, true);
    DrawTrendSeries(Target, pView, &Line, &Plot, uiMaximum, ullFirst, uiPoints, false, true, true);
    DrawTrendSeries(Target, pView, &Line, &Plot, uiMaximum, ullFirst, uiPoints, true, true, true);
    if (IsGroupedResult(pResult)) {
        DrawTrendSource(Target, pView, &Line, (RECT){ Plot.left + ScaleChart(pView, 280), 0, Plot.right, ScaleChart(pView, 38) });
    }
    DrawRangeNote(Target, pView, (RECT){ Plot.left, Plot.bottom + ScaleChart(pView, 54), Plot.right - ScaleChart(pView, 230), Plot.bottom + ScaleChart(pView, 79) }, ullFirst, uiPoints);
}

static void DrawSlotChart(HWND Window, HDC Target, const ChartView_t *pView)
{
    RECT Bounds;
    HGDIOBJ PreviousFont = SelectObject(Target, pView->Font);

    GetClientRect(Window, &Bounds);
    FillRect(Target, &Bounds, GetSysColorBrush(COLOR_WINDOW));
    SetBkMode(Target, TRANSPARENT);
    SetTextColor(Target, GetSysColor(COLOR_WINDOWTEXT));
    if (pView->bShowTrend) {
        DrawTransmissionTrend(Window, Target, pView, Bounds);
    }
    else {
        ChartLayout_t Layout = CalculateChartLayout(Window, pView);
        DrawArea(Target, pView, &Layout, AreaAlert);
        DrawArea(Target, pView, &Layout, AreaLb);
        DrawSelection(Target, pView, &Layout);
        DrawAxes(Target, pView, &Layout);
        if (UsesGroupColors(pView)) {
            DrawGroupLegend(Target, pView, &Layout);
        }
        else if (pView->bShowResults) {
            DrawLegend(Target, pView, &Layout);
        }
    }
    SelectObject(Target, PreviousFont);
}

static void PaintSlotChart(HWND Window, const ChartView_t *pView)
{
    PAINTSTRUCT Paint;
    RECT Client;
    HDC Screen = BeginPaint(Window, &Paint);
    HDC Buffer = CreateCompatibleDC(Screen);
    HBITMAP Bitmap;

    GetClientRect(Window, &Client);
    Bitmap = CreateCompatibleBitmap(Screen, Client.right, Client.bottom);
    if ((NULL != Buffer) && (NULL != Bitmap)) {
        HGDIOBJ PreviousBitmap = SelectObject(Buffer, Bitmap);
        DrawSlotChart(Window, Buffer, pView);
        BitBlt(Screen, 0, 0, Client.right, Client.bottom, Buffer, 0, 0, SRCCOPY);
        SelectObject(Buffer, PreviousBitmap);
    }
    else {
        DrawSlotChart(Window, Screen, pView);
    }

    if (NULL != Bitmap) {
        DeleteObject(Bitmap);
    }
    if (NULL != Buffer) {
        DeleteDC(Buffer);
    }
    EndPaint(Window, &Paint);
}

/* Converts a client point to a cell. While dragging, the point is clamped to the area where the drag started. */
static bool LocateCell(HWND Window, const ChartView_t *pView, int32_t iX, int32_t iY, bool bClamp, AreaType *pArea, uint32_t *puiRow, uint32_t *puiTime)
{
    ChartLayout_t Layout = CalculateChartLayout(Window, pView);
    int32_t iWidth = Layout.iRight - Layout.iLeft;
    int64_t llTime;
    int64_t llRow;

    if ((0 == pView->uiTimeSlots) || (iWidth <= 0) || (Layout.iRowHeight <= 0)) {
        return false;
    }
    llTime = (iX < Layout.iLeft) ? -1 : (int64_t)(iX - Layout.iLeft) * pView->uiTimeSlots / iWidth;
    llRow = (iY < Layout.iTop) ? (int64_t)Layout.uiTopRow - 1 : (int64_t)Layout.uiTopRow + (iY - Layout.iTop) / Layout.iRowHeight;
    if (bClamp) {
        AreaType Area = pView->Drag.Area;
        int64_t llFirst = Layout.auiFirstRows[Area];
        int64_t llLast = llFirst + Layout.auiAreaRows[Area] - 1;

        if (llFirst < Layout.uiTopRow) {
            llFirst = Layout.uiTopRow;
        }
        if (llLast > (int64_t)Layout.uiEndRow - 1) {
            llLast = (int64_t)Layout.uiEndRow - 1;
        }
        if (llFirst > llLast) {
            return false;
        }
        llTime = (llTime < 0) ? 0 : ((llTime >= pView->uiTimeSlots) ? pView->uiTimeSlots - 1 : llTime);
        llRow = (llRow < llFirst) ? llFirst : ((llRow > llLast) ? llLast : llRow);
        *pArea = Area;
        *puiRow = GetRowFrequency(&Layout, Area, (uint32_t)llRow);
        *puiTime = (uint32_t)llTime;
        return true;
    }
    if ((llTime < 0) || (llTime >= pView->uiTimeSlots) || (llRow < Layout.uiTopRow) || (llRow >= Layout.uiEndRow)) {
        return false;
    }
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        int64_t llFirst = Layout.auiFirstRows[uiArea];
        uint32_t uiFrequency;

        if ((llRow < llFirst) || (llRow >= llFirst + Layout.auiAreaRows[uiArea])) {
            continue;
        }
        uiFrequency = GetRowFrequency(&Layout, (AreaType)uiArea, (uint32_t)llRow);
        if ((uint64_t)uiFrequency * pView->uiTimeSlots + (uint64_t)llTime < pView->auiSlots[uiArea]) {
            *pArea = (AreaType)uiArea;
            *puiRow = uiFrequency;
            *puiTime = (uint32_t)llTime;
            return true;
        }
    }
    return false;
}

static void HandleChartMouse(HWND Window, ChartView_t *pView, UINT uiMessage, LPARAM MessageData)
{
    int32_t iX = (int16_t)LOWORD(MessageData);
    int32_t iY = (int16_t)HIWORD(MessageData);
    AreaType Area;
    uint32_t uiRow;
    uint32_t uiTime;

    if (WM_LBUTTONDOWN == uiMessage) {
        if (pView->bEditable && !pView->bShowTrend && LocateCell(Window, pView, iX, iY, false, &Area, &uiRow, &uiTime) && (Area == pView->EditArea)) {
            pView->Drag = (SlotSelection_t){ .Area = Area, .uiStartTime = uiTime, .uiStartRow = uiRow, .uiEndTime = uiTime, .uiEndRow = uiRow };
            pView->bDragging = true;
            SetCapture(Window);
            InvalidateRect(Window, NULL, FALSE);
        }
    }
    else if (WM_MOUSEMOVE == uiMessage) {
        if (pView->bDragging && LocateCell(Window, pView, iX, iY, true, &Area, &uiRow, &uiTime) &&
            ((uiRow != pView->Drag.uiEndRow) || (uiTime != pView->Drag.uiEndTime))) {
            pView->Drag.uiEndRow = uiRow;
            pView->Drag.uiEndTime = uiTime;
            InvalidateRect(Window, NULL, FALSE);
        }
    }
    else if (pView->bDragging) {
        SlotSelection_t Selection = pView->Drag;
        pView->bDragging = false;
        ReleaseCapture();
        InvalidateRect(Window, NULL, FALSE);
        SendMessageW(GetParent(Window), WM_SLOTCHART_SELECT, 0, (LPARAM)&Selection);
    }
}

static LRESULT CALLBACK HandleSlotChartMessage(HWND Window, UINT uiMessage, WPARAM MessageParameter, LPARAM MessageData)
{
    ChartView_t *pView = (ChartView_t *)GetWindowLongPtrW(Window, GWLP_USERDATA);

    if (WM_NCCREATE == uiMessage) {
        const CREATESTRUCTW *pCreate = (const CREATESTRUCTW *)MessageData;
        SetWindowLongPtrW(Window, GWLP_USERDATA, (LONG_PTR)pCreate->lpCreateParams);
        return DefWindowProcW(Window, uiMessage, MessageParameter, MessageData);
    }
    if (NULL == pView) {
        return DefWindowProcW(Window, uiMessage, MessageParameter, MessageData);
    }

    switch (uiMessage) {
    case WM_SIZE:
        RefreshSlotChart(Window, pView);
        return 0;

    case WM_PAINT:
        PaintSlotChart(Window, pView);
        return 0;

    case WM_PRINTCLIENT:
        DrawSlotChart(Window, (HDC)MessageParameter, pView);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_VSCROLL:
        if (!pView->bShowTrend || IsGroupCompareShown(pView)) {
            ScrollSlotChart(Window, pView, LOWORD(MessageParameter));
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (!pView->bShowTrend || IsGroupCompareShown(pView)) {
            uint32_t uiLines = IsGroupCompareShown(pView) ? 1 : 3;
            for (uint32_t uiLine = 0; uiLine < uiLines; uiLine++) {
                ScrollSlotChart(Window, pView, (GET_WHEEL_DELTA_WPARAM(MessageParameter) > 0) ? SB_LINEUP : SB_LINEDOWN);
            }
        }
        return 0;

    case WM_LBUTTONDOWN:
    case WM_MOUSEMOVE:
    case WM_LBUTTONUP:
        HandleChartMouse(Window, pView, uiMessage, MessageData);
        return 0;

    case WM_CAPTURECHANGED:
        if (pView->bDragging && ((HWND)MessageData != Window)) {
            pView->bDragging = false;
            InvalidateRect(Window, NULL, FALSE);
        }
        return 0;

    case WM_SETCURSOR:
        if (pView->bEditable && !pView->bShowTrend && (HTCLIENT == LOWORD(MessageData))) {
            SetCursor(LoadCursorW(NULL, IDC_CROSS));
            return TRUE;
        }
        return DefWindowProcW(Window, uiMessage, MessageParameter, MessageData);

    default:
        return DefWindowProcW(Window, uiMessage, MessageParameter, MessageData);
    }
}

bool RegisterSlotChart(HINSTANCE Instance)
{
    WNDCLASSW Class = {
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = HandleSlotChartMessage,
        .hInstance = Instance,
        .hCursor = LoadCursorW(NULL, IDC_ARROW),
        .lpszClassName = gacChartClass
    };
    return 0 != RegisterClassW(&Class);
}

HWND CreateSlotChart(HWND Parent, HINSTANCE Instance, ChartView_t *pView)
{
    return CreateWindowExW(0, gacChartClass, L"주파수 및 시간 슬롯", WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, 0, 1, 1, Parent, NULL, Instance, pView);
}
