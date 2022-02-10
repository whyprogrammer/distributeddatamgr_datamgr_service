/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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

#define LOG_TAG "KvStoreSyncCallbackClient"

#include <memory>
#include <mutex>
#include "log_print.h"
#include "kvstore_sync_callback_client.h"

namespace OHOS {
namespace DistributedKv {
std::map<std::string, std::shared_ptr<KvStoreSyncCallback>> KvStoreSyncCallbackClient::kvStoreSyncCallbackInfo_;
const std::string KvStoreSyncCallbackClient::CommonSyncCallbackLabel("CommonSyncCallbackLabel");

KvStoreSyncCallbackClient::KvStoreSyncCallbackClient() = default;

KvStoreSyncCallbackClient::KvStoreSyncCallbackClient(std::shared_ptr<KvStoreSyncCallback> kvStoreSyncCallback)
{
    if (kvStoreSyncCallbackInfo_.find(CommonSyncCallbackLabel) == kvStoreSyncCallbackInfo_.end()) {
        AddKvStoreSyncCallback(std::move(kvStoreSyncCallback), CommonSyncCallbackLabel);
    }
}

KvStoreSyncCallbackClient::~KvStoreSyncCallbackClient() = default;

void KvStoreSyncCallbackClient::SyncCompleted(const std::map<std::string, Status> &results, const std::string &label)
{
    if (label.empty() && kvStoreSyncCallbackInfo_.find(CommonSyncCallbackLabel) != kvStoreSyncCallbackInfo_.end()) {
        kvStoreSyncCallbackInfo_[CommonSyncCallbackLabel]->SyncCompleted(results);
    } else if (kvStoreSyncCallbackInfo_.find(label) != kvStoreSyncCallbackInfo_.end()) {
        ZLOGI("label = %{public}s", label.c_str());
        kvStoreSyncCallbackInfo_[label]->SyncCompleted(results);
    }
}

void KvStoreSyncCallbackClient::AddKvStoreSyncCallback(const std::shared_ptr<KvStoreSyncCallback> kvStoreSyncCallback,
                                                       const std::string &label)
{
    std::mutex mtx;
    if(kvStoreSyncCallbackInfo_.find(label) == kvStoreSyncCallbackInfo_.end()) {
        mtx.lock();
        kvStoreSyncCallbackInfo_.insert({label, kvStoreSyncCallback});
        mtx.unlock();
    }
}

std::shared_ptr<KvStoreSyncCallback> KvStoreSyncCallbackClient::GetCommonSyncCallback()
{
    if (kvStoreSyncCallbackInfo_.find(CommonSyncCallbackLabel) != kvStoreSyncCallbackInfo_.end()) {
        return kvStoreSyncCallbackInfo_[CommonSyncCallbackLabel];
    } else {
        return nullptr;
    }
}

std::string KvStoreSyncCallbackClient::GetCommonSyncCallbackLabel()
{
    return CommonSyncCallbackLabel;
}
}  // namespace DistributedKv
}  // namespace OHOS
