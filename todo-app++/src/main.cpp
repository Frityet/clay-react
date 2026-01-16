#include "clay-react++/clay_reactpp.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <expected>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace crpp = clay::reactpp;

struct Theme {
    crpp::Color background;
    crpp::Color surface;
    crpp::Color surface_alt;
    crpp::Color text;
    crpp::Color text_muted;
    crpp::Color accent;
    crpp::Color accent_soft;
    crpp::Color danger;
    crpp::Color success;
    crpp::Color warning;
};

constexpr Theme k_theme = {
    .background = crpp::Color::rgb(244, 246, 249),
    .surface = crpp::Color::rgb(255, 255, 255),
    .surface_alt = crpp::Color::rgb(247, 249, 252),
    .text = crpp::Color::rgb(22, 27, 36),
    .text_muted = crpp::Color::rgb(98, 110, 125),
    .accent = crpp::Color::rgb(59, 130, 246),
    .accent_soft = crpp::Color::rgb(219, 234, 254),
    .danger = crpp::Color::rgb(239, 68, 68),
    .success = crpp::Color::rgb(34, 197, 94),
    .warning = crpp::Color::rgb(234, 179, 8),
};

enum class Filter { all, active, done };
enum class Priority { low, medium, high };

struct TodoItem {
    int id = 0;
    std::string title;
    bool done = false;
    Priority priority = Priority::medium;
};

struct ValidationError {
    std::string message;
};

