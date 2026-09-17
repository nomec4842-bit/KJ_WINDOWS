add_library(kj_gui STATIC
    mixer.cpp
    gui_main.cpp
    editing_panel.cpp
    eq_graph.cpp
    gui_refresh.cpp
    compressor_window.cpp
    effects_window.cpp
    lfo_window.cpp
    piano_editor.cpp
    mod_matrix_window.cpp
    waveform_window.cpp
    ../../external/wdl/lice/lice.cpp
)

target_include_directories(kj_gui PRIVATE ../../include ../../external)
target_link_libraries(kj_gui
    PUBLIC
        kj_core
    PRIVATE
        opengl32
        gdi32
        user32
        comctl32
)
