#include <cstdio>
#include <string.h>

#include "PDB.h"
#include "PDB_RawFile.h"
#include "PDB_InfoStream.h"
#include "PDB_DBIStream.h"
#include "PDB_TPIStream.h"
#include "PDB_NamesStream.h"

#include "typetable.h"

#include "mapped_file.h"
#include <vector>

#define MODULE_LOCAL_PATH_START "C:\\Dev"

// mostly 1:1 with
// https://github.com/MolecularMatters/raw_pdb/blob/main/src/Examples/ExampleMain.cpp
PDB_NO_DISCARD static bool HasValidDBIStreams(const PDB::RawFile& rawPdbFile, const PDB::DBIStream& dbiStream);
PDB_NO_DISCARD static bool IsError(PDB::ErrorCode errorCode);


void public_symbols_stream(
    const PDB::PublicSymbolStream& publicSymbolStream,
    const PDB::CoalescedMSFStream& symbolRecordStream,
    const PDB::ImageSectionStream& imageSectionStream)
{
    const PDB::ArrayView<PDB::HashRecord> hashRecords = publicSymbolStream.GetRecords();
    const size_t count = hashRecords.GetLength();
    printf("=== Public symbols stream ===\n");
    for (const PDB::HashRecord& hashRecord : hashRecords)
    {
        const PDB::CodeView::DBI::Record* record = publicSymbolStream.GetRecord(symbolRecordStream, hashRecord);
        if (record->header.kind != PDB::CodeView::DBI::SymbolRecordKind::S_PUB32)
        {
            // normally, a PDB only contains S_PUB32 symbols in the public symbol stream, but we have seen PDBs that also store S_CONSTANT as public symbols.
            // ignore these.
            continue;
        }
        const uint32_t rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_PUB32.section, record->data.S_PUB32.offset);
        if (rva == 0u)
        {
            // certain symbols (e.g. control-flow guard symbols) don't have a valid RVA, ignore those
            continue;
        }
        printf("%s : %i\n", record->data.S_PUB32.name, rva);
    }
    printf("===================\n");
}

void global_symbols_stream(
    const PDB::GlobalSymbolStream& globalSymbolStream,
    const PDB::CoalescedMSFStream& symbolRecordStream,
    const PDB::ImageSectionStream& imageSectionStream)
{
    const PDB::ArrayView<PDB::HashRecord> hashRecords = globalSymbolStream.GetRecords();
    const size_t count = hashRecords.GetLength();
    printf("=== Global symbols stream ===\n");
    for (const PDB::HashRecord& hashRecord : hashRecords)
    {
        const PDB::CodeView::DBI::Record* record = globalSymbolStream.GetRecord(symbolRecordStream, hashRecord);
        const char* name = nullptr;
        uint32_t rva = 0u;
        switch (record->header.kind)
        {
            case PDB::CodeView::DBI::SymbolRecordKind::S_GDATA32:
            {
                name = record->data.S_GDATA32.name;
                rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GDATA32.section, record->data.S_GDATA32.offset);
            } break;
            case PDB::CodeView::DBI::SymbolRecordKind::S_GTHREAD32:
            {
                name = record->data.S_GTHREAD32.name;
                rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GTHREAD32.section, record->data.S_GTHREAD32.offset);
            } break;
            case PDB::CodeView::DBI::SymbolRecordKind::S_LDATA32:
            {
                name = record->data.S_LDATA32.name;
                rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LDATA32.section, record->data.S_LDATA32.offset);
            } break;
            case PDB::CodeView::DBI::SymbolRecordKind::S_LTHREAD32:
            {
                name = record->data.S_LTHREAD32.name;
                rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LTHREAD32.section, record->data.S_LTHREAD32.offset);
            } break;
            case PDB::CodeView::DBI::SymbolRecordKind::S_UDT:
            {
                name = record->data.S_UDT.name;
            } break;
            case PDB::CodeView::DBI::SymbolRecordKind::S_UDT_ST:
            {
                name = record->data.S_UDT_ST.name;
            } break;
            default:
            {
                //printf("Ignored symbol record kind: %i\n", static_cast<int>(record->header.kind));
            } break;
        }
        if (name && strstr(name, "g_appstate"))
        {
            printf("yippiie\n");
        }
        if (rva == 0u)
        {
            // certain symbols (e.g. control-flow guard symbols) don't have a valid RVA, ignore those
            continue;
        }
        printf("%s : %i\n", name, rva);
    }
    printf("===================\n");
}

