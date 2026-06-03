#include "storage.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <cstdlib>

EM_JS(void, tf_js_save, (const char* data), {
    try { localStorage.setItem('twenty-four-state', UTF8ToString(data)); } catch (e) {}
});
EM_JS(char*, tf_js_load, (), {
    try {
        var s = localStorage.getItem('twenty-four-state');
        if (s === null) return 0;
        var len = lengthBytesUTF8(s) + 1;
        var p = _malloc(len);
        stringToUTF8(s, p, len);
        return p;
    } catch (e) { return 0; }
});
EM_JS(void, tf_js_set_flag, (const char* key, int value), {
    try { localStorage.setItem('tf-' + UTF8ToString(key), value ? '1' : '0'); } catch (e) {}
});
EM_JS(int, tf_js_get_flag, (const char* key), {
    try { return localStorage.getItem('tf-' + UTF8ToString(key)) === '1' ? 1 : 0; } catch (e) { return 0; }
});

namespace tf::storage {
void save(const std::string& data) { tf_js_save(data.c_str()); }
std::string load() {
    char* p = tf_js_load();
    if (!p) return "";
    std::string s(p);
    free(p);
    return s;
}
void saveFlag(const char* key, bool value) { tf_js_set_flag(key, value ? 1 : 0); }
bool loadFlag(const char* key) { return tf_js_get_flag(key) != 0; }
}  // namespace tf::storage

#else
#include <SDL3/SDL.h>
#include <fstream>
#include <sstream>

namespace {
std::string prefDir() {
    char* pref = SDL_GetPrefPath("wellons", "twenty-four");
    std::string dir = pref ? std::string(pref) : std::string();
    if (pref) SDL_free(pref);
    return dir;
}
std::string statePath() {
    std::string dir = prefDir();
    return dir.empty() ? std::string("twenty-four-state.txt") : dir + "state.txt";
}
std::string flagPath(const char* key) {
    std::string dir = prefDir();
    return (dir.empty() ? std::string() : dir) + "flag-" + key + ".txt";
}
}  // namespace

namespace tf::storage {
void save(const std::string& data) {
    std::ofstream f(statePath(), std::ios::binary | std::ios::trunc);
    if (f) f << data;
}
std::string load() {
    std::ifstream f(statePath(), std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
void saveFlag(const char* key, bool value) {
    std::ofstream f(flagPath(key), std::ios::trunc);
    if (f) f << (value ? '1' : '0');
}
bool loadFlag(const char* key) {
    std::ifstream f(flagPath(key));
    char c = '0';
    return (f && (f >> c)) && c == '1';
}
}  // namespace tf::storage

#endif
