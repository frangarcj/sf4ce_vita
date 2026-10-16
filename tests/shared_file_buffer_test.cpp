#include "../loader/shared_file_buffer.h"
#include <assert.h>
#include <stdio.h>

int main() {
    shared_file_buffer::Cache cache;
    assert(cache.Find(nullptr) == -1 && cache.Find("") == -1);
    cache.Remember("uc_ryu_1p.bmv", 2781306, 2781306);
    for (int chunk = 0; chunk < 100; ++chunk)
        assert(cache.Find("uc_ryu_1p.bmv") == 2781306);
    assert(cache.Find("uc_ken_1p.bmv") == -1);
    cache.Remember("uc_ken_1p.bmv", 1024, 2048); // A partial read is not resident.
    assert(cache.Find("uc_ryu_1p.bmv") == -1);
    assert(cache.Find("uc_ken_1p.bmv") == -1);
    cache.Remember("uc_ken_1p.bmv", -1, 2048);
    assert(cache.Find("uc_ken_1p.bmv") == -1);
    cache.Remember("savedata2.dat", 661, 661);
    assert(cache.Find("savedata2.dat") == 661);
    cache.Reset(); // Native resetOldDataFileInfo or a mutable save invalidates it.
    assert(cache.Find("savedata2.dat") == -1);
    cache.Remember("", 100, 100);
    assert(cache.Find("") == -1);
    char long_name[600];
    memset(long_name, 'a', sizeof(long_name)); long_name[599] = 0;
    cache.Remember(long_name, 100, 100);
    assert(cache.Find(long_name) == -1);
    cache.Remember("uc_ryu_1p.bmv", 2781306, 2781306);
    cache.Remember("uc_ken_1p.bmv", 100, 100);
    assert(cache.Find("uc_ryu_1p.bmv") == -1);
    assert(cache.Find("uc_ken_1p.bmv") == 100);
    puts("Shared JNI file buffer: repeated movie chunks, replacement, reset, partial reads and name bounds OK");
}
