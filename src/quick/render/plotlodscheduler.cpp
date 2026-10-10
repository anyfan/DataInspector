#include "plotlodscheduler.h"
#include <QThreadPool>

namespace {
PlotLodResultCache &completedCache()
{
    static PlotLodResultCache cache;
    return cache;
}
QThreadPool &lodPool()
{
    static QThreadPool pool;
    static const bool configured = [] { pool.setMaxThreadCount(2); return true; }();
    Q_UNUSED(configured);
    return pool;
}
}

PlotLodScheduler::PlotLodScheduler(QObject *parent) : QObject(parent)
{
    m_poll.setInterval(8);
    connect(&m_poll, &QTimer::timeout, this, &PlotLodScheduler::finish);
}
PlotLodScheduler::~PlotLodScheduler()
{
    if (m_job) m_job->cancelled = true;
}
void PlotLodScheduler::cancel()
{
    if (m_job) m_job->cancelled = true;
    m_job.reset();
    m_poll.stop();
    m_snapshot = {};
    m_key.reset();
    m_result.reset();
    ++m_revision;
}
void PlotLodScheduler::request(const PlotSeriesSnapshot &snapshot, const LodRequestKey &key)
{
    if (m_key && *m_key == key && m_snapshot.series == snapshot.series) return;
    const bool dataChanged = m_snapshot.series != snapshot.series;
    m_snapshot = snapshot;
    m_key = key;
    ++m_revision;
    if (dataChanged) m_result.reset();
    if (auto cached = completedCache().find(snapshot, key)) {
        if (m_job) m_job->cancelled = true;
        m_result = std::move(cached);
        emit ready();
        return;
    }
    if (m_job) m_job->cancelled = true;
    else start();
}
void PlotLodScheduler::start()
{
    m_job = std::make_shared<Job>();
    m_job->revision = m_revision;
    const auto job = m_job;
    const auto snapshot = m_snapshot;
    const auto key = *m_key;
    lodPool().start([job, snapshot, key] {
        try {
            if (!job->cancelled)
                job->result = std::make_shared<LodResult>(
                    PlotLodBuilder::build(snapshot, key, &job->cancelled));
        } catch (...) {
            // Leave the current preview intact if a build cannot allocate.
        }
        job->done.store(true, std::memory_order_release);
    });
    m_poll.start();
}
void PlotLodScheduler::finish()
{
    if (!m_job || !m_job->done.load(std::memory_order_acquire)) return;
    const auto job = std::move(m_job);
    if (job->revision != m_revision) {
        if (m_result && m_key && m_result->key == *m_key) { m_poll.stop(); return; }
        start(); return;
    }
    m_poll.stop();
    if (!job->cancelled && job->result) {
        m_result = job->result;
        completedCache().insert(m_snapshot, m_result);
    }
    emit ready();
}
