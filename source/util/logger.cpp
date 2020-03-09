#include "logger.hpp"

#include "../common.hpp"
#include "time.hpp"

#include <cstdarg>

namespace {

    constexpr size_t buf_size = 1024;
    constexpr inline const char *const LogPath = "/log.txt";
    char sprint_buf[buf_size];

    std::shared_ptr<IFileSystem> m_sdmcFs;
    s64 offset;
    bool initialized;

    Result fprintf(IFile *file, const char *fmt, ...) {
        va_list args;

        va_start(args, fmt);
        int n = std::vsnprintf(sprint_buf, buf_size, fmt, args);
        va_end(args);

        R_TRY(file->Write(offset, sprint_buf, n, FsWriteOption_None));

        offset += n;
        return ResultSuccess();
    }

}

Result InitializeLog(std::shared_ptr<IFileSystem> &sdmcFs) {
    m_sdmcFs = sdmcFs;

    Result rc = m_sdmcFs->CreateFile(LogPath, 0, 0);
    if (R_FAILED(rc) && rc != 0x402) {
        return rc;
    }
    std::unique_ptr<IFile> file;
    R_TRY(m_sdmcFs->OpenFile(LogPath, FsOpenMode_Read, &file));
    s64 fileSize;
    R_TRY(file->GetSize(&fileSize));
    offset = fileSize;

    initialized = true;

    return ResultSuccess();
}

Result Log(const char *path, int line, const char *func, const char *fmt, ...) {
    R_UNLESS(initialized, 0x1);

    u64 timestamp;
    R_TRY(timeGetCurrentTime(TimeType_Default, &timestamp));

    TimeCalendarTime datetime;
    R_TRY(hos::time::TimestampToCalendarTime(&datetime, timestamp));
    int hour = datetime.hour;
    int min = datetime.minute;
    int sec = datetime.second;

    va_list args;
#ifdef __APPLET__
    printf("%s:%d %s: ", path, line, func);
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
#endif

    std::unique_ptr<IFile> file;
    R_TRY(m_sdmcFs->OpenFile(LogPath, FsOpenMode_Write | FsOpenMode_Append, &file));

    R_TRY(fprintf(file.get(), "[%02d:%02d:%02d] %s:%d %s: ", hour, min, sec, path, line, func));

    va_start(args, fmt);
    int n = std::vsnprintf(sprint_buf, buf_size, fmt, args);
    va_end(args);

    R_TRY(file->Write(offset, sprint_buf, n, FsWriteOption_None));
    offset += n;

    R_TRY(fprintf(file.get(), "\n"));

    file->Flush();

    return ResultSuccess();
}
