#pragma once
#include "pch.h"
#include "Utils.h"

class GamePhaseLogic {
private:
	DefHookOg(void, HandleMatchHasStarted, AFortGameModeAthena*);
	DefUHookOg(OnAircraftExitedDropZone);
public:
	static void Tick();
private:

	InitHooks;
};