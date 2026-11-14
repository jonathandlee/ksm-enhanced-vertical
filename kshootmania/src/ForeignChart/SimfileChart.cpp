// StepMania / Etterna (.sm, .ssc) reader. A port of mania2ksh/simfile.py and mania2ksh/etterna.py.
//
// Three conventions that silently desync a chart if wrong:
//  1. Beat 0 is at audio time -#OFFSET. KSM wants the audio time of pulse 0, so offset = -OFFSET*1000.
//  2. StepMania stops add real time; KSM's stop only freezes the scroll. Stops (and delays and warps)
//     are therefore expressed as tempo changes that make each span last exactly as long as it should.
//  3. A measure is always four beats; #TIMESIGNATURES only moves bar lines and is ignored.
//
// Beats are kept as int64 ticks of 1/604800 beat (240 pulses x 2520), which represents every row
// subdivision simfiles use exactly, so pulse exactness checks match the Python Fraction arithmetic.
#include "SimfileChart.hpp"
#include <algorithm>
#include <regex>
#include <set>

namespace ForeignChart::detail
{
	namespace
	{
		constexpr int64 kTicksPerPulse = 2520;
		constexpr int64 kTicksPerBeat = kResolution * kTicksPerPulse; // 604800

		constexpr double kMaxSynthBpm = 60000.0;
		constexpr double kMinSynthBpm = 0.01;
		constexpr int64 kWarpCorrectionTicks = kTicksPerBeat;  // 1 beat
		constexpr int64 kStopWindowTicks = kTicksPerBeat / 48; // a 192nd note
		constexpr double kDefaultBpm = 120.0;

		const std::map<std::string, int32> kStepstypeKeys{
			{ "dance-single", 4 },
			{ "dance-double", 8 },
			{ "dance-couple", 8 },
			{ "dance-routine", 8 },
			{ "dance-solo", 6 },
			{ "dance-threepanel", 3 },
			{ "pump-single", 5 },
			{ "pump-halfdouble", 6 },
			{ "pump-double", 10 },
			{ "pump-couple", 10 },
			{ "kb7-single", 7 },
			{ "ez2-single", 5 },
			{ "ez2-double", 10 },
			{ "ez2-real", 7 },
			{ "para-single", 5 },
			{ "techno-single4", 4 },
			{ "techno-single5", 5 },
			{ "techno-single8", 8 },
			{ "techno-double4", 8 },
			{ "techno-double5", 10 },
			{ "bm-single5", 6 },
			{ "bm-single7", 8 },
			{ "bm-double5", 12 },
			{ "bm-double7", 16 },
			{ "maniax-single", 4 },
			{ "maniax-double", 8 },
			{ "popn-five", 5 },
			{ "popn-nine", 9 },
		};

		double TicksToBeats(int64 ticks)
		{
			return static_cast<double>(ticks) / kTicksPerBeat;
		}

		int64 BeatsToTicks(double beats)
		{
			return static_cast<int64>(std::llround(beats * kTicksPerBeat));
		}

		std::pair<int64, bool> TicksToPulse(int64 ticks)
		{
			if (ticks % kTicksPerPulse == 0)
				return { ticks / kTicksPerPulse, true };
			return SnapPulse(static_cast<double>(ticks) / kTicksPerPulse);
		}

		std::string StripComments(const std::string& text)
		{
			std::string out;
			out.reserve(text.size());
			for (size_t i = 0; i < text.size(); ++i)
			{
				if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/')
				{
					while (i < text.size() && text[i] != '\r' && text[i] != '\n')
						++i;
					if (i < text.size())
						out.push_back(text[i]);
					continue;
				}
				out.push_back(text[i]);
			}
			return out;
		}

		std::string ToUpperAscii(std::string s)
		{
			for (auto& c : s)
				if (c >= 'a' && c <= 'z')
					c = static_cast<char>(c - 'a' + 'A');
			return s;
		}

