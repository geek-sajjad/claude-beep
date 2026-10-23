#pragma once

namespace cb {

// Runs the tray indicator: a coloured dot in the GNOME top bar that follows the
// aggregated state of every live Claude session. Blocks until quit.
int runIndicator();

} // namespace cb
