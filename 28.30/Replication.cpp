#include "pch.h"
#include "Replication.h"
#include "Misc.h"

float SpatialBias = 320000.f;
float CellSize = 10000.f;
int32_t RepGraphFrame = 0;
ULevel* GetLevel(AActor* Actor) {
	auto Outer = Actor->Outer;

	while (Outer)
	{
		if (Outer->Class == ULevel::StaticClass())
			return (ULevel*)Outer;
		else
			Outer = Outer->Outer;
	}
	return nullptr;
}


UPackage* GetPackage(UObject* Object) {
	auto Outer = Object->Outer;

	while (Outer)
	{
		if (Outer->Class == UPackage::StaticClass())
			return (UPackage*)Outer;
		else
			Outer = Outer->Outer;
	}
	return nullptr;
}

UWorld* GetWorld(AActor* Actor) {
	if (Actor && !Actor->IsDefaultObject() && Actor->Outer && !(Actor->Outer->Flags & EObjectFlags::BeginDestroyed) && !(Actor->Outer->Flags & EObjectFlags(0x10000000)))
	{
		if (auto Level = GetLevel(Actor))
			return Level->OwningWorld;
	}
	return nullptr;
}

TArray<TPair<UNetConnection*, TArray<FNetViewer>>> ViewerMap;
void Replication::BuildViewerMap(UNetDriver* Driver)
{
	if (ViewerMap.Num() == Driver->ClientConnections.Num()) [[likely]]
	{
		for (auto& ViewerPair : ViewerMap)
		{
			auto& Conn = ViewerPair.First;
			auto Owner = Conn->OwningActor;

			if (Owner && GetState(Conn) == USOCK_Open && GetElapsedTime(Conn->Driver) - Conn->LastReceiveTime < 1.5)
			{
				for (auto& Viewer : ViewerPair.Second)
				{
					// we have to update viewers
					auto& ViewingConn = Viewer.Connection;
					APlayerController* ViewingController = ViewingConn->PlayerController;

					if (ViewingController)
					{
						auto OutViewTarget = Conn->IsA<UChildConnection>() ? nullptr : Owner;
						if (auto ViewTarget = GetViewTarget(ViewingController))
							OutViewTarget = ViewTarget;
						Viewer.ViewTarget = ViewingConn->ViewTarget = OutViewTarget;


						FRotator ViewRotation;
						ViewingController->GetPlayerViewPoint(&Viewer.ViewLocation, &ViewRotation);
						constexpr auto radian = 0.017453292519943295;
						double cosPitch = cos(ViewRotation.pitch * radian), sinPitch = sin(ViewRotation.pitch * radian), cosYaw = cos(ViewRotation.Yaw * radian), sinYaw = sin(ViewRotation.Yaw * radian);
						Viewer.ViewDir = FVector(cosPitch * cosYaw, cosPitch * sinYaw, sinPitch);
					}
				}

				if (ViewerPair.Second.Num() - 1 != ViewerPair.First->Children.Num())
				{
					for (int i = ViewerPair.Second.Num() - 1; i < ViewerPair.First->Children.Num(); i++)
					{
						auto& Child = ViewerPair.First->Children[i];

						if (auto Controller = Child->PlayerController)
						{
							Child->ViewTarget = GetViewTarget(Controller);
							ViewerPair.Second.Add(ConstructNetViewer(Child));
						}
						else
							Child->ViewTarget = nullptr;
					}
				}
			}
			else
			{
				for (auto& Viewer : ViewerPair.Second)
				{
					Viewer.ViewTarget = Viewer.Connection->ViewTarget = nullptr;
				}
			}
		}
	}
	else
	{
		if (ViewerMap.Num() > Driver->ClientConnections.Num())
			ViewerMap.ResetNum(); // todo: make a better workaround
		//Log(L"PC");
		//for (auto& Conn : Driver->ClientConnections)
		for (int i = ViewerMap.Num(); i < Driver->ClientConnections.Num(); i++)
		{
			auto& Conn = Driver->ClientConnections[i];
			auto Owner = Conn->OwningActor;
			if (Owner && GetState(Conn) == USOCK_Open && GetElapsedTime(Conn->Driver) - Conn->LastReceiveTime < 1.5)
			{
				auto OutViewTarget = Owner;
				if (auto Controller = Conn->PlayerController)
					if (auto ViewTarget = GetViewTarget(Controller))
						if (GetWorld(Controller))
							OutViewTarget = ViewTarget;

				Conn->ViewTarget = OutViewTarget;

				TArray<FNetViewer> Viewers;
				Viewers.Reserve(1 + Conn->Children.Num());
				Viewers.Add(ConstructNetViewer(Conn));

				for (auto& Child : Conn->Children)
				{
					if (auto Controller = Child->PlayerController)
					{
						Child->ViewTarget = GetViewTarget(Controller);
						Viewers.Add(ConstructNetViewer(Child));
					}
					else
						Child->ViewTarget = nullptr;
				}
				ViewerMap.Add({ Conn, Viewers });
			}
			else
			{
				Conn->ViewTarget = nullptr;
				for (auto& Child : Conn->Children)
					Child->ViewTarget = nullptr;
			}
		}
	}
}

float FRand()
{
	/*random_device rd;
	mt19937 gen(rd());
	uniform_real_distribution<> dis(0, 1);
	float random_number = (float) dis(gen);

	return random_number;*/
	constexpr int32 RandMax = 0x00ffffff < RAND_MAX ? 0x00ffffff : RAND_MAX;
	return (rand() & RandMax) / (float)RandMax;
}

float fastLerp(float a, float b, float t)
{
	//return (a * (1.f - t)) + (b * t);
	return a - (a + b) * t;
}

struct FPrioList
{
	UNetConnection* Conn;
	TWeakObjectPtr<UNetConnection> WeakConn;
	TArray<FNetViewer> ViewerList;
	TArray<FActorPriority> PriorityList;

	FPrioList(UNetConnection* _Conn, TArray<FNetViewer>& _ViewerList, TArray<FActorPriority>& _PriorityList)
		: Conn(_Conn), WeakConn(_Conn), ViewerList(_ViewerList), PriorityList(_PriorityList)
	{
	}
};

TArray<FPrioList> PriorityLists;
int ActiveNetworkObjectsNum = 0;

struct Coordinate2D
{
public:
	int32 X;
	int32 Y;

	bool operator<(const Coordinate2D& _Rhs) const
	{
		return X == _Rhs.X ? Y < _Rhs.Y : X < _Rhs.X;
	}
};


