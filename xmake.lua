add_rules("mode.debug", "mode.release")

set_languages("gnulatest")

add_requires("clay", "libsdl3", "libsdl3_ttf", "libsdl3_image")

if is_mode("debug") then
    set_policy("build.sanitizer.address", true)
    set_policy("build.sanitizer.undefined", true)
    -- set_policy("build.sanitizer.leak", true)
end

set_toolchains("clang")

-- Reflect library (static)
target("reflect")
    set_kind("static")
    add_files("src/reflect/*.c")
    add_cflags("-Wall", "-Wextra", "-Werror")
    add_cflags("-Wno-unused-function")
    add_cflags("-xobjective-c") -- for @encode
    add_includedirs("src/reflect", {public = true})

-- Clay React library (static)
target("clay_react")
    set_kind("static")
    add_files("src/clay_react/*.c")
    add_cflags("-Wall", "-Wextra", "-Werror", "-Wno-unused-function")
    add_cflags("-xobjective-c") -- for @encode
    add_cflags("-fblocks") -- for Clang blocks
    add_includedirs("src", "src/clay_react", {public = true})
    add_deps("reflect")
    add_packages("clay")
    add_links("BlocksRuntime")

-- SDL3 Renderer library (static)
target("renderer")
    set_kind("static")
    add_files("src/renderer/*.c")
    add_cflags("-Wall", "-Wextra", "-Werror", "-Wno-unused-function")
    add_includedirs("src/renderer", {public = true})
    add_packages("clay", "libsdl3", "libsdl3_ttf", "libsdl3_image")

-- Main application (Clay React demo)
target("myapp")
    set_kind("binary")
    add_files("src/main.c", "src/clay.c")
    add_cflags("-Wall", "-Wextra", "-Werror")
    add_cflags("-Wno-unused-function", "-Wno-missing-braces")
    add_cflags("-xobjective-c") -- for @encode
    add_cflags("-fblocks") -- for Clang blocks

    add_includedirs("src")
    add_deps("reflect", "clay_react", "renderer")
    add_packages("clay", "libsdl3", "libsdl3_ttf", "libsdl3_image")
    add_links("BlocksRuntime")

-- Reflect tests
target("reflect_tests")
    set_kind("binary")
    add_files("tests/reflect_tests.c")
    add_cflags("-Wall", "-Wextra", "-Werror")
    add_cflags("-Wno-unused-function")
    add_cflags("-xobjective-c") -- for @encode
    add_undefines("NDEBUG")
    add_includedirs("src/reflect")
    add_deps("reflect")
    add_packages("clay", "libsdl3", "libsdl3_ttf", "libsdl3_image")
