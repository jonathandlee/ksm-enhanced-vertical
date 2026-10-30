#include "Library.hpp"

namespace Library
{
	namespace
	{
		struct ForeignDef
		{
			Kind kind;
			StringView id;
			StringView displayName;
			StringView configKey;
			bool rootIsPackage;
		};

		constexpr std::array<ForeignDef, 3> kForeignDefs{ {
			{ Kind::Osu, U"osu", U"osu!", ConfigIni::Key::kOsuDirectory, true },
			{ Kind::Etterna, U"etterna", U"Etterna", ConfigIni::Key::kEtternaDirectory, false },
			{ Kind::StepMania, U"stepmania", U"StepMania", ConfigIni::Key::kStepManiaDirectory, false },
		} };

		String NormalizeDir(FilePathView path)
		{
			String full = FileSystem::FullPath(path);
			full.replace(U'\\', U'/');
			if (!full.ends_with(U'/')) full.push_back(U'/');
			return full;
		}

		// The user may point at the game install rather than its Songs folder.
		FilePath ResolveSongsFolder(StringView configured)
		{
			const FilePath songs = FileSystem::PathAppend(configured, U"Songs");
			if (FileSystem::IsDirectory(songs)) return songs;
			return FilePath{ configured };
		}

		// Library roots are looked up for every score and cache path, so resolve them once per config value.
		struct CachedForeign
		{
			String configured;
			Optional<Info> info;
		};
		std::array<CachedForeign, 3> s_cache;

		Optional<Info> ForeignInfo(size_t i)
		{
			const auto& def = kForeignDefs[i];
			const String configured{ ConfigIni::GetString(def.configKey) };
			auto& cached = s_cache[i];
			if (cached.configured != configured || (configured.isEmpty() && cached.info))
			{
				cached.configured = configured;
				cached.info.reset();
				if (!configured.isEmpty())
				{
					const FilePath folder = ResolveSongsFolder(configured);
					if (FileSystem::IsDirectory(folder))
					{
						cached.info = Info{ def.kind, String{ def.id }, String{ def.displayName }, NormalizeDir(folder), def.rootIsPackage };
					}
				}
			}
			return cached.info;
		}

		Array<FilePath> SubDirectories(FilePathView path)
		{
			Array<FilePath> dirs = FileSystem::DirectoryContents(path, Recursive::No).filter([](FilePathView p) { return FileSystem::IsDirectory(p); });
			dirs.sort_by([](const FilePath& a, const FilePath& b)
			{
				return FsUtils::DirectoryNameByDirectoryPath(a).lowercased() < FsUtils::DirectoryNameByDirectoryPath(b).lowercased();
			});
			return dirs.map([](FilePathView p) { return FileSystem::FullPath(p); });
		}
	}

	Array<Info> All()
	{
		Array<Info> libraries;
		libraries.push_back(Info{ Kind::Ksm, U"ksm", U"K-Shoot MANIA", NormalizeDir(FsUtils::SongsDirectoryPath()), false });
		for (size_t i = 0; i < kForeignDefs.size(); ++i)
		{
			if (auto info = ForeignInfo(i)) libraries.push_back(*info);
		}
		return libraries;
	}

	bool IsMultiLibrary()
	{
		for (size_t i = 0; i < kForeignDefs.size(); ++i)
		{
			if (ForeignInfo(i)) return true;
		}
		return false;
	}

	Optional<Info> FindById(StringView id)
	{
		for (const auto& library : All())
		{
			if (library.id == id) return library;
		}
		return none;
	}

	Optional<Info> ForeignLibraryForPath(FilePathView fullPath)
	{
		String path{ fullPath };
		path.replace(U'\\', U'/');
		const String lowered = path.lowercased();
		for (size_t i = 0; i < kForeignDefs.size(); ++i)
		{
			const auto info = ForeignInfo(i);
			if (!info) continue;
			const String root = info->rootPath.lowercased();
			if (lowered.starts_with(root) || lowered + U'/' == root) return info;
		}
		return none;
	}

	Info LibraryForPath(FilePathView fullPath)
	{
		if (auto foreign = ForeignLibraryForPath(fullPath)) return *foreign;
		return All().front();
	}

	Array<FilePath> PackageDirectories(const Info& library)
	{
		if (library.rootIsPackage) return { FileSystem::FullPath(library.rootPath) };
		return SubDirectories(library.rootPath);
	}

	Array<FilePath> AllPackageDirectories()
	{
		Array<FilePath> result;
		for (const auto& library : All()) result.append(PackageDirectories(library));
		return result;
	}

	Optional<Info> FromSpecialPath(StringView path)
	{
		if (!IsSpecialPath(path)) return none;
		return FindById(path.substr(1));
	}
}
