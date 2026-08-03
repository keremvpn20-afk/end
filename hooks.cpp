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
    SDK::Player* gLocalPlayer = nullptr;

    // gTickAddressResolved anlamlari:
    //   0        = hic taranmadi / Initialize cagrilmadi
    //   0xDEAD   = ClientInstance bos / bulunamadi
    //   0xBEEF   = ClientInstance var ama dunyaya girilmedi (kalibrasyon bekliyor)
    //   diger    = LocalPlayer offset degeri (HOOKED - calisiyor)
    uintptr_t gTickAddressResolved = 0;
    int gScannedEntitiesCount = 0;

    // Bir kez bulunan offset cache'lenir, her frame yeniden aranmaz
    static uintptr_t sCalibratedOffset = 0;

    // ClientInstance icinde LocalPlayer* offset'ini otomatik bulur.
    // 0x150 - 0x280 araligini 8 byte adimlarla tarar.
    // Gecerli Minecraft dunya koordinati (y: -64..320, x/z: +-30M) veren
    // ilk pointer'i dondurur. Hook yok, sadece okuma yapar.
    static uintptr_t FindLocalPlayerOffset(uintptr_t clientInstance) {
        for (uintptr_t off = 0x150; off <= 0x280; off += 8) {
            uintptr_t candidate = SDK::SafeRead<uintptr_t>(clientInstance + off);
            if (!SDK::IsValidPtr(candidate)) continue;

            float posX = SDK::SafeRead<float>(candidate + 0x4C0 + 0);
            float posY = SDK::SafeRead<float>(candidate + 0x4C0 + 4);
            float posZ = SDK::SafeRead<float>(candidate + 0x4C0 + 8);

            if (posY > -64.f && posY < 320.f &&
                posX > -30000000.f && posX < 30000000.f &&
                posZ > -30000000.f && posZ < 30000000.f &&
                (posX != 0.f || posZ != 0.f)) {
                printf("[yt] LocalPlayer @ ClientInstance+0x%llX (%.1f, %.1f, %.1f)\n",
                       (unsigned long long)off, posX, posY, posZ);
                return off;
            }
        }
        return 0;
    }

    // Her frame CADisplayLink tarafindan cagrilir.
    // Hic hook yok - sadece bellek okuma (iOS sideload uyumlu).
    void ProcessContainerScanning(SDK::Player* /*unused*/) {
        uintptr_t base = Memory::GetBaseAddress();

        // 1. Binary analizden bulunan global slot'tan ClientInstance* oku
        uintptr_t clientInstance = SDK::GetClientInstance(base);
        if (!SDK::IsValidPtr(clientInstance)) {
            gTickAddressResolved = 0xDEAD;
            return;
        }

        // 2. Otomatik kalibrasyon - ilk dunya girisi'nde bir kez calisir, cache'lenir
        if (sCalibratedOffset == 0)
            sCalibratedOffset = FindLocalPlayerOffset(clientInstance);

        if (sCalibratedOffset == 0) {
            gTickAddressResolved = 0xBEEF; // Dunyaya girilmedi
            return;
        }

        uintptr_t localPlayer = SDK::SafeRead<uintptr_t>(clientInstance + sCalibratedOffset);
        if (!SDK::IsValidPtr(localPlayer)) {
            sCalibratedOffset = 0; // Baglanti kesildi, sonraki giris icin sifirla
            gTickAddressResolved = 0xDEAD;
            return;
        }

        gLocalPlayer = (SDK::Player*)localPlayer;
        gTickAddressResolved = sCalibratedOffset; // Debug GUI: HOOKED goster

        if (!storageEspEnabled) {
            std::lock_guard<std::mutex> lock(containerMutex);
            detectedContainers.clear();
            gScannedEntitiesCount = 0;
            return;
        }

        // 3. BlockSource -> BlockEntity listesi tara
        uintptr_t blockSource = SDK::GetBlockSource(localPlayer);
        if (!SDK::IsValidPtr(blockSource)) return;

        SDK::BlockSource* region = (SDK::BlockSource*)blockSource;
        SDK::Vector3 playerPos = SDK::GetPlayerPosition(localPlayer);
        auto entities = region->getBlockEntities();

        std::vector<MappedContainer> temp;
        for (auto* be : entities) {
            if (!be) continue;
            SDK::Vector3 pos = be->getPosition();
            if (pos.y < -64.f || pos.y > 320.f) continue;
            float dist = playerPos.distance(pos);
            if (dist > 100.f) continue;

            int raw = be->getType();
            int mapped = -1;
            if      (raw == 1  && filterChest)      mapped = 1;
            else if (raw == 2  && filterEnderChest) mapped = 2;
            else if (raw == 8  && filterHopper)     mapped = 3;
            else if (raw == 6  && filterSpawner)    mapped = 4;
            else if (raw == 10 && filterShulker)    mapped = 5;
            else if (raw == 15 && filterBarrel)     mapped = 6;

            if (mapped != -1)
                temp.push_back({ mapped, pos, dist });
        }

        std::lock_guard<std::mutex> lock(containerMutex);
        gScannedEntitiesCount = (int)temp.size();
        detectedContainers = std::move(temp);
    }

    void Initialize() {
        // Hook yok. Scanner overlay'dan her frame cagrilir.
        // Ilk kontrol - ClientInstance erisimi var mi?
        ProcessContainerScanning(nullptr);
        printf("[yt] Hook-free mode aktif. ClientInstance global @ base+0x%llx\n",
               (unsigned long long)SDK::kClientInstancePtrOffset);
    }

    void Terminate() {}
}
