#include <imgui/imgui.h>

#include "animation/animation.h"
#include "animation/animation_module.h"
#include "animation/controller.h"
#include "animation/property_animation.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/editor_asset.h"
#include "editor/property_grid.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/hash_map.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "controller_editor.h"
#include "engine/reflection.h"
#include "engine/world.h"
#include "renderer/model.h"
#include "renderer/pose.h"
#include "renderer/model.h"
#include "renderer/pipeline.h"
#include "renderer/render_module.h"
#include "renderer/renderer.h"
#include "renderer/editor/world_viewer.h"


using namespace Lumix;


static const ComponentType ANIMABLE_TYPE = reflection::getComponentType("animable");
static const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
static const ComponentType ENVIRONMENT_PROBE_TYPE = reflection::getComponentType("environment_probe");
static const ComponentType ENVIRONMENT_TYPE = reflection::getComponentType("environment");

namespace {
	
struct AnimationAssetBrowserPlugin : AssetBrowser::IPlugin {
	struct EditorWindow : AssetEditorWindow {
		EditorWindow(const Path& path, StudioApp& app)
			: AssetEditorWindow(app)
			, m_app(app)
			, m_viewer(app)
		{
			m_resource = app.getEngine().getResourceManager().load<Animation>(path);

			Engine& engine = m_app.getEngine();

			m_model = m_app.getEngine().getResourceManager().load<Model>(Path(Path::getResource(path)));
			auto* render_module = static_cast<RenderModule*>(m_viewer.m_world->getModule(MODEL_INSTANCE_TYPE));
			render_module->setModelInstancePath((EntityRef)m_viewer.m_mesh, m_model->getPath());

			m_viewer.m_world->createComponent(ANIMABLE_TYPE, *m_viewer.m_mesh);

			auto* anim_module = static_cast<AnimationModule*>(m_viewer.m_world->getModule(ANIMABLE_TYPE));
			anim_module->setAnimation(*m_viewer.m_mesh, path);
		}

		~EditorWindow() {
			m_resource->decRefCount();
			if (m_model) m_model->decRefCount();
		}

		void windowGUI() override {
			if (ImGui::BeginMenuBar()) {
				if (ImGuiEx::IconButton(ICON_FA_EXTERNAL_LINK_ALT, "Go to parent")) {
					m_app.getAssetBrowser().openEditor(Path(Path::getResource(m_resource->getPath())));
				}
				ImGui::EndMenuBar();
			}

			if (m_resource->isEmpty()) {
				ImGui::TextUnformatted("Loading...");
				return;
			}

			if (!m_resource->isReady()) return;

			Path model_path = m_model ? m_model->getPath() : Path();
			if (m_app.getAssetBrowser().resourceInput("Model", model_path, ResourceType("model"))) {
				if (m_model) m_model->decRefCount();
				m_model = m_app.getEngine().getResourceManager().load<Model>(model_path);
				auto* render_module = static_cast<RenderModule*>(m_viewer.m_world->getModule(MODEL_INSTANCE_TYPE));
				render_module->setModelInstancePath(*m_viewer.m_mesh, m_model ? m_model->getPath() : Path());
			}

			if (!m_model || !m_model->isReady()) return;

			if (!ImGui::BeginTable("tab", 2, ImGuiTableFlags_Resizable)) return;
			ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 250);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			const Array<Animation::RotationCurve>& rotations = m_resource->getRotations();
			const Array<Animation::TranslationCurve>& translations = m_resource->getTranslations();

			if (!translations.empty() && ImGui::TreeNode("Translations")) {
				for (const Animation::TranslationCurve& curve : translations) {
					auto iter = m_model->getBoneIndex(curve.name);
					if (!iter.isValid()) continue;

					const Model::Bone& bone = m_model->getBone(iter.value());
					ImGuiTreeNodeFlags flags = m_selected_bone == curve.name ? ImGuiTreeNodeFlags_Selected : 0;
					flags |= ImGuiTreeNodeFlags_OpenOnArrow;
					bool open = ImGui::TreeNodeEx(bone.name.c_str(), flags);
					if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
						m_selected_bone = curve.name;
					}
					if (open) {
						ImGui::Columns(4);
						for (u32 i = 0; i < curve.count; ++i) {
							const Vec3 p = curve.pos[i];
							if (curve.times) {
								const float t = curve.times[i] / float(0xffff) * m_resource->getLength().seconds();
								ImGui::Text("%.2f s", t);
							}
							else {
								ImGui::Text("frame %d", i);
							}
							ImGui::NextColumn();
							ImGui::Text("%f", p.x);
							ImGui::NextColumn();
							ImGui::Text("%f", p.y);
							ImGui::NextColumn();
							ImGui::Text("%f", p.z);
							ImGui::NextColumn();

						}
						ImGui::Columns();
						ImGui::TreePop();
					}
				}
				ImGui::TreePop();
			}

