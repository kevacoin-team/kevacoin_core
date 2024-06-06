// Copyright (c) 2014-2017 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Copyright (c) 2018 the Kevacoin Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef H_KEVACOIN_KEVA_MAIN
#define H_KEVACOIN_KEVA_MAIN

#include <consensus/amount.h>
#include <chain.h>
#include <deploymentstatus.h>
#include <keva/common.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/keva.h>
#include <serialize.h>
#include <uint256.h>

#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>

class CBlockUndo;
class CCoinsView;
class CCoinsViewCache;
class CTxMemPool;
class CTxMemPoolEntry;
class CValidationState;
class ValidationSignals;
class CKevaNotifier;
class TxValidationState;

typedef std::vector<unsigned char> valtype;

/* Some constants defining namespace, key and value limits.  */
static const unsigned MAX_NAMESPACE_LENGTH = 255;
static const unsigned MAX_KEY_LENGTH = 255;
static const unsigned MAX_VALUE_LENGTH = MAX_SCRIPT_ELEMENT_SIZE; // As defined in script.h

/** The amount of coins to lock in created transactions.  */
static const CAmount KEVA_LOCKED_AMOUNT = COIN / 100;

/* ************************************************************************** */
/* CKevaTxUndo.  */

/**
 * Undo information for one name operation.  This contains either the
 * information that the name was newly created (and should thus be
 * deleted entirely) or that it was updated including the old value.
 */
class CKevaTxUndo
{

private:

  /** The namespace this concerns.  */
  valtype nameSpace;

  /** The key this concerns.  */
  valtype key;

  /** Whether this was an entirely new name (no update).  */
  bool isNew;

  /** The old name value that was overwritten by the operation.  */
  CKevaData oldData;

public:

  // SERIALIZE_METHODS(CKevaTxUndo, obj) { READWRITE(obj.nameSpace, obj.key, obj.isNew, obj.oldData); }
  ADD_SERIALIZE_METHODS;

  template<typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
  {
    READWRITE(nameSpace);
    READWRITE(key);
    READWRITE(isNew);
    if (!isNew) {
      READWRITE(oldData);
    }
  }

  /**
   * Set the data for an update/registration of the given name.  The CCoinsView
   * is used to find out all the necessary information.
   * @param nm The name that is being updated.
   * @param view The (old!) chain state.
   */
  void fromOldState(const valtype& nameSpace, const valtype& key, const CCoinsView& view);

  /**
   * Apply the undo to the chain state given.
   * @param view The chain state to update ("undo").
   */
  void apply(CCoinsViewCache& view) const;

};

/* ************************************************************************** */
/* CKevaMemPool.  */

/**
 * Handle the keva component of the transaction mempool.  This keeps track
 * of keva operations that are in the mempool and ensures that all transactions
 * kept are consistent.  E. g., no two transactions are allowed to register
 * the same namespace, or the same name with the same namespace.
 */
class CKevaMemPool
{

private:

  /** The parent mempool object.  Used to, e. g., remove conflicting tx.  */
  CTxMemPool& pool;

  /**
   * Pending/unconfirmed namespaces.
   * Tuple: txid, namespace, display name
   */
  std::vector<std::tuple<uint256, valtype, valtype>> listUnconfirmedNamespaces;

  /**
   * Pending/unconfirmed key-values.
   * Tuple: txid, namespace, key, value
   */
  std::vector<std::tuple<uint256, valtype, valtype, valtype>> listUnconfirmedKeyValues;

  /**
   * Validate that the namespace is the hash of the first TxIn.
   */
  bool validateNamespace(const CTransaction& tx, const valtype& nameSpace, const CBlockIndex* activeChainTip) const;

public:

  // /**
  //  * Construct with reference to parent mempool.
  //  * @param p The parent pool.
  //  */
  explicit inline CKevaMemPool(CTxMemPool& p) : pool(p) {}

  /**
   * Clear all data.
   */
  inline void clear()
  {
    listUnconfirmedNamespaces.clear();
    listUnconfirmedKeyValues.clear();
  }

  /**
   * Added unconfirmed keva values.
   * @param hash The tx hash.
   * @param entry The new keva entry.
   */
  void addUnchecked(const uint256& hash, const CKevaScript& kevaOp);

  /**
   * Remove the given mempool entry.  It is assumed that it is present.
   * @param entry The entry to remove.
   */
  void remove(const uint256& hash);

  /**
   * Remove conflicts for the given tx, based on name operations.  I. e.,
   * if the tx registers a name that conflicts with another registration
   * in the mempool, detect this and remove the mempool tx accordingly.
   * @param tx The transaction for which we look for conflicts.
   * @param removed Put removed tx here.
   */
  void removeConflicts(const CTransaction& tx);

