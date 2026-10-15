#include "../loader/asset_pack.h"
#include <cassert>
#include <cstdio>
int main(int argc, char **argv)
{
    assert(asset_pack::RosterFile("honda_01.obm") >= 0);
    assert(asset_pack::RosterFile("ryu01_2P.obm") >= 0);
    assert(asset_pack::RosterFile("ryu01_3P.obm") == -1);
    assert(asset_pack::RosterFile("ryu_voice.wav") == -1);
    assert(asset_pack::RosterFile("ryu_../escape.obm") == -1);
    assert(asset_pack::RosterFile("bgm_ryu.ckb") == -1);
    asset_pack::Audit audit;
    assert(!audit.Complete());
    if (argc != 3) { puts("Pass a manifest and extracted asset directory for integration checks"); return 0; }
    FILE *manifest = fopen(argv[1], "r"); assert(manifest);
    char line[512], path[1024];
    asset_pack::Audit missing;
    bool removed_one = false;
    while (fgets(line, sizeof(line), manifest)) {
        char *comma = strchr(line, ','); if (!comma) continue; *comma = 0;
        if (asset_pack::RosterFile(line) < 0) continue;
        snprintf(path, sizeof(path), "%s/%s", argv[2], line);
        FILE *f = fopen(path, "rb");
        bool exists = f != nullptr;
        if (f) fclose(f);
        audit.Add(line, exists);
        missing.Add(line, exists && removed_one);
        removed_one = true;
    }
    fclose(manifest);
    assert(audit.Complete());
    assert(!missing.Complete());
    printf("Roster assets: %u files verified; incomplete pack rejected\n", audit.checked);
}
