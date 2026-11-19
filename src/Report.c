#include "Report.h"
#include "ReportXml.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPORT_MAX_SHEETS (6 + AreaCount * GROUP_MAX_COUNT)
#define REPORT_MAX_UNITS (AreaCount * GROUP_MAX_COUNT)
#define REPORT_SHEET_ROWS UINT64_C(1048576)
#define REPORT_DATA_ROWS (REPORT_SHEET_ROWS - 3)
#define REPORT_BYTE_LIMIT UINT64_C(3900000000)

typedef struct PackagePart {
    char acName[64];
    uint32_t uiOffset;
    uint32_t uiSize;
    uint32_t uiCrc;
} PackagePart_t;

typedef struct WorkbookPackage {
    FILE *pFile;
    PackagePart_t *paParts;
    uint32_t uiPartCount;
    uint32_t uiPartCapacity;
    uint32_t auiCrcTable[256];
    CheckStopFunction CheckStop;
    void *pvContext;
} WorkbookPackage_t;

typedef enum SheetKind {
    SheetSummary,
    SheetGroupSettings,
    SheetGroupResults,
    SheetComparison,
    SheetRoundData,
    SheetGroupRounds,
    SheetRepeat
} SheetKind;

typedef struct GroupUnit {
    AreaType Area;
    uint32_t uiGroup;
} GroupUnit_t;

/* Sheets, group units and chart numbers of one workbook. */
typedef struct WorkbookLayout {
    const SimulationResult_t *pResult;
    bool bGrouped;
    uint32_t uiSheetCount;
    SheetKind aKinds[REPORT_MAX_SHEETS];
    uint32_t uiUnitCount;
    GroupUnit_t aUnits[REPORT_MAX_UNITS];
    uint32_t uiUnitsPerSheet;
    uint32_t uiGroupRoundSheets;
    uint32_t uiFirstGroupRoundSheet;
    uint32_t uiSummaryCharts;
    uint32_t uiChartCount;
} WorkbookLayout_t;

typedef enum ChartValue {
    ValueAreaSuccess,
    ValueAreaCollided,
    ValueGroupSuccess,
    ValueGroupCollided,
    ValueBaselineSuccess,
    ValueBaselineCollided
} ChartValue;

/* Series names live in hidden cells of the 결과 sheet so every viewer shows them in the legend. */
typedef enum SeriesName {
    SeriesSuccess,
    SeriesCollided,
    SeriesGroupedSuccess,
    SeriesBaselineSuccess,
    SeriesGroupedCollided,
    SeriesBaselineCollided,
    SeriesNameCount
} SeriesName;

typedef struct ChartSeries {
    SeriesName Name;
    const char *pcColor;
    bool bDashed;
    char cColumn;
    ChartValue Value;
} ChartSeries_t;

typedef struct ChartSpec {
    char acTitle[96];
    char acSheet[64];
    uint64_t ullFirstRow;
    AreaType Area;
    uint32_t uiGroup;
    ChartSeries_t aSeries[2];
} ChartSpec_t;

typedef struct ChartAnchor {
    uint32_t uiFromRow;
    uint32_t uiToRow;
    uint32_t uiToColumn;
    char acName[96];
} ChartAnchor_t;

static const char *const gpacAreaNames[AreaCount] = { "Alert", "LB" };
static const char *const gpacSeriesNames[SeriesNameCount] = {
    "성공 건수", "충돌 전송 건수", "성공 건수 (그룹 적용)", "성공 건수 (그룹 미적용)", "충돌 전송 건수 (그룹 적용)", "충돌 전송 건수 (그룹 미적용)"
};
#define SERIES_NAME_COLUMN 'Z'
#define SERIES_NAME_FIRST_ROW 4U

static bool CheckCancelled(const WorkbookPackage_t *pPackage)
{
    return (NULL != pPackage->CheckStop) && pPackage->CheckStop(pPackage->pvContext);
}

static void WriteLittleEndian(FILE *pFile, uint32_t uiValue, uint32_t uiBytes)
{
    for (uint32_t uiByte = 0; uiByte < uiBytes; uiByte++) {
        fputc((uint8_t)(uiValue >> (8 * uiByte)), pFile);
    }
}

static bool BeginPart(WorkbookPackage_t *pPackage, const char *pcName)
{
    FILE *pFile = pPackage->pFile;
    int64_t llOffset = _ftelli64(pFile);
    PackagePart_t *pPart;
    uint32_t uiNameLength;
    int32_t iWritten;

    if (CheckCancelled(pPackage) || (llOffset < 0) || ((uint64_t)llOffset >= UINT32_MAX) || (pPackage->uiPartCount >= UINT16_MAX)) {
        return false;
    }
    if (pPackage->uiPartCount == pPackage->uiPartCapacity) {
        uint32_t uiCapacity = (0 == pPackage->uiPartCapacity) ? 32 : 2 * pPackage->uiPartCapacity;
        PackagePart_t *paParts = realloc(pPackage->paParts, uiCapacity * sizeof(*paParts));
        if (NULL == paParts) {
            return false;
        }
        pPackage->paParts = paParts;
        pPackage->uiPartCapacity = uiCapacity;
    }
    pPart = &pPackage->paParts[pPackage->uiPartCount];
    *pPart = (PackagePart_t){ .uiOffset = (uint32_t)llOffset };
    iWritten = snprintf(pPart->acName, sizeof(pPart->acName), "%s", pcName);
    if ((iWritten < 0) || ((size_t)iWritten >= sizeof(pPart->acName))) {
        return false;
    }
    pPackage->uiPartCount++;
    uiNameLength = (uint32_t)iWritten;
    /* Stored ZIP entries need no Office installation or external compression library. */
    WriteLittleEndian(pFile, UINT32_C(0x04034b50), 4);
    WriteLittleEndian(pFile, 20, 2);
    WriteLittleEndian(pFile, 0, 4);
    WriteLittleEndian(pFile, 0, 2);
    WriteLittleEndian(pFile, 33, 2);
    WriteLittleEndian(pFile, 0, 4);
    WriteLittleEndian(pFile, 0, 4);
    WriteLittleEndian(pFile, 0, 4);
    WriteLittleEndian(pFile, uiNameLength, 2);
    WriteLittleEndian(pFile, 0, 2);
    return (uiNameLength == fwrite(pPart->acName, 1, uiNameLength, pFile)) && (0 == ferror(pFile));
}

static bool BeginNumberedPart(WorkbookPackage_t *pPackage, const char *pcFormat, uint32_t uiNumber)
{
    char acName[64];
    int32_t iWritten = snprintf(acName, sizeof(acName), pcFormat, uiNumber);

    return (iWritten > 0) && ((size_t)iWritten < sizeof(acName)) && BeginPart(pPackage, acName);
}

static bool FinishPart(WorkbookPackage_t *pPackage)
{
    PackagePart_t *pPart = &pPackage->paParts[pPackage->uiPartCount - 1];
    FILE *pFile = pPackage->pFile;
    int64_t llEnd = _ftelli64(pFile);
    uint64_t ullStart = pPart->uiOffset + UINT64_C(30) + strlen(pPart->acName);
    uint8_t aucBuffer[16384];
    uint32_t uiRemaining;
    uint32_t uiCrc = UINT32_MAX;

    if ((0 != ferror(pFile)) || (llEnd < 0) || ((uint64_t)llEnd < ullStart) || ((uint64_t)llEnd >= UINT32_MAX) ||
        (0 != _fseeki64(pFile, (int64_t)ullStart, SEEK_SET))) {
        return false;
    }
    pPart->uiSize = (uint32_t)((uint64_t)llEnd - ullStart);
    uiRemaining = pPart->uiSize;
    while (0 != uiRemaining) {
        size_t uBytes = (uiRemaining < sizeof(aucBuffer)) ? uiRemaining : sizeof(aucBuffer);
        if (CheckCancelled(pPackage) || (uBytes != fread(aucBuffer, 1, uBytes, pFile))) {
            return false;
        }
        for (size_t uIndex = 0; uIndex < uBytes; uIndex++) {
            uiCrc = pPackage->auiCrcTable[(uiCrc ^ aucBuffer[uIndex]) & 255] ^ (uiCrc >> 8);
        }
        uiRemaining -= (uint32_t)uBytes;
    }
    pPart->uiCrc = uiCrc ^ UINT32_MAX;
    if (0 != _fseeki64(pFile, pPart->uiOffset + 14, SEEK_SET)) {
        return false;
    }
    WriteLittleEndian(pFile, pPart->uiCrc, 4);
    WriteLittleEndian(pFile, pPart->uiSize, 4);
    WriteLittleEndian(pFile, pPart->uiSize, 4);
    return (0 == ferror(pFile)) && (0 == _fseeki64(pFile, llEnd, SEEK_SET));
}

static bool WritePart(WorkbookPackage_t *pPackage, const char *pcName, const char *pcXml)
{
    return BeginPart(pPackage, pcName) && (fputs(pcXml, pPackage->pFile) >= 0) && FinishPart(pPackage);
}

static bool FinishPackage(WorkbookPackage_t *pPackage)
{
    FILE *pFile = pPackage->pFile;
    int64_t llStart = _ftelli64(pFile);
    int64_t llEnd;

    if ((llStart < 0) || ((uint64_t)llStart >= UINT32_MAX)) {
        return false;
    }
    for (uint32_t uiIndex = 0; uiIndex < pPackage->uiPartCount; uiIndex++) {
        const PackagePart_t *pPart = &pPackage->paParts[uiIndex];
        uint32_t uiLength = (uint32_t)strlen(pPart->acName);
        WriteLittleEndian(pFile, UINT32_C(0x02014b50), 4);
        WriteLittleEndian(pFile, 20, 2);
        WriteLittleEndian(pFile, 20, 2);
        WriteLittleEndian(pFile, 0, 4);
        WriteLittleEndian(pFile, 0, 2);
        WriteLittleEndian(pFile, 33, 2);
        WriteLittleEndian(pFile, pPart->uiCrc, 4);
        WriteLittleEndian(pFile, pPart->uiSize, 4);
        WriteLittleEndian(pFile, pPart->uiSize, 4);
        WriteLittleEndian(pFile, uiLength, 2);
        WriteLittleEndian(pFile, 0, 4);
        WriteLittleEndian(pFile, 0, 4);
        WriteLittleEndian(pFile, 0, 4);
        WriteLittleEndian(pFile, pPart->uiOffset, 4);
        if (uiLength != fwrite(pPart->acName, 1, uiLength, pFile)) {
            return false;
        }
    }
    llEnd = _ftelli64(pFile);
    if ((llEnd < llStart) || ((uint64_t)llEnd >= UINT32_MAX)) {
        return false;
    }
    WriteLittleEndian(pFile, UINT32_C(0x06054b50), 4);
    WriteLittleEndian(pFile, 0, 4);
    WriteLittleEndian(pFile, pPackage->uiPartCount, 2);
    WriteLittleEndian(pFile, pPackage->uiPartCount, 2);
    WriteLittleEndian(pFile, (uint32_t)(llEnd - llStart), 4);
    WriteLittleEndian(pFile, (uint32_t)llStart, 4);
    WriteLittleEndian(pFile, 0, 2);
    return 0 == ferror(pFile);
}

