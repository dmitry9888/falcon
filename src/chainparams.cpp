// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>

#include <iostream>

#include <chainparamsseeds.h>
#include <consensus/merkle.h>
#include <tinyformat.h>
#include <util/system.h>
#include <util/strencodings.h>
#include <util/moneystr.h>
#include <versionbitsinfo.h>

#include <chain/chainparamsimport.h>

#include <assert.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

int64_t CChainParams::GetCoinYearReward(int64_t nTime) const
{
    static const int64_t nSecondsInYear = 365 * 24 * 60 * 60;

    if (strNetworkID != "regtest") {
        // After HF2: 8%, 8%, 7%, 7%, 6%
        if (nTime >= consensus.exploit_fix_2_time) {
            int64_t nPeriodsSinceHF2 = (nTime - consensus.exploit_fix_2_time) / (nSecondsInYear * 2);
            if (nPeriodsSinceHF2 >= 0 && nPeriodsSinceHF2 < 2) {
                return (8 - nPeriodsSinceHF2) * CENT;
            }
            return 6 * CENT;
        }

        // Y1 5%, Y2 4%, Y3 3%, Y4 2%, ... YN 2%
        int64_t nYearsSinceGenesis = (nTime - genesis.nTime) / nSecondsInYear;
        if (nYearsSinceGenesis >= 0 && nYearsSinceGenesis < 3) {
            return (5 - nYearsSinceGenesis) * CENT;
        }
    }

    return nCoinYearReward;
};

bool CChainParams::PushTreasuryFundSettings(int64_t time_from, TreasuryFundSettings &settings)
{
    if (settings.nMinTreasuryStakePercent < 0 or settings.nMinTreasuryStakePercent > 100) {
        throw std::runtime_error("minstakepercent must be in range [0, 100].");
    }

    vTreasuryFundSettings.emplace_back(time_from, settings);

    return true;
};

int64_t CChainParams::GetProofOfStakeReward(const CBlockIndex *pindexPrev, int64_t nFees) const
{
    //int64_t nSubsidy;

    //nSubsidy = (pindexPrev->nMoneySupply / COIN) * GetCoinYearReward(pindexPrev->nTime) / (365 * 24 * (60 * 60 / nTargetSpacing));

    return 400 * COIN + nFees;
};

int64_t CChainParams::GetMaxSmsgFeeRateDelta(int64_t smsg_fee_prev) const
{
    return (smsg_fee_prev * consensus.smsg_fee_max_delta_percent) / 1000000;
};

bool CChainParams::CheckImportCoinbase(int nHeight, uint256 &hash) const
{
    for (auto &cth : Params().vImportedCoinbaseTxns) {
        if (cth.nHeight != (uint32_t)nHeight) {
            continue;
        }
        if (hash == cth.hash) {
            return true;
        }
        return error("%s - Hash mismatch at height %d: %s, expect %s.", __func__, nHeight, hash.ToString(), cth.hash.ToString());
    }

    return error("%s - Unknown height.", __func__);
};


const TreasuryFundSettings *CChainParams::GetTreasuryFundSettings(int64_t nTime) const
{
    for (auto i = vTreasuryFundSettings.crbegin(); i != vTreasuryFundSettings.crend(); ++i) {
        if (nTime > i->first) {
            return &i->second;
        }
    }

    return nullptr;
};

bool CChainParams::IsBech32Prefix(const std::vector<unsigned char> &vchPrefixIn) const
{
    for (auto &hrp : bech32Prefixes)  {
        if (vchPrefixIn == hrp) {
            return true;
        }
    }

    return false;
};

bool CChainParams::IsBech32Prefix(const std::vector<unsigned char> &vchPrefixIn, CChainParams::Base58Type &rtype) const
{
    for (size_t k = 0; k < MAX_BASE58_TYPES; ++k) {
        auto &hrp = bech32Prefixes[k];
        if (vchPrefixIn == hrp) {
            rtype = static_cast<CChainParams::Base58Type>(k);
            return true;
        }
    }

    return false;
};

bool CChainParams::IsBech32Prefix(const char *ps, size_t slen, CChainParams::Base58Type &rtype) const
{
    for (size_t k = 0; k < MAX_BASE58_TYPES; ++k) {
        const auto &hrp = bech32Prefixes[k];
        size_t hrplen = hrp.size();
        if (hrplen > 0
            && slen > hrplen
            && strncmp(ps, (const char*)&hrp[0], hrplen) == 0) {
            rtype = static_cast<CChainParams::Base58Type>(k);
            return true;
        }
    }

    return false;
};

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "The Times 03/Jan/2009 Chancellor on brink of second bailout for banks";
    const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

const std::pair<const char*, CAmount> regTestOutputs[] = {
    std::make_pair("585c2b3914d9ee51f8e710304e386531c3abcc82", 10000 * COIN),
    std::make_pair("c33f3603ce7c46b423536f0434155dad8ee2aa1f", 10000 * COIN),
    std::make_pair("72d83540ed1dcf28bfaca3fa2ed77100c2808825", 10000 * COIN),
    std::make_pair("69e4cc4c219d8971a253cd5db69a0c99c4a5659d", 10000 * COIN),
    std::make_pair("eab5ed88d97e50c87615a015771e220ab0a0991a", 10000 * COIN),
    std::make_pair("119668a93761a34a4ba1c065794b26733975904f", 10000 * COIN),
    std::make_pair("6da49762a4402d199d41d5778fcb69de19abbe9f", 10000 * COIN),
    std::make_pair("27974d10ff5ba65052be7461d89ef2185acbe411", 10000 * COIN),
    std::make_pair("89ea3129b8dbf1238b20a50211d50d462a988f61", 10000 * COIN),
    std::make_pair("3baab5b42a409b7c6848a95dfd06ff792511d561", 10000 * COIN),

    std::make_pair("649b801848cc0c32993fb39927654969a5af27b0", 5000 * COIN),
    std::make_pair("d669de30fa30c3e64a0303cb13df12391a2f7256", 5000 * COIN),
    std::make_pair("f0c0e3ebe4a1334ed6a5e9c1e069ef425c529934", 5000 * COIN),
    std::make_pair("27189afe71ca423856de5f17538a069f22385422", 5000 * COIN),
    std::make_pair("0e7f6fe0c4a5a6a9bfd18f7effdd5898b1f40b80", 5000 * COIN),
};
const size_t nGenesisOutputsRegtest = sizeof(regTestOutputs) / sizeof(regTestOutputs[0]);

