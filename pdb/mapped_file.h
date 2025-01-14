#ifndef _MAPPED_FILE_H
#define _MAPPED_FILE_H


// https://github.com/MolecularMatters/raw_pdb/blob/main/src/Examples/ExampleMemoryMappedFile.h

namespace MemoryMappedFile
{
	struct Handle
	{
#ifdef _WIN32
		void* file;
		void* fileMapping;
#else
		int   file;
#endif
		void* baseAddress;
		size_t len;
	};

	Handle Open(const char* path);
	void Close(Handle& handle);
}

#endif