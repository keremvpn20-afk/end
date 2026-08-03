#pragma once
#include "sdk.hpp"
#include <vector>
#include <mutex>

namespace Hooks {
    // Config properties shared with UI Menu and Drawing Overlay
    extern bool storageEspEnabled;
    
    // Detailed Filter Options inside the "..." sub-settings menu
    extern bool filterChest;
    extern bool filterEnderChest;
    extern bool filterShulker;
    extern bool filterHopper;
    extern bool filterSpawner;
    extern bool filterBarrel;
    
    // Draw Tracers setting inside the "..." sub-settings menu
    extern bool drawTracers;

    // Struct passing spatial coordinates to the CoreGraphics layer
    struct MappedContainer {
        int type; // 1: Chest, 2: EnderChest, 3: Hopper, 4: Spawner, 5: Shulker, 6: Barrel
        SDK::Vector3 worldPos;
        float distance;
    };

    extern std::vector<MappedContainer> detectedContainers;
    extern std::mutex containerMutex;

    // Debugging properties
    extern uintptr_t gTickAddressResolved;
    extern int gScannedEntitiesCount;
    extern SDK::Player* gLocalPlayer;
    extern uintptr_t gDebugBlockSource;
    extern int gDebugListSize;

    // Setup hooks
    void Initialize();
    void Terminate();
    void ProcessContainerScanning(SDK::Player* localPlayer);
}
