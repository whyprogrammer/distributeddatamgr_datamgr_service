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

#ifndef GRD_RESULTSET_API_H
#define GRD_RESULTSET_API_H
#include "grd_type_export.h"
#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef struct GRD_ResultSet GRD_ResultSet;

DOC_API int GRD_Next(GRD_ResultSet *resultSet);

DOC_API int GRD_GetValue(GRD_ResultSet *resultSet, char **value);

DOC_API int GRD_FreeValue(char *value);

DOC_API int GRD_FreeResultSet(GRD_ResultSet *resultSet);

#ifdef __cplusplus
}
#endif // __cplusplus
#endif // GRD_RESULTSET_API_H