FVector GetActorLocation(AActor* Actor)
{
	auto& RootComponent = Actor->RootComponent;
	return RootComponent ? *(FVector*)(__int64(RootComponent) + 0x1e0) : FVector();
}

struct FCellInfo
{
	int32 StartX;
	int32 EndX;
	int32 StartY;
	int32 EndY;
};

struct FConnActorInfo
{
	int LastRepFrame = 0;
	UActorChannel* Channel = nullptr;
	int ChannelCloseFrame = 0;
};

struct FCellInfo GetCellInfoForActor(struct FActorInfo* ActorInfo);

struct FActorInfo
{
public:
	AActor* Actor;
	TWeakObjectPtr<AActor> WeakActor;
	int LastPreRepFrame = 0;
	int RepPeriodFrame = 0;
	int ForceNetUpdateFrame = 0;
	float CullDistance;
	std::map<UNetConnection*, FConnActorInfo> ConnectionActorInfos;
	FCellInfo CellInfo;

	FActorInfo(AActor* _Actor)
		: Actor(_Actor), WeakActor(_Actor), RepPeriodFrame((int)max(30.f / Actor->NetUpdateFrequency, 1)), CullDistance(sqrtf(Actor->NetCullDistanceSquared))
	{
	}
};

FCellInfo GetCellInfoForActor(FActorInfo* ActorInfo)
{
	auto Location = GetActorLocation(ActorInfo->Actor);
	const auto LocationBiasX = Location.X + SpatialBias;
	const auto LocationBiasY = Location.Y + SpatialBias;

	const auto Dist = ActorInfo->CullDistance;
	const auto MinX = LocationBiasX - Dist;
	const auto MinY = LocationBiasY - Dist;
	auto MaxX = LocationBiasX + Dist;
	auto MaxY = LocationBiasY + Dist;


	FCellInfo CellInfo;

	CellInfo.StartX = (int32)max(0, MinX / CellSize);
	CellInfo.StartY = (int32)max(0, MinY / CellSize);

	CellInfo.EndX = (int32)max(0, MaxX / CellSize);
	CellInfo.EndY = (int32)max(0, MaxY / CellSize);

	return CellInfo;
}

typedef TArray<FActorInfo*> ActorListType;
ActorListType AlwaysRelevantActors;
std::map<Coordinate2D, ActorListType> DormancyGridMap;
std::map<Coordinate2D, ActorListType> StaticGridMap;
std::map<Coordinate2D, ActorListType> DynamicGridMap;
std::map<UNetConnection*, ActorListType> PerConnActorInfoMap;
std::map<FName, ActorListType> AlwaysRelevantForLevelMap;
std::map<UNetConnection*, TArray<FActorDestructionInfo*>> PerConnDestructionInfoMap;
ActorListType GlobalActorInfos;
ActorListType DynamicActorInfos;
ActorListType DormancyActorInfos;
TArray<FConnActorInfo> ConnInfos;

auto AddToArray = [&](AActor* Actor)
	{
		if (Actor->bOnlyRelevantToOwner || Actor->bNetUseOwnerRelevancy)
			return;
		auto ActorInfo = new FActorInfo(Actor);
		GlobalActorInfos.Add(ActorInfo);
		if ((Actor->bAlwaysRelevant && !Actor->bOnlyRelevantToOwner) || Actor->IsA<AInfo>() || Actor->IsA<AFortAlwaysRelevantReplicatedActor>())
		{
			auto Level = Actor ? Actor->Outer->Cast<ULevel>() : nullptr;
			bool bPersistent = false;
			if (Level && Level->OwningWorld)
				bPersistent = Level == Level->OwningWorld->PersistentLevel;
			auto StreamingLevelName = Level && !bPersistent ? GetPackage(Level)->Name : FName();

			if (StreamingLevelName.IsValid())
			{
				AlwaysRelevantForLevelMap[StreamingLevelName].Add(ActorInfo);
			}
			else
			{
				AlwaysRelevantActors.Add(ActorInfo);
			}
			return;
		}

		Coordinate2D GridPosition;
		GridPosition.X = int32((GetActorLocation(Actor).X + SpatialBias) / CellSize);
		GridPosition.Y = int32((GetActorLocation(Actor).Y + SpatialBias) / CellSize);
		auto CellInfo = GetCellInfoForActor(ActorInfo);
		ActorInfo->CellInfo = CellInfo;

		if (Actor->IsA<ABuildingSMActor>() || Actor->IsA<AFortStaticReplicatedActor>())
		{

			for (int X = CellInfo.StartX; X <= CellInfo.EndX; X++)
			{
				for (int Y = CellInfo.StartY; Y <= CellInfo.EndY; Y++)
				{
					Coordinate2D GridPosition;
					GridPosition.X = X;
					GridPosition.Y = Y;

					StaticGridMap[GridPosition].Add(ActorInfo);
				}
			}
		}
		else if (Actor->IsA<AFortPickup>() || Actor->IsA<ABuildingGameplayActorConsumable>())
		{
			for (int X = CellInfo.StartX; X <= CellInfo.EndX; X++)
			{
				for (int Y = CellInfo.StartY; Y <= CellInfo.EndY; Y++)
				{
					Coordinate2D GridPosition;
					GridPosition.X = X;
					GridPosition.Y = Y;

					DormancyGridMap[GridPosition].Add(ActorInfo);
				}
			}
			//DormancyGridMap[GridPosition].Add(ActorInfo);
			DormancyActorInfos.Add(ActorInfo);
		}
		else
		{
			for (int X = CellInfo.StartX; X <= CellInfo.EndX; X++)
			{
				for (int Y = CellInfo.StartY; Y <= CellInfo.EndY; Y++)
				{
					Coordinate2D GridPosition;
					GridPosition.X = X;
					GridPosition.Y = Y;

					DynamicGridMap[GridPosition].Add(ActorInfo);
				}
			}
			//DynamicGridMap[GridPosition].Add(ActorInfo);
			DynamicActorInfos.Add(ActorInfo);
		}

		//printf("ActorInfo %s is at %d %d\n", ActorInfo->GetName().c_str(), GridPosition.X, GridPosition.Y);
	};
