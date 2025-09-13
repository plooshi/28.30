#include "pch.h"
#include "Misc.h"
#include "GamePhaseLogic.h"
#include "Replication.h"
//#include "Replication.h"
//#include "Options.h"

int Misc::GetNetMode()
{
	return 1;
}

float Misc::GetMaxTickRate(UEngine* Engine, float DeltaTime, bool bAllowFrameRateSmoothing) {
	// improper, DS is supposed to do hitching differently
	return /*30.f*/ 120.f;
}

bool Misc::RetTrue() { return true; }

void SendClientAdjustment(APlayerController* PlayerController)
{
	if (PlayerController->AcknowledgedPawn != PlayerController->Pawn && !PlayerController->SpectatorPawn)
		return;

	auto Pawn = (ACharacter*)(PlayerController->Pawn ? PlayerController->Pawn : PlayerController->SpectatorPawn);
	if (Pawn && Pawn->RemoteRole == ENetRole::ROLE_AutonomousProxy)
	{
		auto Interface = Utils::GetInterface<INetworkPredictionInterface>(Pawn->CharacterMovement);

		if (Interface)
		{
			void (*SendClientAdjustment)(INetworkPredictionInterface*) = decltype(SendClientAdjustment)(Sarah::Offsets::ImageBase + 0x62B214C);
			SendClientAdjustment(Interface);
		}
	}
}

void SendClientMoveAdjustments(UNetDriver* Driver)
{
	for (UNetConnection* Connection : Driver->ClientConnections)
	{
		if (Connection == nullptr || Connection->ViewTarget == nullptr)
			continue;

		if (APlayerController* PC = Connection->PlayerController)
			SendClientAdjustment(PC);

		for (UNetConnection* ChildConnection : Connection->Children)
		{
			if (ChildConnection == nullptr)
				continue;

			if (APlayerController* PC = ChildConnection->PlayerController)
				SendClientAdjustment(PC);
		}
	}
}

enum class EReplicationSystemSendPass : unsigned
{
	Invalid,

	// Sending data directly after PostDispatch, this is a partial update only, not updating scope and filtering and will only process RPC/Attachmeents
	PostTickDispatch,

	// Sending data part of TickFlush, this will do a full update and replicate data to all connections
	TickFlush,
};

struct FSendUpdateParams
{
	// Type of SendPass we want to do @see EReplicationSystemSendPass
	EReplicationSystemSendPass SendPass = EReplicationSystemSendPass::TickFlush;

	// DeltaTime, only relevant for EReplicationSystemSendPass::TickFlush
	float DeltaSeconds = 0.f;
};

static bool bStartedBus = false;
void Misc::TickFlush(UNetDriver* Driver, float DeltaTime)
{
	/*auto ReplicationSystem = *(UReplicationSystem**)(__int64(Driver) + 0x748);
	if (ReplicationSystem && Driver->ClientConnections.Num() > 0 && Driver->ClientConnections[0]->InternalAck == false)
	{
		static void(*UpdateReplicationViews)(UNetDriver*) = decltype(UpdateReplicationViews)(Sarah::Offsets::ImageBase + 0x6577FB4);
		static void(*PreSendUpdate)(UReplicationSystem*, FSendUpdateParams&) = decltype(PreSendUpdate)(Sarah::Offsets::ImageBase + 0x58675D8);

		UpdateReplicationViews(Driver);
		SendClientMoveAdjustments(Driver);
		FSendUpdateParams Params;
		Params.DeltaSeconds = DeltaTime;
		PreSendUpdate(ReplicationSystem, Params);
	}*/
	Replication::ServerReplicateActors(Driver, DeltaTime);
	if (GetKeyState(VK_F2)) {
		UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), L"startaircraft", nullptr);
	}

	GamePhaseLogic::Tick();

	return TickFlushOG(Driver, DeltaTime);
}

void* Misc::DispatchRequest(void* Arg1, void* MCPData, int)
{
	return DispatchRequestOG(Arg1, MCPData, 3);
}

