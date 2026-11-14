#pragma once

// Song libraries: the KSM songs folder plus optional osu!, Etterna and StepMania song folders
// (config.ini: songsdir, osudir, etternadir, stepmaniadir). With more than one library, song select
// shows them as a level above the package folders.
namespace Library
{
	enum class Kind
	{
		Ksm,
		Osu,
		Etterna,
		StepMania,
	};

	struct Info
	{
		Kind kind = Kind::Ksm;
		String id; // "ksm", "osu", "etterna", "stepmania"
		String displayName;
		FilePath rootPath;			// full path, trailing '/'
		bool rootIsPackage = false; // osu!'s Songs folder holds beatmap sets directly, so it is one package
	};

	/// KSM first, then each foreign library that is configured and exists.
	[[nodiscard]]
	Array<Info> All();

	[[nodiscard]]
	bool IsMultiLibrary();

	[[nodiscard]]
	Optional<Info> FindById(StringView id);

	/// The foreign library whose folder contains this path (none for KSM / anything else).
	[[nodiscard]]
	Optional<Info> ForeignLibraryForPath(FilePathView fullPath);

	/// The library containing this path, falling back to KSM.
	[[nodiscard]]
	Info LibraryForPath(FilePathView fullPath);

	/// Folders shown as packages for a library.
	[[nodiscard]]
	Array<FilePath> PackageDirectories(const Info& library);

	/// Packages across every library (for the All folder).
	[[nodiscard]]
	Array<FilePath> AllPackageDirectories();

	inline constexpr char32 kSpecialPathPrefix = U'>';

	[[nodiscard]]
	inline String SpecialPath(const Info& library)
	{
		return kSpecialPathPrefix + library.id;
	}

	[[nodiscard]]
	inline bool IsSpecialPath(StringView path)
	{
		return !path.isEmpty() && path[0] == kSpecialPathPrefix;
	}

	[[nodiscard]]
	Optional<Info> FromSpecialPath(StringView path);

	/// Prefix used in songs-relative paths for foreign libraries ("@osu/...").
	inline constexpr char32 kRelativePathPrefix = U'@';
}
