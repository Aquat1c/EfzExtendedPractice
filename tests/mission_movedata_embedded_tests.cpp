#include "game/mission/mission_movedata.h"
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

HMODULE g_hSelfModule = nullptr;
namespace {
std::vector<std::string> profiles;
void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    Check(bool(input), "Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
int Character(const char* name) {
    const auto found = std::find(profiles.begin(), profiles.end(), name);
    Check(found != profiles.end(), std::string("Missing profile ") + name);
    return int(found - profiles.begin());
}
}
namespace CharacterSettings {
std::string GetCharacterInternalName(int id) {
    return id >= 0 && id < int(profiles.size()) ? profiles[id] : "missing";
}
}
void LogOut(const std::string&, bool) {}

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    using namespace Mission::MoveData;
    fs::path isolated;
    try {
        Check(argc == 3, "Supply the built DLL and move-data source directory");
        const auto dll = fs::absolute(argv[1]);
        const auto sources = fs::absolute(argv[2]);
        isolated = fs::temp_directory_path() /
            ("efz_embedded_movedata_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(GetTickCount()));
        Check(fs::create_directory(isolated), "Isolated test directory already exists");
        const auto renamed = isolated / "renamed-training.dll";
        fs::copy_file(dll, renamed);
        g_hSelfModule = LoadLibraryExW(renamed.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
        Check(g_hSelfModule != nullptr, "Cannot inspect the built DLL resources");
        Check(!fs::exists(isolated / "assets"), "Test directory unexpectedly contains assets");

        unsigned packed = 0;
        for (const auto& file : fs::directory_iterator(sources)) {
            if (file.path().extension() != ".json") continue;
            const std::string name = file.path().stem().string();
            std::string key = name;
            CharUpperBuffA(key.data(), DWORD(key.size()));
            const HRSRC resource = FindResourceA(g_hSelfModule, key.c_str(), "MOVEDATA");
            Check(resource != nullptr, "DLL is missing embedded move data: " + name);
            const DWORD size = SizeofResource(g_hSelfModule, resource);
            const auto* bytes = static_cast<const char*>(LockResource(LoadResource(g_hSelfModule, resource)));
            Check(bytes && size && std::string(bytes, size) == Read(file.path()),
                  "Embedded move data differs from its source: " + name);
            ++packed;
            if (name != "entity_manifest") profiles.push_back(name);
        }
        Check(packed > 25, "Roster resource coverage is incomplete");
        for (int id = 0; id < int(profiles.size()); ++id)
            Check(EnsureLoaded(id), "Cannot classify moves from the isolated DLL: " + profiles[id]);

        Check(GetClass(Character("nayukib"), 200) == Cls::A, "Nayuki normal lost its classification");
        Check(GetClass(Character("nanase"), 259) == Cls::Special &&
              IsAttack(Character("nanase"), 259), "Alternate table erased Nanase's special");
        Check(GetClass(Character("nanase"), 413) == Cls::Projectile &&
              IsAttack(Character("nanase"), 413), "Alternate projectile was not merged");
        Check(!EnsureLoaded(999) && GetClass(999, 200) == Cls::Unknown,
              "Missing embedded profile published metadata");
        FreeLibrary(g_hSelfModule);
        g_hSelfModule = nullptr;
        Check(!EnsureLoaded(998), "Missing module loaded unrelated resources");
        fs::remove(renamed);
        fs::remove(isolated);
        std::cout << packed << " embedded move-data files verified; isolated DLL classification passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (g_hSelfModule) FreeLibrary(g_hSelfModule);
        std::cerr << error.what() << "\nFixture: " << isolated << '\n';
        return 1;
    }
}
