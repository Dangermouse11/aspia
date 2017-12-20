//
// PROJECT:         Aspia
// FILE:            system_info/category_open_files.h
// LICENSE:         Mozilla Public License Version 2.0
// PROGRAMMERS:     Dmitry Chapyshev (dmitry@aspia.ru)
//

#ifndef _ASPIA_SYSTEM_INFO__CATEGORY_OPEN_FILES_H
#define _ASPIA_SYSTEM_INFO__CATEGORY_OPEN_FILES_H

#include "system_info/category.h"

namespace aspia {

class CategoryOpenFiles : public CategoryInfo
{
public:
    CategoryOpenFiles();

    const char* Name() const final;
    IconId Icon() const final;

    const char* Guid() const final;
    void Parse(Table& table, const std::string& data) final;
    std::string Serialize() final;

private:
    DISALLOW_COPY_AND_ASSIGN(CategoryOpenFiles);
};

} // namespace aspia

#endif // _ASPIA_SYSTEM_INFO__CATEGORY_OPEN_FILES_H
