#include "Body.h"

#include <SessionClass.h>
#include <VeinholeMonsterClass.h>
#include <HouseClass.h>
#include <ThemeClass.h>
#include <Utilities/EnumFunctions.h>
#include <Ext/House/Body.h>
#include <Ext/SWType/Body.h>

std::unique_ptr<ScenarioExt::ExtData> ScenarioExt::Data = nullptr;

bool ScenarioExt::CellParsed = false;

void ScenarioExt::ExtData::SetVariableToByID(bool bIsGlobal, int nIndex, char bState)
{
	auto& dict = Global()->Variables[bIsGlobal];

	auto itr = dict.find(nIndex);

	if (itr != dict.end() && itr->second.Value != bState)
	{
		itr->second.Value = bState;
		ScenarioClass::Instance->VariablesChanged = true;
		if (!bIsGlobal)
			TagClass::NotifyLocalChanged(nIndex);
		else
			TagClass::NotifyGlobalChanged(nIndex);
	}
}

void ScenarioExt::ExtData::GetVariableStateByID(bool bIsGlobal, int nIndex, char* pOut)
{
	auto& dict = Global()->Variables[bIsGlobal];

	auto itr = dict.find(nIndex);
	if (itr != dict.end())
		*pOut = static_cast<char>(itr->second.Value);
}

void ScenarioExt::ExtData::ReadVariables(bool bIsGlobal, CCINIClass* pINI)
{
	if (!bIsGlobal) // Local variables need to be read again
		Global()->Variables[false].clear();
	else if (Global()->Variables[true].size() != 0) // Global variables had been loaded, DO NOT CHANGE THEM
		return;

	const int nCount = pINI->GetKeyCount("VariableNames");
	for (int i = 0; i < nCount; ++i)
	{
		const auto pKey = pINI->GetKeyName("VariableNames", i);
		int nIndex;
		if (sscanf_s(pKey, "%d", &nIndex) == 1)
		{
			auto& var = Global()->Variables[bIsGlobal][nIndex];
			pINI->ReadString("VariableNames", pKey, pKey, Phobos::readBuffer);
			char* buffer;
			strcpy_s(var.Name, strtok_s(Phobos::readBuffer, ",", &buffer));
			if (auto pState = strtok_s(nullptr, ",", &buffer))
				var.Value = atoi(pState);
			else
				var.Value = 0;
		}
	}
}

// you've inspired something controversial
void ScenarioExt::ExtData::SaveVariablesToFile(bool isGlobal)
{
	CCINIClass fINI {};
	CCFileClass file { isGlobal ? "globals.ini" : "locals.ini" };

	if (file.Exists())
		fINI.ReadCCFile(&file);
	else
		file.CreateFileA();

	for (const auto& [_,varext] : Global()->Variables[isGlobal])
		fINI.WriteInteger(ScenarioClass::Instance->FileName, varext.Name, varext.Value, false);

	fINI.WriteCCFile(&file);
	file.Close();
}

void ScenarioExt::Allocate(ScenarioClass* pThis)
{
	Data = std::make_unique<ScenarioExt::ExtData>(pThis);
}

void ScenarioExt::Remove(ScenarioClass* pThis)
{
	Data = nullptr;
}

void ScenarioExt::LoadFromINIFile(ScenarioClass* pThis, CCINIClass* pINI)
{
	Data->LoadFromINI(pINI);
}

void ScenarioExt::ExtData::UpdateAutoDeathObjectsInLimbo()
{
	for (auto const pExt : this->AutoDeathObjects)
	{
		auto const pTechno = pExt->OwnerObject();

		if (!pTechno->IsInLogic && pTechno->IsAlive)
			pExt->CheckDeathConditions(true);
	}
}

void ScenarioExt::ExtData::UpdateTransportReloaders()
{
	for (auto const pExt : this->TransportReloaders)
	{
		auto const pTechno = pExt->OwnerObject();

		if (pTechno->IsAlive && pTechno->Transporter && pTechno->Transporter->IsInLogic)
			pTechno->Reload();
	}
}

void ScenarioExt::ExtData::RegisterActiveSWMusic(HouseClass* house, HouseExt::ExtData::SWExt* sw, int configuredTheme, AffectedHouse affected)
{
	if (!house || !sw || configuredTheme < 0) { return; }
	
	for (const auto& e : this->ActiveSWMusicEntries)
	{
		if (e.House == house && e.SW == sw)
			return;
	}
	
	this->ActiveSWMusicEntries.emplace_back(house, sw, configuredTheme, affected);
}

