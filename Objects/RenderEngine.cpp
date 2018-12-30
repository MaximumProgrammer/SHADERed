#include "RenderEngine.h"
#include "DefaultState.h"
#include "PipelineManager.h"
#include "SystemVariableManager.h"
#include <MoonLight/Base/PixelShader.h>
#include <MoonLight/Base/ConstantBuffer.h>

namespace ed
{
	RenderEngine::RenderEngine(ml::Window* wnd, PipelineManager * pipeline, ProjectParser* project, MessageStack* msgs) :
		m_pipeline(pipeline),
		m_project(project),
		m_msgs(msgs),
		m_wnd(wnd),
		m_lastSize(0, 0)
	{}
	RenderEngine::~RenderEngine()
	{
		FlushCache();
	}
	void RenderEngine::Render(int width, int height)
	{
		// recreate render texture if size is changed
		if (m_lastSize.x != width || m_lastSize.y != height) {
			m_lastSize = DirectX::XMINT2(width, height);
			m_rt.Create(*m_wnd, m_lastSize, ml::Resource::ShaderResource, true);

			m_rtView.Create(*m_wnd, m_rt);
		}

		// update system values
		SystemVariableManager::Instance().SetViewportSize(width, height);

		// cache elements
		m_cache();

		// bind and reset render texture
		m_rt.Bind();
		m_rt.Clear();
		m_rt.ClearDepthStencil(1.0f, 0);

		// set viewport and cache old viewport
		D3D11_VIEWPORT viewport;
		viewport.TopLeftX = viewport.TopLeftY = 0;
		viewport.Height = height;
		viewport.Width = width;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		m_wnd->GetDeviceContext()->RSSetViewports(1, &viewport);

		for (int i = 0; i < m_items.size(); i++) {
			auto it = m_items[i];
			pipe::ShaderPass* data = (pipe::ShaderPass*)it->Data;

			// bind shaders and their variables
			m_wnd->SetInputLayout(data->VSInputLayout);

			for (int j = 0; j < CONSTANT_BUFFER_SLOTS; j++) {
				if (data->VSVariables.IsSlotUsed(j))
					data->VSVariables.GetSlot(j).BindVS(j);
				if (data->PSVariables.IsSlotUsed(j))
					data->PSVariables.GetSlot(j).BindPS(j);
			}

			m_vs[i]->Bind();
			m_ps[i]->Bind();

			// bind default states for each shader pass
			DefaultState::Instance().Bind();

			// render pipeline items
			for (int j = 0; j < data->Items.size(); j++) {
				auto item = data->Items[j];

				if (item->Type == PipelineItem::ItemType::Geometry) {
					ed::pipe::GeometryItem* geoData = reinterpret_cast<ed::pipe::GeometryItem*>(item->Data);

					SystemVariableManager::Instance().SetGeometryTransform(*geoData);
					data->VSVariables.UpdateBuffers(m_wnd);
					data->PSVariables.UpdateBuffers(m_wnd);

					m_wnd->SetTopology(geoData->Topology);
					geoData->Geometry.Draw();
				}
				else if (item->Type == PipelineItem::ItemType::BlendState) {
					ed::pipe::BlendState* blend = reinterpret_cast<ed::pipe::BlendState*>(item->Data);
					blend->State.Bind();
				}
				else if (item->Type == PipelineItem::ItemType::DepthStencilState) {
					ed::pipe::DepthStencilState* state = reinterpret_cast<ed::pipe::DepthStencilState*>(item->Data);
					state->State.Bind(state->StencilReference);
				}
			}
		}
		
		// restore real render target view
		m_wnd->Bind();
	}
	void RenderEngine::Recompile(const char * name)
	{
		int d3dCounter = 0;
		for (int i = 0; i < m_items.size(); i++) {
			auto item = m_items[i];
			if (strcmp(item->Name, name) == 0) {
				ed::pipe::ShaderPass* shader = (ed::pipe::ShaderPass*)item->Data;

				// bind input layout if needed 
				if (shader->VSInputLayout.GetInputElements().size() > 0) {
					m_vs[i]->InputSignature = &shader->VSInputLayout;
					m_vs[i]->InputSignature->Reset();
				} 
				else
					m_vs[i]->InputSignature = nullptr;

				std::string vsContent = m_project->LoadProjectFile(shader->VSPath);
				std::string psContent = m_project->LoadProjectFile(shader->PSPath);
				bool vsCompiled = m_vs[i]->LoadFromMemory(*m_wnd, vsContent.c_str(), vsContent.size(), shader->VSEntry);
				bool psCompiled = m_ps[i]->LoadFromMemory(*m_wnd, psContent.c_str(), psContent.size(), shader->PSEntry);

				if (!vsCompiled || !psCompiled)
					m_msgs->Add(MessageStack::Type::Error, item->Name, "Failed to compile the shader(s)");
				else
					m_msgs->ClearGroup(item->Name);
			}
		}
	}
	void RenderEngine::FlushCache()
	{
		for (int i = 0; i < m_vs.size(); i++)
			delete m_vs[i];
		for (int i = 0; i < m_ps.size(); i++)
			delete m_ps[i];
		m_vs.clear();
		m_ps.clear();
		m_items.clear();
	}
	void RenderEngine::m_cache()
	{
		// check for any changes
		std::vector<ed::PipelineItem*>& items = m_pipeline->GetList();

		// check if no major changes were made, if so dont cache for another 0.25s
		if (m_items.size() == items.size()) {
			if (m_cacheTimer.GetElapsedTime() > 0.5f)
				m_cacheTimer.Restart();
			else return;
		}

		// check if some item was added
		for (int i = 0; i < items.size(); i++) {
			bool found = false;
			for (int j = 0; j < m_items.size(); j++)
				if (items[i]->Data == m_items[j]->Data) {
					found = true;
					break;
				}

			if (!found) {
				m_items.insert(m_items.begin() + i, items[i]);

				/*
					ITEM CACHING
				*/

				ed::pipe::ShaderPass* data = reinterpret_cast<ed::pipe::ShaderPass*>(items[i]->Data);

				ml::VertexShader* vShader = new ml::VertexShader();
				ml::PixelShader* pShader = new ml::PixelShader();

				// bind the input layout
				data->VSInputLayout.Reset();
				if (data->VSInputLayout.GetInputElements().size() > 0)
					vShader->InputSignature = &data->VSInputLayout;
				else
					vShader->InputSignature = nullptr;

				std::string vsContent = m_project->LoadProjectFile(data->VSPath);
				std::string psContent = m_project->LoadProjectFile(data->PSPath);
				bool vsCompiled = vShader->LoadFromMemory(*m_wnd, vsContent.c_str(), vsContent.size(), data->VSEntry);
				bool psCompiled = pShader->LoadFromMemory(*m_wnd, psContent.c_str(), psContent.size(), data->PSEntry);

				if (!vsCompiled || !psCompiled)
					m_msgs->Add(MessageStack::Type::Error, items[i]->Name, "Failed to compile the shader");
				else
					m_msgs->ClearGroup(items[i]->Name);

				m_vs.insert(m_vs.begin() + i, vShader);
				m_ps.insert(m_ps.begin() + i, pShader);
			}
		}

		// check if some item was removed
		for (int i = 0; i < m_items.size(); i++) {
			bool found = false;
			for (int j = 0; j < items.size(); j++)
				if (items[j]->Data == m_items[i]->Data) {
					found = true;
					break;
				}

			if (!found) {
				delete m_vs[i];
				delete m_ps[i];
				m_vs.erase(m_vs.begin() + i);
				m_ps.erase(m_ps.begin() + i);

				m_items.erase(m_items.begin() + i);
			}
		}

		// check if the order of the items changed
		for (int i = 0; i < m_items.size(); i++) {
			// two items at the same position dont match
			if (items[i]->Data != m_items[i]->Data) {
				// find the real position from original list
				for (int j = 0; j < items.size(); j++) {
					// we found the original position so move the item
					if (items[j]->Data == m_items[i]->Data) {
						int dest = j > i ? (j - 1) : j;
						m_items.erase(m_items.begin() + i, m_items.begin() + i + 1);
						m_items.insert(m_items.begin() + dest, items[j]);

						ml::VertexShader* vsCopy = m_vs[i];
						ml::PixelShader* psCopy = m_ps[i];

						m_vs.erase(m_vs.begin() + i);
						m_vs.insert(m_vs.begin() + dest, vsCopy);

						m_ps.erase(m_ps.begin() + i);
						m_ps.insert(m_ps.begin() + dest, psCopy);
					}
				}
			}
		}
	}
}