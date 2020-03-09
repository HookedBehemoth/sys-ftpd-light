#include "commands.hpp"

#include "../util/logger.hpp"
#include "../util/time.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cinttypes>
#include <cstring>
#include <strings.h>
#include <sys/socket.h>

namespace ftp {

    /** @fn static void ABOR(ftp_session_t *session, const char *args)
     *
     *  @brief abort a transfer
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void ABOR(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        if (session->state == COMMAND_STATE) {
            session->SendResponse(225, "No transfer to abort\r\n");
            return;
        }

        /* abort the transfer */
        session->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);

        /* send response for this request */
        session->SendResponse(225, "Aborted\r\n");

        /* send response for transfer */
        session->SendResponse(425, "Transfer aborted\r\n");
    }

    void ALLO(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        session->SendResponse(202, "superfluous command\r\n");
    }

    /*! @fn static void APPE(ftp_session_t *session, const char *args)
     *
     *  @brief append data to a file
     *
     *  @note requires a PASV or PORT connection
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void APPE(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        /* open the file in append mode */
        session->XferFile(args, XFER_FILE_APPE);
    }

    /*! @fn static void CDUP(ftp_session_t *session, const char *args)
     *
     *  @brief CWD to parent directory
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void CDUP(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        /* change to parent directory */
        session->CdUp();

        session->SendResponse(200, "OK\r\n");
    }

    /*! @fn static void CWD(ftp_session_t *session, const char *args)
     *
     *  @brief change working directory
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void CWD(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        /* .. is equivalent to CDUP */
        if (strcmp(args, "..") == 0) {
            session->CdUp();
            session->SendResponse(200, "OK\r\n");
            return;
        }

        /* build the new cwd path */
        if (session->BuildPath(session->cwd, args) != 0) {
            session->SendResponse(553, "%s\r\n", strerror(errno));
            return;
        }

        /* get the path status */
        std::unique_ptr<IFile> file;
        Result rc = session->m_sdmcFs->OpenFile(session->buffer, FsOpenMode_Read, &file);
        if (R_FAILED(rc)) {
            LOG("stat '%s': %d %s\n", session->buffer, errno, strerror(errno));
            session->SendResponse(550, "unavailable\r\n");
            return;
        }
        file->Close();

        /* make sure it is a directory */
        FsDirEntryType type;
        rc = session->m_sdmcFs->GetEntryType(session->buffer, &type);
        if (type != FsDirEntryType_Dir) {
            session->SendResponse(553, "not a directory\r\n");
            return;
        }

        /* copy the path into the cwd */
        strncpy(session->cwd, session->buffer, sizeof(session->cwd));

        session->SendResponse(200, "OK\r\n");
    }

    /*! @fn static void DELE(ftp_session_t *session, const char *args)
     *
     *  @brief delete a file
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void DELE(FTPSession *session, const char *args) {
        int rc;

        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        /* build the file path */
        if (session->BuildPath(session->cwd, args) != 0) {
            session->SendResponse(553, "%s\r\n", strerror(errno));
            return;
        }

        /* try to unlink the path */
        rc = unlink(session->buffer);
        if (rc != 0) {
            /* error unlinking the file */
            LOG("unlink: %d %s\n", errno, strerror(errno));
            session->SendResponse(550, "failed to delete file\r\n");
            return;
        }

        UpdateFreeSpace(session->m_sdmcFs.get());
        session->SendResponse(250, "OK\r\n");
    }

    /*! @fn static void FEAT(ftp_session_t *session, const char *args)
     *
     *  @brief list server features
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void FEAT(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        /* list our features */
        session->SendResponse(-211, "\r\n"
                                    " MDTM\r\n"
                                    " MLST Type%s;Size%s;Modify%s;Perm%s;UNIX.mode%s;\r\n"
                                    " PASV\r\n"
                                    " SIZE\r\n"
                                    " TVFS\r\n"
                                    " UTF8\r\n"
                                    "\r\n"
                                    "211 End\r\n",
                              session->mlst_flags & SESSION_MLST_TYPE ? "*" : "",
                              session->mlst_flags & SESSION_MLST_SIZE ? "*" : "",
                              session->mlst_flags & SESSION_MLST_MODIFY ? "*" : "",
                              session->mlst_flags & SESSION_MLST_PERM ? "*" : "",
                              session->mlst_flags & SESSION_MLST_UNIX_MODE ? "*" : "");
    }

    /*! @fn static void HELP(ftp_session_t *session, const char *args)
     *
     *  @brief print server help
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void HELP(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        /* list our accepted commands */
        session->SendResponse(-214,
                              "The following commands are recognized\r\n"
                              " ABOR ALLO APPE CDUP CWD DELE FEAT HELP LIST MDTM MKD MLSD MLST MODE\r\n"
                              " NLST NOOP OPTS PASS PASV PORT PWD QUIT REST RETR RMD RNFR RNTO STAT\r\n"
                              " STOR STOU STRU SYST TYPE USER XCUP XCWD XMKD XPWD XRMD\r\n"
                              "214 End\r\n");
    }

    /*! @fn static void LIST(ftp_session_t *session, const char *args)
     *
     *  @brief retrieve a directory listing
     *
     *  @note Requires a PORT or PASV connection
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void LIST(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        /* open the path in LIST mode */
        session->XferDir(args, XFER_DIR_LIST, true);
    }

    /*! @fn static void MDTM(ftp_session_t *session, const char *args)
     *
     *  @brief get last modification time
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void MDTM(FTPSession *session, const char *args) {
        FsTimeStampRaw fsTime;
        struct tm *tm;

        if (hosversionBefore(3, 0, 0)) {
            session->SendResponse(550, "Error getting mtime\r\n");
            return;
        }

        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        /* build the path */
        if (session->BuildPath(session->cwd, args) != 0) {
            session->SendResponse(553, "%s\r\n", strerror(errno));
            return;
        }

        Result rc = session->m_sdmcFs->GetFileTimeStampRaw(session->buffer, &fsTime);
        if (R_FAILED(rc)) {
            session->SendResponse(550, "Error getting mtime\r\n");
            return;
        }

        tm = gmtime((const time_t *)&fsTime.modified);
        if (tm == NULL) {
            session->SendResponse(550, "Error getting mtime\r\n");
            return;
        }

        session->buffersize = strftime(session->buffer, sizeof(session->buffer), "%Y%m%d%H%M%S", tm);
        if (session->buffersize == 0) {
            session->SendResponse(550, "Error getting mtime\r\n");
            return;
        }

        session->buffer[session->buffersize] = 0;

        session->SendResponse(213, "%s\r\n", session->buffer);
    }
    /*! @fn static void MKD(ftp_session_t *session, const char *args)
     *
     *  @brief create a directory
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void MKD(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        /* build the path */
        if (session->BuildPath(session->cwd, args) != 0) {
            session->SendResponse(553, "%s\r\n", strerror(errno));
            return;
        }

        /* try to create the directory */
        Result rc = session->m_sdmcFs->CreateDirectory(session->buffer);
        if (R_FAILED(rc) && rc != 0x402) {
            /* mkdir failure */
            LOG("fsFsCreateDirectory: %d\n", rc);
            session->SendResponse(550, "failed to create directory\r\n");
            return;
        }

        UpdateFreeSpace(session->m_sdmcFs.get());
        session->SendResponse(250, "OK\r\n");
    }

    /*! @fn static void MLSD(ftp_session_t *session, const char *args)
     *
     *  @brief set transfer mode
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void MLSD(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        /* open the path in MLSD mode */
        session->XferDir(args, XFER_DIR_MLSD, true);
    }

    /*! @fn static void MLST(ftp_session_t *session, const char *args)
     *
     *  @brief set transfer mode
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void MLST(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        /* build the path */
        if (session->BuildPath(session->cwd, args) != 0) {
            session->SendResponse(501, "%s\r\n", strerror(errno));
            return;
        }

        /* stat path */
        FsDirEntryType type;
        Result rc = session->m_sdmcFs->GetEntryType(session->buffer, &type);
        if (R_FAILED(rc)) {
            session->SendResponse(550, "0x%x\r\n", rc);
            return;
        }

        /* encode \n in path */
        size_t len = session->buffersize;
        char *path = encode_path(session->buffer, &len, true);
        if (!path) {
            session->SendResponse(550, "%s\r\n", strerror(ENOMEM));
            return;
        }

        session->dir_mode = XFER_DIR_MLST;
        rc = session->FillDirent(path, len);
        free(path);
        if (rc != 0) {
            session->SendResponse(550, "%s\r\n", strerror(errno));
            return;
        }

        path = (char *)malloc(session->buffersize + 1);
        if (!path) {
            session->SendResponse(550, "%s\r\n", strerror(ENOMEM));
            return;
        }

        memcpy(path, session->buffer, session->buffersize);
        path[session->buffersize] = 0;
        session->SendResponse(-250, "Status\r\n%s250 End\r\n", path);
        free(path);
    }

    /*! @fn static void MODE(ftp_session_t *session, const char *args)
     *
     *  @brief set transfer mode
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void MODE(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        /* we only accept S (stream) mode */
        if (strcasecmp(args, "S") == 0) {
            session->SendResponse(200, "OK\r\n");
            return;
        }

        session->SendResponse(504, "unavailable\r\n");
    }

    /*! @fn static void NLST(ftp_session_t *session, const char *args)
     *
     *  @brief retrieve a name list
     *
     *  @note Requires a PASV or PORT connection
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void NLST(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        /* open the path in NLST mode */
        return session->XferDir(args, XFER_DIR_NLST, false);
    }

    /*! @fn static void NOOP(ftp_session_t *session, const char *args)
     *
     *  @brief no-op
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void NOOP(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        /* this is a no-op */
        session->SendResponse(200, "OK\r\n");
    }

    /*! @fn static void OPTS(ftp_session_t *session, const char *args)
     *
     *  @brief set options
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void OPTS(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        /* we accept the following UTF8 options */
        if (strcasecmp(args, "UTF8") == 0 || strcasecmp(args, "UTF8 ON") == 0 || strcasecmp(args, "UTF8 NLST") == 0) {
            session->SendResponse(200, "OK\r\n");
            return;
        }

        /* check MLST options */
        if (strncasecmp(args, "MLST ", 5) == 0) {
            static const struct
            {
                const char *name;
                session_mlst_flags_t flag;
            } mlst_flags[] =
                {
                    {
                        "Type;",
                        SESSION_MLST_TYPE,
                    },
                    {
                        "Size;",
                        SESSION_MLST_SIZE,
                    },
                    {
                        "Modify;",
                        SESSION_MLST_MODIFY,
                    },
                    {
                        "Perm;",
                        SESSION_MLST_PERM,
                    },
                    {
                        "UNIX.mode;",
                        SESSION_MLST_UNIX_MODE,
                    },
                };
            static const size_t num_mlst_flags = sizeof(mlst_flags) / sizeof(mlst_flags[0]);

            int flags = 0;
            args += 5;
            const char *p = args;
            while (*p) {
                for (size_t i = 0; i < num_mlst_flags; ++i) {
                    if (strncasecmp(mlst_flags[i].name, p, std::strlen(mlst_flags[i].name)) == 0) {
                        flags |= mlst_flags[i].flag;
                        p += strlen(mlst_flags[i].name) - 1;
                        break;
                    }
                }

                while (*p && *p != ';')
                    ++p;

                if (*p == ';')
                    ++p;
            }

            session->mlst_flags = flags;
            session->SendResponse(200, "MLST OPTS%s%s%s%s%s%s\r\n",
                                  flags ? " " : "",
                                  flags & SESSION_MLST_TYPE ? "Type;" : "",
                                  flags & SESSION_MLST_SIZE ? "Size;" : "",
                                  flags & SESSION_MLST_MODIFY ? "Modify;" : "",
                                  flags & SESSION_MLST_PERM ? "Perm;" : "",
                                  flags & SESSION_MLST_UNIX_MODE ? "UNIX.mode;" : "");
            return;
        }

        session->SendResponse(504, "invalid argument\r\n");
    }

    /*! @fn static void PASS(ftp_session_t *session, const char *args)
     *
     *  @brief provide password
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void PASS(FTPSession *session, const char *args) {
        LOG_DEBUG(args);
        session->user_ok = true;
        session->pass_ok = true;
        session->SetState(COMMAND_STATE, 0);
        session->SendResponse(230, "OK\r\n");
        /* TODO: actually authenticate */
    }

    /*! @fn static void PASV(ftp_session_t *session, const char *args)
     *
     *  @brief request an address to connect to
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void PASV(FTPSession *session, const char *args) {
        int rc;
        char buffer[INET_ADDRSTRLEN + 10];
        char *p;
        in_port_t port;

        LOG_DEBUG(args);

        memset(buffer, 0, sizeof(buffer));

        /* reset the state */
        session->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
        session->flags &= ~(SESSION_PASV | SESSION_PORT);

        /* create a socket to listen on */
        session->pasvSocket.fd = socket(AF_INET, SOCK_STREAM, 0);
        if (session->pasvSocket.fd < 0) {
            LOG("socket: %d %s\n", errno, strerror(errno));
            session->SendResponse(451, "\r\n");
            return;
        }

        /* set the socket options */
        rc = session->pasvSocket.SetOptions();
        if (rc != 0) {
            /* failed to set socket options */
            session->pasvSocket.Close();
            session->SendResponse(451, "\r\n");
            return;
        }

        LOG("binding to %s:%u\n",
            inet_ntoa(session->pasv_addr.sin_addr),
            ntohs(session->pasv_addr.sin_port));

        /* bind to the port */
        rc = bind(session->pasvSocket.fd, (struct sockaddr *)&session->pasv_addr,
                  sizeof(session->pasv_addr));
        if (rc != 0) {
            /* failed to bind */
            LOG("bind: %d %s\n", errno, strerror(errno));
            session->pasvSocket.Close();
            session->SendResponse(451, "\r\n");
            return;
        }

        /* listen on the socket */
        rc = listen(session->pasvSocket.fd, 1);
        if (rc != 0) {
            /* failed to listen */
            LOG("listen: %d %s\n", errno, strerror(errno));
            session->pasvSocket.Close();
            session->SendResponse(451, "\r\n");
            return;
        }

