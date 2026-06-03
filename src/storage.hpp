#pragma once
#include <string>

namespace tf {

// Persists the serialized game state to the right place per platform:
// localStorage on the web, otherwise the OS preferences dir via SDL_GetPrefPath
// (~/Library/Application Support, $XDG_DATA_HOME, %APPDATA%).
namespace storage {
void save(const std::string& data);
std::string load();  // returns "" when nothing is stored

// A tiny separate flag, used for one-time UI state like "tutorial seen".
void saveFlag(const char* key, bool value);
bool loadFlag(const char* key);  // false when unset
}  // namespace storage

}  // namespace tf
