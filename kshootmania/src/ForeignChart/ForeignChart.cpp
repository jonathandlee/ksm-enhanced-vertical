#include "ForeignChart.hpp"
#include "ForeignChartDetail.hpp"
#include "OsuChart.hpp"
#include "SimfileChart.hpp"
#include <mutex>
#include <sstream>

namespace ForeignChart
{
	using namespace detail;

	namespace
	{
		struct SplitPath
		{
			FilePath file;
			Optional<int32> index;
		};

		SplitPath Split(FilePathView chartPath)
		{
			const size_t sep = chartPath.indexOf(kVirtualSeparator);
			if (sep == StringView::npos)
				return { FilePath{ chartPath }, none };
			const String indexStr{ chartPath.substr(sep + 1) };
			return { FilePath{ chartPath.substr(0, sep) }, ParseOpt<int32>(indexStr) };
		}

		Format FormatOfFile(FilePathView file)
		{
			const String ext = FileSystem::Extension(file).lowercased();
			if (ext == U"osu")
				return Format::Osu;
			if (ext == U"sm" || ext == U"ssc")
				return Format::Simfile;
			return Format::None;
		}

		// Small cache: listing a folder and then loading each chart of a simfile would otherwise
		// re-read the same file once per chart.
		std::mutex s_docMutex;
		struct CachedDoc
		{
			FilePath path;
			int64 size = -1;
			Optional<DateTime> writeTime;
			std::shared_ptr<const SimfileDocument> doc;
		};
		std::deque<CachedDoc> s_docCache;
		constexpr size_t kDocCacheSize = 16;

		std::shared_ptr<const SimfileDocument> GetSimfile(FilePathView path)
		{
			const int64 size = FileSystem::FileSize(path);
			const Optional<DateTime> writeTime = FileSystem::WriteTime(path);
			{
				std::lock_guard lock{ s_docMutex };
				for (const auto& c : s_docCache)
				{
					if (c.path == path && c.size == size && c.writeTime == writeTime)
						return c.doc;
				}
			}
			auto doc = std::make_shared<const SimfileDocument>(ReadSimfile(path));
			std::lock_guard lock{ s_docMutex };
			s_docCache.push_front({ FilePath{ path }, size, writeTime, doc });
			if (s_docCache.size() > kDocCacheSize)
				s_docCache.pop_back();
			return doc;
		}

		GriddedChart Convert(FilePathView chartPath)
		{
			const auto [file, index] = Split(chartPath);
			switch (FormatOfFile(file))
			{
			case Format::Osu:
				return ConvertOsu(file);
			case Format::Simfile:
			{
				if (!index)
					throw std::runtime_error("simfile path has no chart index");
				const auto doc = GetSimfile(file);
				return ConvertSimfileChart(*doc, *index, file);
			}
			default:
				throw std::runtime_error("not a foreign chart");
			}
		}

		std::string FormatBpm(double bpm)
		{
			if (std::abs(bpm - std::round(bpm)) < 1e-3)
				return std::to_string(static_cast<int64>(std::llround(bpm)));
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%.2f", bpm);
			std::string s = buf;
			while (!s.empty() && s.back() == '0')
				s.pop_back();
			if (!s.empty() && s.back() == '.')
				s.pop_back();
			return s;
		}

		std::string DisplayBpm(const GriddedChart& chart)
		{
			std::vector<double> bpms = chart.displayBpms;
			if (bpms.empty())
				for (const auto& [_, b] : chart.bpms)
					bpms.push_back(b);
			if (bpms.empty())
				return "120";
			const auto [mn, mx] = std::minmax_element(bpms.begin(), bpms.end());
			if (FormatBpm(*mn) == FormatBpm(*mx))
				return FormatBpm(*mn);
			return FormatBpm(*mn) + "-" + FormatBpm(*mx);
		}

		// --- auto-sync offset overrides ---------------------------------------------------------
		std::mutex s_offsetMutex;
		Optional<HashTable<String, int32>> s_offsets;

		FilePath OffsetOverridePath()
		{
			return FileSystem::PathAppend(FsUtils::AppDataDirectoryPath(), U"foreign_offsets.json");
		}