auto RemoveFromArray = [&](AActor* Actor)
	{
		if (Actor->bOnlyRelevantToOwner || Actor->bNetUseOwnerRelevancy)
			return;
		FActorInfo* ActorInfo = nullptr;
		for (int i = 0; i < GlobalActorInfos.Num(); i++)
		{
			if (GlobalActorInfos[i]->Actor == Actor)
			{
				ActorInfo = GlobalActorInfos[i];
				GlobalActorInfos.Remove(i);
				break;
			}
		}
		if (!ActorInfo)
			return;
		if ((Actor->bAlwaysRelevant && !Actor->bOnlyRelevantToOwner) || Actor->IsA<AInfo>() || Actor->IsA<AFortAlwaysRelevantReplicatedActor>())
		{
			auto Level = Actor ? Actor->Outer->Cast<ULevel>() : nullptr;
			bool bPersistent = false;
			if (Level && Level->OwningWorld)
				bPersistent = Level == Level->OwningWorld->PersistentLevel;
			auto StreamingLevelName = Level && !bPersistent ? GetPackage(Level)->Name : FName();

			if (StreamingLevelName.IsValid())
			{
				auto& Array = AlwaysRelevantForLevelMap[StreamingLevelName];
				for (int i = 0; i < Array.Num(); i++)
					if (Array[i]->Actor == Actor)
					{
						Array.Remove(i);
						break;
					}
			}
			else
			{
				//printf("always relevant: %s\n", ActorInfo->GetName().c_str());
				//AlwaysRelevantActors.Add(ActorInfo);
				for (int i = 0; i < AlwaysRelevantActors.Num(); i++)
					if (AlwaysRelevantActors[i]->Actor == Actor)
					{
						//printf("removed from alwaysrelevant\n");
						AlwaysRelevantActors.Remove(i);
						break;
					}
			}
			return;
		}

		Coordinate2D GridPosition;
		GridPosition.X = int32((GetActorLocation(Actor).X + SpatialBias) / CellSize);
		GridPosition.Y = int32((GetActorLocation(Actor).Y + SpatialBias) / CellSize);

		if (Actor->IsA<ABuildingSMActor>() || Actor->IsA<AFortStaticReplicatedActor>()) {
			auto CellInfo = ActorInfo->CellInfo;

			for (int X = CellInfo.StartX; X <= CellInfo.EndX; X++)
			{
				for (int Y = CellInfo.StartY; Y <= CellInfo.EndY; Y++)
				{
					Coordinate2D GridPosition;
					GridPosition.X = X;
					GridPosition.Y = Y;

					auto& ActorMap = StaticGridMap[GridPosition];
					for (int i = 0; i < ActorMap.Num(); i++)
						if (ActorMap[i]->Actor == Actor)
						{
							StaticGridMap[GridPosition].Remove(i);
							break;
						}
				}
			}
		}
		else if (Actor->IsA<AFortPickup>() || Actor->IsA<ABuildingGameplayActorConsumable>())
		{
			for (int i = 0; i < DormancyActorInfos.Num(); i++)
			{
				if (DormancyActorInfos[i]->Actor == Actor)
				{
					DormancyActorInfos.Remove(i);
					break;
				}
			}
			auto CellInfo = ActorInfo->CellInfo;

			for (int X = CellInfo.StartX; X <= CellInfo.EndX; X++)
			{
				for (int Y = CellInfo.StartY; Y <= CellInfo.EndY; Y++)
				{
					Coordinate2D GridPosition;
					GridPosition.X = X;
					GridPosition.Y = Y;

					auto& ActorMap = DormancyGridMap[GridPosition];
					for (int i = 0; i < ActorMap.Num(); i++)
						if (ActorMap[i]->Actor == Actor)
						{
							ActorMap.Remove(i);
							break;
						}
				}
			}
			return;
		}
		else
		{
			for (int i = 0; i < DynamicActorInfos.Num(); i++)
			{
				if (DynamicActorInfos[i]->Actor == Actor)
				{
					DynamicActorInfos.Remove(i);
					break;
				}
			}
			auto CellInfo = ActorInfo->CellInfo;

			for (int X = CellInfo.StartX; X <= CellInfo.EndX; X++)
			{
				for (int Y = CellInfo.StartY; Y <= CellInfo.EndY; Y++)
				{
					Coordinate2D GridPosition;
					GridPosition.X = X;
					GridPosition.Y = Y;

					auto& ActorMap = DynamicGridMap[GridPosition];
					for (int i = 0; i < ActorMap.Num(); i++)
						if (ActorMap[i]->Actor == Actor)
						{
							ActorMap.Remove(i);
							break;
						}
				}
			}
			return;
		}
	};
auto FindOrAddToArray = [&](AActor* Actor)
	{
		if (Actor->bOnlyRelevantToOwner || Actor->bNetUseOwnerRelevancy)
			return;
		for (int i = 0; i < GlobalActorInfos.Num(); i++)
		{
			if (GlobalActorInfos[i]->Actor == Actor)
			{
				return;
			}
		}
		auto ActorInfo = new FActorInfo(Actor);
		GlobalActorInfos.Add(ActorInfo);
		if ((Actor->bAlwaysRelevant && !Actor->bOnlyRelevantToOwner) || Actor->IsA<AInfo>() || Actor->IsA<AFortAlwaysRelevantReplicatedActor>())
		{
			auto Level = Actor ? Actor->Outer->Cast<ULevel>() : nullptr;
			bool bPersistent = false;
			if (Level && Level->OwningWorld)
				bPersistent = Level == Level->OwningWorld->PersistentLevel;
			auto StreamingLevelName = Level && !bPersistent ? GetPackage(Level)->Name : FName();

			if (StreamingLevelName.IsValid())
			{
				auto& Array = AlwaysRelevantForLevelMap[StreamingLevelName];
				Array.Add(ActorInfo);
			}
			else
			{
				AlwaysRelevantActors.Add(ActorInfo);
			}
			return;
		}
		if (Actor->bAlwaysRelevant || Actor->bOnlyRelevantToOwner || Actor->bNetUseOwnerRelevancy)
			return;

		auto ActorLocation = GetActorLocation(Actor);
		Coordinate2D GridPosition;
		GridPosition.X = int32((ActorLocation.X + SpatialBias) / CellSize);
		GridPosition.Y = int32((ActorLocation.Y + SpatialBias) / CellSize);
		auto CellInfo = GetCellInfoForActor(ActorInfo);
		ActorInfo->CellInfo = CellInfo;

		if (Actor->IsA<ABuildingSMActor>() || Actor->IsA<AFortStaticReplicatedActor>()) {
			for (int X = CellInfo.StartX; X <= CellInfo.EndX; X++)
			{
				for (int Y = CellInfo.StartY; Y <= CellInfo.EndY; Y++)
				{
					Coordinate2D GridPosition;
					GridPosition.X = X;
					GridPosition.Y = Y;

					auto& ActorMap = StaticGridMap[GridPosition];
					StaticGridMap[GridPosition].Add(ActorInfo);
				}
			}
		}
		else if (Actor->IsA<AFortPickup>() || Actor->IsA<ABuildingGameplayActorConsumable>())
		{
			for (int X = CellInfo.StartX; X <= CellInfo.EndX; X++)
			{
				for (int Y = CellInfo.StartY; Y <= CellInfo.EndY; Y++)
				{
					Coordinate2D GridPosition;
					GridPosition.X = X;
					GridPosition.Y = Y;

					auto& ActorMap = DormancyGridMap[GridPosition];
					ActorMap.Add(ActorInfo);
				}
			}
			return;
		}
		else
		{
			for (int X = CellInfo.StartX; X <= CellInfo.EndX; X++)
			{
				for (int Y = CellInfo.StartY; Y <= CellInfo.EndY; Y++)
				{
					Coordinate2D GridPosition;
					GridPosition.X = X;
					GridPosition.Y = Y;

					auto& ActorMap = DynamicGridMap[GridPosition];
					ActorMap.Add(ActorInfo);
				}
			}
			return;
		}

		//printf("ActorInfo %s is at %d %d\n", ActorInfo->GetName().c_str(), GridPosition.X, GridPosition.Y);
	};

