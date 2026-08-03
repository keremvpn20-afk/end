#include "hooks.hpp"
#include "sdk.hpp"
#include "memory.hpp"
#include <mutex>

namespace Hooks {

    bool storageEspEnabled = false;
    bool filterChest       = true;
    bool filterEnderChest  = true;
    bool filterShulker     = true;
    bool filterHopper      = true;
    bool filterSpawner     = true;
    bool filterBarrel      = true;
    bool drawTracers       = false;

    std::vector<MappedContainer> detectedContainers;
    std::mutex containerMutex;

    // Not used for hooking anymore - just updated by scanner
    SDK::Player* gLocalPlayer = nullptr;

    // gTickAddressResolved meanings:
    //   0           = scanner never ran
    //   0xA608634   = scanner ran, ClientInstance found
    //   0xDEAD      = scanner ran, ClientInstance NOT found (wrong offsets or not in world)
    uintptr_t gTickAddressResolved = 0;
    int gScannedEntitiesCount = 0;
    uintptr_t gDebugBlockSource = 0;
    int gDebugListSize = 0;

    // ── Auto-calibrated LocalPlayer offset ───────────────────────────────────
    // We don't know the exact offset of LocalPlayer* inside ClientInstance for v1.26.33.
    // So we probe every 8 bytes from 0x180 to 0x280, read the candidate pointer,
    // then read what would be the Actor position (at +0x4C0).
    // A valid player position has: x != 0, y in [-64, 320], z != 0.
    // The first match is cached and reused on subsequent frames.
    static uintptr_t sCalibratedOffset = 0; // 0 = not yet found

    static uintptr_t FindLocalPlayerOffset(uintptr_t clientInstance) {
        // Binary disassembly shows heavy activity at ClientInstance+0x200/0x208/0x210.
        // We probe 0x150 → 0x280 in 8-byte steps and validate against world coords.
        for (uintptr_t off = 0x150; off <= 0x280; off += 8) {
            uintptr_t candidate = SDK::SafeRead<uintptr_t>(clientInstance + off);
            if (!SDK::IsValidPtr(candidate)) continue;

            // Read position at Actor::pos (offset 0x4C0 in Player)
            float posX = SDK::SafeRead<float>(candidate + 0x4C0 + 0);
            float posY = SDK::SafeRead<float>(candidate + 0x4C0 + 4);
            float posZ = SDK::SafeRead<float>(candidate + 0x4C0 + 8);

            // Strict Minecraft world bounds: y in [-64,320], x/z in ±30M, non-zero
            if (posY > -64.f && posY < 320.f &&
                posX > -30000000.f && posX < 30000000.f &&
                posZ > -30000000.f && posZ < 30000000.f &&
                (posX != 0.f || posZ != 0.f)) {
                printf("[yt] ✓ LocalPlayer* @ ClientInstance+0x%llX (%.1f, %.1f, %.1f)\n",
                       (unsigned long long)off, posX, posY, posZ);
                return off;
            }
        }
        return 0;
    }

    struct BlockSourceLayout {
        uintptr_t bsOffset;
        uintptr_t vecOffset;
    };
    static BlockSourceLayout sLayout = {0, 0};

    // Dinamik olarak hem BlockSource offsetini hem de icindeki BlockEntity vector offsetini bulur.
    static BlockSourceLayout FindBlockSourceLayout(uintptr_t localPlayer) {
        // Player objesi icindeki 0x320-0x3C0 araligini tara (Region/Dimension pointer'lari buradadir)
        for (uintptr_t off = 0x320; off <= 0x3C0; off += 8) {
            uintptr_t candidate = SDK::SafeRead<uintptr_t>(localPlayer + off);
            if (!SDK::IsValidPtr(candidate)) continue;

            // Aday pointer'in (BlockSource?) icindeki 0x30-0x70 araligindaki std::vector'leri tara
            for (uintptr_t vecOff = 0x30; vecOff <= 0x70; vecOff += 8) {
                uintptr_t vecBegin = SDK::SafeRead<uintptr_t>(candidate + vecOff);
                uintptr_t vecEnd   = SDK::SafeRead<uintptr_t>(candidate + vecOff + 8);
                uintptr_t vecCap   = SDK::SafeRead<uintptr_t>(candidate + vecOff + 16);

                // std::vector bellek yapisi: [begin] <= [end] <= [capacity]
                if (vecBegin != 0 && vecEnd >= vecBegin && vecCap >= vecEnd) {
                    if (SDK::IsValidPtr(vecBegin)) {
                        size_t count = (vecEnd - vecBegin) / 8;
                        // Sandiklar/Esyalar genelde 0-5000 arasidir. Cok buyukse coptur.
                        if (count > 0 && count < 10000) {
                            // Onumuzde Ender Chest oldugu icin liste kesinlikle 0'dan buyuk olmali!
                            printf("[yt] ✓ BULUNDU! BlockSource offset: 0x%lX, Vector offset: 0x%lX (Kutu sayisi: %zu)\n", 
                                   (unsigned long)off, (unsigned long)vecOff, count);
                            return {off, vecOff};
                        }
                    }
                }
            }
        }
        return {0, 0};
    }

