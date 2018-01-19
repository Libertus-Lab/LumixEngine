#include "gui_scene.h"
#include "gui_system.h"
#include "sprite_manager.h"
#include "engine/engine.h"
#include "engine/flag_set.h"
#include "engine/iallocator.h"
#include "engine/input_system.h"
#include "engine/plugin_manager.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "engine/serializer.h"
#include "engine/universe/universe.h"
#include "renderer/draw2d.h"
#include "renderer/font_manager.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/render_scene.h"
#include "renderer/texture.h"


namespace Lumix
{


static const ComponentType GUI_BUTTON_TYPE = Reflection::getComponentType("gui_button");
static const ComponentType GUI_RECT_TYPE = Reflection::getComponentType("gui_rect");
static const ComponentType GUI_IMAGE_TYPE = Reflection::getComponentType("gui_image");
static const ComponentType GUI_TEXT_TYPE = Reflection::getComponentType("gui_text");


struct GUIText
{
	GUIText(IAllocator& allocator) : text("", allocator) {}
	~GUIText()
	{
		if (font_resource)
		{
			font_resource->removeRef(*font);
			font_resource->getResourceManager().unload(*font_resource);
		}
	}

	FontResource* font_resource = nullptr;
	Font* font = nullptr;
	string text;
	int font_size = 13;
	u32 color = 0xff000000;
};


struct GUIButton
{
	u32 normal_color;
	u32 hovered_color;
};


struct GUIImage
{
	Sprite* sprite = nullptr;
	u32 color = 0xffffFFFF;
};


struct GUIRect
{
	enum Flags
	{
		IS_VALID = 1 << 0,
		IS_ENABLED = 1 << 1
	};

	struct Anchor
	{
		float points = 0;
		float relative = 0;
	};

	Entity entity;
	FlagSet<Flags, u32> flags;
	Anchor top;
	Anchor right = { 0, 1 };
	Anchor bottom = { 0, 1 };
	Anchor left;

	GUIImage* image = nullptr;
	GUIText* text = nullptr;
};


struct GUISceneImpl LUMIX_FINAL : public GUIScene
{
	GUISceneImpl(GUISystem& system, Universe& context, IAllocator& allocator)
		: m_allocator(allocator)
		, m_universe(context)
		, m_system(system)
		, m_rects(allocator)
		, m_buttons(allocator)
		, m_rect_hovered(allocator)
		, m_rect_hovered_out(allocator)
		, m_button_clicked(allocator)
	{
		context.registerComponentType(GUI_RECT_TYPE
			, this
			, &GUISceneImpl::createRect
			, &GUISceneImpl::destroyRect
			, &GUISceneImpl::serializeRect
			, &GUISceneImpl::deserializeRect);
		context.registerComponentType(GUI_IMAGE_TYPE
			, this
			, &GUISceneImpl::createImage
			, &GUISceneImpl::destroyImage
			, &GUISceneImpl::serializeImage
			, &GUISceneImpl::deserializeImage);
		context.registerComponentType(GUI_TEXT_TYPE
			, this
			, &GUISceneImpl::createText
			, &GUISceneImpl::destroyText
			, &GUISceneImpl::serializeText
			, &GUISceneImpl::deserializeText);
		context.registerComponentType(GUI_BUTTON_TYPE
			, this
			, &GUISceneImpl::createButton
			, &GUISceneImpl::destroyButton
			, &GUISceneImpl::serializeButton
			, &GUISceneImpl::deserializeButton);
		m_font_manager = (FontManager*)system.getEngine().getResourceManager().get(FontResource::TYPE);
	}


