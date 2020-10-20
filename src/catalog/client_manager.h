/*
 * client_manager.h
 * Copyright (C) 4paradigm.com 2020
 * Author denglong
 * Date 2020-09-14
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SRC_CATALOG_CLIENT_MANAGER_H_
#define SRC_CATALOG_CLIENT_MANAGER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/random.h"
#include "base/spinlock.h"
#include "client/tablet_client.h"
#include "storage/schema.h"

namespace rtidb {
namespace catalog {

using TablePartitions = ::google::protobuf::RepeatedPtrField<::rtidb::nameserver::TablePartition>;

class ClientWrapper {
 public:
    explicit ClientWrapper(const std::string& name) : name_(name), tablet_client_() {}

    ClientWrapper(const std::string& name, const std::shared_ptr<::rtidb::client::TabletClient>& client)
        : name_(name), tablet_client_(client) {}

    std::shared_ptr<::rtidb::client::TabletClient> GetClient() {
        return std::atomic_load_explicit(&tablet_client_, std::memory_order_relaxed);
    }

    bool UpdateClient(const std::string& endpoint) {
        auto client = std::make_shared<::rtidb::client::TabletClient>(name_, endpoint);
        if (client->Init() != 0) {
            return false;
        }
        std::atomic_store_explicit(&tablet_client_, client, std::memory_order_relaxed);
        return true;
    }

    bool UpdateClient(const std::shared_ptr<::rtidb::client::TabletClient>& client) {
        std::atomic_store_explicit(&tablet_client_, client, std::memory_order_relaxed);
        return true;
    }

 private:
    std::string name_;
    std::shared_ptr<::rtidb::client::TabletClient> tablet_client_;
};

class PartitionClientManager {
 public:
    PartitionClientManager(uint32_t pid, const std::shared_ptr<ClientWrapper>& leader,
                           const std::vector<std::shared_ptr<ClientWrapper>>& followers);

    inline std::shared_ptr<::rtidb::client::TabletClient> GetLeader() const { return leader_->GetClient(); }

    std::shared_ptr<::rtidb::client::TabletClient> GetFollower();

 private:
    uint32_t pid_;
    std::shared_ptr<ClientWrapper> leader_;
    std::vector<std::shared_ptr<ClientWrapper>> followers_;
    ::rtidb::base::Random rand_;
};

class ClientManager;

class TableClientManager {
 public:
    TableClientManager(const TablePartitions& partitions, const ClientManager& client_manager);

    TableClientManager(const ::rtidb::storage::TableSt& table_st, const ClientManager& client_manager);

    std::shared_ptr<PartitionClientManager> GetPartitionClientManager(uint32_t pid) const {
        if (pid < partition_managers_.size()) {
            return std::atomic_load_explicit(&partition_managers_[pid], std::memory_order_relaxed);
        }
        return std::shared_ptr<PartitionClientManager>();
    }

    bool UpdatePartitionClientManager(const ::rtidb::storage::PartitionSt& partition,
                                      const ClientManager& client_manager);

    std::shared_ptr<::rtidb::client::TabletClient> GetTablets(uint32_t pid) const {
        auto partition_manager = GetPartitionClientManager(pid);
        if (partition_manager) {
            return partition_manager->GetLeader();
        }
        return std::shared_ptr<::rtidb::client::TabletClient>();
    }

 private:
    std::vector<std::shared_ptr<PartitionClientManager>> partition_managers_;
};

class ClientManager {
 public:
    std::shared_ptr<ClientWrapper> GetClient(const std::string& name) const;

    bool UpdateClient(const std::map<std::string, std::string>& real_ep_map);

    bool UpdateClient(const std::map<std::string, std::shared_ptr<::rtidb::client::TabletClient>>& tablet_clients);

 private:
    std::map<std::string, std::string> real_endpoint_map_;
    std::map<std::string, std::shared_ptr<ClientWrapper>> clients_;
    mutable ::rtidb::base::SpinMutex mu_;
};

}  // namespace catalog
}  // namespace rtidb
#endif  // SRC_CATALOG_CLIENT_MANAGER_H_