#include "TFPlayerResource.h"

#include "Entities.h"
#include "Interfaces.h"

#include <client/c_baseentity.h>
#include <client_class.h>
#include <icliententity.h>
#include <icliententitylist.h>
#include <toolframework/ienginetool.h>

std::shared_ptr<TFPlayerResource> TFPlayerResource::m_PlayerResource;

std::shared_ptr<TFPlayerResource> TFPlayerResource::GetPlayerResource()
{
    if (m_PlayerResource && m_PlayerResource->m_PlayerResourceEntity.Get())
        return m_PlayerResource;

    const auto count = Interfaces::GetClientEntityList()->GetHighestEntityIndex();
    for (int i = 0; i < count; i++)
    {
        IClientEntity* unknownEnt = Interfaces::GetClientEntityList()->GetClientEntity(i);
        if (!unknownEnt)
            continue;

        ClientClass* clClass = unknownEnt->GetClientClass();
        if (!clClass)
            continue;

        const char* name = clClass->GetName();
        if (strcmp(name, "CTFPlayerResource"))
            continue;

        m_PlayerResource = std::shared_ptr<TFPlayerResource>(new TFPlayerResource());
        m_PlayerResource->m_PlayerResourceEntity = unknownEnt->GetBaseEntity();
        return m_PlayerResource;
    }

    return nullptr;
}

TFPlayerResource::TFPlayerResource()
{
    auto cc = Entities::GetClientClass("CTFPlayerResource");

    char buf[32];
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        m_AliveOffsets[i] = Entities::GetEntityProp<bool>(cc, Entities::PropIndex(buf, "m_bAlive", i + 1));
        m_DamageOffsets[i] = Entities::GetEntityProp<int>(cc, Entities::PropIndex(buf, "m_iDamage", i + 1));
        m_MaxHealthOffsets[i] = Entities::GetEntityProp<int>(cc, Entities::PropIndex(buf, "m_iMaxHealth", i + 1));

        for (int w = 0; w < STREAK_WEAPONS; w++)
            m_StreakOffsets[i][w] =
                Entities::GetEntityProp<int>(cc, Entities::PropIndex(buf, "m_iStreaks", (i + 1) * STREAK_WEAPONS + w));
    }
}

bool TFPlayerResource::IsAlive(int playerEntIndex)
{
    if (!CheckEntIndex(playerEntIndex, __FUNCTION__))
        return false;

    return m_AliveOffsets[playerEntIndex - 1].GetValue(m_PlayerResourceEntity.Get());
}

int* TFPlayerResource::GetKillstreak(int playerEntIndex, uint_fast8_t weapon)
{
    if (!CheckEntIndex(playerEntIndex, __FUNCTION__))
        return nullptr;

    if (weapon >= STREAK_WEAPONS)
        return nullptr;

    return &m_StreakOffsets[playerEntIndex - 1][weapon].GetValue(m_PlayerResourceEntity.Get());
}

int TFPlayerResource::GetDamage(int playerEntIndex)
{
    if (!CheckEntIndex(playerEntIndex, __FUNCTION__))
        return std::numeric_limits<int>::min();

    return m_DamageOffsets[playerEntIndex - 1].GetValue(m_PlayerResourceEntity.Get());
}

int TFPlayerResource::GetMaxHealth(int playerEntIndex)
{
    if (!CheckEntIndex(playerEntIndex, __FUNCTION__))
        return std::numeric_limits<int>::min();

    return m_MaxHealthOffsets[playerEntIndex - 1].GetValue(m_PlayerResourceEntity.Get());
}

bool TFPlayerResource::CheckEntIndex(int playerEntIndex, const char* functionName)
{
    if (playerEntIndex < 1 || playerEntIndex > Interfaces::GetEngineTool()->GetMaxClients())
    {
        PluginWarning("Out of range playerEntIndex %i in %s()\n", playerEntIndex, functionName);
        return false;
    }

    return true;
}