static void BuildWorkbookLayout(const SimulationResult_t *pResult, WorkbookLayout_t *pLayout)
{
    memset(pLayout, 0, sizeof(*pLayout));
    pLayout->pResult = pResult;
    pLayout->bGrouped = IsGroupedResult(pResult);
    pLayout->aKinds[pLayout->uiSheetCount++] = SheetSummary;
    if (!pLayout->bGrouped) {
        pLayout->aKinds[pLayout->uiSheetCount++] = SheetRoundData;
        pLayout->uiSummaryCharts = AreaCount;
        pLayout->uiChartCount = AreaCount;
        if (IsRepeatedResult(pResult)) {
            pLayout->aKinds[pLayout->uiSheetCount++] = SheetRepeat;
        }
        return;
    }
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        for (uint32_t uiGroup = 0; uiGroup < pResult->aPlans[uiArea].uiGroupCount; uiGroup++) {
            pLayout->aUnits[pLayout->uiUnitCount++] = (GroupUnit_t){ (AreaType)uiArea, uiGroup };
        }
    }
    pLayout->uiUnitsPerSheet = pLayout->uiUnitCount;
    if ((0 != pResult->ullRoundCount) && (REPORT_DATA_ROWS / pResult->ullRoundCount < pLayout->uiUnitCount)) {
        pLayout->uiUnitsPerSheet = (uint32_t)(REPORT_DATA_ROWS / pResult->ullRoundCount);
    }
    pLayout->uiGroupRoundSheets = (pLayout->uiUnitCount + pLayout->uiUnitsPerSheet - 1) / pLayout->uiUnitsPerSheet;
    pLayout->aKinds[pLayout->uiSheetCount++] = SheetGroupSettings;
    pLayout->aKinds[pLayout->uiSheetCount++] = SheetGroupResults;
    pLayout->aKinds[pLayout->uiSheetCount++] = SheetComparison;
    pLayout->aKinds[pLayout->uiSheetCount++] = SheetRoundData;
    pLayout->uiFirstGroupRoundSheet = pLayout->uiSheetCount;
    for (uint32_t uiSheet = 0; uiSheet < pLayout->uiGroupRoundSheets; uiSheet++) {
        pLayout->aKinds[pLayout->uiSheetCount++] = SheetGroupRounds;
    }
    /* The repeat sheet comes last so that the sheet numbers used by the drawings do not move. */
    if (IsRepeatedResult(pResult)) {
        pLayout->aKinds[pLayout->uiSheetCount++] = SheetRepeat;
    }
    pLayout->uiSummaryCharts = 3 * AreaCount;
    pLayout->uiChartCount = pLayout->uiSummaryCharts + pLayout->uiUnitCount;
}

static bool FormatGroupRoundSheetName(char *pcName, size_t uCapacity, uint32_t uiRoundSheet)
{
    int32_t iWritten = (0 == uiRoundSheet) ? snprintf(pcName, uCapacity, "그룹별 회차 데이터")
        : snprintf(pcName, uCapacity, "그룹별 회차 데이터 %u", uiRoundSheet + 1);

    return (iWritten > 0) && ((size_t)iWritten < uCapacity);
}

static bool FormatSheetName(const WorkbookLayout_t *pLayout, uint32_t uiSheet, char *pcName, size_t uCapacity)
{
    const char *const pacNames[] = { "결과", "그룹 설정", "그룹별 결과", "미적용 비교", "회차별 데이터" };
    SheetKind Kind = pLayout->aKinds[uiSheet];
    int32_t iWritten;

    if (SheetGroupRounds == Kind) {
        return FormatGroupRoundSheetName(pcName, uCapacity, uiSheet - pLayout->uiFirstGroupRoundSheet);
    }
    if (SheetRepeat == Kind) {
        iWritten = snprintf(pcName, uCapacity, "반복 실행");
        return (iWritten > 0) && ((size_t)iWritten < uCapacity);
    }
    iWritten = snprintf(pcName, uCapacity, "%s", pacNames[Kind]);
    return (iWritten > 0) && ((size_t)iWritten < uCapacity);
}

static bool WriteWorkbookParts(WorkbookPackage_t *pPackage, const WorkbookLayout_t *pLayout)
{
    FILE *pFile = pPackage->pFile;
    uint32_t uiDrawings = pLayout->bGrouped ? 2 : 1;

    if (!BeginPart(pPackage, "[Content_Types].xml")) {
        return false;
    }
    fputs(CONTENT_TYPES_START, pFile);
    for (uint32_t uiSheet = 0; uiSheet < pLayout->uiSheetCount; uiSheet++) {
        fprintf(pFile, "<Override PartName=\"/xl/worksheets/sheet%u.xml\" ContentType=\"" WORKSHEET_TYPE "\"/>", uiSheet + 1);
    }
    for (uint32_t uiDrawing = 0; uiDrawing < uiDrawings; uiDrawing++) {
        fprintf(pFile, "<Override PartName=\"/xl/drawings/drawing%u.xml\" ContentType=\"" DRAWING_TYPE "\"/>", uiDrawing + 1);
    }
    for (uint32_t uiChart = 0; uiChart < pLayout->uiChartCount; uiChart++) {
        fprintf(pFile, "<Override PartName=\"/xl/charts/chart%u.xml\" ContentType=\"" CHART_TYPE "\"/>", uiChart + 1);
    }
    if ((fputs("</Types>", pFile) < 0) || !FinishPart(pPackage) ||
        !WritePart(pPackage, "_rels/.rels", RELATIONSHIPS
        "<Relationship Id=\"rId1\" Type=\"" REL_NS "/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>") ||
        !BeginPart(pPackage, "xl/workbook.xml")) {
        return false;
    }
    fputs(XML_DECL "<workbook xmlns=\"" SHEET_NS "\" xmlns:r=\"" REL_NS "\"><bookViews><workbookView activeTab=\"0\"/></bookViews><sheets>", pFile);
    for (uint32_t uiSheet = 0; uiSheet < pLayout->uiSheetCount; uiSheet++) {
        char acName[64];
        if (!FormatSheetName(pLayout, uiSheet, acName, sizeof(acName))) {
            return false;
        }
        fprintf(pFile, "<sheet name=\"%s\" sheetId=\"%u\" r:id=\"rId%u\"/>", acName, uiSheet + 1, uiSheet + 1);
    }
    if ((fputs("</sheets></workbook>", pFile) < 0) || !FinishPart(pPackage) || !BeginPart(pPackage, "xl/_rels/workbook.xml.rels")) {
        return false;
    }
    fputs(RELATIONSHIPS, pFile);
    for (uint32_t uiSheet = 0; uiSheet < pLayout->uiSheetCount; uiSheet++) {
        fprintf(pFile, "<Relationship Id=\"rId%u\" Type=\"" REL_NS "/worksheet\" Target=\"worksheets/sheet%u.xml\"/>", uiSheet + 1, uiSheet + 1);
    }
    fprintf(pFile, "<Relationship Id=\"rId%u\" Type=\"" REL_NS "/styles\" Target=\"styles.xml\"/></Relationships>", pLayout->uiSheetCount + 1);
    return FinishPart(pPackage) && WritePart(pPackage, "xl/styles.xml", gacStyles);
}

static void WriteTextCell(FILE *pFile, char cColumn, uint32_t uiRow, uint32_t uiStyle, const char *pcText)
{
    /* All labels are XML-safe constants or generated numbers, never user-supplied strings. */
    fprintf(pFile, "<c r=\"%c%u\" s=\"%u\" t=\"inlineStr\"><is><t>%s</t></is></c>", cColumn, uiRow, uiStyle, pcText);
}

static void WriteGroupNameCell(FILE *pFile, char cColumn, uint32_t uiRow, uint32_t uiGroup)
{
    fprintf(pFile, "<c r=\"%c%u\" s=\"3\" t=\"inlineStr\"><is><t>그룹 %u</t></is></c>", cColumn, uiRow, uiGroup + 1);
}

static void WriteCountCell(FILE *pFile, char cColumn, uint32_t uiRow, uint64_t ullValue)
{
    if (ullValue > UINT64_C(999999999999999)) {
        fprintf(pFile, "<c r=\"%c%u\" s=\"4\" t=\"inlineStr\"><is><t>%llu</t></is></c>", cColumn, uiRow, ullValue);
    }
    else {
        fprintf(pFile, "<c r=\"%c%u\" s=\"4\"><v>%llu</v></c>", cColumn, uiRow, ullValue);
    }
}

static void WriteDifferenceCell(FILE *pFile, char cColumn, uint32_t uiRow, uint64_t ullGrouped, uint64_t ullBaseline)
{
    uint64_t ullDifference = (ullGrouped >= ullBaseline) ? ullGrouped - ullBaseline : ullBaseline - ullGrouped;

    if (ullGrouped >= ullBaseline) {
        WriteCountCell(pFile, cColumn, uiRow, ullDifference);
    }
    else if (ullDifference > UINT64_C(999999999999999)) {
        fprintf(pFile, "<c r=\"%c%u\" s=\"4\" t=\"inlineStr\"><is><t>-%llu</t></is></c>", cColumn, uiRow, ullDifference);
    }
    else {
        fprintf(pFile, "<c r=\"%c%u\" s=\"4\"><v>-%llu</v></c>", cColumn, uiRow, ullDifference);
    }
}

static void WriteTimeCell(FILE *pFile, char cColumn, uint32_t uiRow, uint64_t ullMilliseconds)
{
    fprintf(pFile, "<c r=\"%c%u\" s=\"5\"><v>%llu.%03u</v></c>", cColumn, uiRow,
        ullMilliseconds / 1000, (uint32_t)(ullMilliseconds % 1000));
}

static void WriteComparisonRows(FILE *pFile, const SimulationResult_t *pResult)
{
    const char *const pacHeaders[] = { "채널 타입", "구분", "누적 성공 건수", "누적 충돌 전송 건수", "누적 충돌 칸 수", "누적 유휴 칸 수", "최종 접속 실패 단말 갯수" };

    fputs("<row r=\"56\">", pFile);
    WriteTextCell(pFile, 'A', 56, 1, "그룹 적용 · 미적용 비교 (같은 시드)");
    fputs("</row><row r=\"57\" ht=\"40\" customHeight=\"1\">", pFile);
    for (uint32_t uiColumn = 0; uiColumn < 7; uiColumn++) {
        WriteTextCell(pFile, (char)('A' + uiColumn), 57, 2, pacHeaders[uiColumn]);
    }
    fputs("</row>", pFile);
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        const SlotCounts_t *pGrouped = &pResult->aTotals[uiArea];
        const SlotCounts_t *pBaseline = &pResult->aBaselineTotals[uiArea];
        uint32_t uiRow = 58 + 3 * uiArea;

        fprintf(pFile, "<row r=\"%u\">", uiRow);
        WriteTextCell(pFile, 'A', uiRow, 3, gpacAreaNames[uiArea]);
        WriteTextCell(pFile, 'B', uiRow, 3, "그룹 적용");
        WriteCountCell(pFile, 'C', uiRow, pGrouped->ullSuccess);
        WriteCountCell(pFile, 'D', uiRow, pGrouped->ullCollidedAttempts);
        WriteCountCell(pFile, 'E', uiRow, pGrouped->ullCollision);
        WriteCountCell(pFile, 'F', uiRow, pGrouped->ullIdle);
        WriteCountCell(pFile, 'G', uiRow, pResult->auiUnsuccessful[uiArea]);
        fprintf(pFile, "</row><row r=\"%u\">", uiRow + 1);
        WriteTextCell(pFile, 'A', uiRow + 1, 3, gpacAreaNames[uiArea]);
        WriteTextCell(pFile, 'B', uiRow + 1, 3, "그룹 미적용");
        WriteCountCell(pFile, 'C', uiRow + 1, pBaseline->ullSuccess);
        WriteCountCell(pFile, 'D', uiRow + 1, pBaseline->ullCollidedAttempts);
        WriteCountCell(pFile, 'E', uiRow + 1, pBaseline->ullCollision);
        WriteCountCell(pFile, 'F', uiRow + 1, pBaseline->ullIdle);
        WriteCountCell(pFile, 'G', uiRow + 1, pResult->auiBaselineUnsuccessful[uiArea]);
        fprintf(pFile, "</row><row r=\"%u\">", uiRow + 2);
        WriteTextCell(pFile, 'A', uiRow + 2, 3, gpacAreaNames[uiArea]);
        WriteTextCell(pFile, 'B', uiRow + 2, 3, "차이");
        WriteDifferenceCell(pFile, 'C', uiRow + 2, pGrouped->ullSuccess, pBaseline->ullSuccess);
        WriteDifferenceCell(pFile, 'D', uiRow + 2, pGrouped->ullCollidedAttempts, pBaseline->ullCollidedAttempts);
        WriteDifferenceCell(pFile, 'E', uiRow + 2, pGrouped->ullCollision, pBaseline->ullCollision);
        WriteDifferenceCell(pFile, 'F', uiRow + 2, pGrouped->ullIdle, pBaseline->ullIdle);
        WriteDifferenceCell(pFile, 'G', uiRow + 2, pResult->auiUnsuccessful[uiArea], pResult->auiBaselineUnsuccessful[uiArea]);
        fputs("</row>", pFile);
    }
    fputs("<row r=\"64\">", pFile);
    WriteTextCell(pFile, 'A', 64, 6, "그룹 미적용 값은 같은 시드와 파라미터로 그룹 없이 계산한 기존 방식의 결과입니다. 차이 = 그룹 적용 − 그룹 미적용. 회차별 값은 미적용 비교 시트에 있습니다.");
    fputs("</row>", pFile);
}