void module_symbol_stream(
    const PDB::ModuleInfoStream& moduleInfoStream,
    const PDB::CoalescedMSFStream& symbolRecordStream,
    const PDB::ImageSectionStream& imageSectionStream,
    const PDB::RawFile& rawPdbFile)
{
    printf("=== Module symbols stream ===\n");
    const PDB::ArrayView<PDB::ModuleInfoStream::Module> modules = moduleInfoStream.GetModules();
    printf("LinkerModule: %s\n", moduleInfoStream.FindLinkerModule()->GetName().begin());
    for (const PDB::ModuleInfoStream::Module& module : modules)
    {
        if (!module.HasSymbolStream())
        {
            continue;
        }
        static const char* excludedSubstrs[] = 
        {
            "Microsoft Visual Studio",
            "Windows Kits",
        };
        const char* moduleObjname = module.GetObjectName().begin();
        const char* moduleName = module.GetName().begin();
        bool shouldPrint = true;
        for (int i = 0; i < sizeof(excludedSubstrs)/sizeof(excludedSubstrs[0]); i++)
        {
            if (strstr(moduleObjname, excludedSubstrs[i]) || strstr(moduleName, excludedSubstrs[i]))
            {
                shouldPrint = false;
                break;
            }
        }
        if (shouldPrint)
        {
            // when passing source files straight through to the linker
            // (not using -c to compile to obj, then manually linking)
            // those obj files end up in AppData/Local/Temp (at least on my machine)
            bool isModuleTempFile = strstr(moduleName, "AppData\\Local\\Temp");
            bool isModuleLocalCode = strstr(moduleName, "C:\\Dev\\pdbparse_playground");
            if (isModuleTempFile || isModuleLocalCode)
            {
                printf("===== module  \n\t%s\n\t%s\n", moduleObjname, moduleName);
            }
        }
        const PDB::ModuleSymbolStream moduleSymbolStream = module.CreateSymbolStream(rawPdbFile);
        moduleSymbolStream.ForEachSymbol([&imageSectionStream](const PDB::CodeView::DBI::Record* record)
        {
            const char* name = nullptr;
            uint32_t rva = 0u;
            if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_THUNK32)
            {
                if (record->data.S_THUNK32.thunk == PDB::CodeView::DBI::ThunkOrdinal::TrampolineIncremental)
                {
                    // we have never seen incremental linking thunks stored inside a S_THUNK32 symbol, but better be safe than sorry
                    name = "ILT";
                    rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_THUNK32.section, record->data.S_THUNK32.offset);
                }
            }
            else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_TRAMPOLINE)
            {
                // incremental linking thunks are stored in the linker module
                name = "ILT";
                rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_TRAMPOLINE.thunkSection, record->data.S_TRAMPOLINE.thunkOffset);
            }
            else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_BLOCK32)
            {
                // blocks never store a name and are only stored for indicating whether other symbols are children of this block
            }
            else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LABEL32)
            {
                // labels don't have a name
            }
            else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LPROC32)
            {
                name = record->data.S_LPROC32.name;
                rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LPROC32.section, record->data.S_LPROC32.offset);
            }
            else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_GPROC32)
            {
                name = record->data.S_GPROC32.name;
                rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GPROC32.section, record->data.S_GPROC32.offset);
            }
            else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LPROC32_ID)
            {
                name = record->data.S_LPROC32_ID.name;
                rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LPROC32_ID.section, record->data.S_LPROC32_ID.offset);
            }
            else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_GPROC32_ID)
            {
                name = record->data.S_GPROC32_ID.name;
                rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GPROC32_ID.section, record->data.S_GPROC32_ID.offset);
            }
            else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_REGREL32)
            {
                name = record->data.S_REGREL32.name;
                // You can only get the address while running the program by checking the register value and adding the offset
            }
            else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LDATA32)
            {
                name = record->data.S_LDATA32.name;
                rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LDATA32.section, record->data.S_LDATA32.offset);
            }
            else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LTHREAD32)
            {
                name = record->data.S_LTHREAD32.name;
                rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LTHREAD32.section, record->data.S_LTHREAD32.offset);
            }

            if (rva == 0u)
            {
                // certain symbols (e.g. control-flow guard symbols) don't have a valid RVA, ignore those
                return;
            }
            //printf("%s : %i\n", name, rva);
        });
        //printf("===== end module\n");
    }
    printf("===================\n");
}

