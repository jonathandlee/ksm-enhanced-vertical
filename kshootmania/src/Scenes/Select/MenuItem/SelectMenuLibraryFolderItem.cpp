#include "SelectMenuLibraryFolderItem.hpp"

SelectMenuLibraryFolderItem::SelectMenuLibraryFolderItem(IsCurrentFolderYN isCurrentFolder, const Library::Info& library)
	: m_isCurrentFolder(isCurrentFolder), m_library(library), m_specialPath(Library::SpecialPath(library))
{
}

void SelectMenuLibraryFolderItem::decide(const SelectMenuEventContext& context, [[maybe_unused]] int32 difficultyIdx)
{
	if (m_isCurrentFolder)
	{
		context.fnCloseLibrary();
	}
	else
	{
		context.fnOpenLibrary(m_library.id);
	}
}

void SelectMenuLibraryFolderItem::setCanvasParamsCenter([[maybe_unused]] const SelectMenuEventContext& context, noco::Canvas& canvas, [[maybe_unused]] int32 difficultyIdx) const
{
	canvas.setSubCanvasParamValuesByTag(U"center", {
													   { U"isSong", false },
													   { U"isDirectory", true },
													   { U"isSubDirectory", false },
													   { U"isCourse", false },
													   { U"title", FolderDisplayNameCenter(U"[ {} ]"_fmt(m_library.displayName), m_isCurrentFolder) },
												   });
}

void SelectMenuLibraryFolderItem::setCanvasParamsTopBottom([[maybe_unused]] const SelectMenuEventContext& context, noco::Canvas& canvas, [[maybe_unused]] int32 difficultyIdx, StringView tag) const
{
	canvas.setSubCanvasParamValuesByTag(tag, {
												 { U"isSong", false },
												 { U"isDirectory", true },
												 { U"isSubDirectory", false },
												 { U"isCourse", false },
												 { U"title", FolderDisplayNameTopBottom(U"[ {} ]"_fmt(m_library.displayName), m_isCurrentFolder) },
											 });
}

void SelectMenuLibraryFolderItem::showInFileManager([[maybe_unused]] int32 difficultyIdx) const
{
	System::ShowInFileManager(m_library.rootPath);
}
