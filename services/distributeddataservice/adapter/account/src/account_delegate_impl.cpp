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

#define LOG_TAG "EVENT_HANDLER"

#include "account_delegate_impl.h"
#include <algorithm>
#include <list>
#include <regex>
#include <thread>
#include <unistd.h>
#include <vector>
#include "constant.h"
#include "ohos_account_kits.h"
#include "os_account_manager.h"
#include "permission_validator.h"
#include "utils/crypto.h"

namespace OHOS {
namespace DistributedKv {
using namespace OHOS::EventFwk;
using namespace OHOS::AAFwk;
using namespace OHOS::DistributedData;

EventSubscriber::EventSubscriber(const CommonEventSubscribeInfo &info) : CommonEventSubscriber(info) {}

void EventSubscriber::OnReceiveEvent(const CommonEventData &event)
{
    const auto want = event.GetWant();
    AccountEventInfo accountEventInfo {};
    std::string action = want.GetAction();
    ZLOGI("Want Action is %s", action.c_str());

    if (action == CommonEventSupport::COMMON_EVENT_USER_REMOVED) {
        accountEventInfo.status = AccountStatus::DEVICE_ACCOUNT_DELETE;
        accountEventInfo.deviceAccountId =
            std::to_string(want.GetIntParam(CommonEventSupport::COMMON_EVENT_USER_REMOVED, -1));
        if (accountEventInfo.deviceAccountId == "-1") {
            return;
        }
    } else if (action == CommonEventSupport::COMMON_EVENT_USER_SWITCHED) {
        accountEventInfo.status = AccountStatus::DEVICE_ACCOUNT_SWITCHED;
        accountEventInfo.deviceAccountId =
                std::to_string(want.GetIntParam(CommonEventSupport::COMMON_EVENT_USER_SWITCHED, -1));
    } else {
        return;
    }
    eventCallback_(accountEventInfo);
}

void EventSubscriber::SetEventCallback(EventCallback callback)
{
    eventCallback_ = callback;
}

AccountDelegate::BaseInstance AccountDelegate::getInstance_ = AccountDelegateImpl::GetBaseInstance;

AccountDelegateImpl *AccountDelegateImpl::GetInstance()
{
    static AccountDelegateImpl accountDelegate;
    return &accountDelegate;
}

AccountDelegate *AccountDelegateImpl::GetBaseInstance()
{
    return AccountDelegateImpl::GetInstance();
}

AccountDelegateImpl::~AccountDelegateImpl()
{
    ZLOGE("destruct");
    observerMap_.Clear();
    const auto result = CommonEventManager::UnSubscribeCommonEvent(eventSubscriber_);
    if (!result) {
        ZLOGE("Fail to unregister account event listener!");
    }
}

void AccountDelegateImpl::SubscribeAccountEvent()
{
    ZLOGI("Subscribe account event listener start.");
    MatchingSkills matchingSkills;
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_USER_REMOVED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_USER_SWITCHED);
    CommonEventSubscribeInfo info(matchingSkills);
    eventSubscriber_ = std::make_shared<EventSubscriber>(info);
    eventSubscriber_->SetEventCallback([&](AccountEventInfo &account) {
        account.harmonyAccountId = GetCurrentAccountId();
        NotifyAccountChanged(account);
    });