void collect_relevant_modules(const PDB::ModuleInfoStream& moduleInfoStream, std::vector<const PDB::ModuleInfoStream::Module*>& outModules)
{
    const PDB::ArrayView<PDB::ModuleInfoStream::Module> modules = moduleInfoStream.GetModules();
    //printf("LinkerModule: %s\n", moduleInfoStream.FindLinkerModule()->GetName().begin());
    for (const PDB::ModuleInfoStream::Module& module : modules)
    {
        if (!module.HasSymbolStream())
        {
            continue;
        }
        static const char* excludedSubstrs[] = 
        {
            "Microsoft Visual Studio",
            "Windows Kits",
        };
        const char* moduleObjname = module.GetObjectName().begin();
        const char* moduleName = module.GetName().begin();
        bool shouldPrint = true;
        for (int i = 0; i < sizeof(excludedSubstrs)/sizeof(excludedSubstrs[0]); i++)
        {
            if (strstr(moduleObjname, excludedSubstrs[i]) || strstr(moduleName, excludedSubstrs[i]))
            {
                shouldPrint = false;
                break;
            }
        }
        if (shouldPrint)
        {
            // when passing source files straight through to the linker
            // (not using -c to compile to obj, then manually linking)
            // those obj files end up in AppData/Local/Temp (at least on my machine)
            bool isModuleTempFile = strstr(moduleName, "AppData\\Local\\Temp");
            bool isModuleLocalCode = strstr(moduleName, MODULE_LOCAL_PATH_START);
            if (isModuleTempFile || isModuleLocalCode)
            {
                outModules.push_back(&module);
                //printf("===== module  \n\t%s\n\t%s\n", moduleObjname, moduleName);
            }
        }
    }
}

bool prefix(const char *pre, const char *str)
{
    return strncmp(pre, str, strlen(pre)) == 0;
}

void process_possible_data_symbol(
    const PDB::CodeView::DBI::Record* record,
    const TypeTable& typeTable)
{
    using SRK = PDB::CodeView::DBI::SymbolRecordKind;
    const SRK kind = record->header.kind;
    const PDB::CodeView::DBI::Record::Data& data = record->data;
    #define SANITY_CHECK_NAME(symbolRecordKind) if (strstr(data.symbolRecordKind.name, "@")) break; if (prefix("__", data.symbolRecordKind.name)) break;
    switch (kind)
    {
        case SRK::S_LDATA32:  // (static) local data
        {
            SANITY_CHECK_NAME(S_LDATA32);
            //std::string varTypeName = GetVariableTypeName(typeTable, data.S_LDATA32.typeIndex);
            printf("\tFound static local data %s\n", data.S_LDATA32.name);
        } break;
        case SRK::S_GDATA32:  // global data
        {
            SANITY_CHECK_NAME(S_GDATA32);
            printf("\tFound global data %s\n", data.S_GDATA32.name);
        } break;
        case SRK::S_PUB32:  // public symbol
        {
            SANITY_CHECK_NAME(S_PUB32);
            printf("\tFound public symbol %s\n", data.S_PUB32.name);
        } break;
        case SRK::S_LTHREAD32:  // (static) thread-local data
        {
            SANITY_CHECK_NAME(S_LTHREAD32);
            printf("\tFound (static) thread-local data %s\n", data.S_LTHREAD32.name);
        } break;
        case SRK::S_GTHREAD32:  // global thread-local data
        {
            SANITY_CHECK_NAME(S_GTHREAD32);
            printf("\tFound global thread-local data %s\n", data.S_GTHREAD32.name);
        } break;
        default:
        {
            // schmeep schmop
        } break;
    }
    
}

void ProcessSymbols(
    const PDB::RawFile& rawPdbFile, 
    const PDB::DBIStream& dbiStream, 
    const PDB::TPIStream& tpiStream)
{
    // needed for both public and global streams
    const PDB::CoalescedMSFStream symbolRecordStream = dbiStream.CreateSymbolRecordStream(rawPdbFile);
    const PDB::PublicSymbolStream publicSymbolStream = dbiStream.CreatePublicSymbolStream(rawPdbFile);
	const PDB::GlobalSymbolStream globalSymbolStream = dbiStream.CreateGlobalSymbolStream(rawPdbFile);
	const PDB::ModuleInfoStream moduleInfoStream = dbiStream.CreateModuleInfoStream(rawPdbFile);
    const PDB::ImageSectionStream imageSectionStream = dbiStream.CreateImageSectionStream(rawPdbFile);
    TypeTable typeTable(tpiStream);

    //public_symbols_stream(publicSymbolStream, symbolRecordStream, imageSectionStream);
    //global_symbols_stream(globalSymbolStream, symbolRecordStream, imageSectionStream);
	//module_symbol_stream(moduleInfoStream, symbolRecordStream, imageSectionStream, rawPdbFile);
    std::vector<const PDB::ModuleInfoStream::Module*> relevantModules = {};
    relevantModules.reserve(50);
    collect_relevant_modules(moduleInfoStream, relevantModules);
    printf("processing %zu relevant modules\n", relevantModules.size());
    for (const PDB::ModuleInfoStream::Module* module : relevantModules)
    {
        const PDB::DBI::ModuleInfo* moduleInfo = module->GetInfo();
        if (module->HasSymbolStream())
        {
            printf("module\n{\n%s\n", module->GetName().begin());
            PDB::ModuleSymbolStream moduleSymbolStream = module->CreateSymbolStream(rawPdbFile);
            moduleSymbolStream.ForEachSymbol([&typeTable](const PDB::CodeView::DBI::Record* record){
                process_possible_data_symbol(record, typeTable);
            });
            printf("\n}\n");
        }
    }
}

