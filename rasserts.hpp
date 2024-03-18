#pragma once

#define rassert(condition, info) if (!(condition)) { throw std::runtime_error("Assertion failed!\n" "Add info: " info); }