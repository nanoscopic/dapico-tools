#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "dryrun.h"
#include "elf/elf.h"

namespace {
constexpr uint32_t kFlashSectorSize = 4096;
constexpr uint32_t kFlashPageSize = 256;
constexpr uint32_t kFlashStart = 0x10000000;
constexpr uint32_t kSramStart = 0x20000000;
constexpr uint32_t kFlashEndRp2040 = 0x11000000;
constexpr uint32_t kSramEndRp2040 = 0x20042000;

struct Range {
    uint32_t start;
    uint32_t end;
};

struct MemoryLayout {
    uint32_t flash_end;
    uint32_t sram_end;
};

uint32_t align_down(uint32_t value, uint32_t align) {
    return value & ~(align - 1);
}

uint32_t align_up(uint32_t value, uint32_t align) {
    return (value + align - 1) & ~(align - 1);
}

bool is_flash_address(uint32_t addr, const MemoryLayout &layout) {
    return addr >= kFlashStart && addr < layout.flash_end;
}

bool is_sram_address(uint32_t addr, const MemoryLayout &layout) {
    return addr >= kSramStart && addr < layout.sram_end;
}

uint32_t segment_address(const elf32_ph_entry &segment) {
    if (segment.paddr != 0) {
        return segment.paddr;
    }
    return segment.vaddr;
}

bool map_flash_to_sram(uint32_t addr, uint32_t size, const MemoryLayout &layout, uint32_t &mapped_addr) {
    if (addr < kFlashStart) {
        return false;
    }
    uint32_t offset = addr - kFlashStart;
    mapped_addr = kSramStart + offset;
    if (mapped_addr < kSramStart || mapped_addr + size > layout.sram_end) {
        return false;
    }
    return true;
}

std::vector<Range> merge_ranges(std::vector<Range> ranges) {
    if (ranges.empty()) {
        return ranges;
    }
    std::sort(ranges.begin(), ranges.end(), [](const Range &a, const Range &b) { return a.start < b.start; });
    std::vector<Range> merged;
    merged.push_back(ranges.front());
    for (size_t i = 1; i < ranges.size(); ++i) {
        Range &last = merged.back();
        if (ranges[i].start <= last.end) {
            last.end = std::max(last.end, ranges[i].end);
        } else {
            merged.push_back(ranges[i]);
        }
    }
    return merged;
}
} // namespace

int run_dryrun(const std::string &filename, bool allow_flash, bool exec_after) {
    MemoryLayout memory_layout{kFlashEndRp2040, kSramEndRp2040};
    std::cout << "Dry run: assuming RP2040 memory layout (flash end 0x" << std::hex << memory_layout.flash_end
              << ", SRAM end 0x" << memory_layout.sram_end << ").\n";

    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> ram_segments;
    std::map<uint32_t, std::vector<uint8_t>> flash_pages;
    std::vector<Range> flash_erase_ranges;
    bool skipped_flash_segments = false;
    bool mirrored_flash_segments = false;
    uint32_t entry_point = 0;

    try {
        auto stream = std::make_shared<std::fstream>(filename, std::ios::in | std::ios::binary);
        if (!stream->is_open()) {
            throw std::runtime_error("Failed to open file: " + filename);
        }
        elf_file elf;
        elf.read_file(stream);

        entry_point = elf.header().entry;
        for (const auto &segment : elf.segments()) {
            if (!segment.is_load() || segment.filez == 0) {
                continue;
            }
            uint32_t addr = segment_address(segment);
            if (addr == 0) {
                throw std::runtime_error("ELF segment has no load address");
            }
            std::vector<uint8_t> data = elf.content(segment);
            if (data.empty()) {
                continue;
            }
            if (is_flash_address(addr, memory_layout)) {
                if (!allow_flash) {
                    uint32_t mapped_addr = 0;
                    if (!map_flash_to_sram(addr, static_cast<uint32_t>(data.size()), memory_layout, mapped_addr)) {
                        skipped_flash_segments = true;
                        continue;
                    }
                    mirrored_flash_segments = true;
                    ram_segments.emplace_back(mapped_addr, std::move(data));
                    continue;
                }
                uint32_t end = addr + static_cast<uint32_t>(data.size());
                uint32_t erase_start = align_down(addr, kFlashSectorSize);
                uint32_t erase_end = align_up(end, kFlashSectorSize);
                flash_erase_ranges.push_back(Range{erase_start, erase_end});
                for (size_t i = 0; i < data.size(); ++i) {
                    uint32_t byte_addr = addr + static_cast<uint32_t>(i);
                    uint32_t page_base = align_down(byte_addr, kFlashPageSize);
                    uint32_t page_offset = byte_addr - page_base;
                    auto &page = flash_pages[page_base];
                    if (page.empty()) {
                        page.assign(kFlashPageSize, 0);
                    }
                    page[page_offset] = data[i];
                }
            } else {
                ram_segments.emplace_back(addr, std::move(data));
            }
        }
    } catch (const std::runtime_error &err) {
        std::cerr << "ELF parse failed: " << err.what() << "\n";
        return 1;
    }

    if (!allow_flash && flash_pages.empty() && ram_segments.empty()) {
        std::cerr << "No loadable RAM segments found (flash segments skipped). Use --flash to enable flash writes.\n";
        return 1;
    }
    if (mirrored_flash_segments) {
        std::cout << "Mirroring flash segments into SRAM (use --flash to write flash instead).\n";
    }
    if (skipped_flash_segments) {
        std::cout << "Skipping flash segments that do not fit in SRAM (use --flash to enable flash writes).\n";
    }

    if (!flash_pages.empty()) {
        std::cout << "Dry run: would exit XIP mode.\n";
        auto merged_ranges = merge_ranges(std::move(flash_erase_ranges));
        for (const auto &range : merged_ranges) {
            std::cout << "Dry run: would erase flash 0x" << std::hex << range.start << "-0x" << range.end << " ("
                      << std::dec << (range.end - range.start) << " bytes).\n";
        }
    }

    for (const auto &segment : ram_segments) {
        uint32_t addr = segment.first;
        const auto &data = segment.second;
        std::cout << "Dry run: would write RAM 0x" << std::hex << addr << " (" << std::dec << data.size()
                  << " bytes).\n";
    }

    for (const auto &page : flash_pages) {
        std::cout << "Dry run: would write flash page 0x" << std::hex << page.first << " (" << std::dec
                  << page.second.size() << " bytes).\n";
    }

    if (exec_after) {
        if (entry_point == 0) {
            std::cerr << "ELF entry point is zero; cannot execute.\n";
            return 1;
        }
        uint32_t exec_addr = entry_point;
        if (!allow_flash && is_flash_address(entry_point, memory_layout)) {
            uint32_t mapped_addr = 0;
            if (map_flash_to_sram(entry_point, 4, memory_layout, mapped_addr)) {
                exec_addr = mapped_addr;
            } else {
                std::cerr << "Entry point 0x" << std::hex << entry_point
                          << " cannot be mirrored into SRAM. Use --flash to run from flash.\n";
                return 1;
            }
        } else if (!allow_flash && !is_sram_address(entry_point, memory_layout) &&
                   !is_flash_address(entry_point, memory_layout)) {
            std::cerr << "Entry point 0x" << std::hex << entry_point << " is not in flash or SRAM.\n";
            return 1;
        }
        std::cout << "Dry run: would execute at 0x" << std::hex << exec_addr << ".\n";
    }

    std::cout << "Dry run complete.\n";
    return 0;
}
