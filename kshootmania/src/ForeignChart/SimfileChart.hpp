#pragma once
#include "ForeignChartDetail.hpp"

namespace ForeignChart::detail
{
	struct NoteBlock
	{
		std::string stepstype;
		std::string description;
		std::string difficulty;
		int32 meter = 0;
		std::string noteText;
		std::map<std::string, std::string> tags; // .ssc per-chart tags (split timing)
	};

	struct SimfileDocument
	{
		std::map<std::string, std::string> songTags;
		std::vector<NoteBlock> charts;
		bool isSsc = false;
	};

	SimfileDocument ReadSimfile(FilePathView path);

	/// Indices into doc.charts of charts that contain at least one note.
	std::vector<int32> PlayableChartIndices(const SimfileDocument& doc);

	/// Convert one chart of a simfile. Throws std::runtime_error on failure.
	GriddedChart ConvertSimfileChart(const SimfileDocument& doc, int32 chartIndex, FilePathView path);
}
