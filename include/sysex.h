#ifndef MEGAFM_CMAKE_SYSEX_H
#define MEGAFM_CMAKE_SYSEX_H

#define MAX_SYSEX_DATA_LENGTH 2048
extern int sysExDataIndex;
extern byte sysExData[MAX_SYSEX_DATA_LENGTH];

void handleSysEx();
void abortSysEx(bool errro);

#endif // MEGAFM_CMAKE_SYSEX_H