void Misc::Listen()
{
	auto World = UWorld::GetWorld();
	auto Engine = UEngine::GetEngine();
	FWorldContext* Context = ((FWorldContext * (*)(UEngine*, UWorld*)) (Sarah::Offsets::ImageBase + 0x12C8A10))(Engine, World);
	auto NetDriverName = FName(L"GameNetDriver");
	auto NetDriver = World->NetDriver = ((UNetDriver * (*)(UEngine*, FWorldContext*, FName, int))Sarah::Offsets::CreateNetDriver)(Engine, Context, NetDriverName, 0);
	//*(bool*)(__int64(NetDriver) + 0x751) = true; // bisusingiris

	for (auto& Collection : World->LevelCollections)
		Collection.NetDriver = NetDriver;

	NetDriver->NetDriverName = NetDriverName;
	NetDriver->World = World;


	((void (*)(UNetDriver*, UWorld*))Sarah::Offsets::SetWorld)(NetDriver, World);
	FString Err;
	FURL URL;
	URL.Port = 7777;
	if (((bool (*)(UNetDriver*, UWorld*, FURL&, bool, FString&))Sarah::Offsets::InitListen)(NetDriver, World, /*World->PersistentLevel->*/URL, false, Err)) {

		((void (*)(UNetDriver*, UWorld*))Sarah::Offsets::SetWorld)(NetDriver, World);
	}
	else
	{
		Log(L"Failed to listen");
	}
}

void Misc::SetDynamicFoundationEnabled(UObject* Context, FFrame& Stack)
{
	auto Foundation = (ABuildingFoundation*)Context;
	bool bEnabled;
	Stack.StepCompiledIn(&bEnabled);
	Stack.IncrementCode();
	printf("foundy: %s\n", Foundation->GetName().c_str());
	Foundation->DynamicFoundationRepData.EnabledState = bEnabled ? EDynamicFoundationEnabledState::Enabled : EDynamicFoundationEnabledState::Disabled;
	Foundation->OnRep_DynamicFoundationRepData();
	Foundation->FoundationEnabledState = bEnabled ? EDynamicFoundationEnabledState::Enabled : EDynamicFoundationEnabledState::Disabled;
}

void Misc::SetDynamicFoundationTransform(UObject* Context, FFrame& Stack)
{
	auto Foundation = (ABuildingFoundation*)Context;
	FTransform Transform;
	Stack.StepCompiledIn(&Transform);
	Stack.IncrementCode();
	printf("foundy: %s\n", Foundation->GetName().c_str());
	Foundation->DynamicFoundationTransform = Transform;
	Foundation->DynamicFoundationRepData.Rotation = Transform.Rotation.Rotator();
	Foundation->DynamicFoundationRepData.Translation = Transform.Translation;
	Foundation->StreamingData.FoundationLocation = Transform.Translation;
	Foundation->StreamingData.BoundingBox = Foundation->StreamingBoundingBox;
	Foundation->OnRep_DynamicFoundationRepData();
}

bool Misc::RetFalse()
{
	return false;
}

void Misc::StartNewSafeZonePhase(AFortGameModeAthena* GameMode, int a2)
{
	// thanks heliato
	auto GamePhaseLogic = UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(GameMode);
	AFortGameStateAthena* GameState = (AFortGameStateAthena*)GameMode->GameState;

	AFortSafeZoneIndicator* SafeZoneIndicator = GamePhaseLogic->SafeZoneIndicator;

	int32 CurrentPhase = SafeZoneIndicator->CurrentPhase;

	const float TimeSeconds = (float)UGameplayStatics::GetTimeSeconds(GameState);
	const float ServerWorldTimeSeconds = GameState->ServerWorldTimeSecondsDelta + TimeSeconds;

	static auto TriggerBuildingGameplayActorSpawning = (void (*)(AFortGameModeAthena*)) (Sarah::Offsets::ImageBase + 0x992cd00);
	TriggerBuildingGameplayActorSpawning(GameMode);

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

	}

	//return StartNewSafeZonePhaseOG(GameMode, a2);
}

