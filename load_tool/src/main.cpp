#include <libusb.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "boot/picoboot.h"
#include "elf_file.h"
#include "errors.h"
#include "addresses.h"

namespace {
constexpr uint16_t kVendorIdRaspberryPi = 0x2e8a;
constexpr uint16_t kProductIdRp2040UsbBoot = 0x0003;
constexpr uint16_t kProductIdRp2350UsbBoot = 0x000f;
constexpr uint32_t kFlashSectorSize = 4096;
constexpr uint32_t kFlashPageSize = 256;
constexpr int kUsbTimeoutMs = 3000;

struct PicobootInterface {
    uint8_t interface_number{};
    uint8_t ep_in{};
    uint8_t ep_out{};
};

struct DeviceMatch {
    libusb_device *device{};
    libusb_device_handle *handle{};
    PicobootInterface picoboot{};
};

struct Range {
    uint32_t start;
    uint32_t end;
};

void print_usage(const char *argv0) {
    std::cout << "Usage: " << argv0 << " [--flash] [--verbose] <file.elf>\n"
              << "  --flash    Allow writing flash segments (disabled by default)\n"
              << "  --verbose  Enable libusb debug output\n";
}

bool is_picoboot_interface(const libusb_interface_descriptor &desc, uint8_t &ep_in, uint8_t &ep_out) {
    if (desc.bInterfaceClass != 0xff || desc.bNumEndpoints != 2) {
        return false;
    }
    ep_in = 0;
    ep_out = 0;
    for (int i = 0; i < desc.bNumEndpoints; ++i) {
        const auto &endpoint = desc.endpoint[i];
        if (endpoint.bEndpointAddress & LIBUSB_ENDPOINT_IN) {
            ep_in = endpoint.bEndpointAddress;
        } else {
            ep_out = endpoint.bEndpointAddress;
        }
    }
    return ep_in != 0 && ep_out != 0;
}

std::optional<DeviceMatch> find_device(libusb_context *ctx) {
    libusb_device **list = nullptr;
    ssize_t count = libusb_get_device_list(ctx, &list);
    if (count < 0) {
        return std::nullopt;
    }

    std::optional<DeviceMatch> match;
    for (ssize_t idx = 0; idx < count; ++idx) {
        libusb_device *device = list[idx];
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(device, &desc) != 0) {
            continue;
        }
        if (desc.idVendor != kVendorIdRaspberryPi) {
            continue;
        }
        if (desc.idProduct != kProductIdRp2040UsbBoot && desc.idProduct != kProductIdRp2350UsbBoot) {
            continue;
        }

        libusb_config_descriptor *config = nullptr;
        if (libusb_get_active_config_descriptor(device, &config) != 0) {
            continue;
        }

        for (uint8_t i = 0; i < config->bNumInterfaces; ++i) {
            const auto &interface = config->interface[i];
            if (interface.num_altsetting == 0) {
                continue;
            }
            const auto &alt = interface.altsetting[0];
            uint8_t ep_in = 0;
            uint8_t ep_out = 0;
            if (!is_picoboot_interface(alt, ep_in, ep_out)) {
                continue;
            }
            DeviceMatch candidate{};
            candidate.device = device;
            candidate.picoboot = PicobootInterface{alt.bInterfaceNumber, ep_in, ep_out};
            match = candidate;
            break;
        }
        libusb_free_config_descriptor(config);
        if (match) {
            break;
        }
    }

    libusb_free_device_list(list, 1);
    return match;
}

int claim_interface(libusb_device_handle *handle, uint8_t interface_number) {
#if defined(LIBUSB_API_VERSION) && LIBUSB_API_VERSION >= 0x0100010A
    libusb_set_auto_detach_kernel_driver(handle, 1);
#endif
    return libusb_claim_interface(handle, interface_number);
}

