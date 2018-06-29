/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "SelectedUnitsAI.h"

#include "SelectedUnitsHandler.h"
#include "GlobalUnsynced.h"
#include "WaitCommandsAI.h"
#include "Game/Players/Player.h"
#include "Game/Players/PlayerHandler.h"
#include "Map/Ground.h"
#include "Sim/Misc/GlobalConstants.h"
#include "Sim/Misc/QuadField.h"
#include "Sim/Misc/TeamHandler.h"
#include "Sim/MoveTypes/MoveType.h"
#include "Sim/Units/UnitHandler.h"
#include "Sim/Units/CommandAI/CommandAI.h"
#include "Sim/Units/Unit.h"
#include "Sim/Units/UnitDef.h"
#include "Net/Protocol/NetProtocol.h"

const int CMDPARAM_MOVE_X = 0;
const int CMDPARAM_MOVE_Y = 1;
const int CMDPARAM_MOVE_Z = 2;

typedef std::vector<int> TGroupVect;
typedef std::pair<float, TGroupVect> TGroupPair;

static const auto ugPairComp = [](const TGroupPair& a, const TGroupPair& b) { return (a.first < b.first); };
static const auto idPairComp = [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return (a.first < b.first); };


// Global object
CSelectedUnitsHandlerAI selectedUnitsAI;


inline void CSelectedUnitsHandlerAI::AddUnitSetMaxSpeedCommandNet(CUnit* unit, unsigned char options)
{
	// this sets the WANTED maximum speed of <unit>
	// (via the CommandAI --> MoveType chain) to be
	// equal to its current ACTUAL maximum (not the
	// UnitDef maximum, which can be overridden by
	// scripts)
	CCommandAI* cai = unit->commandAI;

	if (!cai->CanSetMaxSpeed())
		return;

	cai->GiveCommand(Command(CMD_SET_WANTED_MAX_SPEED, options, unit->moveType->GetMaxSpeed()), true);
}

inline void CSelectedUnitsHandlerAI::AddGroupSetMaxSpeedCommandNet(CUnit* unit, unsigned char options)
{
	// sets the wanted speed of this unit to that of the
	// group's current-slowest member (groupMinMaxSpeed
	// is derived from GetMaxSpeed, not GetMaxSpeedDef)
	CCommandAI* cai = unit->commandAI;

	if (!cai->CanSetMaxSpeed())
		return;

	cai->GiveCommand(Command(CMD_SET_WANTED_MAX_SPEED, options, groupMinMaxSpeed), true);
}


static inline bool MayRequireSetMaxSpeedCommand(const Command& c)
{
	switch (c.GetID()) {
		// this is not a complete list
		case CMD_STOP:
		case CMD_WAIT:
		case CMD_SELFD:
		case CMD_FIRE_STATE:
		case CMD_MOVE_STATE:
		case CMD_ONOFF:
		case CMD_REPEAT: {
			return false;
		}
	}
	return true;
}

