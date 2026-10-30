#pragma once
#include "ISelectMenuItem.hpp"
#include "Common/Library.hpp"

// A song library (K-Shoot MANIA / osu! / Etterna / StepMania) shown above the package folders.
// Its fullPath is the special path ">id".
class SelectMenuLibraryFolderItem : public ISelectMenuItem
{
private:
	const IsCurrentFolderYN m_isCurrentFolder;
	const Library::Info m_library;
	const FilePath m_specialPath;

public:
	SelectMenuLibraryFolderItem(IsCurrentFolderYN isCurrentFolder, const Library::Info& library);

	virtual ~SelectMenuLibraryFolderItem() = default;

	virtual void decide(const SelectMenuEventContext& context, int32 difficultyIdx) override;

	virtual bool isFolder() const override
	{
		return true;
	}

	virtual FilePathView fullPath() const override
	{
		return m_specialPath;
	}

	virtual void setCanvasParamsCenter(const SelectMenuEventContext& context, noco::Canvas& canvas, int32 difficultyIdx) const override;

	virtual void setCanvasParamsTopBottom(const SelectMenuEventContext& context, noco::Canvas& canvas, int32 difficultyIdx, StringView tag) const override;

	virtual void showInFileManager(int32 difficultyIdx) const override;
};
