#include "UPKInfo.h"

#include <cstdio>
#include <sstream>
#include <cstring>

UPKInfo::UPKInfo(std::istream& stream): Summary(), NoneIdx(0), ReadError(UPKReadErrors::NoErrors), Compressed(false), CompressedChunk(false)
{
    Read(stream);
}

bool UPKInfo::ReadCompressedHeader(std::istream& stream)
{
    if (!stream.good())
    {
        ReadError = UPKReadErrors::FileError;
        return false;
    }
    stream.seekg(0, std::ios::end);
    size_t Size = stream.tellg();
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(&CompressedHeader.Signature), 4);
    if (CompressedHeader.Signature != 0x9E2A83C1)
    {
        ReadError = UPKReadErrors::BadSignature;
        return false;
    }
    stream.read(reinterpret_cast<char*>(&CompressedHeader.BlockSize), 4);
    stream.read(reinterpret_cast<char*>(&CompressedHeader.CompressedSize), 4);
    stream.read(reinterpret_cast<char*>(&CompressedHeader.UncompressedSize), 4);
    CompressedHeader.NumBlocks = (CompressedHeader.UncompressedSize + CompressedHeader.BlockSize - 1) / CompressedHeader.BlockSize; // Gildor
    uint32_t CompHeadSize = 16 + CompressedHeader.NumBlocks * 8;
    Size -= CompHeadSize; /// actual compressed file size
    if (CompressedHeader.CompressedSize != Size ||
        CompressedHeader.UncompressedSize < Size ||
        CompressedHeader.UncompressedSize < CompressedHeader.CompressedSize)
    {
        ReadError = UPKReadErrors::BadVersion;
        return false;
    }
    CompressedHeader.Blocks.clear();
    for (unsigned i = 0; i < CompressedHeader.NumBlocks; ++i)
    {
        FCompressedChunkBlock Block;
        stream.read(reinterpret_cast<char*>(&Block.CompressedSize), 4);
        stream.read(reinterpret_cast<char*>(&Block.UncompressedSize), 4);
        CompressedHeader.Blocks.push_back(Block);
    }
    Compressed = true;
    CompressedChunk = true;
    ReadError = UPKReadErrors::IsCompressed;
    return false;
}

