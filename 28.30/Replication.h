#pragma once
#include "pch.h"
#include "Utils.h"

struct FNetworkObjectInfo
{
	class SDK::AActor* Actor;

	TWeakObjectPtr<class SDK::AActor> WeakActor;

	double NextUpdateTime;

	double LastNetReplicateTime;

	float OptimalNetUpdateDelta;

	double LastNetUpdateTimestamp;

	TSet<TWeakObjectPtr<class SDK::UNetConnection>> DormantConnections;

	TSet<TWeakObjectPtr<class SDK::UNetConnection>> RecentlyDormantConnections;

	uint8 bPendingNetUpdate : 1;

	uint8 bDirtyForReplay : 1;

	uint8 bSwapRolesOnReplicate : 1;

	uint32 ForceRelevantFrame = 0;
};


template< class ObjectType>
class TSharedPtr
{
public:
	ObjectType* Object;

	int32 SharedReferenceCount;
	int32 WeakReferenceCount;

	FORCEINLINE ObjectType* Get()
	{
		return Object;
	}
	FORCEINLINE ObjectType* Get() const
	{
		return Object;
	}
	FORCEINLINE ObjectType& operator*()
	{
		return *Object;
	}
	FORCEINLINE const ObjectType& operator*() const
	{
		return *Object;
	}
	FORCEINLINE ObjectType* operator->()
	{
		return Object;
	}
	FORCEINLINE ObjectType* operator->() const
	{
		return Object;
	}
};

class FNetworkObjectList
{
public:
	using FNetworkObjectSet = TSet<TSharedPtr<FNetworkObjectInfo>>;

	FNetworkObjectSet AllNetworkObjects;
	FNetworkObjectSet ActiveNetworkObjects;
	FNetworkObjectSet ObjectsDormantOnAllConnections;

	TMap<TWeakObjectPtr<UNetConnection>, int32> NumDormantObjectsPerConnection;

	void Remove(AActor* const Actor);
};

struct alignas(0x4) FServerFrameInfo
{
	int32 LastProcessedInputFrame = -1;
	int32 LastLocalFrame = -1;
	int32 LastSentLocalFrame = -1;

	float TargetTimeDilation = 1.f;
	int8 QuantizedTimeDilation = 1;
	float TargetNumBufferedCmds = 1.f;
	bool bFault = true;
};

class FNetworkGUID
{
public:

	uint32 Value;

public:

	FNetworkGUID()
		: Value(0)
	{
	}

	FNetworkGUID(uint32 V)
		: Value(V)
	{
	}

public:

	friend bool operator==(const FNetworkGUID& X, const FNetworkGUID& Y)
	{
		return (X.Value == Y.Value);
	}

	friend bool operator!=(const FNetworkGUID& X, const FNetworkGUID& Y)
	{
		return (X.Value != Y.Value);
	}
};


struct FActorDestructionInfo
{
public:
	FActorDestructionInfo()
		: Reason(0)
		, bIgnoreDistanceCulling(false)
	{
	}

	TWeakObjectPtr<class SDK::ULevel> Level;
	TWeakObjectPtr<class SDK::UObject> ObjOuter;
	struct SDK::FVector DestroyedPosition;
	class FNetworkGUID NetGUID;
	class SDK::FString PathName;
	class SDK::FName StreamingLevelName;
	uint8_t Reason;

	bool bIgnoreDistanceCulling;
};

template< class ObjectType>
class TUniquePtr
{
public:
	ObjectType* Ptr;

	FORCEINLINE ObjectType* Get()
	{
		return Ptr;
	}
	FORCEINLINE ObjectType* Get() const
	{
		return Ptr;
	}
	FORCEINLINE ObjectType& operator*()
	{
		return *Ptr;
	}
	FORCEINLINE const ObjectType& operator*() const
	{
		return *Ptr;
	}
	FORCEINLINE ObjectType* operator->()
	{
		return Ptr;
	}
	FORCEINLINE ObjectType* operator->() const
	{
		return Ptr;
	}
};

