#include "signalmodel.h"
SignalModel::SignalModel(QObject *parent) : QAbstractListModel(parent) {}
int SignalModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_names.size(); }
QVariant SignalModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_names.size()) return {};
    if (role == Qt::DisplayRole || role == NameRole) return m_names.at(index.row());
    if (role == IndexRole) return index.row();
    if (role == CheckedRole) return m_checked.value(index.row(), false);
    return {};
}
QHash<int, QByteArray> SignalModel::roleNames() const { return {{NameRole, "name"}, {IndexRole, "signalIndex"}, {CheckedRole, "checked"}}; }
void SignalModel::setNames(const QStringList &names) { beginResetModel(); m_names = names; m_checked.fill(false, names.size()); endResetModel(); }
void SignalModel::setChecked(int row, bool checked)
{
    if (row < 0 || row >= m_checked.size() || m_checked.at(row) == checked) return;
    m_checked[row] = checked;
    emit dataChanged(index(row), index(row), {CheckedRole});
}
