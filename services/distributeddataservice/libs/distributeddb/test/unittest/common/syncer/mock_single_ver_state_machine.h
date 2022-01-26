/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MOCK_SINGLE_VER_STATE_MACHINE_H
#define MOCK_SINGLE_VER_STATE_MACHINE_H

#include <gmock/gmock.h>
#include "single_ver_sync_state_machine.h"

namespace DistributedDB {
class MockSingleVerStateMachine : public SingleVerSyncStateMachine {
public:
    void CallStepToTimeout(TimerId timerId)
    {
        SingleVerSyncStateMachine::StepToTimeout(timerId);
    }

    int CallExecNextTask()
    {
        return SyncStateMachine::ExecNextTask();
    }

    MOCK_METHOD1(SwitchStateAndStep, void(uint8_t));

    MOCK_METHOD0(PrepareNextSyncTask, int(void));
};
} // namespace DistributedDB
#endif  // #define MOCK_SINGLE_VER_STATE_MACHINE_H