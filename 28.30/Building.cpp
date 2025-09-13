#include "pch.h"
#include "Building.h"
#include "Inventory.h"

bool Building::CanBePlacedByPlayer(UClass* BuildClass) {
	return ((AFortGameStateAthena*)UWorld::GetWorld()->GameState)->AllPlayerBuildableClasses.Search([BuildClass](UClass* Class) { return Class == BuildClass; });
}

void Building::ServerCreateBuildingActor(UObject* Context, FFrame& Stack)
{
	FCreateBuildingActorData CreateBuildingData;
	Stack.StepCompiledIn(&CreateBuildingData);
	Stack.IncrementCode();

	auto PlayerController = (AFortPlayerController*)Context;
	if (!PlayerController)
		return callOG(PlayerController, L"/Script/FortniteGame.FortPlayerController", ServerCreateBuildingActor, CreateBuildingData);

	auto BuildingClassPtr = ((AFortGameStateAthena*)UWorld::GetWorld()->GameState)->AllPlayerBuildableClassesIndexLookup.SearchForKey([&](UClass* Class, int32 Handle) {
		return Handle == CreateBuildingData.BuildingClassHandle;
		});
	if (!BuildingClassPtr)
		return callOG(PlayerController, L"/Script/FortniteGame.FortPlayerController", ServerCreateBuildingActor, CreateBuildingData);

	auto BuildingClass = *BuildingClassPtr;
	printf("ServerCreateBuildingActor[%s]\n", BuildingClass->GetName().c_str());

	auto Resource = UFortKismetLibrary::K2_GetResourceItemDefinition(((ABuildingSMActor*)BuildingClass->DefaultObject)->ResourceType);


	FFortItemEntry* ItemEntry = nullptr;
	if (!PlayerController->bBuildFree)
	{
		ItemEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
			{ return entry.ItemDefinition == Resource; });
		if (!ItemEntry || ItemEntry->Count < 10)
		{
			PlayerController->ClientSendMessage(UKismetTextLibrary::Conv_StringToText(L"Not enough resources to build! Change building material or gather more!"), nullptr);
			PlayerController->ClientTriggerUIFeedbackEvent(FName(L"BuildPreviewUnableToAfford"));
			return;
			//return callOG(PlayerController, Stack.CurrentNativeFunction, ServerCreateBuildingActor, CreateBuildingData);
		}
	}

	TArray<ABuildingSMActor*> RemoveBuildings;
	char _Unk_OutVar1;
	static auto CantBuild = (__int64 (*)(UWorld*, UObject*, FVector, FRotator, bool, TArray<ABuildingSMActor*> *, char*))(Sarah::Offsets::ImageBase + 0x8E21800);
	CantBuild(UWorld::GetWorld(), BuildingClass, CreateBuildingData.BuildLoc, CreateBuildingData.BuildRot, CreateBuildingData.bMirrored, &RemoveBuildings, &_Unk_OutVar1);
	//if (CantBuild(UWorld::GetWorld(), BuildingClass, CreateBuildingData.BuildLoc, CreateBuildingData.BuildRot, CreateBuildingData.bMirrored, &RemoveBuildings, &_Unk_OutVar1))
	/* {
		PlayerController->ClientSendMessage(UKismetTextLibrary::Conv_StringToText(L"Building in this location is restricted!"), nullptr);
		PlayerController->ClientTriggerUIFeedbackEvent(FName(L"BuildPreviewUnableToPlace"));
		return;
		//return callOG(PlayerController, Stack.CurrentNativeFunction, ServerCreateBuildingActor, CreateBuildingData);
	}*/
	//	return callOG(PlayerController, L"/Script/FortniteGame.FortPlayerController", ServerCreateBuildingActor, CreateBuildingData);


	for (auto& RemoveBuilding : RemoveBuildings)
		RemoveBuilding->K2_DestroyActor();
	RemoveBuildings.Free();

	printf("Heil\n");


	ABuildingSMActor* Building = Utils::SpawnActor<ABuildingSMActor>(BuildingClass, CreateBuildingData.BuildLoc, CreateBuildingData.BuildRot, PlayerController);
	if (!Building)
	{
		return;
		//return callOG(PlayerController, Stack.CurrentNativeFunction, ServerCreateBuildingActor, CreateBuildingData);
	}

	Building->CurrentBuildingLevel = CreateBuildingData.BuildingClassData.UpgradeLevel;
	Building->OnRep_CurrentBuildingLevel();

	Building->SetMirrored(CreateBuildingData.bMirrored);

	Building->bPlayerPlaced = true;

	Building->InitializeKismetSpawnedBuildingActor(Building, PlayerController, true, nullptr, true);


	if (!PlayerController->bBuildFree)
	{
		ItemEntry->Count -= 10;
		if (ItemEntry->Count <= 0)
			Inventory::Remove(PlayerController, ItemEntry->ItemGuid);
		else
			Inventory::ReplaceEntry((AFortPlayerControllerAthena*)PlayerController, *ItemEntry);
	}

	Building->TeamIndex = ((AFortPlayerStateAthena*)PlayerController->PlayerState)->TeamIndex;
	Building->Team = EFortTeam(Building->TeamIndex);


	//return OG(PlayerController, CreateBuildingData);
	return callOG(PlayerController, L"/Script/FortniteGame.FortPlayerController", ServerCreateBuildingActor, CreateBuildingData);
}


