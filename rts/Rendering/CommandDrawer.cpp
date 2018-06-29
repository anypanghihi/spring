/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "CommandDrawer.h"
#include "LineDrawer.h"
#include "Game/Camera.h"
#include "Game/GameHelper.h"
#include "Game/UI/CommandColors.h"
#include "Game/WaitCommandsAI.h"
#include "Map/Ground.h"
#include "Rendering/GL/glExtra.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/GL/RenderDataBuffer.hpp"
#include "Sim/Features/Feature.h"
#include "Sim/Features/FeatureHandler.h"
#include "Sim/Units/CommandAI/Command.h"
#include "Sim/Units/CommandAI/CommandQueue.h"
#include "Sim/Units/CommandAI/CommandAI.h"
#include "Sim/Units/CommandAI/AirCAI.h"
#include "Sim/Units/CommandAI/BuilderCAI.h"
#include "Sim/Units/CommandAI/FactoryCAI.h"
#include "Sim/Units/CommandAI/MobileCAI.h"
#include "Sim/Units/Unit.h"
#include "Sim/Units/UnitDef.h"
#include "Sim/Units/UnitHandler.h"
#include "System/myMath.h"
#include "System/Log/ILog.h"

static const CUnit* GetTrackableUnit(const CUnit* caiOwner, const CUnit* cmdUnit)
{
	if (cmdUnit == nullptr)
		return nullptr;
	if ((cmdUnit->losStatus[caiOwner->allyteam] & (LOS_INLOS | LOS_INRADAR)) == 0)
		return nullptr;

	return cmdUnit;
}

CommandDrawer* CommandDrawer::GetInstance() {
	// luaQueuedUnitSet gets cleared each frame, so this is fine wrt. reloading
	static CommandDrawer drawer;
	return &drawer;
}



void CommandDrawer::Draw(const CCommandAI* cai) const {
	GL::RenderDataBufferC* buffer = GL::GetRenderBufferC();
	Shader::IProgramObject* shader = buffer->GetShader();

	// note: {Air,Builder}CAI inherit from MobileCAI, so test that last
	if ((dynamic_cast<const     CAirCAI*>(cai)) != nullptr) {     DrawAirCAICommands(static_cast<const     CAirCAI*>(cai), buffer); return; }
	if ((dynamic_cast<const CBuilderCAI*>(cai)) != nullptr) { DrawBuilderCAICommands(static_cast<const CBuilderCAI*>(cai), buffer); return; }
	if ((dynamic_cast<const CFactoryCAI*>(cai)) != nullptr) { DrawFactoryCAICommands(static_cast<const CFactoryCAI*>(cai), buffer); return; }
	if ((dynamic_cast<const  CMobileCAI*>(cai)) != nullptr) {  DrawMobileCAICommands(static_cast<const  CMobileCAI*>(cai), buffer); return; }

	DrawCommands(cai, buffer);

	// hand off all surface circles
	// TODO: grab the minimap transform
	shader->Enable();
	shader->SetUniformMatrix4x4<const char*, float>("u_movi_mat", false, camera->GetViewMatrix());
	shader->SetUniformMatrix4x4<const char*, float>("u_proj_mat", false, camera->GetProjectionMatrix());
	buffer->Submit(GL_LINES);
	shader->Disable();
}



void CommandDrawer::AddLuaQueuedUnit(const CUnit* unit) {
	// needs to insert by id, pointers can become dangling
	luaQueuedUnitSet.insert(unit->id);
}

