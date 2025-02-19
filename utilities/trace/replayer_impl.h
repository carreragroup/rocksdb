//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#pragma once
#ifndef ROCKSDB_LITE

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "rocksdb/rocksdb_namespace.h"
#include "rocksdb/trace_record.h"
#include "rocksdb/utilities/replayer.h"
#include "trace_replay/trace_replay.h"

namespace ROCKSDB_NAMESPACE {

class ColumnFamilyHandle;
class DB;
class Env;
class TraceReader;
class TraceRecord;
class Status;

struct ReplayOptions;

class ReplayerImpl : public Replayer {
 public:
  ReplayerImpl(DB* db, const std::vector<ColumnFamilyHandle*>& handles,
               std::unique_ptr<TraceReader>&& reader);
  ~ReplayerImpl() override;

  using Replayer::Prepare;
  Status Prepare() override;

  using Replayer::Next;
  Status Next(std::unique_ptr<TraceRecord>* record) override;

  using Replayer::Execute;
  Status Execute(const std::unique_ptr<TraceRecord>& record) override;
  Status Execute(std::unique_ptr<TraceRecord>&& record) override;

  using Replayer::Replay;
  Status Replay(const ReplayOptions& options) override;

  using Replayer::GetHeaderTimestamp;
  uint64_t GetHeaderTimestamp() const override;

 private:
  Status ReadHeader(Trace* header);
  Status ReadFooter(Trace* footer);
  Status ReadTrace(Trace* trace);

  // Generic function to convert a Trace to TraceRecord.
  static Status DecodeTraceRecord(Trace* trace, int trace_file_version,
                                  std::unique_ptr<TraceRecord>* record);

  // Generic function to execute a Trace in a thread pool.
  static void BackgroundWork(void* arg);

  Env* env_;
  std::unique_ptr<TraceReader> trace_reader_;
  // When reading the trace header, the trace file version can be parsed.
  // Replayer will use different decode method to get the trace content based
  // on different trace file version.
  int trace_file_version_;
  std::mutex mutex_;
  std::atomic<bool> prepared_;
  std::atomic<bool> trace_end_;
  uint64_t header_ts_;
  std::unique_ptr<TraceRecord::Handler> exec_handler_;
};

// The passin arg of MultiThreadRepkay for each trace record.
struct ReplayerWorkerArg {
  Trace trace_entry;
  int trace_file_version;
  // Handler to execute TraceRecord.
  TraceRecord::Handler* handler;
  // Callback function to report the error status and the timestamp of the
  // TraceRecord.
  std::function<void(Status, uint64_t)> error_cb;
};

}  // namespace ROCKSDB_NAMESPACE
#endif  // ROCKSDB_LITE
