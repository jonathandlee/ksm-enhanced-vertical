// osu!mania (.osu) reader. A port of mania2ksh/osu.py.
//
// osu! stores absolute milliseconds from the start of the audio. The one real trap: beatmaps in file
// format v4 and earlier get +24 ms on every timing point and hit object (EARLY_VERSION_TIMING_OFFSET
// in osu!'s LegacyBeatmapDecoder). AudioLeadIn is deliberately not applied; it is pre-roll, not a shift.
#include "OsuChart.hpp"
#include <algorithm>
#include <fstream>
#include <regex>

namespace ForeignChart::detail
{
	namespace
	{
		constexpr int32 kEarlyVersionTimingOffset = 24;
		constexpr int32 kEarlyVersionMax = 4;
		constexpr int32 kManiaMode = 3;

		struct TimingPoint
		{
			double time = 0.0;
			double beatLength = 0.0;
			int32 meter = 4;
			double bpm() const { return beatLength > 0 ? 60000.0 / beatLength : 0.0; }
		};

		struct HitObject
		{
			double time = 0.0;
			int32 column = 0;
			Optional<double> endTime;
			bool isHold() const { return endTime.has_value() && *endTime > time; }
		};

		struct Beatmap
		{
			int32 formatVersion = 14;
			std::string title, titleUnicode, artist, artistUnicode, creator, version, audioFilename, background;
			int32 previewTime = -1;
			int32 mode = kManiaMode;
			int32 keys = 4;
			std::vector<TimingPoint> timingPoints;
			std::vector<HitObject> hitObjects;
		};