void CommandDrawer::DrawLuaQueuedUnitSetCommands() const
{
	if (luaQueuedUnitSet.empty())
		return;

	glDisable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);

	lineDrawer.Configure(cmdColors.UseColorRestarts(),
	                     cmdColors.UseRestartColor(),
	                     cmdColors.restart,
	                     cmdColors.RestartAlpha());
	lineDrawer.SetupLineStipple();

	glEnable(GL_BLEND);
	glBlendFunc((GLenum)cmdColors.QueuedBlendSrc(),
	            (GLenum)cmdColors.QueuedBlendDst());

	glLineWidth(cmdColors.QueuedLineWidth());

	for (auto ui = luaQueuedUnitSet.cbegin(); ui != luaQueuedUnitSet.cend(); ++ui) {
		const CUnit* unit = unitHandler.GetUnit(*ui);

		if (unit == nullptr || unit->commandAI == nullptr)
			continue;

		Draw(unit->commandAI);
	}

	glLineWidth(1.0f);
	glEnable(GL_DEPTH_TEST);
}

void CommandDrawer::DrawCommands(const CCommandAI* cai, GL::RenderDataBufferC* rdb) const
{
	const CUnit* owner = cai->owner;
	const CCommandQueue& commandQue = cai->commandQue;

	lineDrawer.StartPath(owner->GetObjDrawMidPos(), cmdColors.start);

	if (owner->selfDCountdown != 0)
		lineDrawer.DrawIconAtLastPos(CMD_SELFD);

	for (auto ci = commandQue.begin(); ci != commandQue.end(); ++ci) {
		const int cmdID = ci->GetID();

		switch (cmdID) {
			case CMD_ATTACK:
			case CMD_MANUALFIRE: {
				if (ci->params.size() == 1) {
					const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->params[0]));

					if (unit != nullptr)
						lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.attack);

				} else {
					assert(ci->params.size() >= 3);

					const float x = ci->params[0];
					const float z = ci->params[2];
					const float y = CGround::GetHeightReal(x, z, false) + 3.0f;

					lineDrawer.DrawLineAndIcon(cmdID, float3(x, y, z), cmdColors.attack);
				}
			} break;

			case CMD_WAIT: {
				DrawWaitIcon(*ci);
			} break;
			case CMD_SELFD: {
				lineDrawer.DrawIconAtLastPos(cmdID);
			} break;

			default: {
				DrawDefaultCommand(*ci, owner, rdb);
			} break;
		}
	}
}



void CommandDrawer::DrawAirCAICommands(const CAirCAI* cai, GL::RenderDataBufferC* rdb) const
{
	const CUnit* owner = cai->owner;
	const CCommandQueue& commandQue = cai->commandQue;

	lineDrawer.StartPath(owner->GetObjDrawMidPos(), cmdColors.start);

	if (owner->selfDCountdown != 0)
		lineDrawer.DrawIconAtLastPos(CMD_SELFD);

	for (auto ci = commandQue.begin(); ci != commandQue.end(); ++ci) {
		const int cmdID = ci->GetID();

		switch (cmdID) {
			case CMD_MOVE: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.move);
			} break;
			case CMD_FIGHT: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.fight);
			} break;
			case CMD_PATROL: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.patrol);
			} break;

			case CMD_ATTACK: {
				if (ci->params.size() == 1) {
					const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->params[0]));

					if (unit != nullptr)
						lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.attack);

				} else {
					assert(ci->params.size() >= 3);

					const float x = ci->params[0];
					const float z = ci->params[2];
					const float y = CGround::GetHeightReal(x, z, false) + 3.0f;

					lineDrawer.DrawLineAndIcon(cmdID, float3(x, y, z), cmdColors.attack);
				}
			} break;

			case CMD_AREA_ATTACK: {
				const float3& endPos = ci->GetPos(0);

				lineDrawer.DrawLineAndIcon(cmdID, endPos, cmdColors.attack);
				lineDrawer.Break(endPos, cmdColors.attack);
				glSurfaceCircleRB(rdb, {endPos, ci->params[3]}, cmdColors.attack, 20.0f);
				lineDrawer.RestartWithColor(cmdColors.attack);
			} break;

			case CMD_GUARD: {
				const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->params[0]));

				if (unit != nullptr)
					lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.guard);

			} break;

			case CMD_WAIT: {
				DrawWaitIcon(*ci);
			} break;
			case CMD_SELFD: {
				lineDrawer.DrawIconAtLastPos(cmdID);
			} break;

			default: {
				DrawDefaultCommand(*ci, owner, rdb);
			} break;
		}
	}
}



