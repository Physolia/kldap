/*
  This file is part of libkldap.

  Copyright (c) 2002-2010 Tobias Koenig <tokoe@kde.org>

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General  Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public License
  along with this library; see the file COPYING.LIB.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301, USA.
*/

#ifndef ADDHOSTDIALOG_H
#define ADDHOSTDIALOG_H

#include "kldap_export.h"
#include <QDialog>

namespace KLDAP {
class LdapServer;
class AddHostDialogPrivate;
/**
 * @brief The AddHostDialog class
 * @author Laurent Montel <montel@kde.org>
 */
class KLDAP_EXPORT AddHostDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddHostDialog(KLDAP::LdapServer *server, QWidget *parent = nullptr);
    ~AddHostDialog();

Q_SIGNALS:
    void changed(bool);

private Q_SLOTS:
    void slotHostEditChanged(const QString &);
    void slotOk();

private:
    AddHostDialogPrivate *const d;
};
}

#endif // ADDHOSTDIALOG_H
