#pragma once

#include "engine/lumix.h"

namespace Lumix {

template <typename T> struct Delegate;
template <typename T> struct UniquePtr;

namespace os {
	struct FileIterator;
	struct InputFile;
	struct OutputFile;
}

struct LUMIX_ENGINE_API FileSystem {
	using ContentCallback = Delegate<void(Span<const u8>, bool)>;

	struct LUMIX_ENGINE_API AsyncHandle {
		static AsyncHandle invalid() { return AsyncHandle(0xffFFffFF); };
		explicit AsyncHandle(u32 value) : value(value) {}
		u32 value;
		bool isValid() const { return value != 0xffFFffFF; }
	};

	static UniquePtr<FileSystem> create(const char* base_path, struct IAllocator& allocator);
	static UniquePtr<FileSystem> createPacked(const char* pak_path, struct IAllocator& allocator);

	virtual ~FileSystem() {}

	virtual u64 getLastModified(struct StringView path) = 0;
	virtual bool copyFile(StringView from, StringView to) = 0;
	virtual bool moveFile(StringView from, StringView to) = 0;
	virtual bool deleteFile(StringView path) = 0;
	virtual bool fileExists(StringView path) = 0;
	virtual os::FileIterator* createFileIterator(StringView dir) = 0;
	virtual bool open(StringView path, os::InputFile& file) = 0;
	virtual bool open(StringView path, os::OutputFile& file) = 0;

	virtual void setBasePath(const char* path) = 0;
	virtual const char* getBasePath() const = 0;
	virtual void processCallbacks() = 0;
	virtual bool hasWork() = 0;

	[[nodiscard]] virtual bool saveContentSync(const struct Path& file, Span<const u8> content) = 0;
	[[nodiscard]] virtual bool getContentSync(const struct Path& file, struct OutputMemoryStream& content) = 0;
	virtual AsyncHandle getContent(const Path& file, const ContentCallback& callback) = 0;
	virtual void cancel(AsyncHandle handle) = 0;
};

} // namespace Lumix
