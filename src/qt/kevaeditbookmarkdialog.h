// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KEVACOIN_QT_KEVAEDITBOOKMARKDIALOG_H
#define KEVACOIN_QT_KEVAEDITBOOKMARKDIALOG_H

#include <QObject>
#include <QString>

#include <QDialog>

namespace Ui {
    class KevaEditBookmarkDialog;
}

class KevaBookmarksDialog;


/** Dialog showing namepsace creation. */
class KevaEditBookmarkDialog : public QDialog
{
    Q_OBJECT

public:
    explicit KevaEditBookmarkDialog(QWidget *parent, const QString& id, const QString& name);
    ~KevaEditBookmarkDialog();

public Q_SLOTS:
    void save();
    void close();
    void onNameChanged(const QString & ns);

private:
    Ui::KevaEditBookmarkDialog *ui;
    QString id;
    QString name;
    KevaBookmarksDialog* parentDialog;
};

#endif // KEVACOIN_QT_KEVAEDITBOOKMARKDIALOG_H
