#include "pch.h"
#include "Vehicles.h"

#include "Utils.h"

void Vehicles::SpawnVehicles()
{
    auto Spawners = Utils::GetAll<AFortAthenaLivingWorldStaticPointProvider>();
     UEAllocatedMap<FName, UClass*> VehicleSpawnerMap =
    {
        { FName(L"Athena.Vehicle.SpawnLocation.Valet.BasicCar.Taxi"), Utils::FindObject<UClass>(L"/Valet/TaxiCab/Valet_TaxiCab_Vehicle.Valet_TaxiCab_Vehicle_C") },
        { FName(L"Athena.Vehicle.SpawnLocation.Valet.BasicCar.Modded"), Utils::FindObject<UClass>(L"/ModdedBasicCar/Vehicle/Valet_BasicCar_Vehicle_SuperSedan.Valet_BasicCar_Vehicle_SuperSedan_C") },
        { FName(L"Athena.Vehicle.SpawnLocation.Valet.BasicTruck.Upgraded"), Utils::FindObject<UClass>(L"/Valet/BasicTruck/Valet_BasicTruck_Vehicle_Upgrade.Valet_BasicTruck_Vehicle_Upgrade_C") },
        { FName(L"Athena.Vehicle.SpawnLocation.Valet.BigRig.Upgraded"), Utils::FindObject<UClass>(L"/Valet/BigRig/Valet_BigRig_Vehicle_Upgrade.Valet_BigRig_Vehicle_Upgrade_C") },
        { FName(L"Athena.Vehicle.SpawnLocation.Valet.SportsCar.Upgraded"), Utils::FindObject<UClass>(L"/Valet/SportsCar/Valet_SportsCar_Vehicle_Upgrade.Valet_SportsCar_Vehicle_Upgrade_C") },
        { FName(L"Athena.Vehicle.SpawnLocation.Valet.BasicCar.Upgraded"), Utils::FindObject<UClass>(L"/Valet/BasicCar/Valet_BasicCar_Vehicle_Upgrade.Valet_BasicCar_Vehicle_Upgrade_C") }
    };

    for (auto& Spawner : Spawners)
    {
        UClass* VehicleClass = nullptr;
        for (auto& Tag : Spawner->FiltersTags.GameplayTags)
        {
            if (VehicleSpawnerMap.contains(Tag.TagName))
            {
                VehicleClass = VehicleSpawnerMap[Tag.TagName];
                break;
            }
        }
        if (VehicleClass)
        {
            Utils::SpawnActor<AFortAthenaVehicle>(VehicleClass, Spawner->K2_GetActorLocation(), Spawner->K2_GetActorRotation());
        }
        else {
            for (auto& Tag : Spawner->FiltersTags.GameplayTags)
            {
                Log(L"Fix: Tag: %s", Tag.TagName.ToWString().c_str());
            }
            Log(L"");
        }
    }

    Spawners.Free();

    Log(L"Spawned vehicles");
}

void Vehicles::ServerMove(UObject* Context, FFrame& Stack)
{
    FReplicatedPhysicsPawnState State;
    Stack.StepCompiledIn(&State);
    Stack.IncrementCode();
    auto Pawn = (AFortPhysicsPawn*)Context;

    UPrimitiveComponent* RootComponent = Pawn->RootComponent->Cast<UPrimitiveComponent>();

    if (RootComponent)
    {
        FRotator RealRotation = State.Rotation.Rotator();

        RealRotation.Yaw = FRotator::UnwindDegrees(RealRotation.Yaw);

        RealRotation.pitch = 0;
        RealRotation.Roll = 0;

        RootComponent->K2_SetWorldLocationAndRotation(State.Translation, RealRotation, false, nullptr, true);
        RootComponent->SetPhysicsLinearVelocity(State.LinearVelocity, 0, FName(0));
        RootComponent->SetPhysicsAngularVelocityInDegrees(State.AngularVelocity, 0, FName(0));
    }
}

void Vehicles::Hook()
{
    Utils::ExecHook(L"/Script/FortniteGame.FortPhysicsPawn.ServerMove", ServerMove);
    Utils::ExecHook(L"/Script/FortniteGame.FortPhysicsPawn.ServerMoveReliable", ServerMove);
}
