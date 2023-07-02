add_qtc_plugin(TabbedEditor
  PLUGIN_DEPENDS Core TextEditor OpenMV
  SOURCES
    plugin.cpp plugin.h
    tabbar.cpp tabbar.h
    resources/res.qrc
    constants.h tabbededitortr.h
)
