#include "daemon_tx_async_worker.h"

/* Bodies moved verbatim from daemon_tx_async_worker.h. */

void daemon_tx_async_worker_init(DaemonTxAsyncWorker *async)
{
    if (!async)
        return;

    std::lock_guard<std::mutex> guard(async->lock);
    daemon_tx_worker_init(&async->worker);
    async->send_fn = NULL;
    async->send_ctx = NULL;
    async->cad_probe_fn = NULL;
    async->cad_probe_ctx = NULL;
    async->cad_sleep_fn = NULL;
    async->cad_sleep_ctx = NULL;
    async->result_fn = NULL;
    async->result_ctx = NULL;
    async->running = false;
    async->stop_requested = false;
}

void daemon_tx_async_worker_configure(DaemonTxAsyncWorker *async,
                                                    DaemonTxSendFn send_fn,
                                                    void *send_ctx,
                                                    DaemonTxWorkerResultFn result_fn,
                                                    void *result_ctx)
{
    if (!async)
        return;

    std::lock_guard<std::mutex> guard(async->lock);
    async->send_fn = send_fn;
    async->send_ctx = send_ctx;
    async->result_fn = result_fn;
    async->result_ctx = result_ctx;
}

void daemon_tx_async_worker_configure_cad(DaemonTxAsyncWorker *async,
                                                         DaemonTxCadProbeFn cad_probe_fn,
                                                         void *cad_probe_ctx,
                                                         DaemonTxSleepFn cad_sleep_fn,
                                                         void *cad_sleep_ctx)
{
    if (!async)
        return;

    std::lock_guard<std::mutex> guard(async->lock);
    async->cad_probe_fn = cad_probe_fn;
    async->cad_probe_ctx = cad_probe_ctx;
    async->cad_sleep_fn = cad_sleep_fn;
    async->cad_sleep_ctx = cad_sleep_ctx;
}

size_t daemon_tx_async_worker_pending(DaemonTxAsyncWorker *async)
{
    if (!async)
        return 0;

    std::lock_guard<std::mutex> guard(async->lock);
    return daemon_tx_worker_pending(&async->worker);
}

size_t daemon_tx_async_worker_accepted(DaemonTxAsyncWorker *async)
{
    if (!async)
        return 0;

    std::lock_guard<std::mutex> guard(async->lock);
    return daemon_tx_worker_accepted(&async->worker);
}

size_t daemon_tx_async_worker_processed(DaemonTxAsyncWorker *async)
{
    if (!async)
        return 0;

    std::lock_guard<std::mutex> guard(async->lock);
    return daemon_tx_worker_processed(&async->worker);
}

size_t daemon_tx_async_worker_rejected(DaemonTxAsyncWorker *async)
{
    if (!async)
        return 0;

    std::lock_guard<std::mutex> guard(async->lock);
    return daemon_tx_worker_rejected(&async->worker);
}

size_t daemon_tx_async_worker_dropped(DaemonTxAsyncWorker *async)
{
    if (!async)
        return 0;

    std::lock_guard<std::mutex> guard(async->lock);
    return daemon_tx_worker_dropped(&async->worker);
}

int daemon_tx_async_worker_running(DaemonTxAsyncWorker *async)
{
    if (!async)
        return 0;

    std::lock_guard<std::mutex> guard(async->lock);
    return async->running ? 1 : 0;
}

int daemon_tx_async_worker_last_result_copy(DaemonTxAsyncWorker *async,
                                                             DaemonTxJobResult *out)
{
    const DaemonTxJobResult *last;

    if (!async || !out)
        return 0;

    std::lock_guard<std::mutex> guard(async->lock);
    last = daemon_tx_worker_last_result(&async->worker);
    if (!last)
        return 0;

    *out = *last;
    return 1;
}

void daemon_tx_async_worker_loop(DaemonTxAsyncWorker *async)
{
    for (;;) {
        DaemonTxJob job;
        DaemonTxSendFn send_fn;
        void *send_ctx;
        DaemonTxCadProbeFn cad_probe_fn;
        void *cad_probe_ctx;
        DaemonTxSleepFn cad_sleep_fn;
        void *cad_sleep_ctx;
        DaemonTxWorkerResultFn result_fn;
        void *result_ctx;
        DaemonTxJobResult result;

        {
            std::unique_lock<std::mutex> guard(async->lock);

            while (!async->stop_requested &&
                   daemon_tx_worker_pending(&async->worker) == 0) {
                async->wake.wait(guard);
            }

            if (async->stop_requested &&
                daemon_tx_worker_pending(&async->worker) == 0) {
                async->running = false;
                return;
            }

            if (daemon_tx_queue_pop(&async->worker.queue, &job) != 0)
                continue;

            async->job_active.store(true);

            send_fn = async->send_fn;
            send_ctx = async->send_ctx;
            cad_probe_fn = async->cad_probe_fn;
            cad_probe_ctx = async->cad_probe_ctx;
            cad_sleep_fn = async->cad_sleep_fn;
            cad_sleep_ctx = async->cad_sleep_ctx;
            result_fn = async->result_fn;
            result_ctx = async->result_ctx;
        }

        result = daemon_tx_execute_job_with_sender_and_cad(&job,
                                                           send_fn,
                                                           send_ctx,
                                                           cad_probe_fn,
                                                           cad_probe_ctx,
                                                           cad_sleep_fn,
                                                           cad_sleep_ctx);
        async->job_active.store(false);

        {
            DaemonTxWorkerDrainCtx drain;

            std::lock_guard<std::mutex> guard(async->lock);
            drain.worker = &async->worker;
            drain.result_fn = result_fn;
            drain.result_ctx = result_ctx;
            daemon_tx_worker_record_result(&result, &drain);
        }
    }
}

int daemon_tx_async_worker_start(DaemonTxAsyncWorker *async)
{
    if (!async)
        return -1;

    {
        std::lock_guard<std::mutex> guard(async->lock);

        if (async->running)
            return 0;

        async->stop_requested = false;
        async->running = true;
    }

    try {
        async->thread = std::thread(daemon_tx_async_worker_loop, async);
    } catch (...) {
        std::lock_guard<std::mutex> guard(async->lock);
        async->running = false;
        return -1;
    }

    return 0;
}

void daemon_tx_async_worker_stop(DaemonTxAsyncWorker *async)
{
    if (!async)
        return;

    {
        std::lock_guard<std::mutex> guard(async->lock);
        async->stop_requested = true;
        daemon_tx_queue_discard_all(&async->worker.queue);
    }

    async->wake.notify_all();

    if (async->thread.joinable())
        async->thread.join();

    {
        std::lock_guard<std::mutex> guard(async->lock);
        async->running = false;
    }
}

int daemon_tx_async_worker_submit(DaemonTxAsyncWorker *async,
                                                const DaemonTxJob *job)
{
    int rc;

    if (!async)
        return -1;

    {
        std::lock_guard<std::mutex> guard(async->lock);

        if (async->stop_requested) {
            async->worker.rejected++;
            return -1;
        }

        rc = daemon_tx_worker_submit(&async->worker, job);
    }

    if (rc == 0)
        async->wake.notify_one();

    return rc;
}