void SetEditingPlayer(ABuildingSMActor* _this, AFortPlayerStateZone* NewEditingPlayer)
{
	auto& _LastEditInstigatorPC = *(TWeakObjectPtr<AController>*)(__int64(_this) + offsetof(ABuildingSMActor, EditingPlayer) + sizeof(void*));
	if (_this->Role == ENetRole::ROLE_Authority && (!_this->EditingPlayer || !NewEditingPlayer))
	{
		_this->SetNetDormancy(ENetDormancy(2 - (NewEditingPlayer != 0)));
		_this->ForceNetUpdate();
		auto EditingPlayer = _this->EditingPlayer;
		if (EditingPlayer)
		{
			auto Handle = EditingPlayer->Owner;
			if (Handle)
			{
				if (auto PlayerController = Handle->Cast<AFortPlayerController>())
				{
					_LastEditInstigatorPC.ObjectIndex = PlayerController->Index;
					_LastEditInstigatorPC.ObjectSerialNumber = UObject::GObjects->GetItemByIndex(PlayerController->Index)->SerialNumber;
					_this->EditingPlayer = NewEditingPlayer;
					return;
				}
			}
		}
		else
		{
			if (!NewEditingPlayer)
			{
				_this->EditingPlayer = NewEditingPlayer;
				return;
			}

			auto Handle = NewEditingPlayer->Owner;
			if (auto PlayerController = Handle->Cast<AFortPlayerController>())
			{
				_LastEditInstigatorPC.ObjectIndex = PlayerController->Index;
				_LastEditInstigatorPC.ObjectSerialNumber = UObject::GObjects->GetItemByIndex(PlayerController->Index)->SerialNumber;
				_this->EditingPlayer = NewEditingPlayer;
			}
		}
	}
}



void Building::ServerBeginEditingBuildingActor(UObject* Context, FFrame& Stack)
{
	ABuildingSMActor* Building;
	Stack.StepCompiledIn(&Building);
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerController*)Context;
	if (!PlayerController || !PlayerController->MyFortPawn || !Building || Building->TeamIndex != static_cast<AFortPlayerStateAthena*>(PlayerController->PlayerState)->TeamIndex)
		return;

	AFortPlayerStateAthena* PlayerState = (AFortPlayerStateAthena*)PlayerController->PlayerState;
	if (!PlayerState)
		return;

	SetEditingPlayer(Building, PlayerState);

	auto EditTool = PlayerController->MyFortPawn->CurrentWeapon->Cast<AFortWeap_EditingTool>();
	auto EditToolEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry) {
		return entry.ItemDefinition->IsA<UFortEditToolItemDefinition>();
		});

	PlayerController->MyFortPawn->EquipWeaponDefinition((UFortWeaponItemDefinition*)EditToolEntry->ItemDefinition, EditToolEntry->ItemGuid, EditToolEntry->TrackerGuid, false);

	if (auto EditTool = PlayerController->MyFortPawn->CurrentWeapon->Cast<AFortWeap_EditingTool>())
	{
		EditTool->EditActor = Building;
		EditTool->OnRep_EditActor();
	}

	//PlayerController->ServerExecuteInventoryItem(EditToolEntry->ItemGuid);
}

