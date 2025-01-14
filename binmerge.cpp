#include <assert.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "type_enumeration.h"

struct FieldData
{
    size_t size = 0;
    // TODO: / NOTE: there's wacky silly ways I could handle outputting/managing this internal field data.
    // But honestly i think an enum and a switch case with printf specifiers is fine
    Type type;
    char* data = 0;
    char name[40] = {0};
};

struct FormatLayout
{
    uint32_t magic = 0;
    size_t fieldsCount = 0;
    FieldData fields[];
    size_t GetStructureSize() const
    {
        size_t result = 0;
        for (size_t i = 0; i < fieldsCount; i++)
        {
            result += fields[i].size;
        }
        return result;
    }
};

// -----------------------------
// EXAMPLE HARDCODED TYPE
struct Vector3
{
    float x, y, z;
};
struct ExampleFileFormat
{
    uint32_t magic = 0xDEADBEEF;
    uint32_t x;
    Vector3 pos;
    char name[20];
    uint64_t counter;
    void PrintMe()
    {
        printf("ExampleFileFormat:");
        printf("x = %i\n", x);
        printf("pos = %f %f %f\n", pos.x, pos.y, pos.z);
        printf("name = %s\n", name);
        printf("counter = %lu\n", counter);
    }
};
static FormatLayout ExampleFileFormatHardcodedMetadata =
{
    .magic = 0xDEADBEEF,
    .fieldsCount = 4,
    .fields =
    {
        {
            .size = sizeof(ExampleFileFormat::x),
            //Types::INTEGER,
            .name = "x",
        },
        {
            .size = sizeof(ExampleFileFormat::pos),
            //Types::STRUCTURE,
            .name = "pos",
        },
        {
            .size = sizeof(ExampleFileFormat::name),
            //Types::CSTRING,
            .name = "name",
        },
        {
            .size = sizeof(ExampleFileFormat::counter),
            //Types::LONG,
            .name = "counter",
        }
    },
};
// ----------------------------

// logic for handling a modification merge at the finest granularity (a single struct field)
bool AtomicMergeModificationResult(const FieldData& base, const FieldData& local, const FieldData& remote, FieldData& merged)
{
    // assuming these are all the same size, which is an incorrect assumption....
#define COMPARE(one, two) (one.size == two.size && memcmp(&one.data, &two.data, one.size) == 0)
    bool baseToLocal = COMPARE(base, local);
    bool baseToRemote = COMPARE(base, remote);
    if (baseToLocal && baseToRemote)
    {
        // no changes
        merged = base;
    }
    bool localToRemote = COMPARE(local, remote);
    if (localToRemote)
    {
        // same change, return that change
        merged = local;
    }
    if (baseToLocal && !baseToRemote)
    {
        // base is same as local, but remote has different changes, so we merge remote changes here
        merged = remote;
    }
    if (!baseToLocal && baseToRemote)
    {
        // local differs from base, but remote is same, merge in local
        merged = local;
    }
    if (!baseToLocal && !baseToRemote && !localToRemote)
    {
        // both local and remote have made *different* changes, this is a merge conflict
        return false;
    }
    return true;
}

FormatLayout MergeFormats(const FormatLayout& base, const FormatLayout& local, const FormatLayout& remote)
{
    // we never expect the magic to change. 
    auto magic = base.magic;
    bool localMagicMatch = memcmp(&local.magic, &magic, sizeof(magic)) == 0;
    bool remoteMagicMatch = memcmp(&remote.magic, &magic, sizeof(magic)) == 0;
    if (!localMagicMatch || !remoteMagicMatch)
    {
        printf("magic not matching! failed to merge\n");
        return {};
    }
    FormatLayout mergedResult = {0};
    // how to merge arbitrary potentially recursively complex structures...
    for (size_t i = 0; i < base.fieldsCount; i++)
    {
        // need to handle a few cases
        
        // first, case where all fields match up on all revisions
        // in that case, can just iterate through fields and check for modifications, using AtomicMergeModificationResult

        // next, case where field(s) have been added

        // next, case where field(s) have been removed

        // next, case where field(s) have been re-ordered
        
        const auto& baseField = base.fields[i];
        const auto& localField = local.fields[i];
        const auto& remoteField = remote.fields[i];
        auto& mergedResultField = mergedResult.fields[i];
        //Types type = formatMetadata.fields[i].type;
        // if (!AtomicMergeResult(baseField, localField, remoteField, mergedResultField)) 
        // { 
        //     printf("merge conflict! %s\n", base.fields[i].name); 
        //     __debugbreak(); 
        // } 
    }
    return mergedResult;
}

// terms:
// base = original version of the file before changes
// local = your changes (p4 calls this "target")
// remote = someone else's changes (being merged against yours) (p4 calls this "source")
int main(int argc, char* argv[])
{
    ExampleFileFormat base =
    {
        .x = 10,
        .pos = Vector3{ 1.0f, 2.0f, 3.0f },
        .name = "test",
        .counter = 123
    };
    ExampleFileFormat local =
    {
        .x = 10,
        .pos = Vector3{ 1.0f, 2.0f, 3.0f },
        .name = "testlocal",
        .counter = 123
    };
    ExampleFileFormat remote =
    {
        .x = 10,
        .pos = Vector3{ 1.0f, 2.0f, 3.0f },
        .name = "testremote",
        .counter = 123
    };

    FormatLayout merged = MergeFormats(ExampleFileFormatHardcodedMetadata, &base, &local, &remote);
    printf("Resulting merged data:\n");
    merged->PrintMe();
    
    return 0;
}
