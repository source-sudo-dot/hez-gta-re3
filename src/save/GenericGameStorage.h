#pragma once

#include "Game.h"
#include "PCSave.h"

#define SLOT_COUNT (8)
// The autosave writes here.  It sits past the eight the player saves into and past
// the ninth that mission retry keeps for itself, so nothing can write over it by
// hand and it cannot be written over by a failed mission either.  The load list is
// the only place it shows.
#define AUTOSAVE_SLOT (SLOT_COUNT + 1)
#define SLOT_TOTAL (AUTOSAVE_SLOT + 1)

bool GenericSave(int file);
bool GenericLoad();
bool ReadInSizeofSaveFileBuffer(int32 &file, uint32 &size);
bool ReadDataFromFile(int32 file, uint8 *buf, uint32 size);
bool CloseFile(int32 file);
void DoGameSpecificStuffAfterSucessLoad();
bool CheckSlotDataValid(int32 slot);
void MakeSpaceForSizeInBufferPointer(uint8 *&presize, uint8 *&buf, uint8 *&postsize);
void CopySizeAndPreparePointer(uint8 *&buf, uint8 *&postbuf, uint8 *&postbuf2, uint32 &unused, uint32 &size);
void DoGameSpecificStuffBeforeSave();
void MakeValidSaveName(int32 slot);
wchar *GetSavedGameDateAndTime(int32 slot);
wchar *GetNameOfSavedGame(int32 slot);
bool CheckDataNotCorrupt(int32 slot, char *name);
bool RestoreForStartLoad();
int align4bytes(int32 size);

#ifdef FIX_INCOMPATIBLE_SAVES
uint8 GetSaveType(char *savename);
bool FixSave(int32 slot, uint8 save_type);
#endif

extern class CDate CompileDateAndTime;

extern char DefaultPCSaveFileName[260];
extern char ValidSaveName[260];
extern char LoadFileName[256];
extern wchar SlotFileName[SLOT_TOTAL][260];
extern wchar SlotSaveDate[SLOT_TOTAL][70];
extern int CheckSum;
extern enum eLevelName m_LevelToLoad;
extern int Slots[SLOT_TOTAL+1];

extern bool b_FoundRecentSavedGameWantToLoad;
extern bool JustLoadedDontFadeInYet;
extern bool StillToFadeOut;
extern uint32 TimeStartedCountingForFade;
extern uint32 TimeToStayFadedBeforeFadeOut;

extern char SaveFileNameJustSaved[260]; // 8F2570

const char TopLineEmptyFile[] = "THIS FILE IS NOT VALID YET";

#ifdef MISSION_REPLAY
extern int8 IsQuickSave; // originally int

bool SaveGameForPause(int);

enum {
	SAVE_TYPE_NORMAL,
	SAVE_TYPE_QUICKSAVE,
	SAVE_TYPE_2,
	SAVE_TYPE_QUICKSAVE_FOR_MISSION_REPLAY
};

#endif
