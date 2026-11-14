#include "DiagnosticRunner.hpp"
#include "DiagnosticArguments.hpp"
#if KSM_ENABLE_DIAGNOSTICS
#include "AudioTimingFixture.hpp"
#include "AudioEndGameFixture.hpp"
#include "ApsVideoFixture.hpp"
#include "TimingHudFixture.hpp"
#include "ExScoreFixture.hpp"
#include "ApsFixture.hpp"
#include "JudgeSettingFixture.hpp"
#include "RenderGeometryFixture.hpp"
#include "LaserRenderFixture.hpp"
#include "ChartLoadFixture.hpp"
#include "UnicodePathFixture.hpp"
#include "ErrorReportFixture.hpp"
#include "SkinPresetFixture.hpp"
#include "Ini/AudioCalibration.hpp"
#include "Vertical/OptionsPresentation.hpp"
#include "ksmaudio/ksmaudio.hpp"

namespace DiagnosticRunner
{
	bool RunAfterAudioInitialization()
	{
		if (ModDiagnostics::Arguments().contains(U"--ex-score-test"))
		{
			try
			{
				ExScoreFixture::Run();
			}
			catch (const Error& error)
			{
				TextWriter{ FsUtils::GetResourcePath(U"ex-score-failure.txt") }.writeln(error);
			}
			Vertical::OptionsPresentation::Reset();
			ksmaudio::Terminate();
			return true;
		}
		// --foreign-chart-dump <folder> <out.jsonl>: load every osu!/StepMania chart under a folder and dump
		// its timing and notes, for comparing against the mania2ksh converter. Writes nothing else.
		if (const auto& args = ModDiagnostics::Arguments(); args.contains(U"--foreign-chart-dump"))
		{
			const auto it = std::find(args.begin(), args.end(), U"--foreign-chart-dump");
			if (std::distance(it, args.end()) >= 3)
			{
				const FilePath root = *(it + 1);
				TextWriter out{ *(it + 2) };
				Array<FilePath> dirs = { root };
				for (const auto& p : FileSystem::DirectoryContents(root, Recursive::Yes))
				{
					if (FileSystem::IsDirectory(p))
					{
						dirs << p;
					}
				}
				for (const auto& dir : dirs)
				{
					for (const auto& chartPath : ForeignChart::ListCharts(dir))
					{
						const kson::ChartData chart = ChartIO::LoadChartData(chartPath);
						nlohmann::json line = nlohmann::json::object();
						line["path"] = chartPath.toUTF8();
						line["error"] = static_cast<int>(chart.error);
						if (chart.error == kson::ErrorType::None)
						{
							line["title"] = chart.meta.title;
							line["level"] = chart.meta.level;
							line["difficulty"] = chart.meta.difficulty.idx;
							line["offset"] = chart.audio.bgm.offset;
							line["audio"] = chart.audio.bgm.filename;
							line["jacket"] = chart.meta.jacketFilename;
							nlohmann::json bpm = nlohmann::json::array();
							for (const auto& [pulse, value] : chart.beat.bpm)
							{
								bpm.push_back({ pulse, value });
							}
							line["bpm"] = bpm;
							const auto lanes = [](const auto& laneArray)
							{
								nlohmann::json arr = nlohmann::json::array();
								for (const auto& lane : laneArray)
								{
									nlohmann::json l = nlohmann::json::array();
									for (const auto& [pulse, interval] : lane)
									{
										l.push_back({ pulse, interval.length });
									}
									arr.push_back(l);
								}
								return arr;
							};
							line["bt"] = lanes(chart.note.bt);
							line["fx"] = lanes(chart.note.fx);
						}
						out.writeln(Unicode::FromUTF8(line.dump()));
					}
				}
			}
			Vertical::OptionsPresentation::Reset();
			ksmaudio::Terminate();
			return true;
		}
		if (ModDiagnostics::Arguments().contains(U"--timing-hud-smoke-test"))
		{
			try
			{
				TimingHudFixture::Run();
			}
			catch (const Error& error)
			{
				TextWriter{ FsUtils::GetResourcePath(U"timing-hud-failure.txt") }.writeln(error);
			}
			Vertical::OptionsPresentation::Reset();
			ksmaudio::Terminate();
			return true;
		}
		if (ModDiagnostics::Arguments().contains(U"--audio-timing-smoke-test"))
		{
			try
			{
				AudioTimingFixture::Run();
			}
			catch (const Error& error)
			{
				TextWriter{ FileSystem::PathAppend(FsUtils::AppDataDirectoryPath(), U"audio-timing-failure.txt") }.writeln(error);
			}
			Vertical::OptionsPresentation::Reset();
			ksmaudio::Terminate();
			return true;
		}
		if (ModDiagnostics::Arguments().contains(U"--audio-calibration-smoke-test"))
		{
			AudioCalibration::RunFixtureTests();
			Vertical::OptionsPresentation::Reset();
			ksmaudio::Terminate();
			return true;
		}
		return false;
	}

