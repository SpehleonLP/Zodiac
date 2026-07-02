#ifndef HAVE_ZODIAC
#  error "HAVE_ZODIAC not defined — Zodiac compiles to nothing; the whole suite would pass vacuously."
#endif
#include <gtest/gtest.h>
TEST(Canary, Builds){ SUCCEED(); }
