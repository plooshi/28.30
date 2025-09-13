#pragma once
#include "pch.h"

class Inventory
{
public:
	static FFortRangedWeaponStats* GetStats(UFortWeaponItemDefinition*);
	static int GetLevel(const FDataTableCategoryHandle&);
	static FFortItemEntry* MakeItemEntry(UFortItemDefinition*, int32, int32);
	static void TriggerInventoryUpdate(AFortPlayerController*, FFortItemEntry*);
	static UFortWorldItem* GiveItem(AFortPlayerController*, UFortItemDefinition*, int = 1, int = 0, int = 0, bool = true, bool = true, int = 0);
	static UFortWorldItem* GiveItem(AFortPlayerController*, FFortItemEntry, int = -1, bool = true, bool = true);
	static EFortQuickBars GetQuickbar(UFortItemDefinition*);
	static AFortPickupAthena* SpawnPickup(FVector, FFortItemEntry&, EFortPickupSourceTypeFlag = EFortPickupSourceTypeFlag::Tossed, EFortPickupSpawnSource = EFortPickupSpawnSource::Unset, AFortPlayerPawn* = nullptr, int = -1, bool = true, bool = true, bool = true);
	static AFortPickupAthena* SpawnPickup(FVector, UFortItemDefinition*, int, int, EFortPickupSourceTypeFlag = EFortPickupSourceTypeFlag::Tossed, EFortPickupSpawnSource = EFortPickupSpawnSource::Unset, AFortPlayerPawn* = nullptr, bool = true, bool = true);
	static AFortPickupAthena* SpawnPickup(ABuildingContainer*, FFortItemEntry, AFortPlayerPawn* = nullptr, int = -1);
	static void ReplaceEntry(AFortPlayerController*, FFortItemEntry&);
	static void Remove(AFortPlayerController*, FGuid);
private:
	static bool ServerRemoveInventoryItem(AFortPlayerControllerAthena*, FGuid, int, bool, bool);
	static void GiveItemToInventoryOwner(UObject*, FFrame&);
	static bool RemoveInventoryItem(__int64 InterfaceThing, FGuid ItemGuid, int Count, bool bForceRemoval);
	static bool AddItemToInventoryOwner(UFortItemEntryComponent*, TScriptInterface<class IFortInventoryOwnerInterface>, bool);
	static int32 K2_RemoveItemFromPlayer(UObject*, FFrame&, int32*);
	static int32 K2_RemoveItemFromPlayerByGuid(UObject*, FFrame&, int32*);

	InitHooks;
};