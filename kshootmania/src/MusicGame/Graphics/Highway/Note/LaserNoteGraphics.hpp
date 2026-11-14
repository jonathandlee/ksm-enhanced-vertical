#pragma once
#include "MusicGame/GameStatus.hpp"
#include "MusicGame/PlayOption.hpp"
#include "MusicGame/Scroll/HighwayScroll.hpp"
#include "MusicGame/Graphics/Highway/HighwayRenderTexture.hpp"

namespace MusicGame::Graphics
{
	class LaserNoteGraphics
	{
	private:
		/// @brief 描画対象のLASERセクション
		struct VisibleLaserSection
		{
			/// @brief セクション開始位置のPulse値
			kson::Pulse y;

			/// @brief セクションへのポインタ
			const kson::LaserSection* pLaserSection;

			/// @brief 判定状況に応じたテクスチャの行
			int32 textureRow;

			/// @brief Index of this section's first point in m_pointPositionsY
			std::size_t pointPositionsOffset;
		};

		const Texture m_laserNoteTexture;
		const Texture m_laserNoteMaskTexture;
		const std::array<TiledTexture, kson::kNumLaserLanesSZ> m_laserNoteStartTextures;

		/// @brief 描画対象のセクションを溜めるバッファ(通常テクスチャとマスクテクスチャの描画で共用)
		mutable Array<VisibleLaserSection> m_visibleLaserSections;

		/// @brief Y position (with the laser shift) of every point of the visible sections, computed once per
		///        frame and shared by both texture passes and the slam look-ahead loops
		mutable Array<int32> m_pointPositionsY;

	public:
		LaserNoteGraphics();

		void draw(const kson::ChartData& chartData, const std::array<HashSet<kson::Pulse>, kson::kNumLaserLanesSZ>& laserCurvedPulses, const PlayOption& playOption, const GameStatus& gameStatus, const Scroll::HighwayScrollContext& highwayScrollContext, const HighwayRenderTexture& target) const;
	};
}
