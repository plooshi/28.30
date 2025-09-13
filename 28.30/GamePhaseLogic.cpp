#include "pch.h"
#include "GamePhaseLogic.h"
#include "Misc.h"

void SetGamePhase(UObject* WorldContextObject, EAthenaGamePhase GamePhase)
{
	auto GamePhaseLogic = UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(WorldContextObject);
	auto OldGamePhase = GamePhaseLogic->GamePhase;
	GamePhaseLogic->GamePhase = GamePhase;
	GamePhaseLogic->OnRep_GamePhase(OldGamePhase);
}

void SetGamePhaseStep(UObject* WorldContextObject, EAthenaGamePhaseStep GamePhaseStep)
{
	auto GamePhaseLogic = UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(WorldContextObject);
	GamePhaseLogic->GamePhaseStep = GamePhaseStep;
	GamePhaseLogic->HandleGamePhaseStepChanged(GamePhaseStep);
}

void SpawnAircrafts(AFortGameModeAthena* GameMode)
{
	auto GameState = (AFortGameStateAthena*)GameMode->GameState;
	auto Playlist = GameState->CurrentPlaylistInfo.BasePlaylist;
	auto GamePhaseLogic = UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(GameMode);

	int NumAircraftsToSpawn = 1;
	if (Playlist->AirCraftBehavior == EAirCraftBehavior::OpposingAirCraftForEachTeam)
		NumAircraftsToSpawn = GameState->TeamCount;

	TArray<AFortAthenaAircraft*> Aircrafts;
	for (int i = 0; i < NumAircraftsToSpawn; i++)
		Aircrafts.Add(AFortAthenaAircraft::SpawnAircraft(UWorld::GetWorld(), GameState->MapInfo->AircraftClass, GameState->MapInfo->FlightInfos[i]));

	GamePhaseLogic->SetAircrafts(Aircrafts);
	GamePhaseLogic->OnRep_Aircrafts();
}

void GamePhaseLogic::HandleMatchHasStarted(AFortGameModeAthena* GameMode)
{
	HandleMatchHasStartedOG(GameMode);

	auto Time = (float)UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());
	auto WarmupDuration = 120.f;

	auto GamePhaseLogic = UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(GameMode);

	GamePhaseLogic->WarmupCountdownStartTime = Time;
	GamePhaseLogic->WarmupCountdownEndTime = Time + WarmupDuration;
	GamePhaseLogic->WarmupCountdownDuration = WarmupDuration;
	GamePhaseLogic->WarmupEarlyCountdownDuration = 0.f;
	auto GameState = (AFortGameStateAthena*)GameMode->GameState;

	TArray<AFortAthenaAircraft*> Aircrafts;
	Aircrafts.Add(AFortAthenaAircraft::SpawnAircraft(UWorld::GetWorld(), GameState->MapInfo->AircraftClass, GameState->MapInfo->FlightInfos[0]));
	GamePhaseLogic->SetAircrafts(Aircrafts);
	GamePhaseLogic->OnRep_Aircrafts();
	SetGamePhase(GameMode, EAthenaGamePhase::Warmup);
	SetGamePhaseStep(GameMode, EAthenaGamePhaseStep::Warmup);
}


