#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/usb/USBSpec.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "addresses.h"
#include "boot/picoboot.h"
#include "elf_file.h"
#include "errors.h"

namespace {
constexpr uint16_t kVendorIdRaspberryPi = 0x2e8a;
constexpr uint16_t kProductIdRp2040UsbBoot = 0x0003;
constexpr uint16_t kProductIdRp2350UsbBoot = 0x000f;
constexpr uint32_t kFlashSectorSize = 4096;
constexpr uint32_t kFlashPageSize = 256;
constexpr uint32_t kUsbTimeoutMs = 3000;

struct PicobootInterface {
    UInt8 interface_number{};
    UInt8 pipe_in{};
    UInt8 pipe_out{};
    IOUSBInterfaceInterface **iface{};
};

struct DeviceMatch {
    IOUSBDeviceInterface **device{};
    PicobootInterface picoboot{};
};

struct Range {
    uint32_t start;
    uint32_t end;
};

void print_usage(const char *argv0) {
    std::cout << "Usage: " << argv0 << " [--flash] [--no-exec] <file.elf>\n"
              << "  --flash    Allow writing flash segments instead of RAM-mirroring\n"
              << "  --no-exec  Skip executing the loaded image\n";
}

uint32_t cf_number_to_uint32(CFTypeRef value) {
    if (!value || CFGetTypeID(value) != CFNumberGetTypeID()) {
        return 0;
    }
    uint32_t out = 0;
    CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt32Type, &out);
    return out;
}

IOUSBDeviceInterface **create_device_interface(io_service_t device_service) {
    IOCFPlugInInterface **plug_in = nullptr;
    SInt32 score = 0;
    IOReturn ret = IOCreatePlugInInterfaceForService(device_service, kIOUSBDeviceUserClientTypeID,
                                                     kIOCFPlugInInterfaceID, &plug_in, &score);
    if (ret != kIOReturnSuccess || !plug_in) {
        return nullptr;
    }

    IOUSBDeviceInterface **device = nullptr;
    HRESULT result = (*plug_in)->QueryInterface(plug_in, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
                                                reinterpret_cast<void **>(&device));
    (*plug_in)->Release(plug_in);
    if (result || !device) {
        return nullptr;
    }
    return device;
}

IOUSBInterfaceInterface **create_interface_interface(io_service_t interface_service) {
    IOCFPlugInInterface **plug_in = nullptr;
    SInt32 score = 0;
    IOReturn ret = IOCreatePlugInInterfaceForService(interface_service, kIOUSBInterfaceUserClientTypeID,
                                                     kIOCFPlugInInterfaceID, &plug_in, &score);
    if (ret != kIOReturnSuccess || !plug_in) {
        return nullptr;
    }

    IOUSBInterfaceInterface **iface = nullptr;
    HRESULT result = (*plug_in)->QueryInterface(plug_in, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID),
                                                reinterpret_cast<void **>(&iface));
    (*plug_in)->Release(plug_in);
    if (result || !iface) {
        return nullptr;
    }
    return iface;
}

