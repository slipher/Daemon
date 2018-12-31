// REPLACES Unvanquished/src/cgame/cg_api.cpp

#include "cg_local.h"
#include "engine/client/cg_msgdef.h"

#include "shared/VMMain.h"
#include "shared/CommandBufferClient.h"
#include "shared/CommonProxies.h"
#include "shared/client/cg_api.h"

#include "./replay_data.h"

// Symbols required by the shared VMMain code

int VM::VM_API_VERSION = CGAME_API_VERSION;

void RenderReplayFrame();

void VM::VMHandleSyscall(uint32_t id, Util::Reader reader) {
    int major = id >> 16;
    int minor = id & 0xffff;
    if (major == VM::QVM) {
        switch (minor) {
            case CG_STATIC_INIT:
                IPC::HandleMsg<CGameStaticInitMsg>(VM::rootChannel, std::move(reader), [] (int milliseconds) {
                    VM::InitializeProxies(milliseconds);
                    FS::Initialize();
                    srand(time(nullptr));
					cmdBuffer.Init();
                });
                break;

            case CG_INIT:
                IPC::HandleMsg<CGameInitMsg>(VM::rootChannel, std::move(reader), [] (int serverMessageNum, int clientNum, glconfig_t gl, GameStateCSs gamestate) {
                    //CG_Init(serverMessageNum, clientNum, gl, gamestate);
                    cmdBuffer.TryFlush();
                });
                break;

            case CG_SHUTDOWN:
                IPC::HandleMsg<CGameShutdownMsg>(VM::rootChannel, std::move(reader), [] {
                    //CG_Shutdown();
                });
                break;

			case CG_ROCKET_VM_INIT:
				IPC::HandleMsg<CGameRocketInitMsg>(VM::rootChannel, std::move(reader), [] (glconfig_t gl) {
					//CG_Rocket_Init(gl);
				});
				break;

			case CG_ROCKET_FRAME:
				IPC::HandleMsg<CGameRocketFrameMsg>(VM::rootChannel, std::move(reader), [] (cgClientState_t cs) {
					//CG_Rocket_Frame(cs);
					RenderReplayFrame();
					cmdBuffer.TryFlush();
				});
				break;

            case CG_DRAW_ACTIVE_FRAME:
                IPC::HandleMsg<CGameDrawActiveFrameMsg>(VM::rootChannel, std::move(reader), [] (int serverTime, bool demoPlayback) {
                    //CG_DrawActiveFrame(serverTime, demoPlayback);
                    cmdBuffer.TryFlush();
                });
                break;

            case CG_CROSSHAIR_PLAYER:
                IPC::HandleMsg<CGameCrosshairPlayerMsg>(VM::rootChannel, std::move(reader), [] (int& player) {
                    player = -1;
                });
                break;

            case CG_KEY_EVENT:
                IPC::HandleMsg<CGameKeyEventMsg>(VM::rootChannel, std::move(reader), [] (Keyboard::Key key, bool down) {
                    //CG_KeyEvent(key, down);
                    cmdBuffer.TryFlush();
                });
                break;

            case CG_MOUSE_EVENT:
                IPC::HandleMsg<CGameMouseEventMsg>(VM::rootChannel, std::move(reader), [] (int dx, int dy) {
                    //CG_MouseEvent(dx, dy);
					cmdBuffer.TryFlush();
                });
                break;

			case CG_MOUSE_POS_EVENT:
				IPC::HandleMsg<CGameMousePosEventMsg>(VM::rootChannel, std::move(reader), [] (int x, int y) {
					//CG_MousePosEvent(x, y);
					cmdBuffer.TryFlush();
				});
				break;

			case CG_CHARACTER_INPUT_EVENT:
				IPC::HandleMsg<CGameCharacterInputMsg>(VM::rootChannel, std::move(reader), [] (int c) {
					//Rocket_ProcessTextInput(c);
					cmdBuffer.TryFlush();
				});
				break;

			case CG_CONSOLE_LINE:
				IPC::HandleMsg<CGameConsoleLineMsg>(VM::rootChannel, std::move(reader), [](std::string str) {
					//Rocket_AddConsoleText( str );
					cmdBuffer.TryFlush();
				});
				break;

			case CG_FOCUS_EVENT:
				IPC::HandleMsg<CGameFocusEventMsg>(VM::rootChannel, std::move(reader), [] (bool focus) {
					//CG_FocusEvent(focus);
					cmdBuffer.TryFlush();
				});
				break;

            default:
                Com_Error(errorParm_t::ERR_DROP, "VMMain(): unknown cgame command %i", minor);

        }
    } else if (major < VM::LAST_COMMON_SYSCALL) {
        VM::HandleCommonSyscall(major, minor, std::move(reader), VM::rootChannel);
    } else {
        Com_Error(errorParm_t::ERR_DROP, "unhandled VM major syscall number %i", major);
    }
}

