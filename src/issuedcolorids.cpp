// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <issuedcolorids.h>
#include <txdb.h>

static const char DB_ISSUED_COLORID = 'I';

bool CIssuedColorIds::LoadFromDB(CCoinsViewDB& coinsdb)
{
    std::set<ColorIdentifier> colorIds;
    if (!coinsdb.LoadIssuedColorIds(colorIds))
        return false;
    m_confirmed = std::move(colorIds);
    return true;
}

bool CIssuedColorIds::IsIssued(const ColorIdentifier& id) const
{
    return m_confirmed.count(id) > 0;
}

void CIssuedColorIds::Insert(const std::set<ColorIdentifier>& ids)
{
    for (const auto& id : ids) {
        m_confirmed.insert(id);
        m_pending.erases.erase(id);
        m_pending.inserts.insert(id);
    }
}

void CIssuedColorIds::Erase(const ColorIdentifier& id)
{
    m_confirmed.erase(id);
    m_pending.inserts.erase(id);
    m_pending.erases.insert(id);
}

void CIssuedColorIds::CommitToBatch(CDBBatch& batch)
{
    for (const auto& id : m_pending.inserts)
        batch.Write(std::make_pair(DB_ISSUED_COLORID, id.toVector()), true);
    for (const auto& id : m_pending.erases)
        batch.Erase(std::make_pair(DB_ISSUED_COLORID, id.toVector()));
    m_pending.clear();
}