// const std::pair<const char*, CAmount> genesisOutputs[] = {
//     std::make_pair("f5ca7e6fd0474c46bfb0965ae36e024fc2d9eae6",100000 * COIN),   // FsaLaRwUM2QNNE4emhgn9ZEYABNq4z5Pq9                  
//     std::make_pair("af5927b14a5faadd536f524c3891ddd18c1fbc46",100000 * COIN),   // Fm9sW5h4L8mpjgAtHZf3YTUDREFrVDKGrJ
//     std::make_pair("561bd9e6c75529f687ef7a6d373eedfdfad33761",100000 * COIN),   // Fd21vYLcLoEE9Lf9G5wPz66kFBXWhiJZy4
//     std::make_pair("44b7295c589024cfd5c82779627bfc2f5bfe48c1",100000 * COIN),   // FbS3pF43CRF2R7FRxDhquSxyj3y6Shb6E7
//     std::make_pair("2d108d884755ce62059e240f008935d358f479d2",100000 * COIN),   // FZGzgexYAJRooCqJF3YWdU3yeq12c9kChe
//     std::make_pair("22b039301ff51aba1d5f14ac1342fd5e98dbecf9",100000 * COIN),   // FYL8XeYEJQqDZvfKDs6dA4uAGHrfYKgP7T
//     std::make_pair("e3039bba0232d17f5bf9c3f26b37007c0ce7008c",100000 * COIN),   // Fqs4AAXSAC5foJ8vSwR9nFf98PXzhsn8L1
//     std::make_pair("31cee7e4477a327cd3ef08e832de465754cd18e5",100000 * COIN),   // FZi5RNugKjvtiGDKCpTnTHog9uLaKJ1FFi
//     std::make_pair("582f4b44bfdb06c7dd8734c4d8f823f670fb1167",100000 * COIN),   // FdCzZqVwLW62KqjZMvGo3bnsJXYemc8G9j
//     std::make_pair("e237f933155224b031e3d860dcd221e1b4ad2297",100000 * COIN),   // FqnrDNBvdoMY9gRDnCJHLL7B6mpMWaT1Wh
//     std::make_pair("e31db3c6af10eca78af376f7ec816ccf1cf7b212",1000000000 * COIN),   // FqsbRCnYDUoWVNRVo2wxLZiJuzAWgpci7P
//     std::make_pair("b95b220809588f5ab0295e23641e53af6385822b",10000000000 * COIN),   // Fn4ndRf69KotLpm3w844TzFJRbUL95rtKW
//     std::make_pair("95debb9147e896f9450441febe8f1a684a29508d",17808180000 * COIN),   // Fiq9xEMKzGGzf7WXh8KJ6efGwHt5EJi9GJ
//     std::make_pair("556e3e549d83c4c0e751509fe1230d3c4be9fbcc",17808180000 * COIN),   // FcxRx4pjfETsHriEPknw34dgBhXoz3rJ3t
//     std::make_pair("89e35a29e5a73f6ce80fdebc78a299d790efb016",17808180000 * COIN),   // FhjoNu5JYnYe9ZyrQUJ4jyG5ugcBq5u3Mq
//     std::make_pair("80dff1c5bbbebdd254523e61acb2098286862985",17808180000 * COIN),   // Fgv9DPtzGCtBwZJDc7fvpSMKQkkizzT9iS
// };
// const size_t nGenesisOutputs = sizeof(genesisOutputs) / sizeof(genesisOutputs[0]);

