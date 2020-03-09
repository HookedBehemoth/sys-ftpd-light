#include "IFile.hpp"

#include <memory>

IFile::IFile(FsFile &&file)
    : m_file(std::move(file)), open(true) {}

IFile::~IFile() {
    this->Close();
}

Result IFile::Read(s64 off, void *buf, u64 read_size, u32 option, u64 *bytes_read) {
    return fsFileRead(&m_file, off, buf, read_size, option, bytes_read);
}
Result IFile::Write(s64 off, const void *buf, u64 write_size, u32 option) {
    return fsFileWrite(&m_file, off, buf, write_size, option);
}
Result IFile::Flush() {
    return fsFileFlush(&m_file);
}
Result IFile::SetSize(s64 sz) {
    return fsFileSetSize(&m_file, sz);
}
Result IFile::GetSize(s64 *out) {
    return fsFileGetSize(&m_file, out);
}

void IFile::Close() {
    if (this->open) {
        fsFileClose(&m_file);
        this->open = false;
    }
}