bool UPKInfo::Read(std::istream& stream)
{
    CompressedHeader = FCompressedChunkHeader{};
    if (!stream.good())
    {
        ReadError = UPKReadErrors::FileError;
        return false;
    }
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(&Summary.Signature), 4);
    if (Summary.Signature != 0x9E2A83C1)
    {
        ReadError = UPKReadErrors::BadSignature;
        return false;
    }
    int32_t tmpVer;
    stream.read(reinterpret_cast<char*>(&tmpVer), 4);
    Summary.Version = tmpVer % (1 << 16);
    Summary.LicenseeVersion = tmpVer >> 16;
    Summary.HeaderSizeOffset = stream.tellg();
    stream.read(reinterpret_cast<char*>(&Summary.HeaderSize), 4);
    stream.read(reinterpret_cast<char*>(&Summary.FolderNameLength), 4);
    if (Summary.FolderNameLength > 0)
    {
        getline(stream, Summary.FolderName, '\0');
    }
    else
    {
        Summary.FolderName = "";
    }
    stream.read(reinterpret_cast<char*>(&Summary.PackageFlags), 4);
    Summary.NameCountOffset = stream.tellg();
    stream.read(reinterpret_cast<char*>(&Summary.NameCount), 4);
    stream.read(reinterpret_cast<char*>(&Summary.NameOffset), 4);
    stream.read(reinterpret_cast<char*>(&Summary.ExportCount), 4);
    stream.read(reinterpret_cast<char*>(&Summary.ExportOffset), 4);
    stream.read(reinterpret_cast<char*>(&Summary.ImportCount), 4);
    stream.read(reinterpret_cast<char*>(&Summary.ImportOffset), 4);
    stream.read(reinterpret_cast<char*>(&Summary.DependsOffset), 4);
    stream.read(reinterpret_cast<char*>(&Summary.SerialOffset), 4);
    stream.read(reinterpret_cast<char*>(&Summary.Unknown2), 4);
    stream.read(reinterpret_cast<char*>(&Summary.Unknown3), 4);
    stream.read(reinterpret_cast<char*>(&Summary.Unknown4), 4);
    stream.read(reinterpret_cast<char*>(&Summary.GUID), sizeof(Summary.GUID));
    stream.read(reinterpret_cast<char*>(&Summary.GenerationsCount), 4);
    Summary.Generations.clear();
    for (unsigned i = 0; i < Summary.GenerationsCount; ++i)
    {
        FGenerationInfo EntryToRead;
        stream.read(reinterpret_cast<char*>(&EntryToRead.ExportCount), 4);
        stream.read(reinterpret_cast<char*>(&EntryToRead.NameCount), 4);
        stream.read(reinterpret_cast<char*>(&EntryToRead.NetObjectCount), 4);
        Summary.Generations.push_back(EntryToRead);
    }
    stream.read(reinterpret_cast<char*>(&Summary.EngineVersion), 4);
    stream.read(reinterpret_cast<char*>(&Summary.CookerVersion), 4);
    stream.read(reinterpret_cast<char*>(&Summary.CompressionFlags), 4);
    stream.read(reinterpret_cast<char*>(&Summary.NumCompressedChunks), 4);
    Compressed = ((Summary.NumCompressedChunks > 0) || (Summary.CompressionFlags != 0));
    Summary.CompressedChunks.clear();
    for (unsigned i = 0; i < Summary.NumCompressedChunks; ++i)
    {
        FCompressedChunk CompressedChunk;
        stream.read(reinterpret_cast<char*>(&CompressedChunk.UncompressedOffset), 4);
        stream.read(reinterpret_cast<char*>(&CompressedChunk.UncompressedSize), 4);
        stream.read(reinterpret_cast<char*>(&CompressedChunk.CompressedOffset), 4);
        stream.read(reinterpret_cast<char*>(&CompressedChunk.CompressedSize), 4);
        Summary.CompressedChunks.push_back(CompressedChunk);
    }
    Summary.UnknownDataChunk.clear();
    /// for uncompressed packages unknown data is located between NumCompressedChunks and NameTable
    if (Summary.NumCompressedChunks < 1 && Summary.NameOffset - stream.tellg() > 0)
    {
        Summary.UnknownDataChunk.resize(Summary.NameOffset - stream.tellg());
    }
    /// for compressed packages unknown data is located between last CompressedChunk entry and first compressed data
    else if (Summary.NumCompressedChunks > 0 && Summary.CompressedChunks[0].CompressedOffset - stream.tellg() > 0)
    {
        Summary.UnknownDataChunk.resize(Summary.CompressedChunks[0].CompressedOffset - stream.tellg());
    }
    if (Summary.UnknownDataChunk.size() > 0)
    {
        stream.read(Summary.UnknownDataChunk.data(), Summary.UnknownDataChunk.size());
    }
    if (Compressed == true)
    {
        ReadError = UPKReadErrors::IsCompressed;
        return false;
    }
    NameTable.clear();
    stream.seekg(Summary.NameOffset);
    for (unsigned i = 0; i < Summary.NameCount; ++i)
    {
        FNameEntry EntryToRead;
        EntryToRead.EntryOffset = stream.tellg();
        stream.read(reinterpret_cast<char*>(&EntryToRead.NameLength), 4);
        if (EntryToRead.NameLength > 0)
        {
            getline(stream, EntryToRead.Name, '\0');
        }
        else
        {
            EntryToRead.Name = "";
        }
        stream.read(reinterpret_cast<char*>(&EntryToRead.NameFlagsL), 4);
        stream.read(reinterpret_cast<char*>(&EntryToRead.NameFlagsH), 4);
        EntryToRead.EntrySize = (unsigned)stream.tellg() - EntryToRead.EntryOffset;
        NameTable.push_back(EntryToRead);
        if (EntryToRead.Name == "None")
            NoneIdx = i;
    }
    ImportTable.clear();
    stream.seekg(Summary.ImportOffset);
    ImportTable.push_back(FObjectImport()); /// null object (default zero-initialization)
    for (unsigned i = 0; i < Summary.ImportCount; ++i)
    {
        FObjectImport EntryToRead;
        EntryToRead.EntryOffset = stream.tellg();
        stream.read(reinterpret_cast<char*>(&EntryToRead.PackageIdx), sizeof(EntryToRead.PackageIdx));
        stream.read(reinterpret_cast<char*>(&EntryToRead.TypeIdx), sizeof(EntryToRead.TypeIdx));
        stream.read(reinterpret_cast<char*>(&EntryToRead.OwnerRef), sizeof(EntryToRead.OwnerRef));
        stream.read(reinterpret_cast<char*>(&EntryToRead.NameIdx), sizeof(EntryToRead.NameIdx));
        EntryToRead.EntrySize = (unsigned)stream.tellg() - EntryToRead.EntryOffset;
        ImportTable.push_back(EntryToRead);
    }
    ExportTable.clear();
    stream.seekg(Summary.ExportOffset);
    ExportTable.push_back(FObjectExport()); /// null-object
    for (unsigned i = 0; i < Summary.ExportCount; ++i)
    {
        FObjectExport EntryToRead;
        EntryToRead.EntryOffset = stream.tellg();
        stream.read(reinterpret_cast<char*>(&EntryToRead.TypeRef), sizeof(EntryToRead.TypeRef));
        stream.read(reinterpret_cast<char*>(&EntryToRead.ParentClassRef), sizeof(EntryToRead.ParentClassRef));
        stream.read(reinterpret_cast<char*>(&EntryToRead.OwnerRef), sizeof(EntryToRead.OwnerRef));
        stream.read(reinterpret_cast<char*>(&EntryToRead.NameIdx), sizeof(EntryToRead.NameIdx));
        stream.read(reinterpret_cast<char*>(&EntryToRead.ArchetypeRef), sizeof(EntryToRead.ArchetypeRef));
        stream.read(reinterpret_cast<char*>(&EntryToRead.ObjectFlagsH), sizeof(EntryToRead.ObjectFlagsH));
        stream.read(reinterpret_cast<char*>(&EntryToRead.ObjectFlagsL), sizeof(EntryToRead.ObjectFlagsL));
        stream.read(reinterpret_cast<char*>(&EntryToRead.SerialSize), sizeof(EntryToRead.SerialSize));
        stream.read(reinterpret_cast<char*>(&EntryToRead.SerialOffset), sizeof(EntryToRead.SerialOffset));
        stream.read(reinterpret_cast<char*>(&EntryToRead.ExportFlags), sizeof(EntryToRead.ExportFlags));
        stream.read(reinterpret_cast<char*>(&EntryToRead.NetObjectCount), sizeof(EntryToRead.NetObjectCount));
        stream.read(reinterpret_cast<char*>(&EntryToRead.GUID), sizeof(EntryToRead.GUID));
        stream.read(reinterpret_cast<char*>(&EntryToRead.Unknown1), sizeof(EntryToRead.Unknown1));
        EntryToRead.NetObjects.resize(EntryToRead.NetObjectCount);
        if (EntryToRead.NetObjectCount > 0)
        {
            stream.read(reinterpret_cast<char*>(EntryToRead.NetObjects.data()), EntryToRead.NetObjects.size()*4);
        }
        EntryToRead.EntrySize = (unsigned)stream.tellg() - EntryToRead.EntryOffset;
        ExportTable.push_back(EntryToRead);
    }
    DependsBuf.clear();
    DependsBuf.resize(Summary.SerialOffset - Summary.DependsOffset);
    if (DependsBuf.size() > 0)
    {
        stream.read(DependsBuf.data(), DependsBuf.size());
    }
    if (!stream.good())
    {
        ReadError = UPKReadErrors::FileError;
        return false;
    }
    /// resolve names
    for (unsigned i = 1; i < ImportTable.size(); ++i)
    {
        ImportTable[i].Name = IndexToName(ImportTable[i].NameIdx);
        ImportTable[i].FullName = ResolveFullName(-i);
        ImportTable[i].Type = IndexToName(ImportTable[i].TypeIdx);
        if (ImportTable[i].Type == "")
        {
            ImportTable[i].Type = "Class";
        }
    }
    for (unsigned i = 1; i < ExportTable.size(); ++i)
    {
        ExportTable[i].Name = IndexToName(ExportTable[i].NameIdx);
        ExportTable[i].FullName = ResolveFullName(i);
        ExportTable[i].Type = ObjRefToName(ExportTable[i].TypeRef);
        if (ExportTable[i].Type == "")
        {
            ExportTable[i].Type = "Class";
        }
    }
    return true;
}

