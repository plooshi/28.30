#include "pch.h"
#include "Utils.h"
#include "Looting.h"
#include "Inventory.h"

void Looting::SetupLDSForPackage(TArray<FFortItemEntry>& LootDrops, SDK::FName Package, int i, FName TierGroup, int WorldLevel)
{
	TArray<FFortLootPackageData*> LPGroups;
	for (auto const& Val : LPGroupsAll)
	{
		if (!Val)
			continue;

		if (Val->LootPackageID != Package)
			continue;
		if (i != -1 && Val->LootPackageCategory != i)
			continue;
		if (WorldLevel >= 0) {
			if (Val->MaxWorldLevel >= 0 && WorldLevel > Val->MaxWorldLevel)
				continue;
			if (Val->MinWorldLevel >= 0 && WorldLevel < Val->MinWorldLevel)
				continue;
		}

		LPGroups.Add(Val);
	}
	if (LPGroups.Num() == 0)
		return;

	auto LootPackage = PickWeighted(LPGroups, [](float Total) { return ((float)rand() / 32767.f) * Total; });
	if (!LootPackage)
		return;

	if (LootPackage->LootPackageCall.Num() > 1)
	{
		for (int i = 0; i < LootPackage->Count; i++)
			SetupLDSForPackage(LootDrops, FName(LootPackage->LootPackageCall), 0, TierGroup);

		return;
	}

	auto ItemDefinition = LootPackage->ItemDefinition->Cast<UFortWorldItemDefinition>();
	if (!ItemDefinition)
		return;

	bool found = false;
	for (auto& LootDrop : LootDrops)
		if (LootDrop.ItemDefinition == ItemDefinition)
		{
			LootDrop.Count += LootPackage->Count;

			if (LootDrop.Count > (int32)Utils::EvaluateScalableFloat(ItemDefinition->MaxStackSize)) {
				auto OGCount = LootDrop.Count;
				LootDrop.Count = (int32)Utils::EvaluateScalableFloat(ItemDefinition->MaxStackSize);

				if (Inventory::GetQuickbar((UFortItemDefinition*)LootDrop.ItemDefinition) == EFortQuickBars::Secondary)
					LootDrops.Add(*Inventory::MakeItemEntry(ItemDefinition, OGCount - (int32)Utils::EvaluateScalableFloat(ItemDefinition->MaxStackSize), std::clamp(Inventory::GetLevel(ItemDefinition->LootLevelData), ItemDefinition->MinLevel, ItemDefinition->MaxLevel)));
			}

			if (Inventory::GetQuickbar((UFortItemDefinition*)LootDrop.ItemDefinition) == EFortQuickBars::Secondary)
				found = true;
		}

	if (!found && LootPackage->Count > 0)
		LootDrops.Add(*Inventory::MakeItemEntry(ItemDefinition, LootPackage->Count, std::clamp(Inventory::GetLevel(ItemDefinition->LootLevelData), ItemDefinition->MinLevel, ItemDefinition->MaxLevel)));
}

