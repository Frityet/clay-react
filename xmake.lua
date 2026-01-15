add_rules("mode.debug", "mode.release")

set_languages("gnulatest")

add_requires("clay")

set_toolchains("clang")

option("clay-backend")
    set_default("sdl3")
    set_showmenu(true)
    set_values("sdl3", "sdl2", "cairo", "xcb", "raylib", "sokol", "terminal", "web", "win32_gdi", "playdate")
    set_description("Clay renderer backend for Clay React apps")
option_end()

local renderer = get_config("clay-backend") or "sdl3"
if renderer == "sdl3" then
    add_requires("libsdl3", "libsdl3_ttf", "libsdl3_image")
elseif renderer == "sdl2" then
    add_requires("libsdl2", "libsdl2_ttf", "libsdl2_image")
elseif renderer == "cairo" then
    add_requires("cairo", "libxcb", "xcb-util-keysyms")
elseif renderer == "xcb" then
    add_requires("libxcb", "xcb-util-keysyms")
elseif renderer == "raylib" then
    add_requires("raylib")
elseif renderer == "sokol" then
    add_requires("sokol")
end

add_requires("jsmn")

if is_mode("debug") then
    set_policy("build.sanitizer.address", true)
    set_policy("build.sanitizer.undefined", true)
end

add_cflags("-Wall", "-Wextra", "-Werror")
add_cflags("-xobjective-c")
add_cflags("-Wno-unused-function")
add_cflags (
    "-Wanon-enum-enum-conversion",
    "-Wassign-enum",
    "-Wenum-conversion",
    "-Wenum-enum-conversion"
)
add_cflags (
    "-Wnull-dereference",
    "-Wnull-conversion",
    "-Wnullability-completeness",
    "-Wnullable-to-nonnull-conversion",
    "-Wno-missing-field-initializers",
    "-Wno-auto-var-id"
)

local reflect_test_cases = {
    "test_parse_struct",
    "test_parse_struct_no_field",
    "test_get_value",
    "test_type_size_array",
    "test_parse_union",
    "test_padded_struct_layout",
    "test_parse_invalid",
    "test_nested_struct_access",
    "test_value_to_json_struct",
    "test_value_to_json_nested_struct",
    "test_value_to_json_array",
    "test_value_to_json_pointer",
    "test_value_to_json_cstring",
    "test_json_to_value_struct_simple",
    "test_json_to_value_nested_struct",
    "test_json_to_value_array",
    "test_json_to_value_pointer_fields",
    "test_json_to_value_cstring",
    "test_json_to_value_sized_ptr",
    "test_json_to_value_sized_ptr_with_length",
    "test_json_to_value_sized_ptr_mismatch",
    "test_json_to_value_sized_string",
    "test_json_to_value_serialise_as",
    "test_value_to_json_tagged_union",
    "test_json_to_value_tagged_union_ordered",
    "test_json_to_value_tagged_union_reordered",
    "test_json_optional_no_deserialise",
    "test_json_missing_required",
    "test_pointer_type",
    "test_const_qualifier",
    "test_type_hash_equal",
    "test_cast",
    "test_renderdata",
    "test_sized_ptr",
    "test_sized_field",
    "test_serialise_as",
    "test_type_cache",
}

local clay_react_test_cases = {
    "test_state_persistence",
    "test_effects",
    "test_effect_queue_realloc",
    "test_memo",
    "test_callback",
    "test_ref",
    "test_use_id",
    "test_text_input",
    "test_click_handler_props",
    "test_keyed_components",
    "test_context",
    "test_signal",
}

includes("reflect", "clay_react", "todo_app")

-- Reflect tests
target("reflect_tests")
    set_kind("binary")
    set_default(false)
    add_files("tests/reflect_tests.c")
    add_cflags("-Wall", "-Wextra", "-Werror")


    add_undefines("NDEBUG")
    add_includedirs("reflect/src")
    add_deps("reflect")
    add_packages("clay", "libsdl3", "libsdl3_ttf", "libsdl3_image")
    for _, name in ipairs(reflect_test_cases) do
        add_tests(name, {runargs = name})
    end

-- Clay React tests
target("clay_react_tests")
    set_kind("binary")
    set_default(false)
    add_files("tests/clay_react_tests.c")
    add_cflags("-Wno-missing-braces")
    add_cflags("-fblocks")
    add_undefines("NDEBUG")
    add_includedirs("clay_react/src", "reflect/src")
    add_deps("clay_react", "reflect")
    add_packages("clay")
    add_links("BlocksRuntime")
    for _, name in ipairs(clay_react_test_cases) do
        add_tests(name, {runargs = name})
    end