#define CLOSEPROXIMITY					500.
#define NEARSIGHTTHRESHOLD				2000.
#define MEDSIGHTTHRESHOLD				3162.
#define FARSIGHTTHRESHOLD				8000.
#define CLOSEPROXIMITYSQUARED			(CLOSEPROXIMITY*CLOSEPROXIMITY)
#define NEARSIGHTTHRESHOLDSQUARED		(NEARSIGHTTHRESHOLD*NEARSIGHTTHRESHOLD)
#define MEDSIGHTTHRESHOLDSQUARED		(MEDSIGHTTHRESHOLD*MEDSIGHTTHRESHOLD)
#define FARSIGHTTHRESHOLDSQUARED		(FARSIGHTTHRESHOLD*FARSIGHTTHRESHOLD)


struct FActorPriority
{
	float Priority;

	bool bRelevant;
	UActorChannel* Channel;
	AActor* Actor;

	FActorDestructionInfo* DestructionInfo;

	FActorPriority() :
		Priority(0), Actor(NULL), bRelevant(false), Channel(NULL), DestructionInfo(NULL)
	{
	}

	FActorPriority(UNetConnection* InConnection, UActorChannel* InChannel, AActor* InActor, bool InRelevant, const TArray<FNetViewer>& Viewers, float InPriority)
		: Actor(InActor), Channel(InChannel), bRelevant(InRelevant), DestructionInfo(NULL), Priority(InPriority)
	{
		//Priority = Actor->Index;
		/*const auto Time = float(Channel ? (*(double*)(__int64(InConnection->Driver) + 0x230) - *(double*)(__int64(InChannel) + 0x80)) : InConnection->Driver->SpawnPrioritySeconds);

		Priority = 0;
		for (int32 i = 0; i < Viewers.Num(); i++)
		{
			float (*GetNetPriority)(AActor*, const FVector&, const FVector&, class AActor*, AActor*, UActorChannel*, float, bool) = decltype(GetNetPriority)(Actor->VTable[0x87]);
			auto ThisPrio = (int32)round(65536.0f * GetNetPriority(Actor, Viewers[i].ViewLocation, Viewers[i].ViewDir, Viewers[i].InViewer, Viewers[i].ViewTarget, InChannel, Time, false));

			Priority = max(Priority, ThisPrio);
		}*/
	}

	FActorPriority(UNetConnection* InConnection, FActorDestructionInfo* DestructInfo, const TArray<FNetViewer>& Viewers)
		: Actor(NULL), bRelevant(false), Channel(NULL), DestructionInfo(DestructInfo)
	{
		Priority = 0;

		for (int32 i = 0; i < Viewers.Num(); i++)
		{
			float Time = InConnection->Driver->SpawnPrioritySeconds;

			FVector Dir = DestructionInfo->DestroyedPosition - Viewers[i].ViewLocation;
			double DistSq = Dir.SizeSquared();

			// adjust priority based on distance and whether actor is in front of viewer
			if ((Viewers[i].ViewDir | Dir) < 0.f)
			{
				if (DistSq > NEARSIGHTTHRESHOLDSQUARED)
					Time *= 0.2f;
				else if (DistSq > CLOSEPROXIMITYSQUARED)
					Time *= 0.4f;
			}
			else if (DistSq > MEDSIGHTTHRESHOLDSQUARED)
				Time *= 0.4f;

			Priority = max(Priority, int32(65536.0f * Time));
		}
	}

	bool operator>(FActorPriority& _Rhs) {
		return Priority > _Rhs.Priority;
	}
};

enum class EChannelCreateFlags : uint32_t
{
	None = (1 << 0),
	OpenedLocally = (1 << 1)
};

enum EConnectionState
{
	USOCK_Invalid = 0, // Connection is invalid, possibly uninitialized.
	USOCK_Closed = 1, // Connection permanently closed.
	USOCK_Pending = 2, // Connection is awaiting connection.
	USOCK_Open = 3, // Connection is open.
};

