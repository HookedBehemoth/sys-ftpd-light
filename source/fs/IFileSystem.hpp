#pragma once

#include "IDirectory.hpp"
#include "IFile.hpp"

#include <memory>
#include <switch.h>

class IFileSystem {
  private:
    FsFileSystem m_fs;

  public:
    IFileSystem(FsFileSystem &&fs);
    ~IFileSystem();

    Result CreateFile(const char *path, s64 size, u32 option);
    Result DeleteFile(const char *path);
    Result CreateDirectory(const char *path);
    Result DeleteDirectory(const char *path);
    Result DeleteDirectoryRecursively(const char *path);
    Result RenameFile(const char *cur_path, const char *new_path);
    Result RenameDirectory(const char *cur_path, const char *new_path);
    Result GetEntryType(const char *path, FsDirEntryType *out);
    Result OpenFile(const char *path, u32 mode, std::unique_ptr<IFile> *out);
    Result OpenDirectory(const char *path, u32 mode, std::unique_ptr<IDirectory> *out);
    Result Commit();
    Result GetFreeSpace(const char *path, s64 *out);
    Result GetTotalSpace(const char *path, s64 *out);
    Result GetFileTimeStampRaw(const char *path, FsTimeStampRaw *out); ///< [3.0.0+]
};
