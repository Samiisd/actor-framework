/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2019 Dominik Charousset                                     *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#pragma once

#include <caf/logger.hpp>
#include <unordered_map>

#include "caf/byte.hpp"
#include "caf/ip_endpoint.hpp"
#include "caf/net/endpoint_manager.hpp"
#include "caf/net/fwd.hpp"
#include "caf/net/packet_writer_decorator.hpp"
#include "caf/net/transport_worker.hpp"
#include "caf/span.hpp"
#include "caf/unit.hpp"

namespace caf {
namespace net {

/// implements a dispatcher that dispatches between transport and workers.
template <class Transport, class IdType>
class transport_worker_dispatcher {
public:
  // -- member types -----------------------------------------------------------

  using id_type = IdType;

  using transport_type = Transport;

  using factory_type = typename transport_type::factory_type;

  using application_type = typename factory_type::application_type;

  using worker_type = transport_worker<application_type, id_type>;

  using worker_ptr = transport_worker_ptr<application_type, id_type>;

  // -- constructors, destructors, and assignment operators --------------------

  transport_worker_dispatcher(transport_type& transport, factory_type factory)
    : factory_(std::move(factory)), transport_(&transport) {
    // nop
  }

  // -- member functions -------------------------------------------------------

  template <class Parent>
  error init(Parent&) {
    CAF_ASSERT(workers_by_id_.empty());
    return none;
  }

  template <class Parent>
  error handle_data(Parent& parent, span<byte> data, id_type id) {
    auto worker = find_by_id(id);
    if (!worker) {
      // TODO: where to get id_type from here?
      add_new_worker(parent, node_id{}, id);
      worker = find_by_id(id);
    }
    return worker->handle_data(parent, data);
  }

  template <class Parent>
  void write_message(Parent& parent,
                     std::unique_ptr<endpoint_manager_queue::message> msg) {
    auto receiver = msg->receiver;
    if (!receiver)
      return;
    auto nid = receiver->node();
    auto worker = find_by_node(nid);
    if (!worker) {
      // TODO: where to get id_type from here?
      add_new_worker(parent, nid, id_type{});
      worker = find_by_node(nid);
    }
    worker->write_message(parent, std::move(msg));
  }

  template <class Parent>
  void resolve(Parent& parent, const uri& locator, const actor& listener) {
    if (auto worker = find_by_node(make_node_id(locator)))
      worker->resolve(parent, locator.path(), listener);
  }

  template <class Parent>
  void new_proxy(Parent& parent, const node_id& nid, actor_id id) {
    if (auto worker = find_by_node(nid))
      worker->new_proxy(parent, nid, id);
  }

  template <class Parent>
  void local_actor_down(Parent& parent, const node_id& nid, actor_id id,
                        error reason) {
    if (auto worker = find_by_node(nid))
      worker->local_actor_down(parent, nid, id, std::move(reason));
  }

  template <class... Ts>
  void set_timeout(uint64_t timeout_id, id_type id, Ts&&...) {
    workers_by_timeout_id_.emplace(timeout_id, workers_by_id_.at(id));
  }

  template <class Parent>
  void timeout(Parent& parent, atom_value value, uint64_t id) {
    auto worker = workers_by_timeout_id_.at(id);
    worker->timeout(parent, value, id);
    workers_by_timeout_id_.erase(id);
  }

  void handle_error(sec error) {
    for (const auto& p : workers_by_id_) {
      auto worker = p.second;
      worker->handle_error(error);
    }
  }

  template <class Parent>
  error add_new_worker(Parent& parent, node_id node, id_type id) {
    auto application = factory_.make();
    auto worker = std::make_shared<worker_type>(std::move(application), id);
    if (auto err = worker->init(parent))
      return err;
    workers_by_id_.emplace(std::move(id), worker);
    workers_by_node_.emplace(std::move(node), std::move(worker));
    return none;
  }

private:
  worker_ptr find_by_node(const node_id& nid) {
    if (workers_by_node_.empty())
      return nullptr;
    auto it = workers_by_node_.find(nid);
    if (it == workers_by_node_.end()) {
      CAF_LOG_ERROR("could not find worker by node: " << CAF_ARG(nid));
      return nullptr;
    }
    return it->second;
  }

  worker_ptr find_by_id(const IdType& id) {
    if (workers_by_id_.empty())
      return nullptr;
    auto it = workers_by_id_.find(id);
    if (it == workers_by_id_.end()) {
      CAF_LOG_ERROR("could not find worker by node: " << CAF_ARG(id));
      return nullptr;
    }
    return it->second;
  }

  // -- worker lookups ---------------------------------------------------------

  std::unordered_map<id_type, worker_ptr> workers_by_id_;
  std::unordered_map<node_id, worker_ptr> workers_by_node_;
  std::unordered_map<uint64_t, worker_ptr> workers_by_timeout_id_;

  factory_type factory_;
  transport_type* transport_;
};

} // namespace net
} // namespace caf