AFortSafeZoneIndicator* SetupSafeZoneIndicator(AFortGameModeAthena* GameMode)
{
	// thanks heliato
	auto GamePhaseLogic = UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(GameMode);
	auto GameState = (AFortGameStateAthena*)GameMode->GameState;

	if (!GamePhaseLogic->SafeZoneIndicator)
	{
		FTransform Transform = { FVector(), {} };
		FActorSpawnParameters addr{};

		addr.NameMode = ESpawnActorNameMode::Required_Fatal;
		addr.ObjectFlags = EObjectFlags::Transactional;
		addr.bNoFail = true;
		addr.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AFortSafeZoneIndicator* SafeZoneIndicator = ((AFortSafeZoneIndicator * (*)(UWorld*, UClass*, FTransform const*, FActorSpawnParameters*))(Sarah::Offsets::ImageBase + 0x1999190))(UWorld::GetWorld(), GamePhaseLogic->SafeZoneIndicatorClass, &Transform, &addr);

		if (SafeZoneIndicator)
		{
			FFortSafeZoneDefinition& SafeZoneDefinition = GameState->MapInfo->SafeZoneDefinition;
			float SafeZoneCount = Utils::EvaluateScalableFloat(SafeZoneDefinition.Count, 0);

			if (SafeZoneIndicator->SafeZonePhases.IsValid())
				SafeZoneIndicator->SafeZonePhases.Free();

			const float Time = (float)UGameplayStatics::GetTimeSeconds(GameState);

			for (float i = 0; i < SafeZoneCount; i++)
			{
				FFortSafeZonePhaseInfo PhaseInfo{};
				PhaseInfo.Radius = Utils::EvaluateScalableFloat(SafeZoneDefinition.Radius, i);
				PhaseInfo.WaitTime = Utils::EvaluateScalableFloat(SafeZoneDefinition.WaitTime, i);
				PhaseInfo.ShrinkTime = Utils::EvaluateScalableFloat(SafeZoneDefinition.ShrinkTime, i);
				PhaseInfo.PlayerCap = (int)Utils::EvaluateScalableFloat(SafeZoneDefinition.PlayerCapSolo, i);

				UDataTableFunctionLibrary::EvaluateCurveTableRow(GameState->AthenaGameDataTable, FName(L"Default.SafeZone.Damage"), i, nullptr, &PhaseInfo.DamageInfo.Damage, FString());
				if (i == 0.f)
					PhaseInfo.DamageInfo.Damage = 0.01f;
				PhaseInfo.DamageInfo.bPercentageBasedDamage = true;
				PhaseInfo.TimeBetweenStormCapDamage = Utils::EvaluateScalableFloat(GamePhaseLogic->TimeBetweenStormCapDamage, i);
				PhaseInfo.StormCapDamagePerTick = Utils::EvaluateScalableFloat(GamePhaseLogic->StormCapDamagePerTick, i);
				PhaseInfo.StormCampingIncrementTimeAfterDelay = Utils::EvaluateScalableFloat(GamePhaseLogic->StormCampingIncrementTimeAfterDelay, i);
				PhaseInfo.StormCampingInitialDelayTime = Utils::EvaluateScalableFloat(GamePhaseLogic->StormCampingInitialDelayTime, i);
				PhaseInfo.MegaStormGridCellThickness = (int)Utils::EvaluateScalableFloat(SafeZoneDefinition.MegaStormGridCellThickness, i);
				PhaseInfo.UsePOIStormCenter = false;

				PhaseInfo.Center = /*GamePhaseLogic->SafeZoneLocations[(int)i]*/ { 0, 0, 0 };

				SafeZoneIndicator->SafeZonePhases.Add(PhaseInfo);

				SafeZoneIndicator->PhaseCount++;
			}

			SafeZoneIndicator->OnRep_PhaseCount();

			SafeZoneIndicator->SafeZoneStartShrinkTime = Time + SafeZoneIndicator->SafeZonePhases[0].WaitTime;
			SafeZoneIndicator->SafeZoneFinishShrinkTime = SafeZoneIndicator->SafeZoneStartShrinkTime + SafeZoneIndicator->SafeZonePhases[0].ShrinkTime;

			SafeZoneIndicator->CurrentPhase = 0;
			SafeZoneIndicator->OnRep_CurrentPhase();
		}


		GamePhaseLogic->SafeZoneIndicator = SafeZoneIndicator;
		GamePhaseLogic->OnRep_SafeZoneIndicator();
		SafeZoneIndicator->ForceNetUpdate();
	}

	return GamePhaseLogic->SafeZoneIndicator;
}