		HashTable<String, int32>& Offsets()
		{
			if (!s_offsets)
			{
				s_offsets.emplace();
				const JSON json = JSON::Load(OffsetOverridePath());
				if (json && json.isObject())
				{
					for (const auto& object : json)
					{
						if (const auto v = object.value.getOpt<int32>())
							s_offsets->emplace(object.key, *v);
					}
				}
			}
			return *s_offsets;
		}

		Optional<int32> OffsetOverride(FilePathView chartPath)
		{
			std::lock_guard lock{ s_offsetMutex };
			const auto& offsets = Offsets();
			if (const auto it = offsets.find(FileSystem::FullPath(chartPath)); it != offsets.end())
				return it->second;
			return none;
		}

		std::string BuildKsonText(const GriddedChart& chart, FilePathView chartPath)
		{
			const FilePath folder = FileSystem::ParentPath(RealFile(chartPath));

			nlohmann::json meta = nlohmann::json::object();
			meta["title"] = chart.difficultyName.empty() ? chart.title : chart.title + " [" + chart.difficultyName + "]";
			meta["artist"] = chart.artist;
			meta["chart_author"] = chart.chartAuthor;
			meta["difficulty"] = chart.slot; // must be an integer, or the chart loses its slot
			meta["level"] = Clamp(chart.level, 1, 20);
			meta["disp_bpm"] = DisplayBpm(chart);
			if (!chart.titleTranslit.empty())
				meta["title_translit"] = chart.titleTranslit;
			if (!chart.artistTranslit.empty())
				meta["artist_translit"] = chart.artistTranslit;
			{
				const FilePath jacket = FindAsset(folder, Unicode::FromUTF8(chart.jacketFilename), kImageSuffixes);
				if (!jacket.isEmpty())
					meta["jacket_filename"] = FileSystem::RelativePath(jacket, folder).toUTF8();
			}
			{
				std::string information = chart.source + " " + std::to_string(chart.keys) + "K";
				if (!chart.difficultyName.empty())
					information += " [" + chart.difficultyName + "]";
				if (chart.sourceLevel)
					information += " lv" + std::to_string(chart.sourceLevel);
				meta["information"] = information;
			}

			const auto laneJson = [](const std::map<int64, int64>& lane)
			{
				nlohmann::json arr = nlohmann::json::array();
				for (const auto& [pulse, length] : lane)
				{
					if (length == 0)
						arr.push_back(pulse);
					else
						arr.push_back(nlohmann::json::array({ pulse, length }));
				}
				return arr;
			};
			nlohmann::json note = nlohmann::json::object();
			note["bt"] = nlohmann::json::array();
			for (const auto& lane : chart.bt)
				note["bt"].push_back(laneJson(lane));
			note["fx"] = nlohmann::json::array();
			for (const auto& lane : chart.fx)
				note["fx"].push_back(laneJson(lane));

			nlohmann::json bgm = nlohmann::json::object();
			{
				const FilePath audio = FindAsset(folder, Unicode::FromUTF8(chart.audioFilename), kAudioSuffixes);
				bgm["filename"] = audio.isEmpty() ? chart.audioFilename : FileSystem::RelativePath(audio, folder).toUTF8();
			}
			bgm["offset"] = chart.offsetMs;
			if (chart.previewMs > 0)
				bgm["preview"] = { { "offset", chart.previewMs }, { "duration", 15000 } };

			nlohmann::json beat = nlohmann::json::object();
			beat["bpm"] = nlohmann::json::array();
			for (const auto& [pulse, bpm] : chart.bpms)
				beat["bpm"].push_back(nlohmann::json::array({ pulse, bpm }));
			const bool plainTimeSig = chart.timeSigs.size() == 1 && chart.timeSigs.front() == std::tuple<int64, int32, int32>{ 0, 4, 4 };
			if (!plainTimeSig)
			{
				beat["time_sig"] = nlohmann::json::array();
				for (const auto& [idx, n, d] : chart.timeSigs)
					beat["time_sig"].push_back(nlohmann::json::array({ idx, nlohmann::json::array({ n, d }) }));
			}

			nlohmann::json doc = nlohmann::json::object();
			doc["format_version"] = 1;
			doc["meta"] = std::move(meta);
			doc["beat"] = std::move(beat);
			doc["note"] = std::move(note);
			doc["audio"] = { { "bgm", std::move(bgm) } };
			return doc.dump();
		}

