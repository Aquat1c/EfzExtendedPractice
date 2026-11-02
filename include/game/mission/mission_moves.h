#pragma once

namespace Mission::Moves {

const char* Notation(int moveId);

bool IsKnownComboMove(int moveId);

bool IsMovementMove(int moveId);

}
