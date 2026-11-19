#ifndef SLOTTED_ALOHA_GROUP_DIALOG_H
#define SLOTTED_ALOHA_GROUP_DIALOG_H

#include <windows.h>

#include "Group.h"

typedef struct GroupDialogInput {
    HINSTANCE Instance;
    HFONT Font;
    HFONT BoldFont;
    int32_t iDpi;
    SimulationConfig_t Config;
} GroupDialogInput_t;

bool RegisterGroupDialog(HINSTANCE Instance);
bool ShowGroupDialog(HWND Owner, const GroupDialogInput_t *pInput, GroupConfig_t *pGroups);

#endif