			if (!rotations.empty() && ImGui::TreeNode("Rotations")) {
				for (const Animation::RotationCurve& curve : rotations) {
					auto iter = m_model->getBoneIndex(curve.name);
					if (!iter.isValid()) continue;

					const Model::Bone& bone = m_model->getBone(iter.value());
					ImGuiTreeNodeFlags flags = m_selected_bone == curve.name ? ImGuiTreeNodeFlags_Selected : 0;
					flags |= ImGuiTreeNodeFlags_OpenOnArrow;

					bool open = ImGui::TreeNodeEx(bone.name.c_str(), flags);
					if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
						m_selected_bone = curve.name;
					}
					if (open) {
						ImGui::Columns(4);
						for (u32 i = 0; i < curve.count; ++i) {
							const Vec3 r = radiansToDegrees(curve.rot[i].toEuler());
							if (curve.times) {
								const float t = curve.times[i] / float(0xffff) * m_resource->getLength().seconds();
								ImGui::Text("%.2f s", t);
							}
							else {
								ImGui::Text("frame %d", i);
							}
							ImGui::NextColumn();
							ImGui::Text("%f", r.x);
							ImGui::NextColumn();
							ImGui::Text("%f", r.y);
							ImGui::NextColumn();
							ImGui::Text("%f", r.z);
							ImGui::NextColumn();
						}
						ImGui::Columns();
						ImGui::TreePop();
					}
				}
				ImGui::TreePop();
			}

			ImGui::TableNextColumn();
			previewGUI();

			ImGui::EndTable();
		}
		
		void previewGUI() {
			ASSERT(m_model->isReady());

			if (m_play) {
				if (ImGuiEx::IconButton(ICON_FA_PAUSE, "Pause")) m_play = false;
			}
			else {
				if (ImGuiEx::IconButton(ICON_FA_PLAY, "Play")) m_play = true;
			}
			auto* anim_module = static_cast<AnimationModule*>(m_viewer.m_world->getModule(ANIMABLE_TYPE));
			Animable& animable = anim_module->getAnimable(*m_viewer.m_mesh);
			float t = animable.time.seconds();
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1);
			if (ImGui::SliderFloat("##time", &t, 0, m_resource->getLength().seconds())) {
				animable.time = Time::fromSeconds(t);
				anim_module->updateAnimable(*m_viewer.m_mesh, 0);
			}

			auto* render_module = static_cast<RenderModule*>(m_viewer.m_world->getModule(MODEL_INSTANCE_TYPE));
			bool show_mesh = render_module->isModelInstanceEnabled(*m_viewer.m_mesh);
			ImGuiEx::Label("Show mesh");
			if (ImGui::Checkbox("##sm", &show_mesh)) {
				render_module->enableModelInstance(*m_viewer.m_mesh, show_mesh);
			}
			
			ImGuiEx::Label("Show skeleton");
			ImGui::Checkbox("##ss", &m_show_skeleton);
			if (m_show_skeleton) m_viewer.drawSkeleton(m_selected_bone);
			
			ImGuiEx::Label("Playback speed");
			ImGui::DragFloat("##spd", &m_playback_speed, 0.01f, 0, FLT_MAX);

			if (m_play && m_playback_speed > 0) {
				anim_module->updateAnimable(*m_viewer.m_mesh, m_app.getEngine().getLastTimeDelta() * m_playback_speed);
			}
			else {
				if (ImGui::Button("Step")) {
					anim_module->updateAnimable(*m_viewer.m_mesh, 1 / 30.f);
				}
			}

			if (!m_init) {
				m_viewer.resetCamera(*m_model);
				m_init = true;
			}

			m_viewer.gui();
		}

		const Path& getPath() override { return m_resource->getPath(); }
		const char* getName() const override { return "animation editor"; }

		StudioApp& m_app;
		Animation* m_resource;
		Model* m_model = nullptr;
		bool m_init = false;
		bool m_show_skeleton = true;
		bool m_play = true;
		float m_playback_speed = 1.f;
		WorldViewer m_viewer;
		BoneNameHash m_selected_bone;
	};
	
	explicit AnimationAssetBrowserPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("ani", Animation::TYPE);
	}

	const char* getLabel() const override { return "Animation"; }

	void openEditor(const Path& path) override {
		IAllocator& allocator = m_app.getAllocator();
		UniquePtr<EditorWindow> win = UniquePtr<EditorWindow>::create(allocator, path, m_app);
		m_app.getAssetBrowser().addWindow(win.move());
	}

	StudioApp& m_app;
	Animation* m_resource;
};