const std::pair<const char*, CAmount> genesisOutputs[] = {
    std::make_pair("3583cd03cef04f6f05eba161710680acef3cf219", 8223372000 * COIN), // FjgDPwsmKFK1rDDWEQx1bJrNanKhQjokcR
    std::make_pair("3b215afe78fb6712bd218f67b25fe01c55099a4e", 8223372000 * COIN), // FcuDCyU6m3vCqrz8FRTUJ535dcRe4Q8U4q
    std::make_pair("e1a2a21391f056a35f49f14fa960054745c5ef4c", 8223372000 * COIN), // Fd21vYLcLoEE9Lf9G5wPz66kFBXWhiJZy4
    std::make_pair("86b5f801deccccc1ffd6362ef99fc4a3501d2824", 8223372000 * COIN), // FbS3pF43CRF2R7FRxDhquSxyj3y6Shb6E7
    std::make_pair("8e1f4d4cc39492c2673ffa9e7da62bb3cf6292e5", 8223372000 * COIN), // FZGzgexYAJRooCqJF3YWdU3yeq12c9kChe
    std::make_pair("1d268e16ac4d6ee528eed6625c0c5fd2c136fb32", 8223372000 * COIN), // FYL8XeYEJQqDZvfKDs6dA4uAGHrfYKgP7T
    std::make_pair("4c0a160227a21a686e10a693d7e2a3162063f21e", 8223372000 * COIN), // Fqs4AAXSAC5foJ8vSwR9nFf98PXzhsn8L1
    std::make_pair("5a9e4a67bb1bc3834e0d61257b78d8a869a0dc6b", 8223372000 * COIN), // FZi5RNugKjvtiGDKCpTnTHog9uLaKJ1FFi
    std::make_pair("b50bbb4d1c01c6c0f5d22956e309c6ac5e20eaa0", 8223372000 * COIN), // FdCzZqVwLW62KqjZMvGo3bnsJXYemc8G9j
    std::make_pair("6ab3afb442892a273afa4c244fd33c9b18891581", 8223372000 * COIN), // FqnrDNBvdoMY9gRDnCJHLL7B6mpMWaT1Wh
    // std::make_pair("e31db3c6af10eca78af376f7ec816ccf1cf7b212", 1000000000 * COIN),  // FqsbRCnYDUoWVNRVo2wxLZiJuzAWgpci7P
    // std::make_pair("b95b220809588f5ab0295e23641e53af6385822b", 10000000000 * COIN), // Fn4ndRf69KotLpm3w844TzFJRbUL95rtKW
    // std::make_pair("95debb9147e896f9450441febe8f1a684a29508d", 17808180000 * COIN), // Fiq9xEMKzGGzf7WXh8KJ6efGwHt5EJi9GJ
    // std::make_pair("556e3e549d83c4c0e751509fe1230d3c4be9fbcc", 17808180000 * COIN), // FcxRx4pjfETsHriEPknw34dgBhXoz3rJ3t
    // std::make_pair("89e35a29e5a73f6ce80fdebc78a299d790efb016", 17808180000 * COIN), // FhjoNu5JYnYe9ZyrQUJ4jyG5ugcBq5u3Mq
    // std::make_pair("80dff1c5bbbebdd254523e61acb2098286862985", 17808180000 * COIN), // Fgv9DPtzGCtBwZJDc7fvpSMKQkkizzT9iS
};
const size_t nGenesisOutputs = sizeof(genesisOutputs) / sizeof(genesisOutputs[0]);

const std::pair<const char*, CAmount> genesisOutputsTestnet[] = {
    std::make_pair("ba36bf1e094990c73561bbc4504a17a44c5d8482",9137080000 * COIN),   // fX5uorVo7C8FZsnjJgMb3BTXny1MaLd9jV
    std::make_pair("cb89ecdbab041d2fbea13332f5cd4258c519b0e8",9137080000 * COIN),   // fYfWwUGacyiyNeunu1z4z8buLmcS3XpGRZ
    std::make_pair("e4c4fb67f6f4ebcbf758293fbe85c8022ce474eb",9137080000 * COIN),   // faxvacAW5ahqPQnerCes4mVM48JyY3AqhZ
    std::make_pair("c75ae57b4f3f6ee367876f824c5a20d2b3835618",9137080000 * COIN),   // fYHPu5r2km9n2ERximhdT55wZBT5K47ajr
    std::make_pair("2afe88b46b04e044ca6c2368acf6756eb01d70e1",9137080000 * COIN),   // fJ2dqhuUSTy9D5VnVThk1hQxMXbtzRSyZK
    std::make_pair("18ac970a287a13d2709d07f00f969889c6903b23",9137080000 * COIN),   // fGMmWgcspTGVmuxTgTaWQuYgWfbcAopsLU
    std::make_pair("7104d563289a9305853d1c982d9ac5a29af79740",9137080000 * COIN),   // fQQtgqKqxsSEm6U9NcSQoTpcsjYtLxKMcH
    std::make_pair("7fb0761e71c9147714d2fcc410c6d9f9fdf5a284",9137080000 * COIN),   // fRkTkS2KrysSHEjAdiwbUE7cg3TNBopFmX
    std::make_pair("c75a1fcc87d04f1f52df5da596ed9fef6f8bf8f7",9137080000 * COIN),   // fYHNyRxS6Ks23DucWjhg2Uk7xJDXegBVu1
};
const size_t nGenesisOutputsTestnet = sizeof(genesisOutputsTestnet) / sizeof(genesisOutputsTestnet[0]);


static CBlock CreateGenesisBlockRegTest(uint32_t nTime, uint32_t nNonce, uint32_t nBits)
{
    const char *pszTimestamp = "The Times 03/Jan/2009 Chancellor on brink of second bailout for banks";

    CMutableTransaction txNew;
    txNew.nVersion = FALCON_TXN_VERSION;
    txNew.SetType(TXN_COINBASE);
    txNew.vin.resize(1);
    uint32_t nHeight = 0;  // bip34
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp)) << OP_RETURN << nHeight;

    txNew.vpout.resize(nGenesisOutputsRegtest);
    for (size_t k = 0; k < nGenesisOutputsRegtest; ++k) {
        OUTPUT_PTR<CTxOutStandard> out = MAKE_OUTPUT<CTxOutStandard>();
        out->nValue = regTestOutputs[k].second;
        out->scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex(regTestOutputs[k].first) << OP_EQUALVERIFY << OP_CHECKSIG;
        txNew.vpout[k] = out;
    }

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = FALCON_BLOCK_VERSION;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));

    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    genesis.hashWitnessMerkleRoot = BlockWitnessMerkleRoot(genesis);

    return genesis;
}

