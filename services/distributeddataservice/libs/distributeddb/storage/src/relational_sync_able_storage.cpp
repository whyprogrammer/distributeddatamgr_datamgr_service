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
#ifdef RELATIONAL_STORE
#include "relational_sync_able_storage.h"
#include "platform_specific.h"
#include "generic_single_ver_kv_entry.h"

namespace DistributedDB {
#define CHECK_STORAGE_ENGINE do { \
    if (storageEngine_ == nullptr) { \
        return -E_INVALID_DB; \
    } \
} while (0)

RelationalSyncAbleStorage::RelationalSyncAbleStorage(StorageEngine *engine)
    : storageEngine_(static_cast<SQLiteSingleRelationalStorageEngine *>(engine))
{}

RelationalSyncAbleStorage::~RelationalSyncAbleStorage()
{}

// Get interface type of this kvdb.
int RelationalSyncAbleStorage::GetInterfaceType() const
{
    return SYNC_RELATION;
}

// Get the interface ref-count, in order to access asynchronously.
void RelationalSyncAbleStorage::IncRefCount()
{
    LOGD("RelationalSyncAbleStorage ref +1");
    IncObjRef(this);
}

// Drop the interface ref-count.
void RelationalSyncAbleStorage::DecRefCount()
{
    LOGD("RelationalSyncAbleStorage ref -1");
    DecObjRef(this);
}

// Get the identifier of this kvdb.
std::vector<uint8_t> RelationalSyncAbleStorage::GetIdentifier() const
{
    return {};
}

// Get the max timestamp of all entries in database.
void RelationalSyncAbleStorage::GetMaxTimeStamp(TimeStamp &stamp) const
{
    std::lock_guard<std::mutex> lock(maxTimeStampMutex_);
    stamp = currentMaxTimeStamp_;
}

int RelationalSyncAbleStorage::SetMaxTimeStamp(TimeStamp timestamp)
{
    std::lock_guard<std::mutex> lock(maxTimeStampMutex_);
    if (timestamp > currentMaxTimeStamp_) {
        currentMaxTimeStamp_ = timestamp;
    }
    return E_OK;
}

SQLiteSingleVerRelationalStorageExecutor *RelationalSyncAbleStorage::GetHandle(bool isWrite, int &errCode,
    OperatePerm perm) const
{
    if (storageEngine_ == nullptr) {
        errCode = -E_INVALID_DB;
        return nullptr;
    }
    return static_cast<SQLiteSingleVerRelationalStorageExecutor *>(storageEngine_->FindExecutor(isWrite, perm,
        errCode));
}

void RelationalSyncAbleStorage::ReleaseHandle(SQLiteSingleVerRelationalStorageExecutor *&handle) const
{
    if (storageEngine_ == nullptr) {
        return;
    }
    StorageExecutor *databaseHandle = handle;
    storageEngine_->Recycle(databaseHandle);
}

// Get meta data associated with the given key.
int RelationalSyncAbleStorage::GetMetaData(const Key &key, Value &value) const
{
    CHECK_STORAGE_ENGINE;
    if (key.size() > DBConstant::MAX_KEY_SIZE) {
        return -E_INVALID_ARGS;
    }

    int errCode = E_OK;
    auto handle = GetHandle(true, errCode, OperatePerm::NORMAL_PERM);
    if (handle == nullptr) {
        return errCode;
    }
    errCode = handle->GetKvData(key, value);
    ReleaseHandle(handle);
    return errCode;
}

// Put meta data as a key-value entry.
int RelationalSyncAbleStorage::PutMetaData(const Key &key, const Value &value)
{
    CHECK_STORAGE_ENGINE;
    int errCode = E_OK;
    auto *handle = GetHandle(true, errCode, OperatePerm::NORMAL_PERM);
    if (handle == nullptr) {
        return errCode;
    }

    errCode = handle->PutKvData(key, value); // meta doesn't need time.
    if (errCode != E_OK) {
        LOGE("Put kv data err:%d", errCode);
    }
    ReleaseHandle(handle);
    return errCode;
}

