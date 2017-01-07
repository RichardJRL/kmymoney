/***************************************************************************
                          newspliteditor.cpp
                             -------------------
    begin                : Sat Apr 9 2016
    copyright            : (C) 2016 by Thomas Baumgart
    email                : Thomas Baumgart <tbaumgart@kde.org>
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "newspliteditor.h"

// ----------------------------------------------------------------------------
// QT Includes

#include <QTreeView>
#include <QCompleter>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QDebug>

// ----------------------------------------------------------------------------
// KDE Includes

#include <KLocalizedString>

// ----------------------------------------------------------------------------
// Project Includes

#include "kmymoneyaccountcombo.h"
#include "models.h"
#include "costcentermodel.h"
#include "ledgermodel.h"
#include "mymoneysplit.h"
#include "ui_newspliteditor.h"
#include "widgethintframe.h"
#include "ledgerview.h"

struct NewSplitEditor::Private
{
  Private(NewSplitEditor* parent)
  : ui(new Ui_NewSplitEditor)
  , accountsModel(new AccountNamesFilterProxyModel(parent))
  , costCenterModel(new QSortFilterProxyModel(parent))
  , splitModel(0)
  , accepted(false)
  , costCenterRequired(false)
  , costCenterOk(false)
  , inverseAmounts(false)
  {
    accountsModel->setObjectName("AccountNamesFilterProxyModel");
    costCenterModel->setObjectName("SortedCostCenterModel");
    statusModel.setObjectName("StatusModel");

    costCenterModel->setSortLocaleAware(true);
    costCenterModel->setSortCaseSensitivity(Qt::CaseInsensitive);

    createStatusEntry(MyMoneySplit::NotReconciled);
    createStatusEntry(MyMoneySplit::Cleared);
    createStatusEntry(MyMoneySplit::Reconciled);
    // createStatusEntry(MyMoneySplit::Frozen);
  }

  void createStatusEntry(MyMoneySplit::reconcileFlagE status);
  bool checkForValidSplit(bool doUserInteraction = true);

  int amountTypeDebitIndex() const { return 1; }
  int amountTypeCreditIndex() const { return 0; }

  bool costCenterChanged(int costCenterIndex);
  bool categoryChanged(const QString& accountId);
  bool numberChanged(const QString& newNumber);
  bool amountChanged(const QString& newAmount);

  Ui_NewSplitEditor*            ui;
  AccountNamesFilterProxyModel* accountsModel;
  QSortFilterProxyModel*        costCenterModel;
  SplitModel*                   splitModel;
  bool                          accepted;
  bool                          costCenterRequired;
  bool                          costCenterOk;
  bool                          inverseAmounts;
  QStandardItemModel            statusModel;
  QString                       transactionSplitId;
  QString                       accountId;
};

void NewSplitEditor::Private::createStatusEntry(MyMoneySplit::reconcileFlagE status)
{
  QStandardItem* p = new QStandardItem(KMyMoneyUtils::reconcileStateToString(status, true));
  p->setData(status);
  statusModel.appendRow(p);
}

bool NewSplitEditor::Private::checkForValidSplit(bool doUserInteraction)
{
  QStringList infos;
  bool rc = true;
  if(!costCenterChanged(ui->costCenterCombo->currentIndex())) {
    infos << ui->costCenterCombo->toolTip();
    rc = false;
  }

  if(doUserInteraction) {
    /// @todo add dialog here that shows the @a infos
  }
  return rc;
}

bool NewSplitEditor::Private::costCenterChanged(int costCenterIndex)
{
  bool rc = true;
  WidgetHintFrame::hide(ui->costCenterCombo, i18n("The cost center this transaction should be assigned to."));
  if(costCenterIndex != -1) {
    if(costCenterRequired && ui->costCenterCombo->currentText().isEmpty()) {
      WidgetHintFrame::show(ui->costCenterCombo, i18n("A cost center assignment is required for a transaction in the selected category."));
      rc = false;
    }
  }
  return rc;
}

bool NewSplitEditor::Private::categoryChanged(const QString& accountId)
{
  bool rc = true;
  if(!accountId.isEmpty()) {
    try {
      MyMoneyAccount category = MyMoneyFile::instance()->account(accountId);
      const bool isIncomeExpense = category.isIncomeExpense();
      ui->costCenterCombo->setEnabled(isIncomeExpense);
      ui->costCenterLabel->setEnabled(isIncomeExpense);
      costCenterRequired = category.isCostCenterRequired();
      rc &= costCenterChanged(ui->costCenterCombo->currentIndex());
    } catch (MyMoneyException &e) {
      qDebug() << "Ooops: invalid account id" << accountId << "in" << Q_FUNC_INFO;
    }
  }
  return rc;
}

bool NewSplitEditor::Private::numberChanged(const QString& newNumber)
{
  bool rc = true;
  WidgetHintFrame::hide(ui->numberEdit, i18n("The check number used for this transaction."));
  if(!newNumber.isEmpty()) {
    const LedgerModel* model = Models::instance()->ledgerModel();
    QModelIndexList list = model->match(model->index(0, 0), LedgerModel::NumberRole,
                                        QVariant(newNumber),
                                        -1,                         // all splits
                                        Qt::MatchFlags(Qt::MatchExactly | Qt::MatchCaseSensitive | Qt::MatchRecursive));

    foreach(QModelIndex index, list) {
      qDebug() << model->data(index, LedgerModel::AccountIdRole).toString();
      if(model->data(index, LedgerModel::AccountIdRole) == ui->accountCombo->getSelected()
      && model->data(index, LedgerModel::TransactionSplitIdRole) != transactionSplitId) {
        WidgetHintFrame::show(ui->numberEdit, i18n("The check number <b>%1</b> has already been used in this account.").arg(newNumber));
        rc = false;
        break;
      }
    }
  }
  return rc;
}

bool NewSplitEditor::Private::amountChanged(const QString& newAmount)
{
  Q_UNUSED(newAmount);

  bool rc = true;
  if(ui->amountEdit->value().isNegative()) {
    // switch the type
    ui->amountTypeCombo->setCurrentIndex(ui->amountTypeCombo->currentIndex() ? 0 : 1);
    // and reverse the sign
    ui->amountEdit->setValue(ui->amountEdit->value().abs());
  }
  return rc;
}



NewSplitEditor::NewSplitEditor(QWidget* parent, const QString& accountId)
  : QFrame(parent, Qt::FramelessWindowHint /* | Qt::X11BypassWindowManagerHint */)
  , d(new Private(this))
{
  SplitView* view = qobject_cast<SplitView*>(parent->parentWidget());
  Q_ASSERT(view != 0);
  d->splitModel = qobject_cast<SplitModel*>(view->model());

  d->accountId = accountId;
  d->ui->setupUi(this);
  d->ui->enterButton->setIcon(QIcon::fromTheme("dialog-ok"));
  d->ui->cancelButton->setIcon(QIcon::fromTheme("dialog-cancel"));
  d->accountsModel->addAccountGroup(MyMoneyAccount::Asset);
  d->accountsModel->addAccountGroup(MyMoneyAccount::Liability);
  d->accountsModel->addAccountGroup(MyMoneyAccount::Income);
  d->accountsModel->addAccountGroup(MyMoneyAccount::Expense);
  d->accountsModel->addAccountGroup(MyMoneyAccount::Equity);
  d->accountsModel->setHideEquityAccounts(false);
  d->accountsModel->setSourceModel(Models::instance()->accountsModel());
  d->accountsModel->sort(0);
  d->ui->accountCombo->setModel(d->accountsModel);

  d->costCenterModel->setSortRole(Qt::DisplayRole);
  d->costCenterModel->setSourceModel(Models::instance()->costCenterModel());
  d->costCenterModel->sort(0);

  d->ui->costCenterCombo->setEditable(true);
  d->ui->costCenterCombo->setModel(d->costCenterModel);
  d->ui->costCenterCombo->setModelColumn(0);
  d->ui->costCenterCombo->completer()->setFilterMode(Qt::MatchContains);

  WidgetHintFrameCollection* frameCollection = new WidgetHintFrameCollection(this);
  frameCollection->addFrame(new WidgetHintFrame(d->ui->costCenterCombo));
  frameCollection->addFrame(new WidgetHintFrame(d->ui->numberEdit, WidgetHintFrame::Warning));
  frameCollection->addWidget(d->ui->enterButton);

  connect(d->ui->numberEdit, SIGNAL(textChanged(QString)), this, SLOT(numberChanged(QString)));
  connect(d->ui->costCenterCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(costCenterChanged(int)));
  connect(d->ui->accountCombo, SIGNAL(accountSelected(QString)), this, SLOT(categoryChanged(QString)));
  connect(d->ui->amountEdit, SIGNAL(valueChanged(QString)), this, SLOT(amountChanged(QString)));

  connect(d->ui->cancelButton, SIGNAL(clicked(bool)), this, SLOT(reject()));
  connect(d->ui->enterButton, SIGNAL(clicked(bool)), this, SLOT(acceptEdit()));
}

