/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <boost/algorithm/string.hpp>
#include <boost/filesystem/operations.hpp>

#include <gtest/gtest.h>

#include <osquery/events.h>
#include <osquery/tables.h>

#include "osquery/database/db_handle.h"

namespace osquery {

//const std::string kTestingEventsDBPath = "/tmp/rocksdb-osquery-testevents";

class EventsDatabaseTests : public ::testing::Test {};

class FakeEventPublisher
    : public EventPublisher<SubscriptionContext, EventContext> {
  DECLARE_PUBLISHER("FakePublisher");
};

class FakeEventSubscriber : public EventSubscriber<FakeEventPublisher> {
 public:
  FakeEventSubscriber() { setName("FakeSubscriber"); }
  /// Add a fake event at time t
  Status testAdd(int t) {
    Row r;
    r["testing"] = "hello from space";
    return add(r, t);
  }
};

TEST_F(EventsDatabaseTests, test_event_module_id) {
  auto sub = std::make_shared<FakeEventSubscriber>();
  sub->doNotExpire();

  // Not normally available outside of EventSubscriber->Add().
  auto event_id1 = sub->getEventID();
  EXPECT_EQ(event_id1, "1");
  auto event_id2 = sub->getEventID();
  EXPECT_EQ(event_id2, "2");
}

TEST_F(EventsDatabaseTests, test_event_add) {
  auto sub = std::make_shared<FakeEventSubscriber>();
  auto status = sub->testAdd(1);
  EXPECT_TRUE(status.ok());
}

TEST_F(EventsDatabaseTests, test_record_indexing) {
  auto sub = std::make_shared<FakeEventSubscriber>();
  auto status = sub->testAdd(2);
  status = sub->testAdd(11);
  status = sub->testAdd(61);
  status = sub->testAdd((1 * 3600) + 1);
  status = sub->testAdd((2 * 3600) + 1);

  // An "all" range, will pick up everything in the largest index.
  auto indexes = sub->getIndexes(0, 3 * 3600);
  auto output = boost::algorithm::join(indexes, ", ");
  EXPECT_EQ(output, "3600.0, 3600.1, 3600.2");

  // Restrict range to "most specific".
  indexes = sub->getIndexes(0, 5);
  output = boost::algorithm::join(indexes, ", ");
  EXPECT_EQ(output, "10.0");

  // Get a mix of indexes for the lower bounding.
  indexes = sub->getIndexes(2, (3 * 3600));
  output = boost::algorithm::join(indexes, ", ");
  EXPECT_EQ(output, "10.0, 10.1, 3600.1, 3600.2, 60.1");

  // Rare, but test ONLY intermediate indexes.
  indexes = sub->getIndexes(2, (3 * 3600), 1);
  output = boost::algorithm::join(indexes, ", ");
  EXPECT_EQ(output, "60.0, 60.1, 60.120, 60.60");

  // Add specific indexes to the upper bound.
  status = sub->testAdd((2 * 3600) + 11);
  status = sub->testAdd((2 * 3600) + 61);
  indexes = sub->getIndexes(2 * 3600, (2 * 3600) + 62);
  output = boost::algorithm::join(indexes, ", ");
  EXPECT_EQ(output, "10.726, 60.120");

  // Request specific lower and upper bounding.
  indexes = sub->getIndexes(2, (2 * 3600) + 62);
  output = boost::algorithm::join(indexes, ", ");
  EXPECT_EQ(output, "10.0, 10.1, 10.726, 3600.1, 60.1, 60.120");
}

TEST_F(EventsDatabaseTests, test_record_range) {
  auto sub = std::make_shared<FakeEventSubscriber>();

  // Search within a specific record range.
  auto indexes = sub->getIndexes(0, 10);
  auto records = sub->getRecords(indexes);
  EXPECT_EQ(records.size(), 2U); // 1, 2

  // Search within a large bound.
  indexes = sub->getIndexes(3, 3601);
  // This will include the 0-10 bucket meaning 1, 2 will show up.
  records = sub->getRecords(indexes);
  EXPECT_EQ(records.size(), 5U); // 1, 2, 11, 61, 3601

  // Get all of the records.
  indexes = sub->getIndexes(0, 3 * 3600);
  records = sub->getRecords(indexes);
  EXPECT_EQ(records.size(), 8U); // 1, 2, 11, 61, 3601, 7201, 7211, 7261

  // stop = 0 is an alias for everything.
  indexes = sub->getIndexes(0, 0);
  records = sub->getRecords(indexes);
  EXPECT_EQ(records.size(), 8U);
}

TEST_F(EventsDatabaseTests, test_record_expiration) {
  auto sub = std::make_shared<FakeEventSubscriber>();

  // No expiration
  auto indexes = sub->getIndexes(0, 5000);
  auto records = sub->getRecords(indexes);
  EXPECT_EQ(records.size(), 5U); // 1, 2, 11, 61, 3601

  sub->expire_events_ = true;
  sub->expire_time_ = 10;
  indexes = sub->getIndexes(0, 5000);
  records = sub->getRecords(indexes);
  EXPECT_EQ(records.size(), 3U); // 11, 61, 3601

  indexes = sub->getIndexes(0, 5000, 0);
  records = sub->getRecords(indexes);
  EXPECT_EQ(records.size(), 3U); // 11, 61, 3601

  indexes = sub->getIndexes(0, 5000, 1);
  records = sub->getRecords(indexes);
  EXPECT_EQ(records.size(), 3U); // 11, 61, 3601

  indexes = sub->getIndexes(0, 5000, 2);
  records = sub->getRecords(indexes);
  EXPECT_EQ(records.size(), 3U); // 11, 61, 3601

  // Check that get/deletes did not act on cache.
  sub->expire_time_ = 0;
  indexes = sub->getIndexes(0, 5000);
  records = sub->getRecords(indexes);
  EXPECT_EQ(records.size(), 3U); // 11, 61, 3601
}
}
