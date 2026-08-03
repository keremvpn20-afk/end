#pragma once
#include <stdint.h>
#include <vector>
#include <string>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <sys/mman.h>
#include <unistd.h>

#include <mach-o/loader.h>
#include <string.h>

namespace Memory {
    
    // Retrieves base address of the main executable segment (minecraftpe)
    inline uintptr_t GetBaseAddress() {
        return (uintptr_t)_dyld_get_image_header(0);
    }

    // Changes page memory permissions to execute read-write dynamically
    inline bool SetPageWritable(uintptr_t address, size_t size, bool writable) {
        uintptr_t pageSize = sysconf(_SC_PAGESIZE);
        uintptr_t pageStart = address & ~(pageSize - 1);
        uintptr_t pageEnd = (address + size + pageSize - 1) & ~(pageSize - 1);
        size_t protectSize = pageEnd - pageStart;

        int protection = PROT_READ | PROT_EXEC;
        if (writable) {
            protection |= PROT_WRITE;
        }

        return mprotect((void*)pageStart, protectSize, protection) == 0;
    }

    // Patches code segment safely by overriding page permissions
    inline bool Patch(uintptr_t address, const std::vector<uint8_t>& bytes) {
        if (!address || bytes.empty()) return false;

        if (!SetPageWritable(address, bytes.size(), true)) {
            return false;
        }

        memcpy((void*)address, bytes.data(), bytes.size());

        SetPageWritable(address, bytes.size(), false);
        return true;
    }

    // Dynamic signature scanner (pattern scanning) that reads only within mapped __TEXT segment boundaries
    inline uintptr_t FindSignature(const std::string& signature) {
        uintptr_t base = GetBaseAddress();
        
        const struct mach_header_64* header = (const struct mach_header_64*)base;
        if (header->magic != MH_MAGIC_64) return 0;

        // Parse signature string (e.g., "E1 03 00 AA ? ? ? ? 08 00 80 D2")
        std::vector<int> patternBytes;
        for (size_t i = 0; i < signature.length(); i++) {
            if (signature[i] == ' ') continue;
            if (signature[i] == '?') {
                patternBytes.push_back(-1); // Wildcard marker
                if (i + 1 < signature.length() && signature[i + 1] == '?') {
                    i++;
                }
            } else {
                std::string byteStr = signature.substr(i, 2);
                patternBytes.push_back((int)strtol(byteStr.c_str(), nullptr, 16));
                i++;
            }
        }

        uintptr_t scanStart = 0;
        size_t scanSize = 0;

        // Obtain vmaddr slide offset of image
        intptr_t slide = _dyld_get_image_vmaddr_slide(0);

        // Safely parse segment boundaries by traversing load commands
        const struct load_command* lc = (const struct load_command*)((uintptr_t)header + sizeof(struct mach_header_64));
        for (uint32_t i = 0; i < header->ncmds; i++) {
            if (lc->cmd == LC_SEGMENT_64) {
                const struct segment_command_64* seg = (const struct segment_command_64*)lc;
                if (strcmp(seg->segname, "__TEXT") == 0) {
                    scanStart = seg->vmaddr + slide;
                    scanSize = seg->vmsize;
                    break;
                }
            }
            lc = (const struct load_command*)((uintptr_t)lc + lc->cmdsize);
        }

        // Fallback safety limits if header query fails
        if (scanStart == 0 || scanSize == 0) {
            scanStart = base;
            scanSize = 25 * 1024 * 1024; 
        }

        const uint8_t* scanBytes = (const uint8_t*)scanStart;
        size_t patternSize = patternBytes.size();

        for (size_t i = 0; i < scanSize - patternSize; i++) {
            bool found = true;
            for (size_t j = 0; j < patternSize; j++) {
                if (patternBytes[j] != -1 && scanBytes[i + j] != patternBytes[j]) {
                    found = false;
                    break;
                }
            }
            if (found) {
                return (uintptr_t)(scanStart + i);
            }
        }

        return 0;
    }
}