static bool WriteSummary(WorkbookPackage_t *pPackage, const WorkbookLayout_t *pLayout)
{
    FILE *pFile = pPackage->pFile;
    const SimulationResult_t *pResult = pLayout->pResult;
    const SimulationConfig_t *pConfig = &pResult->Config;
    const char *const pacSettings[] = { "심볼레이트 (sps)", "Wave ID", "슈퍼프레임 주기 (초)", "시뮬레이션 시간 (초)", "단말 수" };
    const uint64_t aullValues[] = { pConfig->uiSymbolRate, pConfig->uiWaveId, pConfig->ullSuperframeMilliseconds, pConfig->ullDurationMilliseconds, pConfig->uiTerminalCount };
    const char *const pacFinal[] = { "채널 타입", "누적 성공 건수", "누적 충돌 전송 건수", "누적 충돌 칸 수", "누적 유휴 칸 수", "최종 접속 실패 단말 갯수" };

    if (!BeginPart(pPackage, "xl/worksheets/sheet1.xml")) {
        return false;
    }
    fprintf(pFile, SHEET_START "<dimension ref=\"A1:Z%u\"/><sheetViews><sheetView showGridLines=\"0\" workbookViewId=\"0\"/></sheetViews>"
        "<sheetFormatPr defaultRowHeight=\"24\"/><cols><col min=\"1\" max=\"1\" width=\"26\" customWidth=\"1\"/>"
        "<col min=\"2\" max=\"5\" width=\"23\" customWidth=\"1\"/><col min=\"6\" max=\"8\" width=\"25\" customWidth=\"1\"/>"
        "<col min=\"26\" max=\"26\" width=\"28\" hidden=\"1\" customWidth=\"1\"/></cols><sheetData>"
        "<row r=\"1\" ht=\"34\" customHeight=\"1\">", pLayout->bGrouped ? 64U : 54U);
    WriteTextCell(pFile, 'A', 1, 1, "Slotted Aloha 채널 경쟁 상황 시뮬레이션");
    fputs("</row><row r=\"3\">", pFile);
    WriteTextCell(pFile, 'A', 3, 1, "설정 값");
    WriteTextCell(pFile, 'E', 3, 1, "최종 접속 실패 단말 갯수");
    fputs("</row><row r=\"4\" ht=\"32\" customHeight=\"1\">", pFile);
    WriteTextCell(pFile, 'A', 4, 2, "항목");
    WriteTextCell(pFile, 'B', 4, 2, "값");
    WriteTextCell(pFile, 'E', 4, 2, "채널 타입");
    WriteTextCell(pFile, 'F', 4, 2, "채널 수");
    WriteTextCell(pFile, 'G', 4, 2, "단말 수");
    WriteTextCell(pFile, 'H', 4, 2, "최종 접속 실패 단말 갯수");
    WriteTextCell(pFile, SERIES_NAME_COLUMN, SERIES_NAME_FIRST_ROW, 3, gpacSeriesNames[SeriesSuccess]);
    fputs("</row>", pFile);
    for (uint32_t uiIndex = 0; uiIndex < 5; uiIndex++) {
        uint32_t uiRow = uiIndex + 5;
        uint32_t uiSeries = uiRow - SERIES_NAME_FIRST_ROW;
        fprintf(pFile, "<row r=\"%u\">", uiRow);
        WriteTextCell(pFile, 'A', uiRow, 3, pacSettings[uiIndex]);
        if ((2 == uiIndex) || (3 == uiIndex)) {
            WriteTimeCell(pFile, 'B', uiRow, aullValues[uiIndex]);
        }
        else {
            WriteCountCell(pFile, 'B', uiRow, aullValues[uiIndex]);
        }
        if (uiIndex < AreaCount) {
            WriteTextCell(pFile, 'E', uiRow, 3, (AreaAlert == uiIndex) ? "Alert" : "LB");
            WriteCountCell(pFile, 'F', uiRow, pConfig->auiSlots[uiIndex]);
            WriteCountCell(pFile, 'G', uiRow, pConfig->uiTerminalCount);
            WriteCountCell(pFile, 'H', uiRow, pResult->auiUnsuccessful[uiIndex]);
        }
        if (3 == uiIndex) {
            WriteTextCell(pFile, 'E', uiRow, 6, "종료 시점까지 해당 채널 타입에서 한 번도 성공하지 못한 단말 수입니다. 완료 회차가 없거나 채널 수가 0인 채널 타입은 집계하지 않습니다.");
        }
        if ((SeriesCollided == uiSeries) || pLayout->bGrouped) {
            WriteTextCell(pFile, SERIES_NAME_COLUMN, uiRow, 3, gpacSeriesNames[uiSeries]);
        }
        fputs("</row>", pFile);
    }
    if (pLayout->bGrouped) {
        fputs("<row r=\"10\">", pFile);
        WriteTextCell(pFile, 'A', 10, 3, "그룹 수");
        WriteCountCell(pFile, 'B', 10, pConfig->uiGroupCount);
        fputs("</row>", pFile);
    }
    fputs("<row r=\"11\">", pFile);
    if ((0 != pResult->ullRoundCount) && IsRepeatedResult(pResult)) {
        fprintf(pFile, "<c r=\"A11\" s=\"6\" t=\"inlineStr\"><is><t>반복 실행 %u회 중 1번째 실행의 결과입니다. 반복 평균·표준편차는 반복 실행 시트에 있습니다. "
            "각 지점의 숫자는 해당 회차의 전송 단말 수입니다.</t></is></c>", pResult->uiRepeatCount);
    }
    else {
        WriteTextCell(pFile, 'A', 11, 6, (0 == pResult->ullRoundCount) ? "완료된 수신 회차가 없습니다. 그래프에 표시할 데이터가 없습니다."
            : "각 지점의 숫자는 해당 회차의 전송 단말 수입니다. 회차별 데이터 시트에 모든 회차와 누적 값을 기록했습니다.");
    }
    fputs("</row><row r=\"50\">", pFile);
    WriteTextCell(pFile, 'A', 50, 1, "Alert · LB 최종 결과");
    fputs("</row><row r=\"51\" ht=\"40\" customHeight=\"1\">", pFile);
    for (uint32_t uiColumn = 0; uiColumn < 6; uiColumn++) {
        WriteTextCell(pFile, (char)('A' + uiColumn), 51, 2, pacFinal[uiColumn]);
    }
    fputs("</row>", pFile);
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        uint32_t uiRow = 52 + uiArea;
        const SlotCounts_t *pCounts = &pResult->aTotals[uiArea];
        fprintf(pFile, "<row r=\"%u\">", uiRow);
        WriteTextCell(pFile, 'A', uiRow, 3, (AreaAlert == uiArea) ? "Alert" : "LB");
        WriteCountCell(pFile, 'B', uiRow, pCounts->ullSuccess);
        WriteCountCell(pFile, 'C', uiRow, pCounts->ullCollidedAttempts);
        WriteCountCell(pFile, 'D', uiRow, pCounts->ullCollision);
        WriteCountCell(pFile, 'E', uiRow, pCounts->ullIdle);
        WriteCountCell(pFile, 'F', uiRow, pResult->auiUnsuccessful[uiArea]);
        fputs("</row>", pFile);
    }
    fputs("<row r=\"54\">", pFile);
    WriteTextCell(pFile, 'A', 54, 6, "누적 값은 1회차부터 마지막 수신 회차까지의 합계입니다. 동일 단말의 여러 전송을 각각 셉니다.");
    fputs("</row>", pFile);
    if (pLayout->bGrouped) {
        WriteComparisonRows(pFile, pResult);
    }
    fprintf(pFile, "</sheetData><mergeCells count=\"%u\"><mergeCell ref=\"A1:H1\"/><mergeCell ref=\"A3:C3\"/>"
        "<mergeCell ref=\"E3:H3\"/><mergeCell ref=\"E8:H9\"/><mergeCell ref=\"A11:H11\"/>"
        "<mergeCell ref=\"A50:H50\"/><mergeCell ref=\"A54:H54\"/>%s</mergeCells><drawing r:id=\"rId1\"/></worksheet>",
        pLayout->bGrouped ? 9U : 7U, pLayout->bGrouped ? "<mergeCell ref=\"A56:H56\"/><mergeCell ref=\"A64:H64\"/>" : "");
    return FinishPart(pPackage);
}

static bool WriteRoundData(WorkbookPackage_t *pPackage, const SimulationResult_t *pResult, uint32_t uiSheet)
{
    FILE *pFile = pPackage->pFile;
    uint64_t aullSuccess[AreaCount] = { 0 };
    uint64_t aullCollision[AreaCount] = { 0 };
    const char *const pacHeaders[] = { "회차", "수신 시각 (초)",
        "Alert&#10;성공 건수", "Alert&#10;충돌 전송 건수", "Alert&#10;충돌 칸 수", "Alert&#10;유휴 칸 수", "Alert&#10;누적 성공 건수", "Alert&#10;누적 충돌 전송 건수",
        "LB&#10;성공 건수", "LB&#10;충돌 전송 건수", "LB&#10;충돌 칸 수", "LB&#10;유휴 칸 수", "LB&#10;누적 성공 건수", "LB&#10;누적 충돌 전송 건수" };

    if (!BeginNumberedPart(pPackage, "xl/worksheets/sheet%u.xml", uiSheet)) {
        return false;
    }
    fputs(SHEET_START "<sheetViews><sheetView showGridLines=\"0\" workbookViewId=\"0\">"
        "<pane xSplit=\"2\" ySplit=\"3\" topLeftCell=\"C4\" activePane=\"bottomRight\" state=\"frozen\"/>"
        "</sheetView></sheetViews><sheetFormatPr defaultRowHeight=\"24\"/><cols>"
        "<col min=\"1\" max=\"1\" width=\"12\" customWidth=\"1\"/><col min=\"2\" max=\"14\" width=\"24\" customWidth=\"1\"/></cols><sheetData>"
        "<row r=\"1\" ht=\"32\" customHeight=\"1\">", pFile);
    WriteTextCell(pFile, 'A', 1, 1, "회차별 전송 결과");
    fputs("</row><row r=\"2\">", pFile);
    WriteTextCell(pFile, 'A', 2, 6, "누적 성공 건수와 누적 충돌 전송 건수는 해당 행의 회차까지 더한 값입니다.");
    fputs("</row><row r=\"3\" ht=\"42\" customHeight=\"1\">", pFile);
    for (uint32_t uiColumn = 0; uiColumn < 14; uiColumn++) {
        WriteTextCell(pFile, (char)('A' + uiColumn), 3, 2, pacHeaders[uiColumn]);
    }
    fputs("</row>", pFile);
    for (uint64_t ullIndex = 0; ullIndex < pResult->ullRoundCount; ullIndex++) {
        uint32_t uiRow = (uint32_t)ullIndex + 4;
        if (CheckCancelled(pPackage) || (0 != ferror(pFile))) {
            return false;
        }
        fprintf(pFile, "<row r=\"%u\">", uiRow);
        WriteCountCell(pFile, 'A', uiRow, ullIndex + 1);
        WriteTimeCell(pFile, 'B', uiRow, (ullIndex + 1) * pResult->Config.ullSuperframeMilliseconds);
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            RoundAreaCounts_t Counts = GetRoundAreaCounts(pResult, ullIndex, (AreaType)uiArea);
            char cFirst = (AreaAlert == uiArea) ? 'C' : 'I';
            aullSuccess[uiArea] += Counts.uiSuccess;
            aullCollision[uiArea] += Counts.uiCollidedAttempts;
            WriteCountCell(pFile, cFirst, uiRow, Counts.uiSuccess);
            WriteCountCell(pFile, cFirst + 1, uiRow, Counts.uiCollidedAttempts);
            WriteCountCell(pFile, cFirst + 2, uiRow, Counts.uiCollision);
            WriteCountCell(pFile, cFirst + 3, uiRow, pResult->Config.auiSlots[uiArea] - Counts.uiSuccess - Counts.uiCollision);
            WriteCountCell(pFile, cFirst + 4, uiRow, aullSuccess[uiArea]);
            WriteCountCell(pFile, cFirst + 5, uiRow, aullCollision[uiArea]);
        }
        fputs("</row>", pFile);
    }
    fprintf(pFile, "</sheetData><autoFilter ref=\"A3:N%llu\"/><mergeCells count=\"2\">"
        "<mergeCell ref=\"A1:N1\"/><mergeCell ref=\"A2:N2\"/></mergeCells></worksheet>", pResult->ullRoundCount + 3);
    return FinishPart(pPackage);
}

