#include "pch.h"
#include "GameMode.h"
#include "Misc.h"
#include "Abilities.h"
#include "Inventory.h"
#include "Looting.h"
#include "Vehicles.h"
//#include "Player.h"

void SetPlaylist(AFortGameModeAthena* GameMode, UFortPlaylistAthena* Playlist) {
	auto GamePhaseLogic = UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(GameMode);
	auto GameState = (AFortGameStateAthena*)GameMode->GameState;

	GameState->CurrentPlaylistInfo.BasePlaylist = Playlist;
	GameState->CurrentPlaylistInfo.OverridePlaylist = Playlist;
	GameState->CurrentPlaylistInfo.BasePlaylist->GarbageCollectionFrequency = 9999999999999999.f;
	GameState->CurrentPlaylistInfo.PlaylistReplicationKey++;
	GameState->CurrentPlaylistInfo.MarkArrayDirty();

	GameState->CurrentPlaylistId = GameMode->CurrentPlaylistId = Playlist->PlaylistId;
	GameMode->CurrentPlaylistName = Playlist->PlaylistName;
	GameState->OnRep_CurrentPlaylistInfo();
	GameState->OnRep_CurrentPlaylistId();

	GameMode->GameSession->MaxPlayers = Playlist->MaxPlayers;

	GamePhaseLogic->AirCraftBehavior = Playlist->AirCraftBehavior;
	GameState->WorldLevel = Playlist->LootLevel;
	//GamePhaseLogic->CachedSafeZoneStartUp = Playlist->SafeZoneStartUp;

	//GamePhaseLogic->bAlwaysDBNO = Playlist->MaxSquadSize > 1;
	//GamePhaseLogic->bDBNOEnabled = Playlist->MaxSquadSize > 1;
	GameMode->bAlwaysDBNO = false;
	GameMode->bDBNOEnabled = false;
	//GamePhaseLogic->AISettings = Playlist->AISettings;
	//if (GamePhaseLogic->AISettings)
	//	GamePhaseLogic->AISettings->AIServices[1] = UAthenaAIServicePlayerBots::StaticClass();
}

