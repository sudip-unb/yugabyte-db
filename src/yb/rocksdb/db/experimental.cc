//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//


#include "yb/rocksdb/db/db_impl.h"

namespace rocksdb {
namespace experimental {

#ifndef ROCKSDB_LITE

Status SuggestCompactRange(DB* db, ColumnFamilyHandle* column_family,
                           const Slice* begin, const Slice* end) {
  auto dbimpl = dynamic_cast<DBImpl*>(db);
  if (dbimpl == nullptr) {
    return STATUS(InvalidArgument, "Didn't recognize DB object");
  }

  return dbimpl->SuggestCompactRange(column_family, begin, end);
}

Status PromoteL0(DB* db, ColumnFamilyHandle* column_family, int target_level) {
  auto dbimpl = dynamic_cast<DBImpl*>(db);
  if (dbimpl == nullptr) {
    return STATUS(InvalidArgument, "Didn't recognize DB object");
  }
  return dbimpl->PromoteL0(column_family, target_level);
}

#else  // ROCKSDB_LITE

Status SuggestCompactRange(DB* db, ColumnFamilyHandle* column_family,
                           const Slice* begin, const Slice* end) {
  return STATUS(NotSupported, "Not supported in RocksDB LITE");
}

Status PromoteL0(DB* db, ColumnFamilyHandle* column_family, int target_level) {
  return STATUS(NotSupported, "Not supported in RocksDB LITE");
}

#endif  // ROCKSDB_LITE

Status SuggestCompactRange(DB* db, const Slice* begin, const Slice* end) {
  return SuggestCompactRange(db, db->DefaultColumnFamily(), begin, end);
}

}  // namespace experimental
}  // namespace rocksdb