static bool WriteComparisonSheet(WorkbookPackage_t *pPackage, const SimulationResult_t *pResult, uint32_t uiSheet)
{
    FILE *pFile = pPackage->pFile;
    uint64_t aaullSums[AreaCount][4] = { { 0 } };
    const char *const pacMeasures[] = { "성공 건수 (그룹 적용)", "성공 건수 (미적용)", "충돌 전송 건수 (그룹 적용)", "충돌 전송 건수 (미적용)",
        "누적 성공 (그룹 적용)", "누적 성공 (미적용)", "누적 충돌 전송 (그룹 적용)", "누적 충돌 전송 (미적용)" };

    if (!BeginNumberedPart(pPackage, "xl/worksheets/sheet%u.xml", uiSheet)) {
        return false;
    }
    fputs(SHEET_START "<sheetViews><sheetView showGridLines=\"0\" workbookViewId=\"0\">"
        "<pane xSplit=\"2\" ySplit=\"3\" topLeftCell=\"C4\" activePane=\"bottomRight\" state=\"frozen\"/>"
        "</sheetView></sheetViews><sheetFormatPr defaultRowHeight=\"24\"/><cols>"
        "<col min=\"1\" max=\"1\" width=\"12\" customWidth=\"1\"/><col min=\"2\" max=\"18\" width=\"22\" customWidth=\"1\"/></cols><sheetData>"
        "<row r=\"1\" ht=\"32\" customHeight=\"1\">", pFile);
    WriteTextCell(pFile, 'A', 1, 1, "그룹 적용 · 미적용 회차별 비교");
    fputs("</row><row r=\"2\">", pFile);
    WriteTextCell(pFile, 'A', 2, 6, "그룹 미적용 값은 같은 시드와 파라미터로 그룹 없이 계산한 결과입니다. 누적 값은 해당 행의 회차까지 더한 값입니다.");
    fputs("</row><row r=\"3\" ht=\"48\" customHeight=\"1\">", pFile);
    WriteTextCell(pFile, 'A', 3, 2, "회차");
    WriteTextCell(pFile, 'B', 3, 2, "수신 시각 (초)");
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        for (uint32_t uiMeasure = 0; uiMeasure < 8; uiMeasure++) {
            fprintf(pFile, "<c r=\"%c3\" s=\"2\" t=\"inlineStr\"><is><t>%s&#10;%s</t></is></c>", (char)('C' + 8 * uiArea + uiMeasure), gpacAreaNames[uiArea], pacMeasures[uiMeasure]);
        }
    }
    fputs("</row>", pFile);
    for (uint64_t ullIndex = 0; ullIndex < pResult->ullRoundCount; ullIndex++) {
        uint32_t uiRow = (uint32_t)ullIndex + 4;
        if (CheckCancelled(pPackage) || (0 != ferror(pFile))) {
            return false;
        }
        fprintf(pFile, "<row r=\"%u\">", uiRow);
        WriteCountCell(pFile, 'A', uiRow, ullIndex + 1);
        WriteTimeCell(pFile, 'B', uiRow, (ullIndex + 1) * pResult->Config.ullSuperframeMilliseconds);
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            RoundAreaCounts_t Grouped = GetRoundAreaCounts(pResult, ullIndex, (AreaType)uiArea);
            RoundAreaCounts_t Baseline = GetBaselineRoundCounts(pResult, ullIndex, (AreaType)uiArea);
            char cFirst = (char)('C' + 8 * uiArea);
            aaullSums[uiArea][0] += Grouped.uiSuccess;
            aaullSums[uiArea][1] += Baseline.uiSuccess;
            aaullSums[uiArea][2] += Grouped.uiCollidedAttempts;
            aaullSums[uiArea][3] += Baseline.uiCollidedAttempts;
            WriteCountCell(pFile, cFirst, uiRow, Grouped.uiSuccess);
            WriteCountCell(pFile, cFirst + 1, uiRow, Baseline.uiSuccess);
            WriteCountCell(pFile, cFirst + 2, uiRow, Grouped.uiCollidedAttempts);
            WriteCountCell(pFile, cFirst + 3, uiRow, Baseline.uiCollidedAttempts);
            for (uint32_t uiSum = 0; uiSum < 4; uiSum++) {
                WriteCountCell(pFile, cFirst + 4 + (char)uiSum, uiRow, aaullSums[uiArea][uiSum]);
            }
        }
        fputs("</row>", pFile);
    }
    fprintf(pFile, "</sheetData><autoFilter ref=\"A3:R%llu\"/><mergeCells count=\"2\">"
        "<mergeCell ref=\"A1:R1\"/><mergeCell ref=\"A2:R2\"/></mergeCells></worksheet>", pResult->ullRoundCount + 3);
    return FinishPart(pPackage);
}

static uint64_t CountSlotRuns(const SimulationResult_t *pResult)
{
    uint32_t uiTimeSlots = GetTimeSlotCount(pResult->Config.uiSymbolRate, pResult->Config.uiWaveId);
    uint64_t ullRuns = 0;

    for (uint32_t uiArea = 0; (0 != uiTimeSlots) && (uiArea < AreaCount); uiArea++) {
        const AreaGroupPlan_t *pPlan = &pResult->aPlans[uiArea];
        for (uint32_t uiGroup = 0; (NULL != pPlan->pauiSlotList) && (uiGroup < pPlan->uiGroupCount); uiGroup++) {
            const uint32_t *pauiSlots = &pPlan->pauiSlotList[pPlan->auiSlotStart[uiGroup]];
            for (uint32_t uiIndex = 0; uiIndex < pPlan->auiSlotCount[uiGroup]; uiIndex++) {
                if ((0 == uiIndex) || (pauiSlots[uiIndex] != pauiSlots[uiIndex - 1] + 1) || (0 == pauiSlots[uiIndex] % uiTimeSlots)) {
                    ullRuns++;
                }
            }
        }
    }
    return ullRuns;
}

static const char *GetModeText(bool bManual)
{
    return bManual ? "직접 지정" : "균등";
}

static const char *GetSlotModeText(SlotMode Mode)
{
    if (SlotModeProportional == Mode) {
        return "단말 수 비례";
    }
    else {
        return GetModeText(SlotModeManual == Mode);
    }
}

static void WriteTerminalModeCell(FILE *pFile, char cColumn, uint32_t uiRow, const AreaGroupPlan_t *pPlan)
{
    if ((0 == pPlan->uiFixedTerminalGroups) || (pPlan->uiFixedTerminalGroups == pPlan->uiGroupCount)) {
        WriteTextCell(pFile, cColumn, uiRow, 3, GetModeText(0 != pPlan->uiFixedTerminalGroups));
    }
    else {
        fprintf(pFile, "<c r=\"%c%u\" s=\"3\" t=\"inlineStr\"><is><t>직접 %u개 그룹 · 나머지 균등</t></is></c>", cColumn, uiRow, pPlan->uiFixedTerminalGroups);
    }
}

static bool WriteSlotRuns(WorkbookPackage_t *pPackage, const WorkbookLayout_t *pLayout, uint32_t *puiRow)
{
    FILE *pFile = pPackage->pFile;
    const SimulationResult_t *pResult = pLayout->pResult;
    uint32_t uiTimeSlots = GetTimeSlotCount(pResult->Config.uiSymbolRate, pResult->Config.uiWaveId);

    for (uint32_t uiUnit = 0; uiUnit < pLayout->uiUnitCount; uiUnit++) {
        const GroupUnit_t *pUnit = &pLayout->aUnits[uiUnit];
        const AreaGroupPlan_t *pPlan = &pResult->aPlans[pUnit->Area];
        uint32_t uiCount = pPlan->auiSlotCount[pUnit->uiGroup];
        const uint32_t *pauiSlots = (0 == uiCount) ? NULL : &pPlan->pauiSlotList[pPlan->auiSlotStart[pUnit->uiGroup]];
        uint32_t uiIndex = 0;

        while (uiIndex < uiCount) {
            uint32_t uiLast = uiIndex;
            uint32_t uiRow = (*puiRow)++;

            while ((uiLast + 1 < uiCount) && (pauiSlots[uiLast + 1] == pauiSlots[uiLast] + 1) && (0 != pauiSlots[uiLast + 1] % uiTimeSlots)) {
                uiLast++;
            }
            if (CheckCancelled(pPackage) || (0 != ferror(pFile))) {
                return false;
            }
            fprintf(pFile, "<row r=\"%u\">", uiRow);
            WriteTextCell(pFile, 'A', uiRow, 3, gpacAreaNames[pUnit->Area]);
            WriteGroupNameCell(pFile, 'B', uiRow, pUnit->uiGroup);
            WriteCountCell(pFile, 'C', uiRow, pauiSlots[uiIndex] / uiTimeSlots + 1);
            WriteCountCell(pFile, 'D', uiRow, pauiSlots[uiIndex] % uiTimeSlots + 1);
            WriteCountCell(pFile, 'E', uiRow, pauiSlots[uiLast] % uiTimeSlots + 1);
            WriteCountCell(pFile, 'F', uiRow, uiLast - uiIndex + 1);
            WriteCountCell(pFile, 'G', uiRow, (uint64_t)pauiSlots[uiIndex] + 1);
            WriteCountCell(pFile, 'H', uiRow, (uint64_t)pauiSlots[uiLast] + 1);
            fputs("</row>", pFile);
            uiIndex = uiLast + 1;
        }
    }
    return true;
}