// Delete multiple meta data records in a transaction.
int RelationalSyncAbleStorage::DeleteMetaData(const std::vector<Key> &keys)
{
    for (const auto &key : keys) {
        if (key.empty() || key.size() > DBConstant::MAX_KEY_SIZE) {
            return -E_INVALID_ARGS;
        }
    }
    int errCode = E_OK;
    auto handle = GetHandle(true, errCode, OperatePerm::NORMAL_PERM);
    if (handle == nullptr) {
        return errCode;
    }

    handle->StartTransaction(TransactType::IMMEDIATE);
    errCode = handle->DeleteMetaData(keys);
    if (errCode != E_OK) {
        handle->Rollback();
        LOGE("[SinStore] DeleteMetaData failed, errCode = %d", errCode);
    } else {
        handle->Commit();
    }
    ReleaseHandle(handle);
    return errCode;
}

// Delete multiple meta data records with key prefix in a transaction.
int RelationalSyncAbleStorage::DeleteMetaDataByPrefixKey(const Key &keyPrefix) const
{
    if (keyPrefix.empty() || keyPrefix.size() > DBConstant::MAX_KEY_SIZE) {
        return -E_INVALID_ARGS;
    }

    int errCode = E_OK;
    auto handle = GetHandle(true, errCode, OperatePerm::NORMAL_PERM);
    if (handle == nullptr) {
        return errCode;
    }

    errCode = handle->DeleteMetaDataByPrefixKey(keyPrefix);
    if (errCode != E_OK) {
        LOGE("[SinStore] DeleteMetaData by prefix key failed, errCode = %d", errCode);
    }
    ReleaseHandle(handle);
    return errCode;
}

// Get all meta data keys.
int RelationalSyncAbleStorage::GetAllMetaKeys(std::vector<Key> &keys) const
{
    CHECK_STORAGE_ENGINE;
    int errCode = E_OK;
    auto *handle = GetHandle(true, errCode, OperatePerm::NORMAL_PERM);
    if (handle == nullptr) {
        return errCode;
    }

    errCode = handle->GetAllMetaKeys(keys);
    ReleaseHandle(handle);
    return errCode;
}

const KvDBProperties &RelationalSyncAbleStorage::GetDbProperties() const
{
    return properties;
}

static void ReleaseKvEntries(std::vector<SingleVerKvEntry *> &entries)
{
    for (auto &itemEntry : entries) {
        delete itemEntry;
        itemEntry = nullptr;
    }
    entries.clear();
}

static int GetKvEntriesByDataItems(std::vector<SingleVerKvEntry *> &entries, std::vector<DataItem> &dataItems)
{
    int errCode = E_OK;
    for (auto &item : dataItems) {
        auto entry = new (std::nothrow) GenericSingleVerKvEntry();
        if (entry == nullptr) {
            errCode = -E_OUT_OF_MEMORY;
            LOGE("GetKvEntries failed, errCode:%d", errCode);
            ReleaseKvEntries(entries);
            break;
        }
        entry->SetEntryData(std::move(item));
        entries.push_back(entry);
    }
    return errCode;
}

static size_t GetDataItemSerialSize(const DataItem &item, size_t appendLen)
{
    // timestamp and local flag: 3 * uint64_t, version(uint32_t), key, value, origin dev and the padding size.
    // the size would not be very large.
    static const size_t maxOrigDevLength = 40;
    size_t devLength = std::max(maxOrigDevLength, item.origDev.size());
    size_t dataSize = (Parcel::GetUInt64Len() * 3 + Parcel::GetUInt32Len() + Parcel::GetVectorCharLen(item.key) +
                       Parcel::GetVectorCharLen(item.value) + devLength + appendLen);
    return dataSize;
}

static constexpr float QUERY_SYNC_THRESHOLD = 0.50;
static bool CanHoldDeletedData(const std::vector<DataItem> &dataItems, const DataSizeSpecInfo &dataSizeInfo,
     size_t appendLen)
{
    bool reachThreshold = false;
    for (size_t i = 0, blockSize = 0; !reachThreshold && i < dataItems.size(); i++) {
        blockSize += GetDataItemSerialSize(dataItems[i], appendLen);
        reachThreshold = (blockSize >= dataSizeInfo.blockSize * QUERY_SYNC_THRESHOLD);
    }
    return !reachThreshold;
}

static void ProcessContinueTokenForQuerySync(const std::vector<DataItem> &dataItems, int &errCode,
    SQLiteSingleVerContinueToken *&token)
{
    if (errCode != -E_UNFINISHED) { // Error happened or get data finished. Token should be cleared.
        delete token;
        token = nullptr;
        return;
    }

    if (dataItems.empty()) {
        errCode = -E_INTERNAL_ERROR;
        LOGE("Get data unfinished but data items is empty.");
        delete token;
        token = nullptr;
        return;
    }

    TimeStamp nextBeginTime = dataItems.back().timeStamp + 1;
    if (nextBeginTime > INT64_MAX) {
        nextBeginTime = INT64_MAX;
    }
    bool getDeleteData = ((dataItems.back().flag & DataItem::DELETE_FLAG) != 0);
    if (getDeleteData) {
        token->FinishGetQueryData();
        token->SetDeletedNextBeginTime("", nextBeginTime);
    } else {
        token->SetNextBeginTime("", nextBeginTime);
    }
}