std::string UPKInfo::IndexToName(UNameIndex idx)
{
    std::ostringstream ss;
    ss << GetNameEntry(idx.NameTableIdx).Name;
    if (idx.Numeric > 0 && ss.str() != "None")
        ss << "_" << int(idx.Numeric - 1);
    return ss.str();
}

std::string UPKInfo::ObjRefToName(UObjectReference ObjRef)
{
    if (ObjRef == 0 || -ObjRef >= (int)ImportTable.size() || ObjRef >= (int)ExportTable.size())
    {
        return "";
    }
    else if (ObjRef > 0)
    {
        return IndexToName(GetExportEntry(ObjRef).NameIdx);
    }
    else if (ObjRef < 0)
    {
        return IndexToName(GetImportEntry(-ObjRef).NameIdx);
    }
    return "";
}

std::string UPKInfo::ResolveFullName(UObjectReference ObjRef)
{
    std::string name;
    name = ObjRefToName(ObjRef);
    UObjectReference next = GetOwnerRef(ObjRef);
    while (next != 0)
    {
        name = ObjRefToName(next) + "." + name;
        next = GetOwnerRef(next);
    }
    return name;
}

UObjectReference UPKInfo::GetOwnerRef(UObjectReference ObjRef)
{
    if (ObjRef == 0)
    {
        return 0;
    }
    else if (ObjRef > 0)
    {
        return GetExportEntry(ObjRef).OwnerRef;
    }
    else if (ObjRef < 0)
    {
        return GetImportEntry(-ObjRef).OwnerRef;
    }
    return 0;
}

int UPKInfo::FindName(std::string name)
{
    for (unsigned i = 0; i < NameTable.size(); ++i)
    {
        if (NameTable[i].Name == name)
            return i;
    }
    return -1;
}

UObjectReference UPKInfo::FindObject(std::string FullName, bool isExport)
{
    /// Import object
    if (isExport == false)
    {
        for (unsigned i = 1; i < ImportTable.size(); ++i)
        {
            if (ImportTable[i].FullName == FullName)
                return -i;
        }
    }
    /// Export object
    for (unsigned i = 1; i < ExportTable.size(); ++i)
    {
        if (ExportTable[i].FullName == FullName)
            return i;
    }
    /// Object not found
    return 0;
}

UObjectReference UPKInfo::FindObjectByName(std::string Name, bool isExport)
{
    /// Import object
    if (isExport == false)
    {
        for (unsigned i = 1; i < ImportTable.size(); ++i)
        {
            if (ImportTable[i].Name == Name)
                return -i;
        }
    }
    /// Export object
    for (unsigned i = 1; i < ExportTable.size(); ++i)
    {
        if (ExportTable[i].Name == Name)
            return i;
    }
    /// Object not found
    return 0;
}

UObjectReference UPKInfo::FindObjectByOffset(size_t offset)
{
    for (unsigned i = 1; i < ExportTable.size(); ++i)
    {
        if (offset >= ExportTable[i].SerialOffset && offset < ExportTable[i].SerialOffset + ExportTable[i].SerialSize)
            return i;
    }
    return 0;
}

const FObjectExport& UPKInfo::GetExportEntry(uint32_t idx)
{
    if (idx < ExportTable.size())
        return ExportTable[idx];
    else
        return ExportTable[0];
}

const FObjectImport& UPKInfo::GetImportEntry(uint32_t idx)
{
    if (idx < ImportTable.size())
        return ImportTable[idx];
    else
        return ImportTable[0];
}

const FNameEntry& UPKInfo::GetNameEntry(uint32_t idx)
{
    if (idx < NameTable.size())
        return NameTable[idx];
    else
        return NameTable[NoneIdx];
}

std::string UPKInfo::FormatCompressedHeader()
{
    std::ostringstream ss;
    ss << "Signature: " << FormatHEX(CompressedHeader.Signature) << std::endl
       << "BlockSize: " << CompressedHeader.BlockSize << std::endl
       << "CompressedSize: " << CompressedHeader.CompressedSize << std::endl
       << "UncompressedSize: " << CompressedHeader.UncompressedSize << std::endl
       << "NumBlocks: " << CompressedHeader.NumBlocks << std::endl;
    for (unsigned i = 0; i < CompressedHeader.Blocks.size(); ++i)
    {
        ss << "Blocks[" << i << "]:" << std::endl
           << "\tCompressedSize: " << CompressedHeader.Blocks[i].CompressedSize << std::endl
           << "\tUncompressedSize: " << CompressedHeader.Blocks[i].UncompressedSize << std::endl;
    }
    return ss.str();
}

