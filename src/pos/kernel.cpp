// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2014 The BlackCoin developers
// Copyright (c) 2017 The Particl Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp>

#include <pos/kernel.h>
#include <txdb.h>
#include <validation.h>
#include <chainparams.h>
#include <serialize.h>
#include <streams.h>
#include <hash.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/ismine.h> // valtype
#include <policy/policy.h>
#include <consensus/validation.h>
#include <coins.h>

/**
 * Stake Modifier (hash modifier of proof-of-stake):
 * The purpose of stake modifier is to prevent a txout (coin) owner from
 * computing future proof-of-stake generated by this txout at the time
 * of transaction confirmation. To meet kernel protocol, the txout
 * must hash with a future stake modifier to generate the proof.
 */
uint256 ComputeStakeModifierV2(const CBlockIndex *pindexPrev, const uint256 &kernel)
{
    if (!pindexPrev)
        return uint256();  // genesis block's modifier is 0

    CDataStream ss(SER_GETHASH, 0);
    ss << kernel << pindexPrev->bnStakeModifier;
    return Hash(ss.begin(), ss.end());
}

/**
 * BlackCoin kernel protocol
 * coinstake must meet hash target according to the protocol:
 * kernel (input 0) must meet the formula
 *     hash(nStakeModifier + txPrev.block.nTime + txPrev.nTime + txPrev.vout.hash + txPrev.vout.n + nTime) < bnTarget * nWeight
 * this ensures that the chance of getting a coinstake is proportional to the
 * amount of coins one owns.
 * The reason this hash is chosen is the following:
 *   nStakeModifier: scrambles computation to make it very difficult to precompute
 *                   future proof-of-stake
 *   txPrev.block.nTime: prevent nodes from guessing a good timestamp to
 *                       generate transaction for future advantage,
 *                       obsolete since v3
 *   txPrev.nTime: slightly scrambles computation
 *   txPrev.vout.hash: hash of txPrev, to reduce the chance of nodes
 *                     generating coinstake at the same time
 *   txPrev.vout.n: output number of txPrev, to reduce the chance of nodes
 *                  generating coinstake at the same time
 *   nTime: current timestamp
 *   block/tx hash should not be used here as they can be generated in vast
 *   quantities so as to generate blocks faster, degrading the system back into
 *   a proof-of-work situation.
 */
bool CheckStakeKernelHash(const CBlockIndex *pindexPrev,
    uint32_t nBits, uint32_t nBlockFromTime,
    CAmount prevOutAmount, const COutPoint &prevout, uint32_t nTime,
    uint256 &hashProofOfStake, uint256 &targetProofOfStake,
    bool fPrintProofOfStake)
{
    // CheckStakeKernelHashV2

    if (nTime < nBlockFromTime)  // Transaction timestamp violation
        return error("%s: nTime violation", __func__);

    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0)
        return error("%s: SetCompact failed.", __func__);

    // Weighted target
    int64_t nValueIn = prevOutAmount;
    arith_uint256 bnWeight = arith_uint256(nValueIn);
    bnTarget *= bnWeight;

    targetProofOfStake = ArithToUint256(bnTarget);

    uint256 bnStakeModifier = pindexPrev->bnStakeModifier;
    int nStakeModifierHeight = pindexPrev->nHeight;
    int64_t nStakeModifierTime = pindexPrev->nTime;

    CDataStream ss(SER_GETHASH, 0);
    ss << bnStakeModifier;
    ss << nBlockFromTime << prevout.hash << prevout.n << nTime;
    hashProofOfStake = Hash(ss.begin(), ss.end());

    if (fPrintProofOfStake)
    {
        LogPrintf("%s: using modifier=%s at height=%d timestamp=%s\n",
            __func__, bnStakeModifier.ToString(), nStakeModifierHeight,
            DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nStakeModifierTime));
        LogPrintf("%s: check modifier=%s nTimeKernel=%u nPrevout=%u nTime=%u hashProof=%s\n",
            __func__, bnStakeModifier.ToString(),
            nBlockFromTime, prevout.n, nTime,
            hashProofOfStake.ToString());
    };

    // Now check if proof-of-stake hash meets target protocol
    if (UintToArith256(hashProofOfStake) > bnTarget)
        return false;

    if (LogAcceptCategory(BCLog::POS) && !fPrintProofOfStake)
    {
        LogPrintf("%s: using modifier=%s at height=%d timestamp=%s\n",
            __func__, bnStakeModifier.ToString(), nStakeModifierHeight,
            DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nStakeModifierTime));
        LogPrintf("%s: pass modifier=%s nTimeKernel=%u nPrevout=%u nTime=%u hashProof=%s\n",
            __func__, bnStakeModifier.ToString(),
            nBlockFromTime, prevout.n, nTime,
            hashProofOfStake.ToString());
    };

    return true;
}