	void renderRect(GUIRect& rect, Pipeline& pipeline, const Rect& parent_rect)
	{
		if (!rect.flags.isSet(GUIRect::IS_VALID)) return;
		if (!rect.flags.isSet(GUIRect::IS_ENABLED)) return;
		
		float l = parent_rect.x + rect.left.points + parent_rect.w * rect.left.relative;
		float r = parent_rect.x + rect.right.points + parent_rect.w * rect.right.relative;
		float t = parent_rect.y + rect.top.points + parent_rect.h * rect.top.relative;
		float b = parent_rect.y + rect.bottom.points + parent_rect.h * rect.bottom.relative;
			 
		Draw2D& draw = pipeline.getDraw2D();
		if (rect.image)
		{
			if (rect.image->sprite && rect.image->sprite->getTexture())
			{
				Sprite* sprite = rect.image->sprite;
				Texture* tex = sprite->getTexture();
				if (sprite->type == Sprite::PATCH9)
				{
					struct Quad {
						float l, t, r, b;
					} pos = {
						l + sprite->left,
						t + sprite->top,
						r - tex->width + sprite->right,
						b - tex->height + sprite->bottom
					};
					Quad uvs = {
						sprite->left / (float)tex->width,
						sprite->top / (float)tex->height,
						sprite->right / (float)tex->width,
						sprite->bottom / (float)tex->height
					};

					draw.AddImage(&tex->handle, { l, t }, { pos.l, pos.t }, { 0, 0 }, { uvs.l, uvs.t });
					draw.AddImage(&tex->handle, { pos.l, t }, { pos.r, pos.t }, { uvs.l, 0 }, { uvs.r, uvs.t });
					draw.AddImage(&tex->handle, { pos.r, t }, { r, pos.t }, { uvs.r, 0 }, { 1, uvs.t });

					draw.AddImage(&tex->handle, { l, pos.t }, { pos.l, pos.b }, { 0, uvs.t }, { uvs.l, uvs.b });
					draw.AddImage(&tex->handle, { pos.l, pos.t }, { pos.r, pos.b }, { uvs.l, uvs.t }, { uvs.r, uvs.b });
					draw.AddImage(&tex->handle, { pos.r, pos.t }, { r, pos.b }, { uvs.r, uvs.t }, { 1, uvs.b });

					draw.AddImage(&tex->handle, { l, pos.b }, { pos.l, b }, { 0, uvs.b }, { uvs.l, 1 });
					draw.AddImage(&tex->handle, { pos.l, pos.b }, { pos.r, b }, { uvs.l, uvs.b }, { uvs.r, 1 });
					draw.AddImage(&tex->handle, { pos.r, pos.b }, { r, b }, { uvs.r, uvs.b }, { 1, 1 });

				}
				else
				{
					draw.AddImage(&tex->handle, { l, t }, { r, b });
				}
			}
			else
			{
				draw.AddRectFilled({ l, t }, { r, b }, rect.image->color);
			}
		}
		if (rect.text)
		{
			draw.AddText(rect.text->font, (float)rect.text->font_size, { l, t }, rect.text->color, rect.text->text.c_str());
		}

		Entity child = m_universe.getFirstChild(rect.entity);
		if (child.isValid())
		{
			int idx = m_rects.find(child);
			if (idx >= 0)
			{
				renderRect(*m_rects.at(idx), pipeline, { l, t, r - l, b - t });
			}
		}
		Entity sibling = m_universe.getNextSibling(rect.entity);
		if (sibling.isValid())
		{
			int idx = m_rects.find(sibling);
			if (idx >= 0)
			{
				renderRect(*m_rects.at(idx), pipeline, parent_rect);
			}
		}
	}


	void render(Pipeline& pipeline, const Vec2& canvas_size) override
	{
		if (!m_root) return;

		m_canvas_size = canvas_size;
		renderRect(*m_root, pipeline, {0, 0, canvas_size.x, canvas_size.y});
	}


	Vec4 getButtonNormalColorRGBA(Entity entity) override
	{
		return ABGRu32ToRGBAVec4(m_buttons[entity].normal_color);
	}


	void setButtonNormalColorRGBA(Entity entity, const Vec4& color) override
	{
		m_buttons[entity].normal_color = RGBAVec4ToABGRu32(color);
	}


	Vec4 getButtonHoveredColorRGBA(Entity entity) override
	{
		return ABGRu32ToRGBAVec4(m_buttons[entity].hovered_color);
	}


	void setButtonHoveredColorRGBA(Entity entity, const Vec4& color) override
	{
		m_buttons[entity].hovered_color = RGBAVec4ToABGRu32(color);
	}