static bool WriteGroupSettingsSheet(WorkbookPackage_t *pPackage, const WorkbookLayout_t *pLayout, uint32_t uiSheet)
{
    FILE *pFile = pPackage->pFile;
    const SimulationResult_t *pResult = pLayout->pResult;
    const SimulationConfig_t *pConfig = &pResult->Config;
    uint32_t uiTimeSlots = GetTimeSlotCount(pConfig->uiSymbolRate, pConfig->uiWaveId);
    const char *const pacAreaHeaders[] = { "채널 타입", "채널 수", "시간 슬롯 수", "주파수 위치 수", "그룹 수", "배정 칸 수", "미배정 칸 수", "단말 배분", "슬롯 배정" };
    const char *const pacUnitHeaders[] = { "채널 타입", "그룹", "단말 수", "단말 ID 시작", "단말 ID 끝", "배정 칸 수", "단말 수 지정" };
    const char *const pacRunHeaders[] = { "채널 타입", "그룹", "주파수 위치", "시간 시작", "시간 끝", "칸 수", "슬롯 번호 시작", "슬롯 번호 끝" };
    uint32_t uiRow;
    uint32_t uiRunTitle;

    if (!BeginNumberedPart(pPackage, "xl/worksheets/sheet%u.xml", uiSheet)) {
        return false;
    }
    fputs(SHEET_START "<sheetViews><sheetView showGridLines=\"0\" workbookViewId=\"0\"/></sheetViews><sheetFormatPr defaultRowHeight=\"24\"/>"
        "<cols><col min=\"1\" max=\"2\" width=\"14\" customWidth=\"1\"/><col min=\"3\" max=\"9\" width=\"18\" customWidth=\"1\"/></cols><sheetData>"
        "<row r=\"1\" ht=\"32\" customHeight=\"1\">", pFile);
    WriteTextCell(pFile, 'A', 1, 1, "그룹 설정");
    fputs("</row><row r=\"2\">", pFile);
    WriteTextCell(pFile, 'A', 2, 6, "그룹 수는 Alert와 LB가 공유합니다. 단말은 그룹 순서대로 그룹별 단말 수만큼 연속 ID로 배정합니다. 단말 ID·주파수 위치·시간 위치·슬롯 번호는 모두 1부터 셉니다. 주파수 위치 1이 가장 낮은 주파수(격자의 맨 아래 줄)입니다. 단말 수 지정이 자동인 그룹은 직접 지정한 그룹을 뺀 나머지 단말을 균등하게 나눕니다.");
    fputs("</row><row r=\"4\" ht=\"32\" customHeight=\"1\">", pFile);
    for (uint32_t uiColumn = 0; uiColumn < 9; uiColumn++) {
        WriteTextCell(pFile, (char)('A' + uiColumn), 4, 2, pacAreaHeaders[uiColumn]);
    }
    fputs("</row>", pFile);
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        const AreaGroupPlan_t *pPlan = &pResult->aPlans[uiArea];
        bool bActive = 0 != pPlan->uiGroupCount;
        uiRow = 5 + uiArea;

        fprintf(pFile, "<row r=\"%u\">", uiRow);
        WriteTextCell(pFile, 'A', uiRow, 3, gpacAreaNames[uiArea]);
        WriteCountCell(pFile, 'B', uiRow, pConfig->auiSlots[uiArea]);
        WriteCountCell(pFile, 'C', uiRow, uiTimeSlots);
        WriteCountCell(pFile, 'D', uiRow, GetFrequencyRowCount(pConfig->auiSlots[uiArea], uiTimeSlots));
        WriteCountCell(pFile, 'E', uiRow, pPlan->uiGroupCount);
        WriteCountCell(pFile, 'F', uiRow, bActive ? pConfig->auiSlots[uiArea] - pPlan->uiUnassignedSlots : 0);
        WriteCountCell(pFile, 'G', uiRow, bActive ? pPlan->uiUnassignedSlots : 0);
        if (bActive) {
            WriteTerminalModeCell(pFile, 'H', uiRow, pPlan);
        }
        else {
            WriteTextCell(pFile, 'H', uiRow, 3, "경쟁 없음");
        }
        WriteTextCell(pFile, 'I', uiRow, 3, bActive ? GetSlotModeText(pPlan->SlotAssignment) : "경쟁 없음");
        fputs("</row>", pFile);
    }
    fputs("<row r=\"8\">", pFile);
    WriteTextCell(pFile, 'A', 8, 1, "그룹별 단말·배정 칸");
    fputs("</row><row r=\"9\" ht=\"32\" customHeight=\"1\">", pFile);
    for (uint32_t uiColumn = 0; uiColumn < 7; uiColumn++) {
        WriteTextCell(pFile, (char)('A' + uiColumn), 9, 2, pacUnitHeaders[uiColumn]);
    }
    fputs("</row>", pFile);
    uiRow = 10;
    for (uint32_t uiUnit = 0; uiUnit < pLayout->uiUnitCount; uiUnit++, uiRow++) {
        const GroupUnit_t *pUnit = &pLayout->aUnits[uiUnit];
        const AreaGroupPlan_t *pPlan = &pResult->aPlans[pUnit->Area];
        uint32_t uiTerminals = GetGroupTerminalCount(pPlan, pUnit->uiGroup);
        uint64_t ullFirst = (uint64_t)GetGroupFirstTerminal(pPlan, pUnit->uiGroup) + 1;

        fprintf(pFile, "<row r=\"%u\">", uiRow);
        WriteTextCell(pFile, 'A', uiRow, 3, gpacAreaNames[pUnit->Area]);
        WriteGroupNameCell(pFile, 'B', uiRow, pUnit->uiGroup);
        WriteCountCell(pFile, 'C', uiRow, uiTerminals);
        if (0 == uiTerminals) {
            WriteTextCell(pFile, 'D', uiRow, 3, "-");
            WriteTextCell(pFile, 'E', uiRow, 3, "-");
        }
        else {
            WriteCountCell(pFile, 'D', uiRow, ullFirst);
            WriteCountCell(pFile, 'E', uiRow, ullFirst + uiTerminals - 1);
        }
        WriteCountCell(pFile, 'F', uiRow, pPlan->auiSlotCount[pUnit->uiGroup]);
        WriteTextCell(pFile, 'G', uiRow, 3, pPlan->abFixedTerminals[pUnit->uiGroup] ? "직접" : "자동");
        fputs("</row>", pFile);
    }
    uiRunTitle = uiRow + 1;
    fprintf(pFile, "<row r=\"%u\">", uiRunTitle);
    WriteTextCell(pFile, 'A', uiRunTitle, 1, "그룹별 슬롯 배정 구간");
    fprintf(pFile, "</row><row r=\"%u\" ht=\"32\" customHeight=\"1\">", uiRunTitle + 1);
    for (uint32_t uiColumn = 0; uiColumn < 8; uiColumn++) {
        WriteTextCell(pFile, (char)('A' + uiColumn), uiRunTitle + 1, 2, pacRunHeaders[uiColumn]);
    }
    fputs("</row>", pFile);
    uiRow = uiRunTitle + 2;
    if (!WriteSlotRuns(pPackage, pLayout, &uiRow)) {
        return false;
    }
    fputs("</sheetData><mergeCells count=\"2\"><mergeCell ref=\"A1:I1\"/><mergeCell ref=\"A2:I2\"/></mergeCells></worksheet>", pFile);
    return FinishPart(pPackage);
}

static void WriteGroupResultRow(FILE *pFile, uint32_t uiRow, AreaType Area, const SlotCounts_t *pCounts)
{
    WriteTextCell(pFile, 'A', uiRow, 3, gpacAreaNames[Area]);
    WriteCountCell(pFile, 'E', uiRow, pCounts->ullSuccess);
    WriteCountCell(pFile, 'F', uiRow, pCounts->ullCollidedAttempts);
    WriteCountCell(pFile, 'G', uiRow, pCounts->ullCollision);
    WriteCountCell(pFile, 'H', uiRow, pCounts->ullIdle);
}

/* Returns the last used row so that charts can be placed below the table. */
static bool WriteGroupResultsSheet(WorkbookPackage_t *pPackage, const WorkbookLayout_t *pLayout, uint32_t uiSheet, uint32_t *puiLastRow)
{
    FILE *pFile = pPackage->pFile;
    const SimulationResult_t *pResult = pLayout->pResult;
    const char *const pacHeaders[] = { "채널 타입", "그룹", "단말 수", "배정 칸 수", "누적 성공 건수", "누적 충돌 전송 건수", "누적 충돌 칸 수", "누적 유휴 칸 수", "최종 접속 실패 단말 갯수" };
    uint32_t uiRow = 5;

    if (!BeginNumberedPart(pPackage, "xl/worksheets/sheet%u.xml", uiSheet)) {
        return false;
    }
    fputs(SHEET_START "<sheetViews><sheetView showGridLines=\"0\" workbookViewId=\"0\"/></sheetViews><sheetFormatPr defaultRowHeight=\"24\"/>"
        "<cols><col min=\"1\" max=\"2\" width=\"14\" customWidth=\"1\"/><col min=\"3\" max=\"9\" width=\"20\" customWidth=\"1\"/></cols><sheetData>"
        "<row r=\"1\" ht=\"32\" customHeight=\"1\">", pFile);
    WriteTextCell(pFile, 'A', 1, 1, "그룹별 결과");
    fputs("</row><row r=\"2\">", pFile);
    WriteTextCell(pFile, 'A', 2, 6, "누적 값은 1회차부터 마지막 수신 회차까지의 합계입니다. 합계 행은 그 채널 타입 그룹들의 합이며, 어느 그룹에도 배정하지 않은 칸(항상 유휴)은 넣지 않습니다.");
    fputs("</row><row r=\"4\" ht=\"40\" customHeight=\"1\">", pFile);
    for (uint32_t uiColumn = 0; uiColumn < 9; uiColumn++) {
        WriteTextCell(pFile, (char)('A' + uiColumn), 4, 2, pacHeaders[uiColumn]);
    }
    fputs("</row>", pFile);
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        const AreaGroupPlan_t *pPlan = &pResult->aPlans[uiArea];
        SlotCounts_t Total = { 0 };
        uint64_t ullSlots = 0;
        uint64_t ullUnsuccessful = 0;

        if (0 == pPlan->uiGroupCount) {
            continue;
        }
        for (uint32_t uiGroup = 0; uiGroup < pPlan->uiGroupCount; uiGroup++, uiRow++) {
            fprintf(pFile, "<row r=\"%u\">", uiRow);
            WriteGroupResultRow(pFile, uiRow, (AreaType)uiArea, &pResult->aaGroupTotals[uiArea][uiGroup]);
            WriteGroupNameCell(pFile, 'B', uiRow, uiGroup);
            WriteCountCell(pFile, 'C', uiRow, GetGroupTerminalCount(pPlan, uiGroup));
            WriteCountCell(pFile, 'D', uiRow, pPlan->auiSlotCount[uiGroup]);
            WriteCountCell(pFile, 'I', uiRow, pResult->aauiGroupUnsuccessful[uiArea][uiGroup]);
            fputs("</row>", pFile);
            AddSlotCounts(&Total, &pResult->aaGroupTotals[uiArea][uiGroup]);
            ullSlots += pPlan->auiSlotCount[uiGroup];
            ullUnsuccessful += pResult->aauiGroupUnsuccessful[uiArea][uiGroup];
        }
        fprintf(pFile, "<row r=\"%u\">", uiRow);
        WriteGroupResultRow(pFile, uiRow, (AreaType)uiArea, &Total);
        WriteTextCell(pFile, 'B', uiRow, 3, "합계");
        WriteCountCell(pFile, 'C', uiRow, GetGroupFirstTerminal(pPlan, pPlan->uiGroupCount));
        WriteCountCell(pFile, 'D', uiRow, ullSlots);
        WriteCountCell(pFile, 'I', uiRow, ullUnsuccessful);
        fputs("</row>", pFile);
        uiRow++;
    }
    fprintf(pFile, "<row r=\"%u\">", uiRow);
    WriteTextCell(pFile, 'A', uiRow, 6, "그래프의 각 지점 숫자는 해당 회차에 그 그룹에서 성공하거나 충돌한 전송 단말 수입니다. 회차별 값은 그룹별 회차 데이터 시트에 있습니다.");
    fprintf(pFile, "</row></sheetData><mergeCells count=\"3\"><mergeCell ref=\"A1:I1\"/><mergeCell ref=\"A2:I2\"/><mergeCell ref=\"A%u:I%u\"/></mergeCells>"
        "<drawing r:id=\"rId1\"/></worksheet>", uiRow, uiRow);
    *puiLastRow = uiRow;
    return FinishPart(pPackage);
}