bool bReady = false;
bool GameMode::ReadyToStartMatch(UObject* Context, FFrame& Stack, bool* Ret) {
	static auto Playlist = Utils::FindObject<UFortPlaylistAthena>(L"/Game/Athena/Playlists/Playlist_DefaultSolo.Playlist_DefaultSolo");
	Stack.IncrementCode();
	auto GameMode = Context->Cast<AFortGameModeAthena>();
	if (!GameMode)
		return *Ret = callOGWithRet(((AGameMode*)Context), L"/Script/Engine.GameMode", ReadyToStartMatch);
	auto GameState = ((AFortGameStateAthena*)GameMode->GameState);

	if (GameMode->CurrentPlaylistId == -1) {
		GameMode->WarmupRequiredPlayerCount = 1;
		SetPlaylist(GameMode, Playlist);

		GameState->bIsUsingDownloadOnDemand = false;
		for (auto& Level : Playlist->AdditionalLevels)
		{
			bool Success = false;
			std::cout << "Level: " << Level.Get()->Name.ToString() << std::endl;
			ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(UWorld::GetWorld(), Level, FVector(), FRotator(), &Success, FString(), nullptr, false);
			FAdditionalLevelStreamed level{};
			level.bIsServerOnly = false;
			level.LevelName = Level.ObjectID.AssetPath.AssetName;
			if (Success) GameState->AdditionalPlaylistLevelsStreamed.Add(level);
		}
		for (auto& Level : Playlist->AdditionalLevelsServerOnly)
		{
			bool Success = false;
			std::cout << "Level: " << Level.Get()->Name.ToString() << std::endl;
			ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(UWorld::GetWorld(), Level, FVector(), FRotator(), &Success, FString(), nullptr, false);
			FAdditionalLevelStreamed level{};
			level.bIsServerOnly = true;
			level.LevelName = Level.ObjectID.AssetPath.AssetName;
			if (Success) GameState->AdditionalPlaylistLevelsStreamed.Add(level);
		}
		GameState->OnRep_AdditionalPlaylistLevelsStreamed();

		GameState->AddComponentByClass(UFortAthenaLivingWorldManager::StaticClass(), true, { FVector {}, {} }, false);


		/*GamePhaseLogic->AIDirector = Utils::SpawnActor<AAthenaAIDirector>({});
		GamePhaseLogic->AIDirector->Activate();

		GamePhaseLogic->AIGoalManager = Utils::SpawnActor<AFortAIGoalManager>({});*/


		return *Ret = false;
	}

	if (!GameMode->bWorldIsReady)
	{
		auto Starts = Utils::GetAll<AFortPlayerStartWarmup>();
		auto StartsNum = Starts.Num();
		Starts.Free();
		if (StartsNum == 0 || !GameState->MapInfo)
			return *Ret = false;

		void (*LoadBuiltInGameFeaturePlugins)(UFortGameFeaturePluginManager*, bool) = decltype(LoadBuiltInGameFeaturePlugins)(Sarah::Offsets::ImageBase + 0x21FC550);
		UFortGameFeaturePluginManager* PluginManager = nullptr;
		for (int i = 0; i < UObject::GObjects->Num(); i++)
		{
			auto Object = UObject::GObjects->GetByIndex(i);
			if (Object && !Object->IsDefaultObject() && Object->Class == UFortGameFeaturePluginManager::StaticClass())
			{
				PluginManager = (UFortGameFeaturePluginManager*)Object;
				break;
			}
		}

		LoadBuiltInGameFeaturePlugins(PluginManager, true);


		/*FAircraftFlightInfo FlightInfo;
		FlightInfo.FlightStartLocation = FVector_NetQuantize100{ 0, 0, Utils::EvaluateScalableFloat(GameState->MapInfo->AircraftHeight) };
		FlightInfo.FlightStartRotation = FRotator{};
		FlightInfo.FlightSpeed = Utils::EvaluateScalableFloat(GameState->MapInfo->AircraftSpeed);
		FlightInfo.TimeTillFlightEnd = 35;
		FlightInfo.TimeTillDropEnd = 30;
		FlightInfo.TimeTillDropStart = 4;
		GameState->MapInfo->FlightInfos.Add(FlightInfo);*/
		auto InitializeFlightPath = (void(*)(AFortAthenaMapInfo*, AFortGameStateAthena*, UFortGameStateComponent_BattleRoyaleGamePhaseLogic*, bool, double, float, float)) (Sarah::Offsets::ImageBase + 0x83E9DAC);
		InitializeFlightPath(GameState->MapInfo, GameState, UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(GameState), false, 0.f, 0.f, 360.f);
		UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(GameState)->SafeZonesStartTime = -1;

		GameMode->bDisableGCOnServerDuringMatch = true;
		GameMode->bPlaylistHotfixChangedGCDisabling = true;

		GameMode->DefaultPawnClass = Utils::FindObject<UClass>(L"/Game/Athena/PlayerPawn_Athena.PlayerPawn_Athena_C");
		//Misc::Listen();

		GameState->DefaultParachuteDeployTraceForGroundDistance = 10000;

		AbilitySets.Add(Utils::FindObject<UFortAbilitySet>(L"/Game/Abilities/Player/Generic/Traits/DefaultPlayer/GAS_AthenaPlayer.GAS_AthenaPlayer"));
		//GamePhaseLogic->DefaultPawnClass = Utils::FindObject<UClass>(L"/Game/Athena/PlayerPawn_Athena.PlayerPawn_Athena_C");


		auto AddToTierData = [&](UDataTable* Table, UEAllocatedVector<FFortLootTierData*>& TempArr) {
			Table->AddToRoot();
			if (auto CompositeTable = Table->Cast<UCompositeDataTable>())
				for (auto& ParentTable : CompositeTable->ParentTables)
					for (auto& [Key, Val] : (TMap<FName, FFortLootTierData*>) ParentTable->RowMap) {
						TempArr.push_back(Val);
					}

			for (auto& [Key, Val] : (TMap<FName, FFortLootTierData*>) Table->RowMap) {
				TempArr.push_back(Val);
			}
			};

		auto AddToPackages = [&](UDataTable* Table, UEAllocatedVector<FFortLootPackageData*>& TempArr) {
			Table->AddToRoot();
			if (auto CompositeTable = Table->Cast<UCompositeDataTable>())
				for (auto& ParentTable : CompositeTable->ParentTables)
					for (auto& [Key, Val] : (TMap<FName, FFortLootPackageData*>) ParentTable->RowMap) {
						TempArr.push_back(Val);
					}

			for (auto& [Key, Val] : (TMap<FName, FFortLootPackageData*>) Table->RowMap) {
				TempArr.push_back(Val);
			}
			};


		UEAllocatedVector<FFortLootTierData*> LootTierDataTempArr;
		auto LootTierData = Playlist->LootTierData.Get();
		if (!LootTierData)
			LootTierData = Utils::FindObject<UDataTable>(L"/Game/Items/Datatables/AthenaLootTierData_Client.AthenaLootTierData_Client");
		if (LootTierData)
			AddToTierData(LootTierData, LootTierDataTempArr);
		for (auto& Val : LootTierDataTempArr)
		{
			Looting::TierDataAllGroups.Add(Val);
		}

		UEAllocatedVector<FFortLootPackageData*> LootPackageTempArr;
		auto LootPackages = Playlist->LootPackages.Get();
		if (!LootPackages) LootPackages = Utils::FindObject<UDataTable>(L"/Game/Items/Datatables/AthenaLootPackages_Client.AthenaLootPackages_Client");
		if (LootPackages)
			AddToPackages(LootPackages, LootPackageTempArr);
		for (auto& Val : LootPackageTempArr)
		{
			Looting::LPGroupsAll.Add(Val);
		}

		for (int i = 0; i < UObject::GObjects->Num(); i++)
		{
			auto Object = UObject::GObjects->GetByIndex(i);

			if (!Object || !Object->Class || Object->IsDefaultObject())
				continue;

			if (auto GameFeatureData = Object->Cast<UFortGameFeatureData>())
			{
				auto LootTableData = GameFeatureData->DefaultLootTableData;
				auto LTDFeatureData = LootTableData.LootTierData.Get();
				auto AbilitySet = GameFeatureData->PlayerAbilitySet.Get();
				auto LootPackageData = LootTableData.LootPackageData.Get();
				if (AbilitySet) {
					if (AbilitySet->GetName() == "GAS_Juno_SharedBy_AllMinifigs" || AbilitySet->GetName() == "GAS_JamIsland_Defaults" || AbilitySet->GetName() == "GAS_PilgrimQuickplay_Defaults" || AbilitySet->GetName() == "GAS_Athena_PilgrimCore" || AbilitySet->GetName() == "AbilitySet_24Movement")
						continue;
					printf("Ability set: %s\n", UKismetSystemLibrary::GetPathName(AbilitySet).ToString().c_str());
					AbilitySet->AddToRoot();
					AbilitySets.Add(AbilitySet);
				}
				if (LTDFeatureData) {
					UEAllocatedVector<FFortLootTierData*> LTDTempData;

					AddToTierData(LTDFeatureData, LTDTempData);

					for (auto& Tag : Playlist->GameplayTagContainer.GameplayTags)
						for (auto& Override : GameFeatureData->PlaylistOverrideLootTableData)
							if (Tag.TagName == Override.First.TagName)
								AddToTierData(Override.Second.LootTierData.Get(), LTDTempData);

					for (auto& Val : LTDTempData)
					{
						Looting::TierDataAllGroups.Add(Val);
					}
				}
				if (LootPackageData) {
					UEAllocatedVector<FFortLootPackageData*> LPTempData;


					AddToPackages(LootPackageData, LPTempData);

					for (auto& Tag : Playlist->GameplayTagContainer.GameplayTags)
						for (auto& Override : GameFeatureData->PlaylistOverrideLootTableData)
							if (Tag.TagName == Override.First.TagName)
								AddToPackages(Override.Second.LootPackageData.Get(), LPTempData);

					for (auto& Val : LPTempData)
					{
						Looting::LPGroupsAll.Add(Val);
					}
				}
			}
		}

		Looting::SpawnFloorLootForContainer(Utils::FindObject<UBlueprintGeneratedClass>(L"/Game/Athena/Environments/Blueprints/Tiered_Athena_FloorLoot_Warmup.Tiered_Athena_FloorLoot_Warmup_C"));
		Looting::SpawnFloorLootForContainer(Utils::FindObject<UBlueprintGeneratedClass>(L"/Game/Athena/Environments/Blueprints/Tiered_Athena_FloorLoot_01.Tiered_Athena_FloorLoot_01_C"));
		
		Vehicles::SpawnVehicles();
		
		UCurveTable* AthenaGameDataTable = GameState->AthenaGameDataTable; // this is js playlist gamedata or default gamedata if playlist doesn't have one

		if (AthenaGameDataTable)
		{
			static FName DefaultSafeZoneDamageName = FName(L"Default.SafeZone.Damage");

			for (const auto& [RowName, RowPtr] : ((UDataTable*)AthenaGameDataTable)->RowMap) // same offset
			{
				if (RowName != DefaultSafeZoneDamageName)
					continue;

				FSimpleCurve* Row = (FSimpleCurve*)RowPtr;

				if (!Row)
					continue;

				for (auto& Key : Row->Keys)
				{
					FSimpleCurveKey* KeyPtr = &Key;

					if (KeyPtr->Time == 0.f)
					{
						KeyPtr->Value = 0.f;
					}
				}

				Row->Keys.Add(FSimpleCurveKey(1.f, 0.01f), 1);
			}
		}

		/*auto DataLayers = UWorld::GetWorld()->PersistentLevel->WorldDataLayers;
		auto SetDataLayerRuntimeState = (void (*)(AWorldDataLayers*, UDataLayerInstance*, EDataLayerRuntimeState, bool)) (Sarah::Offsets::ImageBase + 0x81ED4C0);
		auto LobbyName = FName(L"DataLayer_8DD02A1E4DB7293E83D7C297C548D1F5");
		for (auto& Instance_Uncasted : DataLayers->DataLayerInstances)
		{
			auto Instance = (UDataLayerInstanceWithAsset*)Instance_Uncasted;
			if (Instance->GetName() == "DataLayer_B69182544DFBA9C542022A8018B884F0")
			{
				SetDataLayerRuntimeState(DataLayers, Instance, EDataLayerRuntimeState::Activated, true);
			}
			printf("DLInstance: %s\n", Instance->GetName().c_str());
			printf("DLInstance_Name: %s\n", Instance->DataLayerAsset->GetName().c_str());
		}*/
		Misc::Listen();

		SetConsoleTitleA("Sarah 28.30: Ready");
		GameMode->bWorldIsReady = true;

		return *Ret = false;
	}
	return *Ret = GameMode->AlivePlayers.Num() > 0;
}