static CBlock CreateGenesisBlockTestNet(uint32_t nTime, uint32_t nNonce, uint32_t nBits)
{
    const char *pszTimestamp = "The Times 03/Jan/2009 Chancellor on brink of second bailout for banks";

    CMutableTransaction txNew;
    txNew.nVersion = FALCON_TXN_VERSION;
    txNew.SetType(TXN_COINBASE);
    txNew.vin.resize(1);
    uint32_t nHeight = 0;  // bip34
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp)) << OP_RETURN << nHeight;

    txNew.vpout.resize(nGenesisOutputsTestnet);
    for (size_t k = 0; k < nGenesisOutputsTestnet; ++k) {
        OUTPUT_PTR<CTxOutStandard> out = MAKE_OUTPUT<CTxOutStandard>();
        out->nValue = genesisOutputsTestnet[k].second;
        out->scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex(genesisOutputsTestnet[k].first) << OP_EQUALVERIFY << OP_CHECKSIG;
        txNew.vpout[k] = out;
    }

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = FALCON_BLOCK_VERSION;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));

    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    genesis.hashWitnessMerkleRoot = BlockWitnessMerkleRoot(genesis);

    return genesis;
}

static CBlock CreateGenesisBlockMainNet(uint32_t nTime, uint32_t nNonce, uint32_t nBits)
{
    const char *pszTimestamp = "BTC 000000000000000000c679bc2209676d05129834627c7b1c02d1018b224c6f37";

    CMutableTransaction txNew;
    txNew.nVersion = FALCON_TXN_VERSION;
    txNew.SetType(TXN_COINBASE);

    txNew.vin.resize(1);
    uint32_t nHeight = 0;  // bip34
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp)) << OP_RETURN << nHeight;

    txNew.vpout.resize(nGenesisOutputs);
    for (size_t k = 0; k < nGenesisOutputs; ++k) {
        OUTPUT_PTR<CTxOutStandard> out = MAKE_OUTPUT<CTxOutStandard>();
        out->nValue = genesisOutputs[k].second;
        out->scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex(genesisOutputs[k].first) << OP_EQUALVERIFY << OP_CHECKSIG;
        txNew.vpout[k] = out;
    }

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = FALCON_BLOCK_VERSION;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));

    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    genesis.hashWitnessMerkleRoot = BlockWitnessMerkleRoot(genesis);

    return genesis;
}


