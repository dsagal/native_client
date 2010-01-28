/*
 * Copyright 2008 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can
 * be found in the LICENSE file.
 */


#include <sys/time.h>
#include <time.h>

#include "native_client/src/include/portability.h"
#include "base/basictypes.h"

#include "native_client/src/shared/platform/time.h"

// The Time routines in this file use standard POSIX routines, or almost-
// standard routines in the case of timegm.
// The TimeTicks routines are Mach-specific.

// Time -----------------------------------------------------------------------

// The internal representation of Time uses time_t directly, so there is no
// offset.  The epoch is 1970-01-01 00:00:00 UTC.
// static
const int64 NaCl::Time::kTimeTToMicrosecondsOffset = GG_INT64_C(0);

// static
int64 NaCl::Time::CurrentWallclockMicroseconds() {
  struct timeval tv;
  struct timezone tz = { 0, 0 };  // UTC
  if (gettimeofday(&tv, &tz) != 0) {
    // DCHECK(0) << "Could not determine time of day";
  }
  // Combine seconds and microseconds in a 64-bit field containing microseconds
  // since the epoch.  That's enough for nearly 600 centuries.
  return tv.tv_sec * GG_UINT64_C(1000000) + tv.tv_usec;
}

// static
NaCl::Time NaCl::Time::FromExploded(bool is_local, const Exploded& exploded) {
  struct tm timestruct;
  timestruct.tm_sec    = exploded.second;
  timestruct.tm_min    = exploded.minute;
  timestruct.tm_hour   = exploded.hour;
  timestruct.tm_mday   = exploded.day_of_month;
  timestruct.tm_mon    = exploded.month - 1;
  timestruct.tm_year   = exploded.year - 1900;
  timestruct.tm_wday   = exploded.day_of_week;  // mktime/timegm ignore this
  timestruct.tm_yday   = 0;     // mktime/timegm ignore this
  timestruct.tm_isdst  = -1;    // attempt to figure it out
  timestruct.tm_gmtoff = 0;     // not a POSIX field, so mktime/timegm ignore
  timestruct.tm_zone   = NULL;  // not a POSIX field, so mktime/timegm ignore

  time_t seconds;
  if (is_local)
    seconds = mktime(&timestruct);
  else
    seconds = timegm(&timestruct);
  // DCHECK(seconds >= 0) << "mktime/timegm could not convert from exploded";

  uint64 milliseconds = seconds * kMillisecondsPerSecond + exploded.millisecond;
  return Time(milliseconds * kMicrosecondsPerMillisecond);
}

void NaCl::Time::Explode(bool is_local, Exploded* exploded) const {
  // Time stores times with microsecond resolution, but Exploded only carries
  // millisecond resolution, so begin by being lossy.
  uint64 milliseconds = us_ / kMicrosecondsPerMillisecond;
  time_t seconds = milliseconds / kMillisecondsPerSecond;

  struct tm timestruct;
  if (is_local)
    localtime_r(&seconds, &timestruct);
  else
    gmtime_r(&seconds, &timestruct);

  exploded->year         = timestruct.tm_year + 1900;
  exploded->month        = timestruct.tm_mon + 1;
  exploded->day_of_week  = timestruct.tm_wday;
  exploded->day_of_month = timestruct.tm_mday;
  exploded->hour         = timestruct.tm_hour;
  exploded->minute       = timestruct.tm_min;
  exploded->second       = timestruct.tm_sec;
  exploded->millisecond  = milliseconds % kMillisecondsPerSecond;
}

// TimeTicks ------------------------------------------------------------------

// static
NaCl::TimeTicks NaCl::TimeTicks::Now() {
  struct timeval tv;
  struct timezone tz = { 0, 0 };  // UTC
  if (gettimeofday(&tv, &tz) != 0) {
    // DCHECK(0) << "Could not determine time of day";
    return TimeTicks(0);
  }
  // Combine seconds and microseconds in a 64-bit field containing microseconds
  // since the epoch.  That's enough for nearly 600 centuries.
  return TimeTicks(tv.tv_sec * GG_UINT64_C(1000000) + tv.tv_usec);
}

// static
NaCl::TimeTicks NaCl::TimeTicks::UnreliableHighResNow() {
  return Now();
}

void NaCl::TimeTicks::InitTimespec(struct timespec *ts) const {
  // from microseconds into seconds
  ts->tv_sec = ticks_ / GG_UINT64_C(1000000);
  // microseconds into nano
  ts->tv_nsec = (ticks_ - ts->tv_sec * GG_UINT64_C(1000000)) * 1000;
}
