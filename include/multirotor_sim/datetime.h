#pragma once

#include "multirotor_sim/gtime.h"

class GTime;
class DateTime
{
public:

    enum
    {
      SECONDS_IN_WEEK = 604800,
      SECONDS_IN_HALF_WEEK = 302400,
      SECONDS_IN_DAY = 86400,
      SECONDS_IN_HOUR = 3600,
      SECONDS_IN_MINUTE = 60,
      LEAP_SECONDS = 18,
      GPS_UTC_OFFSET_SEC = 315964800,
    };

    int year;
    int month;
    int day;
    int hour;
    int minute;
    double second;

    DateTime();
    DateTime(const GTime& g);
    DateTime& operator= (const GTime& g);
    GTime toGTime() const;
};