void Building::ServerEditBuildingActor(UObject* Context, FFrame& Stack)
{
	ABuildingSMActor* Building;
	TSubclassOf<ABuildingSMActor> NewClass;
	uint8 RotationIterations;
	bool bMirrored;
	Stack.StepCompiledIn(&Building);
	Stack.StepCompiledIn(&NewClass);
	Stack.StepCompiledIn(&RotationIterations);
	Stack.StepCompiledIn(&bMirrored);
	Stack.IncrementCode();
	
	auto PlayerController = (AFortPlayerController*)Context;
	if (!PlayerController || !Building || !NewClass || !Building->IsA<ABuildingSMActor>() || !CanBePlacedByPlayer(NewClass) || Building->EditingPlayer != PlayerController->PlayerState || Building->bDestroyed)
		return;

	SetEditingPlayer(Building, nullptr);

	auto a = (bool(*)(ABuildingActor*, int)) Building->VTable[0x1d3];
	static auto ReplaceBuildingActor = (ABuildingSMActor * (*)(ABuildingSMActor*, unsigned int, UObject*, unsigned int, int, bool, AFortPlayerController*))(Sarah::Offsets::ImageBase + 0x889BD24);

	//ABuildingSMActor* NewBuild = ReplaceBuildingActor(Building, 1, NewClass, Building->CurrentBuildingLevel, RotationIterations, bMirrored, PlayerController);
	Building->ForceNetUpdate();
	auto Loc = Building->K2_GetActorLocation();
	auto Rot = Building->K2_GetActorRotation();
	auto CalcRotIterationAroundCentroid = (void(*)(ABuildingActor*, int, FVector*, FRotator*, FVector*, FRotator*)) (Sarah::Offsets::ImageBase + 0x880E480);
	FVector OutLoc;
	FRotator OutRot;
	CalcRotIterationAroundCentroid(Building, RotationIterations, &Loc, &Rot, &OutLoc, &OutRot);
	//Rot.Yaw += RotationIterations * 90.f;
	auto Transform = FTransform(OutLoc, OutRot);
	auto NewBuild = /*Utils::SpawnActorUnfinished<ABuildingSMActor>(NewClass, OutLoc, OutRot)*/ (ABuildingSMActor*) ABuildingActor::K2_SpawnBuildingActor(PlayerController, NewClass, Transform, nullptr, nullptr, true, false);
	NewBuild->CurrentBuildingLevel = Building->CurrentBuildingLevel;
	NewBuild->SetMirrored(bMirrored);
	NewBuild->SetTeam(Building->TeamIndex);
	auto InitializeBuildingActor = (void(*)(ABuildingSMActor *,
		EFortBuildingInitializationReason Reason,
		__int16 InOwnerPersistentID,
		ABuildingActor * BuildingOwner,
		const ABuildingActor * ReplacedBuilding,
		bool bForcePlayBuildUpAnim)) NewBuild->VTable[0x143];
	InitializeBuildingActor(NewBuild, EFortBuildingInitializationReason::Replaced, ((AFortPlayerState*)PlayerController->PlayerState)->WorldPlayerId, nullptr, Building, true);
	NewBuild->BuildingReplacementType = EBuildingReplacementType::BRT_Edited;
	auto& _LastEditInstigatorPC = *(TWeakObjectPtr<AController>*)(__int64(Building) + offsetof(ABuildingSMActor, EditingPlayer) + sizeof(void*));
	auto& New_LastEditInstigatorPC = *(TWeakObjectPtr<AController>*)(__int64(NewBuild) + offsetof(ABuildingSMActor, EditingPlayer) + sizeof(void*));
	
	New_LastEditInstigatorPC = _LastEditInstigatorPC;
	for (auto& BuildingAttachment : Building->AttachedBuildingActors)
	{
		auto Bwo = Utils::GetInterface<IFortAttachToActorInterface>(Building);
		auto Wow = (void(*)(IInterface*, ABuildingActor*, bool, bool, void*)) Bwo->VTable[7];
		Wow(Bwo, BuildingAttachment, false, false, nullptr);
		NewBuild->AttachBuildingActorToMe(BuildingAttachment, false);
	}
	Building->AttachedBuildingActors.ResetNum();
	Building->bAutoReleaseCurieContainerOnDestroyed = false;
	Building->ReplacementDestructionReason = EBuildingReplacementType::BRT_Edited;
	for (auto& Invocation : Building->OnReplacementDestruction.InvocationList)
	{
		Invocation.Object->ProcessEvent(Invocation.Object->Class->FindFunction(Invocation.FunctionName), &Building->ReplacementDestructionReason);
	}
	Building->SilentDie(true);
	auto PostInitializeSpawnedBuildingActor = (void(*)(ABuildingSMActor*,
		EFortBuildingInitializationReason Reason)) NewBuild->VTable[0x144];
	PostInitializeSpawnedBuildingActor(NewBuild, EFortBuildingInitializationReason::Replaced);
	Utils::FinishSpawnActor(NewBuild, OutLoc, OutRot);
	if (NewBuild)
		NewBuild->bPlayerPlaced = true;
}

