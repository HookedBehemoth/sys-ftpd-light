#pragma once
#include "types.hpp"
#include "../fs.hpp"

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

Result UpdateFreeSpace(IFileSystem *fs);