		void LogFailure(FilePathView chartPath, const std::exception& e)
		{
			Logger << U"[ksm warning] ForeignChart: {} ({})"_fmt(Unicode::FromUTF8(e.what()), chartPath);
		}
	}

	Format FormatOf(FilePathView chartPath)
	{
		return FormatOfFile(Split(chartPath).file);
	}

	FilePath RealFile(FilePathView chartPath)
	{
		return Split(chartPath).file;
	}

	bool Exists(FilePathView chartPath)
	{
		return FileSystem::IsFile(RealFile(chartPath));
	}

	Array<FilePath> ListCharts(FilePathView directoryPath)
	{
		Array<FilePath> files = FileSystem::DirectoryContents(directoryPath, Recursive::No).filter([](FilePathView p)
																								   {
																									   return FileSystem::IsFile(p) && FormatOfFile(p) != Format::None;
																								   });
		files.sort_by([](const FilePath& a, const FilePath& b)
					  {
						  return FileSystem::FileName(a).lowercased() < FileSystem::FileName(b).lowercased();
					  });

		HashSet<String> sscStems;
		for (const auto& f : files)
		{
			if (FileSystem::Extension(f).lowercased() == U"ssc")
				sscStems.insert(FileSystem::BaseName(f).lowercased());
		}

		Array<FilePath> result;
		for (const auto& f : files)
		{
			try
			{
				if (FormatOfFile(f) == Format::Osu)
				{
					if (IsManiaBeatmapQuick(f))
						result.push_back(f);
					continue;
				}
				// Packs often ship both; the .ssc is the newer, richer one.
				if (FileSystem::Extension(f).lowercased() == U"sm" && sscStems.contains(FileSystem::BaseName(f).lowercased()))
					continue;
				const auto doc = GetSimfile(f);
				for (const int32 index : PlayableChartIndices(*doc))
				{
					result.push_back(f + kVirtualSeparator + s3d::Format(index));
				}
			}
			catch (const std::exception& e)
			{
				LogFailure(f, e);
			}
		}
		return result;
	}

	kson::ChartData LoadChartData(FilePathView chartPath)
	{
		try
		{
			const GriddedChart chart = Convert(chartPath);
			std::istringstream stream{ BuildKsonText(chart, chartPath) };
			kson::ChartData chartData = kson::LoadKsonChartData(stream);
			if (chartData.error == kson::ErrorType::None)
			{
				if (const auto offset = OffsetOverride(chartPath))
					chartData.audio.bgm.offset = *offset;
			}
			return chartData;
		}
		catch (const std::exception& e)
		{
			LogFailure(chartPath, e);
			kson::ChartData chartData;
			chartData.error = kson::ErrorType::GeneralChartFormatError;
			return chartData;
		}
	}

	kson::MetaChartData LoadMetaChartData(FilePathView chartPath)
	{
		try
		{
			const GriddedChart chart = Convert(chartPath);
			std::istringstream stream{ BuildKsonText(chart, chartPath) };
			return kson::LoadKsonMetaChartData(stream);
		}
		catch (const std::exception& e)
		{
			LogFailure(chartPath, e);
			kson::MetaChartData chartData;
			chartData.error = kson::ErrorType::GeneralChartFormatError;
			return chartData;
		}
	}

	String ScoreStem(StringView relativeChartPath)
	{
		const size_t sep = relativeChartPath.indexOf(kVirtualSeparator);
		if (sep == StringView::npos)
			return FsUtils::EliminateExtension(relativeChartPath);
		return FsUtils::EliminateExtension(relativeChartPath.substr(0, sep)) + U"[" + String{ relativeChartPath.substr(sep + 1) } + U"]";
	}

	void SaveOffsetOverride(FilePathView chartPath, int32 offset)
	{
		std::lock_guard lock{ s_offsetMutex };
		auto& offsets = Offsets();
		offsets[FileSystem::FullPath(chartPath)] = offset;
		JSON json;
		for (const auto& [key, value] : offsets)
			json[key] = value;
		json.save(OffsetOverridePath());
	}
}
