#pragma once
#include <Siv3D.hpp>
#include <cmath>
#include "Common/CommonDefines.hpp"
#include "Judgment/JudgmentDefines.hpp"

namespace MusicGame
{
	struct TimingHudSettings
	{
		JudgeColor judgeColor = JudgeColor::kFastBlue;
		TimingPosition position = TimingPosition::kUpperPlus;
		TimingMsDisplay display = TimingMsDisplay::kCrit;
		TimingMsColor color = TimingMsColor::kCrit; // ±ms COLOR: when the ms number is colored
		TimingMsColor word = TimingMsColor::kNear;	// FAST/SLOW: when the FAST/SLOW word appears (same thresholds)
	};
	// Saved enum IDs remain stable; menu order is independent of persisted values.
	inline constexpr std::array<TimingMsDisplay, 4> kTimingDisplayChoices{ TimingMsDisplay::kOff, TimingMsDisplay::kNear, TimingMsDisplay::kCrit, TimingMsDisplay::kAll };
	inline constexpr std::array<StringView, 4> kTimingDisplayLabels{ U"OFF", U"NEAR", U"CRIT", U"ALL" };
	// Shared by ±ms COLOR and FAST/SLOW, which use the same thresholds.
	inline constexpr std::array<StringView, 5> kTimingColorLabels{ U"OFF", U"NEAR", U"CRIT", U"±16ms", U"ALL" };
	inline constexpr int32 kTimingThresholdMax = static_cast<int32>(TimingMsColor::kCount) - 1;
	/// Whether a timing error of this size passes a NEAR / CRIT / ±16ms / ALL threshold.
	inline bool MeetsTimingThreshold(TimingMsColor threshold, double signedMs)
	{
		const double magnitude = std::abs(signedMs);
		switch (threshold)
		{
		case TimingMsColor::kNear:
			return magnitude >= Judgment::TimingWindow::ChipNote::kWindowSecCritical * 1000.0;
		case TimingMsColor::kCrit:
			return magnitude > Judgment::kPerfectCriticalWindowMs;
		case TimingMsColor::kSixteenMs:
			return magnitude > 16.0;
		case TimingMsColor::kAll:
			return true;
		default:
			return false;
		}
	}
	/// Whether a hit shows the FAST/SLOW word. NEAR keeps the original judgment-based rule (NEAR and
	/// ERROR results, which also covers promoted FX timing); the others go by the size of the error.
	inline bool ShowsTimingWord(TimingMsColor threshold, Judgment::JudgmentResult result, double signedMs)
	{
		if (threshold == TimingMsColor::kNear)
			return result != Judgment::JudgmentResult::kCritical && signedMs != 0;
		return std::round(signedMs * 10) != 0 && MeetsTimingThreshold(threshold, signedMs);
	}
	inline int32 TimingDisplayCursor(TimingMsDisplay value)
	{
		for (int32 i = 0; i < 4; ++i)
			if (kTimingDisplayChoices[i] == value)
				return i;
		return 0;
	}
	inline TimingMsDisplay TimingDisplayChoice(int32 cursor)
	{
		return kTimingDisplayChoices[Clamp(cursor, 0, 3)];
	}
	inline Color JudgeDirectionColor(JudgeColor palette, bool fast)
	{
		return fast == (palette == JudgeColor::kFastRed) ? Color{ 220, 88, 100 } : Color{ 88, 132, 220 };
	}
	inline Color JudgeTimingColor(JudgeColor palette, double ms)
	{
		return ms == 0 ? Color{ 178, 178, 178 } : JudgeDirectionColor(palette, ms > 0);
	}
	inline String JudgeImage(StringView base, JudgeColor palette)
	{
		String name{ base };
		if (palette == JudgeColor::kFastRed)
			name.replace(U".png", U"_inversion.png");
		return name;
	}
	struct TimingWordFeedback
	{
		double signedMs = 0;
		double displayedAt = 0;
	};
	struct TimingFeedback
	{
		Judgment::JudgmentResult result = Judgment::JudgmentResult::kUnspecified;
		double signedMs = 0;
		double displayedAt = 0;
		Optional<TimingWordFeedback> retainedWord;
	};
	// One candidate per frame; equal magnitudes retain the existing lane processing order.
	class TimingFeedbackState
	{
		Optional<TimingFeedback> m_pending, m_latest;
		Optional<TimingWordFeedback> m_word;