int send_picoboot_command(libusb_device_handle *handle, const PicobootInterface &iface, picoboot_cmd &cmd,
                          uint8_t *buffer, uint32_t buffer_len) {
    (void)buffer_len;
    static uint32_t token = 1;
    cmd.dMagic = PICOBOOT_MAGIC;
    cmd.dToken = token++;

    int sent = 0;
    int ret = libusb_bulk_transfer(handle, iface.ep_out, reinterpret_cast<uint8_t *>(&cmd), sizeof(cmd), &sent, kUsbTimeoutMs);
    if (ret != 0 || sent != static_cast<int>(sizeof(cmd))) {
        return ret != 0 ? ret : LIBUSB_ERROR_IO;
    }

    if (cmd.dTransferLength != 0) {
        if (cmd.bCmdId & 0x80u) {
            int received = 0;
            ret = libusb_bulk_transfer(handle, iface.ep_in, buffer, cmd.dTransferLength, &received, kUsbTimeoutMs * 3);
            if (ret != 0 || received != static_cast<int>(cmd.dTransferLength)) {
                return ret != 0 ? ret : LIBUSB_ERROR_IO;
            }
        } else {
            int transferred = 0;
            ret = libusb_bulk_transfer(handle, iface.ep_out, buffer, cmd.dTransferLength, &transferred, kUsbTimeoutMs * 3);
            if (ret != 0 || transferred != static_cast<int>(cmd.dTransferLength)) {
                return ret != 0 ? ret : LIBUSB_ERROR_IO;
            }
        }
    }

    uint8_t ack = 0;
    int received = 0;
    if (cmd.bCmdId & 0x80u) {
        ret = libusb_bulk_transfer(handle, iface.ep_out, &ack, 1, &received, kUsbTimeoutMs);
    } else {
        ret = libusb_bulk_transfer(handle, iface.ep_in, &ack, 1, &received, kUsbTimeoutMs);
    }
    return ret;
}

int picoboot_reset(libusb_device_handle *handle, const PicobootInterface &iface) {
    return libusb_control_transfer(handle, LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE,
                                   PICOBOOT_IF_RESET, 0, iface.interface_number, nullptr, 0, kUsbTimeoutMs);
}

int picoboot_exit_xip(libusb_device_handle *handle, const PicobootInterface &iface) {
    picoboot_cmd cmd{};
    cmd.bCmdId = PC_EXIT_XIP;
    cmd.bCmdSize = 0;
    cmd.dTransferLength = 0;
    return send_picoboot_command(handle, iface, cmd, nullptr, 0);
}

int picoboot_flash_erase(libusb_device_handle *handle, const PicobootInterface &iface, uint32_t addr, uint32_t size) {
    picoboot_cmd cmd{};
    cmd.bCmdId = PC_FLASH_ERASE;
    cmd.bCmdSize = sizeof(cmd.range_cmd);
    cmd.range_cmd.dAddr = addr;
    cmd.range_cmd.dSize = size;
    cmd.dTransferLength = 0;
    return send_picoboot_command(handle, iface, cmd, nullptr, 0);
}

int picoboot_write(libusb_device_handle *handle, const PicobootInterface &iface, uint32_t addr, const uint8_t *buffer, uint32_t size) {
    picoboot_cmd cmd{};
    cmd.bCmdId = PC_WRITE;
    cmd.bCmdSize = sizeof(cmd.range_cmd);
    cmd.range_cmd.dAddr = addr;
    cmd.range_cmd.dSize = size;
    cmd.dTransferLength = size;
    return send_picoboot_command(handle, iface, cmd, const_cast<uint8_t *>(buffer), size);
}

uint32_t align_down(uint32_t value, uint32_t align) {
    return value & ~(align - 1);
}

uint32_t align_up(uint32_t value, uint32_t align) {
    return (value + align - 1) & ~(align - 1);
}

bool is_flash_address(uint32_t addr) {
    return addr >= FLASH_START && addr < FLASH_END_RP2350;
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

uint32_t segment_address(const elf32_ph_entry &segment) {
    if (segment.paddr != 0) {
        return segment.paddr;
    }
    return segment.vaddr;
}
} // namespace

