#include "elf/elf.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace {
constexpr size_t kElfHeaderSize = 52;
constexpr size_t kIdentSize = 16;
constexpr uint8_t kElfClass32 = 1;
constexpr uint8_t kElfDataLittleEndian = 1;

uint16_t read_u16(const std::vector<uint8_t> &data, size_t offset) {
    if (offset + 2 > data.size()) {
        throw std::runtime_error("ELF file too small");
    }
    return static_cast<uint16_t>(data[offset]) |
           (static_cast<uint16_t>(data[offset + 1]) << 8);
}

uint32_t read_u32(const std::vector<uint8_t> &data, size_t offset) {
    if (offset + 4 > data.size()) {
        throw std::runtime_error("ELF file too small");
    }
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}
}

void elf_file::read_file(const std::shared_ptr<std::istream> &stream) {
    if (!stream || !*stream) {
        throw std::runtime_error("Invalid ELF stream");
    }

    stream->seekg(0, std::ios::end);
    std::streamoff size = stream->tellg();
    if (size <= 0) {
        throw std::runtime_error("ELF file is empty");
    }
    if (static_cast<uint64_t>(size) > std::numeric_limits<size_t>::max()) {
        throw std::runtime_error("ELF file too large");
    }
    stream->seekg(0, std::ios::beg);

    data_.assign(static_cast<size_t>(size), 0);
    stream->read(reinterpret_cast<char *>(data_.data()), size);
    if (!*stream) {
        throw std::runtime_error("Failed to read ELF file");
    }

    if (data_.size() < kElfHeaderSize) {
        throw std::runtime_error("ELF header truncated");
    }

    if (data_[0] != 0x7f || data_[1] != 'E' || data_[2] != 'L' || data_[3] != 'F') {
        throw std::runtime_error("Missing ELF magic");
    }
    if (data_[4] != kElfClass32) {
        throw std::runtime_error("Unsupported ELF class");
    }
    if (data_[5] != kElfDataLittleEndian) {
        throw std::runtime_error("Unsupported ELF endian");
    }

    header_.entry = read_u32(data_, 24);
    header_.phoff = read_u32(data_, 28);
    header_.phentsize = read_u16(data_, 42);
    header_.phnum = read_u16(data_, 44);

    if (header_.phoff < kIdentSize || header_.phentsize == 0) {
        throw std::runtime_error("ELF program header table missing");
    }

    size_t ph_table_size = static_cast<size_t>(header_.phentsize) * header_.phnum;
    if (header_.phoff + ph_table_size > data_.size()) {
        throw std::runtime_error("ELF program header table truncated");
    }

    segments_.clear();
    segments_.reserve(header_.phnum);
    for (uint16_t i = 0; i < header_.phnum; ++i) {
        size_t base = header_.phoff + static_cast<size_t>(header_.phentsize) * i;
        if (base + 32 > data_.size()) {
            throw std::runtime_error("ELF program header truncated");
        }
        elf32_ph_entry entry;
        entry.type = read_u32(data_, base + 0);
        entry.offset = read_u32(data_, base + 4);
        entry.vaddr = read_u32(data_, base + 8);
        entry.paddr = read_u32(data_, base + 12);
        entry.filez = read_u32(data_, base + 16);
        entry.memsz = read_u32(data_, base + 20);
        entry.flags = read_u32(data_, base + 24);
        entry.align = read_u32(data_, base + 28);
        segments_.push_back(entry);
    }
}

std::vector<uint8_t> elf_file::content(const elf32_ph_entry &segment) const {
    if (segment.filez == 0) {
        return {};
    }
    if (segment.offset + segment.filez > data_.size()) {
        throw std::runtime_error("ELF segment out of range");
    }
    auto begin = data_.begin() + static_cast<std::ptrdiff_t>(segment.offset);
    auto end = begin + static_cast<std::ptrdiff_t>(segment.filez);
    return std::vector<uint8_t>(begin, end);
}