void MoveActorInfo(FActorInfo* ActorInfo, bool bDynamic)
{
	//printf("MoveActor[%s]\n", ActorInfo->Actor->GetName().c_str());
	auto& PreviousCellInfo = ActorInfo->CellInfo;
	auto NewCellInfo = GetCellInfoForActor(ActorInfo);
	bool bDirty = false;
	auto& ActorMap = bDynamic ? DynamicGridMap : DormancyGridMap;


	if (NewCellInfo.StartX > PreviousCellInfo.EndX || NewCellInfo.EndX < PreviousCellInfo.StartX ||
		NewCellInfo.StartY > PreviousCellInfo.EndY || NewCellInfo.EndY < PreviousCellInfo.StartY) [[unlikely]]
	{
		// No longer intersecting, we just have to remove from all previous nodes and add to all new nodes

		bDirty = true;

		for (int32 X = PreviousCellInfo.StartX; X <= PreviousCellInfo.StartX; ++X)
		{
			for (int32 Y = PreviousCellInfo.StartY; Y <= PreviousCellInfo.EndY; ++Y)
			{
				Coordinate2D GridPosition;
				GridPosition.X = X;
				GridPosition.Y = Y;

				auto& ActorList = ActorMap[GridPosition];
				for (int i = 0; i < ActorList.Num(); i++)
				{
					if (ActorList[i]->Actor == ActorInfo->Actor)
					{
						ActorList.Remove(i);
						break;
					}
				}
			}
		}

		for (int32 X = NewCellInfo.StartX; X <= NewCellInfo.StartX; ++X)
		{
			for (int32 Y = NewCellInfo.StartY; Y <= NewCellInfo.EndY; ++Y)
			{
				Coordinate2D GridPosition;
				GridPosition.X = X;
				GridPosition.Y = Y;

				ActorMap[GridPosition].Add(ActorInfo);
			}
		}
	}
	else
	{
		// Some overlap so lets find out what cells need to be added or removed

		if (PreviousCellInfo.StartX < NewCellInfo.StartX)
		{
			// We lost columns on the left side
			bDirty = true;

			for (int32 X = PreviousCellInfo.StartX; X < NewCellInfo.StartX; ++X)
			{
				for (int32 Y = PreviousCellInfo.StartY; Y <= PreviousCellInfo.EndY; ++Y)
				{
					Coordinate2D GridPosition;
					GridPosition.X = X;
					GridPosition.Y = Y;

					auto& ActorList = ActorMap[GridPosition];
					for (int i = 0; i < ActorList.Num(); i++)
					{
						if (ActorList[i]->Actor == ActorInfo->Actor)
						{
							ActorList.Remove(i);
							break;
						}
					}
				}
			}
		}
		else if (PreviousCellInfo.StartX > NewCellInfo.StartX)
		{
			// We added columns on the left side
			bDirty = true;

			for (int32 X = NewCellInfo.StartX; X < PreviousCellInfo.StartX; ++X)
			{
				for (int32 Y = NewCellInfo.StartY; Y <= NewCellInfo.EndY; ++Y)
				{
					Coordinate2D GridPosition;
					GridPosition.X = X;
					GridPosition.Y = Y;

					ActorMap[GridPosition].Add(ActorInfo);
				}
			}

		}

		if (PreviousCellInfo.EndX < NewCellInfo.EndX)
		{
			// We added columns on the right side
			bDirty = true;

			for (int32 X = PreviousCellInfo.EndX + 1; X <= NewCellInfo.EndX; ++X)
			{
				for (int32 Y = NewCellInfo.StartY; Y <= NewCellInfo.EndY; ++Y)
				{
					Coordinate2D GridPosition;
					GridPosition.X = X;
					GridPosition.Y = Y;

					ActorMap[GridPosition].Add(ActorInfo);
				}
			}

		}
		else if (PreviousCellInfo.EndX > NewCellInfo.EndX)
		{
			// We lost columns on the right side
			bDirty = true;

			for (int32 X = NewCellInfo.EndX + 1; X <= PreviousCellInfo.EndX; ++X)
			{
				for (int32 Y = PreviousCellInfo.StartY; Y <= PreviousCellInfo.EndY; ++Y)
				{
					Coordinate2D GridPosition;
					GridPosition.X = X;
					GridPosition.Y = Y;

					auto& ActorList = ActorMap[GridPosition];
					for (int i = 0; i < ActorList.Num(); i++)
					{
						if (ActorList[i]->Actor == ActorInfo->Actor)
						{
							ActorList.Remove(i);
							break;
						}
					}
				}
			}
		}

		// --------------------------------------------------

		// We've handled left/right sides. So while handling top and bottom we only need to worry about this run of X cells
		const int32 StartX = (int32)max(NewCellInfo.StartX, PreviousCellInfo.StartX);
		const int32 EndX = (int32)min(NewCellInfo.EndX, PreviousCellInfo.EndX);

		if (PreviousCellInfo.StartY < NewCellInfo.StartY)
		{
			// We lost rows on the top side
			bDirty = true;

			for (int32 X = StartX; X <= EndX; ++X)
			{
				for (int32 Y = PreviousCellInfo.StartY; Y < NewCellInfo.StartY; ++Y)
				{
					Coordinate2D GridPosition;
					GridPosition.X = X;
					GridPosition.Y = Y;

					auto& ActorList = ActorMap[GridPosition];
					for (int i = 0; i < ActorList.Num(); i++)
					{
						if (ActorList[i]->Actor == ActorInfo->Actor)
						{
							ActorList.Remove(i);
							break;
						}
					}
				}
			}
		}
		else if (PreviousCellInfo.StartY > NewCellInfo.StartY)
		{
			// We added rows on the top side
			bDirty = true;

			for (int32 X = StartX; X <= EndX; ++X)
			{
				for (int32 Y = NewCellInfo.StartY; Y < PreviousCellInfo.StartY; ++Y)
				{
					Coordinate2D GridPosition;
					GridPosition.X = X;
					GridPosition.Y = Y;

					ActorMap[GridPosition].Add(ActorInfo);
				}
			}
		}

		if (PreviousCellInfo.EndY < NewCellInfo.EndY)
		{
			// We added rows on the bottom side
			bDirty = true;

			for (int32 X = StartX; X <= EndX; ++X)
			{
				for (int32 Y = PreviousCellInfo.EndY + 1; Y <= NewCellInfo.EndY; ++Y)
				{
					Coordinate2D GridPosition;
					GridPosition.X = X;
					GridPosition.Y = Y;

					ActorMap[GridPosition].Add(ActorInfo);
				}
			}
		}
		else if (PreviousCellInfo.EndY > NewCellInfo.EndY)
		{
			// We lost rows on the bottom side
			bDirty = true;

			for (int32 X = StartX; X <= EndX; ++X)
			{
				for (int32 Y = NewCellInfo.EndY + 1; Y <= PreviousCellInfo.EndY; ++Y)
				{
					Coordinate2D GridPosition;
					GridPosition.X = X;
					GridPosition.Y = Y;

					auto& ActorList = ActorMap[GridPosition];
					for (int i = 0; i < ActorList.Num(); i++)
					{
						if (ActorList[i]->Actor == ActorInfo->Actor)
						{
							ActorList.Remove(i);
							break;
						}
					}
				}
			}
		}
	}

	if (bDirty)
	{
		ActorInfo->CellInfo = NewCellInfo;
	}
}
void MoveDynamicActors()
{
	for (auto& ActorInfo : DynamicActorInfos)
		MoveActorInfo(ActorInfo, true);

	for (auto& ActorInfo : DormancyActorInfos)
		if (ActorInfo->Actor->NetDormancy <= ENetDormancy::DORM_Awake)
			MoveActorInfo(ActorInfo, false);
}

