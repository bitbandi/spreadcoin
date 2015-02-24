#include "masternodes.h"

static const unsigned int g_MasternodesStartPayments = 300;
static const unsigned int g_MasternodesStopPayments = 150;

CElectedMasternodes g_ElectedMasternodes;

bool CElectedMasternodes::NextPayee(const COutPoint& PrevPayee, CCoinsViewCache *pCoins, CKeyID &keyid, COutPoint& outpoint)
{
    if (PrevPayee.IsNull())
    {
        // Not enough masternodes to start payments
        if (masternodes.size() < g_MasternodesStartPayments)
            return false;

        // Start payments form the beginning
        uint64_t amount;
        outpoint = *masternodes.begin();
        return MN_GetKeyIDAndAmount(outpoint, keyid, amount, pCoins);
    }
    else
    {
        // Stop payments if there are not enough masternodes
        if (masternodes.size() < g_MasternodesStopPayments)
            return false;

        auto iter = masternodes.upper_bound(PrevPayee);

        // Rewind
        if (iter == masternodes.end())
            iter = masternodes.begin();

        uint64_t amount;
        outpoint = *iter;
        return MN_GetKeyIDAndAmount(outpoint, keyid, amount, pCoins);
    }
}

CKeyID CElectedMasternodes::FillBlock(CBlockIndex* pindex, CCoinsViewCache &Coins)
{
    if (pindex->nHeight <= (int)getThirdHardforkBlock())
        return CKeyID(0);

    pindex->velected[IN].clear();
    pindex->velected[OUT].clear();

    COutPoint payeeOutpoint;
    CKeyID payee(0);
    NextPayee(pindex->pprev->mn, &Coins, payee, payeeOutpoint);

    for (COutPoint outpoint : masternodes)
    {
        if (!MN_IsAcceptableMasternodeInput(outpoint, &Coins))
        {
            pindex->velected[IN].push_back(outpoint);
        }
    }

    boost::unordered_map<COutPoint, int> vvotes[2];
    MN_GetVotes(pindex, vvotes);

    for (int j = 0; j < 2; j++)
    {
        for (const std::pair<COutPoint, int>& pair : vvotes[j])
        {
            if (pair.second > g_MasternodesElectionPeriod/2)
            {
                COutPoint outpoint = pair.first;

                bool present = masternodes.count(outpoint) != 0;
                if (j == present)
                    continue;

                if (j && masternodes.size() + pindex->velected[j].size() >= g_MaxMasternodes)
                    continue;
                if (j && !MN_IsAcceptableMasternodeInput(outpoint, &Coins))
                    continue;
                if (std::find(pindex->velected[j].begin(), pindex->velected[j].end(), outpoint) != pindex->velected[j].end())
                    continue;

                pindex->velected[j].push_back(outpoint);
            }
        }
    }

    if (!payee)
        return CKeyID(0);

    pindex->mn = payeeOutpoint;
    pindex->mnKeyId = payee;
    return payee;
}

void CElectedMasternodes::ApplyBlock(CBlockIndex* pindex, bool connect)
{
    for (const COutPoint& outpoint : pindex->velected[connect])
    {
        assert(masternodes.insert(outpoint).second);
    }

    for (const COutPoint& outpoint : pindex->velected[!connect])
    {
        assert(masternodes.erase(outpoint) != 0);
    }
}

void CElectedMasternodes::ApplyMany(CBlockIndex* pindex)
{
    for (; pindex; pindex = pindex->pnext)
    {
        ApplyBlock(pindex, true);
    }
}

bool CElectedMasternodes::IsElected(const COutPoint& outpoint)
{
    return masternodes.count(outpoint) != 0;
}

void MN_LoadElections()
{
    g_ElectedMasternodes.ApplyMany(FindBlockByHeight(getThirdHardforkBlock() + 1));
}
