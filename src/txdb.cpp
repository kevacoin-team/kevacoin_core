// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <txdb.h>

#include <keva/common.h>
#include <coins.h>
#include <dbwrapper.h>
#include <logging.h>
#include <primitives/transaction.h>
#include <random.h>
#include <script/keva.h>
#include <serialize.h>
#include <uint256.h>
#include <util/vector.h>

#include <cassert>
#include <cstdlib>
#include <iterator>
#include <utility>

static constexpr uint8_t DB_COIN{'C'};
static constexpr uint8_t DB_BEST_BLOCK{'B'};
static constexpr uint8_t DB_HEAD_BLOCKS{'H'};
// Keys used in previous version that might still be found in the DB:
static constexpr uint8_t DB_COINS{'c'};
static constexpr uint8_t DB_NAME{'n'};
static constexpr uint8_t DB_NS_ASSOC{'a'};

bool CCoinsViewDB::NeedsUpgrade()
{
    std::unique_ptr<CDBIterator> cursor{m_db->NewIterator()};
    // DB_COINS was deprecated in v0.15.0, commit
    // 1088b02f0ccd7358d2b7076bb9e122d59d502d02
    cursor->Seek(std::make_pair(DB_COINS, uint256{}));
    return cursor->Valid();
}

namespace {

struct CoinEntry {
    COutPoint* outpoint;
    uint8_t key;
    explicit CoinEntry(const COutPoint* ptr) : outpoint(const_cast<COutPoint*>(ptr)), key(DB_COIN)  {}

    SERIALIZE_METHODS(CoinEntry, obj) { READWRITE(obj.key, obj.outpoint->hash, VARINT(obj.outpoint->n)); }
};

} // namespace

CCoinsViewDB::CCoinsViewDB(DBParams db_params, CoinsViewOptions options) :
    m_db_params{std::move(db_params)},
    m_options{std::move(options)},
    m_db{std::make_unique<CDBWrapper>(m_db_params)} { }

void CCoinsViewDB::ResizeCache(size_t new_cache_size)
{
    // We can't do this operation with an in-memory DB since we'll lose all the coins upon
    // reset.
    if (!m_db_params.memory_only) {
        // Have to do a reset first to get the original `m_db` state to release its
        // filesystem lock.
        m_db.reset();
        m_db_params.cache_bytes = new_cache_size;
        m_db_params.wipe_data = false;
        m_db = std::make_unique<CDBWrapper>(m_db_params);
    }
}

bool CCoinsViewDB::GetCoin(const COutPoint &outpoint, Coin &coin) const {
    return m_db->Read(CoinEntry(&outpoint), coin);
}

bool CCoinsViewDB::HaveCoin(const COutPoint &outpoint) const {
    return m_db->Exists(CoinEntry(&outpoint));
}

uint256 CCoinsViewDB::GetBestBlock() const {
    uint256 hashBestChain;
    if (!m_db->Read(DB_BEST_BLOCK, hashBestChain))
        return uint256();
    return hashBestChain;
}

std::vector<uint256> CCoinsViewDB::GetHeadBlocks() const {
    std::vector<uint256> vhashHeadBlocks;
    if (!m_db->Read(DB_HEAD_BLOCKS, vhashHeadBlocks)) {
        return std::vector<uint256>();
    }
    return vhashHeadBlocks;
}

class CDbKeyIterator : public CKevaIterator
{

private:

    /* The backing LevelDB iterator.  */
    CDBIterator* iter;

    /* This iterator is for namespace association search. */
    bool isAssociation;

public:

    ~CDbKeyIterator();

    /**
     * Construct a new name iterator for the database.
     * @param db The database to create the iterator for.
     */
    CDbKeyIterator(const CDBWrapper& db, const valtype& nameSpace, bool association=false);

    /* Implement iterator methods.  */
    void seek(const valtype& start);
    bool next(valtype& key, CKevaData& data);

};

CDbKeyIterator::~CDbKeyIterator() {
    delete iter;
}

CDbKeyIterator::CDbKeyIterator(const CDBWrapper& db, const valtype& ns, bool association)
    : CKevaIterator(ns), iter(const_cast<CDBWrapper*>(&db)->NewIterator()), isAssociation(association)
{
    seek(valtype());
}