TArray<FFortItemEntry> Looting::ChooseLootForContainer(FName TierGroup, int LootTier, int WorldLevel)
{
	TArray<FFortLootTierData*> TierDataGroups;
	for (auto const& Val : TierDataAllGroups) {
		if (Val->TierGroup == TierGroup && (LootTier == -1 ? true : LootTier == Val->LootTier))
			TierDataGroups.Add(Val);
	}
	auto LootTierData = PickWeighted(TierDataGroups, [](float Total) { return ((float)rand() / 32767.f) * Total; });
	if (!LootTierData)
		return {};

	float DropCount = 0;
	if (LootTierData->NumLootPackageDrops > 0) {
		DropCount = LootTierData->NumLootPackageDrops < 1 ? 1 : (float)((int)((LootTierData->NumLootPackageDrops * 2) - .5f) >> 1);

		if (LootTierData->NumLootPackageDrops > 1) {
			float idk = LootTierData->NumLootPackageDrops - DropCount;

			if (idk > 0.0000099999997f)
				DropCount += idk >= ((float)rand() / 32767);
		}
	}

	float AmountOfLootDrops = 0;
	float MinLootDrops = 0;

	for (auto& Min : LootTierData->LootPackageCategoryMinArray)
		AmountOfLootDrops += Min;

	int SumWeights = 0;

	for (int i = 0; i < LootTierData->LootPackageCategoryWeightArray.Num(); ++i)
		if (LootTierData->LootPackageCategoryWeightArray[i] > 0 && LootTierData->LootPackageCategoryMaxArray[i] != 0)
			SumWeights += LootTierData->LootPackageCategoryWeightArray[i];

	while (SumWeights > 0)
	{
		AmountOfLootDrops++;

		if (AmountOfLootDrops >= LootTierData->NumLootPackageDrops) {
			AmountOfLootDrops = AmountOfLootDrops;
			break;
		}

		SumWeights--;
	}

	if (!AmountOfLootDrops)
		AmountOfLootDrops = AmountOfLootDrops;

	TArray<FFortItemEntry> LootDrops;

	for (int i = 0; i < AmountOfLootDrops && i < LootTierData->LootPackageCategoryMinArray.Num(); i++)
		for (int j = 0; j < LootTierData->LootPackageCategoryMinArray[i] && LootTierData->LootPackageCategoryMinArray[i] >= 1; j++)
			SetupLDSForPackage(LootDrops, LootTierData->LootPackage, i, TierGroup, WorldLevel);

	std::map<UFortWorldItemDefinition*, int32> AmmoMap;
	for (auto& Item : LootDrops)
		if (Item.ItemDefinition->IsA<UFortWeaponRangedItemDefinition>() && !((UFortItemDefinition*)Item.ItemDefinition)->IsStackable() && ((UFortWorldItemDefinition*)Item.ItemDefinition)->GetAmmoWorldItemDefinition_BP())
		{
			auto AmmoDefinition = ((UFortWorldItemDefinition*)Item.ItemDefinition)->GetAmmoWorldItemDefinition_BP();
			int i = 0;
			auto AmmoEntry = LootDrops.Search([&](FFortItemEntry& Entry)
				{
					if (AmmoMap[AmmoDefinition] > 0 && i < AmmoMap[AmmoDefinition])
					{
						i++;
						return false;
					}
					AmmoMap[AmmoDefinition]++;
					return Entry.ItemDefinition == AmmoDefinition;
				});

			if (AmmoEntry)
				continue;

			FFortLootPackageData* Group = nullptr;
			static auto AmmoSmall = FName(L"WorldList.AthenaAmmoSmall");
			for (auto const& Val : LPGroupsAll)
				if (Val->LootPackageID == AmmoSmall && Val->ItemDefinition == AmmoDefinition)
				{
					Group = Val;
					break;
				}

			if (Group)
				LootDrops.Add(*Inventory::MakeItemEntry(AmmoDefinition, Group->Count, 0));
		}

	return LootDrops;
}


