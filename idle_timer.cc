// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>

#include "base/idle_timer.h"

#include "base/message_loop.h"
#include "base/time.h"

IdleTimerTask::IdleTimerTask(TimeDelta idle_time, bool repeat)
  : idle_interval_(idle_time),
    repeat_(repeat),
    get_last_input_info_fn_(GetLastInputInfo) {
}

IdleTimerTask::~IdleTimerTask() {
  Stop();
}

void IdleTimerTask::Start() {
  DCHECK(!timer_.get());
  StartTimer();
}

void IdleTimerTask::Stop() {
  timer_.reset();
}

void IdleTimerTask::Run() {
  // Verify we can fire the idle timer.
  if (TimeUntilIdle().InMilliseconds() <= 0) {
    OnIdle();
    last_time_fired_ = Time::Now();
  }
  Stop();
  StartTimer();  // Restart the timer for next run.
}

void IdleTimerTask::StartTimer() {
  DCHECK(timer_ == NULL);
  TimeDelta delay = TimeUntilIdle();
  if (delay.InMilliseconds() < 0)
    delay = TimeDelta();
  timer_.reset(new OneShotTimer(delay));
  timer_->set_unowned_task(this);
  timer_->Start();
}

TimeDelta IdleTimerTask::CurrentIdleTime() {
  // TODO(mbelshe): This is windows-specific code.
  LASTINPUTINFO info;
  info.cbSize = sizeof(info);
  if (get_last_input_info_fn_(&info)) {
    // Note: GetLastInputInfo returns a 32bit value which rolls over ~49days.
    int32 last_input_time = info.dwTime;
    int32 current_time = GetTickCount();
    int32 interval = current_time - last_input_time;
    // Interval will go negative if we've been idle for 2GB of ticks.
    if (interval < 0)
      interval = -interval;
    return TimeDelta::FromMilliseconds(interval);
  }
  NOTREACHED();
  return TimeDelta::FromMilliseconds(0);
}

TimeDelta IdleTimerTask::TimeUntilIdle() {
  TimeDelta time_since_last_fire = Time::Now() - last_time_fired_;
  TimeDelta current_idle_time = CurrentIdleTime();
  if (current_idle_time > time_since_last_fire) {
    if (repeat_)
      return idle_interval_ - time_since_last_fire;
    return idle_interval_;
  }
  return idle_interval_ - current_idle_time;
}

