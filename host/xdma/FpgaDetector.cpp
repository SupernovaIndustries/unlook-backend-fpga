#include "FpgaDetector.hpp"

#include <sys/stat.h>

#include "RegisterMap.hpp"
#include "XdmaTransport.hpp"

namespace unlook_fpga {

namespace {
bool nodeExists(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}
} // namespace

ProbeResult probeFpga(const std::string& devicePrefix) {
    ProbeResult r;
    r.devicePath = devicePrefix;

    const std::string userNode = devicePrefix + "_user";
    if (!nodeExists(userNode)) {
        r.detail = "no XDMA device node (" + userNode + ")";
        return r;
    }

    // Open briefly and confirm the core identifies itself via the ID magic.
    XdmaTransport t(devicePrefix, /*dmaTimeoutMs*/ 33);
    if (!t.open()) {
        r.detail = "device present but open failed: " + t.lastError();
        return r;
    }

    uint32_t id = 0, version = 0;
    if (!t.readReg(reg::ID, id) || id != reg::kMagic) {
        r.detail = "ID register mismatch (got 0x" + std::to_string(id) +
                   ", expected Unlook core)";
        return r;
    }
    t.readReg(reg::VERSION, version);

    r.present          = true;
    r.pciVendorId      = 0x10ee;   // Xilinx (the core answered on the XDMA BAR)
    r.bitstreamVersion = version;
    r.detail           = "Unlook XC7A200T core v" + std::to_string(version) +
                         " on " + devicePrefix;
    return r;
}

} // namespace unlook_fpga
