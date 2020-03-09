#pragma once

#include "../common.hpp"
#include "ftp.hpp"

enum LoopStatus : u8 {
    CONTINUE,
    RESTART,
    EXIT,
};

/*! session state */
enum session_state_t : u8 {
    COMMAND_STATE,       /*!< waiting for a command */
    DATA_CONNECT_STATE,  /*!< waiting for connection after PASV command */
    DATA_TRANSFER_STATE, /*!< data transfer in progress */
};

/*! ftp_session_set_state flags */
enum set_state_flags_t : u8 {
    CLOSE_PASV = BIT(0), /*!< Close the pasv_fd */
    CLOSE_DATA = BIT(1), /*!< Close the data_fd */
};

/*! ftp_session_t flags */
enum session_flags_t : u8 {
    SESSION_BINARY = BIT(0), /*!< data transfers in binary mode */
    SESSION_PASV = BIT(1),   /*!< have pasv_addr ready for data transfer command */
    SESSION_PORT = BIT(2),   /*!< have peer_addr ready for data transfer command */
    SESSION_RECV = BIT(3),   /*!< data transfer in source mode */
    SESSION_SEND = BIT(4),   /*!< data transfer in sink mode */
    SESSION_RENAME = BIT(5), /*!< last command was RNFR and buffer contains path */
    SESSION_URGENT = BIT(6), /*!< in telnet urgent mode */
};

/*! ftp_xfer_dir mode */
enum xfer_dir_mode_t : u8 {
    XFER_DIR_LIST, /*!< Long list */
    XFER_DIR_MLSD, /*!< Machine list directory */
    XFER_DIR_MLST, /*!< Machine list */
    XFER_DIR_NLST, /*!< Short list */
    XFER_DIR_STAT, /*!< Stat command */
};

/*! ftp_xfer_file mode */
enum xfer_file_mode_t : u8 {
    XFER_FILE_RETR, /*!< Retrieve a file */
    XFER_FILE_STOR, /*!< Store a file */
    XFER_FILE_APPE, /*!< Append a file */
};

enum session_mlst_flags_t : u8 {
    SESSION_MLST_TYPE = BIT(0),
    SESSION_MLST_SIZE = BIT(1),
    SESSION_MLST_MODIFY = BIT(2),
    SESSION_MLST_PERM = BIT(3),
    SESSION_MLST_UNIX_MODE = BIT(4),
};

#define POLL_UNKNOWN (~(POLLIN | POLLPRI | POLLOUT))

#define XFER_BUFFERSIZE 0x4000
#define SOCK_BUFFERSIZE 0x4000
#define FILE_BUFFERSIZE 0x8000
#define CMD_BUFFERSIZE 0x1000

#define LISTEN_PORT 5000

/*! socket buffersize */
constexpr static inline int sock_buffersize = SOCK_BUFFERSIZE;
static inline u64 start_time;

struct Socket {
    Socket();
    Result Close();
    Result SetNonBlocking();
    Result SetOptions();

    int fd;
    bool connected;
};

/*! encode a path
 *
 *  @param[in]     path   path to encode
 *  @param[in,out] len    path length
 *  @param[in]     quotes whether to encode quotes
 *
 *  @returns encoded path
 *
 *  @note The caller must free the returned path
 */
char *encode_path(const char *path, size_t *len, bool quotes);

/*! validate a path
 *
 *  @param[in] args path to validate
 */
int validate_path(const char *args);

/*! send a file to the client
 *
 *  @param[in] session ftp session
 *
 *  @returns whether to call again
 */
LoopStatus retrieve_transfer(FTPSession *session);

/*! send a file to the client
 *
 *  @param[in] session ftp session
 *
 *  @returns whether to call again
 */
LoopStatus store_transfer(FTPSession *session);

Result UpdateFreeSpace(IFileSystem *fs);

namespace ftp {

    R_DEFINE_ERROR_RESULT(SocketInitFailed, 0x242);
    R_DEFINE_ERROR_RESULT(SetSockOptFailed, 0x442);
    R_DEFINE_ERROR_RESULT(BindFailed, 0x642);
    R_DEFINE_ERROR_RESULT(ListenFailed, 0x842);
    R_DEFINE_ERROR_RESULT(UpdateFailed, 0xA42);
    R_DEFINE_ERROR_RESULT(AcceptFailed, 0xC42);
    R_DEFINE_ERROR_RESULT(SessionAllocateFailed, 0xE42);
    R_DEFINE_ERROR_RESULT(GetSocketNameFailed, 0x1042);
    R_DEFINE_ERROR_RESULT(GetHostNameFailed, 0x1242);
    R_DEFINE_ERROR_RESULT(BadCommandSequence, 0x1442);
    R_DEFINE_ERROR_RESULT(FdManipulationFailed, 0x1642);
    R_DEFINE_ERROR_RESULT(ConnectFailed, 0x1842);
    R_DEFINE_ERROR_RESULT(OpenedLogFile, 0x1A42);
    R_DEFINE_ERROR_RESULT(OpenFileFailed, 0x1C42);

}