void Building::ServerEndEditingBuildingActor(UObject* Context, FFrame& Stack)
{
	ABuildingSMActor* Building;
	Stack.StepCompiledIn(&Building);
	Stack.IncrementCode();

	auto PlayerController = (AFortPlayerController*)Context;
	if (!PlayerController || !PlayerController->MyFortPawn || !Building || Building->EditingPlayer != (AFortPlayerStateZone*)PlayerController->PlayerState || Building->TeamIndex != static_cast<AFortPlayerStateAthena*>(PlayerController->PlayerState)->TeamIndex || Building->bDestroyed)
		return;

	SetEditingPlayer(Building, nullptr);

	auto EditToolEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry) {
		return entry.ItemDefinition->IsA<UFortEditToolItemDefinition>();
		});

	PlayerController->MyFortPawn->EquipWeaponDefinition((UFortWeaponItemDefinition*)EditToolEntry->ItemDefinition, EditToolEntry->ItemGuid, EditToolEntry->TrackerGuid, false);

	if (auto EditTool = PlayerController->MyFortPawn->CurrentWeapon->Cast<AFortWeap_EditingTool>())
	{
		EditTool->EditActor = nullptr;
		EditTool->OnRep_EditActor();
	}
}

void Building::ServerRepairBuildingActor(UObject* Context, FFrame& Stack)
{
	ABuildingSMActor* Building;
	Stack.StepCompiledIn(&Building);
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerController*)Context;
	if (!PlayerController)
		return;

	auto Price = (int32)std::floor((10.f * (1.f - Building->GetHealthPercent())) * 0.75f);
	auto res = UFortKismetLibrary::K2_GetResourceItemDefinition(Building->ResourceType);
	auto itemEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([res](FFortItemEntry& entry) {
		return entry.ItemDefinition == res;
		});

	itemEntry->Count -= Price;
	if (itemEntry->Count <= 0)
		Inventory::Remove(PlayerController, itemEntry->ItemGuid);
	else
		Inventory::ReplaceEntry(PlayerController, *itemEntry);

	Building->RepairBuilding(PlayerController, Price);

	if (auto ControllerAthena = PlayerController->Cast<AFortPlayerControllerAthena>()) ControllerAthena->BuildingsRepaired++;
}

