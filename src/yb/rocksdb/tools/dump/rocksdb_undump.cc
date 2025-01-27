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

#if !(defined GFLAGS) || defined(ROCKSDB_LITE)

#include <cstdio>
int main() {
#ifndef GFLAGS
  fprintf(stderr, "Please install gflags to run rocksdb tools\n");
#endif
#ifdef ROCKSDB_LITE
  fprintf(stderr, "DbUndumpTool is not supported in ROCKSDB_LITE\n");
#endif
  return 1;
}

#else

#include "yb/util/flags.h"
#include "yb/rocksdb/convenience.h"
#include "yb/rocksdb/db_dump_tool.h"

DEFINE_string(dump_location, "", "Path to the dump file that will be loaded");
DEFINE_string(db_path, "", "Path to the db that we will undump the file into");
DEFINE_bool(compact, false, "Compact the db after loading the dumped file");
DEFINE_string(db_options, "",
              "Options string used to open the database that will be loaded");

int main(int argc, char **argv) {
  GFLAGS::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_db_path == "" || FLAGS_dump_location == "") {
    fprintf(stderr, "Please set --db_path and --dump_location\n");
    return 1;
  }

  rocksdb::UndumpOptions undump_options;
  undump_options.db_path = FLAGS_db_path;
  undump_options.dump_location = FLAGS_dump_location;
  undump_options.compact_db = FLAGS_compact;

  rocksdb::Options db_options;
  if (FLAGS_db_options != "") {
    rocksdb::Options parsed_options;
    rocksdb::Status s = rocksdb::GetOptionsFromString(
        db_options, FLAGS_db_options, &parsed_options);
    if (!s.ok()) {
      fprintf(stderr, "Cannot parse provided db_options\n");
      return 1;
    }
    db_options = parsed_options;
  }

  rocksdb::DbUndumpTool tool;
  if (!tool.Run(undump_options, db_options)) {
    return 1;
  }
  return 0;
}
#endif  // !(defined GFLAGS) || defined(ROCKSDB_LITE)