/**
 * Main network
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";

        consensus.nSubsidyHalvingInterval = 115292150;
        consensus.BIP34Height = 0;
        consensus.BIP65Height = 0;
        consensus.BIP66Height = 0;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 0;

        consensus.OpIsCoinstakeTime = 0x5A04EC00;       // 2017-11-10 00:00:00 UTC
        consensus.fAllowOpIsCoinstakeWithP2PKH = false;
        consensus.nPaidSmsgTime = 0x5C791EC0;           // 2019-03-01 12:00:00
        consensus.csp2shTime = 0x5C791EC0;              // 2019-03-01 12:00:00
        consensus.smsg_fee_time = 0x5D2DBC40;           // 2019-07-16 12:00:00
        consensus.bulletproof_time = 0x5D2DBC40;        // 2019-07-16 12:00:00
        consensus.rct_time = 0x5D2DBC40;                // 2019-07-16 12:00:00
        consensus.smsg_difficulty_time = 0x5D2DBC40;    // 2019-07-16 12:00:00
        consensus.exploit_fix_1_time = 1614268800;      // 2021-02-25 16:00:00
        consensus.exploit_fix_2_time = 1626109200;      // 2021-07-12 17:00:00 UTC

        consensus.m_frozen_anon_index = 27340;
        consensus.m_frozen_blinded_height = 884433;


        consensus.smsg_fee_period = 5040;
        consensus.smsg_fee_funding_tx_per_k = 200000;
        consensus.smsg_fee_msg_per_day_per_k = 50000;
        consensus.smsg_fee_max_delta_percent = 43;
        consensus.smsg_min_difficulty = 0x1effffff;
        consensus.smsg_difficulty_max_delta = 0xffff;

        consensus.powLimit = uint256S("0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");
        consensus.defaultAssumeValid = uint256S("0x00003bc4dba06d28199512174f83fab108e953d2854ebb3f60f0e06b10515227"); // 0

        consensus.nMinRCTOutputDepth = 12;

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0x80;
        pchMessageStart[1] = 0x85;
        pchMessageStart[2] = 0xb6;
        pchMessageStart[3] = 0xba;
        nDefaultPort = 51839;
        nBIP44ID = 0x8000031a;

        nModifierInterval = 10 * 60;    // 10 minutes
        nStakeMinConfirmations = 225;   // 225 * 2 minutes
        nTargetSpacing = 120;           // 2 minutes
        nTargetTimespan = 24 * 60;      // 24 mins

        AddImportHashesMain(vImportedCoinbaseTxns);
        SetLastImportHeight();

        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 1;

        genesis = CreateGenesisBlockMainNet(1649365200, 29672, 0x1f00ffff); // 2022-02-22 12:00:00
        consensus.hashGenesisBlock = genesis.GetHash();

         //calculate Genesis Block
        //Reset genesis
//         consensus.hashGenesisBlock = uint256S("0x");
//         std::cout << std::string("Begin calculating Mainnet Genesis Block:\n");
//         if (true && (genesis.GetHash() != consensus.hashGenesisBlock)) {
//             std::cout << std::string("Calculating Mainnet Genesis Block:\n");
//             arith_uint256 hashTarget = arith_uint256().SetCompact(genesis.nBits);
//             uint256 hash;
//             genesis.nNonce = 0;
//             // This will figure out a valid hash and Nonce if you're
//             // creating a different genesis block:
//             // uint256 hashTarget = CBigNum().SetCompact(genesis.nBits).getuint256();
//             // hashTarget.SetCompact(genesis.nBits, &fNegative, &fOverflow).getuint256();
//             // while (genesis.GetHash() > hashTarget)
//             std::cout  << "HashTarget: " << hashTarget.ToString() << "\n";
//             while (UintToArith256(genesis.GetHash()) > hashTarget)
//             {
//                 ++genesis.nNonce;
//                 if (genesis.nNonce == 0)
//                 {
//                     std::cout << std::string("NONCE WRAPPED, incrementing time");
//                     std::cout << std::string("NONCE WRAPPED, incrementing time:\n");
//                     ++genesis.nTime;
//                 }
//                 if (genesis.nNonce % 10000 == 0)
//                 {
//                     std::cout << std::string("Mainnet: nonce ");
//                     std::cout << genesis.nNonce;
//                     std::cout << std::string(", hash = ");
//                     std::cout << genesis.GetHash().ToString().c_str();
//                     std::cout << std::string("\n");
//                     // std::cout << strNetworkID << " nonce: " << genesis.nNonce << " time: " << genesis.nTime << " hash: " << genesis.GetHash().ToString().c_str() << "\n";
//                 }
//             }
//             std::cout << "Mainnet ---\n";
//             std::cout << "   nonce: " << genesis.nNonce <<  "\n";
//             std::cout << "   time: " << genesis.nTime << "\n";
//             std::cout << "   hash: " << genesis.GetHash().ToString().c_str() << "\n";
//             std::cout << "   merklehash: "  << genesis.hashMerkleRoot.ToString().c_str() << "\n";
//             std::cout << "   witnessmerklehash: " << genesis.hashWitnessMerkleRoot.ToString().c_str() << "\n";
//         }
//         std::cout << std::string("Finished calculating Mainnet Genesis Block:\n");

//         nonce: 198552
//    time: 1643605200
//    hash: 0000b0c13b738ed2bdbfa0b652324e261121f35cb220bce4648bed24ba1bb88d
//    merklehash: 4d61e113684df5c386309371a3ce074ff0e1612823f806ef62004f82928ae8b3
//    witnessmerklehash: 5b2d149d3068a7459eea284a921fe21bfd82325289dfae514f6fd0d6bfe9121b

        assert(consensus.hashGenesisBlock == uint256S("0x00003bc4dba06d28199512174f83fab108e953d2854ebb3f60f0e06b10515227"));
        assert(genesis.hashMerkleRoot == uint256S("0x99c336480f832fad9b5c686515ec0447847fdeb1b73f57b3a89253d08c872387"));
        assert(genesis.hashWitnessMerkleRoot == uint256S("0x7493103f5d41509098ef2f79cf7feac2dfe2d668ae74e486986b99a983264712"));

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as a oneshot if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        /*vSeeds.emplace_back("mainnet-seed.falcon.io");
        vSeeds.emplace_back("dnsseed-mainnet.falcon.io");
        vSeeds.emplace_back("mainnet.falcon.io");
        vSeeds.emplace_back("dnsseed.tecnovert.net");*/


        // vTreasuryFundSettings.emplace_back(0,
        //     TreasuryFundSettings("RJAPhgckEgRGVPZa9WoGSWW24spskSfLTQ", 10, 60));
        // vTreasuryFundSettings.emplace_back(consensus.OpIsCoinstakeTime,
        //     TreasuryFundSettings("RBiiQBnQsVPPQkUaJVQTjsZM9K2xMKozST", 10, 60));
        // vTreasuryFundSettings.emplace_back(consensus.exploit_fix_2_time,
        //     TreasuryFundSettings("RQYUDd3EJohpjq62So4ftcV5XZfxZxJPe9", 50, 650));


        base58Prefixes[PUBKEY_ADDRESS]     = {0x24}; // F
        base58Prefixes[SCRIPT_ADDRESS]     = {0x30}; // L 
        base58Prefixes[PUBKEY_ADDRESS_256] = {0x39};
        base58Prefixes[SCRIPT_ADDRESS_256] = {0x3d};
        base58Prefixes[SECRET_KEY]         = {0x6c};
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x69, 0x6e, 0x82, 0xd1}; // PPAR
        base58Prefixes[EXT_SECRET_KEY]     = {0x8f, 0x1d, 0xae, 0xb8}; // XPAR
        base58Prefixes[STEALTH_ADDRESS]    = {0x14};
        base58Prefixes[EXT_KEY_HASH]       = {0x4b}; // X
        base58Prefixes[EXT_ACC_HASH]       = {0x17}; // A
        base58Prefixes[EXT_PUBLIC_KEY_BTC] = {0x04, 0x88, 0xB2, 0x1E}; // xpub
        base58Prefixes[EXT_SECRET_KEY_BTC] = {0x04, 0x88, 0xAD, 0xE4}; // xprv

        bech32Prefixes[PUBKEY_ADDRESS].assign       ("ph",(const char*)"ph"+2);
        bech32Prefixes[SCRIPT_ADDRESS].assign       ("pr",(const char*)"pr"+2);
        bech32Prefixes[PUBKEY_ADDRESS_256].assign   ("pl",(const char*)"pl"+2);
        bech32Prefixes[SCRIPT_ADDRESS_256].assign   ("pj",(const char*)"pj"+2);
        bech32Prefixes[SECRET_KEY].assign           ("px",(const char*)"px"+2);
        bech32Prefixes[EXT_PUBLIC_KEY].assign       ("pep",(const char*)"pep"+3);
        bech32Prefixes[EXT_SECRET_KEY].assign       ("pex",(const char*)"pex"+3);
        bech32Prefixes[STEALTH_ADDRESS].assign      ("ps",(const char*)"ps"+2);
        bech32Prefixes[EXT_KEY_HASH].assign         ("pek",(const char*)"pek"+3);
        bech32Prefixes[EXT_ACC_HASH].assign         ("pea",(const char*)"pea"+3);
        bech32Prefixes[STAKE_ONLY_PKADDR].assign    ("pcs",(const char*)"pcs"+3);

        bech32_hrp = "fw";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        m_is_test_chain = false;

        checkpointData = {
            {
                { 0,     uint256S("0x00003bc4dba06d28199512174f83fab108e953d2854ebb3f60f0e06b10515227")},
            }
        };

        //chainTxData = ChainTxData {
            // Data from rpc: getchaintxstats 4096 dcbe7be05060974cca33a52790e047a73358812102a97cd5b3089f2469bd6601
        //    /* nTime    */ 1632606640,
        //    /* nTxCount */ 1212654,
        //    /* dTxRate  */ 0.008
        //};
    }

    void SetOld()
    {
        consensus.BIP16Exception = uint256S("0x00000000000002dc756eebf4f49723ed8d30cc28a5f108eb94b1ba88ac4f9c22");
        consensus.BIP34Height = 227931;
        consensus.BIP34Hash = uint256S("0x000000000000024b89b42a942fe0d9fea3bb44ab7bd1b19115dd6a759c0808b8");
        consensus.BIP65Height = 388381; // 000000000000000004c2b624ed5d7756c508d90fd0da2c7c679febfa6c4735f0
        consensus.BIP66Height = 363725; // 00000000000000000379eaa19dce8c9b722d46ae6a57c2f1a988119488b50931
        consensus.CSVHeight = 419328; // 000000000000000004a1b34462cb8aeebd5799177f7a29cf28f2d1961716b5b5
        consensus.SegwitHeight = 481824; // 0000000000000000001c8018d9cb3b742ef25114f27563e3fc4a1902167f9893
        consensus.MinBIP9WarningHeight = consensus.SegwitHeight + consensus.nMinerConfirmationWindow;
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        genesis = CreateGenesisBlock(1231006505, 2083236893, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,0);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,5);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,128);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        bech32_hrp = "bc";
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        consensus.nSubsidyHalvingInterval = 115292150;
        consensus.BIP34Height = 0;
        consensus.BIP65Height = 0;
        consensus.BIP66Height = 0;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 0;
        consensus.MinBIP9WarningHeight = 0;

        consensus.OpIsCoinstakeTime = 0;
        consensus.fAllowOpIsCoinstakeWithP2PKH = true; // TODO: clear for next testnet
        consensus.nPaidSmsgTime = 0;
        consensus.csp2shTime = 0x5C67FB40;              // 2019-02-16 12:00:00
        consensus.smsg_fee_time = 0x5C67FB40;           // 2019-02-16 12:00:00
        consensus.bulletproof_time = 0x5C67FB40;        // 2019-02-16 12:00:00
        consensus.rct_time = 0;
        consensus.smsg_difficulty_time = 0x5D19F5C0;    // 2019-07-01 12:00:00
        consensus.exploit_fix_1_time = 1614268800;      // 2021-02-25 16:00:00

        consensus.smsg_fee_period = 5040;
        consensus.smsg_fee_funding_tx_per_k = 200000;
        consensus.smsg_fee_msg_per_day_per_k = 50000;
        consensus.smsg_fee_max_delta_percent = 43;
        consensus.smsg_min_difficulty = 0x1effffff;
        consensus.smsg_difficulty_max_delta = 0xffff;

        consensus.powLimit = uint256S("0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");
        consensus.defaultAssumeValid = uint256S("0x0000c92f5a15d4d6b2244d7e44afb2da88542de3c45610c5ce5d86be38d39275"); // 0

        consensus.nMinRCTOutputDepth = 12;

        pchMessageStart[0] = 0x0b;
        pchMessageStart[1] = 0x9a;
        pchMessageStart[2] = 0x9c;
        pchMessageStart[3] = 0xae;
        nDefaultPort = 52038;
        nBIP44ID = 0x80000001;

        nModifierInterval = 10 * 60;    // 10 minutes
        nStakeMinConfirmations = 225;   // 225 * 2 minutes
        nTargetSpacing = 120;           // 2 minutes
        nTargetTimespan = 24 * 60;      // 24 mins


        AddImportHashesTest(vImportedCoinbaseTxns);
        SetLastImportHeight();

        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 1;

        genesis = CreateGenesisBlockTestNet(1634752800, 13833, 0x1f00ffff); // 2021-10-21 21:00:00
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock == uint256S("0x0000c92f5a15d4d6b2244d7e44afb2da88542de3c45610c5ce5d86be38d39275"));
        assert(genesis.hashMerkleRoot == uint256S("0x4c5a7c8c9a617a90bff3631c9e79d318080ed555117ca9091449da9fe5790f73"));
        assert(genesis.hashWitnessMerkleRoot == uint256S("0x17eccf574bdbf94155634a8d52c889244312cebc3e1d8f87a0a48427e8462a3d"));

        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        /*vSeeds.emplace_back("testnet-seed.falcon.io");
        vSeeds.emplace_back("dnsseed-testnet.falcon.io");
        vSeeds.emplace_back("dnsseed-testnet.tecnovert.net");*/

        //vTreasuryFundSettings.push_back(std::make_pair(0, TreasuryFundSettings("rTvv9vsbu269mjYYEecPYinDG8Bt7D86qD", 10, 60)));

        base58Prefixes[PUBKEY_ADDRESS]     = {0x5f}; // f
        base58Prefixes[SCRIPT_ADDRESS]     = {0x7f}; // t
        base58Prefixes[PUBKEY_ADDRESS_256] = {0x77};
        base58Prefixes[SCRIPT_ADDRESS_256] = {0x7b};
        base58Prefixes[SECRET_KEY]         = {0x2e};
        base58Prefixes[EXT_PUBLIC_KEY]     = {0xe1, 0x42, 0x78, 0x00}; // ppar
        base58Prefixes[EXT_SECRET_KEY]     = {0x04, 0x88, 0x94, 0x78}; // xpar
        base58Prefixes[STEALTH_ADDRESS]    = {0x15}; // T
        base58Prefixes[EXT_KEY_HASH]       = {0x89}; // x
        base58Prefixes[EXT_ACC_HASH]       = {0x53}; // a
        base58Prefixes[EXT_PUBLIC_KEY_BTC] = {0x04, 0x35, 0x87, 0xCF}; // tpub
        base58Prefixes[EXT_SECRET_KEY_BTC] = {0x04, 0x35, 0x83, 0x94}; // tprv

        bech32Prefixes[PUBKEY_ADDRESS].assign       ("tph",(const char*)"tph"+3);
        bech32Prefixes[SCRIPT_ADDRESS].assign       ("tpr",(const char*)"tpr"+3);
        bech32Prefixes[PUBKEY_ADDRESS_256].assign   ("tpl",(const char*)"tpl"+3);
        bech32Prefixes[SCRIPT_ADDRESS_256].assign   ("tpj",(const char*)"tpj"+3);
        bech32Prefixes[SECRET_KEY].assign           ("tpx",(const char*)"tpx"+3);
        bech32Prefixes[EXT_PUBLIC_KEY].assign       ("tpep",(const char*)"tpep"+4);
        bech32Prefixes[EXT_SECRET_KEY].assign       ("tpex",(const char*)"tpex"+4);
        bech32Prefixes[STEALTH_ADDRESS].assign      ("tps",(const char*)"tps"+3);
        bech32Prefixes[EXT_KEY_HASH].assign         ("tpek",(const char*)"tpek"+4);
        bech32Prefixes[EXT_ACC_HASH].assign         ("tpea",(const char*)"tpea"+4);
        bech32Prefixes[STAKE_ONLY_PKADDR].assign    ("tpcs",(const char*)"tpcs"+4);

        bech32_hrp = "tfw";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        m_is_test_chain = true;

        checkpointData = {
            {
                {0, uint256S("0x0000c92f5a15d4d6b2244d7e44afb2da88542de3c45610c5ce5d86be38d39275")},
            }
        };

        //chainTxData = ChainTxData{
            // Data from rpc: getchaintxstats 4096 9313d6187a7b8bdd18f988d159331702ead08fe8001e602d5a42cb3a60f8313e
        //    /* nTime    */ 1632544144,
        //    /* nTxCount */ 1034741,
        //    /* dTxRate  */ 0.0067
        //};
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    explicit CRegTestParams(const ArgsManager& args) {
        strNetworkID = "regtest";
        consensus.nSubsidyHalvingInterval = 150;
        consensus.BIP16Exception = uint256();
        consensus.BIP34Height = 500; // BIP34 activated on regtest (Used in functional tests)
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1351; // BIP65 activated on regtest (Used in functional tests)
        consensus.BIP66Height = 1251; // BIP66 activated on regtest (Used in functional tests)
        consensus.CSVHeight = 432; // CSV activated on regtest (Used in rpc activation tests)
        consensus.SegwitHeight = 0; // SEGWIT is always activated on regtest unless overridden
        consensus.MinBIP9WarningHeight = 0;

        consensus.OpIsCoinstakeTime = 0;
        consensus.fAllowOpIsCoinstakeWithP2PKH = false;
        consensus.nPaidSmsgTime = 0;
        consensus.csp2shTime = 0;
        consensus.smsg_fee_time = 0;
        consensus.bulletproof_time = 0;
        consensus.rct_time = 0;
        consensus.smsg_difficulty_time = 0;

        consensus.clamp_tx_version_time = 0;

        consensus.smsg_fee_period = 50;
        consensus.smsg_fee_funding_tx_per_k = 200000;
        consensus.smsg_fee_msg_per_day_per_k = 50000;
        consensus.smsg_fee_max_delta_percent = 4300;
        consensus.smsg_min_difficulty = 0x1f0fffff;
        consensus.smsg_difficulty_max_delta = 0xffff;

        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        consensus.nMinRCTOutputDepth = 2;

        pchMessageStart[0] = 0xb0;
        pchMessageStart[1] = 0x89;
        pchMessageStart[2] = 0x83;
        pchMessageStart[3] = 0x8d;
        nDefaultPort = 12038;
        nBIP44ID = 0x80000001;


        nModifierInterval = 2 * 60;     // 2 minutes
        nStakeMinConfirmations = 12;
        nTargetSpacing = 5;             // 5 seconds
        nTargetTimespan = 16 * 60;      // 16 mins
        nStakeTimestampMask = 0;

        SetLastImportHeight();

        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        UpdateActivationParametersFromArgs(args);

        genesis = CreateGenesisBlockRegTest(1487714923, 0, 0x207fffff);

        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x6cd174536c0ada5bfa3b8fde16b98ae508fff6586f2ee24cf866867098f25907"));
        assert(genesis.hashMerkleRoot == uint256S("0xf89653c7208af2c76a3070d436229fb782acbd065bd5810307995b9982423ce7"));
        assert(genesis.hashWitnessMerkleRoot == uint256S("0x36b66a1aff91f34ab794da710d007777ef5e612a320e1979ac96e5f292399639"));


        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = true;
        m_is_test_chain = true;

        checkpointData = {
            {
                {0, uint256S("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206")},
            }
        };

        base58Prefixes[PUBKEY_ADDRESS]     = {0x5f}; // f
        base58Prefixes[SCRIPT_ADDRESS]     = {0x7f}; // t
        base58Prefixes[PUBKEY_ADDRESS_256] = {0x77};
        base58Prefixes[SCRIPT_ADDRESS_256] = {0x7b};
        base58Prefixes[SECRET_KEY]         = {0x2e};
        base58Prefixes[EXT_PUBLIC_KEY]     = {0xe1, 0x42, 0x78, 0x00}; // ppar
        base58Prefixes[EXT_SECRET_KEY]     = {0x04, 0x88, 0x94, 0x78}; // xpar
        base58Prefixes[STEALTH_ADDRESS]    = {0x15}; // T
        base58Prefixes[EXT_KEY_HASH]       = {0x89}; // x
        base58Prefixes[EXT_ACC_HASH]       = {0x53}; // a
        base58Prefixes[EXT_PUBLIC_KEY_BTC] = {0x04, 0x35, 0x87, 0xCF}; // tpub
        base58Prefixes[EXT_SECRET_KEY_BTC] = {0x04, 0x35, 0x83, 0x94}; // tprv

        bech32Prefixes[PUBKEY_ADDRESS].assign       ("tph",(const char*)"tph"+3);
        bech32Prefixes[SCRIPT_ADDRESS].assign       ("tpr",(const char*)"tpr"+3);
        bech32Prefixes[PUBKEY_ADDRESS_256].assign   ("tpl",(const char*)"tpl"+3);
        bech32Prefixes[SCRIPT_ADDRESS_256].assign   ("tpj",(const char*)"tpj"+3);
        bech32Prefixes[SECRET_KEY].assign           ("tpx",(const char*)"tpx"+3);
        bech32Prefixes[EXT_PUBLIC_KEY].assign       ("tpep",(const char*)"tpep"+4);
        bech32Prefixes[EXT_SECRET_KEY].assign       ("tpex",(const char*)"tpex"+4);
        bech32Prefixes[STEALTH_ADDRESS].assign      ("tps",(const char*)"tps"+3);
        bech32Prefixes[EXT_KEY_HASH].assign         ("tpek",(const char*)"tpek"+4);
        bech32Prefixes[EXT_ACC_HASH].assign         ("tpea",(const char*)"tpea"+4);
        bech32Prefixes[STAKE_ONLY_PKADDR].assign    ("tpcs",(const char*)"tpcs"+4);

        bech32_hrp = "lcfw";

        chainTxData = ChainTxData{
            0,
            0,
            0
        };
    }

    void SetOld()
    {
        genesis = CreateGenesisBlock(1296688602, 2, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        /*
        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        */

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "bcrt";
    }

    /**
     * Allows modifying the Version Bits regtest parameters.
     */
    void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
    {
        consensus.vDeployments[d].nStartTime = nStartTime;
        consensus.vDeployments[d].nTimeout = nTimeout;
    }
    void UpdateActivationParametersFromArgs(const ArgsManager& args);
};