  /**
   * Check if a tx can be added (based on name criteria) without
   * causing a conflict.
   * @param tx The transaction to check.
   * @return True if it doesn't conflict.
   */
  bool checkTx(const CTransaction& tx, const CBlockIndex* activeChainTip) const;

  /** Keva get unconfirmed namespaces. */
  void getUnconfirmedNamespaceList(std::vector<std::tuple<valtype, valtype, uint256>>& nameSpaces) const;

  /** Keva get unconfirmed key value. */
  bool getUnconfirmedKeyValue(const valtype& nameSpace, const valtype& key, valtype& value) const;

  /** Keva get list of unconfirmed key value list. */
  void getUnconfirmedKeyValueList(std::vector<std::tuple<valtype, valtype, valtype, uint256>>& keyValueList, const valtype& nameSpace);

};

/* ************************************************************************** */
/* CNameConflictTracker.  */

/**
 * Utility class that listens to a mempool's removal notifications to track
 * name conflicts.  This is used for DisconnectTip and unit testing.
 */
class CNameConflictTracker
{

private:

  std::shared_ptr<std::vector<CTransactionRef>> txNameConflicts;
  CTxMemPool& pool;

public:

  explicit CNameConflictTracker(CTxMemPool &p);
  ~CNameConflictTracker();

  inline const std::shared_ptr<const std::vector<CTransactionRef>>
  GetNameConflicts() const
  {
    return txNameConflicts;
  }

  void AddConflictedEntry(CTransactionRef txRemoved);

};

/**
 * Notify Keva transactions. This is for ZMQ notification.
 */
class CKevaNotifier
{
private:
    ValidationSignals* signals;

public:
    CKevaNotifier(ValidationSignals*);
    void KevaNamespaceCreated(const CTransaction& tx, int nHeight, const std::string& nameSpace);
    void KevaUpdated(const CTransaction& tx, int nHeight, const std::string& nameSpace, const std::string& key, const std::string& value);
    void KevaDeleted(const CTransaction& tx, int nHeight, const std::string& nameSpace, const std::string& key);
};

/* ************************************************************************** */

/**
 * Check a transaction according to the additional Kevacoin rules.  This
 * ensures that all keva operations (if any) are valid and that it has
 * name operations iff it is marked as Namecoin tx by its version.
 * @param tx The transaction to check.
 * @param nHeight Height at which the tx will be.
 * @param view The current chain state.
 * @param state Resulting validation state.
 * @param flags Verification flags.
 * @return True in case of success.
 */
bool CheckKevaTransaction(const CTransaction& tx, unsigned nHeight, const CCoinsView& view,
                          TxValidationState& state, unsigned flags);

/**
 * Apply the changes of a keva transaction to the database.
 * @param tx The transaction to apply.
 * @param pindex Pointer to the block.
 * @param view The chain state to update.
 * @param undo Record undo information here.
 */
void ApplyKevaTransaction(const CTransaction& tx, int nHeight,
                          CCoinsViewCache& view, CBlockUndo& undo, CKevaNotifier& notifier);

/**
 * Expire all names at the given height.  This removes their coins
 * from the UTXO set.
 * @param height The new block height.
 * @param view The coins view to update.
 * @param undo The block undo object to record undo information.
 * @param names List all expired names here.
 * @return True if successful.
 */
bool ExpireNames(unsigned nHeight, CCoinsViewCache& view, CBlockUndo& undo,
                 std::set<valtype>& names);

/**
 * Undo name coin expirations.  This also does some checks verifying
 * that all is fine.
 * @param nHeight The height at which the names were expired.
 * @param undo The block undo object to use.
 * @param view The coins view to update.
 * @param names List all unexpired names here.
 * @return True if successful.
 */
bool UnexpireNames(unsigned nHeight, CBlockUndo& undo,
                   CCoinsViewCache& view, std::set<valtype>& names);

/**
 * Check the name database consistency.  This calls CCoinsView::ValidateKevaDB,
 * but only if applicable depending on the -checkkevadb setting.  If it fails,
 * this throws an assertion failure.
 * @param disconnect Whether we are disconnecting blocks.
 */
void CheckNameDB(bool disconnect);

template<typename DEP>
bool DeploymentActiveAt(const CBlockIndex& index, const Consensus::Params& params, DEP dep)
{
    return DeploymentActiveAt(index, params, dep);
}

#endif // H_KEVACOIN_KEVA_MAIN