std::string UPKInfo::FormatSummary()
{
    if (CompressedChunk == true)
        return FormatCompressedHeader();
    std::ostringstream ss;
    ss << "Signature: " << FormatHEX(Summary.Signature) << std::endl
       << "Version: " << Summary.Version << std::endl
       << "LicenseeVersion: " << Summary.LicenseeVersion << std::endl
       << "HeaderSize: " << Summary.HeaderSize << " (" << FormatHEX(Summary.HeaderSize) << ")" << std::endl
       << "Folder: " << Summary.FolderName << std::endl
       << "PackageFlags: " << FormatHEX(Summary.PackageFlags) << std::endl
       << FormatPackageFlags(Summary.PackageFlags)
       << "NameCount: " << Summary.NameCount << std::endl
       << "NameOffset: " << FormatHEX(Summary.NameOffset) << std::endl
       << "ExportCount: " << Summary.ExportCount << std::endl
       << "ExportOffset: " << FormatHEX(Summary.ExportOffset) << std::endl
       << "ImportCount: " << Summary.ImportCount << std::endl
       << "ImportOffset: " << FormatHEX(Summary.ImportOffset) << std::endl
       << "DependsOffset: " << FormatHEX(Summary.DependsOffset) << std::endl
       << "SerialOffset: " << FormatHEX(Summary.SerialOffset) << std::endl
       << "Unknown2: " << FormatHEX(Summary.Unknown2) << std::endl
       << "Unknown3: " << FormatHEX(Summary.Unknown3) << std::endl
       << "Unknown4: " << FormatHEX(Summary.Unknown4) << std::endl
       << "GUID: " << FormatHEX(Summary.GUID) << std::endl
       << "GenerationsCount: " << Summary.GenerationsCount << std::endl;
    for (unsigned i = 0; i < Summary.Generations.size(); ++i)
    {
        ss << "Generations[" << i << "]:" << std::endl
           << "\tExportCount: " << Summary.Generations[i].ExportCount << std::endl
           << "\tNameCount: " << Summary.Generations[i].NameCount << std::endl
           << "\tNetObjectCount: " << Summary.Generations[i].NetObjectCount << std::endl;
    }
    ss << "EngineVersion: " << Summary.EngineVersion << std::endl
       << "CookerVersion: " << Summary.CookerVersion << std::endl
       << "CompressionFlags: " << FormatHEX(Summary.CompressionFlags) << std::endl
       << FormatCompressionFlags(Summary.CompressionFlags)
       << "NumCompressedChunks: " << Summary.NumCompressedChunks << std::endl;
    for (unsigned i = 0; i < Summary.CompressedChunks.size(); ++i)
    {
        ss << "CompressedChunks[" << i << "]:" << std::endl
           << "\tUncompressedOffset: " << FormatHEX(Summary.CompressedChunks[i].UncompressedOffset) << " (" << Summary.CompressedChunks[i].UncompressedOffset << ")" << std::endl
           << "\tUncompressedSize: " << Summary.CompressedChunks[i].UncompressedSize << std::endl
           << "\tCompressedOffset: " << FormatHEX(Summary.CompressedChunks[i].CompressedOffset) << "(" << Summary.CompressedChunks[i].CompressedOffset << ")" << std::endl
           << "\tCompressedSize: " << Summary.CompressedChunks[i].CompressedSize << std::endl;
    }
    if (Summary.UnknownDataChunk.size() > 0)
    {
        ss << "Unknown data size: " << Summary.UnknownDataChunk.size() << std::endl;
        ss << "Unknown data: " << FormatHEX(Summary.UnknownDataChunk) << std::endl;
    }
    return ss.str();
}

std::string UPKInfo::FormatNames(bool verbose)
{
    std::ostringstream ss;
    ss << "NameTable:" << std::endl;
    for (unsigned i = 0; i < NameTable.size(); ++i)
    {
        ss << FormatName(i, verbose);
    }
    return ss.str();
}

std::string UPKInfo::FormatImports(bool verbose)
{
    std::ostringstream ss;
    ss << "ImportTable:" << std::endl;
    for (unsigned i = 1; i < ImportTable.size(); ++i)
    {
        ss << FormatImport(i, verbose);
    }
    return ss.str();
}

std::string UPKInfo::FormatExports(bool verbose)
{
    std::ostringstream ss;
    ss << "ExportTable:" << std::endl;
    for (unsigned i = 1; i < ExportTable.size(); ++i)
    {
        ss << FormatExport(i, verbose);
    }
    return ss.str();
}

std::string UPKInfo::FormatName(uint32_t idx, bool verbose)
{
    std::ostringstream ss;
    FNameEntry Entry = GetNameEntry(idx);
    ss << FormatHEX((uint32_t)idx) << " (" << idx << ") ( "
       << FormatHEX((char*)&idx, sizeof(idx)) << "): "
       << Entry.Name << std::endl;
    if (verbose == true)
    {
        ss << "\tNameFlagsL: " << FormatHEX(Entry.NameFlagsL) << std::endl
           << "\tNameFlagsH: " << FormatHEX(Entry.NameFlagsH) << std::endl;
    }
    return ss.str();
}

