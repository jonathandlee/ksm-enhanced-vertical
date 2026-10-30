#include "ForeignChartDetail.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#if SIV3D_PLATFORM(WINDOWS)
#include <Siv3D/Windows/Windows.hpp>
#endif

namespace ForeignChart::detail
{
	const Array<String> kAudioSuffixes = { U"ogg", U"mp3", U"wav", U"flac", U"m4a", U"opus" };
	const Array<String> kImageSuffixes = { U"jpg", U"jpeg", U"png", U"bmp", U"gif" };

	namespace
	{
		// Subdivisions of a beat that charts actually use, coarsest first.
		constexpr std::array<int64, 8> kSnapDivisors{ 1, 2, 3, 4, 6, 8, 12, 16 };
		constexpr int64 kSnapFallbackStep = 5; // 1/48 beat
		constexpr double kSnapTolerance = 3.0;

		constexpr int32 kMaxNativeKeys = 6;

		const std::array<std::vector<Lane>, 7> kSpatialLayouts{ {
			{},
			{ { false, 1 } },
			{ { false, 1 }, { false, 2 } },
			{ { false, 0 }, { false, 1 }, { false, 2 } },
			{ { false, 0 }, { false, 1 }, { false, 2 }, { false, 3 } },
			{ { true, 0 }, { false, 0 }, { false, 1 }, { false, 2 }, { false, 3 } },
			{ { true, 0 }, { false, 0 }, { false, 1 }, { false, 2 }, { false, 3 }, { true, 1 } },
		} };

		bool IsValidUtf8(std::string_view s)
		{
			size_t i = 0;
			while (i < s.size())
			{
				const unsigned char c = static_cast<unsigned char>(s[i]);
				int32 extra;
				if (c < 0x80)
				{
					++i;
					continue;
				}
				else if ((c >> 5) == 0x6)
					extra = 1;
				else if ((c >> 4) == 0xE)
					extra = 2;
				else if ((c >> 3) == 0x1E)
					extra = 3;
				else
					return false;
				for (int32 k = 1; k <= extra; ++k)
				{
					if (i + k >= s.size())
						return false;
					if ((static_cast<unsigned char>(s[i + k]) >> 6) != 0x2)
						return false;
				}
				i += extra + 1;
			}
			return true;
		}

#if SIV3D_PLATFORM(WINDOWS)
		Optional<std::string> DecodeCodePage(const std::string& raw, UINT codePage)
		{
			if (raw.empty())
				return std::string{};
			const int wlen = ::MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, raw.data(), static_cast<int>(raw.size()), nullptr, 0);
			if (wlen <= 0)
				return none;
			std::wstring wide(static_cast<size_t>(wlen), L'\0');
			::MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, raw.data(), static_cast<int>(raw.size()), wide.data(), wlen);
			const int ulen = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, nullptr, 0, nullptr, nullptr);
			std::string out(static_cast<size_t>(ulen), '\0');
			::WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, out.data(), ulen, nullptr, nullptr);
			return out;
		}
