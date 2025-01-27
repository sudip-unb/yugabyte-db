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
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>
#include "yb/rocksdb/db/transaction_log_impl.h"
#include "yb/rocksdb/db/write_batch_internal.h"
#include "yb/rocksdb/util/file_reader_writer.h"

using std::unique_ptr;

namespace rocksdb {

TransactionLogIteratorImpl::TransactionLogIteratorImpl(
    const std::string& dir, const DBOptions* options,
    const TransactionLogIterator::ReadOptions& read_options,
    const EnvOptions& soptions, const SequenceNumber seq,
    std::unique_ptr<VectorLogPtr> files, VersionSet const* const versions)
    : dir_(dir),
      options_(options),
      read_options_(read_options),
      soptions_(soptions),
      startingSequenceNumber_(seq),
      files_(std::move(files)),
      started_(false),
      isValid_(false),
      currentFileIndex_(0),
      currentBatchSeq_(0),
      currentLastSeq_(0),
      versions_(versions) {
  assert(files_ != nullptr);
  assert(versions_ != nullptr);

  reporter_.env = options_->env;
  reporter_.info_log = options_->info_log.get();
  SeekToStartSequence(); // Seek till starting sequence
}

Status TransactionLogIteratorImpl::OpenLogFile(
    const LogFile* logFile, unique_ptr<SequentialFileReader>* file_reader) {
  Env* env = options_->env;
  unique_ptr<SequentialFile> file;
  Status s;
  if (logFile->Type() == kArchivedLogFile) {
    std::string fname = ArchivedLogFileName(dir_, logFile->LogNumber());
    s = env->NewSequentialFile(fname, &file, soptions_);
  } else {
    std::string fname = LogFileName(dir_, logFile->LogNumber());
    s = env->NewSequentialFile(fname, &file, soptions_);
    if (!s.ok()) {
      //  If cannot open file in DB directory.
      //  Try the archive dir, as it could have moved in the meanwhile.
      fname = ArchivedLogFileName(dir_, logFile->LogNumber());
      s = env->NewSequentialFile(fname, &file, soptions_);
    }
  }
  if (s.ok()) {
    file_reader->reset(new SequentialFileReader(std::move(file)));
  }
  return s;
}

BatchResult TransactionLogIteratorImpl::GetBatch()  {
  assert(isValid_);  //  cannot call in a non valid state.
  BatchResult result;
  result.sequence = currentBatchSeq_;
  result.writeBatchPtr = std::move(currentBatch_);
  return result;
}

Status TransactionLogIteratorImpl::status() {
  return currentStatus_;
}

bool TransactionLogIteratorImpl::Valid() {
  return started_ && isValid_;
}

bool TransactionLogIteratorImpl::RestrictedRead(
    Slice* record,
    std::string* scratch) {
  // Don't read if no more complete entries to read from logs
  if (currentLastSeq_ >= versions_->LastSequence()) {
    return false;
  }
  return currentLogReader_->ReadRecord(record, scratch);
}

void TransactionLogIteratorImpl::SeekToStartSequence(
    uint64_t startFileIndex,
    bool strict) {
  std::string scratch;
  Slice record;
  started_ = false;
  isValid_ = false;
  if (files_->size() <= startFileIndex) {
    return;
  }
  Status s = OpenLogReader(files_->at(startFileIndex).get());
  if (!s.ok()) {
    currentStatus_ = s;
    reporter_.Info(currentStatus_.ToString().c_str());
    return;
  }
  while (RestrictedRead(&record, &scratch)) {
    if (record.size() < 12) {
      reporter_.Corruption(
        record.size(), STATUS(Corruption, "very small log record"));
      continue;
    }
    UpdateCurrentWriteBatch(record);
    if (currentLastSeq_ >= startingSequenceNumber_) {
      if (strict && currentBatchSeq_ != startingSequenceNumber_) {
        currentStatus_ = STATUS(Corruption, "Gap in sequence number. Could not "
                                            "seek to required sequence number");
        reporter_.Info(currentStatus_.ToString().c_str());
        return;
      } else if (strict) {
        reporter_.Info("Could seek required sequence number. Iterator will "
                       "continue.");
      }
      isValid_ = true;
      started_ = true; // set started_ as we could seek till starting sequence
      return;
    } else {
      isValid_ = false;
    }
  }

  // Could not find start sequence in first file. Normally this must be the
  // only file. Otherwise log the error and let the iterator return next entry
  // If strict is set, we want to seek exactly till the start sequence and it
  // should have been present in the file we scanned above
  if (strict) {
    currentStatus_ = STATUS(Corruption, "Gap in sequence number. Could not "
                                        "seek to required sequence number");
    reporter_.Info(currentStatus_.ToString().c_str());
  } else if (files_->size() != 1) {
    currentStatus_ = STATUS(Corruption, "Start sequence was not found, "
                                        "skipping to the next available");
    reporter_.Info(currentStatus_.ToString().c_str());
    // Let NextImpl find the next available entry. started_ remains false
    // because we don't want to check for gaps while moving to start sequence
    NextImpl(true);
  }
}

void TransactionLogIteratorImpl::Next() {
  return NextImpl(false);
}

void TransactionLogIteratorImpl::NextImpl(bool internal) {
  std::string scratch;
  Slice record;
  isValid_ = false;
  if (!internal && !started_) {
    // Runs every time until we can seek to the start sequence
    return SeekToStartSequence();
  }
  while(true) {
    assert(currentLogReader_);
    if (currentLogReader_->IsEOF()) {
      currentLogReader_->UnmarkEOF();
    }
    while (RestrictedRead(&record, &scratch)) {
      if (record.size() < 12) {
        reporter_.Corruption(
          record.size(), STATUS(Corruption, "very small log record"));
        continue;
      } else {
        // started_ should be true if called by application
        assert(internal || started_);
        // started_ should be false if called internally
        assert(!internal || !started_);
        UpdateCurrentWriteBatch(record);
        if (internal && !started_) {
          started_ = true;
        }
        return;
      }
    }

    // Open the next file
    if (currentFileIndex_ < files_->size() - 1) {
      ++currentFileIndex_;
      Status s = OpenLogReader(files_->at(currentFileIndex_).get());
      if (!s.ok()) {
        isValid_ = false;
        currentStatus_ = s;
        return;
      }
    } else {
      isValid_ = false;
      if (currentLastSeq_ == versions_->LastSequence()) {
        currentStatus_ = Status::OK();
      } else {
        currentStatus_ = STATUS(Corruption, "NO MORE DATA LEFT");
      }
      return;
    }
  }
}

bool TransactionLogIteratorImpl::IsBatchExpected(
    const WriteBatch* batch,
    const SequenceNumber expectedSeq) {
  assert(batch);
  SequenceNumber batchSeq = WriteBatchInternal::Sequence(batch);
  if (batchSeq != expectedSeq) {
    char buf[200];
    snprintf(buf, sizeof(buf),
             "Discontinuity in log records. Got seq=%" PRIu64
             ", Expected seq=%" PRIu64 ", Last flushed seq=%" PRIu64
             ".Log iterator will reseek the correct batch.",
             batchSeq, expectedSeq, versions_->LastSequence());
    reporter_.Info(buf);
    return false;
  }
  return true;
}

void TransactionLogIteratorImpl::UpdateCurrentWriteBatch(const Slice& record) {
  std::unique_ptr<WriteBatch> batch(new WriteBatch());
  WriteBatchInternal::SetContents(batch.get(), record);

  SequenceNumber expectedSeq = currentLastSeq_ + 1;
  // If the iterator has started, then confirm that we get continuous batches
  if (started_ && !IsBatchExpected(batch.get(), expectedSeq)) {
    // Seek to the batch having expected sequence number
    if (expectedSeq < files_->at(currentFileIndex_)->StartSequence()) {
      // Expected batch must lie in the previous log file
      // Avoid underflow.
      if (currentFileIndex_ != 0) {
        currentFileIndex_--;
      }
    }
    startingSequenceNumber_ = expectedSeq;
    // currentStatus_ will be set to Ok if reseek succeeds
    currentStatus_ = STATUS(NotFound, "Gap in sequence numbers");
    return SeekToStartSequence(currentFileIndex_, true);
  }

  currentBatchSeq_ = WriteBatchInternal::Sequence(batch.get());
  currentLastSeq_ = currentBatchSeq_ +
                    WriteBatchInternal::Count(batch.get()) - 1;
  // currentBatchSeq_ can only change here
  assert(currentLastSeq_ <= versions_->LastSequence());

  currentBatch_ = std::move(batch);
  isValid_ = true;
  currentStatus_ = Status::OK();
}

Status TransactionLogIteratorImpl::OpenLogReader(const LogFile* logFile) {
  unique_ptr<SequentialFileReader> file;
  Status s = OpenLogFile(logFile, &file);
  if (!s.ok()) {
    return s;
  }
  assert(file);
  currentLogReader_.reset(new log::Reader(options_->info_log,
                                          std::move(file),
                                          &reporter_,
                                          read_options_.verify_checksums_,
                                          0,
                                          logFile->LogNumber()));
  return Status::OK();
}
}  //  namespace rocksdb
#endif  // ROCKSDB_LITE
