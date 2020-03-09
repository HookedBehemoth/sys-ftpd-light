#include "ftp.hpp"

#include "../util/logger.hpp"
#include "commands.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>

namespace {

    /*! list of ftp sessions */
    FTPSession *sessions = nullptr;

}

FTPSession::FTPSession(std::shared_ptr<IFileSystem> &sdmcFs)
    : cwd("/"), m_sdmcFs(sdmcFs) {}

FTPSession::~FTPSession() {
    cmdSocket.Close();
    pasvSocket.Close();
    dataSocket.Close();
}

/*! poll sockets for ftp session
 *
 *  @returns next session
 */
FTPSession *FTPSession::Poll() {
    int rc;
    struct pollfd pollinfo[2];
    nfds_t nfds = 1;

    /* the first pollfd is the command socket */
    pollinfo[0].fd = this->cmdSocket.fd;
    pollinfo[0].events = POLLIN | POLLPRI;
    pollinfo[0].revents = 0;

    switch (this->state) {
        case COMMAND_STATE:
            /* we are waiting to read a command */
            break;

        case DATA_CONNECT_STATE:
            if (this->flags & SESSION_PASV) {
                /* we are waiting for a PASV connection */
                pollinfo[1].fd = this->pasvSocket.fd;
                pollinfo[1].events = POLLIN;
            } else {
                /* we are waiting to complete a PORT connection */
                pollinfo[1].fd = this->dataSocket.fd;
                pollinfo[1].events = POLLOUT;
            }
            pollinfo[1].revents = 0;
            nfds = 2;
            break;

        case DATA_TRANSFER_STATE:
            /* we need to transfer data */
            pollinfo[1].fd = this->dataSocket.fd;
            if (this->flags & SESSION_RECV)
                pollinfo[1].events = POLLIN;
            else
                pollinfo[1].events = POLLOUT;
            pollinfo[1].revents = 0;
            nfds = 2;
            break;
    }

    /* poll the selected sockets */
    rc = poll(pollinfo, nfds, 0);
    if (rc < 0) {
        LOG("poll: %d %s", errno, strerror(errno));
        this->cmdSocket.Close();
    } else if (rc > 0) {
        /* check the command socket */
        if (pollinfo[0].revents != 0) {
            /* handle command */
            if (pollinfo[0].revents & POLL_UNKNOWN)
                LOG("cmd_fd: revents=0x%08X", pollinfo[0].revents);

            /* we need to read a new command */
            if (pollinfo[0].revents & (POLLERR | POLLHUP)) {
                LOG_DEBUG("cmd revents=0x%x", pollinfo[0].revents);
                this->cmdSocket.Close();
            } else if (pollinfo[0].revents & (POLLIN | POLLPRI)) {
                this->ReadCommand(pollinfo[0].revents);
            }
        }

        /* check the data/pasv socket */
        if (nfds > 1 && pollinfo[1].revents != 0) {
            switch (this->state) {
                case COMMAND_STATE:
                    /* this shouldn't happen? */
                    break;

                case DATA_CONNECT_STATE:
                    if (pollinfo[1].revents & POLL_UNKNOWN)
                        LOG("pasv_fd: revents=0x%08X", pollinfo[1].revents);

                    /* we need to accept the PASV connection */
                    if (pollinfo[1].revents & (POLLERR | POLLHUP)) {
                        this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
                        this->SendResponse(426, "Data connection failed\r\n");
                    } else if (pollinfo[1].revents & POLLIN) {
                        if (this->Accept() != 0)
                            this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
                    } else if (pollinfo[1].revents & POLLOUT) {

                        LOG("connected to %s:%u", inet_ntoa(this->peer_addr.sin_addr), ntohs(this->peer_addr.sin_port));

                        this->SetState(DATA_TRANSFER_STATE, CLOSE_PASV);
                        this->SendResponse(150, "Ready\r\n");
                    }
                    break;

                case DATA_TRANSFER_STATE:
                    if (pollinfo[1].revents & POLL_UNKNOWN)
                        LOG("data_fd: revents=0x%08X", pollinfo[1].revents);

                    /* we need to transfer data */
                    if (pollinfo[1].revents & (POLLERR | POLLHUP)) {
                        this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
                        this->SendResponse(426, "Data connection failed\r\n");
                    } else if (pollinfo[1].revents & (POLLIN | POLLOUT)) {
                        this->Transfer();
                    }
                    break;
            }
        }
    }

    /* still connected to peer; return next session */
    if (this->cmdSocket.fd >= 0)
        return this->next;

    /* disconnected from peer; destroy it and return next session */
    LOG_DEBUG("disconnected from peer");
    auto *next = this->next;

    /* unlink from sessions list */
    if (this->next)
        this->next->prev = this->prev;
    if (this == sessions)
        sessions = this->next;
    else {
        this->prev->next = this->next;
        if (this == sessions->prev)
            sessions->prev = this->prev;
    }
    delete this;
    /* TODO: Linked list */
    return next;
}

