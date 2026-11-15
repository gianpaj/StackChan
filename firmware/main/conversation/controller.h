#pragma once

namespace local_conversation {
bool reserve_worker_stack();
bool start();
// Pump microphone input on the USB task; true selects nonblocking USB polling.
bool poll();
// Returns true for conversation commands, including rejected busy commands.
bool command(const char* line);
}
