#pragma once

#include "DataLoader.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

struct FilterSpec {
  int max_stops = -1;             // -1: any, 0: non-stop, 1: <=1 stop, 2: <=2 stops
  int max_price = -1;             // -1: any, otherwise <= max_price in dollars
  int max_duration_mins = -1;     // -1: any, otherwise <= max_duration_mins
  int dep_time_start_min = -1;    // -1: any, e.g. 360 (06:00)
  int dep_time_end_min = -1;      // -1: any, e.g. 720 (12:00)
  std::string dep_time_label;     // e.g. "Morning (06:00-12:00)"
  std::string target_airline_code;// e.g. "EK"
  std::string target_airline_name;// e.g. "Emirates"
  int preference_override = -1;   // -1: default, 0: best, 1: cheapest, 2: fastest

  bool has_any_filter() const {
    return max_stops != -1 || max_price != -1 || max_duration_mins != -1 ||
           dep_time_start_min != -1 || !target_airline_code.empty();
  }
};

class FilterParser {
public:
  static std::string to_lower(const std::string &s) {
    std::string res;
    res.reserve(s.size());
    for (unsigned char c : s) {
      res.push_back(static_cast<char>(std::tolower(c)));
    }
    return res;
  }

  static bool contains_word(const std::string &text, const std::string &word) {
    if (word.empty()) return false;
    size_t pos = 0;
    while ((pos = text.find(word, pos)) != std::string::npos) {
      bool left_ok = (pos == 0 || !std::isalnum(static_cast<unsigned char>(text[pos - 1])));
      bool right_ok = (pos + word.size() >= text.size() || !std::isalnum(static_cast<unsigned char>(text[pos + word.size()])));
      if (left_ok && right_ok) return true;
      pos += word.size();
    }
    return false;
  }

