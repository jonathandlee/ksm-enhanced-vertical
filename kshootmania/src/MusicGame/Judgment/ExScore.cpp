#include "ExScore.hpp"
#include "ButtonLaneJudgment.hpp"
#include "LaserLaneJudgment.hpp"
#include "Common/FsUtils.hpp"
#include "kson/IO/KshIO.hpp"
namespace MusicGame::Judgment
{
	int64 ChartExMaximum(const kson::ChartData& chart)
	{
		const auto timing = kson::CreateTimingCache(chart.beat);
		int64 maximum = 0;
		const auto button = [&](const auto& lane, Button key)
		{
			ButtonLaneJudgment judgment{ JudgmentPlayMode::kOn, GaugeType::kNormalGauge, FastSlowMode::kHide, key, lane, chart.beat, timing };
			return static_cast<int64>(judgment.chipJudgmentCount()) * 5 + static_cast<int64>(judgment.longJudgmentCount()) * 2;
		};
		for (size_t i = 0; i < chart.note.bt.size(); ++i)
			maximum += button(chart.note.bt[i], static_cast<Button>(kButtonBT_A + i));
		for (size_t i = 0; i < chart.note.fx.size(); ++i)
			maximum += button(chart.note.fx[i], static_cast<Button>(kButtonFX_L + i));
		for (size_t i = 0; i < chart.note.laser.size(); ++i)
		{
			LaserLaneJudgment judgment{ JudgmentPlayMode::kOn, static_cast<int32>(i), chart.note.laser[i], chart.beat, timing };
			maximum += static_cast<int64>(judgment.lineJudgmentCount() + judgment.slamJudgmentCount()) * 2;
		}
		return maximum;
	}
	int64 LoadChartExMaximum(FilePathView path)
	{
		if (ForeignChart::IsForeignChart(path))
			return 0; // EX SCORE is off for osu!/StepMania charts
		try
		{
			const auto chart = FsUtils::HasKsonExtension(path) ? kson::LoadKsonChartData(String{ path }.toUTF8()) : kson::LoadKshChartData(String{ path }.toUTF8());
			if (chart.error != kson::ErrorType::None)
				return 0;
			if (chart.beat.bpm.empty())
			{
				TextWriter{ FsUtils::GetResourcePath(U"bad-charts.txt"), OpenMode::Append }.writeln(U"no bpm\t{}"_fmt(path));
				return 0;
			}
			return ChartExMaximum(chart);
		}
		catch (const std::exception& e)
		{
			TextWriter{ FsUtils::GetResourcePath(U"bad-charts.txt"), OpenMode::Append }.writeln(U"{}\t{}"_fmt(Unicode::FromUTF8(e.what()), path));
			return 0;
		}
	}
}
