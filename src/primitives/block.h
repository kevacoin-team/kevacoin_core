// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KEVACOINNNNNN_PRIMITIVES_BLOCK_H
#define KEVACOINNNNNN_PRIMITIVES_BLOCK_H

#include <primitives/transaction.h>
#include <serialize.h>
#include <uint256.h>
#include <util/time.h>


#include <cryptonote_basic/cryptonote_format_utils.h>

/**
 * This header is to store the proof-of-work of cryptonote mining.
 * Kevacoin uses Cryptonight PoW and uses its existing infrastructure
 * (mining pools and miners)
 */
class CryptoNoteHeader
{
public:
    uint8_t major_version;
    uint8_t minor_version;  // now used as a voting mechanism, rather than how this particular block is built
    uint64_t timestamp;
    uint256  prev_id;
    uint32_t nonce;
    uint256  merkle_root;
    size_t   nTxes; // Number of transactions.

    CryptoNoteHeader()
    {
        SetNull();
    }

    void SetNull()
    {
        major_version = 0;
        minor_version = 0;
        prev_id.SetNull();
        timestamp = 0;
        nonce = 0;
        merkle_root.SetNull();
        nTxes = 0;
    }

    bool IsNull() const
    {
        return (timestamp == 0);
    }

    // load
    template <template <bool> class Archive>
    bool do_serialize(Archive<false>& ar)
    {
      VARINT_FIELD(major_version)
      VARINT_FIELD(minor_version)
      VARINT_FIELD(timestamp)
      crypto::hash prev_hash;
      FIELD(prev_hash)
      memcpy(prev_id.begin(), &prev_hash, prev_id.size());
      FIELD(nonce)
      crypto::hash merkle_hash;
      FIELD(merkle_hash)
      memcpy(merkle_root.begin(), &merkle_hash, merkle_root.size());
      VARINT_FIELD(nTxes)
      return true;
    }

    // store
    template <template <bool> class Archive>
    bool do_serialize(Archive<true>& ar)
    {
      VARINT_FIELD(major_version)
      VARINT_FIELD(minor_version)
      VARINT_FIELD(timestamp)
      crypto::hash prev_hash;
      memcpy(&prev_hash, prev_id.begin(), prev_id.size());
      FIELD(prev_hash)
      FIELD(nonce)
      crypto::hash merkle_hash;
      memcpy(&merkle_hash, merkle_root.begin(), merkle_root.size());
      FIELD(merkle_hash)
      VARINT_FIELD(nTxes)
      return true;
    }


    // SERIALIZE_METHODS(CryptoNoteHeader, obj) { READWRITE(obj.major_version, obj.minor_version, obj.timestamp, obj.prev_id, obj.nonce, obj.merkle_root, obj.nTxes); }
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        if (ser_action.ForRead()) {
            std::string blob;
            READWRITE(blob);
            std::stringstream ss;
            ss << blob;
            // load
            binary_archive<false> ba(ss);
            ::serialization::serialize(ba, *this);
        } else {
            std::stringstream ss;
            // store
            binary_archive<true> ba(ss);
            ::serialization::serialize(ba, *this);
            std::string blob = ss.str();
            READWRITE(blob);
        }
    }
};

/** Nodes collect new transactions into a block, hash them into a hash tree,
 * and scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements.  When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain.  The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 */
class CBlockHeader
{
public:
    // header
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    // nNonce is no longer used as nonce, as the nonce is now in cnHeader.
    // Repurpose it for block height used in Cryptnight variant 4.
    uint32_t nNonce;
    // CryptoNote header for emulation or merged mining
    CryptoNoteHeader cnHeader;

    CBlockHeader()
    {
        SetNull();
    }

    SERIALIZE_METHODS(CBlockHeader, obj) { READWRITE(obj.nVersion, obj.hashPrevBlock, obj.hashMerkleRoot, obj.nTime, obj.nBits, obj.nNonce, obj.cnHeader); }

    void SetNull()
    {
        nVersion = 0;
        hashPrevBlock.SetNull();
        hashMerkleRoot.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
        cnHeader.SetNull();
    }

    bool IsNull() const
    {
        return (nBits == 0);
    }

    uint256 GetOriginalBlockHash() const;
    uint256 GetHash() const;
    uint256 GetPoWHash(uint64_t, uint256&) const;

    NodeSeconds Time() const
    {
        return NodeSeconds{std::chrono::seconds{nTime}};
    }

    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }

    // void SetLegacy(bool legacy)
    // {
    //     legacyMode = legacy;
    // }

    bool isCNConsistent() const;

    // bool isLegacy()
    // {
    //     return legacyMode;
    // }

// protected:
//     // When legacyMode is true, cnHeader is not used.
//     // This is used for regtest.
//     bool legacyMode;
};


class CBlock : public CBlockHeader
{
public:
    // network and disk
    std::vector<CTransactionRef> vtx;

    // Memory-only flags for caching expensive checks
    mutable bool fChecked;                            // CheckBlock()
    mutable bool m_checked_witness_commitment{false}; // CheckWitnessCommitment()
    mutable bool m_checked_merkle_root{false};        // CheckMerkleRoot()

    CBlock()
    {
        SetNull();
    }

    CBlock(const CBlockHeader &header)
    {
        SetNull();
        *(static_cast<CBlockHeader*>(this)) = header;
    }

    SERIALIZE_METHODS(CBlock, obj)
    {
        READWRITE(AsBase<CBlockHeader>(obj), obj.vtx);
    }

    void SetNull()
    {
        CBlockHeader::SetNull();
        vtx.clear();
        fChecked = false;
        m_checked_witness_commitment = false;
        m_checked_merkle_root = false;
    }

    CBlockHeader GetBlockHeader() const
    {
        CBlockHeader block;
        block.nVersion       = nVersion;
        block.hashPrevBlock  = hashPrevBlock;
        block.hashMerkleRoot = hashMerkleRoot;
        block.nTime          = nTime;
        block.nBits          = nBits;
        block.nNonce         = nNonce;
        block.cnHeader       = cnHeader;
        // block.SetLegacy(legacyMode);
        return block;
    }

    std::string ToString() const;
};

/** Describes a place in the block chain to another node such that if the
 * other node doesn't have the same branch, it can find a recent common trunk.
 * The further back it is, the further before the fork it may be.
 */
struct CBlockLocator
{
    /** Historically CBlockLocator's version field has been written to network
     * streams as the negotiated protocol version and to disk streams as the
     * client version, but the value has never been used.
     *
     * Hard-code to the highest protocol version ever written to a network stream.
     * SerParams can be used if the field requires any meaning in the future,
     **/
    static constexpr int DUMMY_VERSION = 70016;

    std::vector<uint256> vHave;

    CBlockLocator() {}

    explicit CBlockLocator(std::vector<uint256>&& have) : vHave(std::move(have)) {}

    SERIALIZE_METHODS(CBlockLocator, obj)
    {
        int nVersion = DUMMY_VERSION;
        READWRITE(nVersion);
        READWRITE(obj.vHave);
    }

    void SetNull()
    {
        vHave.clear();
    }

    bool IsNull() const
    {
        return vHave.empty();
    }
};

#endif // KEVACOINNNNNN_PRIMITIVES_BLOCK_H