NewSplitEditor::~NewSplitEditor()
{
}

void NewSplitEditor::setInversedViewOfAmounts(bool inverse)
{
  d->inverseAmounts = inverse;
}

bool NewSplitEditor::isInversedViewOfAmounts()
{
  return d->inverseAmounts;
}

bool NewSplitEditor::accepted() const
{
  return d->accepted;
}

void NewSplitEditor::acceptEdit()
{
  if(d->checkForValidSplit()) {
    d->accepted = true;
    emit done();
  }
}

void NewSplitEditor::reject()
{
  emit done();
}

void NewSplitEditor::keyPressEvent(QKeyEvent* e)
{
  if (!e->modifiers() || (e->modifiers() & Qt::KeypadModifier && e->key() == Qt::Key_Enter)) {
    switch (e->key()) {
    case Qt::Key_Enter:
    case Qt::Key_Return:
      {
        if(focusWidget() == d->ui->cancelButton) {
          reject();
        } else {
          if(d->ui->enterButton->isEnabled()) {
            d->ui->enterButton->click();
          }
          return;
        }
      }
      break;

    case Qt::Key_Escape:
      reject();
      break;

    default:
      e->ignore();
      return;
    }
  } else {
    e->ignore();
  }
}

QString NewSplitEditor::accountId() const
{
  return d->ui->accountCombo->getSelected();
}