void Building::OnDamageServer(ABuildingSMActor* Actor, float Damage, FGameplayTagContainer DamageTags, FVector Momentum, FHitResult HitInfo, AFortPlayerControllerAthena* InstigatedBy, AActor* DamageCauser, FGameplayEffectContextHandle EffectContext) {
	auto GameState = ((AFortGameStateAthena*)UWorld::GetWorld()->GameState);
	if (!InstigatedBy || Actor->bPlayerPlaced || Actor->GetHealth() == 1 || Actor->IsA(UObject::FindClassFast("B_Athena_VendingMachine_C")) || Actor->IsA(GameState->MapInfo->LlamaClass)) return OnDamageServerOG(Actor, Damage, DamageTags, Momentum, HitInfo, InstigatedBy, DamageCauser, EffectContext);
	if (!DamageCauser || !DamageCauser->IsA<AFortWeapon>() || !((AFortWeapon*)DamageCauser)->WeaponData->IsA<UFortWeaponMeleeItemDefinition>()) return OnDamageServerOG(Actor, Damage, DamageTags, Momentum, HitInfo, InstigatedBy, DamageCauser, EffectContext);

	static auto PickaxeTag = FName(L"Weapon.Melee.Impact.Pickaxe");
	auto entry = DamageTags.GameplayTags.Search([](FGameplayTag& entry) {
		return entry.TagName.ComparisonIndex == PickaxeTag.ComparisonIndex;
		});
	if (!entry)
		return OnDamageServerOG(Actor, Damage, DamageTags, Momentum, HitInfo, InstigatedBy, DamageCauser, EffectContext);

	auto Resource = UFortKismetLibrary::K2_GetResourceItemDefinition(Actor->ResourceType);
	if (!Resource)
		return OnDamageServerOG(Actor, Damage, DamageTags, Momentum, HitInfo, InstigatedBy, DamageCauser, EffectContext);
	auto MaxMat = (int32)Utils::EvaluateScalableFloat(Resource->MaxStackSize);

	//FCurveTableRowHandle& BuildingResourceAmountOverride = Actor->BuildingResourceAmountOverride;
	int ResCount = (int)round(UKismetMathLibrary::RandomIntegerInRange(11, 33) / (Actor->GetMaxHealth() / Damage));

	/*if (Actor->BuildingResourceAmountOverride.CurveTable && Actor->BuildingResourceAmountOverride.RowName.ComparisonIndex > 0)
	{
		float Out;
		UDataTableFunctionLibrary::EvaluateCurveTableRow(Actor->BuildingResourceAmountOverride.CurveTable, Actor->BuildingResourceAmountOverride.RowName, 0.f, nullptr, &Out, FString());

		float RC = Out / (Actor->GetMaxHealth() / Damage);

		ResCount = (int)round(RC);
	}*/

	if (ResCount > 0)
	{
		auto itemEntry = InstigatedBy->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry) {
			return entry.ItemDefinition == Resource;
			});

		if (itemEntry)
		{
			itemEntry->Count += ResCount;
			if (itemEntry->Count > MaxMat)
			{
				Inventory::SpawnPickup(InstigatedBy->Pawn->K2_GetActorLocation(), (UFortItemDefinition*) itemEntry->ItemDefinition, itemEntry->Count - MaxMat, 0, EFortPickupSourceTypeFlag::Tossed, EFortPickupSpawnSource::Unset, InstigatedBy->MyFortPawn);
				itemEntry->Count = MaxMat;
			}

			Inventory::ReplaceEntry(InstigatedBy, *itemEntry);
		}
		else
		{
			if (ResCount > MaxMat)
			{
				Inventory::SpawnPickup(InstigatedBy->Pawn->K2_GetActorLocation(), Resource, ResCount - MaxMat, 0, EFortPickupSourceTypeFlag::Tossed, EFortPickupSpawnSource::Unset, InstigatedBy->MyFortPawn);
				ResCount = MaxMat;
			}

			Inventory::GiveItem(InstigatedBy, Resource, ResCount, 0, 0, false);
		}
	}

	InstigatedBy->ClientReportDamagedResourceBuilding(Actor, ResCount == 0 ? EFortResourceType::None : Actor->ResourceType, ResCount, false, Damage == 100.f);
	return OnDamageServerOG(Actor, Damage, DamageTags, Momentum, HitInfo, InstigatedBy, DamageCauser, EffectContext);
}


