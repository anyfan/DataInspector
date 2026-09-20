#include "mat5streamwriter.h"
#include <QtEndian>
#include <limits>

namespace {
void u32(QByteArray &bytes, quint32 value)
{
    char encoded[4];
    qToLittleEndian(value, encoded);
    bytes.append(encoded, 4);
}
quint64 padded(quint64 bytes) { return (bytes + 7) & ~quint64(7); }
}

bool Mat5StreamWriter::write(const QByteArray &bytes)
{
    if (m_device.write(bytes) == bytes.size()) return true;
    m_error = QStringLiteral("写入 MAT 文件失败：%1").arg(m_device.errorString());
    return false;
}

bool Mat5StreamWriter::writeHeader()
{
    QByteArray header(128, '\0');
    const QByteArray description("MATLAB 5.0 MAT-file, DataInspector streaming export");
    header.replace(0, description.size(), description);
    header[124] = '\0'; header[125] = '\1'; // version 0x0100, little endian
    header[126] = 'I'; header[127] = 'M';
    return write(header);
}

bool Mat5StreamWriter::beginMatrix(const QByteArray &name, qsizetype rows,
    qsizetype columns, quint32 arrayClass, quint32 dataType, quint64 payloadBytes)
{
    constexpr quint64 limit = 0x7fffffffULL;
    if (m_open || rows < 0 || columns < 0 || name.isEmpty()
        || rows > std::numeric_limits<qint32>::max()
        || columns > std::numeric_limits<qint32>::max()
        || quint64(name.size()) > limit || payloadBytes > limit
        || 48 + padded(name.size()) + padded(payloadBytes) > limit) {
        m_error = QStringLiteral("MAT 5 矩阵尺寸无效或超过单变量 2 GB 上限");
        return false;
    }
    QByteArray header;
    u32(header, 14); // miMATRIX
    u32(header, quint32(48 + padded(name.size()) + padded(payloadBytes)));
    u32(header, 6); u32(header, 8); // miUINT32 array flags
    u32(header, arrayClass); u32(header, 0);
    u32(header, 5); u32(header, 8); // miINT32 dimensions
    u32(header, quint32(rows)); u32(header, quint32(columns));
    u32(header, 1); u32(header, quint32(name.size())); // miINT8 name
    header.append(name);
    header.append(QByteArray(qsizetype(padded(name.size()) - name.size()), '\0'));
    u32(header, dataType); u32(header, quint32(payloadBytes));
    if (!write(header)) return false;
    m_remaining = payloadBytes;
    m_padding = int(padded(payloadBytes) - payloadBytes);
    m_open = true;
    return true;
}

bool Mat5StreamWriter::writePayload(const QByteArray &bytes)
{
    if (!m_open || quint64(bytes.size()) > m_remaining) {
        m_error = QStringLiteral("MAT 矩阵数据长度不匹配");
        return false;
    }
    if (!write(bytes)) return false;
    m_remaining -= bytes.size();
    return true;
}

bool Mat5StreamWriter::endMatrix()
{
    if (!m_open || m_remaining != 0) {
        m_error = QStringLiteral("MAT 矩阵数据不完整");
        return false;
    }
    if (!write(QByteArray(m_padding, '\0'))) return false;
    m_open = false;
    return true;
}
