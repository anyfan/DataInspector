#pragma once
#include <QAbstractListModel>
#include <QColor>

class SignalModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int checkedCount READ checkedCount NOTIFY checkedCountChanged)
public:
    enum Role { NameRole = Qt::UserRole + 1, IndexRole, CheckedRole, ColorRole, GroupRole };
    explicit SignalModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setNames(const QStringList &names, const QVector<QColor> &colors = {});
    void setFilter(const QString &text);
    void setAllChecked(bool checked);
    int checkedCount() const;
    QColor color(int row) const { return m_colors.value(row, QColor("#4ea1ff")); }
    QString nameAt(int row) const { return m_names.value(row); }
    void setChecked(int row, bool checked);
signals:
    void checkedCountChanged();
private:
    QStringList m_names;
    QVector<bool> m_checked;
    QVector<QColor> m_colors;
    QVector<int> m_visibleRows;
    QString m_filter;
};
