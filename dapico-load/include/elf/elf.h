#pragma once

#include <cstdint>
#include <istream>
#include <memory>
#include <vector>

struct elf32_header {
    uint32_t entry = 0;
    uint32_t phoff = 0;
    uint16_t phentsize = 0;
    uint16_t phnum = 0;
};

struct elf32_ph_entry {
    uint32_t type = 0;
    uint32_t offset = 0;
    uint32_t vaddr = 0;
    uint32_t paddr = 0;
    uint32_t filez = 0;
    uint32_t memsz = 0;
    uint32_t flags = 0;
    uint32_t align = 0;

    bool is_load() const { return type == 1; }
};

class elf_file {
public:
    void read_file(const std::shared_ptr<std::istream> &stream);

    const elf32_header &header() const { return header_; }
    const std::vector<elf32_ph_entry> &segments() const { return segments_; }

    std::vector<uint8_t> content(const elf32_ph_entry &segment) const;

private:
    elf32_header header_{};
    std::vector<elf32_ph_entry> segments_{};
    std::vector<uint8_t> data_{};
};
