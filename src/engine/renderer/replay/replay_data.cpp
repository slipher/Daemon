#include "common/Common.h"

#include "../../client/cg_msgdef.h"
#include "../tr_local.h"
#include "./replay_data.h"



void SaveReplayData(const RenderRecordData& data, Util::Writer& writer) {
	//std::set<std::string> pakPaths;
	{
		const model_t* defaultModel = R_GetModelByHandle(0);
		std::vector<std::pair<int, std::string>> modelNamesByHandle;
		for (int h : data.modelHandles) {
			const model_t* model = R_GetModelByHandle(h);
			if (model == defaultModel) {
				continue;
			}
			switch (model->type) {
			case modtype_t::MOD_IQM:
			case modtype_t::MOD_MD5:
			case modtype_t::MOD_MESH:
				break;
			default:
				Sys::Drop("SerializeReplayData: unsupported model type %d", Util::ordinal(model->type));
			}
			modelNamesByHandle.emplace_back(h, model->name);
		}
		writer.Write<decltype(modelNamesByHandle)>(modelNamesByHandle);
	}
	{
		const shader_t* defaultShader = R_GetShaderByHandle(0);
		std::vector<std::pair<int, std::string>> shaderPathsByHandle;
		for (int h : data.shaderHandles) {
			const shader_t* shader = R_GetShaderByHandle(h);
			if (shader == defaultShader) {
				continue;
			}
			shaderPathsByHandle.emplace_back(h, shader->name);
		}
		writer.Write<decltype(shaderPathsByHandle)>(shaderPathsByHandle);
	}
}