/**
 * Caller must ensure that parameter continueStmtToken is valid.
 * If error happened, token will be deleted here.
 */
int RelationalSyncAbleStorage::GetSyncDataForQuerySync(std::vector<DataItem> &dataItems,
    SQLiteSingleVerContinueToken *&continueStmtToken, const DataSizeSpecInfo &dataSizeInfo) const
{
    if (storageEngine_ == nullptr) {
        return -E_INVALID_DB;
    }

    int errCode = E_OK;
    auto handle = static_cast<SQLiteSingleVerRelationalStorageExecutor *>(storageEngine_->FindExecutor(false,
        OperatePerm::NORMAL_PERM, errCode));
    if (handle == nullptr) {
        goto ERROR;
    }

    errCode = handle->SetTableInfo(continueStmtToken->GetQuery());
    if (errCode != E_OK) {
        goto ERROR;
    }

    // Get query data.
    if (!continueStmtToken->IsGetQueryDataFinished()) {
        LOGD("[SingleVerNStore] Get query data between %llu and %llu.", continueStmtToken->GetQueryBeginTime(),
             continueStmtToken->GetQueryEndTime());
        errCode = handle->GetSyncDataByQuery(
            dataItems, Parcel::GetAppendedLen(), continueStmtToken->GetQuery(), dataSizeInfo,
            // need modify
            std::make_pair(continueStmtToken->GetQueryBeginTime(), continueStmtToken->GetQueryEndTime()));
    }

    // Get query data finished.
    if (errCode == E_OK || errCode == -E_FINISHED) {
        // Clear query timeRange.
        continueStmtToken->FinishGetQueryData();

        // Get delete time next.
        if (!continueStmtToken->IsGetDeletedDataFinished() &&
            CanHoldDeletedData(dataItems, dataSizeInfo, Parcel::GetAppendedLen())) {
            LOGD("[SingleVerNStore] Get deleted data between %llu and %llu.", continueStmtToken->GetDeletedBeginTime(),
                 continueStmtToken->GetDeletedEndTime());
            errCode = handle->GetDeletedSyncDataByTimestamp(
                dataItems, Parcel::GetAppendedLen(),
                // need modify
                continueStmtToken->GetDeletedBeginTime(), continueStmtToken->GetDeletedEndTime(), dataSizeInfo);
        }
    }

    if (errCode == -E_FINISHED) {
        errCode = E_OK;
    }

ERROR:
    if (errCode != -E_UNFINISHED && errCode != E_OK) { // Error happened.
        dataItems.clear();
    }
    ProcessContinueTokenForQuerySync(dataItems, errCode, continueStmtToken);
    ReleaseHandle(handle);
    return errCode;
}

// use kv struct data to sync
// Get the data which would be synced with query condition
int RelationalSyncAbleStorage::GetSyncData(QueryObject &query, const SyncTimeRange &timeRange,
    const DataSizeSpecInfo &dataSizeInfo, ContinueToken &continueStmtToken,
    std::vector<SingleVerKvEntry *> &entries) const
{
    if (!timeRange.IsValid()) {
        return -E_INVALID_ARGS;
    }

    auto token = new (std::nothrow) SQLiteSingleVerContinueToken(timeRange, query); // release in sync module
    if (token == nullptr) {
        LOGE("[SingleVerNStore] Allocate continue token failed.");
        return -E_OUT_OF_MEMORY;
    }

    int innerCode;
    std::vector<DataItem> dataItems;
    int errCode = GetSyncDataForQuerySync(dataItems, token, dataSizeInfo);
    if (errCode != E_OK && errCode != -E_UNFINISHED) { // The code need be sent to outside except new error happened.
        continueStmtToken = static_cast<ContinueToken>(token);
        return errCode;
    }

    innerCode = GetKvEntriesByDataItems(entries, dataItems);
    if (innerCode != E_OK) {
        errCode = innerCode;
        delete token;
        token = nullptr;
    }
    continueStmtToken = static_cast<ContinueToken>(token);
    return errCode;
}