#endif

		std::string Latin1ToUtf8(const std::string& raw)
		{
			std::string out;
			out.reserve(raw.size() * 2);
			for (const char ch : raw)
			{
				const unsigned char c = static_cast<unsigned char>(ch);
				if (c < 0x80)
					out.push_back(static_cast<char>(c));
				else
				{
					out.push_back(static_cast<char>(0xC0 | (c >> 6)));
					out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
				}
			}
			return out;
		}
	}

	std::pair<int64, bool> SnapPulse(double raw)
	{
		if (raw < 0)
			raw = 0.0;
		for (const int64 divisor : kSnapDivisors)
		{
			const int64 step = kResolution / divisor;
			const double candidate = RoundHalfEven(raw / static_cast<double>(step)) * static_cast<double>(step);
			if (std::abs(candidate - raw) <= kSnapTolerance)
			{
				return { static_cast<int64>(candidate), true };
			}
		}
		const double candidate = RoundHalfEven(raw / static_cast<double>(kSnapFallbackStep)) * static_cast<double>(kSnapFallbackStep);
		if (std::abs(candidate - raw) <= static_cast<double>(kSnapFallbackStep))
		{
			return { static_cast<int64>(candidate), false };
		}
		return { static_cast<int64>(RoundHalfEven(raw)), false };
	}

	std::vector<Lane> LaneMap(int32 keys)
	{
		if (keys < 1)
			throw std::runtime_error("chart declares no columns");
		if (keys <= kMaxNativeKeys)
			return kSpatialLayouts[keys];
		const auto& six = kSpatialLayouts[kMaxNativeKeys];
		std::vector<Lane> lanes;
		lanes.reserve(keys);
		for (int32 i = 0; i < keys; ++i)
		{
			lanes.push_back(six[std::min(kMaxNativeKeys - 1, i * kMaxNativeKeys / keys)]);
		}
		return lanes;
	}

	void PlaceNote(GriddedChart& chart, const Lane& lane, int64 start, int64 length)
	{
		auto& target = lane.isFx ? chart.fx[lane.index] : chart.bt[lane.index];
		const auto it = target.find(start);
		if (it != target.end())
		{
			if (length > it->second)
				it->second = length;
			++chart.mergedNotes;
		}
		else
		{
			target.emplace(start, length);
		}
	}

	void ClipHolds(GriddedChart& chart)
	{
		const auto clip = [](std::map<int64, int64>& lane)
		{
			for (auto it = lane.begin(); it != lane.end(); ++it)
			{
				const auto next = std::next(it);
				if (next == lane.end())
					break;
				if (it->second > 0 && it->first + it->second > next->first)
				{
					it->second = next->first - it->first;
				}
			}
		};
		for (auto& lane : chart.bt)
			clip(lane);
		for (auto& lane : chart.fx)
			clip(lane);
	}

	bool IsEmpty(const GriddedChart& chart)
	{
		for (const auto& lane : chart.bt)
			if (!lane.empty())
				return false;
		for (const auto& lane : chart.fx)
			if (!lane.empty())
				return false;
		return true;
	}

	std::string DecodeToUtf8(const std::string& raw)
	{
		std::string_view view = raw;
		if (view.size() >= 3 && static_cast<unsigned char>(view[0]) == 0xEF && static_cast<unsigned char>(view[1]) == 0xBB && static_cast<unsigned char>(view[2]) == 0xBF)
		{
			view.remove_prefix(3);
		}
		if (IsValidUtf8(view))
			return std::string{ view };

		// cp1252 rejects only these five bytes; anything else decodes.
		const bool cp1252Ok = std::none_of(raw.begin(), raw.end(), [](char ch)
										   {
											   const unsigned char c = static_cast<unsigned char>(ch);
											   return c == 0x81 || c == 0x8D || c == 0x8F || c == 0x90 || c == 0x9D;
										   });
#if SIV3D_PLATFORM(WINDOWS)
		if (cp1252Ok)
		{
			if (auto s = DecodeCodePage(raw, 1252))
				return *s;
		}
		if (auto s = DecodeCodePage(raw, 932))
			return *s;
		if (auto s = DecodeCodePage(raw, 949))
			return *s;
#else
		(void)cp1252Ok;
#endif
		return Latin1ToUtf8(raw);
	}

	std::string ReadAllBytes(FilePathView path)
	{
		std::ifstream ifs(std::filesystem::path(Unicode::ToWstring(path)), std::ios::binary);
		if (!ifs)
			return {};
		return std::string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
	}

	FilePath FindAsset(FilePathView folder, StringView name, const Array<String>& suffixes)
	{
		const Array<FilePath> files = FileSystem::DirectoryContents(folder, Recursive::No).filter([](FilePathView p)
																								  {
																									  return FileSystem::IsFile(p);
																								  });
		const auto hasSuffix = [&](FilePathView p)
		{
			return suffixes.contains(FileSystem::Extension(p).lowercased());
		};
		if (!name.isEmpty())
		{
			String normalized{ name };
			normalized.replace(U'\\', U'/');
			const FilePath direct = FileSystem::PathAppend(folder, normalized);
			if (FileSystem::IsFile(direct))
				return direct;
			const String lowered = FileSystem::FileName(normalized).lowercased();
			for (const auto& f : files)
			{
				if (FileSystem::FileName(f).lowercased() == lowered)
					return f;
			}
			const String stem = FileSystem::BaseName(normalized).lowercased();
			for (const auto& f : files)
			{
				if (FileSystem::BaseName(f).lowercased() == stem && hasSuffix(f))
					return f;
			}
		}
		FilePath best;
		int64 bestSize = -1;
		for (const auto& f : files)
		{
			if (!hasSuffix(f))
				continue;
			const int64 size = FileSystem::FileSize(f);
			if (size > bestSize)
			{
				best = f;
				bestSize = size;
			}
		}
		return best;
	}

	double ParseDouble(std::string_view s, double fallback)
	{
		const std::string t = Trim(s);
		if (t.empty())
			return fallback;
		double value = 0.0;
		const char* begin = t.data();
		if (*begin == '+')
			++begin;
		const auto [ptr, ec] = std::from_chars(begin, t.data() + t.size(), value);
		if (ec != std::errc{} || ptr == begin)
			return fallback;
		return value;
	}

	int32 ParseIntTrunc(std::string_view s, int32 fallback)
	{
		const double v = ParseDouble(s, std::numeric_limits<double>::quiet_NaN());
		if (std::isnan(v) || std::abs(v) > 2.0e9)
			return fallback;
		return static_cast<int32>(v); // truncates toward zero, like int(float(s))
	}

	std::string Trim(std::string_view s)
	{
		size_t b = 0, e = s.size();
		while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
			++b;
		while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
			--e;
		return std::string{ s.substr(b, e - b) };
	}

	std::string ToLowerAscii(std::string_view s)
	{
		std::string out{ s };
		for (auto& c : out)
			if (c >= 'A' && c <= 'Z')
				c = static_cast<char>(c - 'A' + 'a');
		return out;
	}
}
