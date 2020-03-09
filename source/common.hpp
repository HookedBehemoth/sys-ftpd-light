#pragma once
#include <functional>
#include <switch.h>

/// Evaluates an expression that returns a result, and returns the result if it would fail.
#define R_TRY(res_expr)                        \
    ({                                         \
        const auto _tmp_r_try_rc = (res_expr); \
        if (R_FAILED(_tmp_r_try_rc)) {         \
            return _tmp_r_try_rc;              \
        }                                      \
    })

/// Evaluates an expression that returns a result, and fatals if it would fail.
#define R_ASSERT(res_expr)                      \
    ({                                          \
        const auto _tmp_r_assert_rc = res_expr; \
        if (R_FAILED(_tmp_r_assert_rc))         \
            fatalThrow(_tmp_r_assert_rc);       \
    })

/// Evaluates a boolean expression, and returns a result unless that expression is true.
#define R_UNLESS(expr, res) \
    ({                      \
        if (!(expr)) {      \
            return res;     \
        }                   \
    })

#define R_DEFINE_ERROR_RESULT(name, desc) \
    constexpr inline Result Result##name() { return desc; }

namespace sm {

    static inline void DoWithSmSession(std::function<void()> f) {
        R_ASSERT(smInitialize());
        f();
        smExit();
    }

}

R_DEFINE_ERROR_RESULT(Success, 0);
