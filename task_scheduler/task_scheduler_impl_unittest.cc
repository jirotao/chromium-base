// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task_scheduler/task_scheduler_impl.h"

#include <stddef.h>

#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/synchronization/waitable_event.h"
#include "base/task_scheduler/task_traits.h"
#include "base/task_scheduler/test_task_factory.h"
#include "base/threading/platform_thread.h"
#include "base/threading/simple_thread.h"
#include "base/threading/thread.h"
#include "base/threading/thread_restrictions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace internal {

namespace {

struct TraitsExecutionModePair {
  TraitsExecutionModePair(const TaskTraits& traits,
                          ExecutionMode execution_mode)
      : traits(traits), execution_mode(execution_mode) {}

  TaskTraits traits;
  ExecutionMode execution_mode;
};

class TaskSchedulerImplTest
    : public testing::TestWithParam<TraitsExecutionModePair> {
 protected:
  TaskSchedulerImplTest() = default;

  void SetUp() override {
    scheduler_ = TaskSchedulerImpl::Create();
    EXPECT_TRUE(scheduler_);
  }
  void TearDown() override { scheduler_->JoinForTesting(); }

  std::unique_ptr<TaskSchedulerImpl> scheduler_;

 private:
  DISALLOW_COPY_AND_ASSIGN(TaskSchedulerImplTest);
};

#if ENABLE_THREAD_RESTRICTIONS
// Returns whether I/O calls are allowed on the current thread.
bool GetIOAllowed() {
  const bool previous_value = ThreadRestrictions::SetIOAllowed(true);
  ThreadRestrictions::SetIOAllowed(previous_value);
  return previous_value;
}
#endif

// Verify that the current thread priority and I/O restrictions are appropriate
// to run a Task with |traits|.
// Note: ExecutionMode is verified inside TestTaskFactory.
void VerifyTaskEnvironement(const TaskTraits& traits) {
  EXPECT_EQ(traits.priority() == TaskPriority::BACKGROUND
                ? ThreadPriority::BACKGROUND
                : ThreadPriority::NORMAL,
            PlatformThread::GetCurrentThreadPriority());

#if ENABLE_THREAD_RESTRICTIONS
  // The #if above is required because GetIOAllowed() always returns true when
  // !ENABLE_THREAD_RESTRICTIONS, even when |traits| don't allow file I/O.
  EXPECT_EQ(traits.with_file_io(), GetIOAllowed());
#endif
}

void VerifyTaskEnvironementAndSignalEvent(const TaskTraits& traits,
                                          WaitableEvent* event) {
  DCHECK(event);
  VerifyTaskEnvironement(traits);
  event->Signal();
}

class ThreadPostingTasks : public SimpleThread {
 public:
  // Creates a thread that posts Tasks to |scheduler| with |traits| and
  // |execution_mode|.
  ThreadPostingTasks(TaskSchedulerImpl* scheduler,
                     const TaskTraits& traits,
                     ExecutionMode execution_mode)
      : SimpleThread("ThreadPostingTasks"),
        traits_(traits),
        factory_(scheduler->CreateTaskRunnerWithTraits(traits, execution_mode),
                 execution_mode) {}

  void WaitForAllTasksToRun() { factory_.WaitForAllTasksToRun(); }

 private:
  void Run() override {
    EXPECT_FALSE(factory_.task_runner()->RunsTasksOnCurrentThread());

    const size_t kNumTasksPerThread = 150;
    for (size_t i = 0; i < kNumTasksPerThread; ++i) {
      factory_.PostTask(test::TestTaskFactory::PostNestedTask::NO,
                        Bind(&VerifyTaskEnvironement, traits_));
    }
  }

  const TaskTraits traits_;
  test::TestTaskFactory factory_;