    std::thread th = std::thread([eventSubscriber = eventSubscriber_]() {
        int tryTimes = 0;
        constexpr int MAX_RETRY_TIME = 300;
        constexpr int RETRY_WAIT_TIME_S = 1;

        // we use this method to make sure register success
        while (tryTimes < MAX_RETRY_TIME) {
            auto result = CommonEventManager::SubscribeCommonEvent(eventSubscriber);
            if (result) {
                break;
            }

            ZLOGE("EventManager: Fail to register subscriber, error:%d", result);
            sleep(RETRY_WAIT_TIME_S);
            tryTimes++;
        }
        if (tryTimes == MAX_RETRY_TIME) {
            ZLOGE("EventManager: Fail to register subscriber!");
        }
        ZLOGI("EventManager: Success to register subscriber.");
    });
    th.detach();
}

std::string AccountDelegateImpl::GetCurrentAccountId(const std::string &bundleName) const
{
    ZLOGD("start");
    if (!bundleName.empty() && PermissionValidator::IsAutoLaunchEnabled(bundleName)) {
        return Constant::DEFAULT_GROUP_ID;
    }
    auto ohosAccountInfo = AccountSA::OhosAccountKits::GetInstance().QueryOhosAccountInfo();
    if (!ohosAccountInfo.first) {
        ZLOGE("get ohosAccountInfo from OhosAccountKits is null, return default");
        return AccountSA::DEFAULT_OHOS_ACCOUNT_UID;
    }
    if (ohosAccountInfo.second.uid_.empty()) {
        ZLOGE("get ohosAccountInfo from OhosAccountKits is null, return default");
        return AccountSA::DEFAULT_OHOS_ACCOUNT_UID;
    }

    return Sha256UserId(ohosAccountInfo.second.uid_);
}

std::string AccountDelegateImpl::GetDeviceAccountIdByUID(int32_t uid) const
{
    int userId = 0;
    auto ret = AccountSA::OsAccountManager::GetOsAccountLocalIdFromUid(uid, userId);
    if (ret != 0) {
        ZLOGE("failed get os account local id from uid, ret:%{public}d", ret);
        return {};
    }
    return std::to_string(userId);
}

void AccountDelegateImpl::NotifyAccountChanged(const AccountEventInfo &accountEventInfo)
{
    observerMap_.ForEach([&accountEventInfo] (const auto& key, const auto& val) {
        val->OnAccountChanged(accountEventInfo);
        return false;
    });
}

Status AccountDelegateImpl::Subscribe(std::shared_ptr<Observer> observer)
{
    ZLOGD("start");
    if (observer == nullptr || observer->Name().empty()) {
        return Status::INVALID_ARGUMENT;
    }
    if (observerMap_.Contains(observer->Name())) {
        return Status::INVALID_ARGUMENT;
    }

    auto ret = observerMap_.Insert(observer->Name(), observer);
    if (ret) {
        ZLOGD("end");
        return Status::SUCCESS;
    }
    ZLOGE("fail");
    return Status::ERROR;
}

Status AccountDelegateImpl::Unsubscribe(std::shared_ptr<Observer> observer)
{
    ZLOGD("start");
    if (observer == nullptr || observer->Name().empty()) {
        return Status::INVALID_ARGUMENT;
    }
    if (!observerMap_.Contains(observer->Name())) {
        return Status::INVALID_ARGUMENT;
    }

    auto ret = observerMap_.Erase(observer->Name());
    if (ret) {
        ZLOGD("end");
        return Status::SUCCESS;
    }
    ZLOGD("fail");
    return Status::ERROR;
}

std::string AccountDelegateImpl::Sha256UserId(const std::string &plainText) const
{
    std::regex pattern("^[0-9]+$");
    if (!std::regex_match(plainText, pattern)) {
        return plainText;
    }

    std::string::size_type sizeType;
    int64_t plainVal;
    std::string::size_type int64MaxLen(std::to_string(INT64_MAX).size());
    // plain text length must be less than INT64_MAX string.
    try {
        plainVal = static_cast<int64_t>(std::stoll(plainText, &sizeType));
    } catch (const std::out_of_range &) {
        plainVal = static_cast<int64_t>(std::stoll(
            plainText.substr(plainText.size() - int64MaxLen + 1, int64MaxLen - 1), &sizeType));
    } catch (const std::exception &) {
        return plainText;
    }

    union UnionLong {
        int64_t val;
        unsigned char byteLen[sizeof(int64_t)];
    };
    UnionLong unionLong {};
    unionLong.val = plainVal;
    std::list<char> unionList(std::begin(unionLong.byteLen), std::end(unionLong.byteLen));
    unionList.reverse();
    std::vector<char> unionVec(unionList.begin(), unionList.end());
    return Crypto::Sha256(unionVec.data(), unionVec.size(), true);
}
}  // namespace DistributedKv
}  // namespace OHOS
