#pragma once

#include "DataLoader.hpp"
#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

namespace AirportSearch {

struct AirportMatch {
  uint32_t id;
  std::string iata;
  std::string name;
  std::string city;
  std::string country;
  size_t degree;
  int score;
};

inline std::string to_lower_trimmed(const std::string &s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
    start++;
  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
    end--;
  std::string out;
  out.reserve(end - start);
  for (size_t i = start; i < end; ++i) {
    out.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(s[i]))));
  }
  return out;
}

inline std::string to_upper_trimmed(const std::string &s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
    start++;
  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
    end--;
  std::string out;
  out.reserve(end - start);
  for (size_t i = start; i < end; ++i) {
    out.push_back(
        static_cast<char>(std::toupper(static_cast<unsigned char>(s[i]))));
  }
  return out;
}

// Cleans string to alphanumeric lowercase and folds common Latin-1 UTF-8 accents
inline std::string to_clean_alphanumeric(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    // UTF-8 2-byte Latin-1 supplement folding (à, á, â, ã, ä, é, è, ê, ë, í, ó, ö, ú, ü, ñ, ç, etc.)
    if (c == 0xc3 && i + 1 < s.size()) {
      unsigned char c2 = static_cast<unsigned char>(s[++i]);
      if ((c2 >= 0x80 && c2 <= 0x86) || (c2 >= 0xa0 && c2 <= 0xa6))
        out.push_back('a');
      else if ((c2 >= 0x88 && c2 <= 0x8b) || (c2 >= 0xa8 && c2 <= 0xab))
        out.push_back('e');
      else if ((c2 >= 0x8c && c2 <= 0x8f) || (c2 >= 0xac && c2 <= 0xaf))
        out.push_back('i');
      else if ((c2 >= 0x92 && c2 <= 0x96) || (c2 >= 0xb2 && c2 <= 0xb6))
        out.push_back('o');
      else if ((c2 >= 0x99 && c2 <= 0x9c) || (c2 >= 0xb9 && c2 <= 0xbc))
        out.push_back('u');
      else if (c2 == 0x91 || c2 == 0xb1)
        out.push_back('n');
      else if (c2 == 0x87 || c2 == 0xa7)
        out.push_back('c');
      continue;
    }
    if (std::isalnum(c)) {
      out.push_back(static_cast<char>(std::tolower(c)));
    }
  }
  return out;
}

inline const std::unordered_map<std::string, std::vector<std::string>> &
get_metro_map() {
  static const std::unordered_map<std::string, std::vector<std::string>>
      METRO_MAP = {
          {"newyork", {"JFK", "EWR", "LGA"}},
          {"nyc", {"JFK", "EWR", "LGA"}},
          {"bangkok", {"BKK", "DMK"}},
          {"london", {"LHR", "LGW", "STN", "LTN", "LCY"}},
          {"tokyo", {"NRT", "HND"}},
          {"paris", {"CDG", "ORY", "BVA"}},
          {"chicago", {"ORD", "MDW"}},
          {"washington", {"IAD", "DCA", "BWI"}},
          {"dc", {"IAD", "DCA", "BWI"}},
          {"sanfrancisco", {"SFO", "OAK", "SJC"}},
          {"sf", {"SFO", "OAK", "SJC"}},
          {"bayarea", {"SFO", "OAK", "SJC"}},
          {"losangeles", {"LAX", "BUR", "ONT", "SNA", "LGB"}},
          {"la", {"LAX", "BUR", "ONT", "SNA", "LGB"}},
          {"miami", {"MIA", "FLL"}},
          {"milan", {"MXP", "LIN", "BGY"}},
          {"rome", {"FCO", "CIA"}},
          {"beijing", {"PEK", "PKX"}},
          {"shanghai", {"PVG", "SHA"}},
          {"toronto", {"YYZ", "YTZ"}},
          {"dubai", {"DXB", "DWC"}},
          {"istanbul", {"IST", "SAW"}},
          {"seoul", {"ICN", "GMP"}},
          {"moscow", {"SVO", "DME", "VKO"}},
          {"singapore", {"SIN"}},
          {"hongkong", {"HKG"}},
          {"sydney", {"SYD"}},
          {"melbourne", {"MEL"}},
          {"dhaka", {"DAC"}},
          {"frankfurt", {"FRA"}},
          {"amsterdam", {"AMS"}},
          {"munich", {"MUC"}},
          {"doha", {"DOH"}},
          {"kualalumpur", {"KUL"}},
          {"delhi", {"DEL"}},
          {"mumbai", {"BOM"}},
          {"bangalore", {"BLR"}},
          {"bengaluru", {"BLR"}},
      };
  return METRO_MAP;
}

