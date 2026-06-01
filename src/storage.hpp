#pragma once
#include <string>

namespace tf {

// Persists the serialized game state to the right place per platform:
// localStorage on the web, otherwise the OS preferences dir via SDL_GetPrefPath
// (~/Library/Application Support, $XDG_DATA_HOME, %APPDATA%).
namespace storage {
void save(const std::string& data);
std::string load();  // returns "" when nothing is stored
}  // namespace storage

}  // namespace tf
