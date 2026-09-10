#pragma once
#include <QAbstractListModel>
#include <QColor>
#include <QSet>
#include <QVector>

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
    void setNames(const QStringList &names, const QStringList &groups,
                  const QVector<QColor> &colors = {});
    void setFilter(const QString &text);
    void setAllChecked(bool checked);
    int checkedCount() const;
    QColor color(int row) const { return m_colors.value(row, QColor("#4ea1ff")); }
    QString nameAt(int row) const { return m_names.value(row); }
    void setChecked(int row, bool checked);
    void setPlotCount(int count);
    void setActivePlot(int plotIndex);
    int activePlot() const { return m_activePlot; }
    void setPlotChecked(int plotIndex, int row, bool checked);
    QVector<int> plotRows(int plotIndex) const;
    QVector<int> visiblePlotRows(int plotIndex) const;
    bool plotSignalVisible(int plotIndex, int row) const;
    void setPlotSignalVisible(int plotIndex, int row, bool visible);
signals:
    void checkedCountChanged();
private:
    QStringList m_names;
    QStringList m_groups;
    QVector<QColor> m_colors;
    QVector<int> m_visibleRows;
    QVector<QSet<int>> m_plotRows;
    QVector<QSet<int>> m_hiddenPlotRows;
    int m_plotCount = 0;
    int m_activePlot = -1;
    QString m_filter;
};
