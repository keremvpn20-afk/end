#pragma once
#include <vector>
#include <cmath>

namespace SDK {

    struct Vector3 {
        float x, y, z;
        Vector3() : x(0), y(0), z(0) {}
        Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
        Vector3 operator+(const Vector3& o) const { return {x+o.x, y+o.y, z+o.z}; }
        Vector3 operator-(const Vector3& o) const { return {x-o.x, y-o.y, z-o.z}; }
        float distance(const Vector3& o) const {
            float dx=x-o.x, dy=y-o.y, dz=z-o.z;
            return sqrtf(dx*dx+dy*dy+dz*dz);
        }
    };

    struct Vector2 {
        float x, y;
        Vector2() : x(0), y(0) {}
        Vector2(float x, float y) : x(x), y(y) {}
    };

    struct Matrix { float m[16]; };

    inline bool WorldToScreen(const Vector3& pos, Vector2& screen,
                               const Matrix& mx, float w, float h) {
        float x = pos.x*mx.m[0] + pos.y*mx.m[4] + pos.z*mx.m[8]  + mx.m[12];
        float y = pos.x*mx.m[1] + pos.y*mx.m[5] + pos.z*mx.m[9]  + mx.m[13];
        float ww= pos.x*mx.m[3] + pos.y*mx.m[7] + pos.z*mx.m[11] + mx.m[15];
        if (ww < 0.1f) return false;
        screen.x = (w/2.f) + (x/ww)*(w/2.f);
        screen.y = (h/2.f) - (y/ww)*(h/2.f);
        return true;
    }

    // Binary analizden bulunan global singleton offset'leri (Minecraft v1.26.33)
    // DATA vmaddr 0x1101D3A10 -> runtime offset from binary base = 0x101D3A10
    static constexpr uintptr_t kClientInstancePtrOffset = 0x101D3A10;
    static constexpr uintptr_t kActorPosOffset          = 0x4C0;
    static constexpr uintptr_t kBlockSourceOffset       = 0x358;
    static constexpr uintptr_t kBlockEntityTypeOffset   = 0x24;
    static constexpr uintptr_t kBlockEntityPosOffset    = 0x2C;
    static constexpr uintptr_t kBSEntityListBegin       = 0x48;
    static constexpr uintptr_t kBSEntityListEnd         = 0x50;

    // Guvenli bellek okuma - null check ile
    template<typename T>
    inline T SafeRead(uintptr_t addr) {
        if (!addr) return T{};
        return *(T*)addr;
    }

    // Global slot'tan ClientInstance pointer'ini oku
    inline uintptr_t GetClientInstance(uintptr_t binaryBase) {
        return SafeRead<uintptr_t>(binaryBase + kClientInstancePtrOffset);
    }

    // LocalPlayer'dan pozisyon oku
    inline Vector3 GetPlayerPosition(uintptr_t localPlayer) {
        if (!localPlayer) return {};
        return SafeRead<Vector3>(localPlayer + kActorPosOffset);
    }

    // LocalPlayer'dan BlockSource pointer'i oku
    inline uintptr_t GetBlockSource(uintptr_t localPlayer) {
        if (!localPlayer) return 0;
        return SafeRead<uintptr_t>(localPlayer + kBlockSourceOffset);
    }

    // Heap pointer gecerlilik kontrolu (iOS arm64)
    inline bool IsValidPtr(uintptr_t p) {
        return p > 0x100000000ULL && p < 0x800000000ULL;
    }

    class BlockEntity {
    public:
        Vector3 getPosition() { return *(Vector3*)((uintptr_t)this + kBlockEntityPosOffset); }
        int     getType()     { return *(int*)((uintptr_t)this + kBlockEntityTypeOffset); }
    };

    class BlockSource {
    public:
        std::vector<BlockEntity*> getBlockEntities() {
            std::vector<BlockEntity*> list;
            uintptr_t s = *(uintptr_t*)((uintptr_t)this + kBSEntityListBegin);
            uintptr_t e = *(uintptr_t*)((uintptr_t)this + kBSEntityListEnd);
            if (!s || !e || e <= s) return list;
            size_t count = (e - s) / sizeof(void*);
            if (count > 2000) count = 2000;
            for (size_t i = 0; i < count; i++) {
                BlockEntity* be = *(BlockEntity**)(s + i*sizeof(void*));
                if (be) list.push_back(be);
            }
            return list;
        }
    };

    class Player {
    public:
        Vector3 getPosition()    { return *(Vector3*)((uintptr_t)this + kActorPosOffset); }
        BlockSource* getRegion() { return *(BlockSource**)((uintptr_t)this + kBlockSourceOffset); }
    };
}