std::optional<DeviceMatch> find_device() {
    CFMutableDictionaryRef matching = IOServiceMatching(kIOUSBDeviceClassName);
    if (!matching) {
        return std::nullopt;
    }

    io_iterator_t iterator = 0;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) != kIOReturnSuccess) {
        return std::nullopt;
    }

    std::optional<DeviceMatch> match;
    io_service_t device_service = 0;
    while ((device_service = IOIteratorNext(iterator)) != 0) {
        CFTypeRef vendor_ref = IORegistryEntryCreateCFProperty(device_service, CFSTR(kUSBVendorID),
                                                               kCFAllocatorDefault, 0);
        CFTypeRef product_ref = IORegistryEntryCreateCFProperty(device_service, CFSTR(kUSBProductID),
                                                                kCFAllocatorDefault, 0);
        uint32_t vendor_id = cf_number_to_uint32(vendor_ref);
        uint32_t product_id = cf_number_to_uint32(product_ref);
        if (vendor_ref) {
            CFRelease(vendor_ref);
        }
        if (product_ref) {
            CFRelease(product_ref);
        }

        if (vendor_id != kVendorIdRaspberryPi) {
            IOObjectRelease(device_service);
            continue;
        }
        if (product_id != kProductIdRp2040UsbBoot && product_id != kProductIdRp2350UsbBoot) {
            IOObjectRelease(device_service);
            continue;
        }

        IOUSBDeviceInterface **device = create_device_interface(device_service);
        if (!device) {
            IOObjectRelease(device_service);
            continue;
        }
        if ((*device)->USBDeviceOpen(device) != kIOReturnSuccess) {
            (*device)->Release(device);
            IOObjectRelease(device_service);
            continue;
        }

        IOUSBFindInterfaceRequest request;
        request.bInterfaceClass = 0xff;
        request.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
        request.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
        request.bAlternateSetting = kIOUSBFindInterfaceDontCare;

        io_iterator_t iface_iterator = 0;
        if ((*device)->CreateInterfaceIterator(device, &request, &iface_iterator) != kIOReturnSuccess) {
            (*device)->USBDeviceClose(device);
            (*device)->Release(device);
            IOObjectRelease(device_service);
            continue;
        }

        io_service_t interface_service = 0;
        while ((interface_service = IOIteratorNext(iface_iterator)) != 0) {
            IOUSBInterfaceInterface **iface = create_interface_interface(interface_service);
            IOObjectRelease(interface_service);
            if (!iface) {
                continue;
            }
            if ((*iface)->USBInterfaceOpen(iface) != kIOReturnSuccess) {
                (*iface)->Release(iface);
                continue;
            }

            UInt8 num_endpoints = 0;
            (*iface)->GetNumEndpoints(iface, &num_endpoints);

            UInt8 interface_number = 0;
            (*iface)->GetInterfaceNumber(iface, &interface_number);

            UInt8 pipe_in = 0;
            UInt8 pipe_out = 0;
            for (UInt8 pipe_ref = 1; pipe_ref <= num_endpoints; ++pipe_ref) {
                UInt8 direction = 0;
                UInt8 number = 0;
                UInt8 transfer_type = 0;
                UInt16 max_packet = 0;
                UInt8 interval = 0;
                if ((*iface)->GetPipeProperties(iface, pipe_ref, &direction, &number, &transfer_type,
                                                &max_packet, &interval) != kIOReturnSuccess) {
                    continue;
                }
                if (transfer_type != kUSBBulk) {
                    continue;
                }
                if (direction == kUSBIn) {
                    pipe_in = pipe_ref;
                } else if (direction == kUSBOut) {
                    pipe_out = pipe_ref;
                }
            }

            if (pipe_in != 0 && pipe_out != 0) {
                match = DeviceMatch{device, PicobootInterface{interface_number, pipe_in, pipe_out, iface}};
                break;
            }

            (*iface)->USBInterfaceClose(iface);
            (*iface)->Release(iface);
        }

        IOObjectRelease(iface_iterator);
        if (match) {
            IOObjectRelease(device_service);
            break;
        }

        (*device)->USBDeviceClose(device);
        (*device)->Release(device);
        IOObjectRelease(device_service);
    }

    IOObjectRelease(iterator);
    return match;
}

IOReturn write_pipe(IOUSBInterfaceInterface **iface, UInt8 pipe, const void *data, UInt32 size, UInt32 timeout_ms) {
    return (*iface)->WritePipeTO(iface, pipe, const_cast<void *>(data), size, timeout_ms, timeout_ms);
}

IOReturn read_pipe(IOUSBInterfaceInterface **iface, UInt8 pipe, void *data, UInt32 *size, UInt32 timeout_ms) {
    return (*iface)->ReadPipeTO(iface, pipe, data, size, timeout_ms, timeout_ms);
}

