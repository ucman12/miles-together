// Miles Together - everything that touches the running game.
#pragma once

namespace mt::game {

// Checks the executable against the sheets and installs the per-frame tick.
// Returns false, changing nothing, when this is not the supported game build.
bool Install();

}  // namespace mt::game