/*TArray<AActor*> ActorList;
TArray<AActor*> AddedList;*/
void Replication::BuildPriorityLists(UNetDriver* Driver, float DeltaTime)
{
	//Log(L"BCL");
	void (*&CallPreReplication)(AActor*, UNetDriver*) = decltype(CallPreReplication)(ReplicationOffsets::CallPreReplication);
	//void (*&RemoveNetworkActor)(UNetDriver*, AActor*) = decltype(RemoveNetworkActor)(ReplicationOffsets::RemoveNetworkActor);
	UActorChannel* (*&FindActorChannelRef)(UNetConnection*, const TWeakObjectPtr<AActor>&) = decltype(FindActorChannelRef)(ReplicationOffsets::FindActorChannelRef);
	void (*&CloseActorChannel)(UActorChannel*, uint8) = decltype(CloseActorChannel)(ReplicationOffsets::CloseActorChannel);
	bool (*&SupportsObject)(void*, const UObject*, const TWeakObjectPtr<UObject>*) = decltype(SupportsObject)(ReplicationOffsets::SupportsObject);
	UObject* (*&GetArchetype)(UObject*) = decltype(GetArchetype)(ReplicationOffsets::GetArchetype);


	auto& ActiveNetworkObjects = GetNetworkObjectList(Driver).ActiveNetworkObjects;
	auto Time = GetTimeSeconds(Driver->World);

	PriorityLists.ResetNum();
	PriorityLists.Reserve(ViewerMap.Num());

	for (auto& ViewerPair : ViewerMap)
	{
		auto& Conn = ViewerPair.First;
		auto& Viewers = ViewerPair.Second;

		TArray<FActorPriority> List;
		List.Reserve(ActiveNetworkObjects.Num() /* + GetDestroyedStartupOrDormantActors(Driver).Num()*/);

		PriorityLists.Add(FPrioList(Conn, Viewers, List));
	}

	if (ActiveNetworkObjectsNum/* != ActiveNetworkObjects.Num()*/ == 0)
	{
		for (auto& ActorInfo : ActiveNetworkObjects)
		{
			auto& Actor = ActorInfo->Actor;

			AddToArray(Actor);
		}

		ActiveNetworkObjectsNum = ActiveNetworkObjects.Num();
		Utils::Hook(ReplicationOffsets::RemoveNetworkActor, RemoveNetworkActor, RemoveNetworkActorOG);
		Utils::Hook(Sarah::Offsets::ImageBase + 0x18A6048, AddNetworkActor, AddNetworkActorOG);
		Utils::Hook(Sarah::Offsets::ImageBase + 0x65629C4, ForceNetUpdate);
		MH_EnableHook(MH_ALL_HOOKS);
	}

	for (auto& PriorityListPair : PriorityLists)
	{
		auto& ViewerPair = PriorityListPair.ViewerList;
		auto& Conn = PriorityListPair.Conn;
		auto& Viewers = PriorityListPair.ViewerList;

		auto AddActor = [&](UNetConnection* PriorityConn, FActorInfo* ActorInfo, bool bDoCullCheck = true)
			{
				auto& ConnectionInfo = ActorInfo->ConnectionActorInfos[PriorityConn];
				if (ConnectionInfo.LastRepFrame == RepGraphFrame)
					return;

				auto& Actor = ActorInfo->Actor;
				if (!Actor)
					return;

				auto Level = GetLevel(Actor);
				if (Actor->bTearOff || ((Actor->bNetStartup || (!Actor->bActorInitialized && !Actor->bActorSeamlessTraveled && Actor->bNetLoadOnClient && Level && !Level->bAlreadyInitializedNetworkActors)) && Actor->NetDormancy == ENetDormancy::DORM_Initial))
					return;
				if (!Conn->Driver)
				{
					PriorityListPair.PriorityList.ResetNum();
					return;
				}
				//printf("add to rep %s\n", ActorInfo->GetName().c_str());
				auto Channel = ConnectionInfo.Channel;
				if (!Channel)
				{
					Channel = ConnectionInfo.Channel = FindActorChannelRef(Conn, ActorInfo->WeakActor);
				}
				if (Channel && (Channel->bPendingDormancy || Channel->Dormant || Channel->Closing))
				{
					//printf("Dormant[%s]\n", ActorInfo->GetName().c_str());
					return;
				}
				if (ConnectionInfo.LastRepFrame != 0 && ConnectionInfo.LastRepFrame + ActorInfo->RepPeriodFrame > RepGraphFrame && ActorInfo->ForceNetUpdateFrame <= ConnectionInfo.LastRepFrame)
				{
					return;
					//return;
				}
				if (ActorInfo->LastPreRepFrame != RepGraphFrame)
				{
					ActorInfo->LastPreRepFrame = RepGraphFrame;
					CallPreReplication(Actor, Driver);
				}

				auto Priority = 0.f;
				if (!Actor->IsA<AInfo>() && !Actor->IsA<APlayerController>() && !Actor->IsA<APlayerState>())
				{
					float SmallestDisSquared = 9999999.f;
					auto Loc = GetActorLocation(Actor);
					bool bAnyNear = false;
					int32 ViewersThatSkipActor = 0;

					for (auto& Viewer : Viewers)
					{
						auto DistanceSquared = (Loc - Viewer.ViewLocation).SizeSquared();
						SmallestDisSquared = (float)min(SmallestDisSquared, DistanceSquared);

						if (bDoCullCheck && DistanceSquared > Actor->NetCullDistanceSquared)
							ViewersThatSkipActor++;
					}

					if (bDoCullCheck && ViewersThatSkipActor == Viewers.Num())
						return;

					const float MaxDistanceScaling = 60000.f * 60000.f;

					const float DistanceFactor = clamp((SmallestDisSquared) / MaxDistanceScaling, 0.f, 1.f);

					Priority += DistanceFactor;
				}

				const float FramesSinceLastRep = ((float)(RepGraphFrame - ConnectionInfo.LastRepFrame));
				const float StarvationFactor = 1.f - clamp(FramesSinceLastRep / (float)20, 0.f, 1.f);

				Priority += StarvationFactor;

				if (Actor->NetDormancy > ENetDormancy::DORM_Awake && ConnectionInfo.LastRepFrame > 0)
					Priority -= 1.5f;

				if (ActorInfo->ForceNetUpdateFrame > ConnectionInfo.LastRepFrame)
					Priority -= 1.f;

				for (auto& Viewer : Viewers)
				{
					if (Actor == Viewer.ViewTarget || Actor == Viewer.InViewer)
						Priority -= 10.f;
				}
				ConnectionInfo.LastRepFrame = RepGraphFrame;
				auto PriorityActor = FActorPriority(PriorityConn, Channel, Actor, true, Viewers, Priority);
				int InsertIdx = 0;
				for (; InsertIdx < PriorityListPair.PriorityList.Num(); InsertIdx++)
				{
					auto& PrioActor = PriorityListPair.PriorityList[InsertIdx];

					if (PrioActor.Priority >= PriorityActor.Priority) [[unlikely]]
						break;
				}
				PriorityListPair.PriorityList.Add(PriorityActor, InsertIdx);
			};

		for (auto& Viewer : Viewers)
		{
			auto& PrioConn = Viewer.Connection;
			auto& PerConnMap = PerConnActorInfoMap[PrioConn];
			auto AddPerConnActor = [&](UNetConnection* PrioConn, AActor* Actor)
				{
					if (!Actor)
						return;

					for (auto& ActorInfo : PerConnMap)
					{
						if (ActorInfo->Actor == Actor)
						{
							AddActor(PrioConn, ActorInfo, false);
							return;
						}
					}

					for (auto& ActorInfo : GlobalActorInfos)
					{
						if (ActorInfo->Actor == Actor)
						{
							AddActor(PrioConn, PerConnMap.Add(ActorInfo), false);
							return;
						}
					}

					auto ActorInfo = new FActorInfo(Actor);
					GlobalActorInfos.Add(ActorInfo);
					AddActor(PrioConn, PerConnMap.Add(ActorInfo), false);
					return;

				};
			AddPerConnActor(PrioConn, Viewer.InViewer);
			AddPerConnActor(PrioConn, Viewer.ViewTarget);

			if (auto PlayerController = Viewer.InViewer->Cast<AFortPlayerController>())
			{
				AddPerConnActor(PrioConn, PlayerController->PlayerState);
				AddPerConnActor(PrioConn, PlayerController->WorldInventory);

				if (auto PlayerControllerAthena = PlayerController->Cast<AFortPlayerControllerAthena>())
				{
					AddPerConnActor(PrioConn, PlayerControllerAthena->ViewTargetInventory);
					AddPerConnActor(PrioConn, PlayerControllerAthena->BroadcastRemoteClientInfo);

					if (auto PlayerState = PlayerController->PlayerState->Cast<AFortPlayerStateAthena>())
					{
						AddPerConnActor(PrioConn, PlayerState->PlayerTeamPrivate.Get());
					}
				}

				if (auto Pawn = PlayerController->Pawn->Cast<AFortPawn>())
				{
					if (Pawn != Viewer.ViewTarget)
						AddPerConnActor(PrioConn, Pawn);

					for (auto& Weapon : Pawn->CurrentWeaponList)
						AddPerConnActor(PrioConn, Weapon);
				}

				if (auto Pawn = Viewer.ViewTarget->Cast<AFortPlayerPawn>())
				{
					if (Pawn->TetherComponent && Pawn->TetherComponent->TetherPawn.ObjectIndex > 0)
						AddPerConnActor(PrioConn, Pawn->TetherComponent->TetherPawn.Get());
				}
				else if (auto AIPawn = Viewer.ViewTarget->Cast<AFortAIPawn>())
				{
					AddPerConnActor(PrioConn, AIPawn->TetheredFollower);
				}
			}

			for (auto& ActorInfo : AlwaysRelevantActors)
			{
				AddActor(PrioConn, ActorInfo, false);
			}

			for (auto& VisibleName : GetClientVisibleLevelNames(PrioConn))
			{
				for (auto& ActorInfo : AlwaysRelevantForLevelMap[VisibleName])
				{
					AddActor(PrioConn, ActorInfo, false);
				}
			}

			Coordinate2D ViewingCell;
			ViewingCell.X = int32((Viewer.ViewLocation.X + SpatialBias) / CellSize);
			ViewingCell.Y = int32((Viewer.ViewLocation.Y + SpatialBias) / CellSize);


			auto AddActorsForCell = [&](Coordinate2D& Coordinate)
				{
					auto& DormancyActors = DormancyGridMap[Coordinate];
					auto& StaticActors = StaticGridMap[Coordinate];
					auto& DynamicActors = DynamicGridMap[Coordinate];

					//for (auto& ActorInfo : DormancyActors)
					for (int i = 0; i < DormancyActors.Num(); i++)
					{
						auto& ActorInfo = DormancyActors[i];
						auto& Actor = ActorInfo->Actor;

						AddActor(PrioConn, ActorInfo);
					}

					for (auto& ActorInfo : StaticActors)
					{
						AddActor(PrioConn, ActorInfo);
					}

					//for (auto& ActorInfo : DynamicActors)
					for (int i = 0; i < DynamicActors.Num(); i++)
					{
						auto& ActorInfo = DynamicActors[i];
						AddActor(PrioConn, ActorInfo);
					}
				};

			AddActorsForCell(ViewingCell);
		}
	}

	/*for (auto& ViewerPair : ViewerMap)
	{
		auto& Conn = ViewerPair.First;

		auto Channel = FindActorChannelRef(Conn, UWorld::GetWorld()->GameState);

		auto& Viewers = ViewerPair.Second;


		auto PriorityConn = Conn;

		auto Bro = FNetworkObjectInfo();
		Bro.ActorInfo = UWorld::GetWorld()->GameState;
		Bro.WeakActor = Bro.ActorInfo;
		auto PriorityActor = FActorPriority(PriorityConn, Channel, &Bro, true, Viewers);
		auto PriorityListPair = PriorityLists.Search([&](TPair<UNetConnection*, TArray<FActorPriority>>& Pair)
			{
				return Pair.First == Conn;
			});
		int InsertIdx = 0;
		for (; InsertIdx < PriorityListPair->Second.Num(); InsertIdx++)
		{
			auto& PrioActor = PriorityListPair->Second[InsertIdx];

			if (PrioActor.Priority <= PriorityActor.Priority)
				break;
		}
		PriorityListPair->Second.Add(PriorityActor, InsertIdx);
	}*/
}