std::string UPKInfo::FormatImport(uint32_t idx, bool verbose)
{
    std::ostringstream ss;
    int32_t invIdx = -idx;
    FObjectImport Entry = GetImportEntry(idx);
    ss << FormatHEX((uint32_t)(-idx)) << " (" << (-(int)idx) << ") ( "
       << FormatHEX((char*)&invIdx, sizeof(invIdx)) << "): "
       << Entry.Type << "\'"
       << Entry.FullName << "\'" << std::endl;
    if (verbose == true)
    {
        ss << "\tPackageIdx: " << FormatHEX(Entry.PackageIdx) << " -> " << IndexToName(Entry.PackageIdx) << std::endl
           << "\tTypeIdx: " << FormatHEX(Entry.TypeIdx) << " -> " << IndexToName(Entry.TypeIdx) << std::endl
           << "\tOwnerRef: " << FormatHEX((uint32_t)Entry.OwnerRef) << " -> " << ObjRefToName(Entry.OwnerRef) << std::endl
           << "\tNameIdx: " << FormatHEX(Entry.NameIdx) << " -> " << IndexToName(Entry.NameIdx) << std::endl;
    }
    return ss.str();
}

std::string UPKInfo::FormatExport(uint32_t idx, bool verbose)
{
    std::ostringstream ss;
    FObjectExport Entry = GetExportEntry(idx);
    ss << FormatHEX((uint32_t)idx) << " (" << idx << ") ( "
       << FormatHEX((char*)&idx, sizeof(idx)) << "): "
       << Entry.Type << "\'"
       << Entry.FullName << "\'" << std::endl;
    if (verbose == true)
    {
        ss << "\tTypeRef: " << FormatHEX((uint32_t)Entry.TypeRef) << " -> " << ObjRefToName(Entry.TypeRef) << std::endl
           << "\tParentClassRef: " << FormatHEX((uint32_t)Entry.ParentClassRef) << " -> " << ObjRefToName(Entry.ParentClassRef) << std::endl
           << "\tOwnerRef: " << FormatHEX((uint32_t)Entry.OwnerRef) << " -> " << ObjRefToName(Entry.OwnerRef) << std::endl
           << "\tNameIdx: " << FormatHEX(Entry.NameIdx) << " -> " << IndexToName(Entry.NameIdx) << std::endl
           << "\tArchetypeRef: " << FormatHEX((uint32_t)Entry.ArchetypeRef) << " -> " << ObjRefToName(Entry.ArchetypeRef) << std::endl
           << "\tObjectFlagsH: " << FormatHEX(Entry.ObjectFlagsH) << std::endl
           << FormatObjectFlagsH(Entry.ObjectFlagsH)
           << "\tObjectFlagsL: " << FormatHEX(Entry.ObjectFlagsL) << std::endl
           << FormatObjectFlagsL(Entry.ObjectFlagsL)
           << "\tSerialSize: " << FormatHEX(Entry.SerialSize) << " (" << Entry.SerialSize << ")" << std::endl
           << "\tSerialOffset: " << FormatHEX(Entry.SerialOffset) << std::endl
           << "\tExportFlags: " << FormatHEX(Entry.ExportFlags) << std::endl
           << FormatExportFlags(Entry.ExportFlags)
           << "\tNetObjectCount: " << Entry.NetObjectCount << std::endl
           << "\tGUID: " << FormatHEX(Entry.GUID) << std::endl
           << "\tUnknown1: " << FormatHEX(Entry.Unknown1) << std::endl;
        for (unsigned i = 0; i < Entry.NetObjects.size(); ++i)
        {
            ss << "\tNetObjects[" << i << "]: " << FormatHEX(Entry.NetObjects[i]) << std::endl;
        }
    }
    return ss.str();
}

/// helper functions

std::string FormatHEX(uint32_t val)
{
    char ch[255];
    sprintf(ch, "0x%08X", val);
    return std::string(ch);
}

std::string FormatHEX(uint16_t val)
{
    char ch[255];
    sprintf(ch, "0x%04X", val);
    return std::string(ch);
}

std::string FormatHEX(uint8_t val)
{
    char ch[255];
    sprintf(ch, "0x%02X", val);
    return std::string(ch);
}

std::string FormatHEX(float val)
{
    std::string ret = "0x";
    uint8_t *p = reinterpret_cast<uint8_t*>(&val);
    for (unsigned i = 0; i < 4; ++i)
    {
        char ch[255];
        sprintf(ch, "%02X", p[3 - i]);
        ret += ch;
    }
    return ret;
}

std::string FormatHEX(FGuid GUID)
{
    char ch[255];
    sprintf(ch, "%08X%08X%08X%08X", GUID.GUID_A, GUID.GUID_B, GUID.GUID_C, GUID.GUID_D);
    return std::string(ch);
}

std::string FormatHEX(UNameIndex NameIndex)
{
    char ch[255];
    sprintf(ch, "0x%08X (Index) 0x%08X (Numeric)", NameIndex.NameTableIdx, NameIndex.Numeric);
    return std::string(ch);
}

std::string FormatHEX(uint32_t L, uint32_t H)
{
    char ch[255];
    sprintf(ch, "0x%08X%08X", H, L);
    return std::string(ch);
}

std::string FormatHEX(std::vector<char> DataChunk)
{
    std::string ret;
    for (unsigned i = 0; i < DataChunk.size(); ++i)
    {
        char ch[255];
        sprintf(ch, "%02X", (uint8_t)DataChunk[i]);
        ret += std::string(ch) + " ";
    }
    return ret;
}

std::string FormatHEX(char* DataChunk, size_t size)
{
    std::string ret;
    for (unsigned i = 0; i < size; ++i)
    {
        char ch[255];
        sprintf(ch, "%02X", (uint8_t)DataChunk[i]);
        ret += std::string(ch) + " ";
    }
    return ret;
}

