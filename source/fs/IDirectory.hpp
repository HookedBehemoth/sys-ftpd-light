#pragma once

#include <switch.h>

class IDirectory {
  private:
    FsDir m_dir;
    bool open;

  public:
    IDirectory(FsDir &&dir);
    ~IDirectory();

    Result Read(s64 *total_entries, size_t max_entries, FsDirectoryEntry *buf);
    Result GetEntryCount(s64 *count);

    inline bool IsOpen() { return open; };
    void Close();
};
