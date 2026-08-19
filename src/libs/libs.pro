message("Entering libs.pro")
TEMPLATE = subdirs
SUBDIRS = \
    qmuparser \
    vpropertyexplorer \
    ifc \
    vobj \
    vdxf \
    vlayout \
    vgeometry \
    vpatterndb \
    vmisc \
    vwidgets \
    vtools \
    vformat \
    fervor \
    vtest \
    tools \
    actionlayer

actionlayer.depends = vpatterndb vgeometry ifc vmisc