	Vec4 getImageColorRGBA(Entity entity) override
	{
		GUIImage* image = m_rects[entity]->image;
		return ABGRu32ToRGBAVec4(image->color);
	}


	static Vec4 ABGRu32ToRGBAVec4(u32 value)
	{
		float inv = 1 / 255.0f;
		return {
			((value >> 0) & 0xFF) * inv,
			((value >> 8) & 0xFF) * inv,
			((value >> 16) & 0xFF) * inv,
			((value >> 24) & 0xFF) * inv,
		};
	}


	static u32 RGBAVec4ToABGRu32(const Vec4& value)
	{
		u8 r = u8(value.x * 255 + 0.5f);
		u8 g = u8(value.y * 255 + 0.5f);
		u8 b = u8(value.z * 255 + 0.5f);
		u8 a = u8(value.w * 255 + 0.5f);
		return (a << 24) + (b << 16) + (g << 8) + r;
	}


	Path getImageSprite(Entity entity) override
	{
		GUIImage* image = m_rects[entity]->image;
		return image->sprite ? image->sprite->getPath() : Path();
	}


	void setImageSprite(Entity entity, const Path& path) override
	{
		GUIImage* image = m_rects[entity]->image;
		if (image->sprite)
		{
			image->sprite->getResourceManager().unload(*image->sprite);
		}
		auto* manager = m_system.getEngine().getResourceManager().get(Sprite::TYPE);
		if (path.isValid())
		{
			image->sprite = (Sprite*)manager->load(path);
		}
		else
		{
			image->sprite = nullptr;
		}
	}


	void setImageColorRGBA(Entity entity, const Vec4& color) override
	{
		GUIImage* image = m_rects[entity]->image;
		image->color = RGBAVec4ToABGRu32(color);
	}


	bool hasGUI(Entity entity) const override
	{
		int idx = m_rects.find(entity);
		if (idx < 0) return false;
		return m_rects.at(idx)->flags.isSet(GUIRect::IS_VALID);
	}


	Entity getRectAt(GUIRect& rect, const Vec2& pos, const Rect& parent_rect) const
	{
		if (!rect.flags.isSet(GUIRect::IS_VALID)) return INVALID_ENTITY;

		Rect r;
		r.x = parent_rect.x + rect.left.points + parent_rect.w * rect.left.relative;
		r.y = parent_rect.y + rect.top.points + parent_rect.h * rect.top.relative;
		float right = parent_rect.x + rect.right.points + parent_rect.w * rect.right.relative;
		float bottom = parent_rect.y + rect.bottom.points + parent_rect.h * rect.bottom.relative;

		r.w = right - r.x;
		r.h = bottom - r.y;

		bool intersect = pos.x >= r.x && pos.y >= r.y && pos.x <= r.x + r.w && pos.y <= r.y + r.h;

		for (Entity child = m_universe.getFirstChild(rect.entity); child.isValid(); child = m_universe.getNextSibling(child))
		{
			int idx = m_rects.find(child);
			if (idx < 0) continue;

			GUIRect* child_rect = m_rects.at(idx);
			Entity entity = getRectAt(*child_rect, pos, r);
			if (entity.isValid()) return entity;
		}

		return intersect ? rect.entity : INVALID_ENTITY;
	}


	Entity getRectAt(const Vec2& pos, const Vec2& canvas_size) const override
	{
		if (!m_root) return INVALID_ENTITY;

		return getRectAt(*m_root, pos, { 0, 0, canvas_size.x, canvas_size.y });
	}


	static Rect getRectOnCanvas(const Rect& parent_rect, GUIRect& rect)
	{
		float l = parent_rect.x + parent_rect.w * rect.left.relative + rect.left.points;
		float r = parent_rect.x + parent_rect.w * rect.right.relative + rect.right.points;
		float t = parent_rect.y + parent_rect.h * rect.top.relative + rect.top.points;
		float b = parent_rect.y + parent_rect.h * rect.bottom.relative + rect.bottom.points;

		return { l, t, r - l, b - t };
	}