uint32 Misc::CheckCheckpointHeartBeat() {
	return -1;
}

void idkf() {
	Log(L"ninja %llx\n", __int64(_ReturnAddress()) - Sarah::Offsets::ImageBase);
}

void (*ProcessEventOG)(UObject*, UFunction*, void*);
void ProcessEvent(UObject* Context, UFunction* Func, void* Params) {
	if (!Context || !Func) return ProcessEventOG(Context, Func, Params);

	printf("[PE] %s\n", Func->GetName().c_str());
	return ProcessEventOG(Context, Func, Params);
}

void (*JewOG)(UWorld* World);
void Jew(UWorld* World)
{
	Log(L"[BlockTillLevelStreamingCompleted] enter");
	JewOG(World);
	Log(L"[BlockTillLevelStreamingCompleted] exit");
}

void (*BeginPlayOG)(AWorldSettings* _Ws);
void BeginPlay(AWorldSettings* _Ws)
{
	Log(L"BEGIN PLAY!!!!");
}


char __fastcall sub_165D01C(uint64_t* a1, __int64 a2, __int64 a3, __int64 a4)
{
	__int64 v8; // rax
	__int64 v9; // r8
	char v10; // bl
	__int64 v12; // [rsp+40h] [rbp+8h] BYREF

	v8 = (*(__int64(__fastcall**)(uint64_t*, __int64))(*a1 + 384LL))(a1, a4);
	v9 = a1[14];
	v12 = v8;
	v10 = a3 ? (*(char(__fastcall**)(__int64, __int64, __int64, __int64*, uint64_t))(*(uint64_t*)a3 + 704LL))(
		a3,
		a2,
		v9,
		&v12,
		0) : false;
	if (a3 && (!v12 || (*(uint64_t*)(v12 + 8) & 0x60000000) == 0))
		(*(void(__fastcall**)(uint64_t*, __int64))(*a1 + 400LL))(a1, a4);
	return v10;
}
auto ImageBase = *(uint64_t*)(__readgsqword(0x60) + 0x10);

char __fastcall sub_315D15C(__int64 a1, __int64 a2, __int64 a3, uint64_t* a4)
{
	char v7; // bp

	if (*(uint32_t*)(a2 + 88) < 0x14u)
		return 0;
	v7 = a3 ? (*(char(__fastcall**)(__int64, __int64, uint64_t))(*(uint64_t*)a3 + 704LL))(a3, a2, *(uint64_t*)(a1 + 112)) : 0;
	if ((*(uint8_t*)(a2 + 40) & 1) != 0)
	{
		uint64_t(*sub_11EF964)(uint64_t, uint64_t) = decltype(sub_11EF964)(Sarah::Offsets::ImageBase + 0x11EF964);
		if (*a4)
			a4[1] = sub_11EF964(*a4, *(uint64_t*)(a1 + 112));
		else
			a4[1] = 0;
	}
	return v7;
}

static bool bStarted = false;
bool Misc::StartAircraftPhase(AFortGameModeAthena* GameMode, char a2)
{
	auto GamePhaseLogic = UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(GameMode);
	auto Ret = StartAircraftPhaseOG(GameMode, a2);

	if (/*bLateGame && */!bStarted)
	{
		bStarted = true;
		auto GameState = (AFortGameStateAthena*)GameMode->GameState;
		GameState->DefaultParachuteDeployTraceForGroundDistance = 1000.f;

		auto Aircraft = GamePhaseLogic->Aircrafts_GameState[0];
		Aircraft->FlightInfo.FlightSpeed = 0.f;
		//FVector Loc = GameMode->SafeZoneLocations[4];
		FVector Loc = {};
		Loc.Z = 17500.f;

		Aircraft->FlightInfo.FlightStartLocation = (FVector_NetQuantize100)Loc;

		Aircraft->FlightInfo.TimeTillFlightEnd = 9;
		Aircraft->FlightInfo.TimeTillDropEnd = 9;
		Aircraft->FlightInfo.TimeTillDropStart = 1;
		Aircraft->DropStartTime = (float)UGameplayStatics::GetTimeSeconds(UWorld::GetWorld()) + 1;
		Aircraft->DropEndTime = (float)UGameplayStatics::GetTimeSeconds(UWorld::GetWorld()) + 9;
		GamePhaseLogic->bAircraftIsLocked = true;
		GamePhaseLogic->SafeZonesStartTime = (float)UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());
	}

	return Ret;
}


