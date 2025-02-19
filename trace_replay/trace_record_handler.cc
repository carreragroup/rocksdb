//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include "trace_replay/trace_record_handler.h"

#include "rocksdb/iterator.h"
#include "rocksdb/write_batch.h"

namespace ROCKSDB_NAMESPACE {

// TraceExecutionHandler
TraceExecutionHandler::TraceExecutionHandler(
    DB* db, const std::vector<ColumnFamilyHandle*>& handles)
    : TraceRecord::Handler(),
      db_(db),
      write_opts_(WriteOptions()),
      read_opts_(ReadOptions()) {
  assert(db != nullptr);
  assert(!handles.empty());
  cf_map_.reserve(handles.size());
  for (ColumnFamilyHandle* handle : handles) {
    assert(handle != nullptr);
    cf_map_.insert({handle->GetID(), handle});
  }
}

TraceExecutionHandler::~TraceExecutionHandler() { cf_map_.clear(); }

Status TraceExecutionHandler::Handle(const WriteQueryTraceRecord& record) {
  WriteBatch batch(record.GetWriteBatchRep().ToString());
  return db_->Write(write_opts_, &batch);
}

Status TraceExecutionHandler::Handle(const GetQueryTraceRecord& record) {
  auto it = cf_map_.find(record.GetColumnFamilyID());
  if (it == cf_map_.end()) {
    return Status::Corruption("Invalid Column Family ID.");
  }
  assert(it->second != nullptr);

  std::string value;
  Status s = db_->Get(read_opts_, it->second, record.GetKey(), &value);

  // Treat not found as ok and return other errors.
  return s.IsNotFound() ? Status::OK() : s;
}

Status TraceExecutionHandler::Handle(
    const IteratorSeekQueryTraceRecord& record) {
  auto it = cf_map_.find(record.GetColumnFamilyID());
  if (it == cf_map_.end()) {
    return Status::Corruption("Invalid Column Family ID.");
  }
  assert(it->second != nullptr);

  Iterator* single_iter = db_->NewIterator(read_opts_, it->second);

  switch (record.GetSeekType()) {
    case IteratorSeekQueryTraceRecord::kSeekForPrev: {
      single_iter->SeekForPrev(record.GetKey());
      break;
    }
    default: {
      single_iter->Seek(record.GetKey());
      break;
    }
  }
  Status s = single_iter->status();
  delete single_iter;
  return s;
}

Status TraceExecutionHandler::Handle(const MultiGetQueryTraceRecord& record) {
  std::vector<ColumnFamilyHandle*> handles;
  handles.reserve(record.GetColumnFamilyIDs().size());
  for (uint32_t cf_id : record.GetColumnFamilyIDs()) {
    auto it = cf_map_.find(cf_id);
    if (it == cf_map_.end()) {
      return Status::Corruption("Invalid Column Family ID.");
    }
    assert(it->second != nullptr);
    handles.push_back(it->second);
  }

  std::vector<Slice> keys = record.GetKeys();

  if (handles.empty() || keys.empty()) {
    return Status::InvalidArgument("Empty MultiGet cf_ids or keys.");
  }
  if (handles.size() != keys.size()) {
    return Status::InvalidArgument("MultiGet cf_ids and keys size mismatch.");
  }

  std::vector<std::string> values;
  std::vector<Status> ss = db_->MultiGet(read_opts_, handles, keys, &values);

  // Treat not found as ok, return other errors.
  for (Status s : ss) {
    if (!s.ok() && !s.IsNotFound()) {
      return s;
    }
  }
  return Status::OK();
}

}  // namespace ROCKSDB_NAMESPACE
