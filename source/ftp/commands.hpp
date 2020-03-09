#pragma once
#include "ftp.hpp"

namespace ftp {

    void ABOR(FTPSession *session, const char *args);
    void ALLO(FTPSession *session, const char *args);
    void APPE(FTPSession *session, const char *args);
    void CDUP(FTPSession *session, const char *args);
    void CWD(FTPSession *session, const char *args);
    void DELE(FTPSession *session, const char *args);
    void FEAT(FTPSession *session, const char *args);
    void HELP(FTPSession *session, const char *args);
    void LIST(FTPSession *session, const char *args);
    void MDTM(FTPSession *session, const char *args);
    void MKD(FTPSession *session, const char *args);
    void MLSD(FTPSession *session, const char *args);
    void MLST(FTPSession *session, const char *args);
    void MODE(FTPSession *session, const char *args);
    void NLST(FTPSession *session, const char *args);
    void NOOP(FTPSession *session, const char *args);
    void OPTS(FTPSession *session, const char *args);
    void PASS(FTPSession *session, const char *args);
    void PASV(FTPSession *session, const char *args);
    void PORT(FTPSession *session, const char *args);
    void PWD(FTPSession *session, const char *args);
    void QUIT(FTPSession *session, const char *args);
    void REST(FTPSession *session, const char *args);
    void RETR(FTPSession *session, const char *args);
    void RMD(FTPSession *session, const char *args);
    void RNFR(FTPSession *session, const char *args);
    void RNTO(FTPSession *session, const char *args);
    void SIZE(FTPSession *session, const char *args);
    void STAT(FTPSession *session, const char *args);
    void STOR(FTPSession *session, const char *args);
    void STOU(FTPSession *session, const char *args);
    void STRU(FTPSession *session, const char *args);
    void SYST(FTPSession *session, const char *args);
    void TYPE(FTPSession *session, const char *args);
    void USER(FTPSession *session, const char *args);

    /*! ftp command descriptor */
    struct Command {
        const char *name;                                   /*!< command name */
        void (*handler)(FTPSession *session, const char *); /*!< command callback */
    };

    constexpr static inline const std::array<Command, 40> commands{{
        {"ABOR", ABOR},
        {"ALLO", ALLO},
        {"APPE", APPE},
        {"CDUP", CDUP},
        {"CWD", CWD},
        {"DELE", DELE},
        {"FEAT", FEAT},
        {"HELP", HELP},
        {"LIST", LIST},
        {"MDTM", MDTM},
        {"MKD", MKD},
        {"MLSD", MLSD},
        {"MLST", MLST},
        {"MODE", MODE},
        {"NLST", NLST},
        {"NOOP", NOOP},
        {"OPTS", OPTS},
        {"PASS", PASS},
        {"PASV", PASV},
        {"PORT", PORT},
        {"PWD", PWD},
        {"QUIT", QUIT},
        {"REST", REST},
        {"RETR", RETR},
        {"RMD", RMD},
        {"RNFR", RNFR},
        {"RNTO", RNTO},
        {"SIZE", SIZE},
        {"STAT", STAT},
        {"STOR", STOR},
        {"STOU", STOU},
        {"STRU", STRU},
        {"SYST", SYST},
        {"TYPE", TYPE},
        {"USER", USER},
        /* Alias */
        {"XCUP", CDUP},
        {"XCWD", CWD},
        {"XMKD", MKD},
        {"XPWD", PWD},
        {"XRMD", RMD},
    }};

}