void CommandDrawer::DrawBuilderCAICommands(const CBuilderCAI* cai, GL::RenderDataBufferC* rdb) const
{
	const CUnit* owner = cai->owner;
	const CCommandQueue& commandQue = cai->commandQue;

	lineDrawer.StartPath(owner->GetObjDrawMidPos(), cmdColors.start);

	if (owner->selfDCountdown != 0)
		lineDrawer.DrawIconAtLastPos(CMD_SELFD);

	for (const Command& ci: commandQue) {
		const int cmdID = ci.GetID();

		if (cmdID < 0) {
			if (cai->buildOptions.find(cmdID) != cai->buildOptions.end()) {
				BuildInfo bi;

				if (!bi.Parse(ci))
					continue;

				cursorIcons.AddBuildIcon(cmdID, bi.pos, owner->team, bi.buildFacing);
				lineDrawer.DrawLine(bi.pos, cmdColors.build);

				// draw metal extraction range
				if (bi.def->extractRange > 0.0f) {
					lineDrawer.Break(bi.pos, cmdColors.build);
					glSurfaceCircleRB(rdb, {bi.pos, bi.def->extractRange}, cmdColors.rangeExtract, 40.0f);
					lineDrawer.Restart();
				}
			}
			continue;
		}

		switch (cmdID) {
			case CMD_MOVE: {
				lineDrawer.DrawLineAndIcon(cmdID, ci.GetPos(0), cmdColors.move);
			} break;
			case CMD_FIGHT:{
				lineDrawer.DrawLineAndIcon(cmdID, ci.GetPos(0), cmdColors.fight);
			} break;
			case CMD_PATROL: {
				lineDrawer.DrawLineAndIcon(cmdID, ci.GetPos(0), cmdColors.patrol);
			} break;

			case CMD_GUARD: {
				const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci.params[0]));

				if (unit != nullptr)
					lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.guard);

			} break;

			case CMD_RESTORE: {
				const float3& endPos = ci.GetPos(0);

				lineDrawer.DrawLineAndIcon(cmdID, endPos, cmdColors.restore);
				lineDrawer.Break(endPos, cmdColors.restore);
				glSurfaceCircleRB(rdb, {endPos, ci.params[3]}, cmdColors.restore, 20.0f);
				lineDrawer.RestartWithColor(cmdColors.restore);
			} break;

			case CMD_ATTACK:
			case CMD_MANUALFIRE: {
				if (ci.params.size() == 1) {
					const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci.params[0]));

					if (unit != nullptr)
						lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.attack);

				} else {
					assert(ci.params.size() >= 3);

					const float x = ci.params[0];
					const float z = ci.params[2];
					const float y = CGround::GetHeightReal(x, z, false) + 3.0f;

					lineDrawer.DrawLineAndIcon(cmdID, float3(x, y, z), cmdColors.attack);
				}
			} break;

			case CMD_RECLAIM:
			case CMD_RESURRECT: {
				const float* color = (cmdID == CMD_RECLAIM) ? cmdColors.reclaim
				                                             : cmdColors.resurrect;
				if (ci.params.size() == 4) {
					const float3& endPos = ci.GetPos(0);

					lineDrawer.DrawLineAndIcon(cmdID, endPos, color);
					lineDrawer.Break(endPos, color);
					glSurfaceCircleRB(rdb, {endPos, ci.params[3]}, color, 20.0f);
					lineDrawer.RestartWithColor(color);
				} else {
					assert(ci.params[0] >= 0.0f);

					const unsigned int id = std::max(0.0f, ci.params[0]);

					if (id >= unitHandler.MaxUnits()) {
						const CFeature* feature = featureHandler.GetFeature(id - unitHandler.MaxUnits());

						if (feature != nullptr)
							lineDrawer.DrawLineAndIcon(cmdID, feature->GetObjDrawMidPos(), color);

					} else {
						const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(id));

						if (unit != nullptr && unit != owner)
							lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), color);

					}
				}
			} break;

			case CMD_REPAIR:
			case CMD_CAPTURE: {
				const float* color = (ci.GetID() == CMD_REPAIR) ? cmdColors.repair: cmdColors.capture;

				if (ci.params.size() == 4) {
					const float3& endPos = ci.GetPos(0);

					lineDrawer.DrawLineAndIcon(cmdID, endPos, color);
					lineDrawer.Break(endPos, color);
					glSurfaceCircleRB(rdb, {endPos, ci.params[3]}, color, 20.0f);
					lineDrawer.RestartWithColor(color);
				} else {
					if (ci.params.size() >= 1) {
						const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci.params[0]));

						if (unit != nullptr)
							lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), color);

					}
				}
			} break;

			case CMD_LOAD_ONTO: {
				const CUnit* unit = unitHandler.GetUnitUnsafe(ci.params[0]);
				lineDrawer.DrawLineAndIcon(cmdID, unit->pos, cmdColors.load);
			} break;
			case CMD_WAIT: {
				DrawWaitIcon(ci);
			} break;
			case CMD_SELFD: {
				lineDrawer.DrawIconAtLastPos(ci.GetID());
			} break;

			default: {
				DrawDefaultCommand(ci, owner, rdb);
			} break;
		}
	}
}