IOReturn send_picoboot_command(IOUSBInterfaceInterface **iface, const PicobootInterface &picoboot,
                               picoboot_cmd &cmd, uint8_t *buffer, uint32_t buffer_len) {
    (void)buffer_len;
    static uint32_t token = 1;
    cmd.dMagic = PICOBOOT_MAGIC;
    cmd.dToken = token++;

    IOReturn ret = write_pipe(iface, picoboot.pipe_out, &cmd, sizeof(cmd), kUsbTimeoutMs);
    if (ret != kIOReturnSuccess) {
        return ret;
    }

    if (cmd.dTransferLength != 0) {
        if (cmd.bCmdId & 0x80u) {
            UInt32 expected = cmd.dTransferLength;
            ret = read_pipe(iface, picoboot.pipe_in, buffer, &expected, kUsbTimeoutMs * 3);
            if (ret != kIOReturnSuccess || expected != cmd.dTransferLength) {
                return ret != kIOReturnSuccess ? ret : kIOReturnError;
            }
        } else {
            ret = write_pipe(iface, picoboot.pipe_out, buffer, cmd.dTransferLength, kUsbTimeoutMs * 3);
            if (ret != kIOReturnSuccess) {
                return ret;
            }
        }
    }

    uint8_t ack = 0;
    if (cmd.bCmdId & 0x80u) {
        ret = write_pipe(iface, picoboot.pipe_out, &ack, 1, kUsbTimeoutMs);
    } else {
        UInt32 ack_len = 1;
        ret = read_pipe(iface, picoboot.pipe_in, &ack, &ack_len, kUsbTimeoutMs);
    }
    return ret;
}

IOReturn picoboot_reset(IOUSBInterfaceInterface **iface, const PicobootInterface &picoboot) {
    IOUSBDevRequest request{};
    request.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBInterface);
    request.bRequest = PICOBOOT_IF_RESET;
    request.wValue = 0;
    request.wIndex = picoboot.interface_number;
    request.wLength = 0;
    request.pData = nullptr;
    return (*iface)->ControlRequest(iface, 0, &request);
}

IOReturn picoboot_get_cmd_status(IOUSBInterfaceInterface **iface, const PicobootInterface &picoboot,
                                 picoboot_cmd_status &status) {
    IOUSBDevRequest request{};
    request.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBVendor, kUSBInterface);
    request.bRequest = PICOBOOT_IF_CMD_STATUS;
    request.wValue = 0;
    request.wIndex = picoboot.interface_number;
    request.wLength = sizeof(status);
    request.pData = &status;
    IOReturn ret = (*iface)->ControlRequest(iface, 0, &request);
    if (ret != kIOReturnSuccess || request.wLenDone != sizeof(status)) {
        return ret != kIOReturnSuccess ? ret : kIOReturnError;
    }
    return ret;
}

IOReturn picoboot_exit_xip(IOUSBInterfaceInterface **iface, const PicobootInterface &picoboot) {
    picoboot_cmd cmd{};
    cmd.bCmdId = PC_EXIT_XIP;
    cmd.bCmdSize = 0;
    cmd.dTransferLength = 0;
    return send_picoboot_command(iface, picoboot, cmd, nullptr, 0);
}

IOReturn picoboot_flash_erase(IOUSBInterfaceInterface **iface, const PicobootInterface &picoboot, uint32_t addr,
                              uint32_t size) {
    picoboot_cmd cmd{};
    cmd.bCmdId = PC_FLASH_ERASE;
    cmd.bCmdSize = sizeof(cmd.range_cmd);
    cmd.range_cmd.dAddr = addr;
    cmd.range_cmd.dSize = size;
    cmd.dTransferLength = 0;
    return send_picoboot_command(iface, picoboot, cmd, nullptr, 0);
}

IOReturn picoboot_write(IOUSBInterfaceInterface **iface, const PicobootInterface &picoboot, uint32_t addr,
                        const uint8_t *buffer, uint32_t size) {
    picoboot_cmd cmd{};
    cmd.bCmdId = PC_WRITE;
    cmd.bCmdSize = sizeof(cmd.range_cmd);
    cmd.range_cmd.dAddr = addr;
    cmd.range_cmd.dSize = size;
    cmd.dTransferLength = size;
    return send_picoboot_command(iface, picoboot, cmd, const_cast<uint8_t *>(buffer), size);
}