void StartNewSafeZonePhase(AFortGameModeAthena* GameMode)
{
	// thanks heliato
	auto GamePhaseLogic = UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(GameMode);
	AFortGameStateAthena* GameState = (AFortGameStateAthena*)GameMode->GameState;

	AFortSafeZoneIndicator* SafeZoneIndicator = GamePhaseLogic->SafeZoneIndicator;

	int32 CurrentPhase = SafeZoneIndicator->CurrentPhase;

	const float TimeSeconds = (float)UGameplayStatics::GetTimeSeconds(GameState);
	const float ServerWorldTimeSeconds = GameState->ServerWorldTimeSecondsDelta + TimeSeconds;

	/*if (bLateGame && GamePhaseLogic->SafeZoneIndicator->CurrentPhase < 5)
	{
		CurrentPhase++;

		FFortSafeZonePhaseInfo& PhaseInfo = SafeZoneIndicator->SafeZonePhases[CurrentPhase];

		SafeZoneIndicator->PreviousCenter = FVector_NetQuantize100(PhaseInfo.Center);
		SafeZoneIndicator->PreviousRadius = PhaseInfo.Radius;

		SafeZoneIndicator->NextCenter = FVector_NetQuantize100(PhaseInfo.Center);
		SafeZoneIndicator->NextRadius = PhaseInfo.Radius;
		SafeZoneIndicator->NextMegaStormGridCellThickness = PhaseInfo.MegaStormGridCellThickness;

		FFortSafeZonePhaseInfo& NextPhaseInfo = SafeZoneIndicator->SafeZonePhases[CurrentPhase + 1];

		SafeZoneIndicator->NextNextCenter = FVector_NetQuantize100(NextPhaseInfo.Center);
		SafeZoneIndicator->NextNextRadius = NextPhaseInfo.Radius;
		SafeZoneIndicator->NextNextMegaStormGridCellThickness = NextPhaseInfo.MegaStormGridCellThickness;

		GameMode->SafeZoneIndicator->SafeZoneStartShrinkTime = ServerWorldTimeSeconds;
		GameMode->SafeZoneIndicator->SafeZoneFinishShrinkTime = GameMode->SafeZoneIndicator->SafeZoneStartShrinkTime + 0.05f;

		SafeZoneIndicator->CurrentDamageInfo = PhaseInfo.DamageInfo;
		SafeZoneIndicator->OnRep_CurrentDamageInfo();

		SafeZoneIndicator->CurrentPhase = CurrentPhase;
		SafeZoneIndicator->OnRep_CurrentPhase();

	}
	else */if (SafeZoneIndicator->SafeZonePhases.IsValidIndex(CurrentPhase + 1))
	{
		FFortSafeZonePhaseInfo& PreviousPhaseInfo = SafeZoneIndicator->SafeZonePhases[CurrentPhase];

		SafeZoneIndicator->PreviousCenter = FVector_NetQuantize100(PreviousPhaseInfo.Center);
		SafeZoneIndicator->PreviousRadius = PreviousPhaseInfo.Radius;

		CurrentPhase++;

		FFortSafeZonePhaseInfo& PhaseInfo = SafeZoneIndicator->SafeZonePhases[CurrentPhase];

		SafeZoneIndicator->NextCenter = FVector_NetQuantize100(PhaseInfo.Center);
		SafeZoneIndicator->NextRadius = PhaseInfo.Radius;
		SafeZoneIndicator->NextMegaStormGridCellThickness = PhaseInfo.MegaStormGridCellThickness;

		if (SafeZoneIndicator->SafeZonePhases.IsValidIndex(CurrentPhase + 1))
		{
			FFortSafeZonePhaseInfo& NextPhaseInfo = SafeZoneIndicator->SafeZonePhases[CurrentPhase + 1];

			SafeZoneIndicator->NextNextCenter = FVector_NetQuantize100(NextPhaseInfo.Center);
			SafeZoneIndicator->NextNextRadius = NextPhaseInfo.Radius;
			SafeZoneIndicator->NextNextMegaStormGridCellThickness = NextPhaseInfo.MegaStormGridCellThickness;
		}

		SafeZoneIndicator->SafeZoneStartShrinkTime = ServerWorldTimeSeconds + PhaseInfo.WaitTime;
		SafeZoneIndicator->SafeZoneFinishShrinkTime = SafeZoneIndicator->SafeZoneStartShrinkTime + PhaseInfo.ShrinkTime;

		SafeZoneIndicator->CurrentDamageInfo = PhaseInfo.DamageInfo;
		SafeZoneIndicator->OnRep_CurrentDamageInfo();

		SafeZoneIndicator->CurrentPhase = CurrentPhase;
		SafeZoneIndicator->OnRep_CurrentPhase();

		SetGamePhaseStep(UWorld::GetWorld(), EAthenaGamePhaseStep::StormHolding);
	}

	//return StartNewSafeZonePhaseOG(GameMode, a2);
}

