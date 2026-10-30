#pragma once
#include <chrono>

// Per-frame timing recorder for diagnosing stutter.
//
// Enabled by framelog=1 in config.ini (normal builds; writes log/frames-<time>.csv on exit) or by
// --frame-log <csv> [seconds] in diagnostics builds (exits after the given time). Each frame stores
// the interval since the previous frame, the time the frame limiter slept, and the scene name, so
// "work" (interval minus sleep) separates slow frames from intentional waiting.
namespace FrameLog
{
	/// Starts recording into csvPath. stopAfterSeconds > 0 writes the log and exits the game after that long.
	void Start(FilePathView csvPath, double stopAfterSeconds = 0.0);

	[[nodiscard]]
	bool Enabled();

	/// Called by FrameRateLimit with the time it spent sleeping this frame.
	void AddSleep(double milliseconds);

	/// Called once per frame after System::Update(). Returns false when a timed run has finished.
	bool EndFrame();

	/// Writes the CSV and a summary next to it. Safe to call more than once.
	void Stop();

	/// Adds time to a named section of the current frame (label must be a string literal).
	void AddSection(const char* label, double milliseconds);

	/// Times the enclosing block as a named section of the current frame. Costs nothing when disabled.
	class Scope
	{
		const char* m_label = nullptr;
		std::chrono::steady_clock::time_point m_start;

	public:
		explicit Scope(const char* label)
		{
			if (Enabled())
			{
				m_label = label;
				m_start = std::chrono::steady_clock::now();
			}
		}
		~Scope()
		{
			if (m_label)
				AddSection(m_label, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_start).count());
		}
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
	};
}