void Building::ServerSpawnDeco(UObject* Context, FFrame& Stack) {
	FVector Location;
	FRotator Rotation;
	ABuildingSMActor* AttachedActor;
	EBuildingAttachmentType InBuildingAttachmentType;
	Stack.StepCompiledIn(&Location);
	Stack.StepCompiledIn(&Rotation);
	Stack.StepCompiledIn(&AttachedActor);
	Stack.StepCompiledIn(&InBuildingAttachmentType);
	Stack.IncrementCode();

	auto DecoTool = (AFortDecoTool*)Context;
	auto ContextTrap = DecoTool->Cast<AFortDecoTool_ContextTrap>();
	auto ItemDefinition = (UFortDecoItemDefinition*)DecoTool->ItemDefinition;

	if (auto ContextTrapTool = DecoTool->Cast<AFortDecoTool_ContextTrap>()) {
		switch ((int)InBuildingAttachmentType) {
		case 0:
		case 6:
			ItemDefinition = ContextTrapTool->ContextTrapItemDefinition->FloorTrap;
			break;
		case 7:
		case 2:
			ItemDefinition = ContextTrapTool->ContextTrapItemDefinition->CeilingTrap;
			break;
		case 1:
			ItemDefinition = ContextTrapTool->ContextTrapItemDefinition->WallTrap;
			break;
		case 8:
			ItemDefinition = ContextTrapTool->ContextTrapItemDefinition->StairTrap;
			break;
		}
	}

	auto NewTrap = Utils::SpawnActorUnfinished<ABuildingActor>(ItemDefinition->BlueprintClass.Get(), Location, Rotation, AttachedActor);
	AttachedActor->AttachBuildingActorToMe(NewTrap, true);
	AttachedActor->bHiddenDueToTrapPlacement = ItemDefinition->bReplacesBuildingWhenPlaced;
	if (ItemDefinition->bReplacesBuildingWhenPlaced) AttachedActor->bActorEnableCollision = false;
	AttachedActor->ForceNetUpdate();

	auto Pawn = (APawn*)DecoTool->Owner;
	if (!Pawn)
		return;
	auto PlayerController = (AFortPlayerControllerAthena*)Pawn->Controller;
	if (!PlayerController)
		return;

	auto Resource = UFortKismetLibrary::GetDefaultObj()->K2_GetResourceItemDefinition(AttachedActor->ResourceType);
	auto itemEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry) {
		return entry.ItemDefinition == DecoTool->ItemDefinition;
		});
	if (!itemEntry)
		return;

	itemEntry->Count--;
	if (itemEntry->Count <= 0)
		Inventory::Remove(PlayerController, itemEntry->ItemGuid);
	else
		Inventory::ReplaceEntry(PlayerController, *itemEntry);

	if (NewTrap->TeamIndex != ((AFortPlayerStateAthena*)PlayerController->PlayerState)->TeamIndex)
	{
		NewTrap->TeamIndex = ((AFortPlayerStateAthena*)PlayerController->PlayerState)->TeamIndex;
		NewTrap->Team = EFortTeam(NewTrap->TeamIndex);
	}

	Utils::FinishSpawnActor(NewTrap, Location, Rotation);
}

