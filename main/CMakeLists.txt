# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES )

set(COMPONENT_SRCS "main.c prelude.c app/app.c network/wifi.c network/server.c network/tasks.c camera/camera.c")
set(COMPONENT_ADD_INCLUDEDIRS ".")

register_component()