std::string FormatHEX(std::string DataString)
{
    std::string ret;
    for (unsigned i = 0; i < DataString.size(); ++i)
    {
        char ch[255];
        sprintf(ch, "%02X", (uint8_t)DataString[i]);
        ret += std::string(ch) + " ";
    }
    return ret;
}

/// format flags

std::string FormatPackageFlags(uint32_t flags)
{
    std::ostringstream ss;
    if (flags & (uint32_t)UPackageFlags::AllowDownload)
    {
        ss << "\t" << FormatHEX((uint32_t)UPackageFlags::AllowDownload) << ": AllowDownload" << std::endl;
    }
    if (flags & (uint32_t)UPackageFlags::ClientOptional)
    {
        ss << "\t" << FormatHEX((uint32_t)UPackageFlags::ClientOptional) << ": ClientOptional" << std::endl;
    }
    if (flags & (uint32_t)UPackageFlags::ServerSideOnly)
    {
        ss << "\t" << FormatHEX((uint32_t)UPackageFlags::ServerSideOnly) << ": ServerSideOnly" << std::endl;
    }
    if (flags & (uint32_t)UPackageFlags::BrokenLinks)
    {
        ss << "\t" << FormatHEX((uint32_t)UPackageFlags::BrokenLinks) << ": BrokenLinks" << std::endl;
    }
    if (flags & (uint32_t)UPackageFlags::Cooked)
    {
        ss << "\t" << FormatHEX((uint32_t)UPackageFlags::Cooked) << ": Cooked" << std::endl;
    }
    if (flags & (uint32_t)UPackageFlags::Unsecure)
    {
        ss << "\t" << FormatHEX((uint32_t)UPackageFlags::Unsecure) << ": Unsecure" << std::endl;
    }
    if (flags & (uint32_t)UPackageFlags::Encrypted)
    {
        ss << "\t" << FormatHEX((uint32_t)UPackageFlags::Encrypted) << ": Encrypted" << std::endl;
    }
    if (flags & (uint32_t)UPackageFlags::Need)
    {
        ss << "\t" << FormatHEX((uint32_t)UPackageFlags::Need) << ": Need" << std::endl;
    }
    if (flags & (uint32_t)UPackageFlags::Map)
    {
        ss << "\t" << FormatHEX((uint32_t)UPackageFlags::Map) << ": Map" << std::endl;
    }
    if (flags & (uint32_t)UPackageFlags::Script)
    {
        ss << "\t" << FormatHEX((uint32_t)UPackageFlags::Script) << ": Script" << std::endl;
    }
    if (flags & (uint32_t)UPackageFlags::Debug)
    {
        ss << "\t" << FormatHEX((uint32_t)UPackageFlags::Debug) << ": Debug" << std::endl;
    }
    if (flags & (uint32_t)UPackageFlags::Imports)
    {
        ss << "\t" << FormatHEX((uint32_t)UPackageFlags::Imports) << ": Imports" << std::endl;
    }
    if (flags & (uint32_t)UPackageFlags::Compressed)
    {
        ss << "\t" << FormatHEX((uint32_t)UPackageFlags::Compressed) << ": Compressed" << std::endl;
    }
    if (flags & (uint32_t)UPackageFlags::FullyCompressed)
    {
        ss << "\t" << FormatHEX((uint32_t)UPackageFlags::FullyCompressed) << ": FullyCompressed" << std::endl;
    }
    if (flags & (uint32_t)UPackageFlags::NoExportsData)
    {
        ss << "\t" << FormatHEX((uint32_t)UPackageFlags::NoExportsData) << ": NoExportsData" << std::endl;
    }
    if (flags & (uint32_t)UPackageFlags::Stripped)
    {
        ss << "\t" << FormatHEX((uint32_t)UPackageFlags::Stripped) << ": Stripped" << std::endl;
    }
    if (flags & (uint32_t)UPackageFlags::Protected)
    {
        ss << "\t" << FormatHEX((uint32_t)UPackageFlags::Protected) << ": Protected" << std::endl;
    }
    return ss.str();
}

std::string FormatCompressionFlags(uint32_t flags)
{
    std::ostringstream ss;
    if (flags & (uint32_t)UCompressionFlags::ZLIB)
    {
        ss << "\t" << FormatHEX((uint32_t)UCompressionFlags::ZLIB) << ": ZLIB" << std::endl;
    }
    if (flags & (uint32_t)UCompressionFlags::LZO)
    {
        ss << "\t" << FormatHEX((uint32_t)UCompressionFlags::LZO) << ": LZO" << std::endl;
    }
    if (flags & (uint32_t)UCompressionFlags::LZX)
    {
        ss << "\t" << FormatHEX((uint32_t)UCompressionFlags::LZX) << ": LZX" << std::endl;
    }
    return ss.str();
}

std::string FormatObjectFlagsL(uint32_t flags)
{
    std::ostringstream ss;
    if (flags & (uint32_t)UObjectFlagsL::Transactional)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsL::Transactional) << ": Transactional" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsL::Unreachable)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsL::Unreachable) << ": Unreachable" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsL::Public)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsL::Public) << ": Public" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsL::Private)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsL::Private) << ": Private" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsL::Automated)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsL::Automated) << ": Automated" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsL::Transient)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsL::Transient) << ": Transient" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsL::Preloading)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsL::Preloading) << ": Preloading" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsL::LoadForClient)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsL::LoadForClient) << ": LoadForClient" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsL::LoadForServer)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsL::LoadForServer) << ": LoadForServer" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsL::LoadForEdit)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsL::LoadForEdit) << ": LoadForEdit" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsL::Standalone)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsL::Standalone) << ": Standalone" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsL::NotForClient)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsL::NotForClient) << ": NotForClient" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsL::NotForServer)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsL::NotForServer) << ": NotForServer" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsL::NotForEdit)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsL::NotForEdit) << ": NotForEdit" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsL::NeedPostLoad)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsL::NeedPostLoad) << ": NeedPostLoad" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsL::HasStack)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsL::HasStack) << ": HasStack" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsL::Native)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsL::Native) << ": Native" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsL::Marked)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsL::Marked) << ": Marked" << std::endl;
    }
    return ss.str();
}