void ScenarioExt::ExtData::UpdateActiveSWMusic()
{
	if (this->ActiveSWMusicEntries.empty()) 
		return;

	for (auto it = this->ActiveSWMusicEntries.begin(); it != this->ActiveSWMusicEntries.end(); )
	{
		const auto& entry = *it;
		
		if (!entry.House || !entry.SW || !entry.SW->MusicActive)
		{
			it = this->ActiveSWMusicEntries.erase(it);
			continue;
		}
		
		if (entry.SW->MusicTimer.Completed())
		{
			if (entry.ConfiguredTheme >= 0 && 
				ThemeClass::Instance.CurrentTheme == entry.ConfiguredTheme &&
				EnumFunctions::CanTargetHouse(entry.Affected, entry.House, HouseClass::CurrentPlayer))
			{
				ThemeClass::Instance.Stop();
			}
			
			entry.SW->MusicTimer.Stop();
			entry.SW->MusicActive = false;
			it = this->ActiveSWMusicEntries.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void ScenarioExt::ExtData::RebuildActiveSWMusic()
{
	this->ActiveSWMusicEntries.clear();
	
	for (const auto pHouse : HouseClass::Array)
	{
		if (!pHouse) 
			continue;
			
		auto& houseExt = *HouseExt::ExtMap.Find(pHouse);
		const int superCount = pHouse->Supers.Count;
		
		for (size_t i = 0; i < houseExt.SuperExts.size() && static_cast<int>(i) < superCount; ++i)
		{
			auto& swExt = houseExt.SuperExts[i];
			if (!swExt.MusicActive)
				continue;
				
			const auto* pSuper = pHouse->Supers[static_cast<int>(i)];
			if (!pSuper || !pSuper->Type)
				continue;
				
			const auto* pTypeExt = SWTypeExt::ExtMap.Find(pSuper->Type);
			const int configuredTheme = pTypeExt->Music_Theme.Get();
			
			if (configuredTheme >= 0)
			{
				this->ActiveSWMusicEntries.emplace_back(pHouse, &swExt, configuredTheme, pTypeExt->Music_AffectedHouses.Get());
			}
		}
	}
}

// =============================
// load / save

void ScenarioExt::ExtData::LoadFromINIFile(CCINIClass* const pINI)
{
	auto pThis = this->OwnerObject();

	INI_EX maINI(pINI);
	INI_EX ruINI(CCINIClass::INI_Rules);

	if (SessionClass::IsCampaign())
	{
		Nullable<bool> SP_MCVRedeploy;
		SP_MCVRedeploy.Read(maINI, GameStrings::Basic, GameStrings::MCVRedeploys);
		if (!SP_MCVRedeploy.isset())
			SP_MCVRedeploy.Read(ruINI, GameStrings::Basic, GameStrings::MCVRedeploys);
		GameModeOptionsClass::Instance.MCVRedeploy = SP_MCVRedeploy.Get(false);

		CCINIClass ini_missionmd {};
		ini_missionmd.LoadFromFile(GameStrings::MISSIONMD_INI);
		auto const scenarioName = pThis->FileName;

		// Override rankings
		pThis->ParTimeEasy = ini_missionmd.ReadTime(scenarioName, "Ranking.ParTimeEasy", pThis->ParTimeEasy);
		pThis->ParTimeMedium = ini_missionmd.ReadTime(scenarioName, "Ranking.ParTimeMedium", pThis->ParTimeMedium);
		pThis->ParTimeDifficult = ini_missionmd.ReadTime(scenarioName, "Ranking.ParTimeHard", pThis->ParTimeDifficult);
		ini_missionmd.ReadString(scenarioName, "Ranking.UnderParTitle", pThis->UnderParTitle, pThis->UnderParTitle);
		ini_missionmd.ReadString(scenarioName, "Ranking.UnderParMessage", pThis->UnderParMessage, pThis->UnderParMessage);
		ini_missionmd.ReadString(scenarioName, "Ranking.OverParTitle", pThis->OverParTitle, pThis->OverParTitle);
		ini_missionmd.ReadString(scenarioName, "Ranking.OverParMessage", pThis->OverParMessage, pThis->OverParMessage);

		this->ShowBriefing = pINI->ReadBool(GameStrings::Basic, "ShowBriefing", this->ShowBriefing);
		this->BriefingTheme = pINI->ReadTheme(GameStrings::Basic, "BriefingTheme", this->BriefingTheme);
	}
}

template <typename T>
void ScenarioExt::ExtData::Serialize(T& Stm)
{
	Stm
		.Process(this->Waypoints)
		.Process(this->Variables[0])
		.Process(this->Variables[1])
		.Process(this->ShowBriefing)
		.Process(this->BriefingTheme)
		.Process(this->AutoDeathObjects)
		.Process(this->TransportReloaders)
		.Process(this->SWSidebar_Enable)
		.Process(this->SWSidebar_Indices)
		.Process(this->DefaultLS640BkgdName)
		.Process(this->DefaultLS800BkgdName)
		.Process(this->DefaultLS800BkgdPal)
//		.Process(this->NewMessageList); // Should not S/L
		;
}

void ScenarioExt::ExtData::LoadFromStream(PhobosStreamReader& Stm)
{
	Extension<ScenarioClass>::LoadFromStream(Stm);
	this->Serialize(Stm);
}

void ScenarioExt::ExtData::SaveToStream(PhobosStreamWriter& Stm)
{
	Extension<ScenarioClass>::SaveToStream(Stm);
	this->Serialize(Stm);
}

// =============================
// container hooks

DEFINE_HOOK(0x683549, ScenarioClass_CTOR, 0x9)
{
	GET(ScenarioClass*, pItem, EAX);

	ScenarioExt::Allocate(pItem);

	ScenarioExt::Global()->Waypoints.clear();
	ScenarioExt::Global()->Variables[0].clear();
	ScenarioExt::Global()->Variables[1].clear();

	return 0;
}

DEFINE_HOOK(0x6BEB7D, ScenarioClass_DTOR, 0x6)
{
	GET(ScenarioClass*, pItem, ESI);

	ScenarioExt::Remove(pItem);
	return 0;
}

IStream* ScenarioExt::g_pStm = nullptr;

DEFINE_HOOK_AGAIN(0x689470, ScenarioClass_SaveLoad_Prefix, 0x5)
DEFINE_HOOK(0x689310, ScenarioClass_SaveLoad_Prefix, 0x5)
{
	GET_STACK(IStream*, pStm, 0x4);

	ScenarioExt::g_pStm = pStm;

	return 0;
}

DEFINE_HOOK(0x689669, ScenarioClass_Load_Suffix, 0x6)
{
	auto buffer = ScenarioExt::Global();

	PhobosByteStream Stm(0);
	if (Stm.ReadBlockFromStream(ScenarioExt::g_pStm))
	{
		PhobosStreamReader Reader(Stm);

		if (Reader.Expect(ScenarioExt::Canary) && Reader.RegisterChange(buffer))
			buffer->LoadFromStream(Reader);
	}

	// rebuild active SW music list after loading
	ScenarioExt::Global()->RebuildActiveSWMusic();
	return 0;
}

DEFINE_HOOK(0x68945B, ScenarioClass_Save_Suffix, 0x8)
{
	auto buffer = ScenarioExt::Global();
	PhobosByteStream saver(sizeof(*buffer));
	PhobosStreamWriter writer(saver);

	writer.Expect(ScenarioExt::Canary);
	writer.RegisterChange(buffer);

	buffer->SaveToStream(writer);
	saver.WriteBlockToStream(ScenarioExt::g_pStm);

	return 0;
}

DEFINE_HOOK(0x68AD2F, ScenarioClass_LoadFromINI, 0x5)
{
	GET(ScenarioClass*, pItem, ESI);
	GET(CCINIClass*, pINI, EDI);

	ScenarioExt::LoadFromINIFile(pItem, pINI);
	return 0;
}

DEFINE_HOOK(0x55B4E1, LogicClass_Update_BeforeAll, 0x5)
{
	VeinholeMonsterClass::UpdateAllVeinholes();

	ScenarioExt::Global()->UpdateAutoDeathObjectsInLimbo();
	ScenarioExt::Global()->UpdateTransportReloaders();

	// SW music timers: only check active entries
	ScenarioExt::Global()->UpdateActiveSWMusic();

	return 0;
}