EFortBuildingType GetBuildingTypeFromBuildingAttachmentType(EBuildingAttachmentType BuildingAttachmentType)
{
	if (uint8(BuildingAttachmentType) <= 7)
	{
		LONG Val = 0xC5;
		if (BitTest(&Val, uint8(BuildingAttachmentType)))
			return EFortBuildingType::Floor;
	}
	if (BuildingAttachmentType == EBuildingAttachmentType::ATTACH_Wall)
		return EFortBuildingType::Wall;
	return EFortBuildingType::None;
}

AFortPlayerController* DecoGetPlayerController(AFortDecoTool* Tool)
{
	auto Instigator = Tool->Instigator;
	if (!Instigator)
		return nullptr;

	auto Pawn = Instigator->Cast<AFortPlayerPawn>();
	if (!Pawn)
		return 0;

	if (auto PlayerController = Pawn->Controller->Cast<AFortPlayerController>())
		return PlayerController;

	auto VehicleActor = Pawn->CurrentVehicle;
	if (!VehicleActor)
		return nullptr;

	auto VehicleInterface = Utils::GetInterface<IFortVehicleInterface>(VehicleActor);

	return VehicleInterface->GetVehicleController(Pawn);
}



void Building::ServerCreateBuildingAndSpawnDeco(UObject* Context, FFrame& Stack) {
	auto Tool = (AFortDecoTool*)Context;
	auto Pawn = (APawn*)Tool->Owner;
	if (!Pawn) return;
	auto PlayerController = (AFortPlayerControllerAthena*)Pawn->Controller;
	if (!PlayerController) return;
	FVector_NetQuantize10 BuildingLocation;
	FRotator BuildingRotation;
	FVector_NetQuantize10 Location;
	FRotator Rotation;
	EBuildingAttachmentType InBuildingAttachmentType;
	bool bSpawnDecoOnExtraPiece;
	FVector BuildingExtraPieceLocation;
	Stack.StepCompiledIn(&BuildingLocation);
	Stack.StepCompiledIn(&BuildingRotation);
	Stack.StepCompiledIn(&Location);
	Stack.StepCompiledIn(&Rotation);
	Stack.StepCompiledIn(&InBuildingAttachmentType);
	Stack.StepCompiledIn(&bSpawnDecoOnExtraPiece);
	Stack.StepCompiledIn(&BuildingExtraPieceLocation);
	Stack.IncrementCode();

	auto ItemDefinition = (UFortDecoItemDefinition*)Tool->ItemDefinition;


	if (auto ContextTrapTool = Tool->Cast<AFortDecoTool_ContextTrap>()) {
		switch ((int)InBuildingAttachmentType) {
		case 0:
		case 6:
			ItemDefinition = ContextTrapTool->ContextTrapItemDefinition->FloorTrap;
			break;
		case 7:
		case 2:
			ItemDefinition = ContextTrapTool->ContextTrapItemDefinition->CeilingTrap;
			break;
		case 1:
			ItemDefinition = ContextTrapTool->ContextTrapItemDefinition->WallTrap;
			break;
		case 8:
			ItemDefinition = ContextTrapTool->ContextTrapItemDefinition->StairTrap;
			break;
		}
	}

	TArray<UBuildingEditModeMetadata*> AutoCreateAttachmentBuildingShapes;
	for (auto& AutoCreateAttachmentBuildingShape : ItemDefinition->AutoCreateAttachmentBuildingShapes)
	{
		AutoCreateAttachmentBuildingShapes.Add(AutoCreateAttachmentBuildingShape);
	}

	auto bIgnoreCanAffordCheck = UFortKismetLibrary::DoesItemDefinitionHaveGameplayTag(ItemDefinition, FGameplayTag(FName(L"Trap.ExtraPiece.Cost.Ignore")));
	TSubclassOf<ABuildingSMActor> Ret;
	auto FindAvailableBuildingClassForBuildingTypeAndEditModePattern = (TSubclassOf<ABuildingSMActor>*(*)(AFortPlayerController*, TSubclassOf<ABuildingSMActor>*, EFortBuildingType, TArray<UBuildingEditModeMetadata*>*, bool, EFortResourceType)) (Sarah::Offsets::ImageBase + 0x71ffa08);
	FindAvailableBuildingClassForBuildingTypeAndEditModePattern(PlayerController, &Ret, GetBuildingTypeFromBuildingAttachmentType(InBuildingAttachmentType), &AutoCreateAttachmentBuildingShapes, bIgnoreCanAffordCheck, ItemDefinition->AutoCreateAttachmentBuildingResourceType);

	auto Build = Ret.Get();

	ABuildingSMActor* Building = nullptr;
	TArray<ABuildingSMActor*> RemoveBuildings;
	char _Unk_OutVar1;
	static auto CantBuild = (__int64 (*)(UWorld*, UObject*, FVector, FRotator, bool, TArray<ABuildingSMActor*> *, char*))(Sarah::Offsets::ImageBase + 0xA0D0A24);
	if (CantBuild(UWorld::GetWorld(), Build, BuildingLocation, BuildingRotation, false, &RemoveBuildings, &_Unk_OutVar1)) return;

	auto Resource = UFortKismetLibrary::GetDefaultObj()->K2_GetResourceItemDefinition(((ABuildingSMActor*)Build->DefaultObject)->ResourceType);
	auto itemEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry) {
		return entry.ItemDefinition == Resource;
		});
	if (!itemEntry) return;

	itemEntry->Count -= 10;
	if (itemEntry->Count <= 0) Inventory::Remove(PlayerController, itemEntry->ItemGuid);
	Inventory::ReplaceEntry((AFortPlayerControllerAthena*)PlayerController, *itemEntry);

	for (auto& RemoveBuilding : RemoveBuildings)
		RemoveBuilding->K2_DestroyActor();
	RemoveBuildings.Free();

	Building = Utils::SpawnActorUnfinished<ABuildingSMActor>(Build, BuildingLocation, BuildingRotation, PlayerController);
	Building->bPlayerPlaced = true;
	Building->TeamIndex = ((AFortPlayerStateAthena*)PlayerController->PlayerState)->TeamIndex;
	Building->Team = EFortTeam(Building->TeamIndex);
	Building->InitializeKismetSpawnedBuildingActor(Building, PlayerController, true, nullptr, true);
	Tool->ServerSpawnDeco(Location, Rotation, Building, InBuildingAttachmentType);
}