void CDbKeyIterator::seek(const valtype& start) {
    auto &prefix = isAssociation ? DB_NS_ASSOC : DB_NAME;
    iter->Seek(std::make_pair(prefix, std::make_pair(nameSpace, start)));
}

bool CDbKeyIterator::next(valtype& key, CKevaData& data) {
    if (!iter->Valid())
        return false;

    auto &prefix = isAssociation ? DB_NS_ASSOC : DB_NAME;
    std::pair<int8_t, std::pair<valtype, valtype>> curKey;
    if (!iter->GetKey(curKey) || curKey.first != prefix)
        return false;

    valtype curNameSpace = std::get<0>(curKey.second);
    if (curNameSpace != nameSpace) {
        return false;
    }
    key = std::get<1>(curKey.second);

    if (!iter->GetValue(data)) {
        LogError("%s : failed to read data from iterator", __func__);
        return false;
    }

    iter->Next();
    return true;
}

CKevaIterator* CCoinsViewDB::IterateKeys(const valtype& nameSpace) const {
    
    return new CDbKeyIterator(*m_db, nameSpace);
}

CKevaIterator* CCoinsViewDB::IterateAssociatedNamespaces(const valtype& nameSpace) const {
    return new CDbKeyIterator(*m_db, nameSpace, true);
}

bool CCoinsViewDB::GetNamespace(const valtype &nameSpace, CKevaData &data) const {
    return m_db->Read(std::make_pair(DB_NAME, std::make_pair(nameSpace, CKevaScript::KEVA_DISPLAY_NAME_KEY)), data);
}

bool CCoinsViewDB::GetName(const valtype &nameSpace, const valtype &key, CKevaData &data) const {
    return m_db->Read(std::make_pair(DB_NAME, std::make_pair(nameSpace, key)), data);
}

bool CCoinsViewDB::GetNamesForHeight(unsigned nHeight, std::set<valtype>& names) const {
    return false;
}

bool CCoinsViewDB::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock, const CKevaCache &names, bool erase) {
    CDBBatch batch(*m_db);
    size_t count = 0;
    size_t changed = 0;
    assert(!hashBlock.IsNull());

    uint256 old_tip = GetBestBlock();
    if (old_tip.IsNull()) {
        // We may be in the middle of replaying.
        std::vector<uint256> old_heads = GetHeadBlocks();
        if (old_heads.size() == 2) {
            if (old_heads[0] != hashBlock) {
                LogPrintLevel(BCLog::COINDB, BCLog::Level::Error, "The coins database detected an inconsistent state, likely due to a previous crash or shutdown. You will need to restart kevacoind with the -reindex-chainstate or -reindex configuration option.\n");
            }
            assert(old_heads[0] == hashBlock);
            old_tip = old_heads[1];
        }
    }

    // In the first batch, mark the database as being in the middle of a
    // transition from old_tip to hashBlock.
    // A vector is used for future extensibility, as we may want to support
    // interrupting after partial writes from multiple independent reorgs.
    batch.Erase(DB_BEST_BLOCK);
    batch.Write(DB_HEAD_BLOCKS, Vector(hashBlock, old_tip));

    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        if (it->second.flags & CCoinsCacheEntry::DIRTY) {
            CoinEntry entry(&it->first);
            if (it->second.coin.IsSpent())
                batch.Erase(entry);
            else
                batch.Write(entry, it->second.coin);
            changed++;
        }
        count++;
        it = erase ? mapCoins.erase(it) : std::next(it);
        if (batch.SizeEstimate() > m_options.batch_write_bytes) {
            LogPrint(BCLog::COINDB, "Writing partial batch of %.2f MiB\n", batch.SizeEstimate() * (1.0 / 1048576.0));
            m_db->WriteBatch(batch);
            batch.Clear();
            if (m_options.simulate_crash_ratio) {
                static FastRandomContext rng;
                if (rng.randrange(m_options.simulate_crash_ratio) == 0) {
                    LogPrintf("Simulating a crash. Goodbye.\n");
                    _Exit(0);
                }
            }
        }
    }

    names.writeBatch(batch);

    // In the last batch, mark the database as consistent with hashBlock again.
    batch.Erase(DB_HEAD_BLOCKS);
    batch.Write(DB_BEST_BLOCK, hashBlock);

    LogPrint(BCLog::COINDB, "Writing final batch of %.2f MiB\n", batch.SizeEstimate() * (1.0 / 1048576.0));
    bool ret = m_db->WriteBatch(batch);
    LogPrint(BCLog::COINDB, "Committed %u changed transaction outputs (out of %u) to coin database...\n", (unsigned int)changed, (unsigned int)count);
    return ret;
}