		NoteBlock ParseSmNotes(const std::string& value)
		{
			std::vector<std::string> parts;
			size_t start = 0;
			for (size_t i = 0; i <= value.size(); ++i)
			{
				if (i == value.size() || value[i] == ':')
				{
					parts.push_back(value.substr(start, i - start));
					start = i + 1;
				}
			}
			while (parts.size() < 6)
				parts.emplace_back();
			NoteBlock block;
			block.stepstype = Trim(parts[0]);
			block.description = Trim(parts[1]);
			block.difficulty = Trim(parts[2]);
			block.meter = ParseIntTrunc(parts[3], 0);
			block.noteText = parts[5];
			return block;
		}

		// "beat=value,beat=value" as used by BPMS, STOPS, DELAYS and WARPS. Sorted by beat (stable).
		std::vector<std::pair<int64, double>> ParseBeatValues(const std::string& raw)
		{
			std::vector<std::pair<int64, double>> out;
			std::string cleaned;
			for (const char c : raw)
				if (c != '\r' && c != '\n')
					cleaned.push_back(c);
			size_t start = 0;
			for (size_t i = 0; i <= cleaned.size(); ++i)
			{
				if (i != cleaned.size() && cleaned[i] != ',')
					continue;
				const std::string chunk = Trim(std::string_view{ cleaned }.substr(start, i - start));
				start = i + 1;
				const size_t eq = chunk.find('=');
				if (chunk.empty() || eq == std::string::npos)
					continue;
				const double beat = ParseDouble(std::string_view{ chunk }.substr(0, eq), std::numeric_limits<double>::quiet_NaN());
				const double value = ParseDouble(std::string_view{ chunk }.substr(eq + 1), std::numeric_limits<double>::quiet_NaN());
				if (std::isnan(beat) || std::isnan(value))
					continue;
				out.emplace_back(BeatsToTicks(beat), value);
			}
			std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b)
							 {
								 return a.first < b.first;
							 });
			return out;
		}

		// StepMania's own beat-to-time mapping, kept separate from the conversion so the tempo map can be
		// solved against it.
		class SimfileTiming
		{
		public:
			double offset = 0.0; // raw #OFFSET; beat 0 at -offset seconds
			std::vector<std::pair<int64, double>> bpms;
			std::vector<std::pair<int64, double>> stops;
			std::vector<std::pair<int64, double>> delays;
			std::vector<std::pair<int64, int64>> warpRanges; // [start, end) ticks

			SimfileTiming(double offsetValue,
						  std::vector<std::pair<int64, double>> bpmValues,
						  std::vector<std::pair<int64, double>> stopValues,
						  std::vector<std::pair<int64, double>> delayValues,
						  std::vector<std::pair<int64, double>> warpValues)
				: offset(offsetValue), bpms(std::move(bpmValues))
			{
				if (bpms.empty())
					bpms.emplace_back(0, kDefaultBpm);
				if (bpms.front().first > 0)
					bpms.insert(bpms.begin(), { 0, bpms.front().second });
				for (auto& [beat, bpm] : bpms)
				{
					// Old simfiles used negative tempos as warps; the magnitude keeps the chart playable.
					if (bpm <= 0)
						bpm = bpm != 0 ? std::abs(bpm) : kDefaultBpm;
				}

				// A negative stop winds the chart back, which is how a warp was spelled before #WARPS.
				std::vector<std::pair<int64, int64>> intervals;
				for (const auto& [beat, seconds] : stopValues)
				{
					if (seconds < 0)
					{
						const double beatsBack = -seconds * bpmAt(beat) / 60.0;
						intervals.emplace_back(beat, beat + BeatsToTicks(beatsBack));
					}
					else if (seconds > 0)
					{
						stops.emplace_back(beat, seconds);
					}
				}
				for (const auto& [beat, seconds] : delayValues)
				{
					if (seconds > 0)
						delays.emplace_back(beat, seconds);
				}
				for (const auto& [beat, length] : warpValues)
				{
					if (length > 0)
						intervals.emplace_back(beat, beat + BeatsToTicks(length));
				}
				std::sort(intervals.begin(), intervals.end());
				for (const auto& [s, e] : intervals)
				{
					if (e <= s)
						continue;
					if (!warpRanges.empty() && s <= warpRanges.back().second)
						warpRanges.back().second = std::max(warpRanges.back().second, e);
					else
						warpRanges.emplace_back(s, e);
				}

				for (const auto& [beat, seconds] : stops)
					m_stopAt[beat] = seconds;
				for (const auto& [beat, seconds] : delays)
					m_delayAt[beat] = seconds;
				buildCache();
			}

			double bpmAt(int64 beat) const
			{
				double bpm = bpms.front().second;
				for (const auto& [segBeat, segBpm] : bpms)
				{
					if (segBeat <= beat)
						bpm = segBpm;
					else
						break;
				}
				return bpm;
			}

			bool isWarped(int64 beat) const
			{
				for (const auto& [s, e] : warpRanges)
					if (s <= beat && beat < e)
						return true;
				return false;
			}

			int64 unwarpedTicks(int64 start, int64 end) const
			{
				if (end <= start)
					return 0;
				int64 total = end - start;
				for (const auto& [ws, we] : warpRanges)
				{
					const int64 overlap = std::min(end, we) - std::max(start, ws);
					if (overlap > 0)
						total -= overlap;
				}
				return std::max<int64>(0, total);
			}

			double timeAt(int64 beat) const
			{
				const auto it = std::upper_bound(m_marks.begin(), m_marks.end(), beat);
				if (it == m_marks.begin())
				{
					return -offset + TicksToBeats(beat) * 60.0 / bpmAt(0);
				}
				const size_t index = static_cast<size_t>(std::distance(m_marks.begin(), it)) - 1;
				const int64 mark = m_marks[index];
				if (beat == mark)
					return m_arrival[index];
				return m_departure[index] + TicksToBeats(unwarpedTicks(mark, beat)) * 60.0 / bpmAt(mark);
			}

		private:
			std::map<int64, double> m_stopAt;
			std::map<int64, double> m_delayAt;
			std::vector<int64> m_marks;
			std::vector<double> m_arrival;
			std::vector<double> m_departure;

			void buildCache()
			{
				std::set<int64> marks{ 0 };
				for (const auto& [b, _] : bpms)
					marks.insert(b);
				for (const auto& [b, _] : stops)
					marks.insert(b);
				for (const auto& [b, _] : delays)
					marks.insert(b);
				for (const auto& [s, e] : warpRanges)
				{
					marks.insert(s);
					marks.insert(e);
				}
				for (const int64 m : marks)
					if (m >= 0)
						m_marks.push_back(m);

				double clock = -offset;
				for (size_t i = 0; i < m_marks.size(); ++i)
				{
					const int64 mark = m_marks[i];
					if (i > 0)
					{
						const int64 previous = m_marks[i - 1];
						clock += TicksToBeats(unwarpedTicks(previous, mark)) * 60.0 / bpmAt(previous);
					}
					// A delay pauses before its own beat; a stop pauses after it.
					if (const auto d = m_delayAt.find(mark); d != m_delayAt.end())
						clock += d->second;
					m_arrival.push_back(clock);
					double departure = clock;
					if (const auto s = m_stopAt.find(mark); s != m_stopAt.end())
						departure += s->second;
					m_departure.push_back(departure);
					clock = departure;
				}
			}
		};

		// Solve each span between slope changes for the tempo that ends it at the right absolute time.
		std::map<int64, double> BuildTempoMap(const SimfileTiming& timing)
		{
			std::set<int64> marks{ 0 };
			for (const auto& [b, _] : timing.bpms)
				marks.insert(b);
			for (const auto& [b, _] : timing.stops)
			{
				marks.insert(b);
				marks.insert(b + kStopWindowTicks);
			}
			for (const auto& [b, _] : timing.delays)
			{
				marks.insert(std::max<int64>(0, b - kStopWindowTicks));
				marks.insert(b);
			}
			for (const auto& [s, e] : timing.warpRanges)
			{
				marks.insert(s);
				marks.insert(e);
				marks.insert(e + kWarpCorrectionTicks);
			}
			std::vector<int64> ordered;
			for (const int64 m : marks)
				if (m >= 0)
					ordered.push_back(m);

			std::map<int64, double> tempo;
			Optional<double> previousBpm;
			const double origin = timing.timeAt(0);
			double clock = 0.0;

			for (size_t index = 0; index < ordered.size(); ++index)
			{
				const int64 beat = ordered[index];
				const int64 pulse = TicksToPulse(beat).first;
				double bpm;
				if (index + 1 < ordered.size())
				{
					const int64 nextBeat = ordered[index + 1];
					const int64 nextPulse = TicksToPulse(nextBeat).first;
					const int64 spanPulses = nextPulse - pulse;
					if (spanPulses <= 0)
						continue;
					const double available = (timing.timeAt(nextBeat) - origin) - clock;
					if (available <= 0)
						bpm = kMaxSynthBpm;
					else
						bpm = std::min(kMaxSynthBpm, std::max(kMinSynthBpm, static_cast<double>(spanPulses) / (4.0 * available)));
					clock += static_cast<double>(spanPulses) / 4.0 / bpm;
				}
				else
				{
					bpm = timing.bpmAt(beat);
				}
				if (previousBpm && std::abs(*previousBpm - bpm) < 1e-9)
					continue;
				tempo[pulse] = bpm;
				previousBpm = bpm;
			}
			if (!tempo.contains(0))
				tempo[0] = timing.bpmAt(0);
			return tempo;
		}

		struct ParsedNote
		{
			int64 beat;
			int32 column;
			int64 length; // ticks; 0 = tap
		};

		struct ParsedRows
		{
			std::vector<ParsedNote> notes;
			int32 columns = 0;
		};

		ParsedRows ParseRows(const std::string& noteText)
		{
			ParsedRows result;
			std::map<int32, int64> openHolds;
			std::vector<int32> openOrder;

			size_t measureStart = 0;
			int64 measureIndex = 0;
			for (size_t i = 0; i <= noteText.size(); ++i)
			{
				if (i != noteText.size() && noteText[i] != ',')
					continue;
				const std::string measure = noteText.substr(measureStart, i - measureStart);
				measureStart = i + 1;

				std::vector<std::string> rows;
				size_t lineStart = 0;
				for (size_t j = 0; j <= measure.size(); ++j)
				{
					if (j != measure.size() && measure[j] != '\n' && measure[j] != '\r')
						continue;
					std::string row = Trim(std::string_view{ measure }.substr(lineStart, j - lineStart));
					lineStart = j + 1;
					if (!row.empty())
						rows.push_back(std::move(row));
				}
				if (!rows.empty())
				{
					const int64 rowCount = static_cast<int64>(rows.size());
					for (int64 rowIndex = 0; rowIndex < rowCount; ++rowIndex)
					{
						const std::string& row = rows[rowIndex];
						result.columns = std::max(result.columns, static_cast<int32>(row.size()));
						const int64 within = 4 * kTicksPerBeat * rowIndex;
						const int64 beat = measureIndex * 4 * kTicksPerBeat + (within % rowCount == 0 ? within / rowCount : static_cast<int64>(std::llround(static_cast<double>(within) / rowCount)));
						for (int32 column = 0; column < static_cast<int32>(row.size()); ++column)
						{
							const char ch = static_cast<char>(std::toupper(static_cast<unsigned char>(row[column])));
							if (ch == '1' || ch == 'L')
							{
								result.notes.push_back({ beat, column, 0 });
							}
							else if (ch == '2' || ch == '4')
							{
								if (!openHolds.contains(column))
									openOrder.push_back(column);
								openHolds[column] = beat;
							}
							else if (ch == '3')
							{
								const auto it = openHolds.find(column);
								if (it == openHolds.end())
									continue;
								result.notes.push_back({ it->second, column, beat - it->second });
								openHolds.erase(it);
								std::erase(openOrder, column);
							}
						}
					}
				}
				++measureIndex;
			}
			// A hold with no tail is shortened to a tap.
			for (const int32 column : openOrder)
			{
				if (const auto it = openHolds.find(column); it != openHolds.end())
					result.notes.push_back({ it->second, column, 0 });
			}
			return result;
		}

		std::string SongTag(const SimfileDocument& doc, const NoteBlock& block, const std::string& name, const std::string& fallback = {})
		{
			if (const auto it = block.tags.find(name); it != block.tags.end())
			{
				const std::string own = Trim(it->second);
				if (!own.empty())
					return own;
			}
			if (const auto it = doc.songTags.find(name); it != doc.songTags.end())
			{
				const std::string song = Trim(it->second);
				if (!song.empty())
					return song;
			}
			return fallback;
		}

		int32 SlotForDifficulty(const std::string& name, int32 level)
		{
			const std::string n = ToLowerAscii(name);
			if (n == "beginner" || n == "easy" || n == "basic" || n == "light")
				return 0;
			if (n == "medium" || n == "normal" || n == "another")
				return 1;
			if (n == "hard" || n == "difficult")
				return 2;
			if (n == "challenge" || n == "expert" || n == "smaniac" || n == "edit")
				return 3;
			if (level <= 6)
				return 0;
			if (level <= 12)
				return 1;
			if (level <= 16)
				return 2;
			return 3;
		}
	}

	SimfileDocument ReadSimfile(FilePathView path)
	{
		const std::string text = StripComments(DecodeToUtf8(ReadAllBytes(path)));
		SimfileDocument doc;
		doc.isSsc = FileSystem::Extension(path).lowercased() == U"ssc";
		// Index rather than pointer: doc.charts reallocates as blocks are added.
		Optional<size_t> currentIndex;
		const auto current = [&]() -> NoteBlock*
		{
			return currentIndex ? &doc.charts[*currentIndex] : nullptr;
		};

		size_t pos = 0;
		while (true)
		{
			const size_t hashAt = text.find('#', pos);
			if (hashAt == std::string::npos)
				break;
			const size_t colonAt = text.find(':', hashAt);
			if (colonAt == std::string::npos)
				break;
			const std::string name = ToUpperAscii(Trim(std::string_view{ text }.substr(hashAt + 1, colonAt - hashAt - 1)));
			const size_t semiAt = text.find(';', colonAt);
			const std::string value = semiAt == std::string::npos ? text.substr(colonAt + 1) : text.substr(colonAt + 1, semiAt - colonAt - 1);

			if (name == "NOTEDATA")
			{
				doc.charts.emplace_back();
				currentIndex = doc.charts.size() - 1;
			}
			else if (name == "NOTES" || name == "NOTES2")
			{
				if (doc.isSsc && current() != nullptr)
				{
					NoteBlock& block = *current();
					block.noteText = value;
					const auto tag = [&](const char* key)
					{
						const auto it = block.tags.find(key);
						return it == block.tags.end() ? std::string{} : it->second;
					};
					block.stepstype = tag("STEPSTYPE");
					block.description = tag("DESCRIPTION");
					if (block.description.empty())
						block.description = tag("CHARTNAME");
					block.difficulty = tag("DIFFICULTY");
					block.meter = ParseIntTrunc(tag("METER"), 0);
				}
				else
				{
					// .sm packs six colon-separated fields into the value.
					doc.charts.push_back(ParseSmNotes(value));
				}
			}
			else if (doc.isSsc && current() != nullptr)
			{
				current()->tags[name] = Trim(value);
			}
			else
			{
				doc.songTags[name] = Trim(value);
			}

			if (semiAt == std::string::npos)
				break;
			pos = semiAt + 1;
		}
		return doc;
	}

	std::vector<int32> PlayableChartIndices(const SimfileDocument& doc)
	{
		std::vector<int32> indices;
		for (int32 i = 0; i < static_cast<int32>(doc.charts.size()); ++i)
		{
			const auto& text = doc.charts[i].noteText;
			if (text.find_first_of("124Ll") != std::string::npos)
				indices.push_back(i);
		}
		return indices;
	}

	GriddedChart ConvertSimfileChart(const SimfileDocument& doc, int32 chartIndex, FilePathView path)
	{
		if (chartIndex < 0 || chartIndex >= static_cast<int32>(doc.charts.size()))
		{
			throw std::runtime_error("simfile chart index out of range");
		}
		const NoteBlock& block = doc.charts[chartIndex];
		const ParsedRows rows = ParseRows(block.noteText);

		int32 declared = 0;
		if (const auto it = kStepstypeKeys.find(ToLowerAscii(block.stepstype)); it != kStepstypeKeys.end())
			declared = it->second;
		const int32 keys = rows.columns ? rows.columns : (declared ? declared : 4);

		const double offset = ParseDouble(SongTag(doc, block, "OFFSET", "0"), 0.0);
		std::vector<std::pair<int64, double>> warps = ParseBeatValues(SongTag(doc, block, "WARPS"));
		const SimfileTiming timing{
			offset,
			ParseBeatValues(SongTag(doc, block, "BPMS")),
			ParseBeatValues(SongTag(doc, block, "STOPS")),
			ParseBeatValues(SongTag(doc, block, "DELAYS")),
			std::move(warps),
		};

		GriddedChart chart;
		chart.bpms = BuildTempoMap(timing);
		for (const auto& [_, bpm] : timing.bpms)
		{
			if (std::find(chart.displayBpms.begin(), chart.displayBpms.end(), bpm) == chart.displayBpms.end())
				chart.displayBpms.push_back(bpm);
		}
		std::sort(chart.displayBpms.begin(), chart.displayBpms.end());
		chart.timeSigs = { { 0, 4, 4 } };
		chart.offsetMs = static_cast<int32>(RoundHalfEven(-offset * 1000.0));

		const auto mapping = LaneMap(keys);
		for (const auto& note : rows.notes)
		{
			if (note.column >= static_cast<int32>(mapping.size()))
				continue;
			if (timing.isWarped(note.beat))
				continue; // unhittable in Etterna too
			const int64 start = TicksToPulse(note.beat).first;
			int64 pulses = 0;
			if (note.length > 0)
				pulses = std::max<int64>(0, TicksToPulse(note.beat + note.length).first - start);
			PlaceNote(chart, mapping[note.column], start, pulses);
		}
		ClipHolds(chart);
		if (IsEmpty(chart))
			throw std::runtime_error("simfile chart has no notes");

		const std::string stem = FileSystem::BaseName(path).toUTF8();
		const std::string title = SongTag(doc, block, "TITLE", stem);
		const std::string subtitle = SongTag(doc, block, "SUBTITLE");
		chart.title = subtitle.empty() ? title : Trim(title + " " + subtitle);
		chart.artist = SongTag(doc, block, "ARTIST");
		chart.chartAuthor = block.description.empty() ? SongTag(doc, block, "CREDIT") : block.description;
		chart.difficultyName = block.difficulty.empty() ? block.description : block.difficulty;
		chart.keys = keys;
		chart.source = doc.isSsc ? "Etterna" : "StepMania";
		chart.titleTranslit = SongTag(doc, block, "TITLETRANSLIT");
		chart.artistTranslit = SongTag(doc, block, "ARTISTTRANSLIT");
		chart.audioFilename = SongTag(doc, block, "MUSIC");
		chart.sourceLevel = block.meter;
		if (const auto meterLevel = LevelFromMeter(block.meter))
		{
			chart.level = *meterLevel;
		}
		else
		{
			// No usable #METER: estimate from note density in real time.
			double first = std::numeric_limits<double>::max(), last = std::numeric_limits<double>::lowest();
			int32 count = 0;
			for (const auto& note : rows.notes)
			{
				if (timing.isWarped(note.beat))
					continue;
				const double t = timing.timeAt(note.beat);
				first = std::min(first, t);
				last = std::max(last, t);
				++count;
			}
			chart.level = count > 0 ? LevelFromDensity(count / std::max(1.0, last - first)) : 1;
		}
		chart.slot = SlotForDifficulty(block.difficulty, chart.level);
		chart.previewMs = std::max(0, static_cast<int32>(RoundHalfEven(ParseDouble(SongTag(doc, block, "SAMPLESTART", "0"), 0.0) * 1000.0)));
		for (const char* tag : { "JACKET", "BACKGROUND", "BANNER" })
		{
			const std::string value = SongTag(doc, block, tag);
			if (!value.empty())
			{
				chart.jacketFilename = value;
				break;
			}
		}
		return chart;
	}
}
