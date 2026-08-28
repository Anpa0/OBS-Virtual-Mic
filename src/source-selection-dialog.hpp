#pragma once

#include <QDialog>
#include <QStringList>

class QListWidget;

class SourceSelectionDialog : public QDialog {
    Q_OBJECT
public:
    SourceSelectionDialog(const QStringList &available,
                          const QStringList &selected,
                          QWidget *parent = nullptr);

    QStringList selectedSources() const;

private:
    QListWidget *list_ = nullptr;
};
