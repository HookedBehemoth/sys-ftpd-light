#include "IFileSystem.hpp"

#include <memory>

IFileSystem::IFileSystem(FsFileSystem &&fs)
    : m_fs(std::move(fs)) {}

IFileSystem::~IFileSystem() {
    fsFsClose(&m_fs);
}

Result IFileSystem::CreateFile(const char *path, s64 size, u32 option) {
    return fsFsCreateFile(&m_fs, path, size, option);
}
Result IFileSystem::DeleteFile(const char *path) {
    return fsFsDeleteFile(&m_fs, path);
}
Result IFileSystem::CreateDirectory(const char *path) {
    return fsFsCreateDirectory(&m_fs, path);
}
Result IFileSystem::DeleteDirectory(const char *path) {
    return fsFsDeleteFile(&m_fs, path);
}
Result IFileSystem::DeleteDirectoryRecursively(const char *path) {
    return fsFsDeleteDirectoryRecursively(&m_fs, path);
}
Result IFileSystem::RenameFile(const char *cur_path, const char *new_path) {
    return fsFsRenameFile(&m_fs, cur_path, new_path);
}
Result IFileSystem::RenameDirectory(const char *cur_path, const char *new_path) {
    return fsFsRenameDirectory(&m_fs, cur_path, new_path);
}
Result IFileSystem::GetEntryType(const char *path, FsDirEntryType *out) {
    return fsFsGetEntryType(&m_fs, path, out);
}
Result IFileSystem::OpenFile(const char *path, u32 mode, std::unique_ptr<IFile> *out) {
    FsFile fsFile;
    Result rc = fsFsOpenFile(&m_fs, path, mode, &fsFile);
    if (R_SUCCEEDED(rc))
        *out = std::make_unique<IFile>(std::move(fsFile));
    return rc;
}
Result IFileSystem::OpenDirectory(const char *path, u32 mode, std::unique_ptr<IDirectory> *out) {
    FsDir fsDir;
    Result rc = fsFsOpenDirectory(&m_fs, path, mode, &fsDir);
    if (R_SUCCEEDED(rc))
        *out = std::make_unique<IDirectory>(std::move(fsDir));
    return rc;
}
Result IFileSystem::Commit() {
    return fsFsCommit(&m_fs);
}
Result IFileSystem::GetFreeSpace(const char *path, s64 *out) {
    return fsFsGetFreeSpace(&m_fs, path, out);
}
Result IFileSystem::GetTotalSpace(const char *path, s64 *out) {
    return fsFsGetTotalSpace(&m_fs, path, out);
}
Result IFileSystem::GetFileTimeStampRaw(const char *path, FsTimeStampRaw *out) {
    return fsFsGetFileTimeStampRaw(&m_fs, path, out);
}
