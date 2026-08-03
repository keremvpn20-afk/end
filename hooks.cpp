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

        // 3. PAGE-BY-PAGE SAFE HEAP SCANNER (YENI VE EN GUVENLI YONTEM!)
        // iOS Kernel (mach_vm_read) kullanarak cok guvenli bir tarama yapacagiz.
        // Asla crash vermez, cunku sadece kernel tarafindan onaylanmis, okunabilir RAM sayfalari (page) taranir.
        // Ayrica Minecraft'ta pozisyonlar (BlockPos) float degil, INT (Tam Sayi) olarak tutulur!
        // Eski kod bu yuzden degerleri sacma sapan floatlara cevirip bulamiyordu.
        
        static auto lastScanTime = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        
        // Listeyi saniyede 1 kere yenile (Ekrandaki cizimler saniyede 60 kere güncellenir, sadece liste yenilemesi 1 sn)
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastScanTime).count() > 1000) {
            lastScanTime = now;
            
            std::vector<MappedContainer> temp;
            SDK::Vector3 playerPos = SDK::GetPlayerPosition(localPlayer);
            
            if (playerPos.y < -64.f || playerPos.y > 320.f) {
                sCalibratedOffset = 0; 
                return;
            }

            int pX = (int)std::floor(playerPos.x);
            int pY = (int)std::floor(playerPos.y);
            int pZ = (int)std::floor(playerPos.z);

            // 16 MB tarama alani (8 MB gerisi, 8 MB ilerisi)
            uintptr_t scanRadius = 0x800000;
            // iOS bellek sayfalari 16KB'dir (0x4000). Player adresini en yakin 16KB baslangicina yuvarla
            uintptr_t scanStart = (localPlayer & ~0x3FFFULL) - scanRadius; 
            uintptr_t scanEnd   = (localPlayer & ~0x3FFFULL) + scanRadius;

            // 16KB gecici tampon bellek
            uint8_t pageBuf[0x4000];
            vm_size_t pageSize = 0x4000;

            for (uintptr_t page = scanStart; page < scanEnd; page += pageSize) {
                vm_size_t readCount = pageSize;
                
                // KERNEL'e sor: "Bu 16KB'lik bellek okunabilir mi?" Okunabilirse pageBuf'a kopyala.
                if (vm_read_overwrite(mach_task_self(), (vm_address_t)page, pageSize, (vm_address_t)pageBuf, &readCount) == KERN_SUCCESS) {
                    
                    // 16KB icinde 8'er byte atlayarak tara
                    for (uintptr_t offset = 0; offset < pageSize - 0x40; offset += 8) {
                        
                        // +0x24: BlockEntityType (int)
                        int type = *(int*)(pageBuf + offset + 0x24);
                        if (type == 1 || type == 2 || type == 6 || type == 8 || type == 10 || type == 15) {
                            
                            // +0x2C: BlockPos (int x, int y, int z) -> Minecraft sandik pozisyonlarini INT tutar!
                            int bx = *(int*)(pageBuf + offset + 0x2C + 0);
                            int by = *(int*)(pageBuf + offset + 0x2C + 4);
                            int bz = *(int*)(pageBuf + offset + 0x2C + 8);
                            
                            if (by > -64 && by < 320) {
                                int dx = std::abs(bx - pX);
                                int dy = std::abs(by - pY);
                                int dz = std::abs(bz - pZ);
                                
                                // Eger sandik oyuncuya 100 bloktan yakinsa (abs = mutlak deger)
                                if (dx < 100 && dy < 100 && dz < 100) {
                                    
                                    // Ekrana cizerken kutunun merkezine oturmasi icin +0.5 ekliyoruz
                                    SDK::Vector3 pos = {(float)bx + 0.5f, (float)by + 0.5f, (float)bz + 0.5f};
                                    float dist = playerPos.distance(pos);
                                    
                                    int mapped = -1;
                                    if      (type == 1  && filterChest)      mapped = 1;
                                    else if (type == 2  && filterEnderChest) mapped = 2;
                                    else if (type == 8  && filterHopper)     mapped = 3;
                                    else if (type == 6  && filterSpawner)    mapped = 4;
                                    else if (type == 10 && filterShulker)    mapped = 5;
                                    else if (type == 15 && filterBarrel)     mapped = 6;

                                    if (mapped != -1) {
                                        temp.push_back({ mapped, pos, dist });
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            std::lock_guard<std::mutex> lock(containerMutex);
            gScannedEntitiesCount = (int)temp.size();
            detectedContainers = std::move(temp);
            gDebugBlockSource = 0x7777; // Yepyeni Kernel Scanner isareti
            gDebugListSize = gScannedEntitiesCount;
        }
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
