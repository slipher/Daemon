#include "../../client/cg_msgdef.h"

constexpr int RR_VERSION = 1;

struct RenderRecordData {
	std::set<int> modelHandles;
	std::set<int> shaderHandles;
	std::set<int> skinHandles;

	void RegisterModelHandle(int handle) {
		modelHandles.insert(handle);
	}
	void RegisterShaderHandle(int handle) {
		shaderHandles.insert(handle);
	}
	void RegisterSkinHandle(int handle) {
		skinHandles.insert(handle);
	}
	void RegisterRefEntity(const refEntity_t& entity) {
		RegisterModelHandle(entity.hModel);
		RegisterSkinHandle(entity.customSkin);
		RegisterShaderHandle(entity.customShader);
	}
};

struct RenderReplayData {
	std::vector<std::pair<int, std::string>> shaders;
};

struct ReplayRemap {
	std::vector<std::pair<int, int>> shaders;
};

struct HandlePointers {
	std::vector<int*> shaders;
};

void SaveReplayData(const RenderRecordData& data, Util::Writer& writer);

struct HandleLoc {
	int tupleOffset;
};

template<typename Msg, int index, typename Map>
void IntParam(Map& m) {
	static_assert(std::is_same<int, typename std::tuple_element<index, Msg::Inputs>::type>::value, "param is not int");
	Msg::Inputs dummyParams;
	int tupleOffset = reinterpret_cast<char*>(&std::get<index>(dummyParams)) - reinterpret_cast<char*>(&dummyParams);
	m[cgameImport_t(Msg::id & 0xFFFF)].push_back({ tupleOffset });
}

template<typename Msg, int index, typename Param, typename Map>
void StructParam(int Param::*member, Map& m) {
	static_assert(std::is_same<Param, typename std::tuple_element<index, Msg::Inputs>::type>::value, "param does not match type");
	Msg::Inputs dummyParams;
	int tupleOffset = reinterpret_cast<char*>(&(std::get<index>(dummyParams).*member)) - reinterpret_cast<char*>(&dummyParams);
	m[cgameImport_t(Msg::id & 0xFFFF)].push_back({ tupleOffset });
}

inline const std::map<cgameImport_t, std::vector<HandleLoc>>& ShaderLocs() {
	static const auto locs = [] {
		std::map<cgameImport_t, std::vector<HandleLoc>> locs;
		StructParam<Render::AddRefEntityToSceneMsg, 0>(&refEntity_t::customShader, locs);
		IntParam<Render::AddPolyToSceneMsg, 0>(locs);
		IntParam<Render::AddPolysToSceneMsg, 0>(locs);
		IntParam<Render::AddLightToSceneMsg, 6>(locs);
		IntParam<Render::DrawStretchPicMsg, 8>(locs);
		IntParam<Render::DrawRotatedPicMsg, 8>(locs);
		IntParam<Render::SetColorGradingMsg, 1>(locs);
		IntParam<Render::Add2dPolysIndexedMsg, 6>(locs);
		return locs;
	}();
	return locs;
}

inline HandlePointers FindHandles(cgameImport_t id, void* tuple) {
	auto ilocs = ShaderLocs().find(id);
	HandlePointers hp;
	if (ilocs != ShaderLocs().end()) {
		for (HandleLoc hl : ilocs->second) {
			int* handle = reinterpret_cast<int*>(reinterpret_cast<char*>(tuple) + hl.tupleOffset);
			hp.shaders.push_back(handle);
		}
	}
	return hp;
}