APawn* GameMode::SpawnDefaultPawnFor(UObject* Context, FFrame& Stack, APawn** Ret) {
	AController* NewPlayer;
	AActor* StartSpot;
	Stack.StepCompiledIn(&NewPlayer);
	Stack.StepCompiledIn(&StartSpot);
	Stack.IncrementCode();
	auto GameMode = (AFortGameModeAthena*)Context;
	auto PlayerController = NewPlayer->Cast<AFortPlayerController>();
	if (!PlayerController) return *Ret = nullptr;
	auto Transform = StartSpot->GetTransform();
	auto Pawn = GameMode->SpawnDefaultPawnAtTransform(NewPlayer, Transform);


	static bool IsFirstPlayer = false;

	if (!IsFirstPlayer)
	{

		IsFirstPlayer = true;
	}


	auto Num = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Num();
	if (Num != 0)
	{
		PlayerController->WorldInventory->Inventory.ReplicatedEntries.ResetNum();
		PlayerController->WorldInventory->Inventory.ItemInstances.ResetNum();
		Inventory::TriggerInventoryUpdate(PlayerController, nullptr);
	}
	Inventory::GiveItem(PlayerController, PlayerController->CosmeticLoadoutPC.Pickaxe->WeaponDefinition);
	for (auto& StartingItem : ((AFortGameModeAthena*)GameMode)->StartingItems)
	{
		if (StartingItem.Count && !StartingItem.Item->IsA<UFortSmartBuildingItemDefinition>())
		{
			Inventory::GiveItem(PlayerController, (UFortItemDefinition*)StartingItem.Item, StartingItem.Count);
		}
	}

	if (Num == 0)
	{
		auto PlayerState = (AFortPlayerStateAthena*)PlayerController->PlayerState;

		static UFortAbilitySet* NormalSet = Utils::FindObject<UFortAbilitySet>(L"/Game/Abilities/Player/Generic/Traits/DefaultPlayer/GAS_AthenaPlayer.GAS_AthenaPlayer");
static UFortAbilitySet* tacticalsprint = Utils::FindObject<UFortAbilitySet>(L"/TacticalSprintGame/Gameplay/AS_TacticalSprint.AS_TacticalSprint");
static UFortAbilitySet* zipsfix = Utils::FindObject<UFortAbilitySet>(L"/Ascender/Gameplay/Ascender/AS_Ascender.AS_Ascender");
Abilities::GiveAbilitySet(PlayerState->AbilitySystemComponent, NormalSet);
Abilities::GiveAbilitySet(PlayerState->AbilitySystemComponent, tacticalsprint);
Abilities::GiveAbilitySet(PlayerState->AbilitySystemComponent, zipsfix);
		

		PlayerState->HeroType = PlayerController->CosmeticLoadoutPC.Character->HeroDefinition;
		((void (*)(APlayerState*, APawn*)) Sarah::Offsets::ApplyCharacterCustomization)(PlayerController->PlayerState, Pawn);

		auto AthenaController = (AFortPlayerControllerAthena*)PlayerController;
		PlayerState->SeasonLevelUIDisplay = AthenaController->XPComponent->CurrentLevel;
		PlayerState->OnRep_SeasonLevelUIDisplay();
		AthenaController->XPComponent->bRegisteredWithQuestManager = true;
		AthenaController->XPComponent->OnRep_bRegisteredWithQuestManager();

		AthenaController->GetQuestManager(ESubGame::Athena)->InitializeQuestAbilities(Pawn);
	}
	/*else if (bLateGame)
	{
		auto Shotgun = Lategame::GetShotguns();
		auto AssaultRifle = Lategame::GetAssaultRifles();
		auto Util = Lategame::GetUtilities();
		auto Heal = Lategame::GetHeals();
		auto HealSlot2 = Lategame::GetHeals();

		int ShotgunClipSize = Inventory::GetStats((UFortWeaponItemDefinition*)Shotgun.Item)->ClipSize;
		int AssaultRifleClipSize = Inventory::GetStats((UFortWeaponItemDefinition*)AssaultRifle.Item)->ClipSize;
		int UtilClipSize = Util.Item->IsA<UFortWeaponItemDefinition>() ? Inventory::GetStats((UFortWeaponItemDefinition*)Util.Item)->ClipSize : 3;
		int HealClipSize = Heal.Item->IsA<UFortWeaponItemDefinition>() ? Inventory::GetStats((UFortWeaponItemDefinition*)Heal.Item)->ClipSize : 3;
		int HealSlot2ClipSize = HealSlot2.Item->IsA<UFortWeaponItemDefinition>() ? Inventory::GetStats((UFortWeaponItemDefinition*)HealSlot2.Item)->ClipSize : 3;

		Inventory::GiveItem(PlayerController, Lategame::GetResource(EFortResourceType::Wood), 500);
		Inventory::GiveItem(PlayerController, Lategame::GetResource(EFortResourceType::Stone), 500);
		Inventory::GiveItem(PlayerController, Lategame::GetResource(EFortResourceType::Metal), 500);

		Inventory::GiveItem(PlayerController, Lategame::GetAmmo(EAmmoType::Assault), 250);
		Inventory::GiveItem(PlayerController, Lategame::GetAmmo(EAmmoType::Shotgun), 50);
		Inventory::GiveItem(PlayerController, Lategame::GetAmmo(EAmmoType::Submachine), 400);
		Inventory::GiveItem(PlayerController, Lategame::GetAmmo(EAmmoType::Rocket), 6);
		Inventory::GiveItem(PlayerController, Lategame::GetAmmo(EAmmoType::Sniper), 20);

		Inventory::GiveItem(PlayerController, AssaultRifle.Item, 1, AssaultRifleClipSize, true);
		Inventory::GiveItem(PlayerController, Shotgun.Item, 1, ShotgunClipSize, true);
		Inventory::GiveItem(PlayerController, Util.Item, Util.Count, UtilClipSize, true);
		Inventory::GiveItem(PlayerController, Heal.Item, 3, HealClipSize, true);
		Inventory::GiveItem(PlayerController, HealSlot2.Item, 3, HealSlot2ClipSize, true);
	}*/

	//MessageBoxA(nullptr, "tranny", "tranny", MB_OK);
	return *Ret = Pawn;
}