void NewSplitEditor::setAccountId(const QString& id)
{
  d->ui->accountCombo->setSelected(id);
}


QString NewSplitEditor::memo() const
{
  return d->ui->memoEdit->toPlainText();
}

void NewSplitEditor::setMemo(const QString& memo)
{
  d->ui->memoEdit->setPlainText(memo);
}

MyMoneyMoney NewSplitEditor::amount() const
{
  MyMoneyMoney value = d->ui->amountEdit->value();
  if(d->ui->amountTypeCombo->currentIndex() == d->amountTypeCreditIndex())
    value = -value;
  return value;
}

void NewSplitEditor::setAmount(MyMoneyMoney value)
{
  if(d->inverseAmounts) {
    value = -value;
  }
  d->ui->amountTypeCombo->setCurrentIndex(d->amountTypeDebitIndex());
  d->ui->amountEdit->setValue(value);
  if(value.isNegative()) {
    d->ui->amountTypeCombo->setCurrentIndex(d->amountTypeCreditIndex());
    d->ui->amountEdit->setValue(-value);
  }
}

QString NewSplitEditor::costCenterId() const
{
  const int row = d->ui->costCenterCombo->currentIndex();
  QModelIndex index = d->ui->costCenterCombo->model()->index(row, 0);
  return d->ui->costCenterCombo->model()->data(index, CostCenterModel::CostCenterIdRole).toString();
}

void NewSplitEditor::setCostCenterId(const QString& id)
{
  QModelIndex index = Models::indexById(d->costCenterModel, CostCenterModel::CostCenterIdRole, id);
  if(index.isValid()) {
    d->ui->costCenterCombo->setCurrentIndex(index.row());
  }
}


QString NewSplitEditor::splitId() const
{
  return d->transactionSplitId;
}

void NewSplitEditor::numberChanged(const QString& newNumber)
{
  d->numberChanged(newNumber);
}

void NewSplitEditor::categoryChanged(const QString& accountId)
{
  d->categoryChanged(accountId);
}

void NewSplitEditor::costCenterChanged(int costCenterIndex)
{
  d->costCenterChanged(costCenterIndex);
}

void NewSplitEditor::amountChanged(const QString& newAmount)
{
  d->amountChanged(newAmount);
}