bool Replication::IsActorRelevantToConnection(const AActor* Actor, const TArray<FNetViewer>& ConnectionViewers)
{
	bool (*&IsNetRelevantFor)(const AActor*, const AActor*, const AActor*, const FVector&) = decltype(IsNetRelevantFor)(Actor->VTable[ReplicationOffsets::IsNetRelevantForVft]);

	for (auto& Viewer : ConnectionViewers)
	{
		if (IsNetRelevantFor(Actor, Viewer.InViewer, Viewer.ViewTarget, Viewer.ViewLocation)) [[unlikely]]
		{
			return true;
		}
		/*else if (!ActorInfo->RootComponent)
		{
			return true;
		}*/
	}

	return false;
}

FNetViewer Replication::ConstructNetViewer(UNetConnection* NetConnection)
{
	FNetViewer NewNetViewer;
	NewNetViewer.Connection = NetConnection;
	NewNetViewer.InViewer = NetConnection->PlayerController ? NetConnection->PlayerController : NetConnection->OwningActor;
	NewNetViewer.ViewTarget = NetConnection->ViewTarget;

	APlayerController* ViewingController = NetConnection->PlayerController;

	if (ViewingController) [[likely]]
	{
		//FRotator ViewRotation = ViewingController->ControlRotation;
		FRotator ViewRotation;
		ViewingController->GetPlayerViewPoint(&NewNetViewer.ViewLocation, &ViewRotation);
		constexpr auto radian = 0.017453292519943295;
		double cosPitch = cos(ViewRotation.pitch * radian), sinPitch = sin(ViewRotation.pitch * radian), cosYaw = cos(ViewRotation.Yaw * radian), sinYaw = sin(ViewRotation.Yaw * radian);
		NewNetViewer.ViewDir = FVector(cosPitch * cosYaw, cosPitch * sinYaw, sinPitch);
	}
	else
	{
		NewNetViewer.ViewLocation = {};
		NewNetViewer.ViewDir = {};
	}

	return NewNetViewer;
}