inline bool is_in_metro(const std::string &clean_query,
                        const std::string &iata_code) {
  const auto &metro_map = get_metro_map();
  auto it = metro_map.find(clean_query);
  if (it != metro_map.end()) {
    for (const auto &code : it->second) {
      if (code == iata_code)
        return true;
    }
  }
  return false;
}

inline bool match_country_alias(const std::string &clean_query,
                                const std::string &clean_country) {
  if (clean_query == "usa" || clean_query == "us" ||
      clean_query == "america" || clean_query == "unitedstates" ||
      clean_query == "unitedstatesofamerica") {
    return clean_country == "unitedstates";
  }
  if (clean_query == "uk" || clean_query == "britain" ||
      clean_query == "greatbritain" || clean_query == "england" ||
      clean_query == "scotland" || clean_query == "wales") {
    return clean_country == "unitedkingdom";
  }
  if (clean_query == "uae" || clean_query == "emirates" ||
      clean_query == "unitedarabemirates") {
    return clean_country == "unitedarabemirates";
  }
  if (clean_query == "russia") {
    return clean_country == "russia" || clean_country == "russianfederation";
  }
  if (clean_query == "korea" || clean_query == "southkorea") {
    return clean_country == "southkorea" ||
           clean_country == "republicofkorea";
  }
  if (clean_query == "deutschland") {
    return clean_country == "germany";
  }
  if (clean_query == "holland" || clean_query == "nederland") {
    return clean_country == "netherlands";
  }
  if (clean_query == "swiss" || clean_query == "schweiz" ||
      clean_query == "suisse") {
    return clean_country == "switzerland";
  }
  if (clean_query == "nz") {
    return clean_country == "newzealand";
  }
  if (clean_query == "espana") {
    return clean_country == "spain";
  }
  if (clean_query == "italia") {
    return clean_country == "italy";
  }
  return clean_country == clean_query;
}