Result FTPSession::Accept() {

    if (this->flags & SESSION_PASV) {
        /* clear PASV flag */
        this->flags &= ~SESSION_PASV;

        /* tell the peer that we're ready to accept the connection */
        this->SendResponse(150, "Ready\r\n");

        /* accept connection from peer */
        Socket newSocket;
        struct sockaddr_in addr;
        socklen_t addrlen = sizeof(addr);
        newSocket.fd = accept(this->pasvSocket.fd, (struct sockaddr *)&addr, &addrlen);
        if (newSocket.fd < 0) {
            LOG("accept: %d %s", errno, strerror(errno));
            this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
            this->SendResponse(425, "Failed to establish connection\r\n");
            return ftp::ResultAcceptFailed();
        }
        newSocket.connected = true;

        /* set the socket to non-blocking */
        Result rc = newSocket.SetNonBlocking();
        if (R_FAILED(rc)) {
            newSocket.Close();
            this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
            this->SendResponse(425, "Failed to establish connection\r\n");
            return rc;
        }

        LOG("accepted connection from %s:%u", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

        /* we are ready to transfer data */
        this->SetState(DATA_TRANSFER_STATE, CLOSE_PASV);
        this->dataSocket = newSocket;

        return ResultSuccess();
    } else {
        /* peer didn't send PASV command */
        this->SendResponse(503, "Bad sequence of commands\r\n");
        return ftp::ResultBadCommandSequence();
    }
}

Result FTPSession::Connect() {
    /* clear PORT flag */
    this->flags &= ~SESSION_PORT;

    /* create a new socket */
    this->dataSocket.fd = socket(AF_INET, SOCK_STREAM, 0);
    if (this->dataSocket.fd < 0) {
        LOG("socket: %d %s\n", errno, strerror(errno));
        return -1;
    }

    /* set socket options */
    Result rc = this->dataSocket.SetOptions();
    if (R_FAILED(rc)) {
        this->dataSocket.Close();
        return rc;
    }

    /* set socket to non-blocking */
    R_TRY(this->dataSocket.SetNonBlocking());

    /* connect to peer */
    rc = connect(this->dataSocket.fd, (struct sockaddr *)&this->peer_addr, sizeof(this->peer_addr));
    if (R_FAILED(rc)) {
        if (errno != EINPROGRESS) {
            LOG("connect: %d %s\n", errno, strerror(errno));
            this->dataSocket.Close();
            return ftp::ResultConnectFailed();
        }
    } else {
        LOG("connected to %s:%u\n", inet_ntoa(this->peer_addr.sin_addr), ntohs(this->peer_addr.sin_port));

        this->SetState(DATA_TRANSFER_STATE, CLOSE_PASV);
        this->SendResponse(150, "Ready\r\n");
    }
    this->dataSocket.connected = true;

    return ResultSuccess();
}

Result FTPSession::BuildPath(const char *cwd, const char *args) {
    int rc;
    char *p;

    this->buffersize = 0;
    memset(this->buffer, 0, sizeof(this->buffer));

    /* make sure the input is a valid path */
    if (validate_path(args) != 0) {
        errno = EINVAL;
        return -1;
    }

    if (args[0] == '/') {
        /* this is an absolute path */
        size_t len = strlen(args);
        if (len > sizeof(this->buffer) - 1) {
            errno = ENAMETOOLONG;
            return -1;
        }

        memcpy(this->buffer, args, len);
        this->buffersize = len;
    } else {
        /* this is a relative path */
        if (strcmp(cwd, "/") == 0)
            rc = std::snprintf(this->buffer, sizeof(this->buffer), "/%s",
                               args);
        else
            rc = std::snprintf(this->buffer, sizeof(this->buffer), "%s/%s",
                               cwd, args);

        if (rc >= sizeof(this->buffer)) {
            errno = ENAMETOOLONG;
            return -1;
        }

        this->buffersize = rc;
    }

    /* remove trailing / */
    p = this->buffer + this->buffersize;
    while (p > this->buffer && *--p == '/') {
        *p = 0;
        --this->buffersize;
    }

    /* if we ended with an empty path, it is the root directory */
    if (this->buffersize == 0)
        this->buffer[this->buffersize++] = '/';

    return ResultSuccess();
}

void FTPSession::CdUp() {
    char *slash = NULL, *p;

    /* remove basename from cwd */
    for (p = this->cwd; *p; ++p) {
        if (*p == '/')
            slash = p;
    }
    *slash = 0;
    if (strlen(this->cwd) == 0)
        strcat(this->cwd, "/");
}

void FTPSession::ReadCommand(int events) {
    char *buffer, *args, *next = NULL;
    size_t i, len;
    int atmark;
    ssize_t rc;

    /* check out-of-band data */
    if (events & POLLPRI) {
        this->flags |= SESSION_URGENT;

        /* check if we are at the urgent marker */
        atmark = sockatmark(this->cmdSocket.fd);
        if (atmark < 0) {
            LOG("sockatmark: %d %s", errno, strerror(errno));
            this->cmdSocket.Close();
            return;
        }

        if (!atmark) {
            /* discard in-band data */
            rc = recv(this->cmdSocket.fd, this->cmd_buffer, sizeof(this->cmd_buffer), 0);
            if (rc < 0 && errno != EWOULDBLOCK) {
                LOG("recv: %d %s", errno, strerror(errno));
                this->cmdSocket.Close();
            }

            return;
        }

        /* retrieve the urgent data */
        rc = recv(this->cmdSocket.fd, this->cmd_buffer, sizeof(this->cmd_buffer), MSG_OOB);
        if (rc < 0) {
            /* EWOULDBLOCK means out-of-band data is on the way */
            if (errno == EWOULDBLOCK)
                return;

            /* error retrieving out-of-band data */
            LOG("recv (oob): %d %s", errno, strerror(errno));
            this->cmdSocket.Close();
            return;
        }

        /* reset the command buffer */
        this->cmd_buffersize = 0;
        return;
    }

    /* prepare to receive data */
    buffer = this->cmd_buffer + this->cmd_buffersize;
    len = sizeof(this->cmd_buffer) - this->cmd_buffersize;
    if (len == 0) {
        /* error retrieving command */
        LOG("Exceeded command buffer size");
        this->cmdSocket.Close();
        return;
    }

    /* retrieve command data */
    rc = recv(this->cmdSocket.fd, buffer, len, 0);
    if (rc < 0) {
        /* error retrieving command */
        LOG("recv: %d %s", errno, strerror(errno));
        this->cmdSocket.Close();
        return;
    }
    if (rc == 0) {
        /* peer closed connection */
        LOG_DEBUG("peer closed connection");
        this->cmdSocket.Close();
        return;
    } else {
        this->cmd_buffersize += rc;
        len = sizeof(this->cmd_buffer) - this->cmd_buffersize;

        if (this->flags & SESSION_URGENT) {
            /* look for telnet data mark */
            for (i = 0; i < this->cmd_buffersize; ++i) {
                if ((unsigned char)this->cmd_buffer[i] == 0xF2) {
                    /* ignore all data that precedes the data mark */
                    if (i < this->cmd_buffersize - 1)
                        memmove(this->cmd_buffer, this->cmd_buffer + i + 1, len - i - 1);
                    this->cmd_buffersize -= i + 1;
                    this->flags &= ~SESSION_URGENT;
                    break;
                }
            }
        }

        /* loop through commands */
        while (true) {
            /* must have at least enough data for the delimiter */
            if (this->cmd_buffersize < 1)
                return;

            /* look for \r\n or \n delimiter */
            for (i = 0; i < this->cmd_buffersize; ++i) {
                if (i < this->cmd_buffersize - 1 && this->cmd_buffer[i] == '\r' && this->cmd_buffer[i + 1] == '\n') {
                    /* we found a \r\n delimiter */
                    this->cmd_buffer[i] = 0;
                    next = &this->cmd_buffer[i + 2];
                    break;
                } else if (this->cmd_buffer[i] == '\n') {
                    /* we found a \n delimiter */
                    this->cmd_buffer[i] = 0;
                    next = &this->cmd_buffer[i + 1];
                    break;
                }
            }

            /* check if a delimiter was found */
            if (i == this->cmd_buffersize)
                return;

            /* decode the command */
            this->DecodePath(i);

            /* split command from arguments */
            args = buffer = this->cmd_buffer;
            while (*args && !isspace((int)*args))
                ++args;
            if (*args)
                *args++ = 0;

            /* look up the command */
            const char *key = buffer;
            LOG_DEBUG("key: %s", key);
            ftp::Command *command = std::find_if(ftp::commands.begin(), ftp::commands.end(), [&](const ftp::Command &cmd) -> bool {
                LOG("searching: %s", cmd.name);
                return strcasecmp(cmd.name, key) == 0;
            });

            /* update command timestamp */
            this->timestamp = time(NULL);

            /* execute the command */
            if (command == NULL) {
                /* send header */
                this->SendResponse(502, "Invalid command \"");

                /* send command */
                len = strlen(buffer);
                buffer = encode_path(buffer, &len, false);
                if (buffer != NULL)
                    this->SendResponseBuffer(buffer, len);
                else
                    this->SendResponseBuffer(key, strlen(key));
                free(buffer);

                /* send args (if any) */
                if (*args != 0) {
                    this->SendResponseBuffer(" ", 1);

                    len = strlen(args);
                    buffer = encode_path(args, &len, false);
                    if (buffer != NULL)
                        this->SendResponseBuffer(buffer, len);
                    else
                        this->SendResponseBuffer(args, strlen(args));
                    free(buffer);
                }

                /* send footer */
                this->SendResponseBuffer("\"\r\n", 3);
            } else if (this->state != COMMAND_STATE) {
                /* only some commands are available during data transfer */
                if (strcasecmp(command->name, "ABOR") != 0 && strcasecmp(command->name, "STAT") != 0 && strcasecmp(command->name, "QUIT") != 0) {
                    this->SendResponse(503, "Invalid command during transfer\r\n");
                    this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
                    this->cmdSocket.Close();
                } else
                    command->handler(this, args);
            } else {
                /* clear RENAME flag for all commands except RNTO */
                if (strcasecmp(command->name, "RNTO") != 0)
                    this->flags &= ~SESSION_RENAME;

                command->handler(this, args);
            }

            /* remove executed command from the command buffer */
            len = this->cmd_buffer + this->cmd_buffersize - next;
            if (len > 0)
                memmove(this->cmd_buffer, next, len);
            this->cmd_buffersize = len;
        }
    }
}

/*! decode a path
 *
 *  @param[in] len     command length
 */
void FTPSession::DecodePath(size_t length) {
    /* decode \0 from the first command */
    for (size_t i = 0; i < length; ++i) {
        /* this is an encoded \n */
        if (this->cmd_buffer[i] == 0)
            this->cmd_buffer[i] = '\n';
    }
}

/*! transfer loop
 *
 *  Try to transfer as much data as the sockets will allow without blocking
 */
void FTPSession::Transfer() {
    LoopStatus rc;
    do {
        rc = this->transfer(this);
    } while (rc == LoopStatus::CONTINUE);
}

/*! set state for ftp session
 *
 *  @param[in] state   state to set
 *  @param[in] flags   flags
 */
void FTPSession::SetState(session_state_t state, int flags) {
    this->state = state;

    /* close pasv and data sockets */
    if (flags & CLOSE_PASV)
        this->pasvSocket.Close();
    if (flags & CLOSE_DATA)
        this->dataSocket.Close();

    if (state == COMMAND_STATE) {
        /* close file/cwd */
        this->m_file->Close();
        this->m_dir->Close();
    }
}

/*! transfer a directory listing
 *
 *  @param[in] session ftp session
 *
 *  @returns whether to call again
 */
LoopStatus FTPSession::ListTransfer() {
    static FsDirectoryEntry entries[0x10];
    ssize_t rc;
    size_t len;
    char *buffer;

    /* check if we sent all available data */
    if (this->bufferpos == this->buffersize) {
        /* check xfer dir type */
        if (this->dir_mode == XFER_DIR_STAT)
            rc = 213;
        else
            rc = 226;

        /* check if this was for a file */
        if (!this->m_dir->IsOpen()) {
            /* we already sent the file's listing */
            this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
            this->SendResponse(rc, "OK\r\n");
            return LoopStatus::EXIT;
        }

        /* get the next directory entry */
        s64 total;
        this->m_dir->Read(&total, 0x10, entries);
        if (total == 0) {
            /* we have exhausted the directory listing */
            this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
            this->SendResponse(rc, "OK\r\n");
            return LoopStatus::EXIT;
        }

        /* check if this was a NLST */
        if (this->dir_mode == XFER_DIR_NLST) {
            /* NLST gives the whole path name */
            this->buffersize = 0;
            if (this->BuildPath(this->lwd, dent->d_name) == 0) {
                /* encode \n in path */
                len = this->buffersize;
                buffer = encode_path(this->buffer, &len, false);
                if (buffer != NULL) {
                    /* copy to the session buffer to send */
                    memcpy(this->buffer, buffer, len);
                    free(buffer);
                    this->buffer[len++] = '\r';
                    this->buffer[len++] = '\n';
                    this->buffersize = len;
                }
            }
        } else {
            /* lstat the entry */
            if ((rc = this->BuildPath(this->lwd, dent->d_name)) != 0) {
                LOG("build_path: %d %s\n", errno, strerror(errno));
            } else if ((rc = lstat(this->buffer, &st)) != 0) {
                LOG("stat '%s': %d %s\n", this->buffer, errno, strerror(errno));
            }

            if (rc != 0) {
                /* an error occurred */
                this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
                this->SendResponse(550, "unavailable\r\n");
                return LoopStatus::EXIT;
            }
            /* encode \n in path */
            len = strlen(dent->d_name);
            buffer = encode_path(dent->d_name, &len, false);
            if (buffer != NULL) {
                rc = ftp_session_fill_dirent(session, &st, buffer, len);
                free(buffer);
                if (rc != 0) {
                    this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
                    this->SendResponse(425, "%s\r\n", strerror(rc));
                    return LoopStatus::EXIT;
                }
            } else
                this->buffersize = 0;
        }
        this->bufferpos = 0;
    }

    /* send any pending data */
    rc = send(this->dataSocket.fd, this->buffer + this->bufferpos,
              this->buffersize - this->bufferpos, 0);
    if (rc <= 0) {
        /* error sending data */
        if (rc < 0) {
            if (errno == EWOULDBLOCK)
                return LoopStatus::EXIT;
            LOG("send: %d %s\n", errno, strerror(errno));
        } else {
            LOG("send: %d %s\n", ECONNRESET, strerror(ECONNRESET));
        }

        this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
        this->SendResponse(426, "Connection broken during transfer\r\n");
        return LoopStatus::EXIT;
    }

    /* we can try to send more data */
    this->bufferpos += rc;
    return LoopStatus::CONTINUE;
}

/*! Transfer a directory
 *
 *  @param[in] args       ftp arguments
 *  @param[in] mode       transfer mode
 *  @param[in] workaround whether to workaround LIST -a
 */
void FTPSession::XferDir(const char *args, xfer_dir_mode_t mode, bool workaround) {
    Result rc;
    size_t len;
    char *buffer;

    /* set up the transfer */
    this->dir_mode = mode;
    this->flags &= ~SESSION_RECV;
    this->flags |= SESSION_SEND;

    this->transfer = this->ListTransfer;
    this->buffersize = 0;
    this->bufferpos = 0;

    if (strlen(args) > 0) {
        /* an argument was provided */
        if (this->BuildPath(this->cwd, args) != 0) {
            /* error building path */
            this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
            this->SendResponse(550, "%s\r\n", strerror(errno));
            return;
        }

        /* check if this is a directory */
        FsDirEntryType type;
        rc = this->m_sdmcFs->GetEntryType(this->buffer, &type);
        if (R_FAILED(rc)) {
            /* work around broken clients that think LIST -a is valid */
            if (workaround && mode == XFER_DIR_LIST) {
                if (args[0] == '-' && (args[1] == 'a' || args[1] == 'l')) {
                    if (args[2] == 0)
                        buffer = strdup(args + 2);
                    else
                        buffer = strdup(args + 3);

                    if (buffer != NULL) {
                        this->XferDir(buffer, mode, false);
                        free(buffer);
                        return;
                    }

                    rc = ENOMEM;
                }
            }

            this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
            this->SendResponse(550, "%s\r\n", strerror(rc));
            return;
        }

        if (type == FsDirEntryType_File) {
            if (mode == XFER_DIR_MLSD) {
                /* specified file instead of directory for MLSD */
                this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
                this->SendResponse(501, "%s\r\n", strerror(EINVAL));
                return;
            } else if (mode == XFER_DIR_NLST) {
                /* NLST uses full path name */
                len = this->buffersize;
                buffer = encode_path(this->buffer, &len, false);
            } else {
                /* everything else uses base name */
                const char *base = strrchr(this->buffer, '/') + 1;

                len = strlen(base);
                buffer = encode_path(base, &len, false);
            }

            if (buffer) {
                rc = ftp_session_fill_dirent(session, &st, buffer, len);
                free(buffer);
            } else
                rc = ENOMEM;

            if (rc != 0) {
                this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
                this->SendResponse(550, "%s\r\n", strerror(rc));
                return;
            }
        } else if (type == FsDirEntryType_Dir) {
            /* it was a directory, so set it as the lwd */
            memcpy(this->lwd, this->buffer, this->buffersize);
            this->lwd[this->buffersize] = 0;
            this->buffersize = 0;

            if (this->dir_mode == XFER_DIR_MLSD && (this->mlst_flags & SESSION_MLST_TYPE)) {
                /* send this directory as type=cdir */
                rc = ftp_session_fill_dirent_cdir(session, this->lwd);
                if (rc != 0) {
                    this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
                    this->SendResponse(550, "%s\r\n", strerror(rc));
                    return;
                }
            }
        }
    } else if (ftp_session_open_cwd(session) != 0) {
        /* no argument, but opening cwd failed */
        this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
        this->SendResponse(550, "%s\r\n", strerror(errno));
        return;
    } else {
        /* set the cwd as the lwd */
        strcpy(this->lwd, this->cwd);
        this->buffersize = 0;

        if (this->dir_mode == XFER_DIR_MLSD && (this->mlst_flags & SESSION_MLST_TYPE)) {
            /* send this directory as type=cdir */
            rc = ftp_session_fill_dirent_cdir(session, this->lwd);
            if (rc != 0) {
                this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
                this->SendResponse(550, "%s\r\n", strerror(rc));
                return;
            }
        }
    }

    if (mode == XFER_DIR_MLST || mode == XFER_DIR_STAT) {
        /* this is a little different; we have to send the data over the command socket */
        this->SetState(DATA_TRANSFER_STATE, CLOSE_PASV | CLOSE_DATA);
        this->dataSocket = this->cmdSocket;
        this->flags |= SESSION_SEND;
        this->SendResponse(-213, "Status\r\n");
        return;
    } else if (this->flags & (SESSION_PORT | SESSION_PASV)) {
        this->SetState(DATA_CONNECT_STATE, CLOSE_DATA);

        if (this->flags & SESSION_PORT) {
            /* setup connection */
            rc = this->Connect();
            if (rc != 0) {
                /* error connecting */
                this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
                this->SendResponse(425, "can't open data connection\r\n");
            }
        }

        return;
    }

    /* we must have got LIST/MLSD/MLST/NLST without a preceding PORT or PASV */
    this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
    this->SendResponse(503, "Bad sequence of commands\r\n");
}

/*! Transfer a file
 *
 *  @param[in] args    ftp arguments
 *  @param[in] mode    transfer mode
 *
 *  @returns failure
 */
void FTPSession::XferFile(const char *args, xfer_file_mode_t mode) {
    int rc;

    /* build the path of the file to transfer */
    if (this->BuildPath(this->cwd, args) != 0) {
        rc = errno;
        this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
        this->SendResponse(553, "%s\r\n", strerror(rc));
        return;
    }

    /* open the file for retrieving or storing */
    if (mode == XFER_FILE_RETR) {
        rc = this->OpenFileRead();
    } else {
        rc = this->OpenFileWrite(mode == XFER_FILE_APPE);
    }

    if (R_FAILED(rc)) {
        /* error opening the file */
        this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
        this->SendResponse(450, "failed to open file\r\n");
        return;
    }

    if (this->flags & (SESSION_PORT | SESSION_PASV)) {
        this->SetState(DATA_CONNECT_STATE, CLOSE_DATA);

        if (this->flags & SESSION_PORT) {
            /* setup connection */
            rc = this->Connect();
            if (R_FAILED(rc)) {
                /* error connecting */
                this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
                this->SendResponse(425, "can't open data connection\r\n");
                return;
            }
        }

        /* set up the transfer */
        this->flags &= ~(SESSION_RECV | SESSION_SEND);
        if (mode == XFER_FILE_RETR) {
            this->flags |= SESSION_SEND;
            this->transfer = retrieve_transfer;
        } else {
            this->flags |= SESSION_RECV;
            this->transfer = store_transfer;
        }

        this->bufferpos = 0;
        this->buffersize = 0;

        return;
    }

    this->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
    this->SendResponse(503, "Bad sequence of commands\r\n");
}

/*! open file for writing for ftp session
 *
 *  @param[in] append  whether to append
 *
 *  @returns -1 for error
 *
 *  @note truncates file
 */
Result FTPSession::OpenFileWrite(bool append) {
    if (!strcmp("/log.txt", this->buffer)) {
        LOG("Tried to open log.txt for reading. That's not allowed!\n");
        return ftp::ResultOpenedLogFile();
    }

    /* allow append if set */
    u32 mode = FsOpenMode_Write | (append ? FsOpenMode_Append : 0);

    /* open file in write mode */
    Result rc = this->m_sdmcFs->OpenFile(this->buffer, mode, &this->m_file);
    if (R_FAILED(rc)) {
        LOG("OpenFile '%s': %d\n", this->buffer, rc);
        return ftp::ResultOpenFileFailed();
    }

    return ResultSuccess();
    ;
}

/*! write to an open file for ftp session
 *
 *  @returns bytes written
 */
u64 FTPSession::WriteFile() {
    u64 write_size = this->buffersize - this->bufferpos;
    /* write to file at current position */
    Result rc = this->m_file->Write(this->filepos, this->buffer + this->bufferpos, write_size, FsReadOption_None);
    if (R_FAILED(rc)) {
        LOG("fsFileWrite: %d\n", rc);
        return -1;
    }

    /* adjust file position */
    this->filepos += write_size;

    UpdateFreeSpace(this->m_sdmcFs.get());
    return write_size;
}

/*! open file for reading for ftp session
 *
 *  @param[in] session ftp session
 *
 *  @returns -1 for error
 */
Result FTPSession::OpenFileRead() {
    /* open file in read mode */
    if (!strcmp("/log.txt", this->buffer)) {
        LOG("Tried to open log.txt for reading. That's not allowed!\n");
        return ftp::ResultOpenedLogFile();
    }

    Result rc = this->m_sdmcFs->OpenFile(this->buffer, FsOpenMode_Read, &this->m_file);
    if (R_FAILED(rc)) {
        LOG("OpenFile '%s': %d\n", this->buffer, rc);
        return ftp::ResultOpenFileFailed();
    }

    /* get the file size */
    s64 tmp;
    rc = this->m_file->GetSize(&tmp);
    if (R_FAILED(rc)) {
        m_file->Close();
        LOG("GetSize '%s': %d\n", this->buffer, rc);
        return -1;
    }
    this->filesize = tmp;

    return ResultSuccess();
}

/*! read from an open file for ftp session
 *
 *  @returns bytes read
 */
u64 FTPSession::ReadFile() {
    /* read file at current position */
    u64 read;
    Result rc = this->m_file->Read(this->filepos, this->buffer, sizeof(this->buffer), 0, &read);

    if (R_FAILED(rc)) {
        LOG("fsFileRead: %d %ld\n", rc, read);
        return -1;
    }

    /* adjust file position */
    this->filepos += read;

    return read;
}

/*! send ftp response to ftp session's peer
 *
 *  @param[in] code    response code
 *  @param[in] fmt     format string
 *  @param[in] ...     format arguments
 */
void FTPSession::SendResponse(int code, const char *fmt, ...) {
    static char buffer[CMD_BUFFERSIZE];
    size_t rc;
    va_list args;

    if (!this->cmdSocket.connected)
        return;

    /* print response code and message to buffer */
    va_start(args, fmt);
    if (code > 0)
        rc = sprintf(buffer, "%d ", code);
    else
        rc = sprintf(buffer, "%d-", -code);
    rc += vsnprintf(buffer + rc, sizeof(buffer) - rc, fmt, args);
    va_end(args);

    if (rc >= sizeof(buffer)) {
        /* couldn't fit message; just send code */
        LOG("%s: buffersize too small", __func__);
        if (code > 0)
            rc = sprintf(buffer, "%d \r\n", code);
        else
            rc = sprintf(buffer, "%d-\r\n", -code);
    }

    this->SendResponseBuffer(buffer, rc);
}

/*! send a response on the command socket
 *
 *  @param[in] buffer  buffer to send
 *  @param[in] len     buffer length
 */
void FTPSession::SendResponseBuffer(const char *buffer, size_t len) {
    if (!this->cmdSocket.connected)
        return;

    /* send response */
    LOG_DEBUG("%s", buffer);
    size_t rc = send(this->cmdSocket.fd, buffer, len, 0);
    if (rc < 0) {
        LOG("send: %d %s", errno, strerror(errno));
        this->cmdSocket.Close();
    } else if (rc != len) {
        LOG("only sent %u/%u bytes", (unsigned int)rc, (unsigned int)len);
        this->cmdSocket.Close();
    }
}

FTP::FTP(std::shared_ptr<IFileSystem> &sdmcFs)
    : m_sdmcFs(sdmcFs), serv_addr(), listenSocket() {
    timeGetCurrentTime(TimeType_Default, &start_time);
}

FTP::~FTP() {}

Result FTP::Init() {
    int rc;

    /* allocate socket to listen for clients */
    listenSocket.fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket.fd < 0) {
        LOG("failed to init socket: %d", listenSocket.fd);
        return ftp::ResultSocketInitFailed();
    }
    listenSocket.connected = true;

    /* get address to listen on */
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(LISTEN_PORT);

    /* reuse address */
    {
        int yes = 1;
        rc = setsockopt(listenSocket.fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (R_FAILED(rc)) {
            LOG("setsockopt: %d %s", errno, strerror(errno));
            this->Exit();
            return ftp::ResultSocketInitFailed();
        }
    }

    /* bind socket to listen address */
    rc = bind(listenSocket.fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (R_FAILED(rc)) {
        LOG("bind: %d %s", errno, strerror(errno));
        this->Exit();
        return ftp::ResultBindFailed();
    }

    /* listen on socket */
    rc = listen(listenSocket.fd, 5);
    if (R_FAILED(rc)) {
        LOG("listen: %d %s", errno, strerror(errno));
        this->Exit();
        return ftp::ResultListenFailed();
    }

    /* print server address */
    rc = this->UpdateStatus();
    if (R_FAILED(rc)) {
        this->Exit();
        return rc;
    }

    return ResultSuccess();
}

/*! ftp loop
 *
 *  @returns whether to keep looping
 */
LoopStatus FTP::Loop() {
    struct pollfd pollinfo;
    FTPSession *session;

    /* we will poll for new client connections */
    pollinfo.fd = this->listenSocket.fd;
    pollinfo.events = POLLIN;
    pollinfo.revents = 0;

    /* poll for a new client */
    int rc = poll(&pollinfo, 1, 0);
    if (rc < 0) {
        /* wifi got disabled */
        LOG("poll: FAILED!");

        if (errno == ENETDOWN)
            return LoopStatus::RESTART;

        LOG("poll: %d %s", errno, strerror(errno));
        return LoopStatus::EXIT;
    } else if (rc > 0) {
        if (pollinfo.revents & POLLIN) {
            /* we got a new client */
            if (R_FAILED(this->AcceptSession())) {
                return LoopStatus::RESTART;
            }
        } else {
            LOG("listenfd: revents=0x%08X", pollinfo.revents);
        }
    }

    /* poll each session */
    session = sessions;
    while (session != NULL)
        session = session->Poll();

    hidScanInput();
    u64 down = hidKeysDown(CONTROLLER_P1_AUTO);
    if (down & KEY_B)
        return LoopStatus::EXIT;

    return LoopStatus::CONTINUE;
}

void FTP::Exit() {
    LOG_DEBUG("exiting ftp server");
}

Result FTP::AcceptSession() {
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    Socket newSocket;

    /* accept connection */
    newSocket.fd = accept(listenSocket.fd, (struct sockaddr *)&addr, &addrlen);
    if (newSocket.fd < 0) {
        LOG("accept: %d %s", errno, strerror(errno));
        return ftp::ResultAcceptFailed();
    }
    newSocket.connected = true;

    LOG_DEBUG("accepted connection from %s:%u", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

    /* allocate a new session */
    FTPSession *session = new FTPSession(this->m_sdmcFs);
    if (session == NULL) {
        LOG("failed to allocate session");
        newSocket.Close();
        return ftp::ResultSessionAllocateFailed();
    }

    /* initialize session */
    session->peer_addr.sin_addr.s_addr = INADDR_ANY;
    session->cmdSocket = newSocket;
    session->mlst_flags = SESSION_MLST_TYPE | SESSION_MLST_SIZE | SESSION_MLST_MODIFY | SESSION_MLST_PERM;
    session->state = COMMAND_STATE;
    session->user_ok = false;
    session->pass_ok = false;

    /* link to the sessions list */
    if (sessions == NULL) {
        sessions = session;
        session->prev = session;
    } else {
        sessions->prev->next = session;
        session->prev = sessions->prev;
        sessions->prev = session;
    }

    /* copy socket address to pasv address */
    addrlen = sizeof(session->pasv_addr);
    int rc = getsockname(newSocket.fd, (struct sockaddr *)&session->pasv_addr, &addrlen);
    if (R_FAILED(rc)) {
        LOG("getsockname: %d %s", errno, strerror(errno));
        session->SendResponse(451, "Failed to get connection info\r\n");
        delete session;
        return ftp::ResultGetSocketNameFailed();
    }

    session->cmdSocket = newSocket;

    /* send initiator response */
    session->SendResponse(220, "Hello!\r\n");

    return ResultSuccess();
}

int FTP::UpdateStatus() {
    LOG_DEBUG("%s:%u", inet_ntoa(serv_addr.sin_addr), ntohs(serv_addr.sin_port));

    UpdateFreeSpace(m_sdmcFs.get());

    char hostname[0x80];
    socklen_t addrlen = sizeof(serv_addr);
    int rc = getsockname(listenSocket.fd, (struct sockaddr *)&serv_addr, &addrlen);
    if (R_FAILED(rc)) {
        LOG("getsockname: %d %s", errno, strerror(errno));
        return ftp::ResultGetSocketNameFailed();
    }

    rc = gethostname(hostname, sizeof(hostname));
    if (R_FAILED(rc)) {
        LOG("gethostname: %d %s", errno, strerror(errno));
        return ftp::ResultGetHostNameFailed();
    }

    LOG_DEBUG("IP: %s Port: %u", hostname, ntohs(serv_addr.sin_port));

    return ResultSuccess();
}