void CSelectedUnitsHandlerAI::GiveCommandNet(Command& c, int player)
{
	const std::vector<int>& netSelected = selectedUnitsHandler.netSelected[player];

	const int nbrOfSelectedUnits = netSelected.size();
	const int cmdID = c.GetID();

	// no units to command
	if (nbrOfSelectedUnits < 1)
		return;

	if ((cmdID == CMD_ATTACK) && (
			(c.GetParamsCount() == 6) ||
			((c.GetParamsCount() == 4) && (c.GetParam(3) > 0.001f))
		))
	{
		SelectAttackNet(c, player);
		return;
	}

	if (nbrOfSelectedUnits == 1) {
		// a single unit selected
		CUnit* unit = unitHandler.GetUnit(*netSelected.begin());

		if (unit == nullptr)
			return;

		unit->commandAI->GiveCommand(c, true);

		if (MayRequireSetMaxSpeedCommand(c))
			AddUnitSetMaxSpeedCommandNet(unit, c.options);

		if (cmdID == CMD_WAIT) {
			if (player == gu->myPlayerNum)
				waitCommandsAI.AcknowledgeCommand(c);
		}

		return;
	}

	// User Move Front Command:
	//
	//   CTRL:      Group Front/Speed  command
	//
	// User Move Command:
	//
	//   ALT:       Group Front        command
	//   ALT+CTRL:  Group Front/Speed  command
	//   CTRL:      Group Locked/Speed command  (maintain relative positions)
	//
	// User Patrol and Fight Commands:
	//
	//   CTRL:      Group Locked/Speed command  (maintain relative positions)
	//   ALT+CTRL:  Group Locked       command  (maintain relative positions)
	//
	if (((cmdID == CMD_MOVE) || (cmdID == CMD_FIGHT)) && (c.GetParamsCount() == 6)) {
		CalculateGroupData(player, !!(c.options & SHIFT_KEY));

		MakeFormationFrontOrder(&c, player);

		const bool groupSpeed = !!(c.options & CONTROL_KEY);

		for (const int unitID: netSelected) {
			CUnit* unit = unitHandler.GetUnit(unitID);

			if (unit == nullptr)
				continue;

			if (groupSpeed) {
				AddGroupSetMaxSpeedCommandNet(unit, c.options);
			} else {
				AddUnitSetMaxSpeedCommandNet(unit, c.options);
			}
		}

		return;
	}

	if ((cmdID == CMD_MOVE) && (c.options & ALT_KEY)) {
		CalculateGroupData(player, !!(c.options & SHIFT_KEY));

		// use the vector from the middle of group to new pos as forward dir
		const float3 pos(c.GetParam(0), c.GetParam(1), c.GetParam(2));
		const float3 frontDir = ((pos - groupCenterCoor) * XZVector).ANormalize();
		const float3  sideDir = frontDir.cross(UpVector);

		// calculate so that the units form in an aproximate square
		const float length = 100.0f + (math::sqrt((float)nbrOfSelectedUnits) * 32.0f);

		// push back some extra params so it confer with a front move
		c.PushPos(pos + (sideDir * length));

		MakeFormationFrontOrder(&c, player);

		const bool groupSpeed = !!(c.options & CONTROL_KEY);

		for (const int unitID: netSelected) {
			CUnit* unit = unitHandler.GetUnit(unitID);

			if (unit == nullptr)
				continue;

			if (groupSpeed) {
				AddGroupSetMaxSpeedCommandNet(unit, c.options);
			} else {
				AddUnitSetMaxSpeedCommandNet(unit, c.options);
			}
		}

		return;
	}

	if ((c.options & CONTROL_KEY) && ((cmdID == CMD_MOVE) || (cmdID == CMD_PATROL) || (cmdID == CMD_FIGHT))) {
		CalculateGroupData(player, !!(c.options & SHIFT_KEY));

		const bool groupSpeed = !(c.options & ALT_KEY);
		const bool queueing = (c.options & SHIFT_KEY);

		for (const int unitID: netSelected) {
			CUnit* unit = unitHandler.GetUnit(unitID);

			if (unit == nullptr)
				continue;

			// modify the destination relative to the center of the group
			Command uc = c;

			const float3 midPos = (queueing? LastQueuePosition(unit): float3(unit->midPos));
			const float3 difPos = midPos - groupCenterCoor;

			uc.params[CMDPARAM_MOVE_X] += difPos.x;
			uc.params[CMDPARAM_MOVE_Y] += difPos.y;
			uc.params[CMDPARAM_MOVE_Z] += difPos.z;
			unit->commandAI->GiveCommand(uc, true);

			if (groupSpeed) {
				AddGroupSetMaxSpeedCommandNet(unit, c.options);
			} else {
				AddUnitSetMaxSpeedCommandNet(unit, c.options);
			}
		}

		return;
	}
	{
		for (const int unitID: netSelected) {
			CUnit* unit = unitHandler.GetUnit(unitID);

			if (unit == nullptr)
				continue;

			// appending a CMD_SET_WANTED_MAX_SPEED command to
			// every command is a little bit wasteful, n'est pas?
			unit->commandAI->GiveCommand(c, true);

			if (!MayRequireSetMaxSpeedCommand(c))
				continue;

			AddUnitSetMaxSpeedCommandNet(unit, c.options);
		}

		if (cmdID != CMD_WAIT)
			return;
		if (player != gu->myPlayerNum)
			return;

		waitCommandsAI.AcknowledgeCommand(c);
	}
}