IOReturn picoboot_exec(IOUSBInterfaceInterface **iface, const PicobootInterface &picoboot, uint32_t addr) {
    picoboot_cmd cmd{};
    cmd.bCmdId = PC_EXEC;
    cmd.bCmdSize = sizeof(cmd.address_only_cmd);
    cmd.address_only_cmd.dAddr = addr;
    cmd.dTransferLength = 0;
    IOReturn ret = send_picoboot_command(iface, picoboot, cmd, nullptr, 0);
    if (ret == kIOReturnSuccess || ret == kIOReturnNoDevice) {
        return kIOReturnSuccess;
    }
    picoboot_cmd_status status{};
    IOReturn status_ret = picoboot_get_cmd_status(iface, picoboot, status);
    if (status_ret == kIOReturnSuccess) {
        if (status.dStatusCode == PICOBOOT_OK || status.dStatusCode == PICOBOOT_REBOOTING) {
            return kIOReturnSuccess;
        }
    } else if (status_ret == kIOReturnNoDevice) {
        return kIOReturnSuccess;
    }
    return ret;
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

bool is_sram_address(uint32_t addr) {
    return addr >= SRAM_START && addr < SRAM_END_RP2350;
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

bool map_flash_to_sram(uint32_t addr, uint32_t size, uint32_t &mapped_addr) {
    if (addr < FLASH_START) {
        return false;
    }
    uint32_t offset = addr - FLASH_START;
    mapped_addr = SRAM_START + offset;
    if (mapped_addr < SRAM_START || mapped_addr + size > SRAM_END_RP2350) {
        return false;
    }
    return true;
}
} // namespace

int main(int argc, char **argv) {
    bool allow_flash = false;
    bool exec_after = true;
    std::string filename;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--flash") {
            allow_flash = true;
        } else if (arg == "--no-exec") {
            exec_after = false;
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

    auto match = find_device();
    if (!match) {
        std::cerr << "No Raspberry Pi BOOTSEL device found.\n";
        return 1;
    }

    IOReturn reset_ret = picoboot_reset(match->picoboot.iface, match->picoboot);
    if (reset_ret != kIOReturnSuccess) {
        std::cerr << "Warning: reset interface failed (IOKit error " << reset_ret << ").\n";
    }

    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> ram_segments;
    std::map<uint32_t, std::vector<uint8_t>> flash_pages;
    std::vector<Range> flash_erase_ranges;
    bool skipped_flash_segments = false;
    bool mirrored_flash_segments = false;
    uint32_t entry_point = 0;

    try {
        auto stream = std::make_shared<std::fstream>(filename, std::ios::in | std::ios::binary);
        if (!stream->is_open()) {
            throw failure_error(ERROR_READ_FAILED, "Failed to open file: " + filename);
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
                throw failure_error(ERROR_FORMAT, "ELF segment has no load address");
            }
            std::vector<uint8_t> data = elf.content(segment);
            if (data.empty()) {
                continue;
            }
            if (is_flash_address(addr)) {
                if (!allow_flash) {
                    uint32_t mapped_addr = 0;
                    if (!map_flash_to_sram(addr, static_cast<uint32_t>(data.size()), mapped_addr)) {
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
    } catch (const failure_error &err) {
        std::cerr << "ELF parse failed: " << err.what() << "\n";
        (*match->picoboot.iface)->USBInterfaceClose(match->picoboot.iface);
        (*match->picoboot.iface)->Release(match->picoboot.iface);
        (*match->device)->USBDeviceClose(match->device);
        (*match->device)->Release(match->device);
        return 1;
    }

    IOReturn ret = kIOReturnSuccess;
    if (!allow_flash && flash_pages.empty() && ram_segments.empty()) {
        std::cerr << "No loadable RAM segments found (flash segments skipped). Use --flash to enable flash writes.\n";
        (*match->picoboot.iface)->USBInterfaceClose(match->picoboot.iface);
        (*match->picoboot.iface)->Release(match->picoboot.iface);
        (*match->device)->USBDeviceClose(match->device);
        (*match->device)->Release(match->device);
        return 1;
    }
    if (mirrored_flash_segments) {
        std::cout << "Mirroring flash segments into SRAM (use --flash to write flash instead).\n";
    }
    if (skipped_flash_segments) {
        std::cout << "Skipping flash segments that do not fit in SRAM (use --flash to enable flash writes).\n";
    }
    if (!flash_pages.empty()) {
        ret = picoboot_exit_xip(match->picoboot.iface, match->picoboot);
        if (ret != kIOReturnSuccess) {
            std::cerr << "Failed to exit XIP mode (IOKit error " << ret << ").\n";
        }

        auto merged_ranges = merge_ranges(std::move(flash_erase_ranges));
        for (const auto &range : merged_ranges) {
            ret = picoboot_flash_erase(match->picoboot.iface, match->picoboot, range.start, range.end - range.start);
            if (ret != kIOReturnSuccess) {
                std::cerr << "Flash erase failed at 0x" << std::hex << range.start << " (IOKit error " << std::dec << ret
                          << ").\n";
                break;
            }
        }
    }

    if (ret == kIOReturnSuccess) {
        for (const auto &segment : ram_segments) {
            uint32_t addr = segment.first;
            const auto &data = segment.second;
            for (size_t offset = 0; offset < data.size();) {
                uint32_t chunk_size = static_cast<uint32_t>(std::min<size_t>(1024, data.size() - offset));
                ret = picoboot_write(match->picoboot.iface, match->picoboot, addr + static_cast<uint32_t>(offset),
                                     data.data() + offset, chunk_size);
                if (ret != kIOReturnSuccess) {
                    std::cerr << "RAM write failed at 0x" << std::hex << (addr + offset) << " (IOKit error " << std::dec
                              << ret << ").\n";
                    break;
                }
                offset += chunk_size;
            }
            if (ret != kIOReturnSuccess) {
                break;
            }
        }
    }

    if (ret == kIOReturnSuccess) {
        for (const auto &page : flash_pages) {
            ret = picoboot_write(match->picoboot.iface, match->picoboot, page.first, page.second.data(), kFlashPageSize);
            if (ret != kIOReturnSuccess) {
                std::cerr << "Flash write failed at 0x" << std::hex << page.first << " (IOKit error " << std::dec << ret
                          << ").\n";
                break;
            }
        }
    }

    if (ret == kIOReturnSuccess && exec_after) {
        if (entry_point == 0) {
            std::cerr << "ELF entry point is zero; cannot execute.\n";
            ret = kIOReturnError;
        } else {
            uint32_t exec_addr = entry_point;
            if (!allow_flash && is_flash_address(entry_point)) {
                uint32_t mapped_addr = 0;
                if (map_flash_to_sram(entry_point, 4, mapped_addr)) {
                    exec_addr = mapped_addr;
                } else {
                    std::cerr << "Entry point 0x" << std::hex << entry_point
                              << " cannot be mirrored into SRAM. Use --flash to run from flash.\n";
                    ret = kIOReturnError;
                }
            } else if (!allow_flash && !is_sram_address(entry_point) && !is_flash_address(entry_point)) {
                std::cerr << "Entry point 0x" << std::hex << entry_point << " is not in flash or SRAM.\n";
                ret = kIOReturnError;
            }
            if (ret == kIOReturnSuccess) {
                ret = picoboot_exec(match->picoboot.iface, match->picoboot, exec_addr);
                if (ret != kIOReturnSuccess) {
                    std::cerr << "Exec failed at 0x" << std::hex << exec_addr << " (IOKit error " << std::dec << ret
                              << ").\n";
                } else {
                    std::cout << "Executing at 0x" << std::hex << exec_addr << ".\n";
                }
            }
        }
    }

    if (ret == kIOReturnSuccess) {
        std::cout << "Load complete.\n";
    }

    (*match->picoboot.iface)->USBInterfaceClose(match->picoboot.iface);
    (*match->picoboot.iface)->Release(match->picoboot.iface);
    (*match->device)->USBDeviceClose(match->device);
    (*match->device)->Release(match->device);
    return ret == kIOReturnSuccess ? 0 : 1;
}
