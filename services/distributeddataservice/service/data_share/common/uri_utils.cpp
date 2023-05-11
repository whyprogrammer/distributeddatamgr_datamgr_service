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
#define LOG_TAG "URIUtils"

#include "uri_utils.h"

#include "log_print.h"
#include "string_ex.h"
#include "uri.h"
#include "utils/anonymous.h"

namespace OHOS::DataShare {
bool URIUtils::GetInfoFromURI(const std::string &uri, UriInfo &uriInfo)
{
    Uri uriTemp(uri);
    std::vector<std::string> splitUri;
    SplitStr(uriTemp.GetPath(), "/", splitUri);
    if (splitUri.size() < PARAM_SIZE) {
        ZLOGE("Invalid uri: %{public}s", DistributedData::Anonymous::Change(uri).c_str());
        return false;
    }

    if (splitUri[BUNDLE_NAME].empty() || splitUri[MODULE_NAME].empty() ||
        splitUri[STORE_NAME].empty() || splitUri[TABLE_NAME].empty()) {
        ZLOGE("Uri has empty field! bundleName: %{public}s  uri: %{public}s", splitUri[BUNDLE_NAME].c_str(),
            DistributedData::Anonymous::Change(uri).c_str());
        return false;
    }

    uriInfo.bundleName = splitUri[BUNDLE_NAME];
    uriInfo.moduleName = splitUri[MODULE_NAME];
    uriInfo.storeName = splitUri[STORE_NAME];
    uriInfo.tableName = splitUri[TABLE_NAME];
    return true;
}

bool URIUtils::IsDataProxyURI(const std::string &uri)
{
    return uri.compare(0, DATA_PROXY_SCHEMA_LEN, URIUtils::DATA_PROXY_SCHEMA) == 0;
}

bool URIUtils::GetBundleNameFromProxyURI(const std::string &uri, std::string &bundleName)
{
    Uri uriTemp(uri);
    if (!uriTemp.GetAuthority().empty()) {
        bundleName = uriTemp.GetAuthority();
    }
    return true;
}

bool URIUtils::GetUserIdFromProxyURI(const std::string &uri, int32_t &user)
{
    Uri uriTemp(uri);
    std::vector<std::string> splitUri;
    SplitStr(uriTemp.GetQuery(), "=", splitUri);
    for (auto iter = splitUri.begin(); iter < splitUri.end(); iter++) {
        if (*iter == "user" && iter + 1 < splitUri.end()) {
            const char *value = (iter + 1)->c_str();
            user = atoi(value);
            return true;
        }
    }
    return false;
}
} // namespace OHOS::DataShare