		std::vector<std::string> SplitLines(const std::string& text)
		{
			std::vector<std::string> lines;
			size_t start = 0;
			for (size_t i = 0; i <= text.size(); ++i)
			{
				if (i == text.size() || text[i] == '\n' || text[i] == '\r')
				{
					lines.emplace_back(text.substr(start, i - start));
					if (i < text.size() && text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
					start = i + 1;
				}
			}
			return lines;
		}

		std::vector<std::string> SplitComma(const std::string& line)
		{
			std::vector<std::string> parts;
			size_t start = 0;
			for (size_t i = 0; i <= line.size(); ++i)
			{
				if (i == line.size() || line[i] == ',')
				{
					parts.push_back(Trim(std::string_view{ line }.substr(start, i - start)));
					start = i + 1;
				}
			}
			return parts;
		}

		std::string StripComment(const std::string& line)
		{
			const std::string trimmed = Trim(line);
			if (trimmed.rfind("//", 0) == 0) return {};
			return trimmed;
		}

		std::string FileNameUtf8(FilePathView path)
		{
			return FileSystem::FileName(path).toUTF8();
		}

		std::string ParseBackground(const std::vector<std::string>& lines)
		{
			for (const auto& line : lines)
			{
				auto parts = SplitComma(line);
				for (auto& p : parts)
				{
					if (!p.empty() && p.front() == '"') p.erase(p.begin());
					if (!p.empty() && p.back() == '"') p.pop_back();
					p = Trim(p);
				}
				if (parts.size() >= 3 && (parts[0] == "0" || parts[0] == "Background") && !parts[2].empty())
				{
					return parts[2];
				}
			}
			return {};
		}

		std::vector<TimingPoint> ParseTimingPoints(const std::vector<std::string>& lines, int32 shift)
		{
			std::vector<TimingPoint> points;
			for (const auto& line : lines)
			{
				const auto parts = SplitComma(line);
				if (parts.size() < 2) continue;
				const double time = ParseDouble(parts[0], 0.0) + shift;
				const double beatLength = ParseDouble(parts[1], 0.0);
				if (beatLength <= 0) continue; // inherited point: scroll speed only
				const int32 meter = parts.size() > 2 ? ParseIntTrunc(parts[2], 4) : 4;
				points.push_back({ time, beatLength, std::max(1, meter) });
			}
			std::stable_sort(points.begin(), points.end(), [](const TimingPoint& a, const TimingPoint& b) { return a.time < b.time; });

			// Two tempo marks on the same millisecond: the later one wins, as it does in osu!.
			std::vector<TimingPoint> deduped;
			for (const auto& p : points)
			{
				if (!deduped.empty() && std::abs(deduped.back().time - p.time) < 1e-6) deduped.back() = p;
				else deduped.push_back(p);
			}
			return deduped;
		}

		std::vector<HitObject> ParseHitObjects(const std::vector<std::string>& lines, int32 keys, int32 shift)
		{
			std::vector<HitObject> objects;
			for (const auto& line : lines)
			{
				const auto parts = SplitComma(line);
				if (parts.size() < 4) continue;
				const int32 x = ParseIntTrunc(parts[0], 0);
				const double time = ParseDouble(parts[2], 0.0) + shift;
				const int32 typeFlags = ParseIntTrunc(parts[3], 0);
				const int32 column = std::min(keys - 1, std::max(0, static_cast<int32>(std::floor(static_cast<double>(x) * keys / 512.0))));

				HitObject obj{ time, column, none };
				if (typeFlags & 128) // mania hold
				{
					const std::string extras = parts.size() > 5 ? parts[5] : "";
					const std::string endRaw = extras.substr(0, extras.find(':'));
					const double endValue = ParseDouble(endRaw, 0.0);
					if (endValue > 0) obj.endTime = endValue + shift;
				}
				objects.push_back(obj);
			}
			std::stable_sort(objects.begin(), objects.end(), [](const HitObject& a, const HitObject& b)
			{
				return a.time != b.time ? a.time < b.time : a.column < b.column;
			});
			return objects;
		}

		Beatmap ParseOsu(FilePathView path)
		{
			const std::string text = DecodeToUtf8(ReadAllBytes(path));
			Beatmap beatmap;
			{
				static const std::regex versionRe{ R"(osu file format v(\d+))", std::regex::icase };
				std::smatch m;
				if (std::regex_search(text, m, versionRe)) beatmap.formatVersion = std::stoi(m[1].str());
			}
			const int32 shift = beatmap.formatVersion <= kEarlyVersionMax ? kEarlyVersionTimingOffset : 0;

			static const std::regex sectionRe{ R"(^\[([A-Za-z0-9]+)\]\s*$)" };
			std::string section;
			std::map<std::string, std::string> general, metadata, difficulty;
			std::vector<std::string> timingLines, objectLines, eventLines;

			for (const auto& line : SplitLines(text))
			{
				const std::string stripped = StripComment(line);
				if (stripped.empty()) continue;
				std::smatch m;
				if (std::regex_match(stripped, m, sectionRe))
				{
					section = ToLowerAscii(m[1].str());
					continue;
				}
				if (section == "general" || section == "metadata" || section == "difficulty")
				{
					const size_t colon = stripped.find(':');
					const std::string key = Trim(std::string_view{ stripped }.substr(0, colon));
					const std::string value = colon == std::string::npos ? "" : Trim(std::string_view{ stripped }.substr(colon + 1));
					auto& target = section == "general" ? general : section == "metadata" ? metadata : difficulty;
					target[key] = value;
				}
				else if (section == "timingpoints") timingLines.push_back(stripped);
				else if (section == "hitobjects") objectLines.push_back(stripped);
				else if (section == "events") eventLines.push_back(stripped);
			}

			const auto get = [](const std::map<std::string, std::string>& m, const char* key) -> std::string
			{
				const auto it = m.find(key);
				return it == m.end() ? std::string{} : it->second;
			};

			beatmap.mode = ParseIntTrunc(get(general, "Mode"), kManiaMode);
			if (beatmap.mode != kManiaMode)
			{
				throw std::runtime_error("\"" + FileNameUtf8(path) + "\" is not an osu!mania beatmap");
			}
			beatmap.audioFilename = get(general, "AudioFilename");
			beatmap.previewTime = ParseIntTrunc(get(general, "PreviewTime"), -1);
			if (shift && beatmap.previewTime >= 0) beatmap.previewTime += shift;

			beatmap.title = get(metadata, "Title");
			beatmap.titleUnicode = get(metadata, "TitleUnicode");
			beatmap.artist = get(metadata, "Artist");
			beatmap.artistUnicode = get(metadata, "ArtistUnicode");
			beatmap.creator = get(metadata, "Creator");
			beatmap.version = get(metadata, "Version");
			beatmap.keys = std::max(1, ParseIntTrunc(get(difficulty, "CircleSize"), 4));
			beatmap.background = ParseBackground(eventLines);
			beatmap.timingPoints = ParseTimingPoints(timingLines, shift);
			beatmap.hitObjects = ParseHitObjects(objectLines, beatmap.keys, shift);

			if (beatmap.hitObjects.empty()) throw std::runtime_error("\"" + FileNameUtf8(path) + "\" has no notes");
			if (beatmap.timingPoints.empty()) throw std::runtime_error("\"" + FileNameUtf8(path) + "\" has no uninherited timing point");
			return beatmap;
		}

		struct Segment
		{
			double time;
			int64 pulse;
			double bpm;
			int32 meter;
		};

		// Maps osu! absolute times onto the kson pulse grid. Pulse 0 sits on a downbeat at or before the
		// first object; note times are converted relative to their own tempo segment so rounding does
		// not accumulate across tempo changes.
		class OsuTiming
		{
		public:
			int32 offsetMs = 0;
			std::vector<Segment> segments;
			std::vector<std::tuple<int64, int32, int32>> timeSigs;

			explicit OsuTiming(const Beatmap& beatmap)
			{
				const auto& points = beatmap.timingPoints;
				const auto& first = points.front();
				double earliestObject = beatmap.hitObjects.front().time;
				for (const auto& obj : beatmap.hitObjects) earliestObject = std::min(earliestObject, obj.time);
				const double earliest = std::min(earliestObject, first.time);

				const double measureMs = first.beatLength * first.meter;
				const double lead = first.time - earliest;
				const int64 measuresBack = (lead > 0 && measureMs > 0) ? static_cast<int64>(std::ceil(lead / measureMs)) : 0;
				const double origin = first.time - static_cast<double>(measuresBack) * measureMs;

				offsetMs = static_cast<int32>(RoundHalfEven(origin));
				segments.push_back({ origin, 0, first.bpm(), first.meter });
				for (size_t i = 1; i < points.size(); ++i)
				{
					const auto& point = points[i];
					const Segment previous = segments.back();
					const double raw = static_cast<double>(previous.pulse) + (point.time - previous.time) * previous.bpm / 60000.0 * kResolution;
					const auto [pulse, exact] = SnapPulse(raw);
					if (pulse <= previous.pulse)
					{
						segments.back() = { point.time, previous.pulse, point.bpm(), point.meter };
						continue;
					}
					segments.push_back({ point.time, pulse, point.bpm(), point.meter });
				}
				buildTimeSigs();
			}

			int64 pulse(double time) const
			{
				const Segment* chosen = &segments.front();
				for (const auto& segment : segments)
				{
					if (segment.time <= time + 1e-6) chosen = &segment;
					else break;
				}
				const double raw = static_cast<double>(chosen->pulse) + (time - chosen->time) * chosen->bpm / 60000.0 * kResolution;
				return std::max<int64>(0, SnapPulse(raw).first);
			}

		private:
			void buildTimeSigs()
			{
				int32 meter = segments.front().meter;
				timeSigs.emplace_back(0, meter, 4);
				int64 measureIndex = 0;
				int64 measureStart = 0;
				for (size_t i = 1; i < segments.size(); ++i)
				{
					const auto& segment = segments[i];
					if (segment.meter == meter) continue;
					const int64 measurePulses = kResolution * meter;
					while (measureStart + measurePulses <= segment.pulse)
					{
						measureStart += measurePulses;
						++measureIndex;
					}
					if (measureStart != segment.pulse && segment.pulse - measureStart > measurePulses / 2)
					{
						// kson can only change time signature on a bar line.
						measureStart += measurePulses;
						++measureIndex;
					}
					meter = segment.meter;
					timeSigs.emplace_back(measureIndex, meter, 4);
				}
			}
		};

		Optional<int32> ExplicitLevel(const std::string& name)
		{
			static const std::array<std::regex, 3> patterns{
				std::regex{ R"(\blv\.?\s*(\d{1,2})\b)", std::regex::icase },
				std::regex{ R"(\blevel\s*(\d{1,2})\b)", std::regex::icase },
				std::regex{ R"(\[(\d{1,2})\])" },
			};
			for (const auto& re : patterns)
			{
				std::smatch m;
				if (std::regex_search(name, m, re))
				{
					const int32 value = std::stoi(m[1].str());
					if (1 <= value && value <= 20) return value;
				}
			}
			return none;
		}

		Optional<int32> KeywordRank(const std::string& name)
		{
			static const std::vector<std::pair<std::string, int32>> keywords{
				{ "beginner", 0 }, { "ez", 0 }, { "easy", 0 },
				{ "basic", 1 }, { "nm", 1 }, { "normal", 1 },
				{ "advanced", 2 }, { "hd", 2 }, { "hard", 2 },
				{ "hyper", 3 }, { "mx", 3 }, { "insane", 3 }, { "another", 3 },
				{ "sc", 4 }, { "expert", 4 }, { "extra", 4 }, { "exhaust", 4 }, { "extreme", 4 },
				{ "lunatic", 5 }, { "maximum", 5 },
				{ "overjoy", 6 }, { "gravity", 6 }, { "heavenly", 6 },
			};
			const std::string lowered = ToLowerAscii(name);
			Optional<int32> best;
			for (const auto& [keyword, rank] : keywords)
			{
				if (lowered.find(keyword) != std::string::npos && (!best || rank > *best)) best = rank;
			}
			return best;
		}

		int32 SlotForLevel(int32 level)
		{
			if (level <= 6) return 0;
			if (level <= 12) return 1;
			if (level <= 16) return 2;
			return 3;
		}
	}