	Rect getRectOnCanvas(Entity entity, const Vec2& canvas_size) const override
	{
		int idx = m_rects.find(entity);
		if (idx < 0) return { 0, 0, canvas_size.x, canvas_size.y };
		Entity parent = m_universe.getParent(entity);
		Rect parent_rect = getRectOnCanvas(parent, canvas_size);
		GUIRect* gui = m_rects[entity];
		float l = parent_rect.x + parent_rect.w * gui->left.relative + gui->left.points;
		float r = parent_rect.x + parent_rect.w * gui->right.relative + gui->right.points;
		float t = parent_rect.y + parent_rect.h * gui->top.relative + gui->top.points;
		float b = parent_rect.y + parent_rect.h * gui->bottom.relative + gui->bottom.points;

		return { l, t, r - l, b - t };
	}


	void enableRect(Entity entity, bool enable) override { m_rects[entity]->flags.set(GUIRect::IS_ENABLED, enable); }
	bool isRectEnabled(Entity entity) override { return m_rects[entity]->flags.isSet(GUIRect::IS_ENABLED); }
	float getRectLeftPoints(Entity entity) override { return m_rects[entity]->left.points; }
	void setRectLeftPoints(Entity entity, float value) override { m_rects[entity]->left.points = value; }
	float getRectLeftRelative(Entity entity) override { return m_rects[entity]->left.relative; }
	void setRectLeftRelative(Entity entity, float value) override { m_rects[entity]->left.relative = value; }

	float getRectRightPoints(Entity entity) override { return m_rects[entity]->right.points; }
	void setRectRightPoints(Entity entity, float value) override { m_rects[entity]->right.points = value; }
	float getRectRightRelative(Entity entity) override { return m_rects[entity]->right.relative; }
	void setRectRightRelative(Entity entity, float value) override { m_rects[entity]->right.relative = value; }

	float getRectTopPoints(Entity entity) override { return m_rects[entity]->top.points; }
	void setRectTopPoints(Entity entity, float value) override { m_rects[entity]->top.points = value; }
	float getRectTopRelative(Entity entity) override { return m_rects[entity]->top.relative; }
	void setRectTopRelative(Entity entity, float value) override { m_rects[entity]->top.relative = value; }

	float getRectBottomPoints(Entity entity) override { return m_rects[entity]->bottom.points; }
	void setRectBottomPoints(Entity entity, float value) override { m_rects[entity]->bottom.points = value; }
	float getRectBottomRelative(Entity entity) override { return m_rects[entity]->bottom.relative; }
	void setRectBottomRelative(Entity entity, float value) override { m_rects[entity]->bottom.relative = value; }


	void setTextFontSize(Entity entity, int value) override
	{
		GUIText* gui_text = m_rects[entity]->text;
		FontResource* res = gui_text->font_resource;
		if (res) res->removeRef(*gui_text->font);
		gui_text->font_size = value;
		if (res) gui_text->font = res->addRef(gui_text->font_size);
	}
	
	
	int getTextFontSize(Entity entity) override
	{
		GUIText* gui_text = m_rects[entity]->text;
		return gui_text->font_size;
	}
	
	
	Vec4 getTextColorRGBA(Entity entity) override
	{
		GUIText* gui_text = m_rects[entity]->text;
		return ABGRu32ToRGBAVec4(gui_text->color);
	}


	void setTextColorRGBA(Entity entity, const Vec4& color) override
	{
		GUIText* gui_text = m_rects[entity]->text;
		gui_text->color = RGBAVec4ToABGRu32(color);
	}


	Path getTextFontPath(Entity entity) override
	{
		GUIText* gui_text = m_rects[entity]->text;
		return gui_text->font_resource == nullptr ? Path() : gui_text->font_resource->getPath();
	}


	void setTextFontPath(Entity entity, const Path& path) override
	{
		GUIText* gui_text = m_rects[entity]->text;
		FontResource* res = gui_text->font_resource;
		if (res)
		{
			res->removeRef(*gui_text->font);
			res->getResourceManager().unload(*res);
		}
		if (!path.isValid())
		{
			gui_text->font_resource = nullptr;
			gui_text->font = m_font_manager->getDefaultFont();
			return;
		}
		gui_text->font_resource = (FontResource*)m_font_manager->load(path);
		gui_text->font = gui_text->font_resource->addRef(gui_text->font_size);
	}


