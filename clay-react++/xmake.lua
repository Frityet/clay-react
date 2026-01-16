local renderer = get_config("clay-backend") or "sdl3"

target("clay-react++")
    set_kind("static")
    set_languages("cxx23")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_packages("clay", {public = true})

    if renderer == "sdl3" then
        add_defines("CLAY_RENDERER_SDL3")
        add_packages("libsdl3", {public = true})
        add_packages("libsdl3_ttf", {public = true})
        add_packages("libsdl3_image", {public = true})
        add_files("src/backends/sdl3_renderer.c")
    else
        add_defines("CLAY_RENDERER_UNSUPPORTED")
    end
