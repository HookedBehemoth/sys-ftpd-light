#pragma once
#include <switch.h>

namespace hos::time {

    Result Initialize();

    Result TimestampToCalendarTime(TimeCalendarTime *datetime, u64 timestamp);
    Result DateTimeToTimestamp(u64 *timestamp, TimeCalendarTime datetime);

}
