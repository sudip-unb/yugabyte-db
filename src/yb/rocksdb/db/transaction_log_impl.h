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

#ifndef ROCKSDB_LITE
#pragma once
#include <vector>

#include "yb/rocksdb/env.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/types.h"
#include "yb/rocksdb/transaction_log.h"
#include "yb/rocksdb/db/version_set.h"
#include "yb/rocksdb/db/log_reader.h"
#include "yb/rocksdb/db/filename.h"
#include "yb/rocksdb/port/port.h"

namespace rocksdb {

class LogFileImpl : public LogFile {
 public:
  LogFileImpl(uint64_t logNum, WalFileType logType, SequenceNumber startSeq,
              uint64_t sizeBytes) :
    logNumber_(logNum),
    type_(logType),
    startSequence_(startSeq),
    sizeFileBytes_(sizeBytes) {
  }

  std::string PathName() const override {
    if (type_ == kArchivedLogFile) {
      return ArchivedLogFileName("", logNumber_);
    }
    return LogFileName("", logNumber_);
  }

  uint64_t LogNumber() const override { return logNumber_; }

  WalFileType Type() const override { return type_; }

  SequenceNumber StartSequence() const override { return startSequence_; }

  uint64_t SizeFileBytes() const override { return sizeFileBytes_; }

  bool operator < (const LogFile& that) const {
    return LogNumber() < that.LogNumber();
  }

 private:
  uint64_t logNumber_;
  WalFileType type_;
  SequenceNumber startSequence_;
  uint64_t sizeFileBytes_;

};

class TransactionLogIteratorImpl : public TransactionLogIterator {
 public:
  TransactionLogIteratorImpl(
      const std::string& dir, const DBOptions* options,
      const TransactionLogIterator::ReadOptions& read_options,
      const EnvOptions& soptions, const SequenceNumber seqNum,
      std::unique_ptr<VectorLogPtr> files, VersionSet const* const versions);

  virtual bool Valid() override;

  virtual void Next() override;

  virtual Status status() override;

  virtual BatchResult GetBatch() override;

 private:
  const std::string& dir_;
  const DBOptions* options_;
  const TransactionLogIterator::ReadOptions read_options_;
  const EnvOptions& soptions_;
  SequenceNumber startingSequenceNumber_;
  std::unique_ptr<VectorLogPtr> files_;
  bool started_;
  bool isValid_;  // not valid when it starts of.
  Status currentStatus_;
  size_t currentFileIndex_;
  std::unique_ptr<WriteBatch> currentBatch_;
  std::unique_ptr<log::Reader> currentLogReader_;
  Status OpenLogFile(const LogFile* logFile,
                     std::unique_ptr<SequentialFileReader>* file);

  struct LogReporter : public log::Reader::Reporter {
    Env* env;
    Logger* info_log;
    virtual void Corruption(size_t bytes, const Status& s) override {
      RLOG(InfoLogLevel::ERROR_LEVEL, info_log,
          "dropping %" ROCKSDB_PRIszt " bytes; %s", bytes,
          s.ToString().c_str());
    }
    virtual void Info(const char* s) {
      RLOG(InfoLogLevel::INFO_LEVEL, info_log, "%s", s);
    }
  } reporter_;

  SequenceNumber currentBatchSeq_; // sequence number at start of current batch
  SequenceNumber currentLastSeq_; // last sequence in the current batch
  // Used only to get latest seq. num
  // TODO(icanadi) can this be just a callback?
  VersionSet const* const versions_;

  // Reads from transaction log only if the writebatch record has been written
  bool RestrictedRead(Slice* record, std::string* scratch);
  // Seeks to startingSequenceNumber reading from startFileIndex in files_.
  // If strict is set,then must get a batch starting with startingSequenceNumber
  void SeekToStartSequence(uint64_t startFileIndex = 0, bool strict = false);
  // Implementation of Next. SeekToStartSequence calls it internally with
  // internal=true to let it find next entry even if it has to jump gaps because
  // the iterator may start off from the first available entry but promises to
  // be continuous after that
  void NextImpl(bool internal = false);
  // Check if batch is expected, else return false
  bool IsBatchExpected(const WriteBatch* batch, SequenceNumber expectedSeq);
  // Update current batch if a continuous batch is found, else return false
  void UpdateCurrentWriteBatch(const Slice& record);
  Status OpenLogReader(const LogFile* file);
};
}  //  namespace rocksdb
#endif  // ROCKSDB_LITE