bool IsConfirmedInNPrevBlocks(const uint256 &hashBlock, const CBlockIndex *pindexFrom, int nMaxDepth, int &nActualDepth)
{
    for (const CBlockIndex *pindex = pindexFrom; pindex && pindexFrom->nHeight - pindex->nHeight < nMaxDepth; pindex = pindex->pprev)
    if (hashBlock == pindex->GetBlockHash())
    {
        nActualDepth = pindexFrom->nHeight - pindex->nHeight;
        return true;
    };

    return false;
}


static bool CheckAge(const CBlockIndex *pindexTip, const uint256 &hashKernelBlock, int &nDepth)
{
    // pindexTip is the current tip of the chain
    // hashKernelBlock is the hash of the block containing the kernel transaction

    int nRequiredDepth = std::min((int)(Params().GetStakeMinConfirmations()-1), (int)(pindexTip->nHeight / 2));

    if (IsConfirmedInNPrevBlocks(hashKernelBlock, pindexTip, nRequiredDepth, nDepth))
        return false;
    return true;
}

// Check kernel hash target and coinstake signature
bool CheckProofOfStake(const CBlockIndex *pindexPrev, const CTransaction &tx, int64_t nTime, unsigned int nBits, uint256 &hashProofOfStake, uint256 &targetProofOfStake)
{
    // pindexPrev is the current tip, the block the new block will connect on
    // nTime is the time of the new/next block
    CValidationState state;

    if (!tx.IsCoinStake()
        || tx.vin.size() < 1)
        return state.DoS(100, error("%s: malformed-txn %s", __func__, tx.GetHash().ToString()), REJECT_INVALID, "malformed-txn");

    uint256 hashBlock;
    CTransactionRef txPrev;

    // Kernel (input 0) must match the stake hash target per coin age (nBits)
    const CTxIn &txin = tx.vin[0];

    uint32_t nBlockFromTime;
    int nDepth;
    CScript kernelPubKey;
    CAmount amount;

    Coin coin;
    if (!pcoinsTip->GetCoin(txin.prevout, coin) || coin.IsSpent())
    {
        // Must find the prevout in the txdb / blocks

        CBlock blockKernel; // block containing stake kernel, GetTransaction should only fill the header.
        if (!GetTransaction(txin.prevout.hash, txPrev, Params().GetConsensus(), blockKernel, true)
            || txin.prevout.n >= txPrev->vpout.size())
            return state.DoS(10, error("%s: prevout-not-in-chain", __func__), REJECT_INVALID, "prevout-not-in-chain");

        const CTxOutBase *outPrev = txPrev->vpout[txin.prevout.n].get();
        if (!outPrev->IsStandardOutput())
            return state.DoS(100, error("%s: invalid-prevout", __func__), REJECT_INVALID, "invalid-prevout");

        int nDepth;
        if (!CheckAge(pindexPrev, hashBlock, nDepth))
            return state.DoS(100, error("%s: Tried to stake at depth %d", __func__, nDepth + 1), REJECT_INVALID, "invalid-stake-depth");

        kernelPubKey = *outPrev->GetPScriptPubKey();
        amount = outPrev->GetValue();
        nBlockFromTime = blockKernel.nTime;
    } else
    {
        if (coin.nType != OUTPUT_STANDARD)
            return state.DoS(100, error("%s: invalid-prevout", __func__), REJECT_INVALID, "invalid-prevout");

        CBlockIndex *pindex = chainActive[coin.nHeight];
        if (!pindex)
            return state.DoS(100, error("%s: invalid-prevout", __func__), REJECT_INVALID, "invalid-prevout");

        nDepth = pindexPrev->nHeight - coin.nHeight;
        int nRequiredDepth = std::min((int)(Params().GetStakeMinConfirmations()-1), (int)(pindexPrev->nHeight / 2));
        if (nRequiredDepth > nDepth)
            return state.DoS(100, error("%s: Tried to stake at depth %d", __func__, nDepth + 1), REJECT_INVALID, "invalid-stake-depth");

        kernelPubKey = coin.out.scriptPubKey;
        amount = coin.out.nValue;
        nBlockFromTime = pindex->GetBlockTime();
    };

    const CScript &scriptSig = txin.scriptSig;
    const CScriptWitness *witness = &txin.scriptWitness;
    ScriptError serror = SCRIPT_ERR_OK;
    std::vector<uint8_t> vchAmount(8);
    memcpy(&vchAmount[0], &amount, 8);
    // Redundant: all inputs are checked later during CheckInputs
    if (!VerifyScript(scriptSig, kernelPubKey, witness, STANDARD_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&tx, 0, vchAmount), &serror))
        return state.DoS(100, error("%s: verify-script-failed, txn %s, reason %s", __func__, tx.GetHash().ToString(), ScriptErrorString(serror)),
            REJECT_INVALID, "verify-script-failed");



    if (!CheckStakeKernelHash(pindexPrev, nBits, nBlockFromTime,
        amount, txin.prevout, nTime, hashProofOfStake, targetProofOfStake, LogAcceptCategory(BCLog::POS)))
        return state.DoS(1, // may occur during initial download or if behind on block chain sync
            error("%s: INFO: check kernel failed on coinstake %s, hashProof=%s", __func__, tx.GetHash().ToString(), hashProofOfStake.ToString()),
            REJECT_INVALID, "check-kernel-failed");

    // Ensure the input scripts all match and that the total output value to the input script is not less than the total input value.
    // The foundation fund split is user selectable, making it difficult to check the blockreward here.
    // Leaving a window for compromised staking nodes to reassign the blockreward to an attacker's address.
    // If coin owners detect this, they can move their coin to a new address.
    if (HasIsCoinstakeOp(kernelPubKey))
    {
        // Sum value from any extra inputs
        for (size_t k = 1; k < tx.vin.size(); ++k)
        {
            const CTxIn &txin = tx.vin[k];
            Coin coin;
            if (!pcoinsTip->GetCoin(txin.prevout, coin) || coin.IsSpent())
            {
                if (!GetTransaction(txin.prevout.hash, txPrev, Params().GetConsensus(), hashBlock, true)
                    || txin.prevout.n >= txPrev->vpout.size())
                    return state.DoS(1, error("%s: prevout-not-in-chain %d", __func__, k), REJECT_INVALID, "prevout-not-in-chain");

                const CTxOutBase *outPrev = txPrev->vpout[txin.prevout.n].get();
                if (!outPrev->IsStandardOutput())
                    return state.DoS(100, error("%s: invalid-prevout %d", __func__, k), REJECT_INVALID, "invalid-prevout");

                if (kernelPubKey != *outPrev->GetPScriptPubKey())
                    return state.DoS(100, error("%s: mixed-prevout-scripts %d", __func__, k), REJECT_INVALID, "mixed-prevout-scripts");
                amount += outPrev->GetValue();

                LogPrint(BCLog::POS, "%s: Input %d of coinstake %s is spent.", k, tx.GetHash().ToString());
            } else
            {
                if (coin.nType != OUTPUT_STANDARD)
                    return state.DoS(100, error("%s: invalid-prevout %d", __func__, k), REJECT_INVALID, "invalid-prevout");
                if (kernelPubKey != coin.out.scriptPubKey)
                    return state.DoS(100, error("%s: mixed-prevout-scripts %d", __func__, k), REJECT_INVALID, "mixed-prevout-scripts");
                amount += coin.out.nValue;
            };
        };

        CAmount nVerify = 0;
        for (const auto &txout : tx.vpout)
        {
            if (!txout->IsType(OUTPUT_STANDARD))
            {
                if (!txout->IsType(OUTPUT_DATA))
                    return state.DoS(100, error("%s: bad-output-type", __func__), REJECT_INVALID, "bad-output-type");
                continue;
            };
            const CScript *pOutPubKey = txout->GetPScriptPubKey();

            if (pOutPubKey && *pOutPubKey == kernelPubKey)
                nVerify += txout->GetValue();
        };

        if (nVerify < amount)
            return state.DoS(100, error("%s: verify-amount-script-failed, txn %s", __func__, tx.GetHash().ToString()),
                REJECT_INVALID, "verify-amount-script-failed");
    };

    return true;
}


