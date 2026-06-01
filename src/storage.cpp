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

namespace tf::storage {
void save(const std::string& data) { tf_js_save(data.c_str()); }
std::string load() {
    char* p = tf_js_load();
    if (!p) return "";
    std::string s(p);
    free(p);
    return s;
}
}  // namespace tf::storage

#else
#include <SDL3/SDL.h>
#include <fstream>
#include <sstream>

namespace {
std::string statePath() {
    char* pref = SDL_GetPrefPath("wellons", "twenty-four");
    std::string path = pref ? std::string(pref) + "state.txt" : std::string("twenty-four-state.txt");
    if (pref) SDL_free(pref);
    return path;
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
}  // namespace tf::storage

#endif
