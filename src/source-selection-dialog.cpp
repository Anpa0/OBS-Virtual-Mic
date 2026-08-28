#include "source-selection-dialog.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

SourceSelectionDialog::SourceSelectionDialog(const QStringList &available,
                                             const QStringList &selected,
                                             QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("OBS Virtual Mic Sources");
    resize(430, 420);

    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(
        "Choose the OBS audio sources that should be sent to OBS Virtual Mic. "
        "This selection is independent of Track 1.", this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    list_ = new QListWidget(this);
    for (const QString &name : available) {
        auto *item = new QListWidgetItem(name, list_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(selected.contains(name) ? Qt::Checked : Qt::Unchecked);
    }
    layout->addWidget(list_, 1);

    auto *note = new QLabel(
        "Implementation note: while Virtual Mic is running, OBS Track 6 is used as a private "
        "mixing bus. Your existing Track 6 routing is restored when Virtual Mic stops.", this);
    note->setWordWrap(true);
    note->setStyleSheet("color: palette(mid);");
    layout->addWidget(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QStringList SourceSelectionDialog::selectedSources() const
{
    QStringList out;
    for (int i = 0; i < list_->count(); ++i) {
        const auto *item = list_->item(i);
        if (item->checkState() == Qt::Checked)
            out.push_back(item->text());
    }
    return out;
}
