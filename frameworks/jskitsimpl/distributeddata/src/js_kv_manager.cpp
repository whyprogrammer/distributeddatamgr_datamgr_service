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
#define LOG_TAG "JS_KVManager"
#include "js_kv_manager.h"
#include "distributed_kv_data_manager.h"
#include "js_device_kv_store.h"
#include "js_single_kv_store.h"
#include "js_util.h"
#include "log_print.h"
#include "napi_queue.h"

using namespace OHOS::DistributedKv;

namespace OHOS::DistributedData {
bool IsStoreTypeSupported(Options options)
{
    return (options.kvStoreType == KvStoreType::DEVICE_COLLABORATION)
        || (options.kvStoreType == KvStoreType::SINGLE_VERSION);
}

JsKVManager::JsKVManager(const std::string& bundleName)
    : bundleName_(bundleName)
{
}

JsKVManager::~JsKVManager()
{
    ZLOGD("no memory leak for JsKVManager");
    std::lock_guard<std::mutex> lck(deathMutex_);
    for (auto& it : deathRecipient_) {
        kvDataManager_.UnRegisterKvStoreServiceDeathRecipient(it);
    }
    deathRecipient_.clear();
}

/*
 * [JS API Prototype]
 * [AsyncCB]  createKVManager(config: KVManagerConfig, callback: AsyncCallback<JsKVManager>): void;
 * [Promise]  createKVManager(config: KVManagerConfig) : Promise<JsKVManager>;
 */
napi_value JsKVManager::CreateKVManager(napi_env env, napi_callback_info info)
{
    ZLOGD("CreateKVManager in");
    struct ContextInfo : public ContextBase {
        JsKVManager* kvManger = nullptr;
        napi_ref ref = nullptr;
    };
    auto ctxt = std::make_shared<ContextInfo>();
    auto input = [env, ctxt](size_t argc, napi_value* argv) {
        // required 1 arguments :: <bundleName>
        ZLOGE_ON_ARGS(ctxt, argc == 1, "invalid arguments!");
        std::string bundleName;
        ctxt->status = JSUtil::GetNamedProperty(env, argv[0], "bundleName", bundleName);
        ZLOGE_ON_ARGS(ctxt, (ctxt->status == napi_ok) && !bundleName.empty(), "invalid bundleName!");

        ctxt->ref = JSUtil::NewWithRef(env, argc, argv, (void**)&ctxt->kvManger, JsKVManager::Constructor(env));
        ZLOGE_ON_ARGS(ctxt, ctxt->kvManger != nullptr, "KVManager::New failed!");
    };
    ctxt->GetCbInfo(env, info, input);

    auto noExecute = NapiAsyncExecute();
    auto output = [env, ctxt](napi_value& result) {
        ctxt->status = napi_get_reference_value(env, ctxt->ref, &result);
        napi_delete_reference(env, ctxt->ref);
        ZLOGE_ON_STATUS(ctxt, "output KVManager failed");
    };
    return NapiQueue::AsyncWork(env, ctxt, std::string(__FUNCTION__), noExecute, output);
}

struct GetKVStoreContext : public ContextBase {
    std::string storeId;
    Options options;
    JsKVStore* kvStore = nullptr;
    napi_ref ref = nullptr;