	void setText(Entity entity, const char* value) override
	{
		GUIText* gui_text = m_rects[entity]->text;
		gui_text->text = value;
	}


	const char* getText(Entity entity) override
	{
		GUIText* text = m_rects[entity]->text;
		return text->text.c_str();
	}


	void serializeRect(ISerializer& serializer, Entity entity)
	{
		const GUIRect& rect = *m_rects[entity];
		
		serializer.write("flags", rect.flags.base);
		serializer.write("top_pts", rect.top.points);
		serializer.write("top_rel", rect.top.relative);

		serializer.write("right_pts", rect.right.points);
		serializer.write("right_rel", rect.right.relative);

		serializer.write("bottom_pts", rect.bottom.points);
		serializer.write("bottom_rel", rect.bottom.relative);

		serializer.write("left_pts", rect.left.points);
		serializer.write("left_rel", rect.left.relative);
	}


	void deserializeRect(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		int idx = m_rects.find(entity);
		GUIRect* rect;
		if (idx >= 0)
		{
			rect = m_rects.at(idx);
		}
		else
		{
			rect = LUMIX_NEW(m_allocator, GUIRect);
			m_rects.insert(entity, rect);
		}
		rect->entity = entity;
		serializer.read(&rect->flags.base);
		serializer.read(&rect->top.points);
		serializer.read(&rect->top.relative);

		serializer.read(&rect->right.points);
		serializer.read(&rect->right.relative);

		serializer.read(&rect->bottom.points);
		serializer.read(&rect->bottom.relative);

		serializer.read(&rect->left.points);
		serializer.read(&rect->left.relative);
		
		m_root = findRoot();
		
		m_universe.onComponentCreated(entity, GUI_RECT_TYPE, this);
	}


	void serializeButton(ISerializer& serializer, Entity entity)
	{
		const GUIButton& button = m_buttons[entity];
		serializer.write("normal_color", button.normal_color);
		serializer.write("hovered_color", button.hovered_color);
	}

	
	void deserializeButton(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		GUIButton& button = m_buttons.emplace(entity);
		serializer.read(&button.normal_color);
		serializer.read(&button.hovered_color);
		m_universe.onComponentCreated(entity, GUI_BUTTON_TYPE, this);
	}


	void serializeImage(ISerializer& serializer, Entity entity)
	{
		const GUIRect& rect = *m_rects[entity];
		serializer.write("color", rect.image->color);
	}


	void deserializeImage(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		int idx = m_rects.find(entity);
		if (idx < 0)
		{
			GUIRect* rect = LUMIX_NEW(m_allocator, GUIRect);
			rect->entity = entity;
			idx = m_rects.insert(entity, rect);
		}
		GUIRect& rect = *m_rects.at(idx);
		rect.image = LUMIX_NEW(m_allocator, GUIImage);
		
		serializer.read(&rect.image->color);
		
		m_universe.onComponentCreated(entity, GUI_IMAGE_TYPE, this);
	}


	void serializeText(ISerializer& serializer, Entity entity)
	{
		const GUIRect& rect = *m_rects[entity];
		serializer.write("font", rect.text->font_resource ? rect.text->font_resource->getPath().c_str() : "");
		serializer.write("color", rect.text->color);
		serializer.write("font_size", rect.text->font_size);
		serializer.write("text", rect.text->text.c_str());
	}


	void deserializeText(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		int idx = m_rects.find(entity);
		if (idx < 0)
		{
			GUIRect* rect = LUMIX_NEW(m_allocator, GUIRect);
			rect->entity = entity;
			idx = m_rects.insert(entity, rect);
		}
		GUIRect& rect = *m_rects.at(idx);
		rect.text = LUMIX_NEW(m_allocator, GUIText)(m_allocator);

		char tmp[MAX_PATH_LENGTH];
		serializer.read(tmp, lengthOf(tmp));
		serializer.read(&rect.text->color);
		serializer.read(&rect.text->font_size);
		serializer.read(&rect.text->text);
		if (tmp[0] == '\0')
		{
			rect.text->font_resource = nullptr;
			rect.text->font = m_font_manager->getDefaultFont();
		}
		else
		{
			rect.text->font_resource = (FontResource*)m_font_manager->load(Path(tmp));
			rect.text->font = rect.text->font_resource->addRef(rect.text->font_size);
		}

		m_universe.onComponentCreated(entity, GUI_TEXT_TYPE, this);
	}