    void ProcessContainerScanning(SDK::Player* /*unused*/) {
        uintptr_t base = Memory::GetBaseAddress();

        // 1. Global slot'tan ClientInstance oku
        uintptr_t clientInstance = SDK::GetClientInstance(base);
        if (!SDK::IsValidPtr(clientInstance)) {
            gTickAddressResolved = 0xDEAD;
            return;
        }

        // 2. Otomatik kalibrasyon (LocalPlayer)
        if (sCalibratedOffset == 0)
            sCalibratedOffset = FindLocalPlayerOffset(clientInstance);

        if (sCalibratedOffset == 0) {
            gTickAddressResolved = 0xBEEF;
            return;
        }

        uintptr_t localPlayer = SDK::SafeRead<uintptr_t>(clientInstance + sCalibratedOffset);
        if (!SDK::IsValidPtr(localPlayer)) {
            sCalibratedOffset = 0; 
            gTickAddressResolved = 0xDEAD;
            return;
        }

        gLocalPlayer = (SDK::Player*)localPlayer;
        gTickAddressResolved = sCalibratedOffset; 

        if (!storageEspEnabled) {
            std::lock_guard<std::mutex> lock(containerMutex);
            detectedContainers.clear();
            gScannedEntitiesCount = 0;
            return;
        }

        // 3. BlockSource ve Vector kalibrasyonu
        if (sLayout.bsOffset == 0) {
            sLayout = FindBlockSourceLayout(localPlayer);
        }
        
        if (sLayout.bsOffset == 0) {
            // Eger 0 ise henuz etrafinda hic chest vs yok veya bulamadi demektir.
            return;
        }

        uintptr_t blockSource = SDK::SafeRead<uintptr_t>(localPlayer + sLayout.bsOffset);
        gDebugBlockSource = sLayout.bsOffset; // Ekrana buldugu ofseti yazdir (0x358 veya 0x368 gibi kisa offset)
        
        if (!SDK::IsValidPtr(blockSource)) {
            sLayout = {0, 0}; // Baglanti koparsa tekrar tara
            return;
        }

        // Vector okuma islemi
        uintptr_t vecBegin = SDK::SafeRead<uintptr_t>(blockSource + sLayout.vecOffset);
        uintptr_t vecEnd   = SDK::SafeRead<uintptr_t>(blockSource + sLayout.vecOffset + 8);
        
        size_t count = 0;
        if (vecEnd >= vecBegin && SDK::IsValidPtr(vecBegin)) {
            count = (vecEnd - vecBegin) / 8;
        }
        if (count > 2000) count = 2000;
        
        gDebugListSize = (int)count;

        std::vector<SDK::BlockEntity*> entities;
        for (size_t i = 0; i < count; i++) {
            SDK::BlockEntity* be = SDK::SafeRead<SDK::BlockEntity*>(vecBegin + i * 8);
            if (be) entities.push_back(be);
        }

        SDK::Vector3 playerPos = SDK::GetPlayerPosition(localPlayer);
        std::vector<MappedContainer> temp;
        for (auto* be : entities) {
            if (!be) continue;
            SDK::Vector3 pos = be->getPosition();
            if (pos.y < -64.f || pos.y > 320.f) continue;
            float dist = playerPos.distance(pos);
            if (dist > 100.f) continue;

            int rawType = be->getType();
            int mappedType = -1;
            if      (rawType == 1  && filterChest)      mappedType = 1;
            else if (rawType == 2  && filterEnderChest) mappedType = 2;
            else if (rawType == 8  && filterHopper)     mappedType = 3;
            else if (rawType == 6  && filterSpawner)    mappedType = 4;
            else if (rawType == 10 && filterShulker)    mappedType = 5;
            else if (rawType == 15 && filterBarrel)     mappedType = 6;

            if (mappedType != -1)
                temp.push_back({ mappedType, pos, dist });
        }

        std::lock_guard<std::mutex> lock(containerMutex);
        gScannedEntitiesCount = (int)temp.size();
        detectedContainers = std::move(temp);
    }

    // ── No-op stubs (hooks removed, everything is read-only now) ────────────
    void Initialize() {
        // Nothing to hook. Scanner runs from overlay every frame.
        // Pre-warm: do one immediate scan to check if ClientInstance is accessible
        ProcessContainerScanning(nullptr);
        printf("[yt] Hook-free mode. ClientInstance global @ base+0x%llx. Status: %s\n",
               (unsigned long long)SDK::kClientInstancePtrOffset,
               (gTickAddressResolved == 0xA608634) ? "OK" : "NOT FOUND YET");
    }

    void Terminate() {}
}
