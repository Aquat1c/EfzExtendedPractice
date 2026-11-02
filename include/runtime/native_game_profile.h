#pragma once

#include "patch_ledger.h"

namespace Practice {

// Result is immutable for process lifetime; native callbacks must use their captured context.
bool GetQualifiedMemorialImage(PatchModule& module);

}