struct PropertyAnimationPlugin : AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	struct EditorWindow : AssetEditorWindow {
		EditorWindow(const Path& path, StudioApp& app)
			: AssetEditorWindow(app)
			, m_app(app)
		{
			m_resource = app.getEngine().getResourceManager().load<PropertyAnimation>(path);
		}

		~EditorWindow() {
			m_resource->decRefCount();
		}

		void save() {
			OutputMemoryStream blob(m_app.getAllocator());
			m_resource->serialize(blob);
			m_app.getAssetBrowser().saveResource(*m_resource, blob);
			m_dirty = false;
		}
		
		bool onAction(const Action& action) override { 
			if (&action == &m_app.getCommonActions().save) save();
			else return false;
			return true;
		}

		void windowGUI() override {
			if (ImGui::BeginMenuBar()) {
				if (ImGuiEx::IconButton(ICON_FA_SAVE, "Save")) save();
				if (ImGuiEx::IconButton(ICON_FA_EXTERNAL_LINK_ALT, "Open externally")) m_app.getAssetBrowser().openInExternalEditor(m_resource);
				if (ImGuiEx::IconButton(ICON_FA_SEARCH, "View in browser")) m_app.getAssetBrowser().locate(*m_resource);
				ImGui::EndMenuBar();
			}

			if (m_resource->isEmpty()) {
				ImGui::TextUnformatted("Loading...");
				return;
			}

			if (!m_resource->isReady()) return;

			ShowAddCurveMenu(m_resource);

			bool changed = false;
			if (!m_resource->curves.empty()) {
				int frames = m_resource->curves[0].frames.back();
				ImGuiEx::Label("Frames");
				if (ImGui::InputInt("##frames", &frames)) {
					for (auto& curve : m_resource->curves) {
						curve.frames.back() = frames;
						changed = true;
					}
				}
			}

			for (int i = 0, n = m_resource->curves.size(); i < n; ++i) {
				PropertyAnimation::Curve& curve = m_resource->curves[i];
				const char* cmp_name = m_app.getComponentTypeName(curve.cmp_type);
				StaticString<64> tmp(cmp_name, " - ", curve.property->name);
				if (ImGui::Selectable(tmp, m_selected_curve == i)) m_selected_curve = i;
			}

			if (m_selected_curve >= m_resource->curves.size()) m_selected_curve = -1;
			if (m_selected_curve < 0) return;

			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 20);
			static ImVec2 size(-1, 200);
			
			PropertyAnimation::Curve& curve = m_resource->curves[m_selected_curve];
			ImVec2 points[16];
			ASSERT((u32)curve.frames.size() < lengthOf(points));
			for (int i = 0; i < curve.frames.size(); ++i)
			{
				points[i].x = (float)curve.frames[i];
				points[i].y = curve.values[i];
			}
			int new_count;
			int last_frame = curve.frames.back();
			int flags = (int)ImGuiEx::CurveEditorFlags::NO_TANGENTS | (int)ImGuiEx::CurveEditorFlags::SHOW_GRID;
			if (m_fit_curve_in_editor)
			{
				flags |= (int)ImGuiEx::CurveEditorFlags::RESET;
				m_fit_curve_in_editor = false;
			}
			int changed_idx = ImGuiEx::CurveEditor("curve", (float*)points, curve.frames.size(), lengthOf(points), size, flags, &new_count, &m_selected_point);
			if (changed_idx >= 0)
			{
				curve.frames[changed_idx] = int(points[changed_idx].x + 0.5f);
				curve.values[changed_idx] = points[changed_idx].y;
				curve.frames.back() = last_frame;
				curve.frames[0] = 0;
			}
			if (new_count != curve.frames.size())
			{
				curve.frames.resize(new_count);
				curve.values.resize(new_count);
				for (int i = 0; i < new_count; ++i)
				{
					curve.frames[i] = int(points[i].x + 0.5f);
					curve.values[i] = points[i].y;
				}
			}