    void GetCbInfo(napi_env env, napi_callback_info info)
    {
        auto input = [env, this](size_t argc, napi_value* argv) {
            // required 2 arguments :: <storeId> <options>
            ZLOGE_ON_ARGS(this, argc == 2, "invalid arguments!");
            status = JSUtil::GetValue(env, argv[0], storeId);
            ZLOGE_ON_ARGS(this, (status == napi_ok) && !storeId.empty(), "invalid storeId!");
            status = JSUtil::GetValue(env, argv[1], options);
            ZLOGE_ON_STATUS(this, "invalid options!");
            ZLOGE_ON_ARGS(this, IsStoreTypeSupported(options), "invalid options.KvStoreType");
            ZLOGD("GetKVStore kvStoreType=%{public}d", options.kvStoreType);
            if (options.kvStoreType == KvStoreType::DEVICE_COLLABORATION) {
                ref = JSUtil::NewWithRef(env, argc, argv, (void**)&kvStore, JsDeviceKVStore::Constructor(env));
            } else if (options.kvStoreType == KvStoreType::SINGLE_VERSION) {
                ref = JSUtil::NewWithRef(env, argc, argv, (void**)&kvStore, JsSingleKVStore::Constructor(env));
            }
        };
        ContextBase::GetCbInfo(env, info, input);
    }
};

/*
 * [JS API Prototype]
 * [AsyncCallback]
 *      getKVStore<T extends KVStore>(storeId: string, options: Options, callback: AsyncCallback<T>): void;
 * [Promise]
 *      getKVStore<T extends KVStore>(storeId: string, options: Options): Promise<T>;
 */
napi_value JsKVManager::GetKVStore(napi_env env, napi_callback_info info)
{
    ZLOGD("GetKVStore in");
    auto ctxt = std::make_shared<GetKVStoreContext>();
    ctxt->GetCbInfo(env, info);

    auto execute = [ctxt]() {
        auto kvm = reinterpret_cast<JsKVManager*>(ctxt->native);
        ZLOGE_ON_ARGS(ctxt, kvm != nullptr, "KVManager is null, failed!");
        AppId appId = { kvm->bundleName_ };
        StoreId storeId = { ctxt->storeId };
        std::shared_ptr<DistributedKv::SingleKvStore> kvStore;
        Status status = kvm->kvDataManager_.GetSingleKvStore(ctxt->options, appId, storeId, kvStore);
        ZLOGD("GetSingleKvStore return status:%{public}d", status);
        ctxt->status = (status == Status::SUCCESS) ? napi_ok : napi_generic_failure;
        ZLOGE_ON_STATUS(ctxt, "KVManager->GetSingleKvStore() failed!");
        ctxt->kvStore->SetNative(kvStore);
    };
    auto output = [env, ctxt](napi_value& result) {
        ctxt->status = napi_get_reference_value(env, ctxt->ref, &result);
        napi_delete_reference(env, ctxt->ref);
        ZLOGE_ON_STATUS(ctxt, "output KvStore failed");
    };
    return NapiQueue::AsyncWork(env, ctxt, std::string(__FUNCTION__), execute, output);
}

/*
 * [JS API Prototype]
 * [AsyncCB]  closeKVStore(appId: string, storeId: string, kvStore: KVStore, callback: AsyncCallback<void>):void
 * [Promise]  closeKVStore(appId: string, storeId: string, kvStore: KVStore):Promise<void>
 */
napi_value JsKVManager::CloseKVStore(napi_env env, napi_callback_info info)
{
    ZLOGD("CloseKVStore in");
    struct ContextInfo : public ContextBase {
        std::string appId;
        std::string storeId;
        napi_value kvStore;
    };
    auto ctxt = std::make_shared<ContextInfo>();
    auto input = [env, ctxt](size_t argc, napi_value* argv) {
        // required 3 arguments :: <appId> <storeId> <kvStore>
        ZLOGE_ON_ARGS(ctxt, argc == 3, "invalid arguments!");
        ctxt->status = JSUtil::GetValue(env, argv[0], ctxt->appId);
        ZLOGE_ON_ARGS(ctxt, (ctxt->status == napi_ok) && !ctxt->appId.empty(), "invalid appId!");
        ctxt->status = JSUtil::GetValue(env, argv[1], ctxt->storeId);
        ZLOGE_ON_ARGS(ctxt, (ctxt->status == napi_ok) && !ctxt->storeId.empty(), "invalid storeId!");
        ZLOGE_ON_ARGS(ctxt, argv[2] != nullptr, "kvStore is nullptr!");
        bool isSingle = JsKVStore::IsInstanceOf(env, argv[2], ctxt->storeId, JsSingleKVStore::Constructor(env));
        bool isDevice = JsKVStore::IsInstanceOf(env, argv[2], ctxt->storeId, JsDeviceKVStore::Constructor(env));
        ZLOGE_ON_ARGS(ctxt, isSingle || isDevice, "kvStore unmatch to storeId!");
    };
    ctxt->GetCbInfo(env, info, input);

    auto execute = [ctxt]() {
        AppId appId { ctxt->appId };
        StoreId storeId { ctxt->storeId };
        Status status = reinterpret_cast<JsKVManager*>(ctxt->native)->kvDataManager_.CloseKvStore(appId, storeId);
        ZLOGD("CloseKVStore return status:%{public}d", status);
        ctxt->status
            = ((status == Status::SUCCESS) || (status == Status::STORE_NOT_FOUND) || (status == Status::STORE_NOT_OPEN))
            ? napi_ok
            : napi_generic_failure;
    };
    return NapiQueue::AsyncWork(env, ctxt, std::string(__FUNCTION__), execute);
}

/*
 * [JS API Prototype]
 * [AsyncCB]  deleteKVStore(appId: string, storeId: string, callback: AsyncCallback<void>): void
 * [Promise]  deleteKVStore(appId: string, storeId: string):Promise<void>
 */
napi_value JsKVManager::DeleteKVStore(napi_env env, napi_callback_info info)
{
    ZLOGD("DeleteKVStore in");
    struct ContextInfo : public ContextBase {
        std::string appId;
        std::string storeId;
    };
    auto ctxt = std::make_shared<ContextInfo>();
    auto input = [env, ctxt](size_t argc, napi_value* argv) {
        // required 2 arguments :: <appId> <storeId>
        ZLOGE_ON_ARGS(ctxt, argc >= 2, "invalid arguments!");
        size_t index = 0;
        ctxt->status = JSUtil::GetValue(env, argv[index++], ctxt->appId);
        ZLOGE_ON_ARGS(ctxt, !ctxt->appId.empty(), "invalid appId");
        ctxt->status = JSUtil::GetValue(env, argv[index++], ctxt->storeId);
        ZLOGE_ON_ARGS(ctxt, !ctxt->storeId.empty(), "invalid storeId");
    };
    ctxt->GetCbInfo(env, info, input);

    auto execute = [ctxt]() {
        AppId appId { ctxt->appId };
        StoreId storeId { ctxt->storeId };
        Status status = reinterpret_cast<JsKVManager*>(ctxt->native)->kvDataManager_.DeleteKvStore(appId, storeId);
        ZLOGD("DeleteKvStore status:%{public}d", status);
        ctxt->status = (status == Status::SUCCESS) ? napi_ok : napi_generic_failure;
    };
    return NapiQueue::AsyncWork(env, ctxt, std::string(__FUNCTION__), execute);
}

/*
 * [JS API Prototype]
 * [AsyncCB]  getAllKVStoreId(appId: string, callback: AsyncCallback<string[]>):void
 * [Promise]  getAllKVStoreId(appId: string):Promise<string[]>
 */
napi_value JsKVManager::GetAllKVStoreId(napi_env env, napi_callback_info info)
{
    ZLOGD("GetAllKVStoreId in");
    struct ContextInfo : public ContextBase {
        std::string appId;
        std::vector<StoreId> storeIdList;
    };

    auto ctxt = std::make_shared<ContextInfo>();
    auto input = [env, ctxt](size_t argc, napi_value* argv) {
        // required 1 arguments :: <appId>
        ZLOGE_ON_ARGS(ctxt, argc == 1, "invalid arguments!");
        ctxt->status = JSUtil::GetValue(env, argv[0], ctxt->appId);
        ZLOGE_ON_ARGS(ctxt, !ctxt->appId.empty(), "invalid appId!");
    };
    ctxt->GetCbInfo(env, info, input);

    auto execute = [ctxt]() {
        auto kvm = reinterpret_cast<JsKVManager*>(ctxt->native);
        ZLOGE_ON_ARGS(ctxt, kvm != nullptr, "KVManager is null, failed!");
        AppId appId { ctxt->appId };
        Status status = kvm->kvDataManager_.GetAllKvStoreId(appId, ctxt->storeIdList);
        ZLOGD("execute status:%{public}d", status);
        ctxt->status = (status == Status::SUCCESS) ? napi_ok : napi_generic_failure;
    };
    auto output = [env, ctxt](napi_value& result) {
        ctxt->status = JSUtil::SetValue(env, ctxt->storeIdList, result);
        ZLOGD("output status:%{public}d", ctxt->status);
    };
    return NapiQueue::AsyncWork(env, ctxt, std::string(__FUNCTION__), execute, output);
}

napi_value JsKVManager::On(napi_env env, napi_callback_info info)
{
    auto ctxt = std::make_shared<ContextBase>();
    auto input = [env, ctxt](size_t argc, napi_value* argv) {
        // required 2 arguments :: <event> <callback>
        ZLOGE_ON_ARGS(ctxt, argc == 2, "invalid arguments!");
        std::string event;
        ctxt->status = JSUtil::GetValue(env, argv[0], event);
        ZLOGI("subscribe to event:%{public}s", event.c_str());
        ZLOGE_ON_ARGS(ctxt, event == "distributedDataServiceDie", "invalid arg[0], i.e. invalid event!");

        napi_valuetype valueType = napi_undefined;
        ctxt->status = napi_typeof(env, argv[1], &valueType);
        ZLOGE_ON_STATUS(ctxt, "napi_typeof failed!");
        ZLOGE_ON_ARGS(ctxt, valueType == napi_function, "callback is not a function");

        JsKVManager* proxy = reinterpret_cast<JsKVManager*>(ctxt->native);
        ZLOGE_ON_ARGS(ctxt, proxy != nullptr, "there is no native kv manager");

        std::lock_guard<std::mutex> lck(proxy->deathMutex_);
        for (auto& it : proxy->deathRecipient_) {
            auto recipient = std::static_pointer_cast<DeathRecipient>(it);
            if (*recipient == argv[1]) {
                ZLOGD("KVManager::On callback already register!");
                return;
            }
        }
        auto kvStoreDeathRecipient = std::make_shared<DeathRecipient>(env, argv[1]);
        proxy->kvDataManager_.RegisterKvStoreServiceDeathRecipient(kvStoreDeathRecipient);
        proxy->deathRecipient_.push_back(kvStoreDeathRecipient);
        ZLOGD("on mapsize: %{public}d", static_cast<int>(proxy->deathRecipient_.size()));
    };
    NAPI_CALL(env, ctxt->GetCbInfoSync(env, info, input));
    return nullptr;
}

napi_value JsKVManager::Off(napi_env env, napi_callback_info info)
{
    ZLOGD("KVManager::Off()");
    auto ctxt = std::make_shared<ContextBase>();
    auto input = [env, ctxt](size_t argc, napi_value* argv) {
        // required 1 or 2 arguments :: <event> [callback]
        ZLOGE_ON_ARGS(ctxt, (argc == 1) || (argc == 2), "invalid arguments!");
        std::string event;
        ctxt->status = JSUtil::GetValue(env, argv[0], event);
        // required 1 arguments :: <event>
        ZLOGI("unsubscribe to event:%{public}s %{public}s specified", event.c_str(), (argc == 1) ? "without": "with");
        ZLOGE_ON_ARGS(ctxt, event == "distributedDataServiceDie", "invalid arg[0], i.e. invalid event!");
        // have 2 arguments :: have the [callback]
        if (argc == 2) {
            napi_valuetype valueType = napi_undefined;
            ctxt->status = napi_typeof(env, argv[1], &valueType);
            ZLOGE_ON_STATUS(ctxt, "napi_typeof failed!");
            ZLOGE_ON_ARGS(ctxt, valueType == napi_function, "callback is not a function");
        }
        JsKVManager* proxy = reinterpret_cast<JsKVManager*>(ctxt->native);
        std::lock_guard<std::mutex> lck(proxy->deathMutex_);
        auto it = proxy->deathRecipient_.begin();
        while (it != proxy->deathRecipient_.end()) {
            auto recipient = std::static_pointer_cast<DeathRecipient>(*it);
            // have 2 arguments :: have the [callback]
            if ((argc == 1) || *recipient == argv[1]) {
                proxy->kvDataManager_.UnRegisterKvStoreServiceDeathRecipient(*it);
                it = proxy->deathRecipient_.erase(it);
            } else {
                ++it;
            }
        }
        ZLOGD("off mapsize: %{public}d", static_cast<int>(proxy->deathRecipient_.size()));
    };
    NAPI_CALL(env, ctxt->GetCbInfoSync(env, info, input));
    ZLOGD("KVManager::Off callback is not register or already unregister!");
    return nullptr;
}

napi_value JsKVManager::Constructor(napi_env env)
{
    const napi_property_descriptor properties[] = {
        DECLARE_NAPI_FUNCTION("getKVStore", JsKVManager::GetKVStore),
        DECLARE_NAPI_FUNCTION("closeKVStore", JsKVManager::CloseKVStore),
        DECLARE_NAPI_FUNCTION("deleteKVStore", JsKVManager::DeleteKVStore),
        DECLARE_NAPI_FUNCTION("getAllKVStoreId", JsKVManager::GetAllKVStoreId),
        DECLARE_NAPI_FUNCTION("on", JsKVManager::On),
        DECLARE_NAPI_FUNCTION("off", JsKVManager::Off)
    };
    size_t count = sizeof(properties) / sizeof(properties[0]);
    return JSUtil::DefineClass(env, "KVManager", properties, count, JsKVManager::New);
}

napi_value JsKVManager::New(napi_env env, napi_callback_info info)
{
    std::string bundleName;
    auto ctxt = std::make_shared<ContextBase>();
    auto input = [env, ctxt, &bundleName](size_t argc, napi_value* argv) {
        // required 1 arguments :: <bundleName>
        ZLOGE_ON_ARGS(ctxt, argc == 1, "invalid arguments!");
        ctxt->status = JSUtil::GetNamedProperty(env, argv[0], "bundleName", bundleName);
        ZLOGE_ON_STATUS(ctxt, "invalid arg[0], i.e. invalid bundleName!");
        ZLOGE_ON_ARGS(ctxt, !bundleName.empty(), "invalid arg[0], i.e. invalid bundleName!");
    };
    NAPI_CALL(env, ctxt->GetCbInfoSync(env, info, input));

    JsKVManager* kvManager = new (std::nothrow) JsKVManager(bundleName);
    NAPI_ASSERT(env, kvManager !=nullptr, "no memory for kvManager");

    auto finalize = [](napi_env env, void* data, void* hint) {
        ZLOGD("kvManager finalize.");
        auto* kvManager = reinterpret_cast<JsKVManager*>(data);
        ZLOGE_RETURN_VOID(kvManager != nullptr, "finalize null!");
        delete kvManager;
    };
    NAPI_CALL(env, napi_wrap(env, ctxt->self, kvManager, finalize, nullptr, nullptr));
    return ctxt->self;
}

DeathRecipient::DeathRecipient(napi_env env, napi_value callback)
    : UvQueue(env, callback)
{
}

void DeathRecipient::OnRemoteDied()
{
    CallFunction();
}
}