std::string trim_copy(std::string_view input) {
    auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    auto begin = std::find_if_not(input.begin(), input.end(),
        [&](char ch) { return is_space(static_cast<unsigned char>(ch)); });
    auto end = std::find_if_not(input.rbegin(), input.rend(),
        [&](char ch) { return is_space(static_cast<unsigned char>(ch)); }).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

std::expected<std::string, ValidationError> normalize_title(std::string_view input) {
    std::string trimmed = trim_copy(input);
    if (trimmed.empty()) {
        return std::unexpected(ValidationError{ "Add a task title first." });
    }
    if (trimmed.size() > 64) {
        trimmed.resize(64);
    }
    return trimmed;
}

struct TodoApp {
    std::vector<TodoItem> items;
    int next_id = 1;
    Filter filter = Filter::all;
    crpp::TextInput draft;
    std::string error;
    bool seeded = false;

    void seed() {
        if (seeded) {
            return;
        }
        seeded = true;
        items.push_back({ next_id++, "Plan weekly sprint", false, Priority::high });
        items.push_back({ next_id++, "Call the dentist", true, Priority::low });
        items.push_back({ next_id++, "Finish UI prototype", false, Priority::medium });
    }

    void add_item(std::string title) {
        items.push_back({ next_id++, std::move(title), false, Priority::medium });
    }

    void render(crpp::UI &ui) {
        seed();

        const int done_count = std::ranges::count_if(items, [](const TodoItem &item) { return item.done; });
        const int total_count = static_cast<int>(items.size());
        const int active_count = total_count - done_count;

        crpp::TextStyle title_style;
        title_style.font_size = 32;
        title_style.line_height = 40;
        title_style.color = k_theme.text;

        crpp::TextStyle body_style;
        body_style.font_size = 16;
        body_style.line_height = 24;
        body_style.color = k_theme.text;

        crpp::TextStyle muted_style = body_style;
        muted_style.font_size = 14;
        muted_style.line_height = 20;
        muted_style.color = k_theme.text_muted;

        crpp::TextStyle error_style = muted_style;
        error_style.color = k_theme.danger;

        crpp::BoxStyle page;
        page.layout = crpp::Layout::column(16);
        page.layout.padding(crpp::Padding::all(28));
        page.layout.sizing(crpp::Sizing::fill());

        crpp::BoxStyle header;
        header.layout = crpp::Layout::column(6);
        header.layout.sizing(crpp::Sizing::fit(0.0f, 0.0f, 0.0f, 0.0f));

        crpp::BoxStyle input_row;
        input_row.layout = crpp::Layout::row(12);
        input_row.layout.sizing(crpp::Sizing::fill());
        input_row.layout.align(CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER);

        crpp::TextInputStyle input_style;
        input_style.placeholder_text = "Add a task...";
        input_style.box.layout = crpp::Layout::row();
        input_style.box.layout.sizing(crpp::Sizing::fill());
        input_style.box.layout.padding(crpp::Padding::xy(14, 10));
        input_style.box.background = k_theme.surface;
        input_style.box.border = crpp::Border::outside(1, k_theme.surface_alt);
        input_style.box.corner_radius = crpp::CornerRadius::all(12.0f);
        input_style.text = body_style;
        input_style.placeholder = muted_style;

        crpp::ButtonStyle add_button;
        add_button.box.layout = crpp::Layout::row();
        add_button.box.layout.padding(crpp::Padding::xy(16, 10));
        add_button.box.layout.align(CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER);
        add_button.box.background = k_theme.accent;
        add_button.box.hover_background = k_theme.accent_soft;
        add_button.box.corner_radius = crpp::CornerRadius::all(12.0f);
        add_button.text = body_style;
        add_button.text.color = crpp::Color::rgb(255, 255, 255);

        crpp::BoxStyle filters_row;
        filters_row.layout = crpp::Layout::row(8);
        filters_row.layout.align(CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER);

        crpp::ButtonStyle filter_button;
        filter_button.box.layout = crpp::Layout::row();
        filter_button.box.layout.padding(crpp::Padding::xy(12, 6));
        filter_button.box.layout.align(CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER);
        filter_button.box.corner_radius = crpp::CornerRadius::all(999.0f);
        filter_button.text = muted_style;

        crpp::BoxStyle list_style;
        list_style.layout = crpp::Layout::column(10);
        list_style.layout.sizing(crpp::Sizing::fill());

        crpp::BoxStyle card;
        card.layout = crpp::Layout::column(12);
        card.layout.padding(crpp::Padding::all(18));
        card.layout.sizing(crpp::Sizing::fill());
        card.background = k_theme.surface;
        card.corner_radius = crpp::CornerRadius::all(16.0f);

        ui.column(page, [&] {
            ui.column(header, [&] {
                ui.text("Todo App++", title_style);
                std::string stats = std::to_string(active_count) + " active / " +
                    std::to_string(done_count) + " done";
                ui.text(stats, muted_style);
            });

            ui.row(input_row, [&] {
                crpp::TextInputResult input_result = ui.text_input({ "todo-input", std::nullopt }, draft, input_style);
                bool wants_add = ui.button({ "todo-add", std::nullopt }, "Add", add_button) || input_result.submitted;
                if (wants_add) {
                    auto normalized = normalize_title(draft.text);
                    if (normalized) {
                        add_item(std::move(*normalized));
                        draft.text.clear();
                        error.clear();
                    } else {
                        error = normalized.error().message;
                    }
                }
            });

            if (!error.empty()) {
                ui.text(error, error_style);
            }

            ui.row(filters_row, [&] {
                constexpr std::array<std::pair<Filter, std::string_view>, 3> filters = {
                    std::pair{ Filter::all, "All" },
                    std::pair{ Filter::active, "Active" },
                    std::pair{ Filter::done, "Done" },
                };

                for (std::size_t i = 0; i < filters.size(); ++i) {
                    auto [mode, label] = filters[i];
                    crpp::ButtonStyle button_style = filter_button;
                    if (filter == mode) {
                        button_style.box.background = k_theme.accent_soft;
                        button_style.text.color = k_theme.accent;
                    } else {
                        button_style.box.background = crpp::Color::rgba(0, 0, 0, 0);
                        button_style.text.color = k_theme.text_muted;
                    }
                    if (ui.button({ "todo-filter", static_cast<std::uint32_t>(i) }, label, button_style)) {
                        filter = mode;
                    }
                }
            });

            ui.column(card, [&] {
                auto matches_filter = [&](const TodoItem &item) {
                    switch (filter) {
                        case Filter::all:
                            return true;
                        case Filter::active:
                            return !item.done;
                        case Filter::done:
                            return item.done;
                    }
                    return true;
                };

                std::optional<int> remove_id;

                for (TodoItem &item : items | std::views::filter(matches_filter)) {
                    crpp::BoxStyle row_style;
                    row_style.layout = crpp::Layout::row(12);
                    row_style.layout.align(CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER);
                    row_style.layout.sizing(crpp::Sizing::fill());
                    row_style.background = item.done ? k_theme.surface_alt : k_theme.surface;
                    row_style.corner_radius = crpp::CornerRadius::all(12.0f);
                    row_style.layout.padding(crpp::Padding::xy(12, 10));

                    crpp::ButtonStyle toggle;
                    toggle.box.layout = crpp::Layout::row();
                    toggle.box.layout.sizing(crpp::Sizing::fixed(22.0f, 22.0f));
                    toggle.box.layout.align(CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER);
                    toggle.box.corner_radius = crpp::CornerRadius::all(6.0f);
                    toggle.box.background = item.done ? k_theme.success : k_theme.surface_alt;
                    toggle.box.border = crpp::Border::outside(1, k_theme.surface_alt);
                    toggle.text = body_style;
                    toggle.text.font_size = 14;
                    toggle.text.line_height = 16;
                    toggle.text.color = item.done ? crpp::Color::rgb(255, 255, 255) : k_theme.text_muted;

                    crpp::ButtonStyle remove_button = toggle;
                    remove_button.box.background = k_theme.danger;
                    remove_button.box.hover_background = k_theme.warning;
                    remove_button.text.color = crpp::Color::rgb(255, 255, 255);
                    remove_button.text.font_size = 12;

                    ui.row(row_style, [&] {
                        if (ui.button({ "todo-toggle", static_cast<std::uint32_t>(item.id) },
                                item.done ? "x" : " ", toggle)) {
                            item.done = !item.done;
                        }

                        crpp::BoxStyle text_slot;
                        text_slot.layout = crpp::Layout::row();
                        text_slot.layout.sizing(crpp::Sizing::fill());
                        ui.box(text_slot, [&] {
                            crpp::TextStyle item_text = body_style;
                            if (item.done) {
                                item_text.color = k_theme.text_muted;
                            }
                            ui.text(item.title, item_text);
                        });

                        if (ui.button({ "todo-remove", static_cast<std::uint32_t>(item.id) },
                                "X", remove_button)) {
                            remove_id = item.id;
                        }
                    });
                }

                if (remove_id) {
                    std::erase_if(items, [&](const TodoItem &item) { return item.id == *remove_id; });
                }

                if (items.empty()) {
                    ui.text("Add your first task to get started.", muted_style);
                }
            });
        });
    }
};

int main() {
    TodoApp app;
    return crpp::run_app({
        .title = "Todo App++ - clay-react++",
        .width = 1024,
        .height = 768,
        .font_path = "resources/Roboto-Regular.ttf",
        .font_size = 22,
        .render = [&app](crpp::UI &ui) { app.render(ui); },
        .background = [] { return k_theme.background; },
    });
}
