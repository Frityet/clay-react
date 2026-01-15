local renderer = get_config("clay-backend") or "sdl3"

target("clay_react")
    set_kind("static")
    add_files("src/clay_react/*.c")
    add_cflags("-fblocks")
    add_includedirs("src", {public = true})
    add_includedirs(path.join(os.projectdir(), "reflect/src"), {public = true})
    add_deps("reflect")
    add_packages("clay", {public = true})
    add_links("BlocksRuntime")

    if renderer == "sdl3" then
        add_defines("CLAY_RENDERER_SDL3")
        add_packages("libsdl3", {public = true})
        add_packages("libsdl3_ttf", {public = true})
        add_packages("libsdl3_image", {public = true})
        add_files("src/clay.c")
    elseif renderer == "sdl2" then
        add_defines("CLAY_RENDERER_SDL2")
        add_packages("libsdl2", {public = true})
        add_packages("libsdl2_ttf", {public = true})
        add_packages("libsdl2_image", {public = true})
        add_files("src/clay.c")
    elseif renderer == "cairo" then
        add_defines("CLAY_RENDERER_CAIRO")
        add_packages("cairo", {public = true})
        add_packages("libxcb", {public = true})
        add_packages("xcb-util-keysyms", {public = true})
        -- clay_renderer_cairo.c includes CLAY_IMPLEMENTATION
    elseif renderer == "xcb" then
        add_defines("CLAY_RENDERER_XCB")
        add_packages("libxcb", {public = true})
        add_packages("xcb-util-keysyms", {public = true})
        add_files("src/clay.c")
    elseif renderer == "raylib" then
        add_defines("CLAY_RENDERER_RAYLIB")
        add_packages("raylib", {public = true})
        add_files("src/clay.c")
    elseif renderer == "sokol" then
        add_defines("CLAY_RENDERER_SOKOL")
        add_packages("sokol", {public = true})
        add_files("src/clay.c")
    elseif renderer == "terminal" then
        add_defines("CLAY_RENDERER_TERMINAL")
        add_files("src/clay.c")
    elseif renderer == "web" then
        add_defines("CLAY_RENDERER_WEB")
        add_files("src/clay.c")
    elseif renderer == "win32_gdi" then
        add_defines("CLAY_RENDERER_WIN32_GDI")
        add_files("src/clay.c")
        if is_plat("windows") then
            add_syslinks("gdi32", "user32", "msimg32")
        end
    elseif renderer == "playdate" then
        add_defines("CLAY_RENDERER_PLAYDATE")
        add_files("src/clay.c")
    end
