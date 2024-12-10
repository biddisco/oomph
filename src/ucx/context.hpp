/*
 * ghex-org
 *
 * Copyright (c) 2014-2023, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <vector>
#include <memory>

#include <boost/lockfree/queue.hpp>

#include <oomph/context.hpp>

// paths relative to backend
#include <../context_base.hpp>
#include <./config.hpp>
#include <rma_context.hpp>
#include <region.hpp>
#include <worker.hpp>
#include <request_state.hpp>
#include <request_data.hpp>
#include <address_db.hpp>

namespace oomph
{
#define OOMPH_UCX_TAG_BITS             32
#define OOMPH_UCX_RANK_BITS            32
#define OOMPH_UCX_ANY_SOURCE_MASK      0x0000000000000000ul
#define OOMPH_UCX_SPECIFIC_SOURCE_MASK 0x00000000fffffffful
#define OOMPH_UCX_TAG_MASK             0xffffffff00000000ul

class context_impl : public context_base
{
  public: // member types
    using region_type = region;
    using device_region_type = region;
    using heap_type = hwmalloc::heap<context_impl>;
    using worker_type = worker_t;

  private: // member types
    struct ucp_context_h_holder
    {
        ucp_context_h m_context;
        ~ucp_context_h_holder() { ucp_cleanup(m_context); }
    };

    using worker_vector = std::vector<std::unique_ptr<worker_type>>;

    template<typename T>
    using lockfree_queue = boost::lockfree::queue<T, boost::lockfree::fixed_sized<false>,
        boost::lockfree::allocator<std::allocator<void>>>;

    using recv_req_queue_type = lockfree_queue<detail::shared_request_state*>;

  private: // members
    type_erased_address_db_t                  m_db;
    ucp_context_h_holder                      m_context;
    heap_type                                 m_heap;
    rma_context                               m_rma_context;
    std::size_t                               m_req_size;
    std::unique_ptr<worker_type>              m_worker; // shared, serialized - per rank
    std::vector<std::unique_ptr<worker_type>> m_workers;

  public:
    ucx_mutex           m_mutex;
    recv_req_queue_type m_recv_req_queue;
    recv_req_queue_type m_cancel_recv_req_queue;

    friend struct worker_t;

  public: // ctors
    context_impl(MPI_Comm mpi_c, bool thread_safe, bool message_pool_never_free,
        std::size_t message_pool_reserve)
    : context_base(mpi_c, thread_safe)
#if defined OOMPH_UCX_USE_PMI
    , m_db(address_db_pmi(context_base::m_mpi_comm))
#else
    , m_db(address_db_mpi(context_base::m_mpi_comm))
#endif
    , m_heap{this, message_pool_never_free, message_pool_reserve}
    , m_rma_context()
    , m_recv_req_queue(128)
    , m_cancel_recv_req_queue(128)
    {
        // read run-time context
        ucp_config_t* config_ptr;
        OOMPH_CHECK_UCX_RESULT(ucp_config_read(NULL, NULL, &config_ptr));

        // set parameters
        ucp_params_t context_params;
        // define valid fields
        context_params.field_mask =
            UCP_PARAM_FIELD_FEATURES            // features
            | UCP_PARAM_FIELD_TAG_SENDER_MASK   // mask which gets sender endpoint from a tag
            | UCP_PARAM_FIELD_MT_WORKERS_SHARED // multi-threaded context: thread safety
            | UCP_PARAM_FIELD_ESTIMATED_NUM_EPS // estimated number of endpoints for this context
            | UCP_PARAM_FIELD_REQUEST_SIZE      // size of reserved space in a non-blocking request
            | UCP_PARAM_FIELD_REQUEST_INIT      // initialize request memory
            ;

        // features
        context_params.features = UCP_FEATURE_TAG   // tag matching
                                  | UCP_FEATURE_RMA // RMA access support
            ;
        // thread safety
        // this should be true if we have per-thread workers,
        // otherwise, if one worker is shared by all thread, it should be false
        // requires benchmarking.
        // This flag indicates if this context is shared by multiple workers from different threads.
        // If so, this context needs thread safety support; otherwise, the context does not need to
        // provide thread safety. For example, if the context is used by single worker, and that
        // worker is shared by multiple threads, this context does not need thread safety; if the
        // context is used by worker 1 and worker 2, and worker 1 is used by thread 1 and worker 2
        // is used by thread 2, then this context needs thread safety. Note that actual thread mode
        // may be different from mode passed to ucp_init. To get actual thread mode use
        // ucp_context_query.
        //context_params.mt_workers_shared = true;
        context_params.mt_workers_shared = this->m_thread_safe;
        // estimated number of connections
        // affects transport selection criteria and theresulting performance
        context_params.estimated_num_eps = m_db.est_size();
        // mask
        // mask which specifies particular bits of the tag which can uniquely identify
        // the sender (UCP endpoint) in tagged operations.
        //context_params.tag_sender_mask  = 0x00000000fffffffful;
        context_params.tag_sender_mask = 0xfffffffffffffffful;
        // additional usable request size
        context_params.request_size = request_data_size::value;
        // initialize a valid request_data object within the ucx provided memory
        context_params.request_init = &request_data::init;

        // initialize UCP
        OOMPH_CHECK_UCX_RESULT(ucp_init(&context_params, config_ptr, &m_context.m_context));
        ucp_config_release(config_ptr);

        // check the actual parameters
        ucp_context_attr_t attr;
        attr.field_mask = UCP_ATTR_FIELD_REQUEST_SIZE | // internal request size
                          UCP_ATTR_FIELD_THREAD_MODE;   // thread safety
        ucp_context_query(m_context.m_context, &attr);
        m_req_size = attr.request_size;
        if (this->m_thread_safe && attr.thread_mode != UCS_THREAD_MODE_MULTI)
            throw std::runtime_error("ucx cannot be used with multi-threaded context");

        // make shared worker
        // use single-threaded UCX mode, as per developer advice
        // https://github.com/openucx/ucx/issues/4609
        m_worker.reset(new worker_type{get(), m_db, UCS_THREAD_MODE_SINGLE});

        // intialize database
        m_db.init(m_worker->address());

        m_rma_context.set_ucp_context(m_context.m_context);
    }

    ~context_impl();

    context_impl(context_impl&&) = delete;
    context_impl& operator=(context_impl&&) = delete;

    ucp_context_h get() const noexcept { return m_context.m_context; }

    region make_region(void* ptr) { return {ptr}; }

    auto& get_heap() noexcept { return m_heap; }

    communicator_impl* get_communicator();

    void progress()
    {
        //{
        //    ucx_lock lock(m_mutex);
        //    while (ucp_worker_progress(m_worker->get())) {}
        //}
        if (m_mutex.try_lock())
        {
            ucp_worker_progress(m_worker->get());
            m_mutex.unlock();
        }
        m_recv_req_queue.consume_all(
            [](detail::shared_request_state* req)
            {
                auto ptr = req->release_self_ref();
                req->invoke_cb();
            });
    }

    void enqueue_recv(detail::shared_request_state* d)
    {
        while (!m_recv_req_queue.push(d)) {}
    }

    void enqueue_cancel_recv(detail::shared_request_state* d)
    {
        while (!m_cancel_recv_req_queue.push(d)) {}
    }

    bool cancel_recv(detail::shared_request_state* s)
    {
        if (m_thread_safe) m_mutex.lock();
        ucp_request_cancel(m_worker->get(), s->m_ucx_ptr);
        while (ucp_worker_progress(m_worker->get())) {}
        // check whether the cancelled callback was enqueued by consuming all queued cancelled
        // callbacks and putting them in a temporary vector
        static thread_local bool                                       found = false;
        static thread_local std::vector<detail::shared_request_state*> m_cancel_recv_req_vec;
        m_cancel_recv_req_vec.clear();
        m_cancel_recv_req_queue.consume_all(
            [this, s, found_ptr = &found](detail::shared_request_state* r)
            {
                if (r == s) *found_ptr = true;
                else
                    m_cancel_recv_req_vec.push_back(r);
            });
        // re-enqueue all callbacks which were not identical with the current callback
        for (auto x : m_cancel_recv_req_vec)
            while (!m_cancel_recv_req_queue.push(x)) {}
        if (m_thread_safe) m_mutex.unlock();

        // delete callback here if it was actually cancelled
        if (found)
        {
            auto ptr = s->release_self_ref();
            s->set_canceled();
            void* ucx_req = s->m_ucx_ptr;
            // destroy request
            request_data::get(ucx_req)->destroy();
            if (m_thread_safe) m_mutex.lock();
            ucp_request_free(ucx_req);
            if (m_thread_safe) m_mutex.unlock();
        }
        return found;
    }

    const char* get_transport_option(const std::string& opt);

    unsigned int num_tag_bits() const noexcept { return OOMPH_UCX_TAG_BITS; }
};

template<>
inline region
register_memory<context_impl>(context_impl& c, void* ptr, std::size_t)
{
    return c.make_region(ptr);
}

#if OOMPH_ENABLE_DEVICE
template<>
inline region
register_device_memory<context_impl>(context_impl& c, int, void* ptr, std::size_t)
{
    return c.make_region(ptr);
}
#endif

} // namespace oomph