std::string FormatObjectFlagsH(uint32_t flags)
{
    std::ostringstream ss;
    if (flags & (uint32_t)UObjectFlagsH::Obsolete)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsH::Obsolete) << ": Obsolete" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsH::Final)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsH::Final) << ": Final" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsH::PerObjectLocalized)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsH::PerObjectLocalized) << ": PerObjectLocalized" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsH::PropertiesObject)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsH::PropertiesObject) << ": PropertiesObject" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsH::ArchetypeObject)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsH::ArchetypeObject) << ": ArchetypeObject" << std::endl;
    }
    if (flags & (uint32_t)UObjectFlagsH::RemappedName)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UObjectFlagsH::RemappedName) << ": RemappedName" << std::endl;
    }
    return ss.str();
}

std::string FormatExportFlags(uint32_t flags)
{
    std::ostringstream ss;
    if (flags & (uint32_t)UExportFlags::ForcedExport)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UExportFlags::ForcedExport) << ": ForcedExport" << std::endl;
    }
    return ss.str();
}

std::string FormatFunctionFlags(uint32_t flags)
{
    std::ostringstream ss;
    if (flags & (uint32_t)UFunctionFlags::Final)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::Final) << ": Final" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::Defined)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::Defined) << ": Defined" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::Iterator)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::Iterator) << ": Iterator" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::Latent)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::Latent) << ": Latent" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::PreOperator)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::PreOperator) << ": PreOperator" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::Singular)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::Singular) << ": Singular" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::Net)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::Net) << ": Net" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::NetReliable)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::NetReliable) << ": NetReliable" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::Simulated)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::Simulated) << ": Simulated" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::Exec)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::Exec) << ": Exec" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::Native)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::Native) << ": Native" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::Event)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::Event) << ": Event" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::Operator)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::Operator) << ": Operator" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::Static)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::Static) << ": Static" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::NoExport)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::NoExport) << ": NoExport" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::OptionalParameters)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::OptionalParameters) << ": OptionalParameters" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::Const)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::Const) << ": Const" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::Invariant)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::Invariant) << ": Invariant" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::Public)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::Public) << ": Public" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::Private)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::Private) << ": Private" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::Protected)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::Protected) << ": Protected" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::Delegate)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::Delegate) << ": Delegate" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::NetServer)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::NetServer) << ": NetServer" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::NetClient)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::NetClient) << ": NetClient" << std::endl;
    }
    if (flags & (uint32_t)UFunctionFlags::DLLImport)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UFunctionFlags::DLLImport) << ": DLLImport" << std::endl;
    }
    return ss.str();
}

std::string FormatStructFlags(uint32_t flags)
{
    std::ostringstream ss;
    if (flags & (uint32_t)UStructFlags::Native)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UStructFlags::Native) << ": Native" << std::endl;
    }
    if (flags & (uint32_t)UStructFlags::Export)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UStructFlags::Export) << ": Export" << std::endl;
    }
    if (flags & (uint32_t)UStructFlags::Long)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UStructFlags::Long) << ": Long" << std::endl;
    }
    if (flags & (uint32_t)UStructFlags::HasComponents)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UStructFlags::HasComponents) << ": HasComponents" << std::endl;
    }
    if (flags & (uint32_t)UStructFlags::Init)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UStructFlags::Init) << ": Init" << std::endl;
    }
    if (flags & (uint32_t)UStructFlags::Transient)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UStructFlags::Transient) << ": Transient" << std::endl;
    }
    if (flags & (uint32_t)UStructFlags::Atomic)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UStructFlags::Atomic) << ": Atomic" << std::endl;
    }
    if (flags & (uint32_t)UStructFlags::Immutable)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UStructFlags::Immutable) << ": Immutable" << std::endl;
    }
    if (flags & (uint32_t)UStructFlags::StrictConfig)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UStructFlags::StrictConfig) << ": StrictConfig" << std::endl;
    }
    if (flags & (uint32_t)UStructFlags::ImmutableWhenCooked)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UStructFlags::ImmutableWhenCooked) << ": ImmutableWhenCooked" << std::endl;
    }
    if (flags & (uint32_t)UStructFlags::AtomicWhenCooked)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UStructFlags::AtomicWhenCooked) << ": AtomicWhenCooked" << std::endl;
    }
    return ss.str();
}

std::string FormatClassFlags(uint32_t flags)
{
    std::ostringstream ss;
    if (flags & (uint32_t)UClassFlags::Abstract)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::Abstract) << ": Abstract" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::Compiled)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::Compiled) << ": Compiled" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::Config)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::Config) << ": Config" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::Transient)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::Transient) << ": Transient" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::Parsed)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::Parsed) << ": Parsed" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::Localized)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::Localized) << ": Localized" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::SafeReplace)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::SafeReplace) << ": SafeReplace" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::RuntimeStatic)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::RuntimeStatic) << ": RuntimeStatic" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::NoExport)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::NoExport) << ": NoExport" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::Placeable)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::Placeable) << ": Placeable" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::PerObjectConfig)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::PerObjectConfig) << ": PerObjectConfig" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::NativeReplication)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::NativeReplication) << ": NativeReplication" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::EditInlineNew)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::EditInlineNew) << ": EditInlineNew" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::CollapseCategories)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::CollapseCategories) << ": CollapseCategories" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::ExportStructs)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::ExportStructs) << ": ExportStructs" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::Instanced)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::Instanced) << ": Instanced" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::HideDropDown)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::HideDropDown) << ": HideDropDown" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::HasComponents)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::HasComponents) << ": HasComponents" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::CacheExempt)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::CacheExempt) << ": CacheExempt" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::Hidden)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::Hidden) << ": Hidden" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::ParseConfig)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::ParseConfig) << ": ParseConfig" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::Deprecated)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::Deprecated) << ": Deprecated" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::HideDropDown2)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::HideDropDown2) << ": HideDropDown2" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::Exported)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::Exported) << ": Exported" << std::endl;
    }
    if (flags & (uint32_t)UClassFlags::NativeOnly)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UClassFlags::NativeOnly) << ": NativeOnly" << std::endl;
    }
    return ss.str();
}

