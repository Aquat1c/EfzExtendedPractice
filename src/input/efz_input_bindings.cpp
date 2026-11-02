#include "../../include/input/efz_input_bindings.h"

#include <windows.h>
#include <array>
#include <fstream>
#include <filesystem>

namespace EFZInputBindings {

namespace {

bool ReadFile(const std::filesystem::path& path, BindingSet& output) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::array<std::uint8_t, kActiveKeyIniBytes> bytes{};
    file.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (file.gcount() != static_cast<std::streamsize>(bytes.size())) return false;
    return DecodeActiveKeyIni(bytes.data(), bytes.size(), output);
}

}

bool LoadActiveKeyIni(BindingSet& output, std::string& sourcePath,
                      std::string& error) {
    output = BindingSet{};
    sourcePath.clear();
    error.clear();

    std::array<std::filesystem::path, 2> candidates{};
    char executable[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, executable, MAX_PATH) != 0) {
        candidates[0] = std::filesystem::path(executable).parent_path() / "key.ini";
    }
    char cwd[MAX_PATH] = {};
    if (GetCurrentDirectoryA(MAX_PATH, cwd) != 0) {
        candidates[1] = std::filesystem::path(cwd) / "key.ini";
    }

    for (const auto& candidate : candidates) {
        if (candidate.empty()) continue;
        std::error_code ec;
        if (!std::filesystem::exists(candidate, ec) || ec) continue;
        if (!ReadFile(candidate, output)) {
            error = "key.ini exists but its active 32-byte binding block is invalid: " +
                    candidate.string();
            return false;
        }
        sourcePath = candidate.string();
        return true;
    }
    error = "key.ini was not found beside efz.exe or in the working directory";
    return false;
}

}
