#pragma once

// Native loading of osu!mania (.osu) and StepMania/Etterna (.sm/.ssc) charts.
//
// One .osu file is one chart. A .sm/.ssc file holds several charts, so each gets a virtual path
// "<file>|<index>" ('|' cannot appear in a Windows filename, so it never collides with a real file).
// Every chart is turned into a kson document in memory and parsed by libkson, so the rest of the
// game sees an ordinary kson::ChartData.
namespace ForeignChart
{
	enum class Format
	{
		None,
		Osu,
		Simfile,
	};

	inline constexpr char32 kVirtualSeparator = U'|';

	/// Bump whenever parser output changes (notes, levels, titles...) so song-list caches of
	/// folders with foreign charts rebuild.
	inline constexpr int32 kParserVersion = 2;

	/// Format of a chart path (understands "<file>|<index>" paths).
	[[nodiscard]]
	Format FormatOf(FilePathView chartPath);

	[[nodiscard]]
	inline bool IsForeignChart(FilePathView chartPath)
	{
		return FormatOf(chartPath) != Format::None;
	}

	/// The file on disk behind a chart path (strips "|<index>").
	[[nodiscard]]
	FilePath RealFile(FilePathView chartPath);

	[[nodiscard]]
	bool Exists(FilePathView chartPath);

	/// Foreign chart paths in a song folder: osu!mania beatmaps, and one path per chart in each
	/// simfile (.ssc preferred over a .sm with the same name).
	[[nodiscard]]
	Array<FilePath> ListCharts(FilePathView directoryPath);

	/// Full chart. On failure, error is set to GeneralChartFormatError.
	[[nodiscard]]
	kson::ChartData LoadChartData(FilePathView chartPath);

	[[nodiscard]]
	kson::MetaChartData LoadMetaChartData(FilePathView chartPath);

	/// Score file stem for a chart path relative to the songs folder (extension removed, and
	/// "song.ssc|3" becomes "song[3]" so it stays a valid filename).
	[[nodiscard]]
	String ScoreStem(StringView relativeChartPath);

	/// Foreign charts are never rewritten; auto-sync offsets are stored here instead.
	void SaveOffsetOverride(FilePathView chartPath, int32 offset);
}