void CRegTestParams::UpdateActivationParametersFromArgs(const ArgsManager& args)
{
    if (gArgs.IsArgSet("-segwitheight")) {
        int64_t height = gArgs.GetArg("-segwitheight", consensus.SegwitHeight);
        if (height < -1 || height >= std::numeric_limits<int>::max()) {
            throw std::runtime_error(strprintf("Activation height %ld for segwit is out of valid range. Use -1 to disable segwit.", height));
        } else if (height == -1) {
            LogPrintf("Segwit disabled for testing\n");
            height = std::numeric_limits<int>::max();
        }
        consensus.SegwitHeight = static_cast<int>(height);
    }

    if (!args.IsArgSet("-vbparams")) return;

    for (const std::string& strDeployment : args.GetArgs("-vbparams")) {
        std::vector<std::string> vDeploymentParams;
        boost::split(vDeploymentParams, strDeployment, boost::is_any_of(":"));
        if (vDeploymentParams.size() != 3) {
            throw std::runtime_error("Version bits parameters malformed, expecting deployment:start:end");
        }
        int64_t nStartTime, nTimeout;
        if (!ParseInt64(vDeploymentParams[1], &nStartTime)) {
            throw std::runtime_error(strprintf("Invalid nStartTime (%s)", vDeploymentParams[1]));
        }
        if (!ParseInt64(vDeploymentParams[2], &nTimeout)) {
            throw std::runtime_error(strprintf("Invalid nTimeout (%s)", vDeploymentParams[2]));
        }
        bool found = false;
        for (int j=0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j) {
            if (vDeploymentParams[0] == VersionBitsDeploymentInfo[j].name) {
                UpdateVersionBitsParameters(Consensus::DeploymentPos(j), nStartTime, nTimeout);
                found = true;
                LogPrintf("Setting version bits activation parameters for %s to start=%ld, timeout=%ld\n", vDeploymentParams[0], nStartTime, nTimeout);
                break;
            }
        }
        if (!found) {
            throw std::runtime_error(strprintf("Invalid deployment (%s)", vDeploymentParams[0]));
        }
    }
}

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

const CChainParams *pParams() {
    return globalChainParams.get();
};

std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams(gArgs));
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}


void SetOldParams(std::unique_ptr<CChainParams> &params)
{
    if (params->NetworkID() == CBaseChainParams::MAIN) {
        return ((CMainParams*)params.get())->SetOld();
    }
    if (params->NetworkID() == CBaseChainParams::REGTEST) {
        return ((CRegTestParams*)params.get())->SetOld();
    }
};

void ResetParams(std::string sNetworkId, bool fFalconModeIn)
{
    // Hack to pass old unit tests
    globalChainParams = CreateChainParams(sNetworkId);
    if (!fFalconModeIn) {
        SetOldParams(globalChainParams);
    }
};

/**
 * Mutable handle to regtest params
 */
CChainParams &RegtestParams()
{
    return *globalChainParams.get();
};
