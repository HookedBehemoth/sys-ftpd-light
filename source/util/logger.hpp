#pragma once
#include "../fs/IFileSystem.hpp"

Result InitializeLog(std::shared_ptr<IFileSystem> &sdmcFs);
Result Log(const char *path, int line, const char *func, const char *fmt, ...) __attribute__((format(printf, 4, 5)));

#define LOG(format, ...) Log(__FILE__, __LINE__, __FUNCTION__, format, ##__VA_ARGS__);
#define LOG_DEBUG(format, ...) LOG(format, ##__VA_ARGS__);

/// Evaluates an expression that returns a result, and returns and logs the result if it would fail.
#define R_TRY_LOG(res_expr)                             \
    ({                                                  \
        const auto _tmp_r_try_rc = (res_expr);          \
        if (R_FAILED(_tmp_r_try_rc)) {                  \
            LOG("failed with rc: 0x%x", _tmp_r_try_rc); \
            return _tmp_r_try_rc;                       \
        }                                               \
    })

/// Evaluates an expression that returns a result, and logs the result if it would fail.
#define R_LOG(res_expr)                                 \
    ({                                                  \
        const auto _tmp_r_try_rc = (res_expr);          \
        if (R_FAILED(_tmp_r_try_rc)) {                  \
            LOG("failed with rc: 0x%x", _tmp_r_try_rc); \
        }                                               \
    })
