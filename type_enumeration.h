#pragma once




enum Type: int
{
    BYTE,
    SHORT,
    INTEGER,
    FLOAT,
    DOUBLE,
    LONG,
    CSTRING,
    SIZEDBUFFER,
    STRUCTURE, // for recursive structures, we can have the field's "data" point to another full FormatLayout structure

    NUM_TYPES,
};