static bool WriteGroupRoundSheet(WorkbookPackage_t *pPackage, const WorkbookLayout_t *pLayout, uint32_t uiSheet, uint32_t uiRoundSheet)
{
    FILE *pFile = pPackage->pFile;
    const SimulationResult_t *pResult = pLayout->pResult;
    const char *const pacHeaders[] = { "회차", "수신 시각 (초)", "채널 타입", "그룹", "성공 건수", "충돌 전송 건수", "충돌 칸 수", "유휴 칸 수", "누적 성공 건수", "누적 충돌 전송 건수" };
    uint32_t uiFirstUnit = uiRoundSheet * pLayout->uiUnitsPerSheet;
    uint32_t uiUnits = pLayout->uiUnitCount - uiFirstUnit;
    uint64_t ullLastRow;

    if (uiUnits > pLayout->uiUnitsPerSheet) {
        uiUnits = pLayout->uiUnitsPerSheet;
    }
    if (!BeginNumberedPart(pPackage, "xl/worksheets/sheet%u.xml", uiSheet)) {
        return false;
    }
    fputs(SHEET_START "<sheetViews><sheetView showGridLines=\"0\" workbookViewId=\"0\">"
        "<pane xSplit=\"4\" ySplit=\"3\" topLeftCell=\"E4\" activePane=\"bottomRight\" state=\"frozen\"/>"
        "</sheetView></sheetViews><sheetFormatPr defaultRowHeight=\"24\"/><cols>"
        "<col min=\"1\" max=\"1\" width=\"10\" customWidth=\"1\"/><col min=\"2\" max=\"2\" width=\"16\" customWidth=\"1\"/>"
        "<col min=\"3\" max=\"4\" width=\"12\" customWidth=\"1\"/><col min=\"5\" max=\"10\" width=\"20\" customWidth=\"1\"/></cols><sheetData>"
        "<row r=\"1\" ht=\"32\" customHeight=\"1\">", pFile);
    if (1 == pLayout->uiGroupRoundSheets) {
        WriteTextCell(pFile, 'A', 1, 1, "그룹별 회차 데이터");
    }
    else {
        fprintf(pFile, "<c r=\"A1\" s=\"1\" t=\"inlineStr\"><is><t>그룹별 회차 데이터 (%u/%u)</t></is></c>", uiRoundSheet + 1, pLayout->uiGroupRoundSheets);
    }
    fputs("</row><row r=\"2\">", pFile);
    WriteTextCell(pFile, 'A', 2, 6, "채널 타입 → 그룹 → 회차 순서입니다. 누적 값은 해당 그룹의 1회차부터 그 행의 회차까지 더한 값입니다. 행 수가 많으면 그룹 단위로 다음 시트에 이어집니다.");
    fputs("</row><row r=\"3\" ht=\"42\" customHeight=\"1\">", pFile);
    for (uint32_t uiColumn = 0; uiColumn < 10; uiColumn++) {
        WriteTextCell(pFile, (char)('A' + uiColumn), 3, 2, pacHeaders[uiColumn]);
    }
    fputs("</row>", pFile);
    for (uint32_t uiUnit = 0; uiUnit < uiUnits; uiUnit++) {
        const GroupUnit_t *pUnit = &pLayout->aUnits[uiFirstUnit + uiUnit];
        uint32_t uiSlots = pResult->aPlans[pUnit->Area].auiSlotCount[pUnit->uiGroup];
        uint64_t ullSuccess = 0;
        uint64_t ullCollided = 0;

        for (uint64_t ullIndex = 0; ullIndex < pResult->ullRoundCount; ullIndex++) {
            uint32_t uiRow = (uint32_t)(4 + (uint64_t)uiUnit * pResult->ullRoundCount + ullIndex);
            RoundAreaCounts_t Counts = GetRoundGroupCounts(pResult, ullIndex, pUnit->Area, pUnit->uiGroup);

            if ((0 == (ullIndex & UINT64_C(1023))) && (CheckCancelled(pPackage) || (0 != ferror(pFile)))) {
                return false;
            }
            ullSuccess += Counts.uiSuccess;
            ullCollided += Counts.uiCollidedAttempts;
            fprintf(pFile, "<row r=\"%u\">", uiRow);
            WriteCountCell(pFile, 'A', uiRow, ullIndex + 1);
            WriteTimeCell(pFile, 'B', uiRow, (ullIndex + 1) * pResult->Config.ullSuperframeMilliseconds);
            WriteTextCell(pFile, 'C', uiRow, 3, gpacAreaNames[pUnit->Area]);
            WriteGroupNameCell(pFile, 'D', uiRow, pUnit->uiGroup);
            WriteCountCell(pFile, 'E', uiRow, Counts.uiSuccess);
            WriteCountCell(pFile, 'F', uiRow, Counts.uiCollidedAttempts);
            WriteCountCell(pFile, 'G', uiRow, Counts.uiCollision);
            WriteCountCell(pFile, 'H', uiRow, uiSlots - Counts.uiSuccess - Counts.uiCollision);
            WriteCountCell(pFile, 'I', uiRow, ullSuccess);
            WriteCountCell(pFile, 'J', uiRow, ullCollided);
            fputs("</row>", pFile);
        }
    }
    ullLastRow = 3 + (uint64_t)uiUnits * pResult->ullRoundCount;
    fprintf(pFile, "</sheetData><autoFilter ref=\"A3:J%llu\"/><mergeCells count=\"2\">"
        "<mergeCell ref=\"A1:J1\"/><mergeCell ref=\"A2:J2\"/></mergeCells></worksheet>", ullLastRow);
    return FinishPart(pPackage);
}

typedef struct RepeatRow {
    const char *pcLabel;
    RepeatMetric Metric;
    RepeatSeries Series;
} RepeatRow_t;

static const RepeatRow_t gaGroupedRepeatRows[] = {
    { "최종 접속 실패 단말 갯수 · 그룹 적용", RepeatFailure, RepeatGrouped },
    { "최종 접속 실패 단말 갯수 · 그룹 미적용", RepeatFailure, RepeatBaseline },
    { "최종 접속 실패 단말 갯수 · 차이 (적용 − 미적용)", RepeatFailure, RepeatDifference },
    { "누적 성공 건수 · 그룹 적용", RepeatSuccess, RepeatGrouped },
    { "누적 성공 건수 · 그룹 미적용", RepeatSuccess, RepeatBaseline },
    { "누적 충돌 전송 건수 · 그룹 적용", RepeatCollided, RepeatGrouped },
    { "누적 충돌 전송 건수 · 그룹 미적용", RepeatCollided, RepeatBaseline }
};

static const RepeatRow_t gaPlainRepeatRows[] = {
    { "최종 접속 실패 단말 갯수", RepeatFailure, RepeatGrouped },
    { "누적 성공 건수", RepeatSuccess, RepeatGrouped },
    { "누적 충돌 전송 건수", RepeatCollided, RepeatGrouped }
};

static void WriteDecimalCell(FILE *pFile, char cColumn, uint32_t uiRow, double dValue)
{
    fprintf(pFile, "<c r=\"%c%u\" s=\"7\"><v>%.6f</v></c>", cColumn, uiRow, dValue);
}

static void WriteWholeCell(FILE *pFile, char cColumn, uint32_t uiRow, double dValue)
{
    fprintf(pFile, "<c r=\"%c%u\" s=\"4\"><v>%.0f</v></c>", cColumn, uiRow, dValue);
}

/* Summary statistics of all repetitions followed by one row per repetition. Repetition 1 is the detailed run. */
static bool WriteRepeatSheet(WorkbookPackage_t *pPackage, const SimulationResult_t *pResult, uint32_t uiSheet)
{
    FILE *pFile = pPackage->pFile;
    bool bGrouped = IsGroupedResult(pResult);
    const RepeatRow_t *paRows = bGrouped ? gaGroupedRepeatRows : gaPlainRepeatRows;
    uint32_t uiRowCount = bGrouped ? (uint32_t)(sizeof(gaGroupedRepeatRows) / sizeof(gaGroupedRepeatRows[0])) : (uint32_t)(sizeof(gaPlainRepeatRows) / sizeof(gaPlainRepeatRows[0]));
    const char *const pacHeaders[] = { "채널 타입", "항목", "평균", "표준편차", "최소", "최대" };
    uint32_t uiRow = 5;
    uint32_t uiRunTitle;
    char cColumn;

    if (!BeginNumberedPart(pPackage, "xl/worksheets/sheet%u.xml", uiSheet)) {
        return false;
    }
    fputs(SHEET_START "<sheetViews><sheetView showGridLines=\"0\" workbookViewId=\"0\"/></sheetViews><sheetFormatPr defaultRowHeight=\"24\"/>"
        "<cols><col min=\"1\" max=\"1\" width=\"12\" customWidth=\"1\"/><col min=\"2\" max=\"2\" width=\"46\" customWidth=\"1\"/>"
        "<col min=\"3\" max=\"14\" width=\"16\" customWidth=\"1\"/></cols><sheetData><row r=\"1\" ht=\"32\" customHeight=\"1\">", pFile);
    fprintf(pFile, "<c r=\"A1\" s=\"1\" t=\"inlineStr\"><is><t>반복 실행 통계 (%u회)</t></is></c>", pResult->uiRepeatCount);
    fputs("</row><row r=\"2\">", pFile);
    WriteTextCell(pFile, 'A', 2, 6, "같은 설정을 실행마다 다른 시드로 다시 계산한 결과입니다. 1번째 실행은 다른 시트의 상세 결과와 같습니다. "
        "표준편차는 표본 표준편차(n−1)이고, 차이는 실행마다 그룹 적용에서 그룹 미적용을 뺀 값입니다.");
    fputs("</row><row r=\"4\" ht=\"32\" customHeight=\"1\">", pFile);
    for (uint32_t uiColumn = 0; uiColumn < 6; uiColumn++) {
        WriteTextCell(pFile, (char)('A' + uiColumn), 4, 2, pacHeaders[uiColumn]);
    }
    fputs("</row>", pFile);
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        if (0 == pResult->Config.auiSlots[uiArea]) {
            continue;
        }
        for (uint32_t uiIndex = 0; uiIndex < uiRowCount; uiIndex++, uiRow++) {
            RepeatStatistic_t Statistic;

            SummarizeRepeats(pResult, (AreaType)uiArea, paRows[uiIndex].Metric, paRows[uiIndex].Series, &Statistic);
            fprintf(pFile, "<row r=\"%u\">", uiRow);
            WriteTextCell(pFile, 'A', uiRow, 3, gpacAreaNames[uiArea]);
            WriteTextCell(pFile, 'B', uiRow, 3, paRows[uiIndex].pcLabel);
            WriteDecimalCell(pFile, 'C', uiRow, Statistic.dMean);
            WriteDecimalCell(pFile, 'D', uiRow, Statistic.dDeviation);
            WriteWholeCell(pFile, 'E', uiRow, Statistic.dMinimum);
            WriteWholeCell(pFile, 'F', uiRow, Statistic.dMaximum);
            fputs("</row>", pFile);
        }
    }
    uiRunTitle = uiRow + 1;
    fprintf(pFile, "<row r=\"%u\">", uiRunTitle);
    WriteTextCell(pFile, 'A', uiRunTitle, 1, "실행별 결과");
    fprintf(pFile, "</row><row r=\"%u\" ht=\"48\" customHeight=\"1\">", uiRunTitle + 1);
    WriteTextCell(pFile, 'A', uiRunTitle + 1, 2, "실행");
    WriteTextCell(pFile, 'B', uiRunTitle + 1, 2, "시드");
    cColumn = 'C';
    for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
        for (uint32_t uiIndex = 0; (0 != pResult->Config.auiSlots[uiArea]) && (uiIndex < uiRowCount); uiIndex++) {
            if (RepeatDifference != paRows[uiIndex].Series) {
                fprintf(pFile, "<c r=\"%c%u\" s=\"2\" t=\"inlineStr\"><is><t>%s&#10;%s</t></is></c>", cColumn++, uiRunTitle + 1, gpacAreaNames[uiArea], paRows[uiIndex].pcLabel);
            }
        }
    }
    fputs("</row>", pFile);
    for (uint32_t uiRepeat = 0; uiRepeat < pResult->uiRepeatCount; uiRepeat++) {
        const RepeatSample_t *pSample = &pResult->paRepeats[uiRepeat];

        uiRow = uiRunTitle + 2 + uiRepeat;
        if (CheckCancelled(pPackage) || (0 != ferror(pFile))) {
            return false;
        }
        fprintf(pFile, "<row r=\"%u\">", uiRow);
        WriteCountCell(pFile, 'A', uiRow, (uint64_t)uiRepeat + 1);
        fprintf(pFile, "<c r=\"B%u\" s=\"3\" t=\"inlineStr\"><is><t>0x%016llX</t></is></c>", uiRow, pSample->ullSeed);
        cColumn = 'C';
        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            for (uint32_t uiIndex = 0; (0 != pResult->Config.auiSlots[uiArea]) && (uiIndex < uiRowCount); uiIndex++) {
                if (RepeatDifference != paRows[uiIndex].Series) {
                    WriteWholeCell(pFile, cColumn++, uiRow, GetRepeatValue(pSample, (AreaType)uiArea, paRows[uiIndex].Metric, paRows[uiIndex].Series));
                }
            }
        }
        fputs("</row>", pFile);
    }
    fputs("</sheetData><mergeCells count=\"2\"><mergeCell ref=\"A1:F1\"/><mergeCell ref=\"A2:N2\"/></mergeCells></worksheet>", pFile);
    return FinishPart(pPackage);
}

