#include "scriptapi.h"
#include <pybind11/stl.h>
#include <QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>
#include <QEventLoop>
#include <QFile>
#include <QThread>

// --- CRC 和 解析辅助结构 ---
static const uint16_t crc16_table[256] = {
    0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
    0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
    0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
    0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
    0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
    0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
    0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
    0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
    0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
    0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
    0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
    0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
    0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
    0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
    0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
    0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
    0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
    0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
    0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
    0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
    0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
    0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
    0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
    0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
    0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
    0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
    0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
    0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
    0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
    0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
    0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
    0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78};

static uint16_t do_crc_R_calculate_look_table(const uint8_t *data, size_t len)
{
    // 查表法更快
    uint16_t crc_reg = 0xffff;

    for (size_t i = 0; i < len; ++i)
    {
        uint8_t index = (crc_reg ^ data[i]) & 0xff;
        crc_reg = (crc_reg >> 8) ^ crc16_table[index];
    }
    return (crc_reg ^ 0xffff);
}

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
    bool isBigEndian;
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

        if (chunk.isBigEndian)
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
        uint16_t calc_crc = do_crc_R_calculate_look_table(&buffer[payload_start], packet_len);

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
        chunk.isBigEndian = isOld;
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