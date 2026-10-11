#pragma once

#include "GrassRuntime.h"
#include <memory>

/** @brief Batches visible grass and retains unchanged GPU instance records across frames. */
class GrassBucketRenderer
{
public:
	GrassBucketRenderer();
	~GrassBucketRenderer();
	void InstallHooks();
	void SetupResources();
	void ClearShaderCache();
	/** @brief Prepare and reuse frame culling, restoring native geometry constants before drawing. */
	void PrepareGeometry(RE::BSRenderPass*, RE::BSShader*, uint32_t, GrassRuntime::SetupGeometry);
	bool IsHookInstalled() const;
	/** @brief Whether native integration is installed without a latched rendering failure. */
	bool IsRenderingAvailable() const;
	void RecordModel(RE::BSMultiStreamInstanceTriShape* shape, const char* path);
	void MarkGroupsChanged(RE::BSMultiStreamInstanceTriShape* shape);
	void MarkGenerated(RE::BSMultiStreamInstanceTriShape* shape);
	void RemoveShape(RE::BSMultiStreamInstanceTriShape* shape);
	/** @brief Snapshot generated grass without changing visibility; generation hooks invalidate retained records. */
	bool CaptureVisible(RE::BSMultiStreamInstanceTriShape* shape, bool nativeVisible);
	/** @brief Queue one compatible representative, retaining native fallback for every member. */
	bool QueueVisible(RE::BSMultiStreamInstanceTriShape*, RE::NiCullingProcess*, int32_t);
	using RenderPass = void (*)(RE::BSRenderPass*, uint32_t, bool, uint32_t);
	/** @brief Expand a failed representative draw through the original native pass route. */
	void DrawRenderPass(RE::BSRenderPass*, uint32_t, bool, uint32_t, RenderPass);
#ifdef DEVBENCH_BRIDGE_ENABLED
	void SetDiagnosticsEnabled(bool enabled);
	json GetDiagnostics() const;
#endif
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
	using NativeDraw = void (*)(RE::BSGraphics::Renderer*, RE::BSGraphics::TriShape*, uint32_t, uint32_t, uint32_t,
		RE::BSGraphics::VertexDesc, RE::BSGraphics::VertexBuffer*);
	static void DrawGroup(RE::BSGraphics::Renderer*, RE::BSGraphics::TriShape*, uint32_t, uint32_t, uint32_t,
		RE::BSGraphics::VertexDesc, RE::BSGraphics::VertexBuffer*);
	static inline REL::Relocation<NativeDraw> originalDraw;
};
