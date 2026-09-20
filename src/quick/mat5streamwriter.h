#pragma once
#include <QByteArray>
#include <QIODevice>
#include <QString>

// Minimal uncompressed Level-5 framing; payloads are emitted in bounded chunks.
// Keeping this separate from export planning makes byte order/size checks explicit.
class Mat5StreamWriter final
{
public:
    explicit Mat5StreamWriter(QIODevice &device) : m_device(device) {}
    bool writeHeader();
    bool beginMatrix(const QByteArray &name, qsizetype rows, qsizetype columns,
                     quint32 arrayClass, quint32 dataType, quint64 payloadBytes);
    bool writePayload(const QByteArray &bytes);
    bool endMatrix();
    QString error() const { return m_error; }
private:
    bool write(const QByteArray &bytes);
    QIODevice &m_device;
    QString m_error;
    quint64 m_remaining = 0;
    int m_padding = 0;
    bool m_open = false;
};