void CommandDrawer::DrawFactoryCAICommands(const CFactoryCAI* cai, GL::RenderDataBufferC* rdb) const
{
	const CUnit* owner = cai->owner;
	const CCommandQueue& commandQue = cai->commandQue;
	const CCommandQueue& newUnitCommands = cai->newUnitCommands;

	lineDrawer.StartPath(owner->GetObjDrawMidPos(), cmdColors.start);

	if (owner->selfDCountdown != 0)
		lineDrawer.DrawIconAtLastPos(CMD_SELFD);

	if (!commandQue.empty() && (commandQue.front().GetID() == CMD_WAIT))
		DrawWaitIcon(commandQue.front());

	for (const Command& ci: newUnitCommands) {
		const int cmdID = ci.GetID();

		switch (cmdID) {
			case CMD_MOVE: {
				lineDrawer.DrawLineAndIcon(cmdID, ci.GetPos(0) + UpVector * 3.0f, cmdColors.move);
			} break;
			case CMD_FIGHT: {
				lineDrawer.DrawLineAndIcon(cmdID, ci.GetPos(0) + UpVector * 3.0f, cmdColors.fight);
			} break;
			case CMD_PATROL: {
				lineDrawer.DrawLineAndIcon(cmdID, ci.GetPos(0) + UpVector * 3.0f, cmdColors.patrol);
			} break;

			case CMD_ATTACK: {
				if (ci.params.size() == 1) {
					const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci.params[0]));

					if (unit != nullptr)
						lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.attack);

				} else {
					assert(ci.params.size() >= 3);

					const float x = ci.params[0];
					const float z = ci.params[2];
					const float y = CGround::GetHeightReal(x, z, false) + 3.0f;

					lineDrawer.DrawLineAndIcon(cmdID, float3(x, y, z), cmdColors.attack);
				}
			} break;

			case CMD_GUARD: {
				const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci.params[0]));

				if (unit != nullptr)
					lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.guard);

			} break;

			case CMD_WAIT: {
				DrawWaitIcon(ci);
			} break;
			case CMD_SELFD: {
				lineDrawer.DrawIconAtLastPos(cmdID);
			} break;

			default: {
				DrawDefaultCommand(ci, owner, rdb);
			} break;
		}

		if ((cmdID < 0) && (ci.params.size() >= 3)) {
			BuildInfo bi;

			if (!bi.Parse(ci))
				continue;

			cursorIcons.AddBuildIcon(cmdID, bi.pos, owner->team, bi.buildFacing);
			lineDrawer.DrawLine(bi.pos, cmdColors.build);

			// draw metal extraction range
			if (bi.def->extractRange > 0.0f) {
				lineDrawer.Break(bi.pos, cmdColors.build);
				glSurfaceCircleRB(rdb, {bi.pos, bi.def->extractRange}, cmdColors.rangeExtract, 40.0f);
				lineDrawer.Restart();
			}
		}
	}
}