#ifndef _3DS
        {
            /* get the socket address since we requested an ephemeral port */
            socklen_t addrlen = sizeof(session->pasv_addr);
            rc = getsockname(session->pasvSocket.fd, (struct sockaddr *)&session->pasv_addr,
                             &addrlen);
            if (rc != 0) {
                /* failed to get socket address */
                LOG("getsockname: %d %s\n", errno, strerror(errno));
                session->pasvSocket.Close();
                session->SendResponse(451, "\r\n");
                return;
            }
        }
#endif

        /* we are now listening on the socket */
        LOG("listening on %s:%u\n",
            inet_ntoa(session->pasv_addr.sin_addr),
            ntohs(session->pasv_addr.sin_port));
        session->flags |= SESSION_PASV;

        /* print the address in the ftp format */
        port = ntohs(session->pasv_addr.sin_port);
        strcpy(buffer, inet_ntoa(session->pasv_addr.sin_addr));
        sprintf(buffer + strlen(buffer), ",%u,%u",
                port >> 8, port & 0xFF);
        for (p = buffer; *p; ++p) {
            if (*p == '.')
                *p = ',';
        }

        session->SendResponse(227, "%s\r\n", buffer);
    }

    /*! @fn static void PORT(ftp_session_t *session, const char *args)
     *
     *  @brief provide an address for the server to connect to
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void PORT(FTPSession *session, const char *args) {
        char *addrstr, *p, *portstr;
        int commas = 0, rc;
        short port = 0;
        unsigned long val;
        struct sockaddr_in addr;

        LOG_DEBUG(args);

        /* reset the state */
        session->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
        session->flags &= ~(SESSION_PASV | SESSION_PORT);

        /* dup the args since they are const and we need to change it */
        addrstr = strdup(args);
        if (addrstr == NULL) {
            session->SendResponse(425, "%s\r\n", strerror(ENOMEM));
            return;
        }

        /* replace a,b,c,d,e,f with a.b.c.d\0e.f */
        for (p = addrstr; *p; ++p) {
            if (*p == ',') {
                if (commas != 3)
                    *p = '.';
                else {
                    *p = 0;
                    portstr = p + 1;
                }
                ++commas;
            }
        }

        /* make sure we got the right number of values */
        if (commas != 5) {
            free(addrstr);
            session->SendResponse(501, "%s\r\n", strerror(EINVAL));
            return;
        }

        /* parse the address */
        rc = inet_aton(addrstr, &addr.sin_addr);
        if (rc == 0) {
            free(addrstr);
            session->SendResponse(501, "%s\r\n", strerror(EINVAL));
            return;
        }

        /* parse the port */
        val = 0;
        port = 0;
        for (p = portstr; *p; ++p) {
            if (!isdigit((int)*p)) {
                if (p == portstr || *p != '.' || val > 0xFF) {
                    free(addrstr);
                    session->SendResponse(501, "%s\r\n", strerror(EINVAL));
                    return;
                }
                port <<= 8;
                port += val;
                val = 0;
            } else {
                val *= 10;
                val += *p - '0';
            }
        }

        /* validate the port */
        if (val > 0xFF || port > 0xFF) {
            free(addrstr);
            session->SendResponse(501, "%s\r\n", strerror(EINVAL));
            return;
        }
        port <<= 8;
        port += val;

        /* fill in the address port and family */
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        free(addrstr);

        memcpy(&session->peer_addr, &addr, sizeof(addr));

        /* we are ready to connect to the client */
        session->flags |= SESSION_PORT;
        session->SendResponse(200, "OK\r\n");
    }

    /*! @fn static void PWD(ftp_session_t *session, const char *args)
     *
     *  @brief print working directory
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void PWD(FTPSession *session, const char *args) {
        static char buffer[CMD_BUFFERSIZE];
        size_t len = sizeof(buffer), i;
        char *path;

        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        /* encode the cwd */
        len = strlen(session->cwd);
        path = encode_path(session->cwd, &len, true);
        if (path != NULL) {
            i = sprintf(buffer, "257 \"");
            if (i + len + 3 > sizeof(buffer)) {
                /* buffer will overflow */
                free(path);
                session->SetState(COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
                session->SendResponse(550, "unavailable\r\n");
                session->SendResponse(425, "%s\r\n", strerror(EOVERFLOW));
                return;
            }
            memcpy(buffer + i, path, len);
            free(path);
            len += i;
            buffer[len++] = '"';
            buffer[len++] = '\r';
            buffer[len++] = '\n';

            session->SendResponseBuffer(buffer, len);
            return;
        }

        session->SendResponse(425, "%s\r\n", strerror(ENOMEM));
    }

    /*! @fn static void QUIT(ftp_session_t *session, const char *args)
     *
     *  @brief terminate ftp session
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void QUIT(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        /* disconnect from the client */
        session->SendResponse(221, "disconnecting\r\n");
        session->cmdSocket.Close();
    }

    /*! @fn static void REST(ftp_session_t *session, const char *args)
     *
     *  @brief restart a transfer
     *
     *  @note sets file position for a subsequent STOR operation
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void REST(FTPSession *session, const char *args) {
        const char *p;
        uint64_t pos = 0;

        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        /* make sure an argument is provided */
        if (args == NULL) {
            session->SendResponse(504, "invalid argument\r\n");
            return;
        }

        /* parse the offset */
        for (p = args; *p; ++p) {
            if (!isdigit((int)*p)) {
                session->SendResponse(504, "invalid argument\r\n");
                return;
            }

            if (UINT64_MAX / 10 < pos) {
                session->SendResponse(504, "invalid argument\r\n");
                return;
            }

            pos *= 10;

            if (UINT64_MAX - (*p - '0') < pos) {
                session->SendResponse(504, "invalid argument\r\n");
                return;
            }

            pos += (*p - '0');
        }

        /* set the restart offset */
        session->filepos = pos;
        session->SendResponse(200, "OK\r\n");
    }

    /*! @fn static void RETR(ftp_session_t *session, const char *args)
     *
     *  @brief retrieve a file
     *
     *  @note Requires a PASV or PORT connection
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void RETR(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        /* open the file to retrieve */
        session->XferFile(args, XFER_FILE_RETR);
    }

    /*! @fn static void RMD(ftp_session_t *session, const char *args)
     *
     *  @brief remove a directory
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void RMD(FTPSession *session, const char *args) {
        int rc;

        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        /* build the path to remove */
        if (session->BuildPath(session->cwd, args) != 0) {
            session->SendResponse(553, "%s\r\n", strerror(errno));
            return;
        }

        /* remove the directory */
        rc = rmdir(session->buffer);
        if (rc != 0) {
            /* rmdir error */
            LOG("rmdir: %d %s\n", errno, strerror(errno));
            session->SendResponse(550, "failed to delete directory\r\n");
            return;
        }

        UpdateFreeSpace(session->m_sdmcFs.get());
        session->SendResponse(250, "OK\r\n");
    }

    /*! @fn static void RNFR(ftp_session_t *session, const char *args)
     *
     *  @brief rename from
     *
     *  @note Must be followed by RNTO
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void RNFR(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        /* build the path to rename from */
        if (session->BuildPath(session->cwd, args) != 0) {
            session->SendResponse(553, "%s\r\n", strerror(errno));
            return;
        }

        /* make sure the path exists */
        FsDirEntryType type;
        Result rc = session->m_sdmcFs->GetEntryType(session->buffer, &type);
        if (R_FAILED(rc)) {
            /* error getting path status */
            LOG("GetEntryType: %d\n", rc);
            session->SendResponse(450, "no such file or directory\r\n");
            return;
        }
        session->rn_type = type;

        /* we are ready for RNTO */
        session->flags |= SESSION_RENAME;
        session->SendResponse(350, "OK\r\n");
    }

    /*! @fn static void RNTO(ftp_session_t *session, const char *args)
     *
     *  @brief rename to
     *
     *  @note Must be preceded by RNFR
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void RNTO(FTPSession *session, const char *args) {
        static char rnfr[FS_MAX_PATH]; // rename-from buffer

        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        /* make sure the previous command was RNFR */
        if (!(session->flags & SESSION_RENAME)) {
            session->SendResponse(503, "Bad sequence of commands\r\n");
            return;
        }

        /* clear the rename state */
        session->flags &= ~SESSION_RENAME;

        /* copy the RNFR path */
        std::memcpy(rnfr, session->buffer, FS_MAX_PATH);

        /* build the path to rename to */
        if (session->BuildPath(session->cwd, args) != 0) {
            session->SendResponse(554, "%s\r\n", strerror(errno));
            return;
        }

        /* rename the file */
        Result rc = ResultSuccess();
        if (session->rn_type == FsDirEntryType_Dir) {
            rc = session->m_sdmcFs->RenameDirectory(rnfr, session->buffer);
        } else if (session->rn_type == FsDirEntryType_File) {
            rc = session->m_sdmcFs->RenameFile(rnfr, session->buffer);
        }
        if (R_FAILED(rc)) {
            /* rename failure */
            LOG("Rename*: 0x%x\n", rc);
            session->SendResponse(550, "failed to rename file/directory\r\n");
            return;
        }

        /* Won't change anyway... */
        UpdateFreeSpace(session->m_sdmcFs.get());
        session->SendResponse(250, "OK\r\n");
    }

    /*! @fn static void SIZE(ftp_session_t *session, const char *args)
     *
     *  @brief get file size
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void SIZE(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        /* build the path to stat */
        if (session->BuildPath(session->cwd, args) != 0) {
            session->SendResponse(553, "%s\r\n", strerror(errno));
            return;
        }

        s64 size;
        std::unique_ptr<IFile> file;
        Result rc = session->m_sdmcFs->OpenFile(session->buffer, FsOpenMode_Read, &file);
        if (R_SUCCEEDED(rc)) {
            rc = file->GetSize(&size);
            file->Close();
        }
        if (R_FAILED(rc)) {
            session->SendResponse(550, "Could not get file size.\r\n");
            return;
        }

        session->SendResponse(213, "%" PRIu64 "\r\n", static_cast<u64>(size));
    }

    /*! @fn static void STAT(ftp_session_t *session, const char *args)
     *
     *  @brief get status
     *
     *  @note If no argument is supplied, and a transfer is occurring, get the
     *        current transfer status. If no argument is supplied, and no transfer
     *        is occurring, get the server status. If an argument is supplied, this
     *        is equivalent to LIST, except the data is sent over the command
     *        socket.
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void STAT(FTPSession *session, const char *args) {
        u64 cur_time;
        timeGetCurrentTime(TimeType_Default, &cur_time);
        u64 uptime = cur_time - hos::time::GetStart();
        u64 hours = uptime / 3600;
        u64 minutes = (uptime / 60) % 60;
        u64 seconds = uptime % 60;

        LOG_DEBUG(args);

        if (session->state == DATA_CONNECT_STATE) {
            /* we are waiting to connect to the client */
            session->SendResponse(-211, "FTP server status\r\n"
                                        " Waiting for data connection\r\n"
                                        "211 End\r\n");
            return;
        } else if (session->state == DATA_TRANSFER_STATE) {
            /* we are in the middle of a transfer */
            session->SendResponse(-211, "FTP server status\r\n"
                                        " Transferred %" PRIu64 " bytes\r\n"
                                        "211 End\r\n",
                                  session->filepos);
            return;
        }

        if (strlen(args) == 0) {
            /* no argument provided, send the server status */
            session->SendResponse(-211, "FTP server status\r\n"
                                        " Uptime: %02ld:%02ld:%02ld\r\n"
                                        "211 End\r\n",
                                  hours, minutes, seconds);
            return;
        }

        /* argument provided, open the path in STAT mode */
        session->XferDir(args, XFER_DIR_STAT, false);
    }

    /*! @fn static void STOR(ftp_session_t *session, const char *args)
     *
     *  @brief store a file
     *
     *  @note Requires a PASV or PORT connection
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void STOR(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        /* open the file to store */
        session->XferFile(args, XFER_FILE_STOR);
    }

    /*! @fn static void STOU(ftp_session_t *session, const char *args)
     *
     *  @brief store a unique file
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void STOU(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        /* we do not support this yet */
        session->SetState(COMMAND_STATE, 0);

        session->SendResponse(502, "unavailable\r\n");
    }

    /*! @fn static void STRU(ftp_session_t *session, const char *args)
     *
     *  @brief set file structure
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void STRU(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        /* we only support F (no structure) mode */
        if (strcasecmp(args, "F") == 0) {
            session->SendResponse(200, "OK\r\n");
            return;
        }

        session->SendResponse(504, "unavailable\r\n");
    }

    /*! @fn static void SYST(ftp_session_t *session, const char *args)
     *
     *  @brief identify system
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void SYST(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        /* we are UNIX compliant with 8-bit characters */
        session->SendResponse(215, "UNIX Type: L8\r\n");
    }

    /*! @fn static void TYPE(ftp_session_t *session, const char *args)
     *
     *  @brief set transfer mode
     *
     *  @note transfer mode is always binary
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void TYPE(FTPSession *session, const char *args) {
        LOG_DEBUG(args);

        session->SetState(COMMAND_STATE, 0);

        /* we always transfer in binary mode */
        session->SendResponse(200, "OK\r\n");
    }

    /*! @fn static void USER(ftp_session_t *session, const char *args)
     *
     *  @brief provide user name
     *
     *  @param[in] session ftp session
     *  @param[in] args    arguments
     */
    void USER(FTPSession *session, const char *args) {
        LOG_DEBUG(args);
        session->user_ok = true;
        session->pass_ok = true;
        session->SetState(COMMAND_STATE, 0);
        session->SendResponse(230, "OK\r\n");
        /* TODO: actually authenticate */
    }

}