//
// Calculate the outer limits and the center of the group coordinates.
//
void CSelectedUnitsHandlerAI::CalculateGroupData(int player, bool queueing) {
	//Finding the highest, lowest and weighted central positional coordinates among the selected units.
	float3 sumCoor;
	float3 minCoor =  OnesVector * 100000.0f;
	float3 maxCoor = -OnesVector * 100000.0f;
	float3 mobileSumCoor;

	int mobileUnits = 0;

	groupSumLength = 0.0f;
	groupMinMaxSpeed = 1e9f;

	const std::vector<int>& playerUnitIDs = selectedUnitsHandler.netSelected[player];

	for (auto ui = playerUnitIDs.begin(); ui != playerUnitIDs.end(); ++ui) {
		CUnit* unit = unitHandler.GetUnit(*ui);

		if (unit == nullptr)
			continue;

		groupSumLength += ((unit->unitDef->xsize + unit->unitDef->zsize) * 0.5f);

		const float3 unitPos = (queueing? LastQueuePosition(unit): float3(unit->midPos));

		minCoor  = float3::min(minCoor, unitPos);
		maxCoor  = float3::max(maxCoor, unitPos);
		sumCoor += unitPos;

		if (!unit->commandAI->CanSetMaxSpeed())
			continue;

		mobileUnits++;
		mobileSumCoor += unitPos;

		groupMinMaxSpeed = std::min(groupMinMaxSpeed, unit->moveType->GetMaxSpeed());
	}

	groupAvgLength = groupSumLength / playerUnitIDs.size();

	// weighted center
	if (mobileUnits > 0)
		groupCenterCoor = mobileSumCoor / mobileUnits;
	else
		groupCenterCoor = sumCoor / playerUnitIDs.size();
}


