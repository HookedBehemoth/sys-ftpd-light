#pragma once
#include <switch.h>

namespace hos::time {

    Result Initialize();
    u64 GetStart();

    Result TimestampToCalendarTime(TimeCalendarTime *datetime, u64 timestamp);
    Result DateTimeToTimestamp(u64 *timestamp, TimeCalendarTime datetime);

}
