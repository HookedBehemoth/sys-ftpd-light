#include "IDirectory.hpp"

#include <memory>

IDirectory::IDirectory(FsDir &&dir)
    : m_dir(std::move(dir)), open(true) {}

IDirectory::~IDirectory() {
    this->Close();
}

Result IDirectory::Read(s64 *total_entries, size_t max_entries, FsDirectoryEntry *buf) {
    return fsDirRead(&m_dir, total_entries, max_entries, buf);
}
Result IDirectory::GetEntryCount(s64 *count) {
    return fsDirGetEntryCount(&m_dir, count);
}

void IDirectory::Close() {
    if (this->open) {
        fsDirClose(&m_dir);
        this->open = false;
    }
}
