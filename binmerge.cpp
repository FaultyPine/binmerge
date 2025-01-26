#include <assert.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <set>

#include "type_enumeration.h"


// since we are merging at the "field granularity", we will never
// need to do an intelligent merge inside of a field itself. (unless maybe the field is itself another structure?)
// For that reason, we can treat any field as being uniquely identified by it's name (and size).
// then, the data for the field is just an opaque sized buffer
constexpr uint32_t INVALID_FIELD_INDEX = UINT32_MAX;
struct FieldData
{
    size_t size = 0; // size 0 means "empty field"
    char* data = nullptr;
    #define MAX_IDENTIFIER_LENGTH (2048) // this is the actual *max* for most compilers, reasonably, it could be smaller
    char name[MAX_IDENTIFIER_LENGTH] = {0};
};
bool AreFieldsSame(const FieldData* first, const FieldData* second)
{
    return first->size == second->size && strncmp(first->name, second->name, MAX_IDENTIFIER_LENGTH) == 0;
}
bool IsFieldEmpty(const FieldData* field)
{
    return field->size == 0;
}

struct FormatLayout
{
    uint32_t magic = 0;
    size_t fieldsCount = 0;
    FieldData* fields;
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
const FieldData* DoesFormatHaveField(const FormatLayout* layout, const FieldData* field, uint32_t* fieldIndexOut = nullptr)
{
    FieldData* result = nullptr;
    for (int i = 0; i < layout->fieldsCount; i++)
    {
        if (AreFieldsSame(&layout->fields[i], field))
        {
            if (fieldIndexOut) { *fieldIndexOut = i; }
            return &layout->fields[i];
        }
    }
    return result;
}
void PrintMe(FormatLayout* layout)
{
    printf("magic: %u", layout->magic);
    printf("num fields: %zu", layout->fieldsCount);
    for (int i = 0; i < layout->fieldsCount; i++)
    {
        printf("field: %s\n", layout->fields[i].name);
        printf("size: %zu", layout->fields[i].size);
        printf("data as str: %.*s\n", (int)layout->fields[i].size, layout->fields[i].data);
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
    printf("counter = %I64u\n", fileformat->counter);
}

static FormatLayout ExampleFileFormatHardcodedMetadata =
{
    .magic = 0xDEADBEEF,
    .fieldsCount = 4,
    .fields = new FieldData[]
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

/* 
https://homes.cs.washington.edu/~mernst/pubs/merge-evaluation-ase2024.pdf
The resolution phase of three-way merging uses the following
algorithm. For each change C in a 3-way diff, let C1 be the difference
between the base and parent 1 and let C2 be the difference between
the base and parent 2. C1 and C2 are at the same location in the
source code.
• If C1 is the same as C2, use it; equivalently, if parent 1 is the
same as parent 2, use it.
• If C1 is empty, use C2; equivalently, if the base is the same as
parent 1, use parent 2.
• If C2 is empty, use C1; equivalently, if the base is the same as
parent 2, use parent 1.
• If C1 differs from C2, report a conflict; equivalently, if the base,
parent 1, and parent 2 all differ, report a conflict.
// TODO: make sure im covering each of these cases...
*/

// logic for handling a modification merge at the finest granularity (a single struct field)
bool AtomicMergeModificationResult(
    const FieldData& base, 
    const FieldData& local, // "parent 1"
    const FieldData& remote,  // "parent 2"
    FieldData& merged)
{
    // TODO: this isn't complete yet
    // first: clean up data comparison logic using above comment
    // do proper merge logic for the *names* of these fields too

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
        uint32_t originalIdx = INVALID_FIELD_INDEX;
        uint32_t newIdx = INVALID_FIELD_INDEX;
        Reorder(uint32_t ogIdx, uint32_t newIdx) : originalIdx(ogIdx), newIdx(newIdx) {}
    };
    // 1 of these per "revision" I.E. local/remote changes
    // EX: unique added fields in local changes from the perspective of the base revision
    // these are like a "delta" with the base revision.
    struct RevisionData
    {
        std::set<const FieldData*> addedFields = {};
        std::set<const FieldData*> removedFields = {};
        std::set<Reorder> reoderedFields = {};
    };
    struct MergeScratchpad
    {
        RevisionData nativeData = {};
        RevisionData foreignData = {};
    };
    MergeScratchpad scratchpad = {};
    // ~~first, from the perspective of the local changes~~
    // ~~General idea: look at changes from the perspective of local, against remote, using a base.~~
    // ~~then vise-versa, from the perspective of remote, against local, using the same base.~~
    // IGNORE ABOVE:
    // new idea
    // diff first revision against base
    // diff other revision against base (local, then remote)
    // then take those diffs and diff them to come up with the merged result (diff the diffs!)
    // so here we take some arbitrary layout, and diff it against the base layout
    // BIG TODO: BOOKMARK: also take name changes and data changes into account here
    // though, changing a field's name and reordering it is basically like removing the old and adding a new field...
    //      how to tell the difference? ^ do we even need to tell the difference? It might be fine to just say "if you rename and reorder a field, it's the same as removing that field and adding a new one with the new name/index"
    auto diffAgainstBaseRevision = [](const FormatLayout& base, const FormatLayout& revisionLayout)
    {
        RevisionData revisionDiff = {};
        // first, looking through base fields
        // from the perspective of the base fields, we can find which fields have been removed and reordered
        for (size_t i = 0; i < base.fieldsCount; i++)
        {
            const FieldData* baseField = &base.fields[i];
            uint32_t revisionLayoutBaseFieldIndex = INVALID_FIELD_INDEX;
            const FieldData* baseFieldInRevisionLayout = DoesFormatHaveField(&revisionLayout, baseField, &revisionLayoutBaseFieldIndex);
            if (revisionLayoutBaseFieldIndex != i)
            {
                // if the index of the field in our revision does not match the index
                // of the field in the base, then we've reordered the field!
                // Do note: these indices are literal indices into the list of fields, not byte-offsets.
                // meaning if a field is added before some other field, that will generate both an "added field"
                // and a "reordering" of fields that come after the added one, since their indices will shift up by 1.
                Reorder reorder(i, revisionLayoutBaseFieldIndex);
                revisionDiff.reoderedFields.insert(reorder);
            }

            // next, case where field(s) have been removed
            if (!baseFieldInRevisionLayout)
            {
                // base field is not in this layout, it has been removed
                revisionDiff.removedFields.insert(baseField);
            }
        }
        //=========== 
        // case where field(s) have been added
        for (uint32_t i = 0; i < revisionLayout.fieldsCount; i++)
        {
            // to get this, we look through our revision's fields, and if any don't exist in base, they have been added
            const FieldData* revisionField = &revisionLayout.fields[i];
            const FieldData* revisionFieldInBase = DoesFormatHaveField(&base, revisionField);
            if (!revisionFieldInBase)
            {
                revisionDiff.addedFields.insert(revisionField);
            }
        }

        return revisionDiff;
    };
    auto baseDiffLocal = diffAgainstBaseRevision(base, local);
    auto baseDiffRemote = diffAgainstBaseRevision(base, remote);
    // now we have data about the local and remote structural changes diffed against the base
    // mmmm still hazy on next steps... gonna write random crap
    // collect these structural diffs into one merged result layout?
    // then do atomic field merges on all fields in merged layout?
    const FormatLayout& nativeLayout = local;
    
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
