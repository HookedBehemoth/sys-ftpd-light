#pragma once

#include <switch.h>

class IFile {
  private:
    FsFile m_file;
    bool open;

  public:
    IFile(FsFile &&file);
    ~IFile();

    /* Stock */
    Result Read(s64 off, void *buf, u64 read_size, u32 option, u64 *bytes_read);
    Result Write(s64 off, const void *buf, u64 write_size, u32 option);
    Result Flush();
    Result SetSize(s64 sz);
    Result GetSize(s64 *out);

    void Close();
};