static void WriteChartTitle(FILE *pFile, const char *pcTitle)
{
    fprintf(pFile, "<c:title><c:tx><c:rich><a:bodyPr/><a:lstStyle/><a:p><a:pPr><a:defRPr sz=\"1200\"/></a:pPr>"
        "<a:r><a:rPr lang=\"ko-KR\"/><a:t>%s</a:t></a:r></a:p></c:rich></c:tx><c:overlay val=\"0\"/></c:title>", pcTitle);
}

static uint32_t GetChartValue(const SimulationResult_t *pResult, const ChartSpec_t *pSpec, ChartValue Value, uint64_t ullIndex)
{
    switch (Value) {
    case ValueAreaSuccess:
        return GetRoundAreaCounts(pResult, ullIndex, pSpec->Area).uiSuccess;

    case ValueAreaCollided:
        return GetRoundAreaCounts(pResult, ullIndex, pSpec->Area).uiCollidedAttempts;

    case ValueGroupSuccess:
        return GetRoundGroupCounts(pResult, ullIndex, pSpec->Area, pSpec->uiGroup).uiSuccess;

    case ValueGroupCollided:
        return GetRoundGroupCounts(pResult, ullIndex, pSpec->Area, pSpec->uiGroup).uiCollidedAttempts;

    case ValueBaselineSuccess:
        return GetBaselineRoundCounts(pResult, ullIndex, pSpec->Area).uiSuccess;

    case ValueBaselineCollided:
        return GetBaselineRoundCounts(pResult, ullIndex, pSpec->Area).uiCollidedAttempts;

    default:
        return 0;
    }
}

static bool WriteChartValues(WorkbookPackage_t *pPackage, const SimulationResult_t *pResult, const ChartSpec_t *pSpec, const ChartSeries_t *pSeries)
{
    FILE *pFile = pPackage->pFile;
    char cColumn = (NULL == pSeries) ? 'B' : pSeries->cColumn;
    uint64_t ullLastRow = (0 == pResult->ullRoundCount) ? pSpec->ullFirstRow : pSpec->ullFirstRow + pResult->ullRoundCount - 1;

    fprintf(pFile, "<c:numRef><c:f>'%s'!$%c$%llu:$%c$%llu</c:f><c:numCache><c:formatCode>%s</c:formatCode><c:ptCount val=\"%llu\"/>",
        pSpec->acSheet, cColumn, pSpec->ullFirstRow, cColumn, ullLastRow, (NULL == pSeries) ? "0.###" : "0", pResult->ullRoundCount);
    for (uint64_t ullIndex = 0; ullIndex < pResult->ullRoundCount; ullIndex++) {
        if (CheckCancelled(pPackage) || (0 != ferror(pFile))) {
            return false;
        }
        if (NULL == pSeries) {
            uint64_t ullTime = (ullIndex + 1) * pResult->Config.ullSuperframeMilliseconds;
            fprintf(pFile, "<c:pt idx=\"%llu\"><c:v>%llu.%03u</c:v></c:pt>", ullIndex, ullTime / 1000, (uint32_t)(ullTime % 1000));
        }
        else {
            fprintf(pFile, "<c:pt idx=\"%llu\"><c:v>%u</c:v></c:pt>", ullIndex, GetChartValue(pResult, pSpec, pSeries->Value, ullIndex));
        }
    }
    return fputs("</c:numCache></c:numRef>", pFile) >= 0;
}

static bool WriteChart(WorkbookPackage_t *pPackage, const SimulationResult_t *pResult, uint32_t uiChart, const ChartSpec_t *pSpec)
{
    FILE *pFile = pPackage->pFile;

    if (!BeginNumberedPart(pPackage, "xl/charts/chart%u.xml", uiChart)) {
        return false;
    }
    fputs(XML_DECL "<c:chartSpace xmlns:c=\"" CHART_NS "\" xmlns:a=\"" DRAWING_NS "\"><c:lang val=\"ko-KR\"/><c:chart>", pFile);
    WriteChartTitle(pFile, pSpec->acTitle);
    fputs("<c:plotArea><c:layout/><c:lineChart><c:grouping val=\"standard\"/><c:varyColors val=\"0\"/>", pFile);
    for (uint32_t uiSeries = 0; uiSeries < 2; uiSeries++) {
        const ChartSeries_t *pSeries = &pSpec->aSeries[uiSeries];
        bool bSecond = 1 == uiSeries;
        fprintf(pFile, "<c:ser><c:idx val=\"%u\"/><c:order val=\"%u\"/><c:tx><c:strRef><c:f>'결과'!$%c$%u</c:f>"
            "<c:strCache><c:ptCount val=\"1\"/><c:pt idx=\"0\"><c:v>%s</c:v></c:pt></c:strCache></c:strRef></c:tx>"
            "<c:spPr><a:ln w=\"25400\"><a:solidFill><a:srgbClr val=\"%s\"/></a:solidFill><a:prstDash val=\"%s\"/></a:ln></c:spPr>"
            "<c:marker><c:symbol val=\"%s\"/><c:size val=\"5\"/></c:marker>"
            "<c:dLbls>",
            uiSeries, uiSeries, SERIES_NAME_COLUMN, SERIES_NAME_FIRST_ROW + (uint32_t)pSeries->Name, gpacSeriesNames[pSeries->Name],
            pSeries->pcColor, pSeries->bDashed ? "dash" : "solid", bSecond ? "square" : "circle");
        for (uint64_t ullIndex = 0; ullIndex < pResult->ullRoundCount; ullIndex++) {
            uint32_t uiFirst = GetChartValue(pResult, pSpec, pSpec->aSeries[0].Value, ullIndex);
            uint32_t uiOther = GetChartValue(pResult, pSpec, pSpec->aSeries[1].Value, ullIndex);
            bool bBelow = bSecond ? (uiOther <= uiFirst) : (uiFirst < uiOther);
            uint32_t uiValue = bSecond ? uiOther : uiFirst;
            const char *pcPosition = bBelow ? "b" : "t";
            if (CheckCancelled(pPackage) || (0 != ferror(pFile))) {
                return false;
            }
            if (0 == uiValue) {
                pcPosition = (bSecond && (0 == uiFirst)) ? "r" : "t";
            }
            fprintf(pFile, "<c:dLbl><c:idx val=\"%llu\"/><c:dLblPos val=\"%s\"/><c:showVal val=\"1\"/></c:dLbl>", ullIndex, pcPosition);
        }
        fputs("<c:numFmt formatCode=\"0\" sourceLinked=\"0\"/><c:showLegendKey val=\"0\"/><c:showVal val=\"1\"/>"
            "<c:showCatName val=\"0\"/><c:showSerName val=\"0\"/></c:dLbls><c:cat>", pFile);
        if (!WriteChartValues(pPackage, pResult, pSpec, NULL) || (fputs("</c:cat><c:val>", pFile) < 0) ||
            !WriteChartValues(pPackage, pResult, pSpec, pSeries)) {
            return false;
        }
        fputs("</c:val><c:smooth val=\"0\"/></c:ser>", pFile);
    }
    fputs("<c:marker val=\"1\"/><c:smooth val=\"0\"/><c:axId val=\"1\"/><c:axId val=\"2\"/></c:lineChart>"
        "<c:catAx><c:axId val=\"1\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling><c:delete val=\"0\"/><c:axPos val=\"b\"/>", pFile);
    WriteChartTitle(pFile, "전송 주기 (수신 시각, 초)");
    fputs("<c:numFmt formatCode=\"0.###\" sourceLinked=\"0\"/><c:tickLblPos val=\"nextTo\"/><c:crossAx val=\"2\"/><c:crosses val=\"autoZero\"/>"
        "<c:auto val=\"1\"/><c:lblAlgn val=\"ctr\"/><c:lblOffset val=\"100\"/></c:catAx>"
        "<c:valAx><c:axId val=\"2\"/><c:scaling><c:orientation val=\"minMax\"/><c:min val=\"0\"/></c:scaling><c:delete val=\"0\"/><c:axPos val=\"l\"/><c:majorGridlines/>", pFile);
    WriteChartTitle(pFile, "단말 수");
    fputs("<c:numFmt formatCode=\"0\" sourceLinked=\"0\"/><c:tickLblPos val=\"nextTo\"/><c:crossAx val=\"1\"/><c:crosses val=\"autoZero\"/><c:crossBetween val=\"between\"/></c:valAx>"
        "</c:plotArea><c:legend><c:legendPos val=\"b\"/><c:overlay val=\"0\"/></c:legend><c:plotVisOnly val=\"1\"/>"
        "<c:dispBlanksAs val=\"gap\"/></c:chart><c:txPr><a:bodyPr/><a:lstStyle/><a:p><a:pPr><a:defRPr sz=\"1000\"/></a:pPr><a:endParaRPr lang=\"ko-KR\"/></a:p></c:txPr></c:chartSpace>", pFile);
    return FinishPart(pPackage);
}

static bool SetChartText(char *pcText, size_t uCapacity, const char *pcFormat, const char *pcArea, uint32_t uiGroup)
{
    int32_t iWritten = snprintf(pcText, uCapacity, pcFormat, pcArea, uiGroup);
    return (iWritten > 0) && ((size_t)iWritten < uCapacity);
}

static bool BuildSummaryChart(const WorkbookLayout_t *pLayout, uint32_t uiChart, ChartSpec_t *pSpec, ChartAnchor_t *pAnchor)
{
    const ChartSeries_t aCompare[4][2] = {
        { { SeriesGroupedSuccess, "349D75", false, 'C', ValueAreaSuccess }, { SeriesBaselineSuccess, "7F8C9A", true, 'D', ValueBaselineSuccess } },
        { { SeriesGroupedCollided, "D86354", false, 'E', ValueAreaCollided }, { SeriesBaselineCollided, "7F8C9A", true, 'F', ValueBaselineCollided } },
        { { SeriesGroupedSuccess, "349D75", false, 'K', ValueAreaSuccess }, { SeriesBaselineSuccess, "7F8C9A", true, 'L', ValueBaselineSuccess } },
        { { SeriesGroupedCollided, "D86354", false, 'M', ValueAreaCollided }, { SeriesBaselineCollided, "7F8C9A", true, 'N', ValueBaselineCollided } }
    };
    *pSpec = (ChartSpec_t){ .ullFirstRow = 4 };
    *pAnchor = (ChartAnchor_t){ .uiToColumn = 8 };
    (void)pLayout;
    if (uiChart < AreaCount) {
        pSpec->Area = (AreaType)uiChart;
        pSpec->aSeries[0] = (ChartSeries_t){ SeriesSuccess, "349D75", false, (char)((AreaAlert == uiChart) ? 'C' : 'I'), ValueAreaSuccess };
        pSpec->aSeries[1] = (ChartSeries_t){ SeriesCollided, "D86354", true, (char)((AreaAlert == uiChart) ? 'D' : 'J'), ValueAreaCollided };
        pAnchor->uiFromRow = 11 + 19 * uiChart;
        pAnchor->uiToRow = 28 + 19 * uiChart;
        return SetChartText(pSpec->acTitle, sizeof(pSpec->acTitle), "%s 전송 추이", gpacAreaNames[uiChart], 0) &&
            SetChartText(pSpec->acSheet, sizeof(pSpec->acSheet), "회차별 데이터", "", 0) &&
            SetChartText(pAnchor->acName, sizeof(pAnchor->acName), "%s 전송 추이", gpacAreaNames[uiChart], 0);
    }
    uiChart -= AreaCount;
    pSpec->Area = (AreaType)(uiChart / 2);
    pSpec->aSeries[0] = aCompare[uiChart][0];
    pSpec->aSeries[1] = aCompare[uiChart][1];
    pAnchor->uiFromRow = 65 + 19 * uiChart;
    pAnchor->uiToRow = 82 + 19 * uiChart;
    return SetChartText(pSpec->acTitle, sizeof(pSpec->acTitle), (0 == uiChart % 2) ? "%s 성공 건수 비교 (그룹 적용 · 미적용)" : "%s 충돌 전송 건수 비교 (그룹 적용 · 미적용)",
            gpacAreaNames[pSpec->Area], 0) &&
        SetChartText(pSpec->acSheet, sizeof(pSpec->acSheet), "미적용 비교", "", 0) &&
        SetChartText(pAnchor->acName, sizeof(pAnchor->acName), (0 == uiChart % 2) ? "%s 성공 건수 비교" : "%s 충돌 전송 건수 비교", gpacAreaNames[pSpec->Area], 0);
}