void CSelectedUnitsHandlerAI::MakeFormationFrontOrder(Command* c, int player)
{
	// called when releasing the mouse; accompanies GuiHandler::DrawFormationFrontOrder
	formationCenterPos = c->GetPos(0);
	formationRightPos = c->GetPos(3);

	// in "front" coordinates (rotated to real, moved by formationRightPos)
	float3 nextPos;

	const std::vector<int>& playerUnitIDs = selectedUnitsHandler.netSelected[player];

	if (formationCenterPos.distance(formationRightPos) < playerUnitIDs.size() + 33) {
		// if the front is not long enough, treat as a standard move
		for (auto ui = playerUnitIDs.begin(); ui != playerUnitIDs.end(); ++ui) {
			CUnit* unit = unitHandler.GetUnit(*ui);

			if (unit == nullptr)
				continue;

			unit->commandAI->GiveCommand(*c, false);
		}

		return;
	}

	groupFrontLength = formationCenterPos.distance(formationRightPos) * 2.0f;
	groupAddedSpace = 0.0f;

	if (groupFrontLength > (groupSumLength * 2.0f * SQUARE_SIZE))
		groupAddedSpace = (groupFrontLength - (groupSumLength * 2.0f * SQUARE_SIZE)) / (playerUnitIDs.size() - 1);

	#if 0
	formationNumColumns = std::max(1, int(groupFrontLength / groupColumnDist));
	#endif

	const float3 formationSideDir = (formationCenterPos - formationRightPos) * XZVector + (UpVector * groupFrontLength * 0.5f);


	sortedUnitGroups.clear();
	frontMoveCommands.clear();

	CreateUnitOrder(sortedUnitPairs, player);

	for (size_t k = 0; k < sortedUnitPairs.size(); ) {
		bool newFormationLine = false;


		// convert flat vector of <priority, unitID> pairs
		// to a vector of <priority, vector<unitID>> pairs
		const auto& suPair = sortedUnitPairs[k];

		const auto suGroupPair = TGroupPair{suPair.first, {}};
		const auto suGroupIter = std::lower_bound(sortedUnitGroups.begin(), sortedUnitGroups.end(), suGroupPair, ugPairComp);

		if (suGroupIter == sortedUnitGroups.end() || suGroupIter->first != suPair.first) {
			sortedUnitGroups.emplace_back(suPair.first, TGroupVect{suPair.second});

			// swap into position
			for (size_t i = sortedUnitGroups.size() - 1; i > 0; i--) {
				if (ugPairComp(sortedUnitGroups[i - 1], sortedUnitGroups[i]))
					break;

				std::swap(sortedUnitGroups[i - 1], sortedUnitGroups[i]);
			}
		} else {
			suGroupIter->second.push_back(suPair.second);
		}


		nextPos = MoveToPos(nextPos, formationSideDir, unitHandler.GetUnit(suPair.second), c, &frontMoveCommands, &newFormationLine);

		if ((++k) < sortedUnitPairs.size()) {
			MoveToPos(nextPos, formationSideDir, unitHandler.GetUnit(suPair.second), c, nullptr, &newFormationLine);

			if (!newFormationLine)
				continue;
		}

		mixedUnitIDs.clear();
		mixedUnitIDs.reserve(frontMoveCommands.size());
		mixedGroupSizes.clear();
		mixedGroupSizes.resize(sortedUnitGroups.size(), 0);


		// mix units in each row to avoid weak flanks consisting solely of e.g. artillery
		for (size_t j = 0; j < frontMoveCommands.size(); j++) {
			size_t bestGroupNum = 0;
			float bestGroupVal = 1.0f;

			for (size_t groupNum = 0; groupNum < sortedUnitGroups.size(); ++groupNum) {
				const size_t maxGroupSize = sortedUnitGroups[groupNum].second.size();
				const size_t curGroupSize = mixedGroupSizes[groupNum];

				if (curGroupSize >= maxGroupSize)
					continue;

				const float groupVal = (0.5f + curGroupSize) / (1.0f * maxGroupSize);

				if (groupVal >= bestGroupVal)
					continue;

				bestGroupVal = groupVal;
				bestGroupNum = groupNum;
			}

			// for each processed command, increase the count by 1 s.t.
			// (at most) groupSize units are shuffled around per group
			const size_t unitIndex = mixedGroupSizes[bestGroupNum]++;

			const auto& groupPair = sortedUnitGroups[bestGroupNum];
			const auto& groupUnitIDs = groupPair.second;

			mixedUnitIDs.push_back(groupUnitIDs[unitIndex]);
		}

		for (size_t i = 0; i < frontMoveCommands.size(); i++) {
			CUnit* unit = unitHandler.GetUnit(mixedUnitIDs[i]);
			CCommandAI* cai = unit->commandAI;

			cai->GiveCommand(frontMoveCommands[i].second, false);
		}

		frontMoveCommands.clear();
		sortedUnitGroups.clear();
	}
}


void CSelectedUnitsHandlerAI::CreateUnitOrder(std::vector< std::pair<float, int> >& out, int player)
{
	const std::vector<int>& playerUnitIDs = selectedUnitsHandler.netSelected[player];

	out.clear();
	out.reserve(playerUnitIDs.size());

	for (const int unitID: playerUnitIDs) {
		const CUnit* unit = unitHandler.GetUnit(unitID);

		if (unit == nullptr)
			continue;

		const UnitDef* ud = unit->unitDef;

		// give weaponless units a long range to make them go to the back
		const float range = (unit->maxRange < 1.0f)? 2000: unit->maxRange;
		const float value = ((ud->metal * 60) + ud->energy) / ud->health * range;

		out.emplace_back(value, unitID);
	}

	std::stable_sort(out.begin(), out.end(), idPairComp);
}