class UFunction* FindFunctionByFName(UClass *Class, FName FuncName)
{
	for (const UStruct* Clss = Class; Clss; Clss = Clss->Super)
	{
		for (UField* Field = Clss->Children; Field; Field = Field->Next)
		{
			if (Field->HasTypeFlag(EClassCastFlags::Function) && Field->Name == FuncName)
				return static_cast<class UFunction*>(Field);
		}
	}

	return nullptr;
}

void GamePhaseLogic::Tick()
{
	auto Time = UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());
	auto GamePhaseLogic = UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(UWorld::GetWorld());

	static bool gettingReady = false;
	if (!gettingReady)
	{
		if (((AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode)->AlivePlayers.Num() > 0 && GamePhaseLogic->WarmupCountdownEndTime - 10.f <= Time)
		{
			gettingReady = true;

			SetGamePhaseStep(UWorld::GetWorld(), EAthenaGamePhaseStep::GetReady);
		}
	}

	static bool startedBus = false;
	if (!startedBus)
	{
		if (((AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode)->AlivePlayers.Num() > 0 && GamePhaseLogic->WarmupCountdownEndTime <= Time)
		{
			startedBus = true;

			auto Aircraft = GamePhaseLogic->Aircrafts_GameState[0].Get();
			auto GameState = (AFortGameStateAthena*)UWorld::GetWorld()->GameState;
			auto& FlightInfo = GameState->MapInfo->FlightInfos[0];
			Aircraft->FlightElapsedTime = 0;
			Aircraft->DropStartTime = (float)Time + FlightInfo.TimeTillDropStart;
			Aircraft->DropEndTime = (float)Time + FlightInfo.TimeTillDropEnd;
			Aircraft->FlightStartTime = (float)Time;
			Aircraft->FlightEndTime = (float)Time + FlightInfo.TimeTillFlightEnd;
			Aircraft->ReplicatedFlightTimestamp = (float)Time;
			GamePhaseLogic->bAircraftIsLocked = true;
			for (auto& Player : ((AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode)->AlivePlayers)
			{
				auto Pawn = (AFortPlayerPawnAthena*)Player->Pawn;
				if (Pawn)
				{
					if (Pawn->Role == ENetRole::ROLE_Authority)
					{
						if (Pawn->bIsInAnyStorm)
						{
							Pawn->bIsInAnyStorm = false;
							Pawn->OnRep_IsInAnyStorm();
						}
					}
					Pawn->bIsInsideSafeZone = true;
					Pawn->OnRep_IsInsideSafeZone();
					for (auto& ScriptDelegate : Pawn->OnEnteredAircraft.InvocationList)
					{
						auto Object = ScriptDelegate.Object;
						Object->ProcessEvent(FindFunctionByFName(Object->Class, ScriptDelegate.FunctionName), nullptr);
					}
				}
				Player->ClientActivateSlot(EFortQuickBars::Primary, 0, 0.f, true, true);
				if (Pawn)
					Pawn->K2_DestroyActor();
				auto Reset = (void (*)(APlayerController*)) (Sarah::Offsets::ImageBase + 0x663C1B4);
				Reset(Player);
				Player->ClientGotoState(FName(L"Spectating"));
			}
			SetGamePhase(UWorld::GetWorld(), EAthenaGamePhase::Aircraft);
			SetGamePhaseStep(UWorld::GetWorld(), EAthenaGamePhaseStep::BusLocked);
		}
	}

	static bool busUnlocked = false;
	if (!busUnlocked)
	{
		if (((AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode)->AlivePlayers.Num() > 0 && GamePhaseLogic->Aircrafts_GameState[0]->DropStartTime != -1 && GamePhaseLogic->Aircrafts_GameState[0]->DropStartTime <= Time)
		{
			busUnlocked = true;
			GamePhaseLogic->bAircraftIsLocked = false;
			SetGamePhaseStep(UWorld::GetWorld(), EAthenaGamePhaseStep::BusFlying);
			
		}
	}

	static bool startedZones = false;
	if (!startedZones)
	{
		if (((AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode)->AlivePlayers.Num() > 0 && GamePhaseLogic->Aircrafts_GameState[0]->DropEndTime != -1 && GamePhaseLogic->Aircrafts_GameState[0]->DropEndTime <= Time)
		{
			startedZones = true;
			auto GameState = (AFortGameStateAthena*)UWorld::GetWorld()->GameState;
			auto GameMode = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;

			for (auto& Player : GameMode->AlivePlayers)
			{
				if (Player->IsInAircraft())
				{
					printf("test3.sys\n");
					Player->GetAircraftComponent()->ServerAttemptAircraftJump({});
				}
			}

			/*if (bLateGame)
			{
				GameState->GamePhase = EAthenaGamePhase::SafeZones;
				GameState->GamePhaseStep = EAthenaGamePhaseStep::StormHolding;
				GameState->OnRep_GamePhase(EAthenaGamePhase::Aircraft);
			}*/
			GamePhaseLogic->SafeZonesStartTime = (float)Time + 60.f;
			SetGamePhase(GameState, EAthenaGamePhase::SafeZones);
			SetGamePhaseStep(UWorld::GetWorld(), EAthenaGamePhaseStep::StormForming);
		}
	}

	static bool deletedBus = false;
	if (!deletedBus)
	{
		if (((AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode)->AlivePlayers.Num() > 0 && GamePhaseLogic->Aircrafts_GameState[0]->FlightEndTime != -1 && GamePhaseLogic->Aircrafts_GameState[0]->FlightEndTime <= Time)
		{
			deletedBus = true;
			auto Aircraft = GamePhaseLogic->Aircrafts_GameState[0].Get();
			Aircraft->K2_DestroyActor();
			GamePhaseLogic->Aircrafts_GameState.Clear();
			GamePhaseLogic->Aircrafts_GameMode.Clear();
		}
	}

	auto GameMode = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;
	static bool formedZone = false;
	if (!formedZone)
	{
		if (((AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode)->AlivePlayers.Num() > 0 && GamePhaseLogic->SafeZonesStartTime != -1 && GamePhaseLogic->SafeZonesStartTime <= Time)
		{
			formedZone = true;
			printf("form\n");
			auto SafeZoneIndicator = SetupSafeZoneIndicator((AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode);
			StartNewSafeZonePhase((AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode);
		}
	}
	else if (formedZone && UGameplayStatics::GetTimeSeconds(GameMode) >= GamePhaseLogic->SafeZoneIndicator->SafeZoneFinishShrinkTime)
		StartNewSafeZonePhase(GameMode);
}

void GamePhaseLogic::OnAircraftExitedDropZone(UObject* Context, FFrame& Stack)
{
	AFortAthenaAircraft* Aircraft;
	Stack.StepCompiledIn(&Aircraft);
	Stack.IncrementCode();
	auto GamePhaseLogic = (UFortGameStateComponent_BattleRoyaleGamePhaseLogic*)Context;
	auto GameState = (AFortGameStateAthena*)UWorld::GetWorld()->GameState;
	auto GameMode = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;
	for (auto& Player : GameMode->AlivePlayers)
	{
		if (Player->IsInAircraft())
		{
			Player->GetAircraftComponent()->ServerAttemptAircraftJump({});
		}
	}

	/*if (bLateGame)
	{
		GameState->GamePhase = EAthenaGamePhase::SafeZones;
		GameState->GamePhaseStep = EAthenaGamePhaseStep::StormHolding;
		GameState->OnRep_GamePhase(EAthenaGamePhase::Aircraft);
	}*/
	SetGamePhase(GameState, EAthenaGamePhase::SafeZones);

	callOG(GamePhaseLogic, L"/Script/FortniteGame.FortGameStateComponent_BattleRoyaleGamePhaseLogic", OnAircraftExitedDropZone, Aircraft);
}


void GamePhaseLogic::Hook()
{
	Utils::Hook(Sarah::Offsets::ImageBase + 0x848B01C, HandleMatchHasStarted, HandleMatchHasStartedOG);
	//Utils::ExecHook(L"/Script/FortniteGame.FortGameStateComponent_BattleRoyaleGamePhaseLogic.OnAircraftExitedDropZone", OnAircraftExitedDropZone, OnAircraftExitedDropZoneOG);
}