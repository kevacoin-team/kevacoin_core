// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>
#include <validation.h>

#include <crypto/common.h>
#include <crypto/hash-ops.h>

#define BEGIN(a)            ((char*)&(a))

extern "C" void cn_slow_hash(const void *data, size_t length, char *hash, int variant, int prehashed, uint64_t height);

// Copy and modified from CalculateDogecoinNextWorkRequired (dogecoin.cpp)
unsigned int CalculateDigishieldNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    const int64_t retargetTimespan = params.nPowTargetTimespan;
    const int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    int64_t nModulatedTimespan = nActualTimespan;
    int64_t nMaxTimespan;
    int64_t nMinTimespan;

    // amplitude filter - thanks to daft27 for this code
    nModulatedTimespan = retargetTimespan + (nModulatedTimespan - retargetTimespan) / 8;

    nMinTimespan = retargetTimespan - (retargetTimespan / 4);
    nMaxTimespan = retargetTimespan + (retargetTimespan / 2);

    // Limit adjustment step
    if (nModulatedTimespan < nMinTimespan)
        nModulatedTimespan = nMinTimespan;
    else if (nModulatedTimespan > nMaxTimespan)
        nModulatedTimespan = nMaxTimespan;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    arith_uint256 bnOld;
    bnNew.SetCompact(pindexLast->nBits);
    bnOld = bnNew;
    bnNew *= nModulatedTimespan;
    bnNew /= retargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    // unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // // Only change once per difficulty adjustment interval
    // if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    // {
    //     if (params.fPowAllowMinDifficultyBlocks)
    //     {
    //         // Special difficulty rule for testnet:
    //         // If the new block's timestamp is more than 2* 10 minutes
    //         // then allow mining of a min-difficulty block.
    //         if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
    //             return nProofOfWorkLimit;
    //         else
    //         {
    //             // Return the last non-special-min-difficulty-rules-block
    //             const CBlockIndex* pindex = pindexLast;
    //             while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
    //                 pindex = pindex->pprev;
    //             return pindex->nBits;
    //         }
    //     }
    //     return pindexLast->nBits;
    // }

    // Kevacoin: This fixes an issue where a 51% attack can change difficulty at will.
    // Go back the full period unless it's the first retarget after genesis. Code courtesy of Art Forz
    int blockstogoback = params.DifficultyAdjustmentInterval()-1;
    if ((pindexLast->nHeight+1) != params.DifficultyAdjustmentInterval())
        blockstogoback = params.DifficultyAdjustmentInterval();

    // Go back by what we want to be 14 days worth of blocks
    // int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    // assert(nHeightFirst >= 0);
    // const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    const CBlockIndex* pindexFirst = pindexLast;
    for (int i = 0; pindexFirst && i < blockstogoback; i++)
        pindexFirst = pindexFirst->pprev;
    assert(pindexFirst);

    return CalculateDigishieldNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

// Check that on difficulty adjustments, the new difficulty does not increase
// or decrease beyond the permitted limits.
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    if (params.fPowAllowMinDifficultyBlocks) return true;

    if (height % params.DifficultyAdjustmentInterval() == 0) {
        int64_t smallest_timespan = params.nPowTargetTimespan/4;
        int64_t largest_timespan = params.nPowTargetTimespan*4;

        const arith_uint256 pow_limit = UintToArith256(params.powLimit);
        arith_uint256 observed_new_target;
        observed_new_target.SetCompact(new_nbits);

        // Calculate the largest difficulty value possible:
        arith_uint256 largest_difficulty_target;
        largest_difficulty_target.SetCompact(old_nbits);
        largest_difficulty_target *= largest_timespan;
        largest_difficulty_target /= params.nPowTargetTimespan;

        if (largest_difficulty_target > pow_limit) {
            largest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 maximum_new_target;
        maximum_new_target.SetCompact(largest_difficulty_target.GetCompact());
        if (maximum_new_target < observed_new_target) return false;

        // Calculate the smallest difficulty value possible:
        arith_uint256 smallest_difficulty_target;
        smallest_difficulty_target.SetCompact(old_nbits);
        smallest_difficulty_target *= smallest_timespan;
        smallest_difficulty_target /= params.nPowTargetTimespan;

        if (smallest_difficulty_target > pow_limit) {
            smallest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 minimum_new_target;
        minimum_new_target.SetCompact(smallest_difficulty_target.GetCompact());
        if (minimum_new_target > observed_new_target) return false;
    } else if (old_nbits != new_nbits) {
        return false;
    }
    return true;
}

static void cn_get_block_hash_by_height(uint64_t seed_height, char cnHash[32])
{
    LOCK(cs_main);
    CBlockIndex* pblockindex = g_chainman->ActiveChain()[seed_height];
    
    if (pblockindex == nullptr) {
        // This will happen during initial block downloading, or when we
        // are out of sync by more than at least SEEDHASH_EPOCH_BLOCKS blocks.
        pblockindex = g_chainman->m_blockman.GetBlockSeedHeight(seed_height);
        if (pblockindex == nullptr) {
            int pp = 0;
            return;
        }
    }
    uint256 blockHash = pblockindex->GetBlockHash();
    const unsigned char* pHash = blockHash.begin();
    for (int j = 31; j >= 0; j--) {
        cnHash[31 - j] = pHash[j];
    }
}

const uint256 GetPoWHash(const CBlockHeader& header)
{
    if (!(header.isCNConsistent())) {
        return (HashWriter{} << header).GetHash();
        // memset(thash.begin(), 0xff, thash.size());
        // return thash;
    }

    uint256 thash;
    cryptonote::blobdata blob = cryptonote::t_serializable_object_to_blob(header.cnHeader);
    uint32_t height = header.nNonce;
    if (header.cnHeader.major_version >= RX_BLOCK_VERSION) {
        uint64_t seed_height = crypto::rx_seedheight(height);
        char cnHash[32];
        cn_get_block_hash_by_height(seed_height, cnHash);
        crypto::rx_slow_hash(height, seed_height, cnHash, blob.data(), blob.size(), BEGIN(thash), 0, 0);
    } else {
        cn_slow_hash(blob.data(), blob.size(), BEGIN(thash), header.cnHeader.major_version - 6, 0, height);
    }

    return thash;
}

bool CheckProofOfWork(CBlock block, unsigned int nBits, const Consensus::Params& params)
{
    return CheckProofOfWork(block.GetBlockHeader(), nBits, params);
}

bool CheckProofOfWork(CBlockHeader header, unsigned int nBits, const Consensus::Params& params)
{
    uint256 hash = GetPoWHash(header);
    return CheckProofOfWork(hash, nBits, params);
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