void CommandDrawer::DrawMobileCAICommands(const CMobileCAI* cai, GL::RenderDataBufferC* rdb) const
{
	const CUnit* owner = cai->owner;
	const CCommandQueue& commandQue = cai->commandQue;

	lineDrawer.StartPath(owner->GetObjDrawMidPos(), cmdColors.start);

	if (owner->selfDCountdown != 0)
		lineDrawer.DrawIconAtLastPos(CMD_SELFD);

	for (auto ci = commandQue.begin(); ci != commandQue.end(); ++ci) {
		const int cmdID = ci->GetID();

		switch (cmdID) {
			case CMD_MOVE: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.move);
			} break;
			case CMD_PATROL: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.patrol);
			} break;
			case CMD_FIGHT: {
				if (ci->params.size() >= 3)
					lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.fight);

			} break;

			case CMD_ATTACK:
			case CMD_MANUALFIRE: {
				if (ci->params.size() == 1) {
					const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->params[0]));

					if (unit != nullptr)
						lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.attack);

				}

				if (ci->params.size() >= 3) {
					const float x = ci->params[0];
					const float z = ci->params[2];
					const float y = CGround::GetHeightReal(x, z, false) + 3.0f;

					lineDrawer.DrawLineAndIcon(cmdID, float3(x, y, z), cmdColors.attack);
				}
			} break;

			case CMD_GUARD: {
				const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->params[0]));

				if (unit != nullptr)
					lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.guard);

			} break;

			case CMD_LOAD_ONTO: {
				const CUnit* unit = unitHandler.GetUnitUnsafe(ci->params[0]);
				lineDrawer.DrawLineAndIcon(cmdID, unit->pos, cmdColors.load);
			} break;

			case CMD_LOAD_UNITS: {
				if (ci->params.size() == 4) {
					const float3& endPos = ci->GetPos(0);

					lineDrawer.DrawLineAndIcon(cmdID, endPos, cmdColors.load);
					lineDrawer.Break(endPos, cmdColors.load);
					glSurfaceCircleRB(rdb, {endPos, ci->params[3]}, cmdColors.load, 20.0f);
					lineDrawer.RestartWithColor(cmdColors.load);
				} else {
					const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(ci->params[0]));

					if (unit != nullptr)
						lineDrawer.DrawLineAndIcon(cmdID, unit->GetObjDrawErrorPos(owner->allyteam), cmdColors.load);

				}
			} break;

			case CMD_UNLOAD_UNITS: {
				if (ci->params.size() == 5) {
					const float3& endPos = ci->GetPos(0);

					lineDrawer.DrawLineAndIcon(cmdID, endPos, cmdColors.unload);
					lineDrawer.Break(endPos, cmdColors.unload);
					glSurfaceCircleRB(rdb, {endPos, ci->params[3]}, cmdColors.unload, 20.0f);
					lineDrawer.RestartWithColor(cmdColors.unload);
				}
			} break;

			case CMD_UNLOAD_UNIT: {
				lineDrawer.DrawLineAndIcon(cmdID, ci->GetPos(0), cmdColors.unload);
			} break;
			case CMD_WAIT: {
				DrawWaitIcon(*ci);
			} break;
			case CMD_SELFD: {
				lineDrawer.DrawIconAtLastPos(cmdID);
			} break;

			default: {
				DrawDefaultCommand(*ci, owner, rdb);
			} break;
		}
	}
}


void CommandDrawer::DrawWaitIcon(const Command& cmd) const
{
	waitCommandsAI.AddIcon(cmd, lineDrawer.GetLastPos());
}