	void clear() override
	{
		for (GUIRect* rect : m_rects)
		{
			LUMIX_DELETE(m_allocator, rect->image);
			LUMIX_DELETE(m_allocator, rect->text);
			LUMIX_DELETE(m_allocator, rect);
		}
		m_rects.clear();
		m_buttons.clear();
	}


	void hoverOut(const GUIRect& rect)
	{
		int idx = m_buttons.find(rect.entity);
		if (idx < 0) return;

		const GUIButton& button = m_buttons.at(idx);
		if (!rect.image) return;

		rect.image->color = button.normal_color;

		m_rect_hovered_out.invoke(rect.entity);
	}


	void hover(const GUIRect& rect)
	{
		int idx = m_buttons.find(rect.entity);
		if (idx < 0) return;

		const GUIButton& button = m_buttons.at(idx);

		if (!rect.image) return;

		rect.image->color = button.hovered_color;

		m_rect_hovered.invoke(rect.entity);
	}


	void handleMouseAxisEvent(const Rect& parent_rect, GUIRect& rect, const Vec2& mouse_pos, const Vec2& prev_mouse_pos)
	{
		if (!rect.flags.isSet(GUIRect::IS_ENABLED)) return;

		const Rect& r = getRectOnCanvas(parent_rect, rect);

		bool is = contains(r, mouse_pos);
		bool was = contains(r, prev_mouse_pos);
		if (is != was)
		{
			is ? hover(rect) : hoverOut(rect);
		}

		for (Entity e = m_universe.getFirstChild(rect.entity); e.isValid(); e = m_universe.getNextSibling(e))
		{
			int idx = m_rects.find(e);
			if (idx < 0) continue;
			handleMouseAxisEvent(r, *m_rects.at(idx), mouse_pos, prev_mouse_pos);
		}
	}


	static bool contains(const Rect& rect, const Vec2& pos)
	{
		return pos.x >= rect.x && pos.y >= rect.y && pos.x <= rect.x + rect.w && pos.y <= rect.y + rect.h;
	}


	void handleMouseButtonEvent(const Rect& parent_rect, GUIRect& rect, const InputSystem::Event& event)
	{
		if (!rect.flags.isSet(GUIRect::IS_ENABLED)) return;

		Vec2 pos(event.data.button.x_abs, event.data.button.y_abs);
		const Rect& r = getRectOnCanvas(parent_rect, rect);
		
		if (contains(r, pos)) m_button_clicked.invoke(rect.entity);

		for (Entity e = m_universe.getFirstChild(rect.entity); e.isValid(); e = m_universe.getNextSibling(e))
		{
			int idx = m_rects.find(e);
			if (idx < 0) continue;
			handleMouseButtonEvent(r, *m_rects.at(idx), event);
		}
	}


	void handleInput()
	{
		if (!m_root) return;
		InputSystem& input = m_system.getEngine().getInputSystem();
		const InputSystem::Event* events = input.getEvents();
		int events_count = input.getEventsCount();
		for (int i = 0; i < events_count; ++i)
		{
			const InputSystem::Event& event = events[i];
			switch (event.type)
			{
				case InputSystem::Event::AXIS:
					if (event.device->type == InputSystem::Device::MOUSE)
					{
						Vec2 pos(event.data.axis.x_abs, event.data.axis.y_abs);
						Vec2 old_pos = pos - Vec2(event.data.axis.x, event.data.axis.y);
						handleMouseAxisEvent({0, 0,  m_canvas_size.x, m_canvas_size.y }, *m_root, pos, old_pos);
					}
					break;
				case InputSystem::Event::BUTTON:
					if (event.device->type == InputSystem::Device::MOUSE)
					{
						handleMouseButtonEvent({ 0, 0, m_canvas_size.x, m_canvas_size.y }, *m_root, event);
					}
					break;
			}
		}
	}