class ReplicationOffsets {
public:
	static inline uint32 TimeSeconds = 0x6b8;
	static inline uint32 ReplicationFrame = 0x468;
	static inline uint32 NetworkObjectList = 0x760;
	static inline uint32 ElapsedTime = 0x258;
	static inline uint32 RelevantTime = 0x78; // no
	static inline uint32 NetTag = 0x2f4; // no
	static inline uint32 DestroyedStartupOrDormantActorGUIDs = 0x14b8;
	static inline uint32 DestroyedStartupOrDormantActors = 0x328;
	static inline uint32 LastProcessedFrame = 0x208;// nein nein
	static inline uint32 ClientVisibleLevelNames = 0x1698;
	static inline uint32 ClientWorldPackageName = 0x1828;
	static inline uint32 RepContextActor = 0x1d58;
	static inline uint32 RepContextLevel = 0x1d60;
	static inline uint32 GuidCache = 0x160;
	static inline uint32 PendingCloseDueToReplicationFailure = 0x1cbe;
	static inline uint32 ServerFrameInfo = 0x7fc;
	static inline uint32 State = 0x134;

	static inline uint32 IsNetRelevantForVft = 0x9e;
	static inline uint32 IsRelevancyOwnerForVft = 0xa0;
	static inline uint32 GetNetOwnerVft = 0xa4;

	static inline uint64 GetViewTarget;
	static inline uint64 CallPreReplication;
	static inline uint64 SendClientAdjustment;
	static inline uint64 CreateChannelByName;
	static inline uint64 SetChannelActor;
	static inline uint64 ReplicateActor;
	static inline uint64 RemoveNetworkActor;
	static inline uint64 ClientHasInitializedLevelFor;
	static inline uint64 FindActorChannelRef;
	static inline uint64 CloseActorChannel;
	static inline uint64 StartBecomingDormant;
	static inline uint64 SupportsObject;
	static inline uint64 GetArchetype;
	static inline uint64 IsNetReady;
	static inline uint64 CloseConnection;
	static inline uint64 SendDestructionInfo;

	static void Init()
	{
		GetViewTarget = Sarah::Offsets::ImageBase + 0x11835F4;
		CallPreReplication = Sarah::Offsets::ImageBase + 0x6182860; // done
		SendClientAdjustment = Sarah::Offsets::ImageBase + 0x62B214C;
		CreateChannelByName = Sarah::Offsets::ImageBase + 0x1AF5B88;
		SetChannelActor = Sarah::Offsets::ImageBase + 0x168E644;
		ReplicateActor = Sarah::Offsets::ImageBase + 0x6347D98;
		RemoveNetworkActor = Sarah::Offsets::ImageBase + 0xF6F28C;
		ClientHasInitializedLevelFor = Sarah::Offsets::ImageBase + 0x7fbb170;
		FindActorChannelRef = Sarah::Offsets::ImageBase + 0xF6D698;
		CloseActorChannel = Sarah::Offsets::ImageBase + 0x6332CC4;
		StartBecomingDormant = Sarah::Offsets::ImageBase + 0x634D728;
		SupportsObject = Sarah::Offsets::ImageBase + 0xfc76a0;
		GetArchetype = Sarah::Offsets::ImageBase + 0xdacc40;
		IsNetReady = Sarah::Offsets::ImageBase + 0x654B2E8;
		CloseConnection = Sarah::Offsets::ImageBase + 0x17fb490;
		SendDestructionInfo = Sarah::Offsets::ImageBase + 0x6575670;
	}
};

class Replication {
public:

	static float& GetTimeSeconds(UWorld* World)
	{
		return *(float*)(__int64(World) + ReplicationOffsets::TimeSeconds);
	}
	static int& GetReplicationFrame(UNetDriver* Driver)
	{
		return *(int*)(__int64(Driver) + ReplicationOffsets::ReplicationFrame);
	}
	static FNetworkObjectList& GetNetworkObjectList(UNetDriver* Driver)
	{
		return *(*(class TSharedPtr<FNetworkObjectList>*)(__int64(Driver) + ReplicationOffsets::NetworkObjectList));
	}
	static double& GetElapsedTime(UNetDriver* Driver)
	{
		return *(double*)(__int64(Driver) + ReplicationOffsets::ElapsedTime);
	}
	static AActor* GetViewTarget(APlayerController* Controller)
	{
		return (decltype(&GetViewTarget)(ReplicationOffsets::GetViewTarget))(Controller);
	}
	static double& GetRelevantTime(UActorChannel* Channel)
	{
		return *(double*)(__int64(Channel) + ReplicationOffsets::RelevantTime);
	}
	static int32& GetNetTag(UNetDriver* Driver)
	{
		return *(int32*)(__int64(Driver) + ReplicationOffsets::NetTag);
	}
	static TSet<FNetworkGUID>& GetDestroyedStartupOrDormantActorGUIDs(UNetConnection* Conn)
	{
		return *(TSet<FNetworkGUID>*)(__int64(Conn) + ReplicationOffsets::DestroyedStartupOrDormantActorGUIDs);
	}
	static TMap<FNetworkGUID, TUniquePtr<FActorDestructionInfo>>& GetDestroyedStartupOrDormantActors(UNetDriver* Driver)
	{
		return *(TMap<FNetworkGUID, TUniquePtr<FActorDestructionInfo>>*)(__int64(Driver) + ReplicationOffsets::DestroyedStartupOrDormantActors);
	}
	static uint32& GetLastProcessedFrame(UNetConnection* Conn)
	{
		return *(uint32*)(__int64(Conn) + ReplicationOffsets::LastProcessedFrame);
	}
	static TSet<FName>& GetClientVisibleLevelNames(UNetConnection* Conn)
	{
		return *(TSet<FName>*)(__int64(Conn) + ReplicationOffsets::ClientVisibleLevelNames);
	}
	static FName& GetClientWorldPackageName(UNetConnection* Conn)
	{
		return *(FName*)(__int64(Conn) + ReplicationOffsets::ClientWorldPackageName);
	}
	static ULevel*& GetRepContextLevel(UNetConnection* Conn)
	{
		return *(ULevel**)(__int64(Conn) + ReplicationOffsets::RepContextLevel);
	}
	static class TSharedPtr<void*>& GetGuidCache(UNetDriver* Driver)
	{
		return *(class TSharedPtr<void*>*)(__int64(Driver) + ReplicationOffsets::GuidCache);
	}
	static bool& GetPendingCloseDueToReplicationFailure(UNetConnection* Conn)
	{
		return *(bool*)(__int64(Conn) + ReplicationOffsets::PendingCloseDueToReplicationFailure);
	}
	static EConnectionState& GetState(UNetConnection* Conn)
	{
		return *(EConnectionState*)(__int64(Conn) + ReplicationOffsets::State);
	}

private:
	static void BuildViewerMap(UNetDriver*);
	static void BuildPriorityLists(UNetDriver*, float);
	static bool IsActorRelevantToConnection(const AActor* Actor, const TArray<FNetViewer>& ConnectionViewers);
	static FNetViewer ConstructNetViewer(UNetConnection* NetConnection);
	static bool IsNetReady(UNetConnection* Connection, bool bSaturate);
	static bool IsNetReady(UChannel* Channel, bool bSaturate);
	static int ProcessPrioritizedActors(UNetDriver* Driver, UNetConnection* Conn, TArray<FActorPriority>&);
public:
	static void SendClientAdjustment(APlayerController*);
	static void ServerReplicateActors(UNetDriver*, float);
	DefHookOg(void, RemoveNetworkActor, UNetDriver*, AActor*);
	DefHookOg(void, AddNetworkActor, UNetDriver*, AActor*);
	static void ForceNetUpdate(UNetDriver*, AActor*);

	InitHooks;
};