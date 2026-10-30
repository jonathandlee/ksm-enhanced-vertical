#pragma once
// Internal to ForeignChart: the format-neutral chart model the osu! and StepMania readers produce.
// A C++ port of mania2ksh's model.py and grid.py (C:\Users\gekic\mania2ksh).
#include <array>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace ForeignChart::detail
{
	constexpr int64 kResolution = 240; // pulses per beat

	struct GriddedChart
	{
		// Meta (UTF-8)
		std::string title;
		std::string artist;
		std::string chartAuthor;
		std::string difficultyName;
		std::string titleTranslit;
		std::string artistTranslit;
		std::string audioFilename;
		std::string jacketFilename;
		std::string source;
		int32 keys = 4;
		int32 level = 1;
		int32 slot = 0;
		int32 sourceLevel = 0;
		int32 previewMs = 0;

		// Audio time in ms at which pulse 0 falls (KSM's definition)
		int32 offsetMs = 0;

		std::map<int64, double> bpms;
		std::vector<std::tuple<int64, int32, int32>> timeSigs{ { 0, 4, 4 } };
		std::vector<double> displayBpms;

		// Lane -> {start pulse: length in pulses}, 0 = chip
		std::array<std::map<int64, int64>, 4> bt;
		std::array<std::map<int64, int64>, 2> fx;

		int32 mergedNotes = 0;
	};

	struct Lane
	{
		bool isFx = false;
		int32 index = 0;
	};

	/// Snap a fractional pulse onto the musical grid. Returns {pulse, exact}.
	std::pair<int64, bool> SnapPulse(double raw);

	/// Map source columns onto KSM's 4 BT + 2 FX lanes ("spatial" layout).
	std::vector<Lane> LaneMap(int32 keys);

	/// Put a note in a lane; a note on an occupied pulse keeps the longer length.
	void PlaceNote(GriddedChart& chart, const Lane& lane, int64 start, int64 length);

	/// A hold must never run into the next note in its lane.
	void ClipHolds(GriddedChart& chart);

	bool IsEmpty(const GriddedChart& chart);

	/// Decode chart text that may be UTF-8, cp1252, Shift-JIS or EUC-KR into UTF-8.
	std::string DecodeToUtf8(const std::string& raw);

	/// Read a whole file as bytes. Empty on failure.
	std::string ReadAllBytes(FilePathView path);

	/// Locate a file a chart refers to: exact, case-insensitive, same stem, then the largest file of the kind.
	FilePath FindAsset(FilePathView folder, StringView name, const Array<String>& suffixes);

	extern const Array<String> kAudioSuffixes;
	extern const Array<String> kImageSuffixes;

	/// Estimate a KSM level (1-20) from notes per second. Logarithmic, so dense chordjack and stream
	/// charts spread out instead of all landing on 20.
	inline int32 LevelFromDensity(double notesPerSecond)
	{
		return Clamp(static_cast<int32>(std::lround(-2.0 + 6.0 * std::log1p(std::max(0.0, notesPerSecond)))), 1, 20);
	}

	/// Map a StepMania/Etterna #METER onto KSM's 1-20 (Etterna meters run past 20). 0 or 1 = unknown.
	inline Optional<int32> LevelFromMeter(int32 meter)
	{
		if (meter <= 1)
			return none;
		if (meter <= 15)
			return meter;
		return Clamp(15 + (meter - 15 + 1) / 3, 15, 20);
	}

	/// Python-style round-half-to-even.
	inline double RoundHalfEven(double v)
	{
		return std::nearbyint(v);
	}

	/// Parse a decimal string leniently. Returns fallback on failure.
	double ParseDouble(std::string_view s, double fallback);
	/// int(float(s)) semantics.
	int32 ParseIntTrunc(std::string_view s, int32 fallback);

	std::string Trim(std::string_view s);
	std::string ToLowerAscii(std::string_view s);
}
