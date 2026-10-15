#ifndef SF4CE_ASSET_PACK_H
#define SF4CE_ASSET_PACK_H
#include <stdint.h>
#include <string.h>

namespace asset_pack {
constexpr const char *kSpritePrefixes[] = {
    "abel", "bison", "bla", "blr", "cammy", "cdy", "chun", "dan",
    "ddl", "deejay", "dhalsim", "eln", "fln", "gki", "gkn", "guile",
    "guy", "honda", "ibk", "jri", "ken", "mkt", "psn", "rose",
    "ryu", "ryx", "sagat", "skr", "vega", "viper", "yun", "zangief"
};

inline bool EndsWith(const char *name, const char *suffix)
{
    size_t n = strlen(name), s = strlen(suffix);
    return n >= s && strcmp(name + n - s, suffix) == 0;
}

inline int RosterFile(const char *name)
{
    if (!name || strchr(name, '/') || strchr(name, '\\') || strstr(name, "..") ||
        strstr(name, "_3P") || !(EndsWith(name, ".obm") || EndsWith(name, ".bin") || EndsWith(name, ".idx")))
        return -1;
    for (unsigned i = 0; i < 32; ++i) {
        size_t n = strlen(kSpritePrefixes[i]);
        if (strncmp(name, kSpritePrefixes[i], n) == 0 &&
            (name[n] == '_' || (name[n] >= '0' && name[n] <= '9')))
            return i;
    }
    return -1;
}

struct Audit {
    unsigned checked = 0, missing = 0;
    uint32_t primary_characters = 0;
    void Add(const char *name, bool exists)
    {
        int character = RosterFile(name);
        if (character < 0) return;
        ++checked;
        if (!exists) ++missing;
        if (EndsWith(name, "_Pict.bin") && exists)
            primary_characters |= uint32_t(1) << character;
    }
    bool Complete() const
    {
        // The supported game's manifest contains 1985 combat files for its
        // 32 characters, excluding optional third-color/legacy files.
        return checked >= 1985 && missing == 0 && primary_characters == UINT32_MAX;
    }
};
}
#endif
