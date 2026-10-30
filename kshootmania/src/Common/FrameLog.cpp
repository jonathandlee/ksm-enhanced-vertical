#include "FrameLog.hpp"
#include "ksmaudio/Diagnostics.hpp"
#include <algorithm>
#include <chrono>

namespace FrameLog
{
	namespace
	{
		constexpr size_t kMaxSections = 8;
		struct Section
		{
			const char* label = nullptr;
			float ms = 0;
		};
		struct Record
		{
			double timeMs;
			float intervalMs;
			float sleepMs;
			const char* scene;
			std::array<Section, kMaxSections> sections;
		};

		bool g_enabled = false;
		FilePath g_path;
		double g_stopAfterMs = 0.0;
		std::chrono::steady_clock::time_point g_start, g_previous;
		double g_pendingSleepMs = 0.0;
		std::array<Section, kMaxSections> g_pendingSections{};
		Array<Record> g_records;

		double Percentile(Array<float> values, double p)
		{
			if (values.isEmpty())
				return 0.0;
			const size_t index = Min(values.size() - 1, static_cast<size_t>(p * static_cast<double>(values.size() - 1) + 0.5));
			std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
			return values[index];
		}
	}

	void Start(FilePathView csvPath, double stopAfterSeconds)
	{
		g_enabled = true;
		g_path = csvPath;
		g_stopAfterMs = stopAfterSeconds * 1000.0;
		g_start = g_previous = std::chrono::steady_clock::now();
		g_pendingSleepMs = 0.0;
		g_records.clear();
		g_records.reserve(1 << 20);
	}

	bool Enabled()
	{
		return g_enabled;
	}

	void AddSection(const char* label, double milliseconds)
	{
		if (!g_enabled)
			return;
		for (auto& section : g_pendingSections)
		{
			if (section.label == label || section.label == nullptr)
			{
				section.label = label;
				section.ms += static_cast<float>(milliseconds);
				return;
			}
		}
	}

	void AddSleep(double milliseconds)
	{
		if (g_enabled)
			g_pendingSleepMs += milliseconds;
	}

	bool EndFrame()
	{
		if (!g_enabled)
			return true;
		const auto now = std::chrono::steady_clock::now();
		const double interval = std::chrono::duration<double, std::milli>(now - g_previous).count();
		const double sinceStart = std::chrono::duration<double, std::milli>(now - g_start).count();
		g_previous = now;
		g_records.push_back({ sinceStart, static_cast<float>(interval), static_cast<float>(g_pendingSleepMs), ksmaudio::qa::scene.load(std::memory_order_relaxed), g_pendingSections });
		g_pendingSleepMs = 0.0;
		g_pendingSections = {};
		if (g_stopAfterMs > 0.0 && sinceStart >= g_stopAfterMs)
		{
			Stop();
			return false;
		}
		return true;
	}

	void Stop()
	{
		if (!g_enabled)
			return;
		g_enabled = false;
		{
			TextWriter csv{ g_path };
			csv.writeln(U"frame,time_ms,interval_ms,sleep_ms,work_ms,scene");
			for (size_t i = 0; i < g_records.size(); ++i)
			{
				const auto& r = g_records[i];
				csv.writeln(U"{},{:.3f},{:.3f},{:.3f},{:.3f},{}"_fmt(i, r.timeMs, r.intervalMs, r.sleepMs, r.intervalMs - r.sleepMs, Unicode::Widen(r.scene ? r.scene : "")));
			}
		}

		// Summary per scene. The first frame after a scene switch includes loading, so it is reported
		// separately from steady-state frames.
		HashTable<String, Array<float>> intervals, work;
		Array<size_t> slowest;
		for (size_t i = 1; i < g_records.size(); ++i)
		{
			const String scene = Unicode::Widen(g_records[i].scene ? g_records[i].scene : "");
			intervals[scene].push_back(g_records[i].intervalMs);
			work[scene].push_back(g_records[i].intervalMs - g_records[i].sleepMs);
			slowest.push_back(i);
		}
		std::sort(slowest.begin(), slowest.end(), [](size_t a, size_t b)
				  {
					  return g_records[a].intervalMs > g_records[b].intervalMs;
				  });
		TextWriter summary{ FileSystem::PathAppend(FileSystem::ParentPath(g_path), FileSystem::BaseName(g_path) + U"-summary.txt") };
		summary.writeln(U"scene        frames   mean    p50    p95    p99    max   >8.3ms >16.7ms >33ms  | work p99  work max");
		for (const auto& [scene, values] : intervals)
		{
			double sum = 0;
			size_t over8 = 0, over16 = 0, over33 = 0;
			for (const float v : values)
			{
				sum += v;
				over8 += v > 8.33f;
				over16 += v > 16.67f;
				over33 += v > 33.3f;
			}
			summary.writeln(U"{:<12} {:>6} {:>6.2f} {:>6.2f} {:>6.2f} {:>6.2f} {:>6.1f} {:>7} {:>7} {:>5}  | {:>8.2f} {:>9.1f}"_fmt(
				scene, values.size(), sum / values.size(), Percentile(values, .5), Percentile(values, .95), Percentile(values, .99),
				*std::max_element(values.begin(), values.end()), over8, over16, over33, Percentile(work[scene], .99),
				*std::max_element(work[scene].begin(), work[scene].end())));
		}
		// Average time per section over all frames (shows steady per-frame costs, not just spikes)
		HashTable<String, double> sectionTotals;
		for (const auto& r : g_records)
		{
			for (const auto& section : r.sections)
			{
				if (section.label)
					sectionTotals[Unicode::Widen(section.label)] += section.ms;
			}
		}
		summary.writeln(U"\nAverage per frame:");
		for (const auto& [label, total] : sectionTotals)
		{
			summary.writeln(U"  {:<22} {:>8.3f} ms   (total {:>8.1f} ms)"_fmt(label, total / Max<size_t>(1, g_records.size()), total));
		}
		summary.writeln(U"\nSlowest frames:");
		for (size_t n = 0; n < Min<size_t>(30, slowest.size()); ++n)
		{
			const auto& r = g_records[slowest[n]];
			String breakdown;
			for (const auto& section : r.sections)
			{
				if (section.label && section.ms >= 0.5f)
					breakdown += U"  {}={:.1f}"_fmt(Unicode::Widen(section.label), section.ms);
			}
			summary.writeln(U"  frame {:>7} at {:>9.1f} ms  interval {:>7.2f} ms  sleep {:>6.2f}  scene {}{}"_fmt(slowest[n], r.timeMs, r.intervalMs, r.sleepMs, Unicode::Widen(r.scene ? r.scene : ""), breakdown));
		}
	}
}
