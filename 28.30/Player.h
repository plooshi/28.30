#pragma once
#include "pch.h"
#include "Utils.h"


class Player
{
private:
	DefUHookOg(ServerLoadingScreenDropped);
	DefUHookOg(ServerAcknowledgePossession);
public:
	static void GetPlayerViewPointInternal(APlayerController*, FVector&, FRotator&);
private:
	static void GetPlayerViewPoint(UObject*, FFrame&);
	static void ServerExecuteInventoryItem(UObject*, FFrame&);
	static void ServerReturnToMainMenu(UObject*, FFrame&);
	static void ServerPlayEmoteItem(UObject*, FFrame&);
	DefUHookOg(ServerAttemptAircraftJump);
	DefUHookOg(ServerSendZiplineState);
	DefUHookOg(ServerHandlePickupInfo);
	static void MovingEmoteStopped(UObject*, FFrame&);
public:
	static void InternalPickup(AFortPlayerControllerAthena*, FFortItemEntry);
private:
	DefHookOg(bool, CompletePickupAnimation, AFortPickup*);
	DefHookOg(void, NetMulticast_Athena_BatchedDamageCues, AFortPlayerPawnAthena*, FAthenaBatchedDamageGameplayCues_Shared, FAthenaBatchedDamageGameplayCues_NonShared);
	static void ReloadWeapon(AFortWeapon*, int);
	DefHookOg(void, ClientOnPawnDied, AFortPlayerControllerAthena*, FFortPlayerDeathReport&);
	static void ServerAttemptInventoryDrop(UObject*, FFrame&);
	DefHookOg(void, ServerCheat, AFortPlayerControllerAthena*, FString&);
	DefUHookOg(OnCapsuleBeginOverlap);

	DefHookOg(void, ServerAttemptInteract, UFortControllerComponent_Interaction*, AFortPawn*, float, AActor*, UPrimitiveComponent*, ETInteractionType, UObject*, EInteractionBeingAttempted);


	static void ServerAddMapMarker(UObject*, FFrame&);
	static void ServerRemoveMapMarker(UObject*, FFrame&);

	DefHookOg(void, ServerReadyToStartMatch, AFortPlayerControllerAthena* Controller);
	DefUHookOg(TeleportPlayerForPlotLoadComplete);
	DefUHookOg(TeleportPlayer);
	static void ServerTeleportToPlaygroundLobbyIsland(AFortPlayerControllerAthena*, FFrame&);
	static void TeleportPlayerToLinkedVolume(AFortAthenaCreativePortal* Portal, FFrame& Frame);
	DefHookOg(void, MakeNewCreativePlot, AFortPlayerControllerAthena* thisPtr, UFortCreativeRealEstatePlotItemDefinition* PlotType, const FString& Locale, const FString& Title);

	DefHookOg(void, OnPlayImpactFX, AFortWeapon*, FHitResult&, EPhysicalSurface, UFXSystemComponent*);
	static void ServerRestartPlayer(APlayerController*);


	InitHooks;
};