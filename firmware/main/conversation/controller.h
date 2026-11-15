#pragma once

namespace local_conversation {
bool reserve_worker_stack();
bool start();
void poll();
// Returns true for conversation commands, including rejected busy commands.
bool command(const char* line);
}