int main(int argc, char **argv) {
    bool verbose = false;
    bool allow_flash = false;
    std::string filename;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--flash") {
            allow_flash = true;
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (filename.empty()) {
            filename = arg;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 2;
        }
    }

    if (filename.empty()) {
        print_usage(argv[0]);
        return 2;
    }

    libusb_context *ctx = nullptr;
    if (libusb_init(&ctx) != 0) {
        std::cerr << "Failed to initialize libusb.\n";
        return 1;
    }

    if (verbose) {
        libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_INFO);
    }

    auto match = find_device(ctx);
    if (!match) {
        std::cerr << "No Raspberry Pi BOOTSEL device found.\n";
        libusb_exit(ctx);
        return 1;
    }

    libusb_device_handle *handle = nullptr;
    if (libusb_open(match->device, &handle) != 0 || !handle) {
        std::cerr << "Failed to open USB device.\n";
        libusb_exit(ctx);
        return 1;
    }

    int claim = claim_interface(handle, match->picoboot.interface_number);
    if (claim != 0) {
        std::cerr << "Failed to claim picoboot interface (libusb error " << claim << ").\n";
        libusb_close(handle);
        libusb_exit(ctx);
        return 1;
    }

    int reset_ret = picoboot_reset(handle, match->picoboot);
    if (reset_ret != 0) {
        std::cerr << "Warning: reset interface failed (libusb error " << reset_ret << ").\n";
    }

    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> ram_segments;
    std::map<uint32_t, std::vector<uint8_t>> flash_pages;
    std::vector<Range> flash_erase_ranges;
    bool skipped_flash_segments = false;

    try {
        auto stream = std::make_shared<std::fstream>(filename, std::ios::in | std::ios::binary);
        if (!stream->is_open()) {
            throw failure_error(ERROR_READ_FAILED, "Failed to open file: " + filename);
        }
        elf_file elf;
        elf.read_file(stream);

        for (const auto &segment : elf.segments()) {
            if (!segment.is_load() || segment.filez == 0) {
                continue;
            }
            uint32_t addr = segment_address(segment);
            if (addr == 0) {
                throw failure_error(ERROR_FORMAT, "ELF segment has no load address");
            }
            std::vector<uint8_t> data = elf.content(segment);
            if (data.empty()) {
                continue;
            }
            if (is_flash_address(addr)) {
                if (!allow_flash) {
                    skipped_flash_segments = true;
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
    } catch (const failure_error &err) {
        std::cerr << "ELF parse failed: " << err.what() << "\n";
        libusb_release_interface(handle, match->picoboot.interface_number);
        libusb_close(handle);
        libusb_exit(ctx);
        return 1;
    }

    int ret = 0;
    if (!allow_flash && flash_pages.empty() && ram_segments.empty()) {
        std::cerr << "No loadable RAM segments found (flash segments skipped by default). Use --flash to enable flash writes.\n";
        libusb_release_interface(handle, match->picoboot.interface_number);
        libusb_close(handle);
        libusb_exit(ctx);
        return 1;
    }
    if (skipped_flash_segments) {
        std::cout << "Skipping flash segments (use --flash to enable flash writes).\n";
    }
    if (!flash_pages.empty()) {
        ret = picoboot_exit_xip(handle, match->picoboot);
        if (ret != 0) {
            std::cerr << "Failed to exit XIP mode (libusb error " << ret << ").\n";
        }

        auto merged_ranges = merge_ranges(std::move(flash_erase_ranges));
        for (const auto &range : merged_ranges) {
            ret = picoboot_flash_erase(handle, match->picoboot, range.start, range.end - range.start);
            if (ret != 0) {
                std::cerr << "Flash erase failed at 0x" << std::hex << range.start << " (libusb error " << std::dec << ret << ").\n";
                break;
            }
        }
    }

    if (ret == 0) {
        for (const auto &segment : ram_segments) {
            uint32_t addr = segment.first;
            const auto &data = segment.second;
            for (size_t offset = 0; offset < data.size();) {
                uint32_t chunk_size = static_cast<uint32_t>(std::min<size_t>(1024, data.size() - offset));
                ret = picoboot_write(handle, match->picoboot, addr + static_cast<uint32_t>(offset),
                                     data.data() + offset, chunk_size);
                if (ret != 0) {
                    std::cerr << "RAM write failed at 0x" << std::hex << (addr + offset) << " (libusb error " << std::dec << ret << ").\n";
                    break;
                }
                offset += chunk_size;
            }
            if (ret != 0) {
                break;
            }
        }
    }

    if (ret == 0) {
        for (const auto &page : flash_pages) {
            ret = picoboot_write(handle, match->picoboot, page.first, page.second.data(), kFlashPageSize);
            if (ret != 0) {
                std::cerr << "Flash write failed at 0x" << std::hex << page.first << " (libusb error " << std::dec << ret << ").\n";
                break;
            }
        }
    }

    if (ret == 0) {
        std::cout << "Load complete.\n";
    }

    libusb_release_interface(handle, match->picoboot.interface_number);
    libusb_close(handle);
    libusb_exit(ctx);
    return ret == 0 ? 0 : 1;
}
