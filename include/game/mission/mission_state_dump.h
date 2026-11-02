#pragma once
// Characters must already match (hotswap first). Stale heap pointers from another session would
// crash, so pointer-bearing ranges are reconciled. Uses Revival's single manual save slot.
#include <string>

namespace Mission::StateDump {

bool Available();

bool Capture(std::string& outB64, std::string& outErr);

// Restore a blob into the running match (Match phase, hotswap/setup idle,
// characters matching the dump - the caller sequences that).
bool Restore(const std::string& b64, std::string& outErr);

}
