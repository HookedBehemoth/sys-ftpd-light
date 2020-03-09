#include "time.hpp"
#include "../common.hpp"

namespace hos::time {

    namespace {
        TimeZoneRule rule;
    }

    Result Initialize() {
        TimeLocationName locName;
        R_TRY(timeGetDeviceLocationName(&locName));

        R_TRY(timeLoadTimeZoneRule(&locName, &rule));

        return ResultSuccess();
    }

    Result TimestampToCalendarTime(TimeCalendarTime *datetime, u64 timestamp) {
        TimeCalendarAdditionalInfo info;
        return timeToCalendarTime(&rule, timestamp, datetime, &info);
    }

    Result DateTimeToTimestamp(u64 *timestamp, TimeCalendarTime datetime) {
        s32 count;
        return timeToPosixTime(&rule, &datetime, timestamp, 1, &count);
    }

}
