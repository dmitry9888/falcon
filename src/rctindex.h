// Copyright (c) 2017-2021 The Falcon Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FALCON_RCTINDEX_H
#define FALCON_RCTINDEX_H

#include <primitives/transaction.h>

class CAnonOutput
{
// Stored in txdb, key is 64bit index
public:
    CAnonOutput() : nBlockHeight(0), nCompromised(0) {};
    CAnonOutput(CCmpPubKey pubkey_, secp256k1_pedersen_commitment commitment_, COutPoint &outpoint_, int nBlockHeight_, uint8_t nCompromised_)
        : pubkey(pubkey_), commitment(commitment_), outpoint(outpoint_), nBlockHeight(nBlockHeight_), nCompromised(nCompromised_) {};

    CCmpPubKey pubkey;
    secp256k1_pedersen_commitment commitment;
    COutPoint outpoint;
    int nBlockHeight;
    uint8_t nCompromised;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(pubkey);
        if (ser_action.ForRead()) {
            s.read((char*)&commitment.data[0], 33);
        } else {
            s.write((char*)&commitment.data[0], 33);
        }

        READWRITE(outpoint);
        READWRITE(nBlockHeight);
        READWRITE(nCompromised);
    }
};

class CAnonKeyImageInfo
{
public:
    CAnonKeyImageInfo() {};
    CAnonKeyImageInfo(const uint256 &txid_, int height_) : txid(txid_), height(height_) {};
    uint256 txid;
    int height = 0;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(txid);
        READWRITE(height);
    }
};


#endif // FALCON_RCTINDEX_H