int main()
{
    // open memmapped pdb file
    const char* pdbPath = "Axe64Lib.pdb";
    MemoryMappedFile::Handle pdbFile = MemoryMappedFile::Open(pdbPath);
    void* pdbFileData = pdbFile.baseAddress;
    // make sure it's well-formed
    if (PDB::ValidateFile(pdbFileData, pdbFile.len) != PDB::ErrorCode::Success)
    {
        return 1;
    }
    // use pdb data to set up streams and indexes and such
    const PDB::RawFile rawPdbFile = PDB::CreateRawFile(pdbFile.baseAddress);
    if (PDB::HasValidDBIStream(rawPdbFile) != PDB::ErrorCode::Success)
	{
		MemoryMappedFile::Close(pdbFile);
		return 2;
	}
    // sets up offsets for info stream (we just use this here for fastlink validation)
	const PDB::InfoStream infoStream(rawPdbFile);
    // more validation
	if (infoStream.UsesDebugFastLink())
	{
		// PDB was linked using unsupported option /DEBUG:FASTLINK
		MemoryMappedFile::Close(pdbFile);
		return 3;
	}
    // infostream also gets us a lot of info about the pdb itself
    const auto h = infoStream.GetHeader();
	printf("Version %u, signature %u, age %u, GUID %08x-%04x-%04x-%02x%02x%02x%02x%02x%02x%02x%02x\n",
		static_cast<uint32_t>(h->version), h->signature, h->age,
		h->guid.Data1, h->guid.Data2, h->guid.Data3,
		h->guid.Data4[0], h->guid.Data4[1], h->guid.Data4[2], h->guid.Data4[3], h->guid.Data4[4], h->guid.Data4[5], h->guid.Data4[6], h->guid.Data4[7]);

    // dbi stream has a lot of the good stuff - info about how program was compiled,
    // (compilation flags etc), compilands, source files, and references to other streams
    const PDB::DBIStream dbiStream = PDB::CreateDBIStream(rawPdbFile);
	if (!HasValidDBIStreams(rawPdbFile, dbiStream))
	{
		MemoryMappedFile::Close(pdbFile);
		return 4;
	}
    // info about types used in the program
	const PDB::TPIStream tpiStream = PDB::CreateTPIStream(rawPdbFile);
	if (PDB::HasValidTPIStream(rawPdbFile) != PDB::ErrorCode::Success)
	{
		MemoryMappedFile::Close(pdbFile);
	    return 5;
	}
    ProcessSymbols(rawPdbFile, dbiStream, tpiStream);
	MemoryMappedFile::Close(pdbFile);
    #ifdef _WIN32
    //system("pause");
    #endif
    return 0;
}


PDB_NO_DISCARD static bool IsError(PDB::ErrorCode errorCode)
	{
		switch (errorCode)
		{
			case PDB::ErrorCode::Success:
				return false;

			case PDB::ErrorCode::InvalidSuperBlock:
				printf("Invalid Superblock\n");
				return true;

			case PDB::ErrorCode::InvalidFreeBlockMap:
				printf("Invalid free block map\n");
				return true;

			case PDB::ErrorCode::InvalidStream:
				printf("Invalid stream\n");
				return true;

			case PDB::ErrorCode::InvalidSignature:
				printf("Invalid stream signature\n");
				return true;

			case PDB::ErrorCode::InvalidStreamIndex:
				printf("Invalid stream index\n");
				return true;

			case PDB::ErrorCode::UnknownVersion:
				printf("Unknown version\n");
				return true;
            case PDB::ErrorCode::InvalidDataSize:
                printf("Invalid data size\n");
                return true;
		}

		// only ErrorCode::Success means there wasn't an error, so all other paths have to assume there was an error
		return true;
	}

PDB_NO_DISCARD static bool HasValidDBIStreams(const PDB::RawFile& rawPdbFile, const PDB::DBIStream& dbiStream)
{
    // check whether the DBI stream offers all sub-streams we need
    if (IsError(dbiStream.HasValidSymbolRecordStream(rawPdbFile)))
    {
        return false;
    }

    if (IsError(dbiStream.HasValidPublicSymbolStream(rawPdbFile)))
    {
        return false;
    }

    if (IsError(dbiStream.HasValidGlobalSymbolStream(rawPdbFile)))
    {
        return false;
    }

    if (IsError(dbiStream.HasValidSectionContributionStream(rawPdbFile)))
    {
        return false;
    }

    if (IsError(dbiStream.HasValidImageSectionStream(rawPdbFile)))
    {
        return false;
    }

    return true;
}