int RelationalSyncAbleStorage::GetSyncDataNext(std::vector<SingleVerKvEntry *> &entries,
    ContinueToken &continueStmtToken, const DataSizeSpecInfo &dataSizeInfo) const
{
    return E_OK;
}

// Release the continue token of getting data.
void RelationalSyncAbleStorage::ReleaseContinueToken(ContinueToken &continueStmtToken) const
{
    return;
}

int RelationalSyncAbleStorage::PutSyncDataWithQuery(const QueryObject &object,
    const std::vector<SingleVerKvEntry *> &entries, const DeviceID &deviceName)
{
    std::vector<DataItem> dataItems;
    for (auto itemEntry : entries) {
        GenericSingleVerKvEntry *entry = static_cast<GenericSingleVerKvEntry *>(itemEntry);
        if (entry != nullptr) {
            DataItem item;
            item.origDev = entry->GetOrigDevice();
            item.flag = entry->GetFlag();
            item.timeStamp = entry->GetTimestamp();
            item.writeTimeStamp = entry->GetWriteTimestamp();
            entry->GetKey(item.key);
            entry->GetValue(item.value);
            entry->GetHashKey(item.hashKey);
            dataItems.push_back(item);
        }
    }

    return PutSyncData(object, dataItems, deviceName);
}

int RelationalSyncAbleStorage::SaveSyncDataItems(const QueryObject &object, std::vector<DataItem> &dataItems,
    const std::string &deviceName)
{
    int errCode = E_OK;
    LOGD("[SQLiteSingleVerNaturalStore::SaveSyncData] Get write handle.");
    auto *handle = GetHandle(true, errCode, OperatePerm::NORMAL_PERM);
    if (handle == nullptr) {
        return errCode;
    }

    errCode = handle->SetTableInfo(object);
    if (errCode != E_OK) {
        ReleaseHandle(handle);
        return errCode;
    }

    TimeStamp maxTimestamp = 0;
    errCode = handle->SaveSyncItems(object, dataItems, deviceName, maxTimestamp);
    if (errCode == E_OK) {
        (void)SetMaxTimeStamp(maxTimestamp);
    }

    ReleaseHandle(handle);
    return errCode;
}

int RelationalSyncAbleStorage::PutSyncData(const QueryObject &query, std::vector<DataItem> &dataItems,
    const std::string &deviceName)
{
    if (deviceName.length() > DBConstant::MAX_DEV_LENGTH) {
        LOGW("Device length is invalid for sync put");
        return -E_INVALID_ARGS;
    }

    int errCode = SaveSyncDataItems(query, dataItems, deviceName); // Currently true to check value content
    if (errCode != E_OK) {
        LOGE("[Relational] PutSyncData errCode:%d", errCode);
    }
    return errCode;
}

int RelationalSyncAbleStorage::RemoveDeviceData(const std::string &deviceName, bool isNeedNotify)
{
    return -E_NOT_SUPPORT;
}

RelationalSchemaObject RelationalSyncAbleStorage::GetSchemaInfo() const
{
    return RelationalSchemaObject();
}

int RelationalSyncAbleStorage::GetSecurityOption(SecurityOption &option) const
{
    return -E_NOT_SUPPORT;
}

void RelationalSyncAbleStorage::NotifyRemotePushFinished(const std::string &deviceId) const
{
    return;
}

// Get the timestamp when database created or imported
int RelationalSyncAbleStorage::GetDatabaseCreateTimeStamp(TimeStamp &outTime) const
{
    return OS::GetCurrentSysTimeInMicrosecond(outTime);
}

// Get batch meta data associated with the given key.
int RelationalSyncAbleStorage::GetBatchMetaData(const std::vector<Key> &keys, std::vector<Entry> &entries) const
{
    return -E_NOT_SUPPORT;
}

// Put batch meta data as a key-value entry vector
int RelationalSyncAbleStorage::PutBatchMetaData(std::vector<Entry> &entries)
{
    return -E_NOT_SUPPORT;
}

std::vector<QuerySyncObject> RelationalSyncAbleStorage::GetTablesQuery()
{
    return {};
}

int RelationalSyncAbleStorage::LocalDataChanged(int notifyEvent, std::vector<QuerySyncObject> &queryObj)
{
    return -E_NOT_SUPPORT;
}

int RelationalSyncAbleStorage::SchemaChanged(int notifyEvent)
{
    return -E_NOT_SUPPORT;
}
}
#endif