  DISALLOW_COPY_AND_ASSIGN(ThreadPostingTasks);
};

// Returns a vector with a TraitsExecutionModePair for each valid
// combination of {ExecutionMode, TaskPriority, WithFileIO()}.
std::vector<TraitsExecutionModePair> GetTraitsExecutionModePairs() {
  std::vector<TraitsExecutionModePair> params;

  const ExecutionMode execution_modes[] = {ExecutionMode::PARALLEL,
                                           ExecutionMode::SEQUENCED,
                                           ExecutionMode::SINGLE_THREADED};

  for (ExecutionMode execution_mode : execution_modes) {
    for (size_t priority_index = static_cast<size_t>(TaskPriority::LOWEST);
         priority_index <= static_cast<size_t>(TaskPriority::HIGHEST);
         ++priority_index) {
      const TaskPriority priority = static_cast<TaskPriority>(priority_index);
      params.push_back(TraitsExecutionModePair(
          TaskTraits().WithPriority(priority), execution_mode));
      params.push_back(TraitsExecutionModePair(
          TaskTraits().WithPriority(priority).WithFileIO(), execution_mode));
    }
  }

  return params;
}

}  // namespace

// Verifies that a Task posted via PostTaskWithTraits with parameterized
// TaskTraits runs on a thread with the expected priority and I/O restrictions.
// The ExecutionMode parameter is ignored by this test.
TEST_P(TaskSchedulerImplTest, PostTaskWithTraits) {
  WaitableEvent task_ran(true, false);
  scheduler_->PostTaskWithTraits(
      FROM_HERE, GetParam().traits,
      Bind(&VerifyTaskEnvironementAndSignalEvent, GetParam().traits,
           Unretained(&task_ran)));
  task_ran.Wait();
}

// Verifies that Tasks posted via a TaskRunner with parameterized TaskTraits and
// ExecutionMode run on a thread with the expected priority and I/O restrictions
// and respect the characteristics of their ExecutionMode.
TEST_P(TaskSchedulerImplTest, PostTasksViaTaskRunner) {
  test::TestTaskFactory factory(
      scheduler_->CreateTaskRunnerWithTraits(GetParam().traits,
                                             GetParam().execution_mode),
      GetParam().execution_mode);
  EXPECT_FALSE(factory.task_runner()->RunsTasksOnCurrentThread());

  const size_t kNumTasksPerTest = 150;
  for (size_t i = 0; i < kNumTasksPerTest; ++i) {
    factory.PostTask(test::TestTaskFactory::PostNestedTask::NO,
                     Bind(&VerifyTaskEnvironement, GetParam().traits));
  }

  factory.WaitForAllTasksToRun();
}

INSTANTIATE_TEST_CASE_P(OneTraitsExecutionModePair,
                        TaskSchedulerImplTest,
                        ::testing::ValuesIn(GetTraitsExecutionModePairs()));

// Spawns threads that simultaneously post Tasks to TaskRunners with various
// TaskTraits and ExecutionModes. Verifies that each Task runs on a thread with
// the expected priority and I/O restrictions and respects the characteristics
// of its ExecutionMode.
TEST(TaskSchedulerImplTest, MultipleTraitsExecutionModePairs) {
  std::unique_ptr<TaskSchedulerImpl> scheduler = TaskSchedulerImpl::Create();

  std::vector<std::unique_ptr<ThreadPostingTasks>> threads_posting_tasks;
  for (const auto& traits_execution_mode_pair : GetTraitsExecutionModePairs()) {
    threads_posting_tasks.push_back(WrapUnique(new ThreadPostingTasks(
        scheduler.get(), traits_execution_mode_pair.traits,
        traits_execution_mode_pair.execution_mode)));
    threads_posting_tasks.back()->Start();
  }

  for (const auto& thread : threads_posting_tasks) {
    thread->WaitForAllTasksToRun();
    thread->Join();
  }

  scheduler->JoinForTesting();
}

// TODO(fdoray): Add tests with Sequences that move around thread pools once
// child TaskRunners are supported.

}  // namespace internal
}  // namespace base