static bool BuildGroupChart(const WorkbookLayout_t *pLayout, uint32_t uiUnit, uint32_t uiTableEnd, ChartSpec_t *pSpec, ChartAnchor_t *pAnchor)
{
    const GroupUnit_t *pUnit = &pLayout->aUnits[uiUnit];
    uint32_t uiRoundSheet = uiUnit / pLayout->uiUnitsPerSheet;
    uint32_t uiOffset = uiUnit % pLayout->uiUnitsPerSheet;
    int32_t iWritten;

    *pSpec = (ChartSpec_t){ .Area = pUnit->Area, .uiGroup = pUnit->uiGroup, .ullFirstRow = 4 + (uint64_t)uiOffset * pLayout->pResult->ullRoundCount };
    pSpec->aSeries[0] = (ChartSeries_t){ SeriesSuccess, "349D75", false, 'E', ValueGroupSuccess };
    pSpec->aSeries[1] = (ChartSeries_t){ SeriesCollided, "D86354", true, 'F', ValueGroupCollided };
    *pAnchor = (ChartAnchor_t){ .uiFromRow = uiTableEnd + 1 + 18 * uiUnit, .uiToRow = uiTableEnd + 17 + 18 * uiUnit, .uiToColumn = 9 };
    iWritten = snprintf(pSpec->acTitle, sizeof(pSpec->acTitle), "%s 그룹 %u 전송 추이", gpacAreaNames[pUnit->Area], pUnit->uiGroup + 1);
    if ((iWritten <= 0) || ((size_t)iWritten >= sizeof(pSpec->acTitle))) {
        return false;
    }
    memcpy(pAnchor->acName, pSpec->acTitle, sizeof(pAnchor->acName));
    return FormatGroupRoundSheetName(pSpec->acSheet, sizeof(pSpec->acSheet), uiRoundSheet);
}

static bool WriteDrawing(WorkbookPackage_t *pPackage, uint32_t uiDrawing, uint32_t uiFirstChart, const ChartAnchor_t *paAnchors, uint32_t uiCount)
{
    FILE *pFile = pPackage->pFile;

    if (!BeginNumberedPart(pPackage, "xl/drawings/_rels/drawing%u.xml.rels", uiDrawing)) {
        return false;
    }
    fputs(RELATIONSHIPS, pFile);
    for (uint32_t uiIndex = 0; uiIndex < uiCount; uiIndex++) {
        fprintf(pFile, "<Relationship Id=\"rId%u\" Type=\"" REL_NS "/chart\" Target=\"../charts/chart%u.xml\"/>", uiIndex + 1, uiFirstChart + uiIndex);
    }
    if ((fputs("</Relationships>", pFile) < 0) || !FinishPart(pPackage) || !BeginNumberedPart(pPackage, "xl/drawings/drawing%u.xml", uiDrawing)) {
        return false;
    }
    fputs(XML_DECL "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\" xmlns:a=\"" DRAWING_NS "\">", pFile);
    for (uint32_t uiIndex = 0; uiIndex < uiCount; uiIndex++) {
        const ChartAnchor_t *pAnchor = &paAnchors[uiIndex];
        fprintf(pFile, "<xdr:twoCellAnchor><xdr:from><xdr:col>0</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>%u</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:from>"
            "<xdr:to><xdr:col>%u</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>%u</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:to>"
            "<xdr:graphicFrame macro=\"\"><xdr:nvGraphicFramePr><xdr:cNvPr id=\"%u\" name=\"%s\"/><xdr:cNvGraphicFramePr/></xdr:nvGraphicFramePr>"
            "<xdr:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/></xdr:xfrm><a:graphic><a:graphicData uri=\"" CHART_NS "\">"
            "<c:chart xmlns:c=\"" CHART_NS "\" xmlns:r=\"" REL_NS "\" r:id=\"rId%u\"/></a:graphicData></a:graphic></xdr:graphicFrame><xdr:clientData/></xdr:twoCellAnchor>",
            pAnchor->uiFromRow, pAnchor->uiToColumn, pAnchor->uiToRow, uiIndex + 1, pAnchor->acName, uiIndex + 1);
    }
    fputs("</xdr:wsDr>", pFile);
    return FinishPart(pPackage);
}

static bool WriteSheets(WorkbookPackage_t *pPackage, const WorkbookLayout_t *pLayout, uint32_t *puiTableEnd)
{
    uint32_t uiRoundSheet = 0;

    for (uint32_t uiSheet = 0; uiSheet < pLayout->uiSheetCount; uiSheet++) {
        bool bWritten = false;

        switch (pLayout->aKinds[uiSheet]) {
        case SheetSummary:
            bWritten = WriteSummary(pPackage, pLayout);
            break;

        case SheetGroupSettings:
            bWritten = WriteGroupSettingsSheet(pPackage, pLayout, uiSheet + 1);
            break;

        case SheetGroupResults:
            bWritten = WriteGroupResultsSheet(pPackage, pLayout, uiSheet + 1, puiTableEnd);
            break;

        case SheetComparison:
            bWritten = WriteComparisonSheet(pPackage, pLayout->pResult, uiSheet + 1);
            break;

        case SheetRoundData:
            bWritten = WriteRoundData(pPackage, pLayout->pResult, uiSheet + 1);
            break;

        case SheetGroupRounds:
            bWritten = WriteGroupRoundSheet(pPackage, pLayout, uiSheet + 1, uiRoundSheet++);
            break;

        case SheetRepeat:
            bWritten = WriteRepeatSheet(pPackage, pLayout->pResult, uiSheet + 1);
            break;

        default:
            break;
        }
        if (!bWritten) {
            return false;
        }
    }
    return true;
}

static bool WriteDrawingsAndCharts(WorkbookPackage_t *pPackage, const WorkbookLayout_t *pLayout, uint32_t uiTableEnd)
{
    ChartAnchor_t *paAnchors = calloc(pLayout->uiChartCount, sizeof(*paAnchors));
    ChartSpec_t *paSpecs = calloc(pLayout->uiChartCount, sizeof(*paSpecs));
    bool bWritten = (NULL != paAnchors) && (NULL != paSpecs);

    for (uint32_t uiChart = 0; bWritten && (uiChart < pLayout->uiChartCount); uiChart++) {
        bWritten = (uiChart < pLayout->uiSummaryCharts)
            ? BuildSummaryChart(pLayout, uiChart, &paSpecs[uiChart], &paAnchors[uiChart])
            : BuildGroupChart(pLayout, uiChart - pLayout->uiSummaryCharts, uiTableEnd, &paSpecs[uiChart], &paAnchors[uiChart]);
    }
    bWritten = bWritten && WritePart(pPackage, "xl/worksheets/_rels/sheet1.xml.rels", RELATIONSHIPS
        "<Relationship Id=\"rId1\" Type=\"" REL_NS "/drawing\" Target=\"../drawings/drawing1.xml\"/></Relationships>");
    if (bWritten && pLayout->bGrouped) {
        bWritten = WritePart(pPackage, "xl/worksheets/_rels/sheet3.xml.rels", RELATIONSHIPS
            "<Relationship Id=\"rId1\" Type=\"" REL_NS "/drawing\" Target=\"../drawings/drawing2.xml\"/></Relationships>");
    }
    bWritten = bWritten && WriteDrawing(pPackage, 1, 1, paAnchors, pLayout->uiSummaryCharts);
    if (bWritten && pLayout->bGrouped) {
        bWritten = WriteDrawing(pPackage, 2, pLayout->uiSummaryCharts + 1, &paAnchors[pLayout->uiSummaryCharts], pLayout->uiUnitCount);
    }
    for (uint32_t uiChart = 0; bWritten && (uiChart < pLayout->uiChartCount); uiChart++) {
        bWritten = WriteChart(pPackage, pLayout->pResult, uiChart + 1, &paSpecs[uiChart]);
    }
    free(paAnchors);
    free(paSpecs);
    return bWritten;
}

ReportCheck CheckExportLimits(const SimulationResult_t *pResult)
{
    uint64_t ullRounds = pResult->ullRoundCount;
    uint64_t ullBytes;

    if (ullRounds > REPORT_MAX_ROUNDS) {
        return ReportTooManyRounds;
    }
    /* Upper estimates of the XML written per row and per chart point keep the stored ZIP below 4GB. */
    ullBytes = UINT64_C(300000) + ullRounds * (900 + 2 * 2 * 260);
    if (IsGroupedResult(pResult)) {
        uint64_t ullUnits = 0;
        uint64_t ullRuns = CountSlotRuns(pResult);

        for (uint32_t uiArea = 0; uiArea < AreaCount; uiArea++) {
            ullUnits += pResult->aPlans[uiArea].uiGroupCount;
        }
        if (14 + ullUnits + ullRuns > REPORT_SHEET_ROWS) {
            return ReportTooLarge;
        }
        ullBytes += ullUnits * ullRounds * (700 + 2 * 260) + ullRounds * (1100 + 4 * 2 * 260) + ullRuns * 500 + ullUnits * 3000;
    }
    if (IsRepeatedResult(pResult)) {
        ullBytes += UINT64_C(20000) + (uint64_t)pResult->uiRepeatCount * 1500;
    }
    return (ullBytes >= REPORT_BYTE_LIMIT) ? ReportTooLarge : ReportOk;
}

static bool SaveWorkbook(const wchar_t *pcPath, const SimulationResult_t *pResult, CheckStopFunction CheckStop, void *pvContext)
{
    WorkbookPackage_t Package = { .pFile = _wfopen(pcPath, L"w+b"), .CheckStop = CheckStop, .pvContext = pvContext };
    WorkbookLayout_t *pLayout = malloc(sizeof(*pLayout));
    uint32_t uiTableEnd = 0;
    bool bWritten;

    if ((NULL == Package.pFile) || (NULL == pLayout)) {
        if (NULL != Package.pFile) {
            fclose(Package.pFile);
        }
        free(pLayout);
        return false;
    }
    for (uint32_t uiIndex = 0; uiIndex < 256; uiIndex++) {
        uint32_t uiCrc = uiIndex;
        for (uint32_t uiBit = 0; uiBit < 8; uiBit++) {
            uiCrc = (uiCrc >> 1) ^ ((0 != (uiCrc & 1)) ? UINT32_C(0xedb88320) : 0);
        }
        Package.auiCrcTable[uiIndex] = uiCrc;
    }
    BuildWorkbookLayout(pResult, pLayout);
    bWritten = WriteWorkbookParts(&Package, pLayout) && WriteSheets(&Package, pLayout, &uiTableEnd) &&
        WriteDrawingsAndCharts(&Package, pLayout, uiTableEnd) && !CheckCancelled(&Package) && FinishPackage(&Package);
    free(Package.paParts);
    free(pLayout);
    return (0 == fclose(Package.pFile)) && bWritten;
}

bool ExportResults(const wchar_t *pcPath, const SimulationResult_t *pResult, CheckStopFunction CheckStop, void *pvContext)
{
    wchar_t acFullPath[MAX_PATH];
    wchar_t acDirectory[MAX_PATH];
    wchar_t acTemporary[MAX_PATH];
    wchar_t *pcFileName = NULL;
    DWORD uiLength = GetFullPathNameW(pcPath, MAX_PATH, acFullPath, &pcFileName);
    size_t uDirectoryLength;
    bool bWritten;

    if ((ReportOk != CheckExportLimits(pResult)) || (0 == uiLength) || (uiLength >= MAX_PATH) || (NULL == pcFileName)) {
        return false;
    }
    uDirectoryLength = (size_t)(pcFileName - acFullPath);
    wmemcpy(acDirectory, acFullPath, uDirectoryLength);
    acDirectory[uDirectoryLength] = L'\0';
    if (0 == GetTempFileNameW(acDirectory, L"alo", 0, acTemporary)) {
        return false;
    }
    bWritten = SaveWorkbook(acTemporary, pResult, CheckStop, pvContext) &&
        ((NULL == CheckStop) || !CheckStop(pvContext)) &&
        (FALSE != MoveFileExW(acTemporary, acFullPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
    if (!bWritten) {
        DeleteFileW(acTemporary);
    }
    return bWritten;
}
