#include <assert.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <set>

#include "type_enumeration.h"


struct FieldData
{
    size_t size = 0;
    // TODO: / NOTE: there's wacky silly ways I could handle outputting/managing this internal field data.
    // But honestly i think an enum and a switch case with printf specifiers is fine
    Type type;
    char* data = 0;
    #define MAX_IDENTIFIER_LENGTH (2048)
    char name[MAX_IDENTIFIER_LENGTH] = {0};
};
bool AreFieldsSame(const FieldData* first, const FieldData* second)
{
    return first->size == second->size && first->type == second->type && strncmp(first->name, second->name, MAX_IDENTIFIER_LENGTH) == 0;
}

struct FormatLayout
{
    uint32_t magic = 0;
    size_t fieldsCount = 0;
    FieldData fields[];
};
size_t GetStructureSize(FormatLayout* layout)
{
    size_t result = 0;
    for (size_t i = 0; i < layout->fieldsCount; i++)
    {
        result += layout->fields[i].size;
    }
    return result;
}
const FieldData* DoesFormatHaveField(const FormatLayout* layout, const FieldData* field)
{
    FieldData* result = nullptr;
    for (int i = 0; i < layout->fieldsCount; i++)
    {
        if (AreFieldsSame(&layout->fields[i], field))
        {
            return &layout->fields[i];
        }
    }
    return result;
}
void PrintMe(FormatLayout* layout)
{
    printf("magic: %u", layout->magic);
    printf("num fields: %ull", layout->fieldsCount);
    for (int i = 0; i < layout->fieldsCount; i++)
    {
        printf("field: %s\n", layout->fields[i].name);
        printf("size: %ull", layout->fields[i].size);
        printf("data as str: %.*s\n", layout->fields[i].size, layout->fields[i].data);
    }
}

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
};
void PrintMe(ExampleFileFormat* fileformat)
{
    printf("ExampleFileFormat:");
    printf("x = %i\n", fileformat->x);
    printf("pos = %f %f %f\n", fileformat->pos.x, fileformat->pos.y, fileformat->pos.z);
    printf("name = %s\n", fileformat->name);
    printf("counter = %lu\n", fileformat->counter);
}

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


// when merging, we require 6 pieces of info
// base revision, local revision and remote revision
// each needing the file format layout metadata, and the actual file contents
// TODO: the "ExampleFileFormat" is temp for testing and iterating. Make it generic in the future
FormatLayout MergeFormats(
    const FormatLayout& base, 
    const FormatLayout& local, 
    const FormatLayout& remote,
    const ExampleFileFormat& fileBase,
    const ExampleFileFormat& fileLocal,
    const ExampleFileFormat& fileRemote)
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
    // here is the meat. Merging arbitrary structures...

    // a single "instance" of a field reordering in some revision
    // "foreign" means not-my-revision. So in terms of local changes, the foreign changes are remote and vise-versa
    // since we do diff checking from the "perspective" of some revision, that revision is called "native"
    struct Reorder
    {
        uint32_t nativeFieldIndex = UINT32_MAX; // the index of "our own" field
        uint32_t foreignFieldIndex = UINT32_MAX; // the index of the "foreign" field that was once *this* field, but has now been reordered
    };
    // 1 of these per "revision" I.E. local/remote changes
    // EX: unique added fields in local changes
    struct RevisionData
    {
        std::set<FieldData*> addedFields = {};
        std::set<FieldData*> removedFields = {};
        std::set<Reorder> reoderedFields = {};
    };
    struct MergeScratchpad
    {
        RevisionData localData = {};
        RevisionData remoteData = {};
    };
    // first, from the perspective of the local changes
    // General idea: look at changes from the perspective of local, against remote, using a base.
    // the vise-versa, from the perspective of remote, against local, using the same base.
    const FormatLayout& nativeLayout = local;
    const FormatLayout& foreignLayout = remote;
    for (size_t i = 0; i < nativeLayout.fieldsCount; i++)
    {
        const FieldData& baseField = base.fields[i];
        const FieldData& nativeField = nativeLayout.fields[i];
        const FieldData& foreignField = foreignLayout.fields[i];
        FieldData& mergedResultField = mergedResult.fields[i];

        // need to handle a few cases
        
        // first, case where all fields match up on all revisions
        // in that case, can just iterate through fields and check for modifications, using AtomicMergeModificationResult

        //=========== next, case where field(s) have been added
        // does this field exist in both local and remote? if so, it has not been added and noop
        // if it exists in one, and not the other, add it to the relevant revision's added fields list

        const FieldData* isForeignFieldInNativeLayout = DoesFormatHaveField(&nativeLayout, &foreignField);


        // ==========

        // next, case where field(s) have been removed

        // next, case where field(s) have been re-ordered
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

    FormatLayout merged = MergeFormats(
        ExampleFileFormatHardcodedMetadata, 
        ExampleFileFormatHardcodedMetadata,
        ExampleFileFormatHardcodedMetadata,
        base, local, remote);
    printf("Resulting merged data:\n");
    PrintMe(&merged);
    
    return 0;
}