RenderReplayData DeserializeReplayData(Util::Reader& reader) {
	RenderReplayData data;
	data.shaders = reader.Read<decltype(data.shaders)>();
	return data;
}

ReplayRemap LoadResources(const RenderReplayData& data) {
	ReplayRemap remap;
	for (const auto& pair : data.shaders) {
		int oldHandle = pair.first;
		int currentHandle = trap_R_RegisterShader(pair.second.c_str(), RSF_DEFAULT); //TODO flags
		if (!currentHandle) {
			Sys::Drop("Couldn't load shader for render replay: \"%s\"", pair.second);
		}
		remap.shaders.emplace_back(oldHandle, currentHandle);
	}
	return remap;
}


template<typename Id, typename... MsgArgs>
void ForwardMsgInternal(IPC::Message<Id, MsgArgs...> p, Util::Reader& reader) {
	using Message = IPC::Message<Id, MsgArgs...>;
	typename IPC::detail::MapTuple<typename Message::Inputs>::type inputs;
	reader.FillTuple<0>(Util::TypeListFromTuple<typename Message::Inputs>(), inputs);
	HandlePointers hp = FindHandles(cgameImport_t(Id::value & 0xffff), &inputs);
	for (int* p : hp.shaders) {
		if (!*p) continue;
		auto it = std::lower_bound(rremap.shaders.begin(), rremap.shaders.end(), std::make_pair(*p, 0));
		if (it == rremap.shaders.end() || it->first != *p) {
			Sys::Drop("Render replay: unmapped shader handle %d", *p);
		}
		*p = it->second;
	}
	struct Sender {
		void operator()(MsgArgs... args) {
			cmdBuffer.SendMsg<decltype(p)>(std::move(args)...);
		}
	};
	Util::apply(Sender(), std::move(inputs));
}

template<typename Msg> void ForwardMsg(Util::Reader& reader) {
	ForwardMsgInternal(Msg(), reader);
}

std::string rrname_last = "";
Log::Logger rr("rr", "[rr]", Log::Level::NOTICE);
std::vector<std::pair<cgameImport_t, std::vector<char>>> messages;
ReplayRemap rremap;

void GetMessagesAndLoadMap() {
	std::string rrname = Cvar::GetValue("rrname");
	if (rrname == rrname_last) return;
	rrname_last = rrname;
	rr.Notice("loading snapshot %s", rrname);

	std::string renderReplayFilename = "rendersnapshot/" + rrname + ".cmds";
	FS::File f = FS::HomePath::OpenRead(renderReplayFilename);
	std::string contents = f.ReadAll();
	Util::Reader r;
	r.GetData().assign(contents.begin(), contents.end());
	int version = r.Read<int>();
	if (version != RR_VERSION) {
		Sys::Drop("Render replay file written with protocol %d but I speak protocol %d", version, RR_VERSION);
	}
	std::string map = r.Read<std::string>();
	messages = r.Read<decltype(messages)>();
	RenderReplayData replayData = DeserializeReplayData(r);
	r.CheckEndRead();
	rr.Notice("read %d raw messages", messages.size());
	// TODO make it also possible to be in the main menu
	// TODO restart vm if 2nd map is loaded
	rr.Notice("map: %s", map);
	trap_R_LoadWorldMap(Str::Format("maps/%s.bsp", map).c_str());
	rr.Notice("loaded map");
	rremap = LoadResources(replayData);
	rr.Notice("loaded other resources");
}

void ReplayMessages() {
	for (const auto& m : messages) {
		Util::Reader reader;
		reader.GetData() = m.second;
#define FWD(msgtype) case msgtype::id & 0xFFFF: ForwardMsg<msgtype>(reader); break
		switch (m.first) {
			FWD(Render::ScissorEnableMsg);
			FWD(Render::ScissorSetMsg);
			FWD(Render::ClearSceneMsg);
			FWD(Render::AddRefEntityToSceneMsg);
			FWD(Render::AddPolyToSceneMsg);
			FWD(Render::AddPolysToSceneMsg);
			FWD(Render::AddLightToSceneMsg);
			FWD(Render::AddAdditiveLightToSceneMsg);
			FWD(Render::SetColorMsg);
			FWD(Render::SetClipRegionMsg);
			FWD(Render::ResetClipRegionMsg);
			FWD(Render::DrawStretchPicMsg);
			FWD(Render::DrawRotatedPicMsg);
			FWD(Render::AddVisTestToSceneMsg);
			FWD(Render::UnregisterVisTestMsg);
			FWD(Render::SetColorGradingMsg);
			FWD(Render::RenderSceneMsg);
			FWD(Render::Add2dPolysIndexedMsg);
		default:
			Sys::Drop("unhandled msg id %d", int(m.first));
		}
		reader.CheckEndRead();
	}
}

void RenderReplayFrame() {
	GetMessagesAndLoadMap();
	ReplayMessages();
}