EFortTeam GameMode::PickTeam(AFortGameModeAthena* GameMode, uint8_t PreferredTeam, AFortPlayerControllerAthena* Controller) {
	uint8_t ret = CurrentTeam;

	if (++PlayersOnCurTeam >= ((AFortGameStateAthena*)GameMode->GameState)->CurrentPlaylistInfo.BasePlaylist->MaxSquadSize)
	{
		CurrentTeam++;
		PlayersOnCurTeam = 0;
	}

	return EFortTeam(ret);
}

void GameMode::HandleStartingNewPlayer(UObject* Context, FFrame& Stack) {
	AFortPlayerControllerAthena* NewPlayer;
	Stack.StepCompiledIn(&NewPlayer);
	Stack.IncrementCode();
	auto GameMode = (AFortGameModeAthena*)Context;
	auto GameState = (AFortGameStateAthena*)GameMode->GameState;
	AFortPlayerStateAthena* PlayerState = (AFortPlayerStateAthena*)NewPlayer->PlayerState;

	PlayerState->SquadId = PlayerState->TeamIndex - 3;
	PlayerState->OnRep_SquadId();

	FGameMemberInfo Member;
	Member.MostRecentArrayReplicationKey = -1;
	Member.ReplicationID = -1;
	Member.ReplicationKey = -1;
	Member.TeamIndex = PlayerState->TeamIndex;
	Member.SquadId = PlayerState->SquadId;
	Member.MemberUniqueId = PlayerState->UniqueID;

	GameState->GameMemberInfoArray.Members.Add(Member);
	GameState->GameMemberInfoArray.MarkItemDirty(Member);

	FActorSpawnParameters SpawnParams{};
	SpawnParams.NameMode = ESpawnActorNameMode::Required_Fatal;
	SpawnParams.ObjectFlags = EObjectFlags::Transactional;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.bNoFail = true;

	NewPlayer->WorldInventory = /*Utils::SpawnActor<AFortInventory>(NewPlayer->WorldInventoryClass, FVector{})*/ ((AFortInventory * (*)(UWorld*, UClass*, FTransform const*, FActorSpawnParameters*))(Sarah::Offsets::ImageBase + 0x1999190))(UWorld::GetWorld(), NewPlayer->WorldInventoryClass, new FTransform(FVector(), FRotator()), &SpawnParams);
	NewPlayer->WorldInventory->SetOwner(NewPlayer);


	if (!NewPlayer->MatchReport)
	{
		NewPlayer->MatchReport = reinterpret_cast<UAthenaPlayerMatchReport*>(UGameplayStatics::SpawnObject(UAthenaPlayerMatchReport::StaticClass(), NewPlayer));
	}

	return callOG(GameMode, L"/Script/Engine.GameModeBase", HandleStartingNewPlayer, NewPlayer);
}

void GameMode::Hook()
{
	Utils::ExecHook(L"/Script/Engine.GameMode.ReadyToStartMatch", ReadyToStartMatch, ReadyToStartMatchOG);
	Utils::ExecHook(L"/Script/Engine.GameModeBase.SpawnDefaultPawnFor", SpawnDefaultPawnFor);
	//Utils::Hook(Sarah::Offsets::PickTeam, PickTeam, PickTeamOG);
	Utils::ExecHook(L"/Script/Engine.GameModeBase.HandleStartingNewPlayer", HandleStartingNewPlayer, HandleStartingNewPlayerOG);
}