	bool RunAfterAssetRegistration()
	{
		if (ModDiagnostics::Arguments().contains(U"--unicode-path-test"))
		{
			UnicodePathFixture::Run();
			Vertical::OptionsPresentation::Reset();
			ksmaudio::Terminate();
			return true;
		}
		if (ModDiagnostics::Arguments().contains(U"--chart-load-test"))
		{
			ChartLoadFixture::Run();
			Vertical::OptionsPresentation::Reset();
			ksmaudio::Terminate();
			return true;
		}

		if (ModDiagnostics::Arguments().contains(U"--render-context-test"))
		{
			try
			{
				LaserRenderFixture::VerifyRenderContexts();
			}
			catch (const Error& error)
			{
				TextWriter{ FsUtils::GetResourcePath(U"render-context-failure.txt") }.writeln(error);
			}
			Vertical::OptionsPresentation::Reset();
			ksmaudio::Terminate();
			return true;
		}
		if (ModDiagnostics::Arguments().contains(U"--laser-range-test"))
		{
			try
			{
				LaserRenderFixture::VerifyRanges();
			}
			catch (const Error& error)
			{
				TextWriter{ FsUtils::GetResourcePath(U"laser-range-failure.txt") }.writeln(error);
			}
			Vertical::OptionsPresentation::Reset();
			ksmaudio::Terminate();
			return true;
		}
		if (ModDiagnostics::Arguments().contains(U"--laser-render-test"))
		{
			LaserRenderFixture::Run();
			Vertical::OptionsPresentation::Reset();
			ksmaudio::Terminate();
			return true;
		}

		if (RenderGeometryFixture::Requested())
		{
			RenderGeometryFixture::Run();
			Vertical::OptionsPresentation::Reset();
			ksmaudio::Terminate();
			return true;
		}
		if (SkinFixture::Enabled() && ModDiagnostics::Arguments().contains(U"--skin-core-test"))
		{
			SkinFixture::Core();
			Vertical::OptionsPresentation::Reset();
			ksmaudio::Terminate();
			return true;
		}
		return false;
	}

	bool RunAfterInputInitialization()
	{
		if (ModDiagnostics::Arguments().contains(U"--error-fatal-test"))
		{
			ErrorReportFixture::RequireIsolated();
			throw std::out_of_range{ "invalid map<K, T> key" };
		}
		if (ModDiagnostics::Arguments().contains(U"--error-report-test"))
		{
			try
			{
				ErrorReportFixture::Run();
			}
			catch (const Error& error)
			{
				TextWriter{ FsUtils::GetResourcePath(U"error-report-failure.txt") }.writeln(error.what());
			}
			Vertical::OptionsPresentation::Reset();
			ksmaudio::Terminate();
			return true;
		}
		if (ModDiagnostics::Arguments().contains(U"--judge-setting-test"))
		{
			try
			{
				JudgeSettingFixture::Run();
			}
			catch (const Error& error)
			{
				TextWriter{ FsUtils::GetResourcePath(U"judge-setting-failure.txt") }.writeln(error);
			}
			Vertical::OptionsPresentation::Reset();
			ksmaudio::Terminate();
			return true;
		}
		if (ModDiagnostics::Arguments().contains(U"--aps-video-fixture"))
		{
			try
			{
				ApsVideoFixture::Run();
			}
			catch (const Error& error)
			{
				TextWriter{ FsUtils::GetResourcePath(U"aps-video-failure.txt") }.writeln(error);
			}
			Vertical::OptionsPresentation::Reset();
			ksmaudio::Terminate();
			return true;
		}
		if (ModDiagnostics::Arguments().contains(U"--aps-test"))
		{
			try
			{
				ApsFixture::Run();
			}
			catch (const Error& error)
			{
				TextWriter{ FsUtils::GetResourcePath(U"aps-failure.txt") }.writeln(error);
			}
			Vertical::OptionsPresentation::Reset();
			ksmaudio::Terminate();
			return true;
		}
		if (ModDiagnostics::Arguments().contains(U"--audio-end-game-test"))
		{
			try
			{
				AudioEndGameFixture::Run();
			}
			catch (const Error& error)
			{
				TextWriter{ FsUtils::GetResourcePath(U"audio-end-game-failure.txt") }.writeln(error);
			}
			Vertical::OptionsPresentation::Reset();
			ksmaudio::Terminate();
			return true;
		}
		return false;
	}
}

#else
namespace DiagnosticRunner
{
	bool RunAfterAudioInitialization()
	{
		return false;
	}
	bool RunAfterAssetRegistration()
	{
		return false;
	}
	bool RunAfterInputInitialization()
	{
		return false;
	}
}
#endif