// Check whether the coinstake timestamp meets protocol
bool CheckCoinStakeTimestamp(int nHeight, int64_t nTimeBlock)
{
    return (nTimeBlock & Params().GetStakeTimestampMask(nHeight)) == 0;
}

bool CheckKernel(const CBlockIndex *pindexPrev, unsigned int nBits, int64_t nTime, const COutPoint &prevout, int64_t *pBlockTime)
{
    uint256 hashProofOfStake, targetProofOfStake;

    Coin coin;
    if (!pcoinsTip->GetCoin(prevout, coin))
        return error("%s: prevout not found", __func__);
    if (coin.nType != OUTPUT_STANDARD)
        return error("%s: prevout not standard output", __func__);
    if (coin.IsSpent())
        return error("%s: prevout is spent", __func__);

    CBlockIndex *pindex = chainActive[coin.nHeight];
    if (!pindex)
        return false;

    int nRequiredDepth = std::min((int)(Params().GetStakeMinConfirmations()-1), (int)(pindexPrev->nHeight / 2));
    int nDepth = pindexPrev->nHeight - coin.nHeight;

    if (nRequiredDepth > nDepth)
        return false;

    if (pBlockTime)
        *pBlockTime = pindex->GetBlockTime();

    CAmount amount = coin.out.nValue;
    return CheckStakeKernelHash(pindexPrev, nBits, *pBlockTime,
        amount, prevout, nTime, hashProofOfStake, targetProofOfStake);
}

