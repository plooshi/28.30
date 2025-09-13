#pragma once
#include "pch.h"

class Looting
{
public:
	static inline TArray<FFortLootTierData*> TierDataAllGroups;
	static inline TArray<FFortLootPackageData*> LPGroupsAll;
private:
	static bool SpawnLootHook(ABuildingContainer*);
	DefHookOg(__int64, PostUpdate, ABuildingContainer*, uint32, __int64);
public:
	static void SpawnLoot(FName&, FVector);
	static void SpawnFloorLootForContainer(UBlueprintGeneratedClass*);
private:
	static bool ServerOnAttemptInteract(ABuildingContainer*, AFortPlayerPawn*);
	static void SetupLDSForPackage(TArray<FFortItemEntry>&, SDK::FName, int, FName, int WorldLevel = ((AFortGameStateAthena*)UWorld::GetWorld()->GameState)->WorldLevel);
	template <typename T>
	static T* PickWeighted(TArray<T*>& Map, float (*RandFunc)(float), bool bCheckZero = true) {
		float TotalWeight = std::accumulate(Map.begin(), Map.end(), 0.0f, [&](float acc, T*& p) { return acc + p->Weight; });
		float RandomNumber = RandFunc(TotalWeight);

		for (auto& Element : Map)
		{
			float Weight = Element->Weight;
			if (bCheckZero && Weight == 0)
				continue;

			if (RandomNumber <= Weight) return Element;

			RandomNumber -= Weight;
		}

		return nullptr;
	}
public:
	static TArray<FFortItemEntry> ChooseLootForContainer(FName, int = -1, int = ((AFortGameStateAthena*)UWorld::GetWorld()->GameState)->WorldLevel);
	static void SpawnConsumableActor(ABGAConsumableSpawner*);
private:
	static bool PickLootDrops(UObject*, FFrame&, bool*);
	static AFortPickup* SpawnPickup(UObject*, FFrame&, AFortPickup**);
	static AFortPickup* K2_SpawnPickupInWorld(UObject*, FFrame&, AFortPickup**);
	static AFortPickup* SpawnItemVariantPickupInWorld(UObject*, FFrame&, AFortPickup**);
	static AFortPickup* K2_SpawnPickupInWorldWithClassAndItemEntry(UObject*, FFrame&, AFortPickup**);

	InitHooks;
};