void Replication::SendClientAdjustment(APlayerController* PlayerController)
{
	if (PlayerController->AcknowledgedPawn != PlayerController->Pawn && !PlayerController->SpectatorPawn)
		return;

	auto Pawn = (ACharacter*)(PlayerController->Pawn ? PlayerController->Pawn : PlayerController->SpectatorPawn);
	if (Pawn && Pawn->RemoteRole == ENetRole::ROLE_AutonomousProxy)
	{
		auto Interface = Utils::GetInterface<INetworkPredictionInterface>(Pawn->CharacterMovement);

		if (Interface)
		{
			void (*&SendClientAdjustment)(INetworkPredictionInterface*) = decltype(SendClientAdjustment)(ReplicationOffsets::SendClientAdjustment);
			SendClientAdjustment(Interface);
		}
	}
}

bool Replication::IsNetReady(UNetConnection* Connection, bool bSaturate) {
	bool (*&IsNetReady)(UNetConnection*, bool) = decltype(IsNetReady)(ReplicationOffsets::IsNetReady);

	return IsNetReady(Connection, bSaturate);
}

bool Replication::IsNetReady(UChannel* Channel, bool bSaturate) {
	if (Channel->NumOutRec >= 255)
		return 0;

	return IsNetReady(Channel->Connection, bSaturate);
}

