#pragma once
#include "pch.h"

#include "Utils.h"

class Vehicles
{
public:
    static void SpawnVehicles();
    InitHooks;
private:
    static void ServerMove(UObject* Context, FFrame& Stack);
};