void Building::Hook() {
	Utils::ExecHook(L"/Script/FortniteGame.FortPlayerController.ServerCreateBuildingActor", ServerCreateBuildingActor, ServerCreateBuildingActorOG);
	Utils::ExecHook(L"/Script/FortniteGame.FortPlayerController.ServerBeginEditingBuildingActor", ServerBeginEditingBuildingActor);
	Utils::ExecHook(L"/Script/FortniteGame.FortPlayerController.ServerEditBuildingActor", ServerEditBuildingActor);
	Utils::ExecHook(L"/Script/FortniteGame.FortPlayerController.ServerEndEditingBuildingActor", ServerEndEditingBuildingActor);
	Utils::ExecHook(L"/Script/FortniteGame.FortPlayerController.ServerRepairBuildingActor", ServerRepairBuildingActor);
	Utils::Hook(Sarah::Offsets::ImageBase + 0x8819168, OnDamageServer, OnDamageServerOG);
	Utils::ExecHook(L"/Script/FortniteGame.FortDecoTool.ServerSpawnDeco", ServerSpawnDeco, ServerSpawnDecoOG);
	Utils::ExecHook(L"/Script/FortniteGame.FortDecoTool.ServerCreateBuildingAndSpawnDeco", ServerCreateBuildingAndSpawnDeco);
	Utils::ExecHook(L"/Script/FortniteGame.FortDecoTool_ContextTrap.ServerSpawnDeco_Implementation", ServerSpawnDeco, ServerSpawnDecoOG);
	Utils::ExecHook(L"/Script/FortniteGame.FortDecoTool_ContextTrap.ServerCreateBuildingAndSpawnDeco", ServerCreateBuildingAndSpawnDeco);
}