int Replication::ProcessPrioritizedActors(UNetDriver* Driver, UNetConnection* Conn, TArray<FActorPriority>& PriorityActors) {
	//Log(L"PPA");
	UActorChannel* (*&CreateChannelByName)(UNetConnection*, FName*, EChannelCreateFlags, int32_t) = decltype(CreateChannelByName)(ReplicationOffsets::CreateChannelByName);
	__int64 (*&SetChannelActor)(UActorChannel*, AActor*, uint32) = decltype(SetChannelActor)(ReplicationOffsets::SetChannelActor);
	__int64 (*&ReplicateActor)(UActorChannel*) = decltype(ReplicateActor)(ReplicationOffsets::ReplicateActor);
	void (*&StartBecomingDormant)(UActorChannel*) = decltype(StartBecomingDormant)(ReplicationOffsets::StartBecomingDormant);

	if (!IsNetReady(Conn, false))
		return 0;

	int i = 0;
	for (auto& PriorityActor : PriorityActors)
	{
		i++;
		auto Actor = PriorityActor.Actor;

		/*if (!ActorInfo && PriorityActor.DestructionInfo) [[unlikely]]
		{
			GetRepContextLevel(Conn) = PriorityActor.DestructionInfo->Level.Get();

			SendDestructionInfo(Driver, Conn, PriorityActor.DestructionInfo);

			GetRepContextLevel(Conn) = nullptr;
			GetDestroyedStartupOrDormantActorGUIDs(Conn).Remove(PriorityActor.DestructionInfo->NetGUID);
			continue;
		}*/

		UActorChannel* Channel = PriorityActor.Channel;
		if (!Channel || Channel->Actor) [[likely]]
		{
			if (!Channel) [[unlikely]]
			{
				//Channel = CreateChannelByName(Conn, &ActorFName, EChannelCreateFlags::OpenedLocally, -1);
				static auto ActorFName = (FName*)(Sarah::Offsets::ImageBase + 0x1197d498);
				Channel = CreateChannelByName(Conn, ActorFName, EChannelCreateFlags::OpenedLocally, -1);


				if (Channel) [[likely]]
					SetChannelActor(Channel, Actor, 0);
			}

			if (Channel) [[likely]]
			{
				//if (PriorityActor.bRelevant) [[unlikely]]
				//	GetRelevantTime(Channel) = GetElapsedTime(Driver) + 0.5 * FRand();
				if (Actor->NetDormancy > ENetDormancy::DORM_Awake && !Channel->bPendingDormancy && !Channel->Dormant) [[unlikely]]
					StartBecomingDormant(Channel);

				if (IsNetReady(Channel, false)) [[likely]]
				{
					if (ReplicateActor(Channel)) [[unlikely]]
					{
						//printf("rep %s\n", ActorInfo->GetName().c_str());
						/*auto TimeSeconds = GetTimeSeconds(Driver->World);
						const float MinOptimalDelta = 1.f / ActorInfo->NetUpdateFrequency;
						const float MaxOptimalDelta = max(1.f / ActorInfo->MinNetUpdateFrequency, MinOptimalDelta);
						const float DeltaBetweenReplications = float(TimeSeconds - ActorInfo->LastNetReplicateTime);

						ActorInfo->OptimalNetUpdateDelta = clamp(DeltaBetweenReplications * 0.7f, MinOptimalDelta, MaxOptimalDelta);
						ActorInfo->LastNetReplicateTime = TimeSeconds;*/
					}
				}
				else
					Actor->ForceNetUpdate();

				if (!IsNetReady(Conn, false)) [[unlikely]]
					return i;
			}
		}
	}

	return PriorityActors.Num();
}

void Replication::ServerReplicateActors(UNetDriver* Driver, float DeltaTime)
{

	GetReplicationFrame(Driver)++;

	BuildViewerMap(Driver);
	if (ViewerMap.Num() == 0)
		return;

	MoveDynamicActors();

	BuildPriorityLists(Driver, DeltaTime);

	auto WorldSettings = Driver->World->PersistentLevel->WorldSettings;
	void (*&CloseConnection)(UNetConnection*) = decltype(CloseConnection)(ReplicationOffsets::CloseConnection);	
	int64(*&SendDestructionInfo)(UNetDriver*, UNetConnection*, FActorDestructionInfo*) = decltype(SendDestructionInfo)(ReplicationOffsets::SendDestructionInfo);



	for (auto& PriorityListPair : PriorityLists)
	{
		auto Conn = PriorityListPair.Conn;

		if (!Conn->ViewTarget) [[unlikely]]
			continue;


		auto& DestructionList = PerConnDestructionInfoMap[Conn];
		for (int i = 0; i < DestructionList.Num(); i++)
		{
			auto& DestructionInfo = DestructionList[i];
			if (DestructionInfo->StreamingLevelName == FName(0) || GetClientVisibleLevelNames(Conn).Contains(DestructionInfo->StreamingLevelName))
			{
				//printf("Destruct %d\n", DestructionInfo->NetGUID.Value);
				GetRepContextLevel(Conn) = DestructionInfo->Level.Get();

				SendDestructionInfo(Driver, Conn, DestructionInfo);

				GetRepContextLevel(Conn) = nullptr;
				DestructionList.Remove(i);
				i--;
			}
		}

		auto& Viewers = PriorityListPair.ViewerList;

		for (auto& Viewer : Viewers)
		{
			if (Viewer.Connection->PlayerController) [[likely]]
				SendClientAdjustment(Viewer.Connection->PlayerController);
		}

		auto& ConnViewers = WorldSettings->ReplicationViewers = Viewers;
		auto& PriorityActors = PriorityListPair.PriorityList;

		ProcessPrioritizedActors(Driver, Conn, PriorityActors);

		PriorityActors.Free();



		//GetLastProcessedFrame(Conn) = GetReplicationFrame(Driver);

		if (GetPendingCloseDueToReplicationFailure(Conn))
			CloseConnection(Conn);
	}

	RepGraphFrame++;
	/*for (auto& ViewerPair : ViewerMap)
		ViewerPair.Second.Free();

	ViewerMap.ResetNum();*/
}

void Replication::RemoveNetworkActor(UNetDriver* Driver, AActor* Actor)
{
	RemoveFromArray(Actor);
}


void Replication::AddNetworkActor(UNetDriver* Driver, AActor* Actor)
{
	if (!Actor)
		return;

	FindOrAddToArray(Actor);
}

void Replication::ForceNetUpdate(UNetDriver* Driver, AActor* Actor)
{
	for (auto& ActorInfo : GlobalActorInfos)
	{
		if (ActorInfo->Actor == Actor)
		{
			ActorInfo->ForceNetUpdateFrame = RepGraphFrame;
			break;
		}
	}
}

void (*AddDestructionInfoOG)(UNetConnection* Conn, FActorDestructionInfo* DestructionInfo);
void AddDestructionInfo(UNetConnection* Conn, FActorDestructionInfo* DestructionInfo)
{
	PerConnDestructionInfoMap[Conn].Add(DestructionInfo);
	AddDestructionInfoOG(Conn, DestructionInfo);
}

void (*RemoveDestructionInfoOG)(UNetConnection* Conn, FActorDestructionInfo* DestructionInfo);
void RemoveDestructionInfo(UNetConnection* Conn, FActorDestructionInfo* DestructionInfo)
{
	auto& DestructionList = PerConnDestructionInfoMap[Conn];
	for (int i = 0; i < DestructionList.Num(); i++)
	{
		if (DestructionList[i]->NetGUID == DestructionInfo->NetGUID)
		{
			DestructionList.Remove(i);
			break;
		}
	}
	RemoveDestructionInfoOG(Conn, DestructionInfo);
}

void Replication::Hook()
{
	Utils::Hook(Sarah::Offsets::ImageBase + 0x6540BF4, AddDestructionInfo, AddDestructionInfoOG);
	Utils::Hook(Sarah::Offsets::ImageBase + 0x636C7DC, RemoveDestructionInfo, RemoveDestructionInfoOG);
	//Utils::Hook(ReplicationOffsets::RemoveNetworkActor, RemoveNetworkActor, RemoveNetworkActorOG);
	//Utils::Hook(Sarah::Offsets::ImageBase + 0x1179BAC, AddNetworkActor, AddNetworkActorOG);
}