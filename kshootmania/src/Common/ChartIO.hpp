#pragma once
#include "ForeignChart/ForeignChart.hpp"

// The one place that turns a chart path into chart data, whatever the format
// (.ksh, .kson, .osu, or a "<simfile>|<index>" StepMania/Etterna chart).
namespace ChartIO
{
	[[nodiscard]]
	inline kson::ChartData LoadChartData(FilePathView chartFilePath)
	{
		if (ForeignChart::IsForeignChart(chartFilePath))
		{
			return ForeignChart::LoadChartData(chartFilePath);
		}
		return FsUtils::HasKsonExtension(chartFilePath)
				   ? kson::LoadKsonChartData(chartFilePath.toUTF8())
				   : kson::LoadKshChartData(chartFilePath.toUTF8());
	}

	[[nodiscard]]
	inline kson::MetaChartData LoadMetaChartData(FilePathView chartFilePath)
	{
		if (ForeignChart::IsForeignChart(chartFilePath))
		{
			return ForeignChart::LoadMetaChartData(chartFilePath);
		}
		return FsUtils::HasKsonExtension(chartFilePath)
				   ? kson::LoadKsonMetaChartData(chartFilePath.toUTF8())
				   : kson::LoadKshMetaChartData(chartFilePath.toUTF8());
	}

	/// .ksh/.kson charts can be edited in place (auto-sync writes the offset back); foreign ones cannot.
	[[nodiscard]]
	inline bool IsWritable(FilePathView chartFilePath)
	{
		return !ForeignChart::IsForeignChart(chartFilePath);
	}
}