void CommandDrawer::DrawDefaultCommand(const Command& c, const CUnit* owner, GL::RenderDataBufferC* rdb) const
{
	// TODO add Lua callin perhaps, for more elaborate needs?
	const CCommandColors::DrawData* dd = cmdColors.GetCustomCmdData(c.GetID());

	if (dd == nullptr)
		return;

	switch (c.params.size()) {
		case  0: { return; } break;
		case  1: {         } break;
		case  2: {         } break;
		default: {
			const float3 endPos = c.GetPos(0) + UpVector * 3.0f;

			if (!dd->showArea || (c.params.size() < 4)) {
				lineDrawer.DrawLineAndIcon(dd->cmdIconID, endPos, dd->color);
			} else {
				lineDrawer.DrawLineAndIcon(dd->cmdIconID, endPos, dd->color);
				lineDrawer.Break(endPos, dd->color);
				glSurfaceCircleRB(rdb, {endPos, c.params[3]}, dd->color, 20.0f);
				lineDrawer.RestartWithColor(dd->color);
			}

			return;
		} break;
	}

	// allow a second param (ignored here) for custom commands
	const CUnit* unit = GetTrackableUnit(owner, unitHandler.GetUnit(c.params[0]));

	if (unit == nullptr)
		return;

	lineDrawer.DrawLineAndIcon(dd->cmdIconID, unit->GetObjDrawErrorPos(owner->allyteam), dd->color);
}




void CommandDrawer::DrawQueuedBuildingSquaresAW(const CBuilderCAI* cai) const
{
	const CCommandQueue& commandQue = cai->commandQue;
	const auto& buildOptions = cai->buildOptions;

	GL::RenderDataBufferC* buffer = GL::GetRenderBufferC();
	Shader::IProgramObject* shader = buffer->GetShader();

	assert(shader->IsBound());

	for (const Command& c: commandQue) {
		if (buildOptions.find(c.GetID()) == buildOptions.end())
			continue;

		BuildInfo bi;

		if (!bi.Parse(c))
			continue;

		if (!camera->InView(bi.pos = CGameHelper::Pos2BuildPos(bi, false)))
			continue;

		#if 0
		// skip under-water positions
		if (bi.pos.y < 0.0f)
			continue;
		#endif

		const float xsize = bi.GetXSize() * (SQUARE_SIZE >> 1);
		const float zsize = bi.GetZSize() * (SQUARE_SIZE >> 1);

		const float h  = bi.pos.y;
		const float x1 = bi.pos.x - xsize;
		const float z1 = bi.pos.z - zsize;
		const float x2 = bi.pos.x + xsize;
		const float z2 = bi.pos.z + zsize;

		// above-water verts
		buffer->SafeAppend({{x1, h + 1.0f, z1}, {buildQueueSquareColor}});
		buffer->SafeAppend({{x1, h + 1.0f, z2}, {buildQueueSquareColor}});
		buffer->SafeAppend({{x2, h + 1.0f, z2}, {buildQueueSquareColor}});
		buffer->SafeAppend({{x2, h + 1.0f, z1}, {buildQueueSquareColor}});

		if (bi.pos.y >= 0.0f)
			continue;

		// below-water verts
		buffer->SafeAppend({{x1, 0.0f, z1}, {0.0f, 0.5f, 1.0f, 1.0f}});
		buffer->SafeAppend({{x1, 0.0f, z2}, {0.0f, 0.5f, 1.0f, 1.0f}});
		buffer->SafeAppend({{x2, 0.0f, z2}, {0.0f, 0.5f, 1.0f, 1.0f}});
		buffer->SafeAppend({{x2, 0.0f, z1}, {0.0f, 0.5f, 1.0f, 1.0f}});
	}

	buffer->Submit(GL_QUADS);
}

