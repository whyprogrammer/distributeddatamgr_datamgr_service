/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#ifndef DATASHARESERVICE_SUBSCRIBE_STRAGETY_H
#define DATASHARESERVICE_SUBSCRIBE_STRAGETY_H

#include <shared_mutex>
#include "seq_strategy.h"
#include "data_proxy_observer.h"

namespace OHOS::DataShare {
class SubscribeStrategy final {
public:
    static int32_t Execute(std::shared_ptr<Context> context, std::function<bool()> process);

private:
    static Strategy *GetStrategy();
};
} // namespace OHOS::DataShare
#endif