float3 CSelectedUnitsHandlerAI::MoveToPos(
	float3 nextCornerPos,
	float3 formationDir,
	const CUnit* unit,
	Command* command,
	std::vector<std::pair<int, Command> >* frontcmds,
	bool* newline
) {
	#if 0
	const int rowNum = posNum / formationNumColumns;
	const int colNum = posNum - rowNum * formationNumColumns;
	const float side = (0.25f + colNum * 0.5f) * groupColumnDist * ((colNum & 1)? -1: 1);
	#endif

	if ((*newline = ((nextCornerPos.x - groupAddedSpace) > groupFrontLength))) {
		nextCornerPos.x  = 0.0f;
		nextCornerPos.z -= (groupAvgLength * 2.0f * SQUARE_SIZE);
	}

	if (frontcmds == nullptr)
		return nextCornerPos;

	if (unit == nullptr)
		return nextCornerPos;

	const int unitSize = (unit->unitDef->xsize + unit->unitDef->zsize) / 2;

	float3  retPos(nextCornerPos.x + unitSize * SQUARE_SIZE * 2 + groupAddedSpace, 0, nextCornerPos.z);
	float3 movePos(nextCornerPos.x + unitSize * SQUARE_SIZE     + groupAddedSpace, 0, nextCornerPos.z); // posit in coordinates of "front"

	if (nextCornerPos.x == 0.0f) {
		movePos.x = unitSize * SQUARE_SIZE;
		retPos.x -= groupAddedSpace;
	}

	float3 pos;
	pos.x = formationRightPos.x + (movePos.x * (formationDir.x / formationDir.y)) - (movePos.z * (formationDir.z / formationDir.y));
	pos.z = formationRightPos.z + (movePos.x * (formationDir.z / formationDir.y)) + (movePos.z * (formationDir.x / formationDir.y));
	pos.y = CGround::GetHeightAboveWater(pos.x, pos.z);

	frontcmds->emplace_back(unit->id, Command(command->GetID(), command->options, pos));
	return retPos;
}


void CSelectedUnitsHandlerAI::SelectAttackNet(const Command& cmd, int player)
{
	// reuse for sorting targets, no overlap with MakeFormationFrontOrder
	sortedUnitPairs.clear();
	targetUnitIDs.clear();

	if (cmd.params.size() == 4) {
		SelectCircleUnits(cmd.GetPos(0), cmd.params[3], player, targetUnitIDs);
	} else {
		SelectRectangleUnits(cmd.GetPos(0), cmd.GetPos(3), player, targetUnitIDs);
	}

	if (targetUnitIDs.empty())
		return;

	const bool queueing = !!(cmd.options & SHIFT_KEY);
	const std::vector<int>& selected = selectedUnitsHandler.netSelected[player];

	const unsigned int targetsCount = targetUnitIDs.size();
	const unsigned int selectedCount = selected.size();
	      unsigned int realCount = 0;

	if (selectedCount == 0)
		return;


	Command attackCmd(CMD_ATTACK, cmd.options, 0.0f);

	// delete the attack commands and bail for CONTROL_KEY
	if (cmd.options & CONTROL_KEY) {
		attackCmd.options |= SHIFT_KEY;

		for (unsigned int s = 0; s < selectedCount; s++) {
			const CUnit* unit = unitHandler.GetUnit(selected[s]);

			if (unit == nullptr)
				continue;

			CCommandAI* commandAI = unit->commandAI;

			for (unsigned int t = 0; t < targetsCount; t++) {
				attackCmd.params[0] = targetUnitIDs[t];

				if (!commandAI->WillCancelQueued(attackCmd))
					continue;

				commandAI->GiveCommand(attackCmd, true);
			}
		}

		return;
	}


	// get the group center
	float3 midPos;

	for (unsigned int s = 0; s < selectedCount; s++) {
		CUnit* unit = unitHandler.GetUnit(selected[s]);

		if (unit == nullptr)
			continue;

		midPos += (queueing? LastQueuePosition(unit): float3(unit->midPos));

		realCount++;
	}

	if (realCount == 0)
		return;

	midPos /= realCount;


	// sort the targets
	for (unsigned int t = 0; t < targetsCount; t++) {
		const CUnit* unit = unitHandler.GetUnit(targetUnitIDs[t]);
		const float3 unitPos = float3(unit->midPos);

		sortedUnitPairs.emplace_back((unitPos - midPos).SqLength2D(), targetUnitIDs[t]);
	}

	std::stable_sort(sortedUnitPairs.begin(), sortedUnitPairs.end(), idPairComp);


	// give the commands; clear queueing-flag for the first
	for (unsigned int s = 0; s < selectedCount; s++) {
		if (!queueing)
			attackCmd.options &= ~SHIFT_KEY;

		CUnit* unit = unitHandler.GetUnit(selected[s]);

		if (unit == nullptr)
			continue;

		CCommandAI* commandAI = unit->commandAI;

		for (unsigned t = 0; t < targetsCount; t++) {
			attackCmd.params[0] = sortedUnitPairs[t].second;

			if (queueing && commandAI->WillCancelQueued(attackCmd))
				continue;

			commandAI->GiveCommand(attackCmd, true);

			AddUnitSetMaxSpeedCommandNet(unit, attackCmd.options);
			// following commands are always queued
			attackCmd.options |= SHIFT_KEY;
		}
	}
}


