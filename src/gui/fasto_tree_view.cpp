#include "gui/fasto_tree_view.h"

#include <QMenu>
#include <QHeaderView>

#include "common/macros.h"

namespace fastonosql
{
    FastoTreeView::FastoTreeView(QWidget* parent)
        : QTreeView(parent)
    {
        setSelectionMode(QAbstractItemView::ExtendedSelection);
        setSelectionBehavior(QAbstractItemView::SelectRows);

        header()->resizeSections(QHeaderView::Stretch);

        setContextMenuPolicy(Qt::CustomContextMenu);
        VERIFY(connect(this, &FastoTreeView::customContextMenuRequested, this, &FastoTreeView::showContextMenu));
    }

    void FastoTreeView::showContextMenu(const QPoint& point)
    {
        QPoint menuPoint = mapToGlobal(point);
        menuPoint.setY(menuPoint.y() + header()->height());
        QMenu menu(this);
        menu.exec(menuPoint);
    }

    void FastoTreeView::resizeEvent(QResizeEvent *event)
    {
        header()->resizeSections(QHeaderView::Stretch);
        QTreeView::resizeEvent(event);        
    }
}