			ImGui::PopItemWidth();

			if (ImGui::BeginPopupContextItem("curve"))
			{
				if (ImGui::Selectable("Fit data")) m_fit_curve_in_editor = true;

				ImGui::EndPopup();
			}

			if (m_selected_point >= 0 && m_selected_point < curve.frames.size())
			{
				ImGuiEx::Label("Frame");
				changed = ImGui::InputInt("##frame", &curve.frames[m_selected_point]) || changed;
				ImGuiEx::Label("Value");
				changed = ImGui::InputFloat("##val", &curve.values[m_selected_point]) || changed;
			}

			ImGuiEx::HSplitter("sizer", &size);
		}

		void ShowAddCurveMenu(PropertyAnimation* animation) {
			if (!ImGui::BeginMenu("Add curve")) return;
		
			for (const reflection::RegisteredComponent& cmp_type : reflection::getComponents()) {
				const char* cmp_type_name = cmp_type.cmp->name;
				if (!hasFloatProperty(cmp_type.cmp)) continue;
				if (!ImGui::BeginMenu(cmp_type_name)) continue;

				const reflection::ComponentBase* component = cmp_type.cmp;
				struct : reflection::IEmptyPropertyVisitor
				{
					void visit(const reflection::Property<float>& prop) override
					{
						int idx = animation->curves.find([&](PropertyAnimation::Curve& rhs) {
							return rhs.cmp_type == cmp_type && rhs.property == &prop;
						});
						if (idx < 0 &&ImGui::MenuItem(prop.name))
						{
							PropertyAnimation::Curve& curve = animation->addCurve();
							curve.cmp_type = cmp_type;
							curve.property = &prop;
							curve.frames.push(0);
							curve.frames.push(animation->curves.size() > 1 ? animation->curves[0].frames.back() : 1);
							curve.values.push(0);
							curve.values.push(0);
						}
					}
					PropertyAnimation* animation;
					ComponentType cmp_type;
				} visitor;

				visitor.animation = animation;
				visitor.cmp_type = cmp_type.cmp->component_type;
				component->visit(visitor);

				ImGui::EndMenu();
			}

			ImGui::EndMenu();
		}
		static bool hasFloatProperty(const reflection::ComponentBase* cmp) {
			struct : reflection::IEmptyPropertyVisitor {
				void visit(const reflection::Property<float>& prop) override { result = true; }
				bool result = false;
			} visitor;
			cmp->visit(visitor);
			return visitor.result;
		}

		const Path& getPath() override { return m_resource->getPath(); }
		const char* getName() const override { return "property animation editor"; }

		StudioApp& m_app;
		PropertyAnimation* m_resource;
		int m_selected_point = -1;
		int m_selected_curve = -1;
		bool m_fit_curve_in_editor = false;
	};

	explicit PropertyAnimationPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("anp", PropertyAnimation::TYPE);
	}

	bool canCreateResource() const override { return true; }
	const char* getDefaultExtension() const override { return "anp"; }
	void createResource(OutputMemoryStream& blob) override {}
	bool compile(const Path& src) override { return m_app.getAssetCompiler().copyCompile(src); }
	const char* getLabel() const override { return "Property animation"; }
	
	void openEditor(const Path& path) override {
		IAllocator& allocator = m_app.getAllocator();
		UniquePtr<EditorWindow> win = UniquePtr<EditorWindow>::create(allocator, path, m_app);
		m_app.getAssetBrowser().addWindow(win.move());
	}

	StudioApp& m_app;
};