bool Looting::SpawnLootHook(ABuildingContainer* Container)
{
	auto& RealTierGroup = Container->SearchLootTierGroup;
	for (const auto& [OldTierGroup, RedirectedTierGroup] : ((AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode)->RedirectAthenaLootTierGroups)
	{
		if (OldTierGroup == Container->SearchLootTierGroup)
		{
			RealTierGroup = RedirectedTierGroup;
			break;
		}
	}

	for (auto& LootDrop : ChooseLootForContainer(RealTierGroup))
		Inventory::SpawnPickup(Container, LootDrop);

	Container->bAlreadySearched = true;
	Container->OnRep_bAlreadySearched();
	Container->SearchBounceData.SearchAnimationCount++;
	Container->BounceContainer();

	return true;
}

__int64 Looting::PostUpdate(ABuildingContainer* BuildingContainer, uint32 a2, __int64 a3)
{
	static auto RareAmmoBox = FName(L"Loot_ApolloAmmoBox_Rare");
	static auto RareChest = FName(L"Loot_ApolloTreasure_Rare");

	if (BuildingContainer->SearchLootTierGroup == RareAmmoBox || BuildingContainer->SearchLootTierGroup == RareChest)
		BuildingContainer->ReplicatedLootTier = 2;
	else
		BuildingContainer->ReplicatedLootTier = 1;

	BuildingContainer->OnRep_LootTier();

	return PostUpdateOG(BuildingContainer, a2, a3);
}

void Looting::SpawnLoot(FName& TierGroup, FVector Loc)
{
	auto& RealTierGroup = TierGroup;

	for (const auto& [OldTierGroup, RedirectedTierGroup] : ((AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode)->RedirectAthenaLootTierGroups)
	{
		if (OldTierGroup == TierGroup)
		{
			RealTierGroup = RedirectedTierGroup;
			break;
		}
	}

	for (auto& LootDrop : ChooseLootForContainer(RealTierGroup))
		Inventory::SpawnPickup(Loc, LootDrop);
}

void Looting::SpawnFloorLootForContainer(UBlueprintGeneratedClass* ContainerType)
{
	auto Containers = Utils::GetAll<ABuildingContainer>(ContainerType);

	for (auto& BuildingContainer : Containers)
		BuildingContainer->K2_DestroyActor();

	Containers.Free();
}

bool Looting::ServerOnAttemptInteract(ABuildingContainer* BuildingContainer, AFortPlayerPawn*)
{

	if (!BuildingContainer)
		return false;

	if (BuildingContainer->bAlreadySearched)
		return true;

	SpawnLoot(BuildingContainer->SearchLootTierGroup, BuildingContainer->K2_GetActorLocation() + BuildingContainer->GetActorRightVector() * 70.f + FVector{ 0, 0, 50 });

	BuildingContainer->bAlreadySearched = true;
	BuildingContainer->OnRep_bAlreadySearched();
	BuildingContainer->SearchBounceData.SearchAnimationCount++;
	BuildingContainer->BounceContainer();

	return true;
}

bool Looting::PickLootDrops(UObject* Object, FFrame& Stack, bool* Ret)
{
	UObject* WorldContextObject;
	FName TierGroupName;
	int32 WorldLevel;
	int32 ForcedLootTier;
	FGameplayTagContainer OptionalLootTags;

	Stack.StepCompiledIn(&WorldContextObject);
	auto& OutLootToDrop = Stack.StepCompiledInRef<TArray<FFortItemEntry>>();
	Stack.StepCompiledIn(&TierGroupName);
	Stack.StepCompiledIn(&WorldLevel);
	Stack.StepCompiledIn(&ForcedLootTier);
	Stack.StepCompiledIn(&OptionalLootTags);
	Stack.IncrementCode();

	printf("PickLootDrops for TierGroup %s\n", TierGroupName.ToString().c_str());

	auto LootDrops = ChooseLootForContainer(TierGroupName, ForcedLootTier, WorldLevel);

	for (auto& LootDrop : LootDrops)
		OutLootToDrop.Add(LootDrop);

	return *Ret = true;
}

AFortPickup* Looting::SpawnPickup(UObject* Object, FFrame& Stack, AFortPickup** Ret)
{
	UFortWorldItemDefinition* ItemDefinition;
	int32 NumberToSpawn;
	AFortPawn* TriggeringPawn;
	FVector Position;
	FVector Direction;
	Stack.StepCompiledIn(&ItemDefinition);
	Stack.StepCompiledIn(&NumberToSpawn);
	Stack.StepCompiledIn(&TriggeringPawn);
	Stack.StepCompiledIn(&Position);
	Stack.StepCompiledIn(&Direction);
	Stack.IncrementCode();

	return *Ret = Inventory::SpawnPickup(Position, ItemDefinition, NumberToSpawn, ItemDefinition->IsA<UFortWeaponItemDefinition>() ? Inventory::GetStats((UFortWeaponItemDefinition*)ItemDefinition)->ClipSize : 0, EFortPickupSourceTypeFlag::Other, EFortPickupSpawnSource::SupplyDrop);
}

AFortPickup* Looting::K2_SpawnPickupInWorld(UObject* Object, FFrame& Stack, AFortPickup** Ret)
{
	class UObject* WorldContextObject;
	class UFortWorldItemDefinition* ItemDefinition;
	int32 NumberToSpawn;
	FVector Position;
	FVector Direction;
	int32 OverrideMaxStackCount;
	bool bToss;
	bool bRandomRotation;
	bool bBlockedFromAutoPickup;
	int32 PickupInstigatorHandle;
	EFortPickupSourceTypeFlag SourceType;
	EFortPickupSpawnSource Source;
	class AFortPlayerController* OptionalOwnerPC;
	bool bPickupOnlyRelevantToOwner;
	Stack.StepCompiledIn(&WorldContextObject);
	Stack.StepCompiledIn(&ItemDefinition);
	Stack.StepCompiledIn(&NumberToSpawn);
	Stack.StepCompiledIn(&Position);
	Stack.StepCompiledIn(&Direction);
	Stack.StepCompiledIn(&OverrideMaxStackCount);
	Stack.StepCompiledIn(&bToss);
	Stack.StepCompiledIn(&bRandomRotation);
	Stack.StepCompiledIn(&bBlockedFromAutoPickup);
	Stack.StepCompiledIn(&PickupInstigatorHandle);
	Stack.StepCompiledIn(&SourceType);
	Stack.StepCompiledIn(&Source);
	Stack.StepCompiledIn(&OptionalOwnerPC);
	Stack.StepCompiledIn(&bPickupOnlyRelevantToOwner);
	Stack.IncrementCode();

	printf("K2_SpawnPickupInWorld[%s]\n", ItemDefinition->GetName().c_str());

	return *Ret = Inventory::SpawnPickup(Position, ItemDefinition, NumberToSpawn, 0, SourceType, Source, OptionalOwnerPC ? OptionalOwnerPC->MyFortPawn : nullptr, bToss, bRandomRotation);
}

AFortPickup* Looting::SpawnItemVariantPickupInWorld(UObject* Object, FFrame& Stack, AFortPickup** Ret)
{
	UObject* WorldContextObject;
	FSpawnItemVariantParams Params;
	Stack.StepCompiledIn(&WorldContextObject);
	Stack.StepCompiledIn(&Params);
	Stack.IncrementCode();

	printf("SpawnItemVariantPickupInWorld[%s]\n", Params.WorldItemDefinition->GetName().c_str());
	return *Ret = Inventory::SpawnPickup(Params.Position, Params.WorldItemDefinition, Params.NumberToSpawn, 0, Params.SourceType, Params.Source, nullptr, Params.bToss, Params.bRandomRotation);
}

AFortPickup* Looting::K2_SpawnPickupInWorldWithClassAndItemEntry(UObject* Context, FFrame& Stack, AFortPickup** Ret)
{
	UObject* WorldContextObject;
	FFortItemEntry Entry;
	TSubclassOf<AFortPickup> PickupClass;
	FVector Position;
	FVector Direction;
	int32 OverrideMaxStackCount;
	bool bToss;
	bool bRandomRotation;
	bool bBlockedFromAutoPickup;
	EFortPickupSourceTypeFlag SourceType;
	EFortPickupSpawnSource Source;
	class AFortPlayerController* OptionalOwnerPC;
	bool bPickupOnlyRelevantToOwner;

	Stack.StepCompiledIn(&WorldContextObject);
	Stack.StepCompiledIn(&Entry);
	Stack.StepCompiledIn(&PickupClass);
	Stack.StepCompiledIn(&Position);
	Stack.StepCompiledIn(&Direction);
	Stack.StepCompiledIn(&OverrideMaxStackCount);
	Stack.StepCompiledIn(&bToss);
	Stack.StepCompiledIn(&bRandomRotation);
	Stack.StepCompiledIn(&bBlockedFromAutoPickup);
	Stack.StepCompiledIn(&SourceType);
	Stack.StepCompiledIn(&Source);
	Stack.StepCompiledIn(&OptionalOwnerPC);
	Stack.StepCompiledIn(&bPickupOnlyRelevantToOwner);
	Stack.IncrementCode();

	printf("K2_SpawnPickupInWorldWithClassAndItemEntry[%s, %s]\n", Entry.ItemDefinition->GetName().c_str(), PickupClass->GetName().c_str());
	return *Ret = Inventory::SpawnPickup(Position, (UFortItemDefinition*) Entry.ItemDefinition, Entry.Count, Entry.Level, SourceType, Source, OptionalOwnerPC ? OptionalOwnerPC->MyFortPawn : nullptr, bToss, bRandomRotation);
}

void Looting::SpawnConsumableActor(ABGAConsumableSpawner* Spawner)
{
	auto LootDrops = ChooseLootForContainer(Spawner->SpawnLootTierGroup);
	if (LootDrops.Num() == 0)
		return;
	auto ItemDefinition = (UBGAConsumableWrapperItemDefinition*)LootDrops[0].ItemDefinition;

	auto GroundLoc = UFortKismetLibrary::FindGroundLocationAt(UWorld::GetWorld(), nullptr, Spawner->K2_GetActorLocation(), -1000, 2500, FName(L"FindGroundLocationAt")); // move to ground
	auto SpawnTransform = FTransform(GroundLoc, Spawner->K2_GetActorRotation());

	Utils::SpawnActor(ItemDefinition->ConsumableClass, SpawnTransform);
}

void Looting::Hook()
{
	Utils::Hook(Sarah::Offsets::ImageBase + 0x8820970, SpawnLootHook);
	//Utils::Hook(Sarah::Offsets::ImageBase + 0x6C5DF44, PostUpdate, PostUpdateOG);
	Utils::ExecHook(L"/Script/FortniteGame.FortKismetLibrary.PickLootDrops", PickLootDrops);
	Utils::ExecHook(L"/Script/FortniteGame.FortAthenaSupplyDrop.SpawnPickup", SpawnPickup);

	Utils::ExecHook(L"/Script/FortniteGame.FortKismetLibrary.K2_SpawnPickupInWorld", K2_SpawnPickupInWorld);
	Utils::ExecHook(L"/Script/FortniteGame.FortKismetLibrary.K2_SpawnPickupInWorldWithClassAndItemEntry", K2_SpawnPickupInWorldWithClassAndItemEntry);
	Utils::ExecHook(L"/Script/FortniteGame.FortKismetLibrary.SpawnItemVariantPickupInWorld", SpawnItemVariantPickupInWorld);
}
