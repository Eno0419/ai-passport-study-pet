#ifndef STUDY_BOOK_NVS_H_
#define STUDY_BOOK_NVS_H_

#include "study_logic.h"

#include <cstdint>
#include <vector>

// Compact NVS persistence for the child's wordbook (up to 32 StudyItems + star_count).
// Replaces /sdcard/study_log.txt on SD-less FoloToy AI Passport.
class StudyBookNvs {
public:
    static bool Load(std::vector<StudyItem>& out_items, uint32_t& out_stars);
    static bool Save(const std::vector<StudyItem>& items, uint32_t stars);
};

#endif  // STUDY_BOOK_NVS_H_
