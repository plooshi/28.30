// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "Utils.h"
#include "Replication.h"
#include "Options.h"

void Main()
{
    Sarah::Offsets::Init();
    ReplicationOffsets::Init();
    UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), L"net.AllowEncryption 0", nullptr);
    *(bool*)(__int64(Sarah::Offsets::ImageBase) + 0x117E1128) = bIris; // use iris rep
#ifdef CLIENT
    UEngine::GetEngine()->GameViewport->ViewportConsole = (UConsole *) UGameplayStatics::SpawnObject(UEngine::GetEngine()->ConsoleClass, UEngine::GetEngine()->GameViewport);
    return;
#endif
    //ReplicationOffsets::Init();
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    SetConsoleTitleA("Sarah 28.30: Setting up");
    auto FrontEndGameMode = (AFortGameMode*)UWorld::GetWorld()->AuthorityGameMode;
    while (FrontEndGameMode->MatchState != UKismetStringLibrary::Conv_StringToName(L"InProgress"));
    //Sleep(3000);

    MH_Initialize();
    for (auto& HookFunc : _HookFuncs)
        HookFunc();
    MH_EnableHook(MH_ALL_HOOKS);

    srand((uint32_t)time(0));

    //*(bool*)(Sarah::Offsets::ImageBase + 0x106946e0) = true; // using iris
    UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), L"log LogNet Error", nullptr);
    *(bool*)Sarah::Offsets::GIsClient = false;
    *(bool*)(__int64(Sarah::Offsets::ImageBase) + 0x1164000D) = true;
    UWorld::GetWorld()->OwningGameInstance->LocalPlayers.Remove(0);
    UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), L"open Helios_Terrain", nullptr);
    //
    //UWorld::GetWorld()->OwningGameInstance->LocalPlayers[0]->PlayerController;
}

BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        thread(Main).detach();
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

