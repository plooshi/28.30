#pragma once
#include "pch.h"
#include "Utils.h"

class Building
{
private:
	static bool CanBePlacedByPlayer(UClass* BuildClass);
	DefUHookOg(ServerCreateBuildingActor);
	static void ServerBeginEditingBuildingActor(UObject*, FFrame&);
	static void ServerEditBuildingActor(UObject*, FFrame&);
	static void ServerEndEditingBuildingActor(UObject*, FFrame&);
	static void ServerRepairBuildingActor(UObject*, FFrame&);
	DefHookOg(void, OnDamageServer, ABuildingSMActor*, float, FGameplayTagContainer, FVector, FHitResult, AFortPlayerControllerAthena*, AActor*, FGameplayEffectContextHandle);
	DefUHookOg(ServerSpawnDeco);
	static void ServerCreateBuildingAndSpawnDeco(UObject*, FFrame&);


	InitHooks;
};
