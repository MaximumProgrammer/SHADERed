#pragma once
#include <string>
#include "../GUIManager.h"
#include "ShaderVariable.h"
#include "../pugixml/pugixml.hpp"

namespace ed
{
	class PipelineManager;

	class ProjectParser
	{
	public:
		ProjectParser(PipelineManager* pipeline, GUIManager* gui);
		~ProjectParser();

		void Open(const std::string& file);
		void OpenTemplate();

		void Save();
		void SaveAs(const std::string& file);

		std::string LoadProjectFile(const std::string& file);
		void SaveProjectFile(const std::string& file, const std::string& data);

		std::string GetRelativePath(const std::string& to);

		void ResetProjectDirectory();
		inline void SetProjectDirectory(const std::string& path) { m_projectPath = path; }
		inline std::string GetProjectDirectory() { return m_projectPath; }

		inline std::string GetOpenedFile() { return m_file; }

	private:
		void m_exportShaderVariables(pugi::xml_node& node, std::vector<ShaderVariable*>& vars);
		D3D11_BLEND m_toBlend(const char* str);
		D3D11_BLEND_OP m_toBlendOp(const char* str);
		D3D11_COMPARISON_FUNC m_toComparisonFunc(const char* str);
		D3D11_STENCIL_OP m_toStencilOp(const char* str);

		GUIManager* m_ui;
		PipelineManager* m_pipe;
		std::string m_file;
		std::string m_projectPath;
	};
}