struct AnimablePropertyGridPlugin final : PropertyGrid::IPlugin {
	explicit AnimablePropertyGridPlugin(StudioApp& app)
		: m_app(app)
	{
		m_is_playing = false;
	}


	void onGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, WorldEditor& editor) override
	{
		if (cmp_type != ANIMABLE_TYPE) return;
		if (entities.length() != 1) return;

		const EntityRef entity = entities[0];
		auto* module = (AnimationModule*)editor.getWorld()->getModule(cmp_type);
		auto* animation = module->getAnimableAnimation(entity);
		if (!animation) return;
		if (!animation->isReady()) return;

		ImGui::Checkbox("Preview", &m_is_playing);
		float time = module->getAnimable(entity).time.seconds();
		if (ImGui::SliderFloat("Time", &time, 0, animation->getLength().seconds()))
		{
			module->getAnimable(entity).time = Time::fromSeconds(time);
			module->updateAnimable(entity, 0);
		}

		if (m_is_playing)
		{
			float time_delta = m_app.getEngine().getLastTimeDelta();
			module->updateAnimable(entity, time_delta);
		}

		if (ImGui::CollapsingHeader("Transformation"))
		{
			auto* render_module = (RenderModule*)module->getWorld().getModule(MODEL_INSTANCE_TYPE);
			if (module->getWorld().hasComponent(entity, MODEL_INSTANCE_TYPE))
			{
				const Pose* pose = render_module->lockPose(entity);
				Model* model = render_module->getModelInstanceModel(entity);
				if (pose && model)
				{
					ImGui::Columns(3);
					for (u32 i = 0; i < pose->count; ++i)
					{
						ImGuiEx::TextUnformatted(model->getBone(i).name);
						ImGui::NextColumn();
						ImGui::Text("%f; %f; %f", pose->positions[i].x, pose->positions[i].y, pose->positions[i].z);
						ImGui::NextColumn();
						ImGui::Text("%f; %f; %f; %f", pose->rotations[i].x, pose->rotations[i].y, pose->rotations[i].z, pose->rotations[i].w);
						ImGui::NextColumn();
					}
					ImGui::Columns();
				}
				if (pose) render_module->unlockPose(entity, false);
			}
		}
	}


	StudioApp& m_app;
	bool m_is_playing;
};


struct StudioAppPlugin : StudioApp::IPlugin {
	explicit StudioAppPlugin(StudioApp& app)
		: m_app(app)
		, m_animable_plugin(app)
		, m_animation_plugin(app)
		, m_prop_anim_plugin(app)
	{}

	const char* getName() const override { return "animation"; }

	void init() override {
		PROFILE_FUNCTION();
		AssetCompiler& compiler = m_app.getAssetCompiler();
		const char* anp_exts[] = { "anp" };
		const char* ani_exts[] = { "ani" };
		compiler.addPlugin(m_prop_anim_plugin, Span(anp_exts));

		AssetBrowser& asset_browser = m_app.getAssetBrowser();
		asset_browser.addPlugin(m_animation_plugin, Span(ani_exts));
		asset_browser.addPlugin(m_prop_anim_plugin, Span(anp_exts));

		m_app.getPropertyGrid().addPlugin(m_animable_plugin);

		m_anim_editor = anim::ControllerEditor::create(m_app);
	}

	bool showGizmo(WorldView&, ComponentUID) override { return false; }
	
	~StudioAppPlugin() {
		AssetCompiler& compiler = m_app.getAssetCompiler();
		compiler.removePlugin(m_prop_anim_plugin);

		AssetBrowser& asset_browser = m_app.getAssetBrowser();
		asset_browser.removePlugin(m_animation_plugin);
		asset_browser.removePlugin(m_prop_anim_plugin);
		m_app.getPropertyGrid().removePlugin(m_animable_plugin);
	}


	StudioApp& m_app;
	AnimablePropertyGridPlugin m_animable_plugin;
	AnimationAssetBrowserPlugin m_animation_plugin;
	PropertyAnimationPlugin m_prop_anim_plugin;
	UniquePtr<anim::ControllerEditor> m_anim_editor;
};


} // anonymous namespace


LUMIX_STUDIO_ENTRY(animation) {
	PROFILE_FUNCTION();
	IAllocator& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}

