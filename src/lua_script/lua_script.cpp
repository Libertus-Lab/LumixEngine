#include "engine/log.h"
#include "engine/file_system.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "lua_script.h"

namespace Lumix {

const ResourceType LuaScript::TYPE("lua_script");

LuaScript::LuaScript(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator, m_path.c_str())
	, m_source_code(m_allocator)
	, m_dependencies(m_allocator)
{
}

LuaScript::~LuaScript() = default;

void LuaScript::unload() {
	for (LuaScript* scr : m_dependencies) scr->decRefCount();
	m_dependencies.clear();
	m_source_code = "";
}

bool LuaScript::load(Span<const u8> mem) {
	InputMemoryStream blob(mem.begin(), mem.length());
	u32 num_deps;
	blob.read(num_deps);
	for (u32 i = 0; i < num_deps; ++i) {
		const char* dep_path = blob.readString();
		LuaScript* scr = m_resource_manager.getOwner().load<LuaScript>(Path(dep_path));
		addDependency(*scr);
		m_dependencies.push(scr);
	}
	m_source_code = StringView((const char*)blob.skip(0), (u32)blob.remaining());
	return true;
}

} // namespace Lumix