	void update(float time_delta, bool paused) override
	{
		if (paused) return;

		handleInput();
	}


	void createRect(Entity entity)
	{
		int idx = m_rects.find(entity);
		GUIRect* rect;
		if (idx >= 0)
		{
			rect = m_rects.at(idx);
		}
		else
		{
			rect = LUMIX_NEW(m_allocator, GUIRect);
			m_rects.insert(entity, rect);
		}
		rect->entity = entity;
		rect->flags.set(GUIRect::IS_VALID);
		rect->flags.set(GUIRect::IS_ENABLED);
		m_universe.onComponentCreated(entity, GUI_RECT_TYPE, this);
		m_root = findRoot();
	}


	void createText(Entity entity)
	{
		int idx = m_rects.find(entity);
		if (idx < 0)
		{
			createRect(entity);
			idx = m_rects.find(entity);
		}
		GUIRect& rect = *m_rects.at(idx);
		rect.text = LUMIX_NEW(m_allocator, GUIText)(m_allocator);
		rect.text->font = m_font_manager->getDefaultFont();

		m_universe.onComponentCreated(entity, GUI_TEXT_TYPE, this);
	}


	void createButton(Entity entity)
	{
		m_buttons.emplace(entity);
		m_universe.onComponentCreated(entity, GUI_BUTTON_TYPE, this);
	}


	void createImage(Entity entity)
	{
		int idx = m_rects.find(entity);
		if (idx < 0)
		{
			createRect(entity);
			idx = m_rects.find(entity);
		}
		GUIRect& rect = *m_rects.at(idx);
		rect.image = LUMIX_NEW(m_allocator, GUIImage);

		m_universe.onComponentCreated(entity, GUI_IMAGE_TYPE, this);
	}


	GUIRect* findRoot()
	{
		if (m_rects.size() == 0) return nullptr;
		for (int i = 0, n = m_rects.size(); i < n; ++i)
		{
			GUIRect& rect = *m_rects.at(i);
			if (!rect.flags.isSet(GUIRect::IS_VALID)) continue;
			Entity e = m_rects.getKey(i);
			Entity parent = m_universe.getParent(e);
			if (parent == INVALID_ENTITY) return &rect;
			if (m_rects.find(parent) < 0) return &rect;
		}
		return nullptr;
	}


	void destroyRect(Entity entity)
	{
		GUIRect* rect = m_rects[entity];
		rect->flags.set(GUIRect::IS_VALID, false);
		if (rect->image == nullptr && rect->text == nullptr)
		{
			LUMIX_DELETE(m_allocator, rect);
			m_rects.erase(entity);
			
		}
		if (rect == m_root)
		{
			m_root = findRoot();
		}
		m_universe.onComponentDestroyed(entity, GUI_RECT_TYPE, this);
	}


	void destroyButton(Entity entity)
	{
		m_buttons.erase(entity);
		m_universe.onComponentDestroyed(entity, GUI_BUTTON_TYPE, this);
	}


	void destroyImage(Entity entity)
	{
		GUIRect* rect = m_rects[entity];
		LUMIX_DELETE(m_allocator, rect->image);
		rect->image = nullptr;
		m_universe.onComponentDestroyed(entity, GUI_IMAGE_TYPE, this);
	}


	void destroyText(Entity entity)
	{
		GUIRect* rect = m_rects[entity];
		LUMIX_DELETE(m_allocator, rect->text);
		rect->text = nullptr;
		m_universe.onComponentDestroyed(entity, GUI_TEXT_TYPE, this);
	}


