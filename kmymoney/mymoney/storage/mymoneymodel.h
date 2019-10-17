/*
 * Copyright 2019       Thomas Baumgart <tbaumgart@kde.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef MYMONEYMODEL_H
#define MYMONEYMODEL_H

// ----------------------------------------------------------------------------
// QT Includes

#include <QObject>
#include <QSharedData>
#include <QSharedDataPointer>
#include <QVariant>
#include <QModelIndex>
#include <QDomDocument>
#include <QDomElement>
#include <QUndoStack>

#include <QDebug>

// ----------------------------------------------------------------------------
// KDE Includes

// ----------------------------------------------------------------------------
// Project Includes

#include "mymoneyenums.h"
#include "mymoneymodelbase.h"

#include "kmm_models_export.h"

class MyMoneyFile;

template <typename T>
class TreeItem
{
public:
    TreeItem(T data, TreeItem<T>* parent = nullptr)
    {
      parentItem = parent;
      object = data;
    }

    ~TreeItem()
    {
      qDeleteAll(childItems);
    }

    TreeItem<T> *child(int row)
    {
      if (row < childItems.count()) {
        return childItems.value(row);
      }
      return nullptr;
    }

    int childCount() const
    {
      return childItems.count();
    }

    void appendChild(TreeItem<T>* item)
    {
      childItems.append(item);
    }

    bool insertChildren(int row, QVector<TreeItem<T>*> items)
    {
      if (row < 0 || row > childItems.count())
        return false;

      // insert empty pointers in one go
      childItems.insert(row, items.count(), nullptr);

      // and move the ones found in items to it
      const auto count = items.count();
      for (int i = 0; i < count; ++i) {
        childItems[row + i] = items[i];
        items[i] = nullptr;
      }
      return true;
    }

    bool insertChild(int row, TreeItem<T>* item)
    {
      if (row < 0 || row > childItems.count())
        return false;

      childItems.insert(row, item);
      return true;
    }

    bool removeChild(int row)
    {
      if (row < 0 || row >= childItems.count())
        return false;

      delete childItems.takeAt(row);
      return true;
    }


    TreeItem<T> *parent()
    {
      return parentItem;
    }

    int row() const
    {
      if (parentItem)
        return parentItem->childItems.indexOf(const_cast<TreeItem<T>*>(this));

      return 0;
    }


    T data() const
    {
      return object;
    }

    const T& constDataRef() const
    {
      return object;
    }

    T& dataRef()
    {
      return object;
    }

    void setData(const T& value)
    {
      object = value;
    }

private:
    T                                       object;
    QVector<TreeItem<T> *>                  childItems;
    TreeItem<T> *                           parentItem;
};


template <typename T>
class MyMoneyModel : public MyMoneyModelBase
{
public:
  enum Operation {
    Invalid,
    Add,
    Modify,
    Remove,
    Reparent
  };

  /**
   * This class represents an undo command within the MyMoney Engine
   *
   * @author Thomas Baumgart
   */
  class UndoCommand : public QUndoCommand
  {
  public:
    // construction/destruction

    explicit UndoCommand(MyMoneyModel<T>* model, const T& before, const T& after, QUndoCommand* parent = nullptr)
    : QUndoCommand(parent)
    , m_model(model)
    , m_before(before)
    , m_after(after)
    {
    }

    void redo() override
    {
      m_model->redo(m_before, m_after);
    }

    void undo() override
    {
      m_model->undo(m_before, m_after);
    }


  protected:
    MyMoneyModel<T>*    m_model;
    T                   m_before;
    T                   m_after;
  };



  explicit MyMoneyModel(QObject* parent, const QString& idLeadin, quint8 idSize, QUndoStack* undoStack)
      : MyMoneyModelBase(parent, idLeadin, idSize)
      , m_undoStack(undoStack)
      , m_idToItemMapper(nullptr)
  {
    m_rootItem = new TreeItem<T>(T());
  }

  virtual ~MyMoneyModel()
  {
    delete m_rootItem;
  }

  void useIdToItemMapper(bool use)
  {
    if (use && (m_idToItemMapper == nullptr)) {
      m_idToItemMapper = new QHash<QString, TreeItem<T>*>();

    } else if(!use && (m_idToItemMapper != nullptr)) {
      delete m_idToItemMapper;
      m_idToItemMapper = nullptr;
    }
  }

  Qt::ItemFlags flags(const QModelIndex &index) const override
  {
    if (!index.isValid())
      return Qt::ItemFlags();
    if (index.row() < 0 || index.row() >= rowCount(index.parent()))
      return Qt::ItemFlags();

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  }

  QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
  {
    TreeItem<T>* parentItem;

    if (!parent.isValid())
      parentItem = m_rootItem;
    else
      parentItem = static_cast<TreeItem<T>*>(parent.internalPointer());

    TreeItem<T> *childItem = parentItem->child(row);
    if (childItem)
      return createIndex(row, column, childItem);
    else
      return QModelIndex();
  }

  QModelIndex parent(const QModelIndex &index) const override
  {
    if (!index.isValid())
      return QModelIndex();

    TreeItem<T> *childItem = static_cast<TreeItem<T>*>(index.internalPointer());
    TreeItem<T> *parentItem = childItem->parent();

    if (parentItem == m_rootItem)
      return QModelIndex();

    return createIndex(parentItem->row(), 0, parentItem);
  }

  int rowCount(const QModelIndex &parent = QModelIndex()) const override
  {
    TreeItem<T> *parentItem;

    if (!parent.isValid())
      parentItem = m_rootItem;
    else
      parentItem = static_cast<TreeItem<T>*>(parent.internalPointer());

    return parentItem->childCount();
  }

  bool insertRows(int startRow, int rows, const QModelIndex &parent = QModelIndex()) override
  {
    TreeItem<T> *parentItem;

    if (!parent.isValid())
      parentItem = m_rootItem;
    else
      parentItem = static_cast<TreeItem<T>*>(parent.internalPointer());

    const int childCount = parentItem->childCount();
    if (startRow < 0 || startRow > childCount)
      return false;

    beginInsertRows(parent, startRow, startRow + rows - 1);

    // appending can be done one by one
    if (startRow < childCount) {
      QVector<TreeItem<T>*> items;
      for (int row = 0; row < rows; ++row) {
        items << new TreeItem<T>(T(), parentItem);
      }
      if (!parentItem->insertChildren(startRow, items)) {
        qDeleteAll(items);
      }
    } else {
      for (int row = 0; row < rows; ++row) {
        TreeItem<T> *newItem = new TreeItem<T>(T(), parentItem);
        if (!parentItem->insertChild(startRow, newItem))
          break;
      }
    }

    endInsertRows();
    setDirty();
    return true;
  }

  bool removeRows(int startRow, int rows, const QModelIndex &parent = QModelIndex()) override
  {
    TreeItem<T> *parentItem;

    if (!parent.isValid())
      parentItem = m_rootItem;
    else
      parentItem = static_cast<TreeItem<T>*>(parent.internalPointer());

    if (startRow < 0 || startRow > parentItem->childCount())
      return false;

    beginRemoveRows(parent, startRow, startRow + rows - 1);

    for (int row = 0; row < rows; ++row) {
      if (m_idToItemMapper) {
        const auto idx = index(startRow, 0, parent);
        m_idToItemMapper->remove(idx.data(eMyMoney::Model::IdRole).toString());
      }
      if (!parentItem->removeChild(startRow))
        break;
    }

    endRemoveRows();
    setDirty();
    return true;
  }

  /**
   * lowerBound returns a QModelIndex with a row pointing to the item
   * with the equal or next higher item.id(). If id is higher than any
   * id found in the model, then the returned QModelIndex has a row
   * equal to rowCount() which cannot be used. It is the caller's
   * responsibility to check for this condition. In case the distance
   * between first and last is less than or equal to zero, an invalid
   * QModelIndex is returned.
   */
  QModelIndex lowerBound(const QString& id, int first, int last) const override
  {
    int count = last - first + 1;
    int row = -1;
    int step;
    if (count <= 0) {
      return QModelIndex();
    }
    while (count > 0) {
      step = count / 2;
      row = first + step;
      const T& item = static_cast<TreeItem<T>*>(index(row, 0).internalPointer())->constDataRef();
      if (item.id() < id) {
        first = ++row;
        count -= step + 1;
      } else {
        count = step;
      }
    }
    return index(row, 0);
  }

  /**
   * upperBound returns a QModelIndex with a row pointing to the item
   * with the next higher item.id(). If id is higher than any
   * id found in the model, then the returned QModelIndex is invalid.
   * It is the caller's responsibility to check for this condition.
   * In case the distance between first and last is less than or equal
   * to zero, an invalid QModelIndex is returned.
   */
  QModelIndex upperBound(const QString& id, int first, int last) const override
  {
    int count = last - first + 1;
    int row = -1;
    int step;
    if (count <= 0) {
      return QModelIndex();
    }
    while (count > 0) {
      step = count / 2;
      row = first + step;
      const T& item = static_cast<TreeItem<T>*>(index(row, 0).internalPointer())->constDataRef();
      if (!(id < item.id())) {
        first = ++row;
        count -= step + 1;
      } else {
        count = step;
      }
    }
    return index(row, 0);
  }

  virtual QModelIndex indexById(const QString& id) const
  {
    if (m_idToItemMapper) {
      const auto item = m_idToItemMapper->value(id, nullptr);
      if (item) {
        return createIndex(item->row(), 0, item);
      }
    }
    const QModelIndexList indexes = match(index(0, 0), eMyMoney::Model::Roles::IdRole, id, 1, Qt::MatchFixedString | Qt::MatchRecursive);
    if (indexes.isEmpty())
      return QModelIndex();
    return indexes.first();
  }

  T itemByIndex(const QModelIndex& idx) const
  {
    if (idx.isValid()) {
      return static_cast<TreeItem<T>*>(idx.internalPointer())->data();
    }
    return T();
  }

  T itemById(const QString& id) const
  {
    return itemByIndex(indexById(id));
  }

  /**
   * clears all objects currently in the model
   */
  void unload()
  {
    beginResetModel();
    clearModelItems();
    // reset the next id to zero
    m_nextId = 0;
    m_dirty = false;
    endResetModel();
  }

  void load(const QMap<QString, T>& list)
  {
    beginResetModel();
    // first get rid of any existing entries
    clearModelItems();

    // create the number of required items
    const auto itemCount = list.count();
    insertRows(0, itemCount);

    // and don't count loading as a modification
    setDirty(false);
    m_nextId = 0;

    int row = 0;
    foreach (const auto item, list) {
      updateNextObjectId(item.id());
      static_cast<TreeItem<T>*>(index(row, 0).internalPointer())->dataRef() = item;
      ++row;
    }
    endResetModel();
    emit modelLoaded();

    qDebug() << "Model for" << m_idLeadin << "loaded with" << rowCount() << "items";
  }

  struct Worker
  {
    Worker() = default;
    virtual ~Worker() = default;
    virtual void operator()(const T& item) = 0;

  };

  struct xmlWriter : public Worker
  {
    xmlWriter(void (*writer)(const T&, QDomDocument&, QDomElement&), QDomDocument& document, QDomElement& element)
    : Worker()
    , m_writer(writer)
    , m_doc(document)
    , m_element(element) {}
    void operator()(const T& item) override { m_writer(item, m_doc, m_element); }

    void (*m_writer)(const T&, QDomDocument&, QDomElement&);
    QDomDocument& m_doc;
    QDomElement& m_element;
  };


  /// @todo possibly move to base class
  virtual int processItems(Worker *worker)
  {
    return processItems(worker, match(index(0, 0), eMyMoney::Model::Roles::IdRole, m_idLeadin, -1, Qt::MatchStartsWith | Qt::MatchRecursive));
  }

  int processItems(Worker *worker, const QModelIndexList& indexes)
  {
    foreach (const auto idx, indexes) {
      worker->operator()(static_cast<TreeItem<T>*>(idx.internalPointer())->constDataRef());
    }
    return indexes.count();
  }

  bool hasReferenceTo(const QString& id) const
  {
    bool rc = false;
    QModelIndexList indexes = match(index(0, 0), eMyMoney::Model::Roles::IdRole, "*", -1, Qt::MatchWildcard | Qt::MatchRecursive);
    foreach (const auto idx, indexes) {
      const T& item = static_cast<TreeItem<T>*>(idx.internalPointer())->constDataRef();
      if ((rc |= item.hasReferenceTo(id)) == true)
        break;
    }
    return rc;
  }

  QList<T> itemList() const
  {
    QList<T> list;
    QModelIndexList indexes = match(index(0, 0), eMyMoney::Model::Roles::IdRole, m_idLeadin, -1, Qt::MatchStartsWith | Qt::MatchRecursive);
    foreach (const auto idx, indexes) {
      list.append(static_cast<TreeItem<T>*>(idx.internalPointer())->constDataRef());
    }
    return list;
  }

  void addItem(T& item)
  {
    // assign an ID and store the object and
    // make sure the caller receives the assigned ID
    item = T(nextId(), item);

    m_undoStack->push(new UndoCommand(this, T(), item));
  }

  void modifyItem(const T& item)
  {
    const auto idx = indexById(item.id());
    if (idx.isValid()) {
      const auto currentItem = itemByIndex(idx);
      m_undoStack->push(new UndoCommand(this, currentItem, item));
    }
  }

  void removeItem(const T& item)
  {
    const auto idx = indexById(item.id());
    if (idx.isValid()) {
      const auto currentItem = itemByIndex(idx);
      m_undoStack->push(new UndoCommand(this, currentItem, T()));
    }
  }

  T itemByName(const QString& name, const QModelIndex parent = QModelIndex()) const
  {
    QModelIndexList indexes = indexListByName(name, parent);
    if (!indexes.isEmpty()) {
      return static_cast<TreeItem<T>*>(indexes.first().internalPointer())->data();
    }
    return T();
  }


