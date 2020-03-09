#include "utl.hpp"

#include "../util/logger.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>

Socket::Socket()
    : fd(-1), connected(false) {}

/*! set a socket to non-blocking
 *
 *  @returns error
 */
Result Socket::SetNonBlocking() {
    int rc, flags;

    /* get the socket flags */
    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        LOG("fcntl: %d %s", errno, strerror(errno));
        return ftp::ResultFdManipulationFailed();
    }

    /* add O_NONBLOCK to the socket flags */
    rc = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (rc != 0) {
        LOG("fcntl: %d %s", errno, strerror(errno));
        return ftp::ResultFdManipulationFailed();
    }

    return ResultSuccess();
}

/*! close a socket
 *
 *  @returns error
 */
Result Socket::Close() {
    if (fd < 0)
        return ResultSuccess();

    int rc;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    struct pollfd pollinfo;

    if (connected) {
        /* get peer address and print */
        rc = getpeername(fd, (struct sockaddr *)&addr, &addrlen);
        if (R_FAILED(rc)) {
            LOG("getpeername: %d %s", errno, strerror(errno));
            LOG("closing connection to fd=%d", fd);
        } else {
            LOG("closing connection to %s:%u", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
        }

        /* shutdown connection */
        rc = shutdown(fd, SHUT_WR);
        if (R_FAILED(rc))
            LOG("shutdown %d %s", errno, strerror(errno));

        /* wait for client to close connection */
        pollinfo.fd = fd;
        pollinfo.events = POLLIN;
        pollinfo.revents = 0;
        rc = poll(&pollinfo, 1, 250);
        if (rc < 0)
            LOG("poll: %d %s", errno, strerror(errno));
    }

    /* set linger to 0 */
    struct linger linger;
    linger.l_onoff = 1;
    linger.l_linger = 0;
    rc = setsockopt(fd, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger));
    if (rc != 0)
        LOG("setsockopt: SO_LINGER %d %s", errno, strerror(errno));

    /* close socket */
    rc = close(fd);
    if (rc != 0)
        LOG("close: %d %s", errno, strerror(errno));

    this->fd = -1;
    this->connected = false;

    return ResultSuccess();
}

Result Socket::SetOptions() {
    /* increase receive buffer size */
    int rc = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sock_buffersize, sizeof(sock_buffersize));
    if (R_FAILED(rc)) {
        LOG("setsockopt: SO_RCVBUF %d %s\n", errno, strerror(errno));
        return ftp::ResultSetSockOptFailed();
    }

    /* increase send buffer size */
    rc = setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
                    &sock_buffersize, sizeof(sock_buffersize));
    if (rc != 0) {
        LOG("setsockopt: SO_SNDBUF %d %s\n", errno, strerror(errno));
        return ftp::ResultSetSockOptFailed();
    }

    return ResultSuccess();
}

char *encode_path(const char *path, size_t *len, bool quotes) {
    bool enc = false;
    size_t i, diff = 0;
    char *out, *p = (char *)path;

    /* check for \n that needs to be encoded */
    if (memchr(p, '\n', *len) != NULL)
        enc = true;

    if (quotes) {
        /* check for " that needs to be encoded */
        p = (char *)path;
        do {
            p = (char *)memchr(p, '"', path + *len - p);
            if (p != NULL) {
                ++p;
                ++diff;
            }
        } while (p != NULL);
    }

    /* check if an encode was needed */
    if (!enc && diff == 0)
        return strdup(path);

    /* allocate space for encoded path */
    p = out = (char *)malloc(*len + diff);
    if (out == NULL)
        return NULL;

    /* copy the path while performing encoding */
    for (i = 0; i < *len; ++i) {
        if (*path == '\n') {
            /* encoded \n is \0 */
            *p++ = 0;
        } else if (quotes && *path == '"') {
            /* encoded " is "" */
            *p++ = '"';
            *p++ = '"';
        } else
            *p++ = *path;
        ++path;
    }

    *len += diff;
    return out;
}

int validate_path(const char *args) {
    const char *p;

    /* make sure no path components are '..' */
    p = args;
    while ((p = strstr(p, "/..")) != NULL) {
        if (p[3] == 0 || p[3] == '/')
            return -1;
    }

    /* make sure there are no '//' */
    if (strstr(args, "//") != NULL)
        return -1;

    return 0;
}

Result UpdateFreeSpace(IFileSystem *fs) {
#define KiB *(1024.0)
#define MiB *(1024.0 * KiB)
#define GiB *(1024.0 * MiB)
    constexpr static const char pathBuffer[FS_MAX_PATH] = "/";

    s64 freeSpace;
    R_TRY_LOG(fs->GetFreeSpace(pathBuffer, &freeSpace));
    /* TODO: format correctly or just copy the original implementation. */
    LOG("%ld", freeSpace);

    return ResultSuccess();
}