void Misc::SpawnInitialSafeZone(AFortGameModeAthena* GameMode)
{
	//return;
	auto GamePhaseLogic = UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(GameMode);
	//GamePhaseLogic->bSafeZoneActive = true;
	//auto SafeZoneIndicator = SetupSafeZoneIndicator(GameMode);
	//StartNewSafeZonePhase(GameMode, 0);
	//return SpawnInitialSafeZoneOG(GameMode);
}

void Misc::UpdateSafeZonesPhase(AFortGameModeAthena* GameMode)
{
	auto GamePhaseLogic = UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(GameMode);
	if (/*GameMode->bSafeZoneActive && */UGameplayStatics::GetTimeSeconds(GameMode) >= GamePhaseLogic->SafeZoneIndicator->SafeZoneFinishShrinkTime /* && !GameMode->bSafeZonePaused*/)
		StartNewSafeZonePhase(GameMode, GamePhaseLogic->SafeZoneIndicator->CurrentPhase + 1);

	return UpdateSafeZonesPhaseOG(GameMode);
}

void Misc::SetDataLayerRuntimeState(AWorldDataLayers* DataLayers, UDataLayerInstance* Instance, EDataLayerRuntimeState State, bool bRecursive)
{
	Log(L"Called");
	if (State == EDataLayerRuntimeState::Unloaded)
	{
		return;
	}

	return SetDataLayerRuntimeStateOG(DataLayers, Instance, State, bRecursive);
}

bool test(AFortSafeZoneIndicator* Indicator, FFortSafeZonePhaseInfo& OutPhaseInfo, int32 InPhaseToGet)
{
	printf("test %d\n", InPhaseToGet);
	return 1;
}

void GetPhaseInfo(UObject* Context, FFrame& Stack, bool* Ret)
{
	auto& OutSafeZonePhase = Stack.StepCompiledInRef<FFortSafeZonePhaseInfo>();
	int32 InPhaseToGet;
	Stack.StepCompiledIn(&InPhaseToGet);
	Stack.IncrementCode();

	auto SafeZoneIndicator = (AFortSafeZoneIndicator*)Context;
	if (SafeZoneIndicator->SafeZonePhases.IsValidIndex(InPhaseToGet))
	{
		OutSafeZonePhase = SafeZoneIndicator->SafeZonePhases[InPhaseToGet];
		*Ret = true;
		return;
	}
	*Ret = false;
}