protected:
  // determine for which type we were created:
  //
  // a) add item: m_after.id() is filled, m_before.id() is empty
  // b) modify item: m_after.id() is filled, m_before.id() is filled
  // c) add item: m_after.id() is empty, m_before.id() is filled
  virtual Operation undoOperation(const T& before, const T& after) const
  {
    const auto afterIdEmpty = after.id().isEmpty();
    const auto beforeIdEmpty = before.id().isEmpty();
    if (!afterIdEmpty && beforeIdEmpty)
      return Add;
    if (!afterIdEmpty && !beforeIdEmpty)
      return Modify;
    if (afterIdEmpty && !beforeIdEmpty)
      return Remove;
    return Invalid;
  }

  virtual void redo(const T& before, const T& after)
  {
    QModelIndex idx;
    switch(undoOperation(before, after)) {
      case Add:
        doAddItem(after);
        break;
      case Modify:
        idx = indexById(after.id());
        if (idx.isValid()) {
          doModifyItem(idx, after);
        }
        break;
      case Remove:
        idx = indexById(before.id());
        if (idx.isValid()) {
          doRemoveItem(idx);
        }
        break;
      case Reparent:
        break;
      case Invalid:
        qDebug() << "Invalid operation in redo";
        break;
    }
  }

  virtual void undo(const T& before, const T& after)
  {
    QModelIndex idx;
    switch(undoOperation(before, after)) {
      case Add:
        idx = indexById(after.id());
        if (idx.isValid()) {
          doRemoveItem(idx);
        }
        break;
      case Modify:
        idx = indexById(before.id());
        if (idx.isValid()) {
          doModifyItem(idx, before);
        }
        break;
      case Remove:
        doAddItem(before);
        break;
      case Reparent:
        break;
      case Invalid:
        qDebug() << "Invalid operation in undo";
        break;
    }
  }



  virtual void doAddItem(const T& item, const QModelIndex& parentIdx = QModelIndex())
  {
    const int row = rowCount(parentIdx);
    insertRows(row, 1, parentIdx);
    const QModelIndex idx = index(row, 0, parentIdx);
    static_cast<TreeItem<T>*>(idx.internalPointer())->dataRef() = item;
    if (m_idToItemMapper) {
      m_idToItemMapper->insert(item.id(), static_cast<TreeItem<T>*>(idx.internalPointer()));
    }
    setDirty();
    emit dataChanged(idx, index(row, columnCount()-1, parentIdx));
  }

  virtual void doModifyItem(const QModelIndex& idx, const T& item)
  {
    if (idx.isValid()) {
      if (m_idToItemMapper) {
        m_idToItemMapper->remove(static_cast<const TreeItem<T>*>(idx.internalPointer())->constDataRef().id());
        m_idToItemMapper->insert(item.id(), static_cast<TreeItem<T>*>(idx.internalPointer()));
      }
      static_cast<TreeItem<T>*>(idx.internalPointer())->dataRef() = item;
      setDirty();
      emit dataChanged(idx, index(idx.row(), columnCount(idx.parent())-1));
    }
  }

  virtual void doRemoveItem(const QModelIndex& idx)
  {
    if (idx.isValid()) {
      if (m_idToItemMapper) {
        m_idToItemMapper->remove(static_cast<const TreeItem<T>*>(idx.internalPointer())->constDataRef().id());
      }
      removeRow(idx.row(), idx.parent());
      setDirty();
    }
  }

  virtual void clearModelItems()
  {
    // get rid of any existing entries should they exist
    if (m_idToItemMapper) {
      m_idToItemMapper->clear();
    }

    if (m_rootItem->childCount()) {
      delete m_rootItem;
      m_rootItem = new TreeItem<T>(T());
    }
  }

protected:
  TreeItem<T> *                 m_rootItem;
  QUndoStack*                   m_undoStack;
  QHash<QString, TreeItem<T>*>* m_idToItemMapper;
};

#endif // MYMONEYMODEL_H