	void serialize(OutputBlob& serializer) override
	{
		serializer.write(m_rects.size());
		for (GUIRect* rect : m_rects)
		{
			serializer.write(rect->flags);
			serializer.write(rect->entity);
			serializer.write(rect->top);
			serializer.write(rect->right);
			serializer.write(rect->bottom);
			serializer.write(rect->left);

			serializer.write(rect->image != nullptr);
			if (rect->image)
			{
				serializer.write(rect->image->color);
			}

			serializer.write(rect->text != nullptr);
			if (rect->text)
			{
				serializer.writeString(rect->text->font_resource ? rect->text->font_resource->getPath().c_str() : "");
				serializer.write(rect->text->color);
				serializer.write(rect->text->font_size);
				serializer.write(rect->text->text);
			}
		}

		serializer.write(m_buttons.size());
		for (int i = 0, c = m_buttons.size(); i < c; ++i)
		{
			serializer.write(m_buttons.getKey(i));
			const GUIButton& button = m_buttons.at(i);
			serializer.write(button);
		}
	}


	void deserialize(InputBlob& serializer) override
	{
		clear();
		int count = serializer.read<int>();
		for (int i = 0; i < count; ++i)
		{
			GUIRect* rect = LUMIX_NEW(m_allocator, GUIRect);
			serializer.read(rect->flags);
			serializer.read(rect->entity);
			serializer.read(rect->top);
			serializer.read(rect->right);
			serializer.read(rect->bottom);
			serializer.read(rect->left);
			m_rects.insert(rect->entity, rect);
			if (rect->flags.isSet(GUIRect::IS_VALID))
			{
				m_universe.onComponentCreated(rect->entity, GUI_RECT_TYPE, this);
			}

			bool has_image = serializer.read<bool>();
			if (has_image)
			{
				rect->image = LUMIX_NEW(m_allocator, GUIImage);
				serializer.read(rect->image->color);
				m_universe.onComponentCreated(rect->entity, GUI_IMAGE_TYPE, this);

			}
			bool has_text = serializer.read<bool>();
			if (has_text)
			{
				rect->text = LUMIX_NEW(m_allocator, GUIText)(m_allocator);
				GUIText& text = *rect->text;
				char tmp[MAX_PATH_LENGTH];
				serializer.readString(tmp, lengthOf(tmp));
				serializer.read(text.color);
				serializer.read(text.font_size);
				serializer.read(text.text);
				if (tmp[0] == '\0')
				{
					text.font_resource = nullptr;
					text.font = m_font_manager->getDefaultFont();
				}
				else
				{
					text.font_resource = (FontResource*)m_font_manager->load(Path(tmp));
					text.font = text.font_resource->addRef(text.font_size);
				}
				m_universe.onComponentCreated(rect->entity, GUI_TEXT_TYPE, this);
			}
		}
		count = serializer.read<int>();
		for (int i = 0; i < count; ++i)
		{
			Entity e;
			serializer.read(e);
			GUIButton& button = m_buttons.emplace(e);
			serializer.read(button);
		}
		m_root = findRoot();
	}
	
	
	DelegateList<void(Entity)>& buttonClicked() override
	{
		return m_button_clicked;
	}


	DelegateList<void(Entity)>& rectHovered() override
	{
		return m_rect_hovered;
	}


	DelegateList<void(Entity)>& rectHoveredOut() override
	{
		return m_rect_hovered_out;
	}


	Universe& getUniverse() override { return m_universe; }
	IPlugin& getPlugin() const override { return m_system; }

	IAllocator& m_allocator;
	Universe& m_universe;
	GUISystem& m_system;
	
	AssociativeArray<Entity, GUIRect*> m_rects;
	AssociativeArray<Entity, GUIButton> m_buttons;
	GUIRect* m_root = nullptr;
	FontManager* m_font_manager = nullptr;
	Vec2 m_canvas_size;
	Entity m_mouse_down_entity;
	DelegateList<void(Entity)> m_button_clicked;
	DelegateList<void(Entity)> m_rect_hovered;
	DelegateList<void(Entity)> m_rect_hovered_out;
};


GUIScene* GUIScene::createInstance(GUISystem& system,
	Universe& universe,
	IAllocator& allocator)
{
	return LUMIX_NEW(allocator, GUISceneImpl)(system, universe, allocator);
}


void GUIScene::destroyInstance(GUIScene* scene)
{
	LUMIX_DELETE(static_cast<GUISceneImpl*>(scene)->m_allocator, scene);
}


} // namespace Lumix