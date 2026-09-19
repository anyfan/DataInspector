#pragma once
#include <QAbstractListModel>
#include <QColor>
#include <QPen>
#include <QSet>
#include <QVector>

class SignalModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int checkedCount READ checkedCount NOTIFY checkedCountChanged)
public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        IndexRole,
        CheckedRole,
        ColorRole,
        GroupRole,
        GroupNodeRole,
        FileNodeRole,
        ExpandedRole,
        DepthRole,
        WidthRole,
        LineStyleRole
    };
    explicit SignalModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setNames(const QStringList &names, const QVector<QColor> &colors = {});
    void setNames(const QStringList &names, const QStringList &groups,
                  const QVector<QColor> &colors = {});
    void appendNames(const QStringList &names, const QStringList &groups,
                     const QVector<QColor> &colors = {});
    int sourceCount() const { return m_names.size(); }
    void setFilter(const QString &text);
    Q_INVOKABLE int revealSignal(int sourceRow);
    Q_INVOKABLE void toggleGroup(const QString &group);
    QVector<int> removeFile(const QString &fileName);
    void setAllChecked(bool checked);
    int checkedCount() const;
    QColor color(int row) const { return signalColor(row); }
    QColor signalColor(int row) const;
    double signalWidth(int row) const;
    Qt::PenStyle signalStyle(int row) const;
    void setSignalPen(int row, const QColor &color, double width,
                      Qt::PenStyle style);
    // Renames one source row; rejects invalid rows and blank names.
    bool renameSignal(int row, const QString &name);
    QString originalNameAt(int row) const { return m_originalNames.value(row); }
    QString nameAt(int row) const { return m_names.value(row); }
    QString groupAt(int row) const { return m_groups.value(row); }
    void setChecked(int row, bool checked);
    void setPlotCount(int count);
    void setActivePlot(int plotIndex);
    int activePlot() const { return m_activePlot; }
    void setPlotChecked(int plotIndex, int row, bool checked);
    QVector<int> plotRows(int plotIndex) const;
signals:
    void checkedCountChanged();
private:
    struct VisibleNode {
        bool groupNode = false;
        int sourceRow = -1;
        QString group;
        int depth = 0;
    };
    void rebuildVisibleNodes();
    int visibleModelRow(int sourceRow) const;
    bool groupExists(const QString &group) const;
    QStringList m_names;
    QStringList m_originalNames;
    QStringList m_groups;
    QVector<QColor> m_colors;
    QVector<double> m_widths;
    QVector<Qt::PenStyle> m_lineStyles;
    QVector<VisibleNode> m_visibleNodes;
    QSet<QString> m_expandedGroups;
    QVector<QSet<int>> m_plotRows;
    int m_plotCount = 0;
    int m_activePlot = -1;
    QString m_filter;
};