	public:
		void offer(Judgment::JudgmentResult result, Optional<double> ms)
		{
			if (!ms || !std::isfinite(*ms))
				return; // Auto and missed notes have no measured input.
			if (!m_pending || std::abs(*ms) > std::abs(m_pending->signedMs))
				m_pending = TimingFeedback{ result, *ms, 0 };
		}
		// wordThreshold is the FAST/SLOW setting: a hit that passes it becomes the word kept on screen.
		Optional<TimingFeedback> commit(double now, TimingMsColor wordThreshold = TimingMsColor::kNear)
		{
			if (m_pending)
			{
				m_latest = m_pending;
				m_latest->displayedAt = now;
				if (ShowsTimingWord(wordThreshold, m_pending->result, m_pending->signedMs))
					m_word = TimingWordFeedback{ m_pending->signedMs, now };
				m_pending.reset();
			}
			if (m_latest)
				m_latest->retainedWord = m_word;
			return m_latest;
		}
		void clear()
		{
			m_pending.reset();
			m_latest.reset();
			m_word.reset();
		}
	};
	inline bool IsTimingNear(Judgment::JudgmentResult result)
	{
		return result == Judgment::JudgmentResult::kNearFast || result == Judgment::JudgmentResult::kNearSlow;
	}
	struct TimingPresentation
	{
		String word, ms;
		Color color{ 178, 178, 178 }, wordColor{ 178, 178, 178 };
		double alpha = 0, wordAlpha = 0;
	};
	inline constexpr double kTimingDisplaySeconds = 1.1;
	inline constexpr double kTimingFadeSeconds = 0.25;
	inline double TimingOpacity(double age)
	{
		return age < 0 || age >= kTimingDisplaySeconds ? 0 : 0.9 * (age <= kTimingDisplaySeconds - kTimingFadeSeconds ? 1.0 : (kTimingDisplaySeconds - age) / kTimingFadeSeconds);
	}
	inline TimingPresentation PresentTiming(const TimingHudSettings& settings, const TimingFeedback& event, double now)
	{
		TimingPresentation view;
		if (!std::isfinite(event.signedMs))
			return view;
		view.alpha = TimingOpacity(now - event.displayedAt);
		const double magnitude = std::abs(event.signedMs);
		const bool nearRange = magnitude >= Judgment::TimingWindow::ChipNote::kWindowSecCritical * 1000.0;
		const bool critRange = magnitude > Judgment::kPerfectCriticalWindowMs;
		// The kept word comes from TimingFeedbackState::commit, which applies the same FAST/SLOW threshold.
		Optional<TimingWordFeedback> word;
		if (settings.word != TimingMsColor::kOff)
		{
			word = event.retainedWord;
			if (!word && ShowsTimingWord(settings.word, event.result, event.signedMs))
				word = TimingWordFeedback{ event.signedMs, event.displayedAt };
		}
		if (word)
		{
			view.wordAlpha = TimingOpacity(now - word->displayedAt);
			if (view.wordAlpha > 0)
				view.word = word->signedMs > 0 ? U"FAST" : U"SLOW";
			view.wordColor = JudgeDirectionColor(settings.judgeColor, word->signedMs > 0);
		}
		if (view.alpha > 0 && (settings.display == TimingMsDisplay::kAll || (settings.display == TimingMsDisplay::kNear && nearRange) || (settings.display == TimingMsDisplay::kCrit && critRange)))
		{
			view.ms = std::round(event.signedMs * 10) == 0 ? U"0.0ms" : U"{:+.1f}ms"_fmt(event.signedMs);
		}
		// Rounded like the text, so a value shown as "0.0ms" is never colored.
		if (MeetsTimingThreshold(settings.color, event.signedMs) && std::round(event.signedMs * 10) != 0)
			view.color = JudgeDirectionColor(settings.judgeColor, event.signedMs > 0);
		return view;
	}
	inline double TimingPositionY(TimingPosition position)
	{
		constexpr std::array<double, 4> y{ 78, 169, 299, 411 };
		return y[Clamp(static_cast<int32>(position), 0, 3)];
	}
	// BT-C rows: 0 JUDGE COLOR, 1-4 the NEW+ timing rows, then ASSIST TICK (5) .. MOVIE (9).
	inline constexpr int32 kTimingMenuFirstCommonRow = 5;
	inline constexpr int32 kTimingMenuLastRow = 9;
	// Shared by navigation and tests, so disabled settings cannot receive a cursor.
	inline int32 TimingMenuCursor(int32 cursor, int32 /* delta */, bool newMode)
	{
		return Clamp(cursor, newMode ? 0 : kTimingMenuFirstCommonRow, kTimingMenuLastRow);
	}
}
