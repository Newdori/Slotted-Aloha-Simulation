#include "Report.h"
#include "ReportXml.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct PackagePart {
    const char *pcName;
    uint32_t uiOffset;
    uint32_t uiSize;
    uint32_t uiCrc;
} PackagePart_t;

typedef struct WorkbookPackage {
    FILE *pFile;
    PackagePart_t aParts[12];
    uint32_t uiPartCount;
    uint32_t auiCrcTable[256];
    CheckStopFunction CheckStop;
    void *pvContext;
} WorkbookPackage_t;

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
    uint32_t uiNameLength = (uint32_t)strlen(pcName);

    if (CheckCancelled(pPackage) || (pPackage->uiPartCount >= 12) || (llOffset < 0) || ((uint64_t)llOffset >= UINT32_MAX)) {
        return false;
    }
    pPackage->aParts[pPackage->uiPartCount++] = (PackagePart_t){ .pcName = pcName, .uiOffset = (uint32_t)llOffset };
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
    return (uiNameLength == fwrite(pcName, 1, uiNameLength, pFile)) && (0 == ferror(pFile));
}

static bool FinishPart(WorkbookPackage_t *pPackage)
{
    PackagePart_t *pPart = &pPackage->aParts[pPackage->uiPartCount - 1];
    FILE *pFile = pPackage->pFile;
    int64_t llEnd = _ftelli64(pFile);
    uint64_t ullStart = pPart->uiOffset + UINT64_C(30) + strlen(pPart->pcName);
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
        const PackagePart_t *pPart = &pPackage->aParts[uiIndex];
        uint32_t uiLength = (uint32_t)strlen(pPart->pcName);
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
        if (uiLength != fwrite(pPart->pcName, 1, uiLength, pFile)) {
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

static bool WriteWorkbookParts(WorkbookPackage_t *pPackage)
{
    return WritePart(pPackage, "[Content_Types].xml", gacContentTypes) &&
        WritePart(pPackage, "_rels/.rels", RELATIONSHIPS
        "<Relationship Id=\"rId1\" Type=\"" REL_NS "/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>") &&
        WritePart(pPackage, "xl/workbook.xml", XML_DECL "<workbook xmlns=\"" SHEET_NS "\" xmlns:r=\"" REL_NS "\">"
        "<bookViews><workbookView activeTab=\"0\"/></bookViews><sheets>"
        "<sheet name=\"결과\" sheetId=\"1\" r:id=\"rId1\"/><sheet name=\"회차별 데이터\" sheetId=\"2\" r:id=\"rId2\"/></sheets></workbook>") &&
        WritePart(pPackage, "xl/_rels/workbook.xml.rels", RELATIONSHIPS
        "<Relationship Id=\"rId1\" Type=\"" REL_NS "/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"" REL_NS "/worksheet\" Target=\"worksheets/sheet2.xml\"/>"
        "<Relationship Id=\"rId3\" Type=\"" REL_NS "/styles\" Target=\"styles.xml\"/></Relationships>") &&
        WritePart(pPackage, "xl/styles.xml", gacStyles);
}

static void WriteTextCell(FILE *pFile, char cColumn, uint32_t uiRow, uint32_t uiStyle, const char *pcText)
{
    /* All labels are XML-safe constants, never user-supplied strings. */
    fprintf(pFile, "<c r=\"%c%u\" s=\"%u\" t=\"inlineStr\"><is><t>%s</t></is></c>", cColumn, uiRow, uiStyle, pcText);
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

static void WriteTimeCell(FILE *pFile, char cColumn, uint32_t uiRow, uint64_t ullMilliseconds)
{
    fprintf(pFile, "<c r=\"%c%u\" s=\"5\"><v>%llu.%03u</v></c>", cColumn, uiRow,
        ullMilliseconds / 1000, (uint32_t)(ullMilliseconds % 1000));
}

static bool WriteSummary(WorkbookPackage_t *pPackage, const SimulationResult_t *pResult)
{
    FILE *pFile = pPackage->pFile;
    const SimulationConfig_t *pConfig = &pResult->Config;
    const char *const pacSettings[] = { "심볼레이트 (sps)", "Wave ID", "슈퍼프레임 주기 (초)", "시뮬레이션 시간 (초)", "단말 수" };
    const uint64_t aullValues[] = { pConfig->uiSymbolRate, pConfig->uiWaveId, pConfig->ullSuperframeMilliseconds, pConfig->ullDurationMilliseconds, pConfig->uiTerminalCount };
    const char *const pacFinal[] = { "영역", "누적 성공 건수", "누적 충돌 전송 건수", "누적 충돌 칸 수", "누적 유휴 칸 수", "접속 실패 단말 갯수" };

    if (!BeginPart(pPackage, "xl/worksheets/sheet1.xml")) {
        return false;
    }
    fputs(SHEET_START "<dimension ref=\"A1:H54\"/><sheetViews><sheetView showGridLines=\"0\" workbookViewId=\"0\"/></sheetViews>"
        "<sheetFormatPr defaultRowHeight=\"24\"/><cols><col min=\"1\" max=\"1\" width=\"26\" customWidth=\"1\"/>"
        "<col min=\"2\" max=\"5\" width=\"23\" customWidth=\"1\"/><col min=\"6\" max=\"8\" width=\"25\" customWidth=\"1\"/></cols><sheetData>"
        "<row r=\"1\" ht=\"34\" customHeight=\"1\">", pFile);
    WriteTextCell(pFile, 'A', 1, 1, "Slotted Aloha 채널 경쟁 상황 시뮬레이션");
    fputs("</row><row r=\"3\">", pFile);
    WriteTextCell(pFile, 'A', 3, 1, "설정 값");
    WriteTextCell(pFile, 'E', 3, 1, "접속 실패 단말");
    fputs("</row><row r=\"4\" ht=\"32\" customHeight=\"1\">", pFile);
    WriteTextCell(pFile, 'A', 4, 2, "항목");
    WriteTextCell(pFile, 'B', 4, 2, "값");
    WriteTextCell(pFile, 'E', 4, 2, "영역");
    WriteTextCell(pFile, 'F', 4, 2, "채널 수");
    WriteTextCell(pFile, 'G', 4, 2, "단말 수");
    WriteTextCell(pFile, 'H', 4, 2, "접속 실패 단말 갯수");
    fputs("</row>", pFile);
    for (uint32_t uiIndex = 0; uiIndex < 5; uiIndex++) {
        uint32_t uiRow = uiIndex + 5;
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
            WriteTextCell(pFile, 'E', uiRow, 6, "종료 시점까지 해당 영역에서 한 번도 성공하지 못한 단말 수입니다. 완료 회차가 없거나 채널 수가 0인 영역은 집계하지 않습니다.");
        }
        fputs("</row>", pFile);
    }
    fputs("<row r=\"11\">", pFile);
    WriteTextCell(pFile, 'A', 11, 6, (0 == pResult->ullRoundCount) ? "완료된 수신 회차가 없습니다. 그래프에 표시할 데이터가 없습니다."
        : "각 지점의 숫자는 해당 회차의 전송 단말 수입니다. 회차별 데이터 시트에 모든 회차와 누적 값을 기록했습니다.");
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
    fputs("</row></sheetData><mergeCells count=\"7\"><mergeCell ref=\"A1:H1\"/><mergeCell ref=\"A3:C3\"/>"
        "<mergeCell ref=\"E3:H3\"/><mergeCell ref=\"E8:H9\"/><mergeCell ref=\"A11:H11\"/>"
        "<mergeCell ref=\"A50:H50\"/><mergeCell ref=\"A54:H54\"/></mergeCells><drawing r:id=\"rId1\"/></worksheet>", pFile);
    return FinishPart(pPackage);
}

static bool WriteRoundData(WorkbookPackage_t *pPackage, const SimulationResult_t *pResult)
{
    FILE *pFile = pPackage->pFile;
    uint64_t aullSuccess[AreaCount] = { 0 };
    uint64_t aullCollision[AreaCount] = { 0 };
    const char *const pacHeaders[] = { "회차", "수신 시각 (초)",
        "Alert&#10;성공 건수", "Alert&#10;충돌 전송 건수", "Alert&#10;충돌 칸 수", "Alert&#10;유휴 칸 수", "Alert&#10;누적 성공 건수", "Alert&#10;누적 충돌 전송 건수",
        "LB&#10;성공 건수", "LB&#10;충돌 전송 건수", "LB&#10;충돌 칸 수", "LB&#10;유휴 칸 수", "LB&#10;누적 성공 건수", "LB&#10;누적 충돌 전송 건수" };

    if (!BeginPart(pPackage, "xl/worksheets/sheet2.xml")) {
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

static void WriteChartTitle(FILE *pFile, const char *pcTitle)
{
    fprintf(pFile, "<c:title><c:tx><c:rich><a:bodyPr/><a:lstStyle/><a:p><a:pPr><a:defRPr sz=\"1200\"/></a:pPr>"
        "<a:r><a:rPr lang=\"ko-KR\"/><a:t>%s</a:t></a:r></a:p></c:rich></c:tx><c:overlay val=\"0\"/></c:title>", pcTitle);
}

static bool WriteChartValues(WorkbookPackage_t *pPackage, const SimulationResult_t *pResult, AreaType Area, bool bCollision, bool bTimes)
{
    FILE *pFile = pPackage->pFile;
    char cColumn = bTimes ? 'B' : (char)(((AreaAlert == Area) ? 'C' : 'I') + (bCollision ? 1 : 0));
    uint64_t ullLastRow = (0 == pResult->ullRoundCount) ? 4 : pResult->ullRoundCount + 3;

    fprintf(pFile, "<c:numRef><c:f>'회차별 데이터'!$%c$4:$%c$%llu</c:f><c:numCache><c:formatCode>%s</c:formatCode><c:ptCount val=\"%llu\"/>",
        cColumn, cColumn, ullLastRow, bTimes ? "0.###" : "0", pResult->ullRoundCount);
    for (uint64_t ullIndex = 0; ullIndex < pResult->ullRoundCount; ullIndex++) {
        if (CheckCancelled(pPackage) || (0 != ferror(pFile))) {
            return false;
        }
        if (bTimes) {
            uint64_t ullTime = (ullIndex + 1) * pResult->Config.ullSuperframeMilliseconds;
            fprintf(pFile, "<c:pt idx=\"%llu\"><c:v>%llu.%03u</c:v></c:pt>", ullIndex, ullTime / 1000, (uint32_t)(ullTime % 1000));
        }
        else {
            RoundAreaCounts_t Counts = GetRoundAreaCounts(pResult, ullIndex, Area);
            fprintf(pFile, "<c:pt idx=\"%llu\"><c:v>%u</c:v></c:pt>", ullIndex, bCollision ? Counts.uiCollidedAttempts : Counts.uiSuccess);
        }
    }
    return fputs("</c:numCache></c:numRef>", pFile) >= 0;
}

static bool WriteChart(WorkbookPackage_t *pPackage, const SimulationResult_t *pResult, AreaType Area)
{
    FILE *pFile = pPackage->pFile;
    if (!BeginPart(pPackage, (AreaAlert == Area) ? "xl/charts/chart1.xml" : "xl/charts/chart2.xml")) {
        return false;
    }
    fputs(XML_DECL "<c:chartSpace xmlns:c=\"" CHART_NS "\" xmlns:a=\"" DRAWING_NS "\"><c:lang val=\"ko-KR\"/><c:chart>", pFile);
    WriteChartTitle(pFile, (AreaAlert == Area) ? "Alert 전송 추이" : "LB 전송 추이");
    fputs("<c:plotArea><c:layout/><c:lineChart><c:grouping val=\"standard\"/><c:varyColors val=\"0\"/>", pFile);
    for (uint32_t uiSeries = 0; uiSeries < 2; uiSeries++) {
        bool bCollision = 1 == uiSeries;
        fprintf(pFile, "<c:ser><c:idx val=\"%u\"/><c:order val=\"%u\"/><c:tx><c:v>%s</c:v></c:tx>"
            "<c:spPr><a:ln w=\"25400\"><a:solidFill><a:srgbClr val=\"%s\"/></a:solidFill><a:prstDash val=\"%s\"/></a:ln></c:spPr>"
            "<c:marker><c:symbol val=\"%s\"/><c:size val=\"5\"/></c:marker>"
            "<c:dLbls>",
            uiSeries, uiSeries, bCollision ? "충돌 전송 건수" : "성공 건수", bCollision ? "D86354" : "349D75",
            bCollision ? "dash" : "solid", bCollision ? "square" : "circle");
        for (uint64_t ullIndex = 0; ullIndex < pResult->ullRoundCount; ullIndex++) {
            RoundAreaCounts_t Counts = GetRoundAreaCounts(pResult, ullIndex, Area);
            bool bBelow = bCollision ? (Counts.uiCollidedAttempts <= Counts.uiSuccess) : (Counts.uiSuccess < Counts.uiCollidedAttempts);
            uint32_t uiValue = bCollision ? Counts.uiCollidedAttempts : Counts.uiSuccess;
            const char *pcPosition = bBelow ? "b" : "t";
            if (CheckCancelled(pPackage) || (0 != ferror(pFile))) {
                return false;
            }
            if (0 == uiValue) {
                pcPosition = (bCollision && (0 == Counts.uiSuccess)) ? "r" : "t";
            }
            fprintf(pFile, "<c:dLbl><c:idx val=\"%llu\"/><c:dLblPos val=\"%s\"/><c:showVal val=\"1\"/></c:dLbl>", ullIndex, pcPosition);
        }
        fputs("<c:numFmt formatCode=\"0\" sourceLinked=\"0\"/><c:showLegendKey val=\"0\"/><c:showVal val=\"1\"/>"
            "<c:showCatName val=\"0\"/><c:showSerName val=\"0\"/></c:dLbls><c:cat>", pFile);
        if (!WriteChartValues(pPackage, pResult, Area, bCollision, true) || (fputs("</c:cat><c:val>", pFile) < 0) ||
            !WriteChartValues(pPackage, pResult, Area, bCollision, false)) {
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

static bool WriteDrawing(WorkbookPackage_t *pPackage)
{
    FILE *pFile = pPackage->pFile;
    if (!WritePart(pPackage, "xl/worksheets/_rels/sheet1.xml.rels", RELATIONSHIPS
        "<Relationship Id=\"rId1\" Type=\"" REL_NS "/drawing\" Target=\"../drawings/drawing1.xml\"/></Relationships>") ||
        !WritePart(pPackage, "xl/drawings/_rels/drawing1.xml.rels", RELATIONSHIPS
        "<Relationship Id=\"rId1\" Type=\"" REL_NS "/chart\" Target=\"../charts/chart1.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"" REL_NS "/chart\" Target=\"../charts/chart2.xml\"/></Relationships>") ||
        !BeginPart(pPackage, "xl/drawings/drawing1.xml")) {
        return false;
    }
    fputs(XML_DECL "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\" xmlns:a=\"" DRAWING_NS "\">", pFile);
    for (uint32_t uiIndex = 0; uiIndex < AreaCount; uiIndex++) {
        fprintf(pFile, "<xdr:twoCellAnchor><xdr:from><xdr:col>0</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>%u</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:from>"
            "<xdr:to><xdr:col>8</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>%u</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:to>"
            "<xdr:graphicFrame macro=\"\"><xdr:nvGraphicFramePr><xdr:cNvPr id=\"%u\" name=\"%s 전송 추이\"/><xdr:cNvGraphicFramePr/></xdr:nvGraphicFramePr>"
            "<xdr:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/></xdr:xfrm><a:graphic><a:graphicData uri=\"" CHART_NS "\">"
            "<c:chart xmlns:c=\"" CHART_NS "\" xmlns:r=\"" REL_NS "\" r:id=\"rId%u\"/></a:graphicData></a:graphic></xdr:graphicFrame><xdr:clientData/></xdr:twoCellAnchor>",
            11 + 19 * uiIndex, 28 + 19 * uiIndex, uiIndex + 1, (AreaAlert == uiIndex) ? "Alert" : "LB", uiIndex + 1);
    }
    fputs("</xdr:wsDr>", pFile);
    return FinishPart(pPackage);
}

static bool SaveWorkbook(const wchar_t *pcPath, const SimulationResult_t *pResult, CheckStopFunction CheckStop, void *pvContext)
{
    WorkbookPackage_t Package = { .pFile = _wfopen(pcPath, L"w+b"), .CheckStop = CheckStop, .pvContext = pvContext };
    bool bWritten;

    if (NULL == Package.pFile) {
        return false;
    }
    for (uint32_t uiIndex = 0; uiIndex < 256; uiIndex++) {
        uint32_t uiCrc = uiIndex;
        for (uint32_t uiBit = 0; uiBit < 8; uiBit++) {
            uiCrc = (uiCrc >> 1) ^ ((0 != (uiCrc & 1)) ? UINT32_C(0xedb88320) : 0);
        }
        Package.auiCrcTable[uiIndex] = uiCrc;
    }
    bWritten = WriteWorkbookParts(&Package) && WriteSummary(&Package, pResult) && WriteRoundData(&Package, pResult) &&
        WriteDrawing(&Package) && WriteChart(&Package, pResult, AreaAlert) && WriteChart(&Package, pResult, AreaLb) &&
        !CheckCancelled(&Package) && FinishPackage(&Package);
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

    if ((pResult->ullRoundCount > REPORT_MAX_ROUNDS) || (0 == uiLength) || (uiLength >= MAX_PATH) || (NULL == pcFileName)) {
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
