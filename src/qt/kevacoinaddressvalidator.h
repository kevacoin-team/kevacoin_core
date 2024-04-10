// Copyright (c) 2011-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KEVACOIN_QT_KEVACOINADDRESSVALIDATOR_H
#define KEVACOIN_QT_KEVACOINADDRESSVALIDATOR_H

#include <QValidator>

/** Base58 entry widget validator, checks for valid characters and
 * removes some whitespace.
 */
class KevacoinAddressEntryValidator : public QValidator
{
    Q_OBJECT

public:
    explicit KevacoinAddressEntryValidator(QObject *parent);

    State validate(QString &input, int &pos) const override;
};

/** Kevacoin address widget validator, checks for a valid kevacoin address.
 */
class KevacoinAddressCheckValidator : public QValidator
{
    Q_OBJECT

public:
    explicit KevacoinAddressCheckValidator(QObject *parent);

    State validate(QString &input, int &pos) const override;
};

#endif // KEVACOIN_QT_KEVACOINADDRESSVALIDATOR_H