void CommandDrawer::DrawQueuedBuildingSquaresUW(const CBuilderCAI* cai) const
{
	const CCommandQueue& commandQue = cai->commandQue;
	const auto& buildOptions = cai->buildOptions;

	GL::RenderDataBufferC* buffer = GL::GetRenderBufferC();
	Shader::IProgramObject* shader = buffer->GetShader();

	assert(shader->IsBound());

	#if 0
	// combined with DrawQueuedBuildingSquaresAW; saves a Submit
	for (const Command& c: commandQue) {
		if (buildOptions.find(c.GetID()) == buildOptions.end())
			continue;

		BuildInfo bi;

		if (!bi.Parse(c))
			continue;

		if (!camera->InView(bi.pos = CGameHelper::Pos2BuildPos(bi, false)))
			continue;

		// skip above-water positions
		if (bi.pos.y >= 0.0f)
			continue;

		const float xsize = bi.GetXSize() * (SQUARE_SIZE >> 1);
		const float zsize = bi.GetZSize() * (SQUARE_SIZE >> 1);

		const float h  = bi.pos.y;
		const float x1 = bi.pos.x - xsize;
		const float z1 = bi.pos.z - zsize;
		const float x2 = bi.pos.x + xsize;
		const float z2 = bi.pos.z + zsize;

		buffer->SafeAppend({{x1, 0.0f, z1}, {0.0f, 0.5f, 1.0f, 1.0f}});
		buffer->SafeAppend({{x1, 0.0f, z2}, {0.0f, 0.5f, 1.0f, 1.0f}});
		buffer->SafeAppend({{x2, 0.0f, z2}, {0.0f, 0.5f, 1.0f, 1.0f}});
		buffer->SafeAppend({{x2, 0.0f, z1}, {0.0f, 0.5f, 1.0f, 1.0f}});
	}

	buffer->Submit(GL_QUADS);
	#endif


	for (const Command& c: commandQue) {
		if (buildOptions.find(c.GetID()) == buildOptions.end())
			continue;

		BuildInfo bi;

		if (!bi.Parse(c))
			continue;

		if (!camera->InView(bi.pos = CGameHelper::Pos2BuildPos(bi, false)))
			continue;

		if (bi.pos.y >= 0.0f)
			continue;

		const float xsize = bi.GetXSize() * (SQUARE_SIZE >> 1);
		const float zsize = bi.GetZSize() * (SQUARE_SIZE >> 1);

		const float h  = bi.pos.y;
		const float x1 = bi.pos.x - xsize;
		const float z1 = bi.pos.z - zsize;
		const float x2 = bi.pos.x + xsize;
		const float z2 = bi.pos.z + zsize;

		// vertical lines for gauging depth
		buffer->SafeAppend({{x1, h   , z1}, {0.0f, 0.0f, 1.0f, 0.5f}});
		buffer->SafeAppend({{x1, 0.0f, z1}, {0.0f, 0.5f, 1.0f, 1.0f}});
		buffer->SafeAppend({{x2, h   , z1}, {0.0f, 0.0f, 1.0f, 0.5f}});
		buffer->SafeAppend({{x2, 0.0f, z1}, {0.0f, 0.5f, 1.0f, 1.0f}});
		buffer->SafeAppend({{x2, h   , z2}, {0.0f, 0.0f, 1.0f, 0.5f}});
		buffer->SafeAppend({{x2, 0.0f, z2}, {0.0f, 0.5f, 1.0f, 1.0f}});
		buffer->SafeAppend({{x1, h   , z2}, {0.0f, 0.0f, 1.0f, 0.5f}});
		buffer->SafeAppend({{x1, 0.0f, z2}, {0.0f, 0.5f, 1.0f, 1.0f}});
	}

	buffer->Submit(GL_LINES);
}


void CommandDrawer::DrawQueuedBuildingSquares(const CBuilderCAI* cai) const
{
	assert(buildQueueSquareColor != nullptr);

	// caller sets LINE polygon-mode and binds shader
	DrawQueuedBuildingSquaresAW(cai);
	DrawQueuedBuildingSquaresUW(cai);
}

