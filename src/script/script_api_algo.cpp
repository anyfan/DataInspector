#include "scriptapi.h"
#include <pybind11/stl.h>
#include <QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>
#include <QEventLoop>
#include <QFile>
#include <QThread>

// --- CRC 和 解析辅助结构 ---

static uint16_t do_crc_R_calculate(const uint8_t *data, size_t len)
{
    uint16_t crc_reg = 0xffff;
    for (size_t i = 0; i < len; ++i)
    {
        uint8_t index = (crc_reg ^ data[i]) & 0xff;
        uint16_t to_xor = index;
        for (int j = 0; j < 8; ++j)
        {
            if (to_xor & 0x0001)
            {
                to_xor = (to_xor >> 1) ^ 0x8408;
            }
            else
            {
                to_xor >>= 1;
            }
        }
        crc_reg = (crc_reg >> 8) ^ to_xor;
    }
    return (crc_reg ^ 0xffff);
}

struct ScanChunk
{
    const uint8_t *dataStart;
    size_t totalSize;
    size_t startOffset;
    size_t endOffset;
    bool isOldRec;
};

struct ChunkResult
{
    std::vector<std::pair<int, std::string>> packets;
    int errorCount = 0;
};

static ChunkResult scan_chunk_worker(const ScanChunk &chunk)
{
    ChunkResult res;
    res.packets.reserve((chunk.endOffset - chunk.startOffset) / 50);

    size_t offset = chunk.startOffset;
    const uint8_t *buffer = chunk.dataStart;

    while (offset + 4 < chunk.totalSize)
    {
        if (offset >= chunk.endOffset)
            break;

        if (buffer[offset] != 0xEB || buffer[offset + 1] != 0x90)
        {
            offset++;
            continue;
        }

        uint8_t b0 = buffer[offset + 2];
        uint8_t b1 = buffer[offset + 3];

        int packet_id = 0;
        int packet_len = 0;

        if (chunk.isOldRec)
        {
            packet_id = (b0 & 0x7f);
            packet_len = ((b0 & 0x80) << 1) | b1;
        }
        else
        {
            packet_id = (b0 & 0x1f);
            packet_len = ((b0 & 0xE0) << 3) | b1;
        }

        size_t payload_start = offset + 4;
        size_t crc_pos = payload_start + packet_len;

        if (crc_pos + 2 > chunk.totalSize)
            break;

        uint16_t file_crc = buffer[crc_pos] | (buffer[crc_pos + 1] << 8);
        uint16_t calc_crc = do_crc_R_calculate(&buffer[payload_start], packet_len);

        if (file_crc == calc_crc)
        {
            std::string payload(reinterpret_cast<const char *>(&buffer[payload_start]), packet_len);
            res.packets.emplace_back(packet_id, std::move(payload));
            offset = crc_pos + 2;
        }
        else
        {
            res.errorCount++;
            offset += 2;
        }
    }
    return res;
}

py::tuple ScriptAPI::parse_flight_data_fast(std::string path, std::string protocol)
{
    QString qPath = QString::fromStdString(path);
    QFile file(qPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        log("Error: Could not open file " + path);
        return py::make_tuple(py::list(), py::dict());
    }

    QByteArray data = file.readAll();
    file.close();

    if (data.isEmpty())
    {
        return py::make_tuple(py::list(), py::dict());
    }

    int threadCount = QThread::idealThreadCount();
    if (threadCount < 1)
        threadCount = 1;
    if (data.size() < 200000)
        threadCount = 1;

    std::vector<ScanChunk> chunks;
    size_t chunkSize = data.size() / threadCount;
    const uint8_t *rawData = reinterpret_cast<const uint8_t *>(data.constData());
    bool isOld = (protocol == "old_rec");

    for (int i = 0; i < threadCount; ++i)
    {
        ScanChunk chunk;
        chunk.dataStart = rawData;
        chunk.totalSize = data.size();
        chunk.startOffset = i * chunkSize;
        chunk.endOffset = (i == threadCount - 1) ? data.size() : (i + 1) * chunkSize;
        chunk.isOldRec = isOld;
        chunks.push_back(chunk);
    }

    QFuture<ChunkResult> future = QtConcurrent::mapped(chunks, scan_chunk_worker);

    QFutureWatcher<ChunkResult> watcher;
    watcher.setFuture(future);

    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcherBase::finished, &loop, &QEventLoop::quit);
    loop.exec(); // 等待计算完成

    std::vector<std::pair<int, py::bytes>> allPackets;
    size_t totalErrors = 0;
    size_t totalValid = 0;

    size_t estimatedTotal = 0;
    for (const auto &res : future.results())
    {
        estimatedTotal += res.packets.size();
    }
    allPackets.reserve(estimatedTotal);

    for (const auto &res : future.results())
    {
        totalErrors += res.errorCount;
        totalValid += res.packets.size();
        for (const auto &pair : res.packets)
        {
            allPackets.emplace_back(pair.first, py::bytes(pair.second));
        }
    }

    py::dict stats;
    stats["valid"] = totalValid;
    stats["error"] = totalErrors;

    return py::make_tuple(allPackets, stats);
}