inline std::vector<AirportMatch>
search_airports(const std::string &query, const DataLoader &loader,
                size_t limit = 5) {
  std::string norm_q = to_lower_trimmed(query);
  std::string clean_q = to_clean_alphanumeric(query);

  std::vector<AirportMatch> matches;
  if (norm_q.empty() || clean_q.empty())
    return matches;

  for (size_t i = 0; i < loader.ui_nodes.size(); ++i) {
    const auto &apt = loader.ui_nodes[i];
    std::string iata_lower = to_lower_trimmed(apt.iata_code);
    std::string city_lower = to_lower_trimmed(apt.city);
    std::string city_clean = to_clean_alphanumeric(apt.city);
    std::string name_lower = to_lower_trimmed(apt.name);
    std::string name_clean = to_clean_alphanumeric(apt.name);

    std::string country_name = (apt.country_id < loader.country_pool.size())
                                   ? loader.country_pool[apt.country_id]
                                   : "";
    std::string country_lower = to_lower_trimmed(country_name);
    std::string country_clean = to_clean_alphanumeric(country_name);

    size_t deg = (i < loader.static_route_graph.size())
                     ? loader.static_route_graph[i].size()
                     : 0;

    int score = 0;
    bool is_country_alias = match_country_alias(clean_q, country_clean);

    // 1. Exact IATA code match (e.g. "JFK", "BKK")
    if (norm_q == iata_lower) {
      if (deg > 0) {
        score = 100000;
      } else if (is_country_alias) {
        score = 5000; // De-prioritize zero-route "USA" Concord-Padgett airstrip
      } else {
        score = 70000;
      }
    }
    // 2. Metro Area Match (e.g. "newyork", "nyc" matches JFK, EWR, LGA)
    else if (is_in_metro(clean_q, apt.iata_code)) {
      score = 85000;
    }
    // 3. Exact city match (e.g. "New York", "newyork", "Bangkok", "Tokyo")
    else if (clean_q == city_clean || norm_q == city_lower) {
      score = 80000;
    }
    // 4. Country alias match (e.g. "USA", "UK", "UAE") or exact country name ("Thailand", "Germany")
    else if (is_country_alias || clean_q == country_clean ||
             norm_q == country_lower) {
      score = 75000;
    }
    // 5. IATA prefix match (e.g. "JF", "BK")
    else if (norm_q.size() >= 2 && iata_lower.rfind(norm_q, 0) == 0 && deg > 0) {
      score = 65000;
    }
    // 6. City starts with query (e.g. "bang" -> Bangkok, "new" -> New York/Newark)
    else if (city_clean.rfind(clean_q, 0) == 0 ||
             city_lower.rfind(norm_q, 0) == 0) {
      score = 50000;
    }
    // 7. Country prefix match (e.g. "thai" -> Thailand, "unit" -> United States)
    else if (country_clean.rfind(clean_q, 0) == 0 ||
             country_lower.rfind(norm_q, 0) == 0) {
      score = 40000;
    }
    // 8. Airport name starts with query
    else if (name_clean.rfind(clean_q, 0) == 0 ||
             name_lower.rfind(norm_q, 0) == 0) {
      score = 30000;
    }
    // 9. City contains query as substring
    else if (city_lower.find(norm_q) != std::string::npos ||
             city_clean.find(clean_q) != std::string::npos) {
      score = 20000;
    }
    // 10. Airport name contains query as substring (e.g. "Kennedy", "Don Mueang")
    else if (name_lower.find(norm_q) != std::string::npos ||
             name_clean.find(clean_q) != std::string::npos) {
      score = 10000;
    }
    // 11. Country name contains query as substring
    else if (country_lower.find(norm_q) != std::string::npos ||
             country_clean.find(clean_q) != std::string::npos) {
      score = 5000;
    }

    if (score > 0) {
      if (deg == 0 && score < 70000) {
        score /= 2; // De-prioritize zero-route inactive airstrips
      }
      // Tie-break within category by connectivity / airport hub degree
      score += static_cast<int>(deg);
      matches.push_back({apt.id, apt.iata_code, apt.name, apt.city,
                         country_name, deg, score});
    }
  }

  std::sort(matches.begin(), matches.end(),
            [](const AirportMatch &a, const AirportMatch &b) {
              return a.score > b.score;
            });

  // Filter out low-relevance noise when top match is high relevance (code, metro, city, or country)
  if (!matches.empty() && matches[0].score >= 70000) {
    std::vector<AirportMatch> high_relevance;
    for (const auto &m : matches) {
      if (m.score >= 50000) {
        high_relevance.push_back(m);
      }
    }
    matches = std::move(high_relevance);
  }

  if (matches.size() > limit) {
    matches.resize(limit);
  }
  return matches;
}

inline std::string resolve_airport_code(const std::string &query,
                                       const DataLoader &loader) {
  std::string trimmed = to_lower_trimmed(query);
  if (trimmed.empty())
    return "";

  // If already a 3-letter IATA code with active routes (and not an alias like "usa")
  std::string upper = to_upper_trimmed(query);
  auto it = loader.iata_to_id.find(upper);
  if (it != loader.iata_to_id.end()) {
    uint32_t id = it->second;
    size_t deg = (id < loader.static_route_graph.size())
                     ? loader.static_route_graph[id].size()
                     : 0;
    if (deg > 0 && trimmed != "usa" && trimmed != "us") {
      return upper;
    }
  }

  // Otherwise perform ranked search to resolve to top matching airport hub
  auto matches = search_airports(query, loader, 1);
  if (!matches.empty()) {
    return matches[0].iata;
  }

  // Fallback to exact dictionary lookup if valid ID exists
  if (it != loader.iata_to_id.end()) {
    return upper;
  }

  return "";
}

} // namespace AirportSearch
