#pragma once
#include "../common.hpp"
#include "../fs.hpp"
#include "types.hpp"
#include "utl.hpp"

#include <arpa/inet.h>

class FTPSession {
  public:
    char cwd[FS_MAX_PATH];
    char lwd[FS_MAX_PATH];
    struct sockaddr_in peer_addr;
    struct sockaddr_in pasv_addr;
    Socket cmdSocket;
    Socket pasvSocket;
    Socket dataSocket;
    u64 timestamp;
    u32 flags; /*! session_flags_t */
    xfer_dir_mode_t dir_mode;
    u32 mlst_flags; /*! session_mlst_flags_t*/
    session_state_t state;
    FsDirEntryType rn_type;
    FTPSession *next;
    FTPSession *prev;

    LoopStatus (FTPSession::*transfer)();
    char buffer[XFER_BUFFERSIZE];
    char file_buffer[FILE_BUFFERSIZE];
    char cmd_buffer[CMD_BUFFERSIZE];
    size_t bufferpos;
    size_t buffersize;
    size_t cmd_buffersize;
    s64 filepos;
    s64 filesize;
    std::shared_ptr<IFileSystem> m_sdmcFs;
    std::unique_ptr<IDirectory> m_dir;
    std::unique_ptr<IFile> m_file;
    bool user_ok;
    bool pass_ok;

  public:
    FTPSession(std::shared_ptr<IFileSystem> &sdmcFs);
    ~FTPSession();

    FTPSession *Poll();
    Result Accept();
    Result Connect();
    Result BuildPath(const char *cwd, const char *args);
    void CdUp();
    void ReadCommand(int events);
    void DecodePath(size_t length);
    void Transfer();
    void SetState(session_state_t state, int flags);
    LoopStatus ListTransfer();
    Result FillDirent(const char *path, int len);
    Result FillDirentCdir(const char *path);

    void XferDir(const char *args, xfer_dir_mode_t mode, bool workaround);
    Result OpenCWD();
    void XferFile(const char *args, xfer_file_mode_t mode);
    Result OpenFileRead();
    Result OpenFileWrite(bool append);
    u64 WriteFile();
    u64 ReadFile();

    LoopStatus RetreiveTransfer();
    LoopStatus StoreTransfer();

    void SendResponse(int code, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
    void SendResponseBuffer(const char *buffer, size_t len);
};

class FTP {
  private:
    /* SDMC filesystem */
    std::shared_ptr<IFileSystem> m_sdmcFs;
    /*! server listen address */
    struct sockaddr_in serv_addr;
    /*! listen socket */
    Socket listenSocket;

  public:
    FTP(std::shared_ptr<IFileSystem> &sdmcFs);
    ~FTP();

    Result Init();
    LoopStatus Loop();
    void Exit();

  private:
    Result AcceptSession();
    int UpdateStatus();
};