	bool IsManiaBeatmapQuick(FilePathView path)
	{
		// [General] comes first in every .osu, so the head of the file is enough.
		std::string head(16384, '\0');
		{
			std::ifstream ifs(std::filesystem::path(Unicode::ToWstring(path)), std::ios::binary);
			if (!ifs) return false;
			ifs.read(head.data(), static_cast<std::streamsize>(head.size()));
			head.resize(static_cast<size_t>(ifs.gcount()));
		}
		static const std::regex modeRe{ R"((^|[\r\n])\s*Mode\s*:\s*(\d+))" };
		std::smatch m;
		if (std::regex_search(head, m, modeRe)) return std::stoi(m[2].str()) == kManiaMode;
		return false; // no Mode line means osu!standard
	}

	GriddedChart ConvertOsu(FilePathView path)
	{
		const Beatmap beatmap = ParseOsu(path);
		const OsuTiming timing{ beatmap };

		GriddedChart chart;
		chart.offsetMs = timing.offsetMs;
		for (const auto& segment : timing.segments) chart.bpms[segment.pulse] = segment.bpm;
		for (const auto& segment : timing.segments)
		{
			if (std::find(chart.displayBpms.begin(), chart.displayBpms.end(), segment.bpm) == chart.displayBpms.end()) chart.displayBpms.push_back(segment.bpm);
		}
		std::sort(chart.displayBpms.begin(), chart.displayBpms.end());
		chart.timeSigs = timing.timeSigs.empty() ? decltype(chart.timeSigs){ { 0, 4, 4 } } : timing.timeSigs;

		const auto mapping = LaneMap(beatmap.keys);
		for (const auto& obj : beatmap.hitObjects)
		{
			if (obj.column >= static_cast<int32>(mapping.size())) continue;
			const int64 start = timing.pulse(obj.time);
			int64 length = 0;
			if (obj.isHold()) length = std::max<int64>(0, timing.pulse(*obj.endTime) - start);
			PlaceNote(chart, mapping[obj.column], start, length);
		}
		ClipHolds(chart);

		// Level and slot: an explicit level in the difficulty name wins; otherwise estimate from density.
		double minTime = beatmap.hitObjects.front().time, maxTime = minTime;
		for (const auto& obj : beatmap.hitObjects)
		{
			minTime = std::min(minTime, obj.time);
			maxTime = std::max(maxTime, obj.time);
		}
		const double spanSec = std::max(1.0, maxTime - minTime) / 1000.0;
		const double nps = static_cast<double>(beatmap.hitObjects.size()) / spanSec;
		const int32 densityLevel = LevelFromDensity(nps);

		if (const auto explicitLevel = ExplicitLevel(beatmap.version))
		{
			chart.level = *explicitLevel;
			chart.slot = SlotForLevel(chart.level);
		}
		else if (const auto keyword = KeywordRank(beatmap.version))
		{
			chart.level = densityLevel;
			chart.slot = *keyword <= 1 ? 0 : *keyword == 2 ? 1 : *keyword == 3 ? 2 : 3;
		}
		else
		{
			chart.level = densityLevel;
			chart.slot = SlotForLevel(chart.level);
		}

		chart.title = beatmap.titleUnicode.empty() ? beatmap.title : beatmap.titleUnicode;
		chart.artist = beatmap.artistUnicode.empty() ? beatmap.artist : beatmap.artistUnicode;
		chart.chartAuthor = beatmap.creator;
		chart.difficultyName = beatmap.version;
		chart.keys = beatmap.keys;
		chart.source = "osu!mania";
		if (!beatmap.titleUnicode.empty() && beatmap.title != beatmap.titleUnicode) chart.titleTranslit = beatmap.title;
		if (!beatmap.artistUnicode.empty() && beatmap.artist != beatmap.artistUnicode) chart.artistTranslit = beatmap.artist;
		chart.audioFilename = beatmap.audioFilename;
		chart.jacketFilename = beatmap.background;
		chart.previewMs = std::max(0, beatmap.previewTime);
		return chart;
	}
}
