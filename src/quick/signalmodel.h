#pragma once
#include <QAbstractListModel>

class SignalModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role { NameRole = Qt::UserRole + 1, IndexRole, CheckedRole };
    explicit SignalModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setNames(const QStringList &names);
    void setChecked(int row, bool checked);
private:
    QStringList m_names;
    QVector<bool> m_checked;
};
