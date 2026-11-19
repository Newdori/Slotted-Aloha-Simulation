#ifndef SLOTTED_ALOHA_REPORT_H
#define SLOTTED_ALOHA_REPORT_H

#include <wchar.h>

#include "Simulation.h"

#define REPORT_MAX_ROUNDS UINT64_C(1048573)

typedef enum ReportCheck {
    ReportOk,
    ReportTooManyRounds,
    ReportTooLarge
} ReportCheck;

ReportCheck CheckExportLimits(const SimulationResult_t *pResult);
bool ExportResults(const wchar_t *pcPath, const SimulationResult_t *pResult, CheckStopFunction CheckStop, void *pvContext);

#endif
