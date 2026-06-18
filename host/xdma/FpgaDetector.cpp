#include "FpgaDetector.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "RegisterMap.hpp"

namespace unlook_fpga {

namespace {

bool nodeExists(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}

/// Scan /sys/bus/pci/devices/*/vendor for the Xilinx vendor id (0x10ee).
/// Returns true and fills @p deviceIdOut if found.
bool findXilinxPciDevice(uint16_t& deviceIdOut) {
    DIR* d = ::opendir("/sys/bus/pci/devices");
    if (!d) return false;
    bool found = false;
    for (dirent* e = ::readdir(d); e != nullptr && !found; e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const std::string base = std::string("/sys/bus/pci/devices/") + e->d_name;
        std::ifstream vf(base + "/vendor");
        if (!vf) continue;
        unsigned vendor = 0;
        vf >> std::hex >> vendor;
        if (vendor == reg::kXilinxVendorId) {
            std::ifstream df(base + "/device");
            unsigned dev = 0;
            if (df) df >> std::hex >> dev;
            deviceIdOut = static_cast<uint16_t>(dev);
            found = true;
        }
    }
    ::closedir(d);
    return found;
}

} // namespace

ProbeResult probeFpga(const std::string& devicePrefix) {
    ProbeResult r;
    r.devicePath = devicePrefix;

    const std::string userNode = devicePrefix + "_user";
    const std::string h2cNode  = devicePrefix + "_h2c_0";
    const std::string c2dNode  = devicePrefix + "_c2d_0";
    if (!nodeExists(userNode) || !nodeExists(h2cNode) || !nodeExists(c2dNode)) {
        r.detail = "XDMA device nodes missing (" + devicePrefix +
                   "_user/_h2c_0/_c2d_0) -- driver loaded?";
        return r;
    }

    uint16_t deviceId = 0;
    if (!findXilinxPciDevice(deviceId)) {
        r.detail = "XDMA nodes present but no Xilinx (0x10ee) PCI device found";
        return r;
    }

    char detail[128];
    std::snprintf(detail, sizeof(detail),
                  "Unlook XC7A200T (XDMA, PCI 0x10ee:0x%04x) on %s",
                  deviceId, devicePrefix.c_str());

    r.present          = true;
    r.pciVendorId      = reg::kXilinxVendorId;
    r.pciDeviceId      = deviceId;
    r.bitstreamVersion = 0;  // tracked via firmware provenance, not a runtime reg
    r.detail           = detail;
    return r;
}

} // namespace unlook_fpga
