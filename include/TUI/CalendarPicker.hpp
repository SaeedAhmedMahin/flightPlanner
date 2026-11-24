#pragma once

#include <algorithm>
#include <cstdio>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

namespace FlightPlanner::TUI {

class CalendarPicker {
public:
  static int day_of_week(int y, int m, int d) {
    static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3)
      y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
  }

  static int days_in_month(int y, int m) {
    if (m == 2)
      return ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : 28;
    if (m == 4 || m == 6 || m == 9 || m == 11)
      return 30;
    return 31;
  }

  static std::string format_date_display(int y, int m, int d) {
    static const char *const month_names[] = {
        "",    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const char *const day_names[] = {"Sun", "Mon", "Tue", "Wed",
                                            "Thu", "Fri", "Sat"};
    if (m < 1 || m > 12)
      m = 1;
    int dow = day_of_week(y, m, d);
    std::ostringstream oss;
    oss << day_names[dow] << ", " << month_names[m] << " " << d << ", " << y;
    return oss.str();
  }

  static std::string format_short_date(int y, int m, int d) {
    static const char *const month_names[] = {
        "",    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const char *const day_names[] = {"Sun", "Mon", "Tue", "Wed",
                                            "Thu", "Fri", "Sat"};
    if (m < 1 || m > 12)
      m = 1;
    int dow = day_of_week(y, m, d);
    std::ostringstream oss;
    oss << day_names[dow] << ", " << month_names[m] << " " << d;
    return oss.str();
  }

  static std::string format_iso_to_short(const std::string &iso) {
    int y = 2026, m = 11, d = 15;
    parse_date_iso(iso, y, m, d);
    return format_short_date(y, m, d);
  }

  static std::string format_date_iso(int y, int m, int d) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
    return std::string(buf);
  }

  static void parse_date_iso(const std::string &str, int &y, int &m, int &d) {
    if (std::sscanf(str.c_str(), "%d-%d-%d", &y, &m, &d) != 3) {
      y = 2026;
      m = 11;
      d = 15;
    }
    if (m < 1)
      m = 1;
    if (m > 12)
      m = 12;
    int max_d = days_in_month(y, m);
    if (d < 1)
      d = 1;
    if (d > max_d)
      d = max_d;
  }

  // State
  int viewing_year = 2026;
  int viewing_month = 11;
  int selected_year = 2026;
  int selected_month = 11;
  int selected_day = 15;

  bool has_min_date = false;
  int min_year = 2026;
  int min_month = 10;
  int min_day = 1;
  std::string min_date_iso;

  std::string target_label = "Select Date";
  bool is_open = false;
  std::function<void(const std::string &iso_date)> on_date_selected;
  std::vector<ftxui::Box> interactive_boxes;

  bool is_day_disabled(int y, int m, int d) const {
    if (!has_min_date)
      return false;
    if (y != min_year)
      return y < min_year;
    if (m != min_month)
      return m < min_month;
    return d < min_day;
  }

  bool is_over_interactive(int x, int y) const {
    for (const auto &b : interactive_boxes) {
      if (b.Contain(x, y))
        return true;
    }
    return false;
  }

  // Day buttons
  std::vector<std::string> day_btn_labels;
  ftxui::Components day_buttons;

  // Control buttons
  ftxui::Component prev_month_btn;
  ftxui::Component next_month_btn;
  ftxui::Component prev_year_btn;
  ftxui::Component next_year_btn;
  ftxui::Component today_btn;
  ftxui::Component confirm_btn;
  ftxui::Component close_btn;

  ftxui::Component modal_container;
  ftxui::Component modal_renderer;

  CalendarPicker() {
    using namespace ftxui;

    day_btn_labels.resize(32);
    for (int d = 1; d <= 31; ++d) {
      day_btn_labels[d] = (d < 10 ? " " : "") + std::to_string(d);
      auto btn = Button(
          &day_btn_labels[d],
          [this, d] {
            if (is_day_disabled(viewing_year, viewing_month, d)) {
              return;
            }
            selected_year = viewing_year;
            selected_month = viewing_month;
            selected_day = d;
            // Immediate apply on click for smooth UX
            if (on_date_selected) {
              on_date_selected(
                  format_date_iso(selected_year, selected_month, selected_day));
            }
            is_open = false;
          },
          ButtonOption::Ascii());
      day_buttons.push_back(btn);
    }

    prev_month_btn = Button(
        " ◀ ",
        [this] {
          if (--viewing_month < 1) {
            viewing_month = 12;
            viewing_year--;
          }
        },
        ButtonOption::Ascii());

    next_month_btn = Button(
        " ▶ ",
        [this] {
          if (++viewing_month > 12) {
            viewing_month = 1;
            viewing_year++;
          }
        },
        ButtonOption::Ascii());

    prev_year_btn = Button(
        " ◀ Year ", [this] { viewing_year--; }, ButtonOption::Ascii());

    next_year_btn = Button(
        " Year ▶ ", [this] { viewing_year++; }, ButtonOption::Ascii());

    today_btn = Button(
        " Nov 15, 2026 ",
        [this] {
          int ty = 2026, tm = 11, td = 15;
          if (is_day_disabled(ty, tm, td)) {
            ty = min_year;
            tm = min_month;
            td = min_day;
          }
          viewing_year = selected_year = ty;
          viewing_month = selected_month = tm;
          selected_day = td;
        },
        ButtonOption::Ascii());

    confirm_btn = Button(
        " ✓ Confirm Date ",
        [this] {
          if (is_day_disabled(selected_year, selected_month, selected_day)) {
            selected_year = min_year;
            selected_month = min_month;
            selected_day = min_day;
          }
          if (on_date_selected) {
            on_date_selected(
                format_date_iso(selected_year, selected_month, selected_day));
          }
          is_open = false;
        },
        ButtonOption::Ascii());

    close_btn = Button(
        " ✕ Cancel ", [this] { is_open = false; }, ButtonOption::Ascii());

    // Assemble components into interactive container
    Components all_components;
    all_components.push_back(prev_month_btn);
    all_components.push_back(next_month_btn);
    all_components.push_back(prev_year_btn);
    all_components.push_back(next_year_btn);
    all_components.push_back(today_btn);
    all_components.push_back(confirm_btn);
    all_components.push_back(close_btn);
    for (auto &b : day_buttons) {
      all_components.push_back(b);
    }

    modal_container = Container::Vertical(all_components);

    modal_renderer = Renderer(modal_container, [this] {
      return render_calendar_view();
    });
  }

  void open(const std::string &initial_date_iso, const std::string &label,
            std::function<void(const std::string &)> callback,
            const std::string &min_iso = "") {
    target_label = label;
    on_date_selected = std::move(callback);
    min_date_iso = min_iso;
    has_min_date = !min_iso.empty();
    if (has_min_date) {
      parse_date_iso(min_iso, min_year, min_month, min_day);
    }

    parse_date_iso(initial_date_iso, selected_year, selected_month, selected_day);
    if (is_day_disabled(selected_year, selected_month, selected_day)) {
      selected_year = min_year;
      selected_month = min_month;
      selected_day = min_day;
    }
    viewing_year = selected_year;
    viewing_month = selected_month;
    is_open = true;
  }

  ftxui::Element render_calendar_view() {
    using namespace ftxui;

    static const char *const month_names[] = {
        "",     "January", "February",  "March",   "April",    "May",     "June",
        "July", "August",  "September", "October", "November", "December"};

    if (viewing_month < 1)
      viewing_month = 1;
    if (viewing_month > 12)
      viewing_month = 12;

    interactive_boxes.clear();
    interactive_boxes.resize(40);
    int box_idx = 0;

    // Header bar with Month and Year navigation
    auto header = hbox({
        prev_month_btn->Render() | color(Color::CyanLight) |
            reflect(interactive_boxes[box_idx++]),
        filler(),
        text(std::string(month_names[viewing_month]) + " " +
             std::to_string(viewing_year)) |
            bold | color(Color::YellowLight),
        filler(),
        next_month_btn->Render() | color(Color::CyanLight) |
            reflect(interactive_boxes[box_idx++]),
    });

    // Day of week column headers
    auto dow_header = hbox({
        text(" Sun ") | dim | color(Color::White),
        text(" Mon ") | dim | color(Color::White),
        text(" Tue ") | dim | color(Color::White),
        text(" Wed ") | dim | color(Color::White),
        text(" Thu ") | dim | color(Color::White),
        text(" Fri ") | dim | color(Color::White),
        text(" Sat ") | dim | color(Color::White),
    });

    // 7-column calendar day grid
    std::vector<Element> week_rows;
    int cur_day = 1;
    int t_days = days_in_month(viewing_year, viewing_month);
    int f_dow = day_of_week(viewing_year, viewing_month, 1);

    for (int w = 0; w < 6 && cur_day <= t_days; ++w) {
      std::vector<Element> row_items;
      for (int col = 0; col < 7; ++col) {
        if ((w == 0 && col < f_dow) || cur_day > t_days) {
          row_items.push_back(text("     ") | size(WIDTH, EQUAL, 5));
        } else {
          int d = cur_day++;
          bool disabled = is_day_disabled(viewing_year, viewing_month, d);
          if (disabled) {
            std::string lbl = " " + std::string(d < 10 ? " " : "") +
                              std::to_string(d) + " ";
            row_items.push_back(text(lbl) | dim | color(Color::GrayDark) |
                                size(WIDTH, EQUAL, 5));
          } else {
            bool is_sel =
                (d == selected_day && viewing_month == selected_month &&
                 viewing_year == selected_year);

            Element cell = day_buttons[d - 1]->Render() | size(WIDTH, EQUAL, 5);
            if (is_sel) {
              cell = cell | bgcolor(Color::RGB(15, 80, 130)) | bold |
                     color(Color::YellowLight);
            }
            if (box_idx < static_cast<int>(interactive_boxes.size())) {
              cell = cell | reflect(interactive_boxes[box_idx++]);
            }
            row_items.push_back(cell);
          }
        }
      }
      week_rows.push_back(hbox(std::move(row_items)));
    }

    std::string sel_display = format_date_display(
        selected_year, selected_month, selected_day);
    std::string sel_iso = format_date_iso(
        selected_year, selected_month, selected_day);

    auto year_nav_row = hbox({
        prev_year_btn->Render() | reflect(interactive_boxes[box_idx++]),
        filler(),
        today_btn->Render() | color(Color::CyanLight) |
            reflect(interactive_boxes[box_idx++]),
        filler(),
        next_year_btn->Render() | reflect(interactive_boxes[box_idx++]),
    });

    auto action_row = hbox({
        confirm_btn->Render() | bold | color(Color::GreenLight) |
            reflect(interactive_boxes[box_idx++]),
        text("  "),
        close_btn->Render() | color(Color::RedLight) |
            reflect(interactive_boxes[box_idx++]),
    });

    std::string right_hint =
        has_min_date ? (" Min: " + min_date_iso + " ") : " Select Date ";

    return window(
        hbox({
            text(" 📅 " + target_label + " ") | bold |
                color(Color::CyanLight),
            filler(),
            text(right_hint) | dim,
        }),
        vbox({
            header,
            separatorLight(),
            dow_header,
            separatorLight(),
            vbox(std::move(week_rows)),
            separatorLight(),
            hbox({
                text(" Selected: ") | dim,
                text(sel_display) | bold | color(Color::GreenLight),
                text(" (" + sel_iso + ")") | dim,
            }) | hcenter,
            separatorLight(),
            year_nav_row,
            separatorLight(),
            action_row | hcenter,
        })) |
        size(WIDTH, EQUAL, 46) | color(Color::Cyan);
  }

  ftxui::Component GetComponent() { return modal_renderer; }
};

} // namespace FlightPlanner::TUI