std::string FormatStateFlags(uint32_t flags)
{
    std::ostringstream ss;
    if (flags & (uint32_t)UStateFlags::Editable)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UStateFlags::Editable) << ": Editable" << std::endl;
    }
    if (flags & (uint32_t)UStateFlags::Auto)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UStateFlags::Auto) << ": Auto" << std::endl;
    }
    if (flags & (uint32_t)UStateFlags::Simulated)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UStateFlags::Simulated) << ": Simulated" << std::endl;
    }
    return ss.str();
}

std::string FormatPropertyFlagsL(uint32_t flags)
{
    std::ostringstream ss;
    if (flags & (uint32_t)UPropertyFlagsL::Editable)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::Editable) << ": Editable" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::Const)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::Const) << ": Const" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::Input)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::Input) << ": Input" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::ExportObject)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::ExportObject) << ": ExportObject" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::OptionalParm)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::OptionalParm) << ": OptionalParm" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::Net)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::Net) << ": Net" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::EditConstArray)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::EditConstArray) << ": EditConstArray" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::EditFixedSize)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::EditFixedSize) << ": EditFixedSize" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::Parm)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::Parm) << ": Parm" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::OutParm)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::OutParm) << ": OutParm" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::SkipParm)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::SkipParm) << ": SkipParm" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::ReturnParm)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::ReturnParm) << ": ReturnParm" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::CoerceParm)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::CoerceParm) << ": CoerceParm" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::Native)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::Native) << ": Native" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::Transient)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::Transient) << ": Transient" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::Config)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::Config) << ": Config" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::Localized)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::Localized) << ": Localized" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::Travel)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::Travel) << ": Travel" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::EditConst)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::EditConst) << ": EditConst" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::GlobalConfig)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::GlobalConfig) << ": GlobalConfig" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::Component)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::Component) << ": Component" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::OnDemand)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::OnDemand) << ": OnDemand" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::Init)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::Init) << ": Init" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::New)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::New) << ": New" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::DuplicateTransient)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::DuplicateTransient) << ": DuplicateTransient" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::NeedCtorLink)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::NeedCtorLink) << ": NeedCtorLink" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::NoExport)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::NoExport) << ": NoExport" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::Cache)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::Cache) << ": Cache" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::NoImport)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::NoImport) << ": NoImport" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::EditorData)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::EditorData) << ": EditorData" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::NoClear)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::NoClear) << ": NoClear" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::EditInline)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::EditInline) << ": EditInline" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::EdFindable)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::EdFindable) << ": EdFindable" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::EditInlineUse)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::EditInlineUse) << ": EditInlineUse" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::Deprecated)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::Deprecated) << ": Deprecated" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::EditInlineNotify)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::EditInlineNotify) << ": EditInlineNotify" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::DataBinding)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::DataBinding) << ": DataBinding" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::SerializeText)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::SerializeText) << ": SerializeText" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsL::Automated)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsL::Automated) << ": Automated" << std::endl;
    }
    return ss.str();
}

std::string FormatPropertyFlagsH(uint32_t flags)
{
    std::ostringstream ss;
    if (flags & (uint32_t)UPropertyFlagsH::RepNotify)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsH::RepNotify) << ": RepNotify" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsH::Interp)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsH::Interp) << ": Interp" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsH::NonTransactional)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsH::NonTransactional) << ": NonTransactional" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsH::EditorOnly)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsH::EditorOnly) << ": EditorOnly" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsH::NotForConsole)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsH::NotForConsole) << ": NotForConsole" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsH::RepRetry)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsH::RepRetry) << ": RepRetry" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsH::PrivateWrite)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsH::PrivateWrite) << ": PrivateWrite" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsH::ProtectedWrite)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsH::ProtectedWrite) << ": ProtectedWrite" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsH::Archetype)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsH::Archetype) << ": Archetype" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsH::EditHide)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsH::EditHide) << ": EditHide" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsH::EditTextBox)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsH::EditTextBox) << ": EditTextBox" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsH::CrossLevelPassive)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsH::CrossLevelPassive) << ": CrossLevelPassive" << std::endl;
    }
    if (flags & (uint32_t)UPropertyFlagsH::CrossLevelActive)
    {
        ss << "\t\t" << FormatHEX((uint32_t)UPropertyFlagsH::CrossLevelActive) << ": CrossLevelActive" << std::endl;
    }

    return ss.str();
}

std::string FormatReadErrors(UPKReadErrors ReadError)
{
    std::ostringstream ss;
    switch(ReadError)
    {
    case UPKReadErrors::FileError:
        ss << "Bad package file!\n";
        break;
    case UPKReadErrors::BadSignature:
        ss << "Bad package signature!\n";
        break;
    case UPKReadErrors::BadVersion:
        ss << "Bad package version!\n";
        break;
    case UPKReadErrors::IsCompressed:
        ss << "Package is compressed, must decompress first!\n";
        break;
    default:
        break;
    }
    return ss.str();
}
