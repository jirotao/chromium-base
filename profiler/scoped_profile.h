// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef BASE_PROFILER_SCOPED_PROFILE_H_
#define BASE_PROFILER_SCOPED_PROFILE_H_

//------------------------------------------------------------------------------
// ScopedProfile provides basic helper functions for profiling a short
// region of code within a scope.  It is separate from the related ThreadData
// class so that it can be included without much other cruft, and provide the
// macros listed below.

#include "base/base_export.h"
#include "base/location.h"
#include "base/profiler/tracked_time.h"

#define TRACK_RUN_IN_THIS_SCOPED_REGION_FOR_OFFICIAL_BUILDS(variable_name) \
    ::tracked_objects::ScopedProfile variable_name(FROM_HERE)

#define TRACK_RUN_IN_IPC_HANDLER(dispatch_function_name) \
    ::tracked_objects::ScopedProfile some_tracking_variable_name(  \
        FROM_HERE_WITH_EXPLICIT_FUNCTION(#dispatch_function_name))


namespace tracked_objects {
class Births;

class BASE_EXPORT ScopedProfile {
 public:
  explicit ScopedProfile(const Location& location);
  ~ScopedProfile();

  // Stop tracing prior to the end destruction of the instance.
  void StopClockAndTally();

 private:
  Births* birth_;  // Place in code where tracking started.
  const TrackedTime start_of_run_;

  DISALLOW_COPY_AND_ASSIGN(ScopedProfile);
};

}  // namespace tracked_objects

#endif   // BASE_PROFILER_SCOPED_PROFILE_H_
