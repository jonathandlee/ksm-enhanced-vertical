#pragma once
#include "Diagnostics/DiagnosticArguments.hpp"
#include "MusicGame/Graphics/Highway/Note/ButtonNoteGraphics.hpp"
#include "MusicGame/Graphics/Highway/Note/LaserNoteGraphics.hpp"
#include "MusicGame/Scroll/HighwayScroll.hpp"
#include "MusicGame/HispeedUtils.hpp"
#include "MusicGame/ViewStatus.hpp"
#include "Common/FsUtils.hpp"
#include <chrono>

// --note-render-test: draws BT/FX notes and lasers of every chart in note-fixture-charts/ at fixed times,
// for several hispeed types and both layouts, and writes a pixel hash plus the draw time of each frame.
// Running it on two builds and comparing the hashes shows whether a drawing change altered any pixel.
namespace NoteRenderFixture
{
	inline uint64 HashImage(const Image& image)
	{
		// FNV-1a over the pixel bytes
		uint64 hash = 14695981039346656037ull;
		const auto* bytes = reinterpret_cast<const uint8*>(image.data());
		for (size_t i = 0; i < image.size_bytes(); ++i)
		{
			hash = (hash ^ bytes[i]) * 1099511628211ull;
		}
		return hash;
	}

	inline void Run()
	{
		using namespace MusicGame;
		using namespace MusicGame::Graphics;
		if (!FileSystem::IsFile(FsUtils::GetResourcePath(U"note-render-fixture")))
			throw Error{ U"Note render tests require an isolated runtime" };
		const auto ms = [](auto start)
		{
			return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		};
		TextWriter report{ FsUtils::GetResourcePath(U"note-render.csv") };
		report.writeln(U"chart,hispeed,vertical,sample,time_sec,main_hash,mask_hash,button_ms,laser_ms");
		int64 frames = 0;
		for (const auto& file : FileSystem::DirectoryContents(FsUtils::GetResourcePath(U"note-fixture-charts"), Recursive::No))
		{
			if (!FsUtils::HasChartExtension(file))
				continue;
			// Same preparation as GameMain: expanded laser curves, stops baked into scroll_speed
			auto chart = FsUtils::HasKsonExtension(file) ? kson::LoadKsonChartData(file.toUTF8()) : kson::LoadKshChartData(file.toUTF8());
			if (chart.error != kson::ErrorType::None)
				throw Error{ U"Fixture chart failed: " + file };
			std::array<HashSet<kson::Pulse>, kson::kNumLaserLanesSZ> curves;
			kson::Pulse lastPulse = 0;
			for (size_t lane = 0; lane < curves.size(); ++lane)
				for (auto& [pulse, section] : chart.note.laser[lane])
				{
					for (const auto& [relative, point] : section.v)
						if (!point.curve.isLinear())
							curves[lane].insert(pulse + relative);
					section = kson::ExpandCurveSegments(section, kson::kCurveSubdivisionInterval);
					lastPulse = Max(lastPulse, pulse + section.v.rbegin()->first);
				}
			for (const auto& lane : chart.note.bt)
				if (!lane.empty())
					lastPulse = Max(lastPulse, lane.rbegin()->first + lane.rbegin()->second.length);
			chart.beat.scrollSpeed = kson::ExpandCurveSegments(chart.beat.scrollSpeed, kson::kCurveSubdivisionInterval);
			chart.beat.scrollSpeed = kson::BakeStopIntoScrollSpeed(chart.beat.scrollSpeed, chart.beat.stop);
			const auto timing = kson::CreateTimingCache(chart.beat);
			const double duration = Max(10.0, kson::PulseToSec(lastPulse, chart.beat, timing));
			const String name = FileSystem::BaseName(file);
			for (const HispeedSetting setting : { HispeedSetting{ HispeedType::OMod, 825 }, HispeedSetting{ HispeedType::XMod, 30 }, HispeedSetting{ HispeedType::CMod, 650 } })
				for (const bool vertical : { false, true })
				{
					const GameRender::Scope scope{ GameRender::Context{ vertical ? Size{ 720, 1280 } : Size{ 1280, 720 }, vertical } };
					Scroll::HighwayScroll scroll{ chart };
					ButtonNoteGraphics buttons;
					LaserNoteGraphics laser;
					HighwayRenderTexture target;
					PlayOption option;
					ViewStatus view;
					for (int sample = 0; sample < 96; ++sample)
					{
						const double time = duration * sample / 95;
						GameStatus status;
						status.currentTimeSec = time;
						status.currentPulseDouble = kson::SecToPulseDouble(time, chart.beat, timing);
						status.currentPulse = static_cast<kson::Pulse>(status.currentPulseDouble);
						status.currentBPM = kson::TempoAt(status.currentPulse, chart.beat);
						scroll.update(setting, status.currentBPM);
						if (!System::Update())
							throw Error{ U"Note fixture interrupted" };
						target.additiveTexture().clear(Palette::Black);
						target.invMultiplyTexture().clear(Palette::Black);
						const Scroll::HighwayScrollContext context{ &scroll, &chart.beat, &timing, &status };
						const auto buttonStart = std::chrono::steady_clock::now();
						buttons.draw(chart, status, view, option, context, target);
						const double buttonMs = ms(buttonStart);
						const auto laserStart = std::chrono::steady_clock::now();
						laser.draw(chart, curves, option, status, context, target);
						const double laserMs = ms(laserStart);
						Graphics2D::Flush();
						Image main, mask;
						target.additiveTexture().readAsImage(main);
						target.invMultiplyTexture().readAsImage(mask);
						report.writeln(U"{},{},{},{},{:.4f},{:016x},{:016x},{:.4f},{:.4f}"_fmt(name, HispeedUtils::ToDisplayString(setting), vertical, sample, time, HashImage(main), HashImage(mask), buttonMs, laserMs));
						++frames;
					}
				}
		}
		TextWriter{ FsUtils::GetResourcePath(U"note-render-passed.txt") }.writeln(U"{} frames drawn"_fmt(frames));
	}
}