  static FilterSpec parse_typed_filter(
      const std::string &input,
      const std::vector<std::pair<std::string, std::string>> &available_airlines) {
    FilterSpec spec;
    if (input.empty()) return spec;

    std::string lower = to_lower(input);

    // 1. STOPS / TRANSITS
    if (contains_word(lower, "nonstop") || contains_word(lower, "non-stop") ||
        lower.find("non stop") != std::string::npos || contains_word(lower, "direct") ||
        lower.find("0 stop") != std::string::npos || lower.find("0 stops") != std::string::npos ||
        contains_word(lower, "0-stop") || contains_word(lower, "0stop") ||
        lower.find("no stop") != std::string::npos || lower.find("no stops") != std::string::npos ||
        lower.find("zero stop") != std::string::npos) {
      spec.max_stops = 0;
    } else if (lower.find("1 stop") != std::string::npos || lower.find("1-stop") != std::string::npos ||
               lower.find("1stop") != std::string::npos || lower.find("one stop") != std::string::npos ||
               lower.find("single stop") != std::string::npos || lower.find("max 1") != std::string::npos ||
               lower.find("<= 1") != std::string::npos || lower.find("<=1") != std::string::npos ||
               lower.find("< 2 stop") != std::string::npos) {
      spec.max_stops = 1;
    } else if (lower.find("2 stop") != std::string::npos || lower.find("2-stop") != std::string::npos ||
               lower.find("2stops") != std::string::npos || lower.find("two stops") != std::string::npos ||
               lower.find("max 2") != std::string::npos || lower.find("<= 2") != std::string::npos ||
               lower.find("<=2") != std::string::npos) {
      spec.max_stops = 2;
    }

    // 2. DEPARTURE TIME OF DAY
    if (contains_word(lower, "morning") || lower.find("early") != std::string::npos ||
        contains_word(lower, "am")) {
      spec.dep_time_start_min = 360;  // 06:00
      spec.dep_time_end_min = 720;    // 12:00
      spec.dep_time_label = "Morning (06:00-12:00)";
    } else if (contains_word(lower, "afternoon") || contains_word(lower, "midday") ||
               contains_word(lower, "noon")) {
      spec.dep_time_start_min = 720;  // 12:00
      spec.dep_time_end_min = 1080;   // 18:00
      spec.dep_time_label = "Afternoon (12:00-18:00)";
    } else if (contains_word(lower, "evening") || lower.find("after 6") != std::string::npos ||
               contains_word(lower, "pm")) {
      spec.dep_time_start_min = 1080; // 18:00
      spec.dep_time_end_min = 1440;   // 24:00
      spec.dep_time_label = "Evening (18:00-24:00)";
    } else if (contains_word(lower, "night") || lower.find("red eye") != std::string::npos ||
               lower.find("redeye") != std::string::npos || contains_word(lower, "overnight")) {
      spec.dep_time_start_min = 0;    // 00:00
      spec.dep_time_end_min = 360;    // 06:00
      spec.dep_time_label = "Night (00:00-06:00)";
    }

    // 3. AIRLINES MATCHING
    // Pass 1: Match full airline name (prefer longest matching name, e.g. "British Airways")
    std::string best_code;
    std::string best_name;
    size_t max_name_len = 0;

    for (const auto &item : available_airlines) {
      const std::string &code = item.first;
      const std::string &name = item.second;
      if (code.empty() || code == "-" || code == "??" || name.empty()) continue;

      std::string lower_name = to_lower(name);
      if (lower.find(lower_name) != std::string::npos) {
        if (lower_name.size() > max_name_len) {
          max_name_len = lower_name.size();
          best_code = code;
          best_name = name;
        }
      }
    }

    if (!best_code.empty()) {
      spec.target_airline_code = best_code;
      spec.target_airline_name = best_name;
    } else {
      // Pass 2: Match isolated 2-letter / 3-letter IATA airline code (e.g. "BA", "EK", "DL", "QR")
      for (const auto &item : available_airlines) {
        const std::string &code = item.first;
        const std::string &name = item.second;
        if (code.empty() || code == "-" || code == "??" || code.size() < 2) continue;

        std::string lower_code = to_lower(code);
        if (contains_word(lower, lower_code)) {
          spec.target_airline_code = code;
          spec.target_airline_name = name;
          break;
        }
      }

      // Pass 3: Match first word of airline name if >= 4 chars and distinct
      if (spec.target_airline_code.empty()) {
        for (const auto &item : available_airlines) {
          const std::string &code = item.first;
          const std::string &name = item.second;
          if (code.empty() || code == "-" || code == "??" || name.empty()) continue;

          std::string lower_name = to_lower(name);
          size_t space_pos = lower_name.find(' ');
          if (space_pos != std::string::npos && space_pos >= 4) {
            std::string first_word = lower_name.substr(0, space_pos);
            if (first_word == "flight" || first_word == "airline" || first_word == "airways") continue;
            if (contains_word(lower, first_word)) {
              spec.target_airline_code = code;
              spec.target_airline_name = name;
              break;
            }
          }
        }
      }
    }

    // 4. PRICE AND DURATION NUMERICAL EXTRACTION
    for (size_t i = 0; i < lower.size(); ++i) {
      if (std::isdigit(static_cast<unsigned char>(lower[i]))) {
        size_t start = i;
        while (i < lower.size() && std::isdigit(static_cast<unsigned char>(lower[i]))) {
          ++i;
        }
        size_t end = i;
        int num = 0;
        try {
          num = std::stoi(lower.substr(start, end - start));
        } catch (...) {
          continue;
        }

        // Check following characters for duration unit (h, hr, hrs, hours)
        size_t next_non_space = end;
        while (next_non_space < lower.size() && std::isspace(static_cast<unsigned char>(lower[next_non_space]))) {
          ++next_non_space;
        }
        bool is_duration = false;
        if (next_non_space < lower.size()) {
          if (lower[next_non_space] == 'h') {
            is_duration = true;
          } else if (lower.substr(next_non_space, 4) == "hour" || lower.substr(next_non_space, 3) == "hrs") {
            is_duration = true;
          }
        }

        if (is_duration) {
          if (num > 0 && num <= 72) {
            spec.max_duration_mins = num * 60;
          }
          continue;
        }

        // Check preceding tokens for price indicator ($ or "under", "<", "<=", "below", "max", "budget")
        bool is_price = false;
        if (start > 0 && lower[start - 1] == '$') {
          is_price = true;
        } else {
          size_t prev_non_space = (start > 0) ? start - 1 : 0;
          while (prev_non_space > 0 && std::isspace(static_cast<unsigned char>(lower[prev_non_space]))) {
            --prev_non_space;
          }
          if (lower[prev_non_space] == '$' || lower[prev_non_space] == '<') {
            is_price = true;
          } else {
            // Check word before
            size_t word_end = prev_non_space + 1;
            size_t word_start = prev_non_space;
            while (word_start > 0 && std::isalpha(static_cast<unsigned char>(lower[word_start - 1]))) {
              --word_start;
            }
            std::string prev_word = lower.substr(word_start, word_end - word_start);
            if (prev_word == "under" || prev_word == "below" || prev_word == "max" ||
                prev_word == "budget" || prev_word == "cheap" || prev_word == "cheapest") {
              is_price = true;
            }
          }
        }

        // Or if following token is "dollars", "bucks", "$"
        if (next_non_space < lower.size()) {
          if (lower[next_non_space] == '$' || lower.substr(next_non_space, 6) == "dollar" ||
              lower.substr(next_non_space, 4) == "buck") {
            is_price = true;
          }
        }

        // If num is >= 100 and <= 50000, and not already identified as duration
        if (!is_price && num >= 100 && num <= 50000) {
          is_price = true;
        }

        if (is_price && num > 0) {
          spec.max_price = num;
        }
      }
    }

    // 5. PREFERENCE OVERRIDE
    if (contains_word(lower, "cheapest") || contains_word(lower, "cheap") ||
        lower.find("lowest price") != std::string::npos) {
      spec.preference_override = 1;
    } else if (contains_word(lower, "fastest") || contains_word(lower, "quickest") ||
               contains_word(lower, "shortest")) {
      spec.preference_override = 2;
    } else if (contains_word(lower, "best") || contains_word(lower, "recommended")) {
      spec.preference_override = 0;
    }

    return spec;
  }
};
