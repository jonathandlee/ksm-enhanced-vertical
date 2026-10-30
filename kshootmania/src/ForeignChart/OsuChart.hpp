#pragma once
#include "ForeignChartDetail.hpp"

namespace ForeignChart::detail
{
	/// Cheap header scan: true if the .osu file declares osu!mania (Mode: 3).
	bool IsManiaBeatmapQuick(FilePathView path);

	/// Convert one osu!mania beatmap. Throws std::runtime_error on charts that cannot be converted.
	GriddedChart ConvertOsu(FilePathView path);
}