size_t CCoinsViewDB::EstimateSize() const
{
    return m_db->EstimateSize(DB_COIN, uint8_t(DB_COIN + 1));
}

/** Specialization of CCoinsViewCursor to iterate over a CCoinsViewDB */
class CCoinsViewDBCursor: public CCoinsViewCursor
{
public:
    // Prefer using CCoinsViewDB::Cursor() since we want to perform some
    // cache warmup on instantiation.
    CCoinsViewDBCursor(CDBIterator* pcursorIn, const uint256&hashBlockIn):
        CCoinsViewCursor(hashBlockIn), pcursor(pcursorIn) {}
    ~CCoinsViewDBCursor() = default;

    bool GetKey(COutPoint &key) const override;
    bool GetValue(Coin &coin) const override;

    bool Valid() const override;
    void Next() override;

private:
    std::unique_ptr<CDBIterator> pcursor;
    std::pair<char, COutPoint> keyTmp;

    friend class CCoinsViewDB;
};

std::unique_ptr<CCoinsViewCursor> CCoinsViewDB::Cursor() const
{
    auto i = std::make_unique<CCoinsViewDBCursor>(
        const_cast<CDBWrapper&>(*m_db).NewIterator(), GetBestBlock());
    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    i->pcursor->Seek(DB_COIN);
    // Cache key of first record
    if (i->pcursor->Valid()) {
        CoinEntry entry(&i->keyTmp.second);
        i->pcursor->GetKey(entry);
        i->keyTmp.first = entry.key;
    } else {
        i->keyTmp.first = 0; // Make sure Valid() and GetKey() return false
    }
    return i;
}

bool CCoinsViewDBCursor::GetKey(COutPoint &key) const
{
    // Return cached key
    if (keyTmp.first == DB_COIN) {
        key = keyTmp.second;
        return true;
    }
    return false;
}

bool CCoinsViewDBCursor::GetValue(Coin &coin) const
{
    return pcursor->GetValue(coin);
}

bool CCoinsViewDBCursor::Valid() const
{
    return keyTmp.first == DB_COIN;
}

void CKevaCache::writeBatch (CDBBatch& batch) const
{
  for (EntryMap::const_iterator i = entries.begin(); i != entries.end(); ++i) {
    std::pair<valtype, valtype> name = std::make_pair(std::get<0>(i->first), std::get<1>(i->first));
    batch.Write(std::make_pair(DB_NAME, name), i->second);
  }

  for (NamespaceMap::const_iterator i = associations.begin(); i != associations.end(); ++i) {
    std::pair<valtype, valtype> name = std::make_pair(std::get<0>(i->first), std::get<1>(i->first));
    batch.Write(std::make_pair(DB_NS_ASSOC, name), i->second);
  }

  for (std::set<NamespaceKeyType>::const_iterator i = deleted.begin(); i != deleted.end(); ++i) {
    std::pair<valtype, valtype> name = std::make_pair(std::get<0>(*i), std::get<1>(*i));
    batch.Erase(std::make_pair(DB_NAME, name));
  }

  for (std::set<NamespaceKeyType>::const_iterator i = disassociations.begin(); i != disassociations.end(); ++i) {
    std::pair<valtype, valtype> name = std::make_pair(std::get<0>(*i), std::get<1>(*i));
    batch.Erase(std::make_pair(DB_NS_ASSOC, name));
  }
}

void CCoinsViewDBCursor::Next()
{
    pcursor->Next();
    CoinEntry entry(&keyTmp.second);
    if (!pcursor->Valid() || !pcursor->GetKey(entry)) {
        keyTmp.first = 0; // Invalidate cached key after last record so that Valid() and GetKey() return false
    } else {
        keyTmp.first = entry.key;
    }
}
