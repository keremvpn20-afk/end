#pragma once
#include "sdk.hpp"
#include <vector>
#include <mutex>

namespace Hooks {
    extern bool storageEspEnabled;
    extern bool filterChest;
    extern bool filterEnderChest;
    extern bool filterShulker;
    extern bool filterHopper;
    extern bool filterSpawner;
    extern bool filterBarrel;
    extern bool drawTracers;

    struct MappedContainer {
        int type; // 1:Chest 2:EnderChest 3:Hopper 4:Spawner 5:Shulker 6:Barrel
        SDK::Vector3 worldPos;
        float distance;
    };

    extern std::vector<MappedContainer> detectedContainers;
    extern std::mutex containerMutex;

    extern uintptr_t gTickAddressResolved;
    extern int gScannedEntitiesCount;
    extern SDK::Player* gLocalPlayer;

    void Initialize();
    void Terminate();
    void ProcessContainerScanning(SDK::Player* localPlayer);
}
