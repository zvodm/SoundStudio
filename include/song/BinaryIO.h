#pragma once
#include <cstdio>
#include <cstdint>
#include <string>

// Tiny raw binary read/write helpers shared by SongIO.cpp (whole-song
// files) and InstrumentIO.cpp (standalone instrument presets), so both use
// the exact same primitives instead of two copies that could drift apart.

template <typename T>
inline void WriteRaw(FILE* f, const T& value)
{
    fwrite(&value, sizeof(T), 1, f);
}

template <typename T>
inline bool ReadRaw(FILE* f, T& value)
{
    return fread(&value, sizeof(T), 1, f) == 1;
}

inline void WriteString(FILE* f, const std::string& s)
{
    uint32_t len = (uint32_t)s.size();
    WriteRaw(f, len);
    if (len > 0) fwrite(s.data(), 1, len, f);
}

inline bool ReadString(FILE* f, std::string& s)
{
    uint32_t len = 0;
    if (!ReadRaw(f, len)) return false;
    s.resize(len);
    if (len > 0 && fread(s.data(), 1, len, f) != len) return false;
    return true;
}