void CSelectedUnitsHandlerAI::SelectCircleUnits(
	const float3& pos,
	float radius,
	int player,
	std::vector<int>& units
) {
	units.clear();

	if (!playerHandler.IsValidPlayer(player))
		return;

	const CPlayer* p = playerHandler.Player(player);

	if (p == nullptr)
		return;

	QuadFieldQuery qfQuery;
	quadField.GetUnitsExact(qfQuery, pos, radius, false);

	const float radiusSqr = radius * radius;
	const unsigned int count = qfQuery.units->size();
	const int allyTeam = teamHandler.AllyTeam(p->team);

	units.reserve(count);

	for (unsigned int i = 0; i < count; i++) {
		CUnit* unit = (*qfQuery.units)[i];

		if (unit == nullptr)
			continue;
		if (unit->allyteam == allyTeam)
			continue;
		if (!(unit->losStatus[allyTeam] & (LOS_INLOS | LOS_INRADAR)))
			continue;

		const float dx = (pos.x - unit->midPos.x);
		const float dz = (pos.z - unit->midPos.z);

		if (((dx * dx) + (dz * dz)) > radiusSqr)
			continue;

		units.push_back(unit->id);
	}
}


void CSelectedUnitsHandlerAI::SelectRectangleUnits(
	const float3& pos0,
	const float3& pos1,
	int player,
	std::vector<int>& units
) {
	units.clear();

	if (!playerHandler.IsValidPlayer(player))
		return;

	const CPlayer* p = playerHandler.Player(player);

	if (p == nullptr)
		return;

	const float3 mins(std::min(pos0.x, pos1.x), 0.0f, std::min(pos0.z, pos1.z));
	const float3 maxs(std::max(pos0.x, pos1.x), 0.0f, std::max(pos0.z, pos1.z));

	QuadFieldQuery qfQuery;
	quadField.GetUnitsExact(qfQuery, mins, maxs);

	const unsigned int count = qfQuery.units->size();
	const int allyTeam = teamHandler.AllyTeam(p->team);

	units.reserve(count);

	for (unsigned int i = 0; i < count; i++) {
		const CUnit* unit = (*qfQuery.units)[i];

		if (unit == nullptr)
			continue;
		if (unit->allyteam == allyTeam)
			continue;
		if (!(unit->losStatus[allyTeam] & (LOS_INLOS | LOS_INRADAR)))
			continue;

		units.push_back(unit->id);
	}
}


float3 CSelectedUnitsHandlerAI::LastQueuePosition(const CUnit* unit)
{
	const CCommandQueue& queue = unit->commandAI->commandQue;

	for (auto it = queue.rbegin(); it != queue.rend(); ++it) {
		const Command& cmd = *it;

		if (cmd.params.size() >= 3)
			return cmd.GetPos(0);
	}

	return unit->midPos;
}