void Misc::Hook() {
	//Utils::Patch(Sarah::Offsets::ImageBase + 0x622ea4b, 0x12b1fc5);
	//Utils::Hook(Sarah::Offsets::ImageBase + 0x74e0a14, Listen);
	Utils::Hook(Sarah::Offsets::WorldNetMode, GetNetMode);
	Utils::Hook(Sarah::Offsets::GetMaxTickRate, GetMaxTickRate, GetMaxTickRateOG);
	Utils::Hook(Sarah::Offsets::TickFlush, TickFlush, TickFlushOG);
	Utils::Hook(Sarah::Offsets::DispatchRequest, DispatchRequest, DispatchRequestOG);
	/*Utils::ExecHook(L"/Script/FortniteGame.BuildingFoundation.SetDynamicFoundationTransform", SetDynamicFoundationTransform);
	Utils::ExecHook(L"/Script/FortniteGame.BuildingFoundation.SetDynamicFoundationEnabled", SetDynamicFoundationEnabled);
	Utils::Hook(Sarah::Offsets::StartNewSafeZonePhase, StartNewSafeZonePhase, StartNewSafeZonePhaseOG);
	Utils::Patch<uint32_t>(Sarah::Offsets::EncryptionPatch, 0x90909090);
	Utils::Patch<uint16_t>(Sarah::Offsets::EncryptionPatch + 4, 0x9090);
	Utils::Patch<uint32_t>(Sarah::Offsets::GameSessionPatch, 0x90909090);
	Utils::Patch<uint16_t>(Sarah::Offsets::GameSessionPatch + 4, 0x9090);
	Utils::Patch<uint32_t>(Sarah::Offsets::ImageBase + 0x6294d9f, 0x90909090);
	Utils::Patch<uint8_t>(Sarah::Offsets::ImageBase + 0x6294da3, 0x90);
	Utils::Patch<uint32_t>(Sarah::Offsets::ImageBase + 0x8055cb1, 0xeb);
	Utils::Hook(Sarah::Offsets::ImageBase + 0x9929720, StartAircraftPhase, StartAircraftPhaseOG);
	Utils::Patch<uint8_t>(Sarah::Offsets::ImageBase + 0x81DBC1F, 0x85);*/
	Utils::Patch<uint32_t>(Sarah::Offsets::ImageBase + 0x4ABA551, 0x90909090);
	Utils::Patch<uint8_t>(Sarah::Offsets::ImageBase + 0x4ABA555, 0x90);
	//Utils::Patch<uint8_t>(Sarah::Offsets::ImageBase + 0x7fce797, 0xeb)
	for (auto& NullFunc : Sarah::Offsets::NullFuncs)
		Utils::Patch<uint8_t>(NullFunc, 0xc3);
	for (auto& RetTrueFunc : Sarah::Offsets::RetTrueFuncs) {
		Utils::Patch<uint32_t>(RetTrueFunc, 0xc0ffc031);
		Utils::Patch<uint8_t>(RetTrueFunc + 4, 0xc3);
	}
	Utils::Hook(Sarah::Offsets::ImageBase + 0x43DA840, CheckCheckpointHeartBeat);
	//Utils::Hook(Sarah::Offsets::ImageBase + 0x1903AD4, RetFalse);
	//Utils::Hook(Sarah::Offsets::ImageBase + 0x116F858, RetTrue);
	//Utils::Hook(Sarah::Offsets::ImageBase + 0x99291B4, SpawnInitialSafeZone, SpawnInitialSafeZoneOG);
	//Utils::Hook(Sarah::Offsets::ImageBase + 0x992E110, UpdateSafeZonesPhase, UpdateSafeZonesPhaseOG);
	//Utils::Hook(Sarah::Offsets::ImageBase + 0x9A408D0, test);
	//Utils::ExecHook(L"/Script/FortniteGame.FortSafeZoneIndicator.GetPhaseInfo", GetPhaseInfo);
	//Utils::Hook(Sarah::Offsets::ImageBase + 0x9A408D0, RetTrue);
	//Utils::Hook(Sarah::Offsets::ImageBase + 0x165D01C, sub_165D01C); // tbh idfk wtf ts is, its sum serializer tho
	//Utils::Hook(Sarah::Offsets::ImageBase + 0x315D15C, sub_315D15C); // tbh idfk wtf ts is, its sum serializer tho

	//Utils::Hook(Sarah::Offsets::ImageBase + 0x1F02160, idkf);
	//Utils::Patch<uint8_t>(Sarah::Offsets::ImageBase + 0x1903AF2, 0xeb);
	//Utils::Hook(Sarah::Offsets::ImageBase + Offsets::ProcessEvent, ProcessEvent, ProcessEventOG);
}
